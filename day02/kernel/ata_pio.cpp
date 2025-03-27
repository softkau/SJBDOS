#include <cstdint>
#include "pci.hpp"
#include "asmfunc.h"
#include "ata_pio.hpp"
#include "logger.hpp"

namespace ata {
  ATADEV_T DetectDevType(int slavebit, const pci::Device& dev) {
    // soft reset : ??
    Log(kWarn, "DEV:%02x.%02x.%02x\n", dev.class_code.base, dev.class_code.sub, dev.class_code.interface);
    Log(kWarn, "header_type:%02x\n", dev.hdr_type);

    uint16_t io_base = pci::ReadBAR<0>(dev.bus, dev.device, dev.func);
    uint16_t dev_ctrl = pci::ReadBAR<1>(dev.bus, dev.device, dev.func);
    if (io_base == 0x00 || io_base == 0x01)
      io_base = 0x1F0;
    if (dev_ctrl == 0x00 || dev_ctrl == 0x01)
      dev_ctrl = 0x3F4;

    // pci::WriteConfigSpace(dev, 0x3C, 0xFE);
    if ((pci::ReadConfigSpace(dev, 0x3C) & 0xFF) == 0xFE) {
      // needs IRQ assignment
      return ATADEV_T::SATA;
    } else {
      // uses IRQ 14, IRQ 15
      return ATADEV_T::PATA;
    }
    Log(kWarn, "IO: 0x%x, DEV: 0x%x\n", io_base, dev_ctrl);
    AtaSoftReset(dev_ctrl);
    IoOut8(io_base+ 6, 0xA0 | slavebit << 4);
    IoIn8(dev_ctrl); // wait 400ns
    IoIn8(dev_ctrl);
    IoIn8(dev_ctrl);
    IoIn8(dev_ctrl);
    uint8_t cl = IoIn8(io_base + 4);
    uint8_t ch = IoIn8(io_base + 5);
    if (cl==0x14 && ch==0xEB) return ATADEV_T::PATAPI;
    if (cl==0x69 && ch==0x96) return ATADEV_T::SATAPI;
    if (cl==0 && ch == 0) return ATADEV_T::PATA;
    if (cl==0x3c && ch==0xc3) return ATADEV_T::SATA;
    return ATADEV_T::UNKNOWN;
  }
}