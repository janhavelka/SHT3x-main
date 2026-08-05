/// @file TransferStats.h
/// @brief Framework-neutral transfer counters shared by diagnostic examples.
/// @note Example support only; not part of the SHT3x library API.
#pragma once

#include <cstdint>

namespace sht3x_example {

struct TransferStats {
  uint32_t readCallbacks = 0;
  uint32_t writeCallbacks = 0;
  uint32_t successes = 0;
  uint32_t failures = 0;
  uint32_t txBytes = 0;
  uint32_t rxBytes = 0;
};

}  // namespace sht3x_example
