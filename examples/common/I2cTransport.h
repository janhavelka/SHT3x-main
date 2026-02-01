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
  Wire.begin(sda, scl);
  Wire.setClock(freqHz);
  Wire.setTimeOut(timeoutMs);
  return true;
}

/// I2C write callback using Wire library
/// @param addr I2C device address (7-bit)
/// @param data Data buffer to write
/// @param len Number of bytes to write
/// @param timeoutMs Timeout (used to set Wire timeout)
/// @param user User context (unused)
/// @return Status indicating success or failure
inline Status wireWrite(uint8_t addr, const uint8_t* data, size_t len,
                        uint32_t timeoutMs, void* user) {
  (void)user;
  (void)timeoutMs;

  Wire.beginTransmission(addr);
  size_t written = Wire.write(data, len);
  uint8_t result = Wire.endTransmission();

  if (result != 0) {
    // Wire error codes: 1=data too long, 2=NACK addr, 3=NACK data, 4=other, 5=timeout
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

/// I2C write-then-read callback using Wire library
/// @param addr I2C device address (7-bit)
/// @param txData Data buffer to write (may be nullptr if txLen==0)
/// @param txLen Number of bytes to write (may be 0 for read-only)
/// @param rxData Buffer for read data
/// @param rxLen Number of bytes to read
/// @param timeoutMs Timeout (used to set Wire timeout)
/// @param user User context (unused)
/// @return Status indicating success or failure
inline Status wireWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                            uint8_t* rxData, size_t rxLen,
                            uint32_t timeoutMs, void* user) {
  (void)user;
  (void)timeoutMs;

  if (txLen > 0) {
    // Write phase
    Wire.beginTransmission(addr);
    size_t written = Wire.write(txData, txLen);
    uint8_t result = Wire.endTransmission(false);  // Repeated start

    if (result != 0) {
      switch (result) {
        case 1: return Status::Error(Err::INVALID_PARAM, "I2C write too long", result);
        case 2: return Status::Error(Err::I2C_NACK_ADDR, "I2C NACK addr", result);
        case 3: return Status::Error(Err::I2C_NACK_DATA, "I2C NACK data", result);
        case 4: return Status::Error(Err::I2C_BUS, "I2C bus error", result);
        case 5: return Status::Error(Err::I2C_TIMEOUT, "I2C timeout", result);
        default: return Status::Error(Err::I2C_ERROR, "I2C write phase failed", result);
      }
    }
    if (written != txLen) {
      return Status::Error(Err::I2C_ERROR, "I2C write incomplete", static_cast<int32_t>(written));
    }
  }

  if (rxLen == 0) {
    return Status::Ok();
  }

  // Read phase
  size_t received = Wire.requestFrom(addr, rxLen);
  if (received != rxLen) {
    if (received == 0) {
      return Status::Error(Err::I2C_NACK_READ, "I2C NACK read header", 0);
    }
    return Status::Error(Err::I2C_ERROR, "I2C read incomplete", static_cast<int32_t>(received));
  }

  for (size_t i = 0; i < rxLen; i++) {
    rxData[i] = Wire.read();
  }

  return Status::Ok();
}

} // namespace transport
