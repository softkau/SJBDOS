#pragma once
#include <cstdint>
#include <array>

#include "error.hpp"

namespace pci {

struct ClassCode {
	uint8_t base, sub, interface, revisionID;
	bool match(uint8_t base, uint8_t sub, uint8_t interface) const {
		return this->base == base && this->sub == sub && this->interface == interface;
	}
	bool match(uint8_t base, uint8_t sub) const {
		return this->base == base && this->sub == sub;
	}
	bool match(uint8_t base) const {
		return this->base == base;
	}
};
struct Device {
	uint8_t bus, device, func, hdr_type;
	uint16_t vendor_id;
	ClassCode class_code;
};

namespace g {
inline std::array<Device, 32> devices;
inline int num_devices;
}

Error ScanAllBus();

constexpr uint16_t CONFIG_ADDRESS = 0x0CF8; // IO address of CONFIG_ADDRESS register
constexpr uint16_t CONFIG_DATA    = 0x0CFC; // IO address of CONFIG_DATA register

namespace lowlevel {
struct CONFIG_ADDRESS_T {
	uint32_t enable : 1;
	uint32_t reserved : 7;
	uint32_t bus : 8;
	uint32_t device : 5;
	uint32_t func : 3;
	uint32_t reg_off : 8;
};
uint32_t MakeAddr(uint8_t bus, uint8_t device, uint8_t func, uint8_t reg_off);
void WriteAddr(uint32_t addr);
void WriteData(uint32_t value);
uint32_t ReadData();
}

uint16_t ReadVendorID(uint8_t bus, uint8_t device, uint8_t func);
uint16_t ReadDeviceID(uint8_t bus, uint8_t device, uint8_t func);
uint8_t ReadHeaderType(uint8_t bus, uint8_t device, uint8_t func);
ClassCode ReadClassCode(uint8_t bus, uint8_t device, uint8_t func);

template <uint8_t NDX>
uint32_t ReadBAR(uint8_t bus, uint8_t device, uint8_t func) {
	static_assert(NDX <= 5);
	lowlevel::WriteAddr(lowlevel::MakeAddr(bus, device, func, 0x10 + 4 * NDX));
	return lowlevel::ReadData();
}

bool IsSingleFunctionDevice(uint8_t hdr_type);

uint32_t ReadConfigSpace(const Device& device, uint8_t reg_off);
void WriteConfigSpace(const Device& device, uint8_t reg_off, uint32_t value);

/**
 * Reads 32/64-bit base address stored in the device of BAR(s).
 * Range of BAR index is 0-5. Trying to read a 64 bit address from 5th-BAR will return an 'Error' object.
 * @param device device to read
 * @param bar_index index of the BAR to be read
 * @retval uint64_t - 32 bit or 64 bit base address from selected BAR.
 * @retval Error - 'kIndexOutOfRange' when trying to read after 5th BAR.
 */
Optional<uint64_t> ReadDeviceBaseAddress(const Device& device, uint8_t bar_index);

enum class MSITriggerMode {
	Edge = 0,
	Level = 1
};

enum class MSIDeliveryMode : uint8_t {
	Fixed = 0b000,
	LowestPriority = 0b001,
	SMI = 0b010,
	NMI = 0b100,
	INIT = 0b101,
	ExtINT = 0b111
};

union CapabilityHeader {
	uint32_t data;
	struct {
		uint32_t cap_id : 8;
		uint32_t next_ptr : 8;
		uint32_t cap : 16;
	} __attribute__((packed)) bits;
	static_assert(sizeof(bits) == sizeof(data));
} __attribute__((packed));

struct MSICapability {
	union {
		uint32_t data;
		struct {
			uint32_t cap_id : 8;
			uint32_t next_ptr : 8;
			uint32_t msi_enable : 1;
			uint32_t multi_msg_capable : 3;
			uint32_t multi_msg_enable : 3;
			uint32_t addr_64_capable : 1;
			uint32_t per_vector_mask_capable : 1;
			uint32_t : 7;
		} __attribute__((packed)) bits;
		static_assert(sizeof(bits) == sizeof(data));
	} __attribute((packed)) header; // 4-byte header

	uint32_t msg_addr;
	uint32_t msg_upper_addr;
	uint32_t msg_data;
	uint32_t mask_bits;
	uint32_t pending_bits;
} __attribute__((packed));

constexpr uint8_t CAPABILITY_MSI = 0x05; // Cap-ID of MSI Capability struct
constexpr uint8_t CAPABILITY_MSIX = 0x11; // Cap-ID of MSI-X Capability struct

/**
 * @brief Reads capability header of PCI device
 * 
 * @param device PCI device of interest
 * @param addr offset where capability header is located
 * @return CapabilityHeader read from [PCI device + addr]
 */
CapabilityHeader ReadCapabilityHeader(const Device& device, uint8_t addr);

/**
 * @brief Configures MSI/MSI-X settings of xHC device
 * 
 * @param device xHC device to device
 * @param msg_addr Value to be written to Message Address Register
 * @param msg_data Value to be written to Message Data Register
 * @param num_vec_exponent Number of vectors to assign/allocate...?
 * @retval Error::kSuccess when success
 */
Error ConfigureMSI(
	const Device& device,
	uint32_t msg_addr,
	uint32_t msg_data,
	unsigned int num_vec_exponent
);

/**
 * @brief Confgiures xHC settings to activate MSI interrupt to fixed destination
 * 
 * @param dev xHC device to configure
 * @param apic_id Local APIC ID of destination core
 * @param trigger_mode Trigger Mode
 * @param delivery_mode Delivery Mode
 * @param vec Interrupt Vector ID
 * @param num_vec_exponenet Number of vectors to assign/allocate...?
 * @retval Error::kSuccess when success
 */
Error ConfigureMSIFixedDestination(
	const Device& dev,
	uint8_t apic_id,
	MSITriggerMode trigger_mode,
	MSIDeliveryMode delivery_mode,
	uint8_t vec,
	unsigned int num_vec_exponenet
);

}

void InitializePCI();
