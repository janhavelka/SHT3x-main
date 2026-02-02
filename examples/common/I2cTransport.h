/// @file I2cTransport.h
/// @brief Wire-based I2C transport adapter for examples
/// @note NOT part of the library - examples only
#pragma once

#include <Arduino.h>
#include <Wire.h>
#include "SHT3x/Status.h"

namespace transport {

using SHT3x::Status;
using SHT3x::Err;

/// Initialize Wire for examples
/// @param sda SDA pin
/// @param scl SCL pin
/// @param freqHz I2C clock frequency
/// @param timeoutMs Wire timeout in milliseconds
/// @return true if initialized
inline bool initWire(int sda, int scl, uint32_t freqHz, uint32_t timeoutMs) {
  // Example-only convenience. In a managed bus, the manager should own these settings.
  Wire.begin(sda, scl);
  Wire.setClock(freqHz);
  Wire.setTimeOut(timeoutMs);
  return true;
}

/// I2C write callback using Wire library
/// @param addr I2C device address (7-bit)
/// @param data Data buffer to write
/// @param len Number of bytes to write
/// @param timeoutMs Timeout requested by the driver (manager-owned in shared buses)
/// @param user User context (unused)
/// @return Status indicating success or failure
inline Status wireWrite(uint8_t addr, const uint8_t* data, size_t len,
                        uint32_t timeoutMs, void* user) {
  (void)user;
  (void)timeoutMs;

  Wire.beginTransmission(addr);
  size_t written = Wire.write(data, len);
  // SHT3x requires STOP between command write and read header.
  uint8_t result = Wire.endTransmission(true);

  if (result != 0) {
    // Arduino Wire error codes (core-dependent): 1=data too long, 2=NACK addr, 3=NACK data,
    // 4=other, 5=timeout (ESP32 Arduino core).
    switch (result) {
      case 1: return Status::Error(Err::INVALID_PARAM, "I2C write too long", result);
      case 2: return Status::Error(Err::I2C_NACK_ADDR, "I2C NACK addr", result);
      case 3: return Status::Error(Err::I2C_NACK_DATA, "I2C NACK data", result);
      case 4: return Status::Error(Err::I2C_BUS, "I2C bus error", result);
      case 5: return Status::Error(Err::I2C_TIMEOUT, "I2C timeout", result);
      default: return Status::Error(Err::I2C_ERROR, "I2C write failed", result);
    }
  }
  if (written != len) {
    return Status::Error(Err::I2C_ERROR, "I2C write incomplete", static_cast<int32_t>(written));
  }

  return Status::Ok();
}

/// I2C read callback using Wire library (read-only for SHT3x)
/// @param addr I2C device address (7-bit)
/// @param txData Unused for SHT3x (txLen must be 0)
/// @param txLen Number of bytes to write (must be 0 for SHT3x reads)
/// @param rxData Buffer for read data
/// @param rxLen Number of bytes to read
/// @param timeoutMs Timeout requested by the driver (manager-owned in shared buses)
/// @param user User context (unused)
/// @return Status indicating success or failure
inline Status wireWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                            uint8_t* rxData, size_t rxLen,
                            uint32_t timeoutMs, void* user) {
  (void)user;
  (void)timeoutMs;

  if (txLen > 0) {
    return Status::Error(Err::INVALID_PARAM, "Combined write+read not supported");
  }

  if (rxLen == 0) {
    return Status::Ok();
  }

  // Read phase
  size_t received = Wire.requestFrom(addr, rxLen);
  if (received != rxLen) {
    if (received == 0) {
      return Status::Error(Err::I2C_ERROR, "I2C read returned 0 bytes",
                           static_cast<int32_t>(received));
    }
    for (size_t i = 0; i < received; i++) {
      (void)Wire.read();
    }
    return Status::Error(Err::I2C_ERROR, "I2C read incomplete", static_cast<int32_t>(received));
  }

  for (size_t i = 0; i < rxLen; i++) {
    rxData[i] = Wire.read();
  }

  return Status::Ok();
}

} // namespace transport
