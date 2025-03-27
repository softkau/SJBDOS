#include "pci.hpp"
#include "asmfunc.h"
#include "logger.hpp"

namespace {

Error addDevice(uint8_t bus, uint8_t dev, uint8_t func, uint8_t hdr_type, uint16_t vendor_id, pci::ClassCode cc) {
	if (pci::g::num_devices == pci::g::devices.size()) return MakeError(Error::kFull);
	pci::g::devices[pci::g::num_devices++] = pci::Device{bus, dev, func, hdr_type, vendor_id, cc};
	return MakeError(Error::kSuccess);
}

Error scanBus(uint8_t bus);
Error scanFunction(uint8_t bus, uint8_t dev, uint8_t func) {
	auto hdr_type = pci::ReadHeaderType(bus, dev, func);
	auto class_code = pci::ReadClassCode(bus, dev, func);
	auto vendor_id = pci::ReadVendorID(bus, dev, func);
	if (auto err = addDevice(bus, dev, func, hdr_type, vendor_id, class_code)) return err;

	if (class_code.base == 0x06u && class_code.sub == 0x04u) {
		// PCI - PCI bridge
		auto bus_numbers = pci::ReadBAR<2>(bus, dev, func);
		auto secondary_bus = static_cast<uint8_t>(bus_numbers >> 8);
		return scanBus(secondary_bus);
	}
	return MakeError(Error::kSuccess);
}

Error scanDevice(uint8_t bus, uint8_t dev) {
	auto hdr_type = pci::ReadHeaderType(bus, dev, 0);
	if (pci::IsSingleFunctionDevice(hdr_type)) {
		return scanFunction(bus, dev, 0);
	}

	for (uint8_t func = 0; func < 8; func++) {
		if (pci::ReadVendorID(bus, dev, func) == 0xFFFFu) continue;

		if (auto err = scanFunction(bus, dev, func)) return err;
	}

	return MakeError(Error::kSuccess);
}

Error scanBus(uint8_t bus) {
	for (uint8_t device = 0; device < 32; device++) {
		if (pci::ReadVendorID(bus, device, 0) == 0xFFFFu) continue;
		
		if (auto err = scanDevice(bus, device)) return err;
	}
	return MakeError(Error::kSuccess);
}

pci::MSICapability ReadMSICapability(const pci::Device& device, uint8_t cap_addr) {
	pci::MSICapability msi_cap{};
	
	msi_cap.header.data = pci::ReadConfigSpace(device, cap_addr);
	msi_cap.msg_addr = pci::ReadConfigSpace(device, cap_addr + 4);
	uint8_t msg_data_begin = cap_addr + 8;

	// if 64 capable, msg_data section starts 4 bytes later than 32 capable
	if (msi_cap.header.bits.addr_64_capable) {
		msi_cap.msg_upper_addr = pci::ReadConfigSpace(device, cap_addr + 8);
		msg_data_begin += 4;
	}

	msi_cap.msg_data = pci::ReadConfigSpace(device, msg_data_begin);
	if (msi_cap.header.bits.per_vector_mask_capable) {
		msi_cap.mask_bits = pci::ReadConfigSpace(device, msg_data_begin + 4);
		msi_cap.pending_bits = pci::ReadConfigSpace(device, msg_data_begin + 8);
	}
	return msi_cap;
}

void WriteMSICapability(const pci::Device& device, uint8_t cap_addr, const pci::MSICapability& msi_cap) {
	pci::WriteConfigSpace(device, cap_addr, msi_cap.header.data);
	pci::WriteConfigSpace(device, cap_addr + 4, msi_cap.msg_addr);
	uint8_t msg_data_begin = cap_addr + 8;

	// if 64 capable, msg_data section starts 4 bytes later than 32 capable
	if (msi_cap.header.bits.addr_64_capable) {
		pci::WriteConfigSpace(device, cap_addr + 8, msi_cap.msg_upper_addr);
		msg_data_begin += 4;
	}

	pci::WriteConfigSpace(device, msg_data_begin, msi_cap.msg_data);
	if (msi_cap.header.bits.per_vector_mask_capable) {
		pci::WriteConfigSpace(device, msg_data_begin + 4, msi_cap.mask_bits);
		pci::WriteConfigSpace(device, msg_data_begin + 8, msi_cap.pending_bits);
	}
}

Error ConfigureMSIRegister(
		const pci::Device& device,
		uint8_t cap_addr,
		uint32_t msg_addr,
		uint32_t msg_data,
		unsigned int num_vec_exponent) {
	auto msi_cap = ReadMSICapability(device, cap_addr);

	if (msi_cap.header.bits.multi_msg_capable <= num_vec_exponent)
		msi_cap.header.bits.multi_msg_enable = msi_cap.header.bits.multi_msg_capable;
	else
		msi_cap.header.bits.multi_msg_enable = num_vec_exponent;

	msi_cap.header.bits.msi_enable = 1;
	msi_cap.msg_addr = msg_addr;
	msi_cap.msg_data = msg_data;

	WriteMSICapability(device, cap_addr, msi_cap);
	return MakeError(Error::kSuccess);
}

Error ConfigureMSIXRegister(
		const pci::Device& device,
		uint8_t cap_addr,
		uint32_t msg_addr,
		uint32_t msg_data,
		unsigned int num_vec_exponent) {
	return MakeError(Error::kNotImplemented);
}

} // end of static namespace


namespace pci {

bool IsSingleFunctionDevice(uint8_t hdr_type) {
	return (hdr_type & 0x80) == 0;
}

Error ScanAllBus() {
	g::num_devices = 0;

	auto hdr_type = ReadHeaderType(0, 0, 0); // get host bridge 0
	if (IsSingleFunctionDevice(hdr_type)) {
		return scanBus(0);
	}

	for (uint8_t func = 0; func < 8; func++) {
		if (ReadVendorID(0, 0, func) == 0xFFFFu) continue;

		if (auto err = scanBus(func)) return err;
	}

	return MakeError(Error::kSuccess);
}

uint32_t lowlevel::MakeAddr(uint8_t bus, uint8_t device, uint8_t func, uint8_t reg_off) {
	auto lsh = [](uint8_t x, uint8_t amt) {
		return x << amt;
	};
	return lsh(1, 31) | lsh(bus, 16) | lsh(device & 31u, 11) | lsh(func & 7u, 8) | (reg_off & 0xFCu);
}

void lowlevel::WriteAddr(uint32_t addr) {
	IoOut32(pci::CONFIG_ADDRESS, addr);
}

void lowlevel::WriteData(uint32_t value) {
	IoOut32(pci::CONFIG_DATA, value);
}

uint32_t lowlevel::ReadData() {
	return IoIn32(pci::CONFIG_DATA);
}

uint16_t ReadVendorID(uint8_t bus, uint8_t device, uint8_t func) {
	lowlevel::WriteAddr(lowlevel::MakeAddr(bus, device, func, 0x00));
	return lowlevel::ReadData() & 0xFFFFu;
}

uint16_t ReadDeviceID(uint8_t bus, uint8_t device, uint8_t func) {
	lowlevel::WriteAddr(lowlevel::MakeAddr(bus, device, func, 0x00));
	return lowlevel::ReadData() >> 16;
}
uint8_t ReadHeaderType(uint8_t bus, uint8_t device, uint8_t func) {
	lowlevel::WriteAddr(lowlevel::MakeAddr(bus, device, func, 0x0C));
	return (lowlevel::ReadData() >> 16) & 0xFFu;
}

ClassCode ReadClassCode(uint8_t bus, uint8_t device, uint8_t func) {
	lowlevel::WriteAddr(lowlevel::MakeAddr(bus, device, func, 0x08));
	uint32_t data = lowlevel::ReadData();
	return ClassCode {
		.base       = static_cast<uint8_t>(data >> 24u),
		.sub        = static_cast<uint8_t>(data >> 16u),
		.interface  = static_cast<uint8_t>(data >> 8u),
		.revisionID = static_cast<uint8_t>(data)
	};
}

uint32_t ReadConfigSpace(const Device& device, uint8_t reg_off) {
	lowlevel::WriteAddr(lowlevel::MakeAddr(device.bus, device.device, device.func, reg_off));
	return lowlevel::ReadData();
}

void WriteConfigSpace(const Device& device, uint8_t reg_off, uint32_t value) {
	lowlevel::WriteAddr(lowlevel::MakeAddr(device.bus, device.device, device.func, reg_off));
	lowlevel::WriteData(value);
}

Optional<uint64_t> ReadDeviceBaseAddress(const Device& device, uint8_t bar_index) {
	/*
	Each BAR has 4 bits of flag values, 28 bits of base address.
	Each BAR may represent 32 bit address, or
	two BARs(concatenated) can represent 64 bit address.
	For more detailed explanation, refer to: https://stackoverflow.com/questions/30190050/what-is-the-base-address-register-bar-in-pcie
	*/
	if (bar_index > 5) return MakeError(Error::kIndexOutOfRange);
	auto reg_off = 0x10 + bar_index * 4;
	uint64_t bar = ReadConfigSpace(device, reg_off);

	switch ((bar & 0b0110) >> 1) {
		case 0: return bar;	// 32 bit address
		case 1: return bar;	// less than 1 MiB ...??
		default: {			// 64 bit address
			if (bar_index == 5) return MakeError(Error::kIndexOutOfRange);
			uint64_t upper_bar = ReadConfigSpace(device, reg_off + 4);
			return (upper_bar << 32) | bar;
		}
	}
}

CapabilityHeader ReadCapabilityHeader(const Device& device, uint8_t addr) {
	CapabilityHeader header;
	header.data = pci::ReadConfigSpace(device, addr);
	return header;
}

Error ConfigureMSI(
		const Device& device,
		uint32_t msg_addr,
		uint32_t msg_data,
		unsigned int num_vec_exponent) {
	uint8_t cap_addr = ReadConfigSpace(device, 0x34) & 0xFFu; // pointer to capability struct
	uint8_t msi_cap_addr = 0, msix_cap_addr = 0;
	while (cap_addr) {
		auto header = ReadCapabilityHeader(device, cap_addr);

		if (header.bits.cap_id == CAPABILITY_MSI)
			msi_cap_addr = cap_addr;
		else if (header.bits.cap_id == CAPABILITY_MSIX)
			msix_cap_addr = cap_addr;

		cap_addr = header.bits.next_ptr;
	}

	if (msi_cap_addr)
		return ConfigureMSIRegister(device, msi_cap_addr, msg_addr, msg_data, num_vec_exponent);
	else if (msix_cap_addr)
		return ConfigureMSIXRegister(device, msix_cap_addr, msg_addr, msg_data, num_vec_exponent);
	return MakeError(Error::kNoPCIMSI);
}

Error ConfigureMSIFixedDestination(
		const Device& dev,
		uint8_t apic_id,
		MSITriggerMode trigger_mode,
		MSIDeliveryMode delivery_mode,
		uint8_t vec,
		unsigned int num_vec_exponenet) {
	uint32_t msg_addr = 0xFEE00000u | (apic_id << 12);
	uint32_t msg_data = (static_cast<uint32_t>(delivery_mode) << 8) | vec;
	if (trigger_mode == MSITriggerMode::Level) {
		msg_data |= 0xC000;
	}
	return ConfigureMSI(dev, msg_addr, msg_data, num_vec_exponenet);
}

} // namespace pci

#include <cstdlib>
#include "ata_pio.hpp"
#include "ide.hpp"

void InitializePCI() {
	if (auto err = pci::ScanAllBus()) {
		Log(kError, "ScanAllBus: %s\n", err.Name());
		exit(1);
	}
  for (int i = 0; i < pci::g::num_devices; i++) {
    auto& dev = pci::g::devices[i];
    if (dev.class_code.base == 0x01) {
      auto dev_type = ata::DetectDevType(0, dev);
      switch (dev_type) {
        case ata::ATADEV_T::PATA: Log(kWarn, "PATA device\n"); break;
        case ata::ATADEV_T::PATAPI: Log(kWarn, "PATAPI device\n"); break;
        case ata::ATADEV_T::SATA: Log(kWarn, "SATA device\n"); break;
        case ata::ATADEV_T::SATAPI: Log(kWarn, "SATAPI device\n"); break;
        case ata::ATADEV_T::UNKNOWN: Log(kWarn, "??? device\n"); break;
      }
    }
  }
}
