/// @file Config.h
/// @brief Configuration structure for SHT3x driver
#pragma once

#include <cstddef>
#include <cstdint>
#include "SHT3x/Status.h"

namespace SHT3x {

/// I2C write callback signature
/// @param addr     I2C device address (7-bit)
/// @param data     Pointer to data to write
/// @param len      Number of bytes to write
/// @param timeoutMs Maximum time to wait for completion
/// @param user     User context pointer passed through from Config
/// @return Status indicating success or failure
using I2cWriteFn = Status (*)(uint8_t addr, const uint8_t* data, size_t len,
                              uint32_t timeoutMs, void* user);

/// I2C write-then-read callback signature
/// @param addr     I2C device address (7-bit)
/// @param txData   Pointer to data to write (may be nullptr if txLen==0)
/// @param txLen    Number of bytes to write (may be 0 for read-only)
/// @param rxData   Pointer to buffer for read data
/// @param rxLen    Number of bytes to read
/// @param timeoutMs Maximum time to wait for completion
/// @param user     User context pointer passed through from Config
/// @return Status indicating success or failure
using I2cWriteReadFn = Status (*)(uint8_t addr, const uint8_t* txData, size_t txLen,
                                  uint8_t* rxData, size_t rxLen, uint32_t timeoutMs,
                                  void* user);

/// Optional bus reset callback (SCL pulse sequence)
/// @param user User context pointer (Config::i2cUser)
/// @return Status indicating success or failure
using BusResetFn = Status (*)(void* user);

/// Measurement repeatability
enum class Repeatability : uint8_t {
  LOW_REPEATABILITY = 0,
  MEDIUM_REPEATABILITY = 1,
  HIGH_REPEATABILITY = 2
};

/// Clock stretching mode for single-shot/serial reads
enum class ClockStretching : uint8_t {
  STRETCH_DISABLED = 0,
  STRETCH_ENABLED = 1
};

/// Periodic measurement rate (measurements per second)
enum class PeriodicRate : uint8_t {
  MPS_0_5 = 0,
  MPS_1 = 1,
  MPS_2 = 2,
  MPS_4 = 3,
  MPS_10 = 4
};

/// Driver operating mode
enum class Mode : uint8_t {
  SINGLE_SHOT = 0,
  PERIODIC = 1,
  ART = 2
};

/// Configuration for SHT3x driver
struct Config {
  // === I2C Transport (required) ===
  I2cWriteFn i2cWrite = nullptr;        ///< I2C write function pointer
  I2cWriteReadFn i2cWriteRead = nullptr; ///< I2C write-read function pointer
  void* i2cUser = nullptr;               ///< User context for callbacks
  BusResetFn busReset = nullptr;         ///< Optional interface reset callback

  // === Device Settings ===
  uint8_t i2cAddress = 0x44;             ///< 0x44 (ADDR=GND) or 0x45 (ADDR=VDD)
  uint32_t i2cTimeoutMs = 50;            ///< I2C transaction timeout in ms

  // === Measurement Settings ===
  Repeatability repeatability = Repeatability::HIGH_REPEATABILITY; ///< Measurement repeatability
  ClockStretching clockStretching = ClockStretching::STRETCH_DISABLED; ///< Single-shot clock stretching
  PeriodicRate periodicRate = PeriodicRate::MPS_1;   ///< Periodic rate (if in periodic mode)
  Mode mode = Mode::SINGLE_SHOT;                      ///< Operating mode
  bool lowVdd = false;                                ///< Use low-VDD timing limits

  // === Timing ===
  uint16_t commandDelayMs = 1;                        ///< Minimum command spacing (tIDLE)

  // === Health Tracking ===
  uint8_t offlineThreshold = 5;                       ///< Consecutive failures before OFFLINE

  // === Reset Safety ===
  bool allowGeneralCallReset = false;                 ///< Allow general call reset on the bus
};

} // namespace SHT3x
