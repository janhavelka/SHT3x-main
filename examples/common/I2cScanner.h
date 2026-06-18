/// @file I2cScanner.h
/// @brief Simple I2C bus scanner for debugging
/// @note NOT part of the library - examples only
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "Log.h"

namespace i2c_scanner {

inline const char* labelForAddress(uint8_t addr) {
  if (addr == 0x44U || addr == 0x45U) {
    return "0x44/0x45=SHT3x";
  }
  if (addr == 0x76U || addr == 0x77U) {
    return "0x76/0x77=BME280/BMP280";
  }
  return "";
}

inline bool isValidAddress(uint8_t addr) {
  return !(addr < 0x08U || addr > 0x77U);
}

/// Check if a specific address responds
/// @param wire I2C bus instance
/// @param addr I2C address to check
/// @param timeoutMs I2C timeout in milliseconds
/// @return true if device responds
inline bool checkAddress(TwoWire& wire, uint8_t addr, uint32_t timeoutMs = 50U) {
  if (addr < 0x08U || addr > 0x77U) {
    return false;
  }
  wire.setTimeOut(timeoutMs);
  wire.beginTransmission(addr);
  return wire.endTransmission(true) == 0;
}

/// Scan I2C bus and print found devices
/// @param wire I2C bus instance
/// @param timeoutMs I2C timeout in milliseconds
/// @return Number of devices found
inline int scan(TwoWire& wire = Wire, uint32_t timeoutMs = 50U) {
  LOGI("Scanning I2C bus...");

  int count = 0;
  for (uint8_t addr = 0x08U; addr <= 0x77U; addr++) {
    wire.setTimeOut(timeoutMs);
    wire.beginTransmission(addr);
    const uint8_t error = wire.endTransmission(true);

    if (error == 0U) {
      const char* label = labelForAddress(addr);
      if (label[0] != '\0') {
        Serial.printf("  Found device at 0x%02X (%s)\n", addr, label);
      } else {
        Serial.printf("  Found device at 0x%02X\n", addr);
      }
      count++;
    }
  }

  if (count == 0) {
    LOGW("No I2C devices found");
  } else {
    LOGI("Scan complete. Found %d device(s).", count);
  }
  LOGI("Known: 0x44/0x45=SHT3x, 0x76/0x77=BME280/BMP280");

  return count;
}

} // namespace i2c_scanner

namespace i2c {

/// Scan default Arduino I2C bus and print found devices.
/// @return Number of devices found
inline int scan() {
  return i2c_scanner::scan(Wire);
}

/// Check if a specific address responds
/// @param addr I2C address to check
/// @return true if device responds
inline bool checkAddress(uint8_t addr) {
  return i2c_scanner::checkAddress(Wire, addr);
}

} // namespace i2c
