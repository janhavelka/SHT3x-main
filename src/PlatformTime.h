/// @file PlatformTime.h
/// @brief Private framework-specific timing fallback for the SHT3x core.
#pragma once

#include <cstdint>

#if defined(ESP_PLATFORM)
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#elif defined(ARDUINO)
#define SHT3X_PLATFORM_HAS_ARDUINO_TIME 1
#elif defined(__has_include)
#if __has_include(<Arduino.h>)
#define SHT3X_PLATFORM_HAS_ARDUINO_TIME 1
#endif
#endif

#if defined(SHT3X_PLATFORM_HAS_ARDUINO_TIME)
#include <Arduino.h>
#endif

namespace SHT3x {
namespace platform {

inline uint32_t nowMs() {
#if defined(ESP_PLATFORM)
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
#elif defined(SHT3X_PLATFORM_HAS_ARDUINO_TIME)
  return millis();
#else
  return 0U;
#endif
}

inline uint32_t nowUs() {
#if defined(ESP_PLATFORM)
  return static_cast<uint32_t>(esp_timer_get_time());
#elif defined(SHT3X_PLATFORM_HAS_ARDUINO_TIME)
  return micros();
#else
  return 0U;
#endif
}

inline void cooperativeYield() {
#if defined(ESP_PLATFORM)
  taskYIELD();
#elif defined(SHT3X_PLATFORM_HAS_ARDUINO_TIME)
  yield();
#endif
}

}  // namespace platform
}  // namespace SHT3x
