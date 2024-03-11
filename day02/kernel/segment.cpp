#include "segment.hpp"
#include <array>
#include "asmfunc.h"
#include "memory_manager.hpp"
#include "logger.hpp"

namespace {
	std::array<SegmentDescriptor, 7> gdt;
	std::array<uint32_t, 26> tss;

	static_assert((kTSS >> 3) + 1 < gdt.size());
}

void SetCodeSegment(SegmentDescriptor& desc, DescriptorType type, unsigned int descriptor_privilege_level, uint32_t base, uint32_t limit) {
	desc.data = 0;

	desc.bits.base_low = base & 0xFFFFu;
	desc.bits.base_middle = (base >> 16) & 0xFFu;
	desc.bits.base_high = base >> 24;

	desc.bits.limit_low = limit & 0xFFFFu;
	desc.bits.limit_high = (limit >> 16) & 0x0Fu;

	desc.bits.type = type;
	desc.bits.system_segment = 1;
	desc.bits.descriptor_privilege_level = descriptor_privilege_level;
	desc.bits.present = 1;
	desc.bits.available = 0;
	desc.bits.long_mode = 1;
	desc.bits.default_operation_size = 0;
	desc.bits.granularity = 1;
}

void SetDataSegment(SegmentDescriptor& desc, DescriptorType type, unsigned int descriptor_privilege_level, uint32_t base, uint32_t limit) {
	SetCodeSegment(desc, type, descriptor_privilege_level, base, limit);
	desc.bits.long_mode = 0;
	desc.bits.default_operation_size = 1;
}
void SetSystemSegment(SegmentDescriptor& desc, DescriptorType type, unsigned int descriptor_privilege_level, uint32_t base, uint32_t limit) {
	SetCodeSegment(desc, type, descriptor_privilege_level, base, limit);
	desc.bits.system_segment = 0;
	desc.bits.long_mode = 0;
}

void SetupSegments() {
	gdt[0].data = 0; // null descriptor
	SetCodeSegment(gdt[1], DescriptorType::ExecuteRead, 0, 0, 0xfffff);
	SetDataSegment(gdt[2], DescriptorType::ReadWrite,   0, 0, 0xfffff);
	SetDataSegment(gdt[3], DescriptorType::ReadWrite,   3, 0, 0xfffff);
	SetCodeSegment(gdt[4], DescriptorType::ExecuteRead, 3, 0, 0xfffff);
	LoadGDT(sizeof gdt - 1, reinterpret_cast<uint64_t>(&gdt[0]));
}

void InitializeSegmentation() {
	SetupSegments();
	SetSegRegs(kKernelSS, kKernelCS);
}

void InitializeTSS() {
	auto alloc_stack = [](size_t num_4kframes) -> uintptr_t {
		auto stack = memory_manager->Allocate(num_4kframes);
		if (!stack.has_value) {
			Log(kError, "failed to allocate rsp0\n");
			exit(1);
		}
		return reinterpret_cast<uintptr_t>(stack.value.Frame()) + num_4kframes * 4096;
	};

	auto set_tss = [](size_t idx, uint64_t value) {
		tss[idx] = value & 0xffffffff;
		tss[idx + 1] = value >> 32;
	};

	set_tss(1, alloc_stack(8)); // rsp0
	set_tss(7 + 2 * kISTForTimer, alloc_stack(8)); // ist1

	uint64_t tss_addr = reinterpret_cast<uint64_t>(&tss[0]);
	SetSystemSegment(gdt[kTSS >> 3], DescriptorType::TSSAvailable, 0, tss_addr & 0xffffffff, sizeof(tss)-1);
	gdt[(kTSS >> 3) + 1].data = tss_addr >> 32;
	
	LoadTR(kTSS);
}
