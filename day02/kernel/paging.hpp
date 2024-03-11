#pragma once

#include <cstdint>
#include "error.hpp"
#include "task.hpp"

union PageMapEntry {
	uint64_t data;

	struct {
		uint64_t present : 1;
		uint64_t writeable : 1;
		uint64_t user : 1;
		uint64_t write_through : 1;
		uint64_t cache_disable : 1;
		uint64_t accessed : 1;
		uint64_t dirty : 1;
		uint64_t huge_page : 1;
		uint64_t global : 1;
		uint64_t : 3;

		uint64_t addr : 40;
		uint64_t : 12;
	} __attribute__((packed)) bits;

	PageMapEntry* ptr() const { return reinterpret_cast<PageMapEntry*>(bits.addr << 12); }
	void SetPtr(PageMapEntry* p) { bits.addr = reinterpret_cast<uint64_t>(p) >> 12; }
};

union LinearAddress4Level {
	uint64_t value;

	struct {
		uint64_t offset : 12;
		uint64_t p_table : 9;
		uint64_t p_directory : 9;
		uint64_t p_dir_pointer : 9;
		uint64_t PML4 : 9;
		uint64_t : 16;
	} __attribute__((packed)) bits;

	unsigned get(int page_lvl) const {
		switch (page_lvl) {
			case 0: return bits.offset;
			case 1: return bits.p_table;
			case 2: return bits.p_directory;
			case 3: return bits.p_dir_pointer;
			case 4: default: return bits.PML4;
		}
	}

	void set(int page_lvl, unsigned value) {
		switch (page_lvl) {
			case 0: bits.offset = value; break;
			case 1: bits.p_table = value; break;
			case 2: bits.p_directory = value; break;
			case 3: bits.p_dir_pointer = value; break;
			case 4: default: bits.PML4 = value; break;
		}
	}
};

void InitializePaging();
void SetupIdentityPageTable();
Error SetupPageMaps(LinearAddress4Level addr, size_t num_4kpages, bool writeable);
Error CleanPageMaps(LinearAddress4Level addr);
Error CleanTempPML4(uint64_t pml4, int start);
WithError<PageMapEntry*> SetupPML4(Task& cur_task);
Error FreePML4(Task& cur_task);
Error HandlePageFault(uint64_t error_code, uint64_t cr2);
Error CopyPageMaps(PageMapEntry* dst, const PageMapEntry* src, int pagemap_lvl, int start);