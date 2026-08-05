/// @file I2cTransport.h
/// @brief Wire-based I2C transport adapter for examples
/// @note NOT part of the library - examples only
#pragma once

#include <Arduino.h>
#include <limits>
#include <Wire.h>
#include "SHT3x/Status.h"
#include "TransferStats.h"

namespace transport {

using SHT3x::Status;
using SHT3x::Err;

using TransferStats = sht3x_example::TransferStats;

inline TransferStats& transferStatsStorage() {
  static TransferStats stats;
  return stats;
}

inline uint32_t saturatingAdd(uint32_t value, size_t increment) {
  const uint32_t maximum = std::numeric_limits<uint32_t>::max();
  if (increment > static_cast<size_t>(maximum - value)) {
    return maximum;
  }
  return value + static_cast<uint32_t>(increment);
}

inline Status recordTransfer(Status status, bool readCallback,
                             size_t txBytes, size_t rxBytes) {
  TransferStats& stats = transferStatsStorage();
  if (readCallback) {
    stats.readCallbacks = saturatingAdd(stats.readCallbacks, 1U);
  } else {
    stats.writeCallbacks = saturatingAdd(stats.writeCallbacks, 1U);
  }
  if (status.ok()) {
    stats.successes = saturatingAdd(stats.successes, 1U);
  } else {
    stats.failures = saturatingAdd(stats.failures, 1U);
  }
  stats.txBytes = saturatingAdd(stats.txBytes, txBytes);
  stats.rxBytes = saturatingAdd(stats.rxBytes, rxBytes);
  return status;
}

inline TransferStats transferStats() {
  return transferStatsStorage();
}

inline void resetTransferStats() {
  transferStatsStorage() = TransferStats{};
}

/// Initialize Wire for examples
/// @param sda SDA pin
/// @param scl SCL pin
/// @param freqHz I2C clock frequency
/// @param timeoutMs Wire timeout in milliseconds
/// @return true if initialized
inline bool initWire(int sda, int scl, uint32_t freqHz, uint32_t timeoutMs) {
  // Example-only convenience. In a managed bus, the manager should own these settings.
  if (!Wire.begin(sda, scl, freqHz)) {
    return false;
  }
  Wire.setClock(freqHz);
  Wire.setTimeOut(timeoutMs);
  return true;
}

/// I2C write callback using Wire library
/// @param addr I2C device address (7-bit)
/// @param data Data buffer to write
/// @param len Number of bytes to write
/// @param timeoutMs Timeout requested by the driver (manager-owned in shared buses)
/// @param user User context (expects TwoWire*)
/// @return Status indicating success or failure
inline Status wireWrite(uint8_t addr, const uint8_t* data, size_t len,
                        uint32_t timeoutMs, void* user) {
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return recordTransfer(Status::Error(Err::INVALID_CONFIG, "Wire instance is null"),
                          false, 0U, 0U);
  }
  if (data == nullptr || len == 0) {
    return recordTransfer(Status::Error(Err::INVALID_PARAM, "I2C write buffer is invalid"),
                          false, 0U, 0U);
  }
  if (timeoutMs == 0) {
    return recordTransfer(Status::Error(Err::INVALID_PARAM, "I2C timeout must be > 0"),
                          false, 0U, 0U);
  }

  const uint32_t previousTimeoutMs = wire->getTimeOut();
  wire->setTimeOut(timeoutMs);
  wire->beginTransmission(addr);
  size_t written = wire->write(data, len);
  // SHT3x requires STOP between command write and read header.
  const uint32_t startMs = millis();
  uint8_t result = wire->endTransmission(true);
  const uint32_t elapsedMs = millis() - startMs;
  wire->setTimeOut(previousTimeoutMs);

  if (elapsedMs > timeoutMs) {
    return recordTransfer(Status::Error(Err::I2C_TIMEOUT, "I2C write timeout",
                                        static_cast<int32_t>(elapsedMs)),
                          false, written, 0U);
  }

  if (result != 0) {
    // Arduino Wire error codes (core-dependent): 1=data too long, 2=NACK addr, 3=NACK data,
    // 4=other, 5=timeout (ESP32 Arduino core).
    switch (result) {
      case 1: return recordTransfer(Status::Error(Err::INVALID_PARAM, "I2C write too long", result), false, written, 0U);
      case 2: return recordTransfer(Status::Error(Err::I2C_NACK_ADDR, "I2C NACK addr", result), false, written, 0U);
      case 3: return recordTransfer(Status::Error(Err::I2C_NACK_DATA, "I2C NACK data", result), false, written, 0U);
      case 4: return recordTransfer(Status::Error(Err::I2C_BUS, "I2C bus error", result), false, written, 0U);
      case 5: return recordTransfer(Status::Error(Err::I2C_TIMEOUT, "I2C timeout", result), false, written, 0U);
      default: return recordTransfer(Status::Error(Err::I2C_ERROR, "I2C write failed", result), false, written, 0U);
    }
  }
  if (written != len) {
    return recordTransfer(Status::Error(Err::I2C_ERROR, "I2C write incomplete",
                                        static_cast<int32_t>(written)),
                          false, written, 0U);
  }

  return recordTransfer(Status::Ok(), false, written, 0U);
}

/// I2C read callback using Wire library (read-only for SHT3x)
/// @param addr I2C device address (7-bit)
/// @param txData Unused for SHT3x (txLen must be 0)
/// @param txLen Number of bytes to write (must be 0 for SHT3x reads)
/// @param rxData Buffer for read data
/// @param rxLen Number of bytes to read
/// @param timeoutMs Timeout requested by the driver (manager-owned in shared buses)
/// @param user User context (expects TwoWire*)
/// @return Status indicating success or failure
inline Status wireWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                            uint8_t* rxData, size_t rxLen,
                            uint32_t timeoutMs, void* user) {
  (void)txData; // SHT3x reads are separate transactions; txLen must remain zero.
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return recordTransfer(Status::Error(Err::INVALID_CONFIG, "Wire instance is null"),
                          true, 0U, 0U);
  }

  if (txLen > 0) {
    return recordTransfer(Status::Error(Err::INVALID_PARAM, "Combined write+read not supported"),
                          true, txLen, 0U);
  }

  if (rxLen == 0) {
    return recordTransfer(Status::Ok(), true, txLen, 0U);
  }
  if (rxData == nullptr) {
    return recordTransfer(Status::Error(Err::INVALID_PARAM, "I2C read buffer is invalid"),
                          true, txLen, 0U);
  }
  if (timeoutMs == 0) {
    return recordTransfer(Status::Error(Err::INVALID_PARAM, "I2C timeout must be > 0"),
                          true, txLen, 0U);
  }

  // Read phase
  const uint32_t previousTimeoutMs = wire->getTimeOut();
  wire->setTimeOut(timeoutMs);
  const uint32_t startMs = millis();
  size_t received = wire->requestFrom(addr, rxLen);
  const uint32_t elapsedMs = millis() - startMs;
  wire->setTimeOut(previousTimeoutMs);
  if (elapsedMs > timeoutMs) {
    for (size_t i = 0; i < received; i++) {
      (void)wire->read();
    }
    return recordTransfer(Status::Error(Err::I2C_TIMEOUT, "I2C read timeout",
                                        static_cast<int32_t>(elapsedMs)),
                          true, txLen, received);
  }
  if (received != rxLen) {
    if (received == 0) {
      return recordTransfer(Status::Error(Err::I2C_ERROR, "I2C read returned 0 bytes",
                                          static_cast<int32_t>(received)),
                            true, txLen, received);
    }
    for (size_t i = 0; i < received; i++) {
      (void)wire->read();
    }
    return recordTransfer(Status::Error(Err::I2C_ERROR, "I2C read incomplete",
                                        static_cast<int32_t>(received)),
                          true, txLen, received);
  }

  for (size_t i = 0; i < rxLen; i++) {
    rxData[i] = wire->read();
  }

  return recordTransfer(Status::Ok(), true, txLen, rxLen);
}

} // namespace transport
