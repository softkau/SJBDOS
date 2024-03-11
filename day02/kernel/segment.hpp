#pragma once
#include <cstdint>

#include "x86_descriptor.hpp"

union SegmentDescriptor {
	uint64_t data;
	struct {
		uint64_t limit_low : 16;					// segment size - 1(low) (in bytes)
		uint64_t base_low : 16;						// segment start address(low)
		uint64_t base_middle : 8;					// segment start address(mid)
		DescriptorType type : 4;					// descriptor type
		uint64_t system_segment : 1;				// value 1 for code/data segment
		uint64_t descriptor_privilege_level : 2;	// privilege level of descriptor
		uint64_t present : 1;						// vaildity of descriptor
		uint64_t limit_high : 4;					// segment size - 1(high) (in bytes)
		uint64_t available : 1;						// available for operating system(freely usable)
		uint64_t long_mode : 1;						// value 1 for x64 code segment
		uint64_t default_operation_size : 1;		// value 1 if long_mode == 0
		uint64_t granularity : 1;					// value 1 to interpret limit_* with 4KiB unit
		uint64_t base_high : 8;						// segment start address(high)
	} __attribute__((packed)) bits;
	static_assert(sizeof data == sizeof bits);
} __attribute__((packed));

void SetCodeSegment(
	SegmentDescriptor& desc,
	DescriptorType type,
	unsigned int descriptor_privilege_level,
	uint32_t segment_address,
	uint32_t segment_size
);

constexpr uint16_t kKernelCS = 1 << 3;
constexpr uint16_t kKernelSS = 2 << 3;
constexpr uint16_t kKernelDS = 0;
constexpr uint16_t kTSS = 5 << 3;

constexpr uint16_t kISTForTimer = 1;

void SetupSegments();
void InitializeSegmentation();
void InitializeTSS();
