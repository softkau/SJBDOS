#pragma once
#include <cstdint>
#include "pci.hpp"
#include "asmfunc.h"

namespace ata {
  enum class ATADEV_T {
    PATAPI, SATAPI, PATA, SATA, UNKNOWN
  };

  ATADEV_T DetectDevType(int slavebit, const pci::Device& dev);
}