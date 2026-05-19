/// @file PlatformTime.h
/// @brief Private framework-neutral timing fallback for the SHT3x core.
#pragma once

#include <cstdint>

namespace SHT3x {
namespace platform {

inline uint32_t nowMs() {
  // Framework examples inject real time through Config::nowMs. The fallback is
  // intentionally inert so the core never includes Arduino or ESP-IDF headers.
  return 0U;
}

inline uint32_t nowUs() {
  return 0U;
}

inline void cooperativeYield() {
}

}  // namespace platform
}  // namespace SHT3x
