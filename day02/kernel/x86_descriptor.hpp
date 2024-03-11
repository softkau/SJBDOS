#pragma once
#include <cstdint>

enum class DescriptorType : uint16_t {
	// system segment & gate descriptor types
	Upper8Bytes = 0,
	LDT = 2,
	TSSAvailable = 9,
	TSSBusy = 11,
	CallGate = 12,
	InterruptGate = 14,
	TrapGate = 15,

	// code & data segment types
	ReadWrite = 2,
	ExecuteRead = 10
};