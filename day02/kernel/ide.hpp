#pragma once
#include <cstdint>

namespace ide {
  void initIDE(uint32_t bar0, uint32_t bar1, uint32_t bar2, uint32_t bar3, uint32_t bar4);
  void readSectors(uint8_t drive, uint8_t num_sectors, uint32_t lba, void* buf);
  void writeSectors(uint8_t drive, uint8_t num_sectors, uint32_t lba, const void* buf);
}