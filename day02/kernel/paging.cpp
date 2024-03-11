#include <cstdint>
#include <array>
#include <cstring>
#include "asmfunc.h"
#include "memory_manager.hpp"
#include "paging.hpp"
#include "error.hpp"

namespace {
	constexpr uint64_t PAGE_SIZE_4K = 4096;
	constexpr uint64_t PAGE_SIZE_2M = 512 * PAGE_SIZE_4K;
	constexpr uint64_t PAGE_SIZE_1G = 512 * PAGE_SIZE_2M;
	constexpr uint64_t PAGE_DIR_COUNT = 64;
	
	using PageTable = std::array<uint64_t, 512>;

	alignas(PAGE_SIZE_4K) std::array<uint64_t, 512> pml4_table;
	alignas(PAGE_SIZE_4K) std::array<uint64_t, 512> pdp_table;
	alignas(PAGE_SIZE_4K) std::array<PageTable, PAGE_DIR_COUNT> page_dir;
}

void InitializePaging() {
	SetupIdentityPageTable();
}

void ResetCR3() {
	SetCR3(reinterpret_cast<uint64_t>(&pml4_table[0]));
}

void SetupIdentityPageTable() {
	pml4_table[0] = reinterpret_cast<uint64_t>(&pdp_table[0]) | 0x003;
	for (int i = 0; i < page_dir.size(); i++) {
		pdp_table[i] = reinterpret_cast<uint64_t>(&page_dir[i]) | 0x003;
		auto& page_table = page_dir[i];
		for (int j = 0; j < 512; j++) {
			page_table[j] = (i * PAGE_SIZE_1G + j * PAGE_SIZE_2M) | 0x083;
		}
	}

	ResetCR3();
	SetCR0(GetCR0() & 0xfffeffff); // allow non-restricted writing for cpl < 3 (superviser mode)
}

WithError<PageMapEntry*> NewPageMap() {
	auto res = memory_manager->Allocate(1);
	if (res.has_value) {
		auto entry = reinterpret_cast<PageMapEntry*>(res.value.Frame());
		memset(entry, 0, sizeof(uint64_t) * 512);
		return { entry, MakeError(Error::kSuccess) };
	}
	else {
		return { nullptr, MakeError(Error::kNoEnoughMemory) };
	}
}

WithError<PageMapEntry*> SetNewPageMapIfNotPresent(PageMapEntry* entry) {
	if (entry->bits.present) return { entry->ptr(), Error::kSuccess };

	auto [child_map, err] = NewPageMap();
	if (err) {
		return { nullptr, err };
	}
	
	entry->SetPtr(child_map);
	entry->bits.present = 1;
	return { entry->ptr(), Error::kSuccess };
}

WithError<size_t> SetupPageMap(PageMapEntry* page_map, int page_map_level, LinearAddress4Level addr, size_t num_4kpages, bool writeable) {
	while (num_4kpages > 0) {
		const auto entry_index = addr.get(page_map_level);
		auto [child_map, err] = SetNewPageMapIfNotPresent(&page_map[entry_index]);
		if (err) {
			return { num_4kpages, err };
		}
		page_map[entry_index].bits.user = 1;

		if (page_map_level == 1) {
			page_map[entry_index].bits.writeable = writeable; // do copy on write
			--num_4kpages;
		}
		else {
			page_map[entry_index].bits.writeable = 1;
			auto [num_remain_pages, err] = SetupPageMap(child_map, page_map_level - 1, addr, num_4kpages, writeable);
			if (err) {
				return { num_4kpages, err };
			}
			num_4kpages = num_remain_pages;
		}

		if (entry_index == 511) {
			break;
		}

		addr.set(page_map_level, entry_index + 1);
		for (int level = page_map_level - 1; level >= 1; --level) {
			addr.set(level, 0);
		}
	}
	return { num_4kpages, MakeError(Error::kSuccess) };
}

Error SetupPageMaps(LinearAddress4Level addr, size_t num_4kpages, bool writeable) {
	auto pml4_table = reinterpret_cast<PageMapEntry*>(GetCR3());
	return SetupPageMap(pml4_table, 4, addr, num_4kpages, writeable).error;
}

Error CleanPageMap(PageMapEntry* page_map, int page_map_level) {
	for (int i = 0; i < 512; i++) {
		auto entry = page_map[i];
		if (!entry.bits.present) {
			continue;
		}

		if (page_map_level > 1) {
			if (auto err = CleanPageMap(entry.ptr(), page_map_level - 1)) {
				return err;
			}
		}

		if (entry.bits.writeable) { // only free copied pages
			const auto entry_addr = reinterpret_cast<uintptr_t>(entry.ptr());
			const FrameID entry_frame{ entry_addr / BytesPerFrame };
			if (auto err = memory_manager->Free(entry_frame, 1)) {
				return err;
			}
		}
		page_map[i].data = 0;
	}
	return MakeError(Error::kSuccess);
}

Error CleanPageMaps(LinearAddress4Level addr) {
	auto pml4_table = reinterpret_cast<PageMapEntry*>(GetCR3());
	auto pdp_table = pml4_table[addr.bits.PML4].ptr();
	pml4_table[addr.bits.PML4].data = 0;

	if (auto err = CleanPageMap(pdp_table, 3)) {
		return err;
	}

	const auto pdp_addr = reinterpret_cast<uintptr_t>(pdp_table);
	const FrameID pdp_frame{ pdp_addr / BytesPerFrame };
	return memory_manager->Free(pdp_frame, 1);
}

Error CleanTempPML4(uint64_t pml4, int start) {
	auto pml4_table = reinterpret_cast<PageMapEntry*>(pml4);
	for (int i = start; i < 512; i++) {
		auto pdp_table = pml4_table[i].ptr();
		pml4_table[i].data = 0;
		if (auto err = CleanPageMap(pdp_table, 3)) {
			return err;
		}

		const auto pdp_addr = reinterpret_cast<uint64_t>(pdp_table);
		const FrameID pdp_frame{ pdp_addr / BytesPerFrame };
		if (auto err = memory_manager->Free(pdp_frame, 1)) {
			return err;
		}
	}

	const FrameID pml4_frame{ pml4 / BytesPerFrame };
	return memory_manager->Free(pml4_frame, 1);
}

WithError<PageMapEntry*> SetupPML4(Task& cur_task) {
	auto pml4 = NewPageMap();
	if (pml4.error) {
		return pml4;
	}

	const auto cur_pml4 = reinterpret_cast<PageMapEntry*>(GetCR3());
	memcpy(pml4.value, cur_pml4, 256 * sizeof(uint64_t)); // copy kernel space to new pml4

	const auto cr3 = reinterpret_cast<uint64_t>(pml4.value);
	cur_task.Context().cr3 = cr3;
	SetCR3(cr3);
	
	return pml4;
}

Error FreePML4(Task& cur_task) {
	const auto cr3 = cur_task.Context().cr3;
	cur_task.Context().cr3 = 0; // wtf?
	ResetCR3();

	const FrameID frame{cr3 / BytesPerFrame};
	return memory_manager->Free(frame, 1);
}

const FileMapping* FindFileMapping(const std::vector<FileMapping>& fmaps, uint64_t vaddr) {
	auto it = std::find_if(fmaps.begin(), fmaps.end(), [vaddr](const FileMapping& x) {
		return x.vaddr_begin <= vaddr && vaddr < x.vaddr_end;
	});
	return (it != fmaps.end()) ? &(*it) : nullptr;
}

Error PreparePageCache(FileDescriptor& fd, const FileMapping& m, uint64_t vaddr) {
	LinearAddress4Level page_vaddr {vaddr};
	if (auto err = SetupPageMaps(page_vaddr, 1, true)) {
		return err;
	}
	
	const long file_offset = page_vaddr.value - m.vaddr_begin;
	void* page_cache = reinterpret_cast<void*>(page_vaddr.value);
	fd.Load(page_cache, 0x1000, file_offset);
	return MakeError(Error::kSuccess);
}

Error SetPageContent(PageMapEntry* table, int part, LinearAddress4Level addr, PageMapEntry* content) {
	if (part == 1) {
		const auto i = addr.get(1);
		table[i].SetPtr(content);
		table[i].bits.writeable = 1;
		InvalidateTLB(addr.value);
		return MakeError(Error::kSuccess);
	}

	const auto i = addr.get(part);
	return SetPageContent(table[i].ptr(), part-1, addr, content);
}

Error CopyOnePage(uint64_t vaddr) {
	auto [ p, err ] = NewPageMap();
	if (err) {
		return err;
	}

	const auto aligned_addr = vaddr & ~static_cast<uint64_t>(0xfff);
	memcpy(p, reinterpret_cast<const void*>(aligned_addr), 0x1000);
	return SetPageContent(reinterpret_cast<PageMapEntry*>(GetCR3()), 4, LinearAddress4Level{vaddr}, p);
}

Error HandlePageFault(uint64_t error_code, uint64_t cr2) {
	auto& task = task_manager->CurrentTask();
	const bool present = (error_code >> 0) & 1;
	const bool rw = (error_code >> 1) & 1;
	const bool user = (error_code >> 2) & 1;
	if (present && rw && user) {
		return CopyOnePage(cr2);	
	} else if (present) {
		return MakeError(Error::kAlreadyAllocated);
	}

	if (task.DPagingBegin() <= cr2 && cr2 < task.DPagingEnd()) {
		return SetupPageMaps(LinearAddress4Level{cr2}, 1, true);
	}
	if (auto m = FindFileMapping(task.FileMaps(), cr2)) {
		return PreparePageCache(*task.files[m->fd], *m, cr2);
	}
	
	return MakeError(Error::kIndexOutOfRange);
}

Error CopyPageMaps(PageMapEntry* dst, const PageMapEntry* src, int pagemap_lvl, int start) {
	if (pagemap_lvl == 1) {
		for (int i = start; i < 512; i++) {
			if (!src[i].bits.present) continue;
			dst[i] = src[i];
			dst[i].bits.writeable = 0;
		}
		return MakeError(Error::kSuccess);
	}

	for (int i = start; i < 512; i++) {
		if (!src[i].bits.present) continue;
		auto [ table, err ] = NewPageMap();
		if (err) return err;

		dst[i] = src[i];
		dst[i].SetPtr(table);
		if (auto err = CopyPageMaps(table, src[i].ptr(), pagemap_lvl - 1, 0)) {
			return err;
		}
	}
	return MakeError(Error::kSuccess);
}