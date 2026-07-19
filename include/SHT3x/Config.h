/// @file Config.h
/// @brief Configuration structure for SHT3x driver
#pragma once

#include <cstddef>
#include <cstdint>
#include "SHT3x/Status.h"

namespace SHT3x {

/// Transport capability flags.
/// @note READ_HEADER_NACK changes driver behavior for periodic Fetch Data
///       readiness. TIMEOUT and BUS_ERROR document transport precision for
///       diagnostics and future policy; callbacks should still return the most
///       specific Err value they can prove.
enum class TransportCapability : uint8_t {
  NONE = 0,
  READ_HEADER_NACK = 1 << 0,   ///< Transport can reliably report read-header NACK
  TIMEOUT = 1 << 1,            ///< Transport can reliably report timeouts
  BUS_ERROR = 1 << 2           ///< Transport can reliably report bus errors
};

inline constexpr TransportCapability operator|(TransportCapability a, TransportCapability b) {
  return static_cast<TransportCapability>(
      static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline constexpr bool hasCapability(TransportCapability caps, TransportCapability cap) {
  return (static_cast<uint8_t>(caps) & static_cast<uint8_t>(cap)) != 0;
}

/// I2C write callback signature
/// @param addr     I2C device address (7-bit)
/// @param data     Pointer to data to write
/// @param len      Number of bytes to write
/// @param timeoutMs Maximum time to wait for completion
/// @param user     User context pointer passed through from Config
/// @return Status indicating success or failure. Return the most specific
///         transport error the adapter can prove:
///         - Err::I2C_NACK_ADDR for address NACK
///         - Err::I2C_NACK_DATA for data NACK
///         - Err::I2C_TIMEOUT for timeout
///         - Err::I2C_BUS for bus/arbitration error
///         - Err::I2C_ERROR when the adapter cannot distinguish the exact cause
/// @note Callbacks must not recursively call public APIs on the same SHT3x
///       instance. Serialize any shared bus access outside the driver. They
///       must return within timeoutMs, avoid unbounded waits, and must not
///       secretly own/configure bus pins, reset pins, global bus timeout, or
///       unrelated bus-manager policy.
using I2cWriteFn = Status (*)(uint8_t addr, const uint8_t* data, size_t len,
                              uint32_t timeoutMs, void* user);

/// I2C read callback signature (read-only for SHT3x)
/// @param addr     I2C device address (7-bit)
/// @param txData   Unused for SHT3x (txLen must be 0)
/// @param txLen    Number of bytes to write (MUST be 0 for SHT3x reads)
/// @param rxData   Pointer to buffer for read data
/// @param rxLen    Number of bytes to read
/// @param timeoutMs Maximum time to wait for completion
/// @param user     User context pointer passed through from Config
/// @return Status indicating success or failure. Return the most specific
///         transport error the adapter can prove:
///         - Err::I2C_NACK_ADDR for address NACK
///         - Err::I2C_NACK_DATA for data NACK
///         - Err::I2C_NACK_READ only when the adapter can prove read-header NACK
///         - Err::I2C_TIMEOUT for timeout
///         - Err::I2C_BUS for bus/arbitration error
///         - Err::I2C_ERROR when the adapter cannot distinguish the exact cause
/// @note The driver issues command writes via i2cWrite() and then calls
///       i2cWriteRead() with txLen==0 to perform the read after a tIDLE delay.
///       Combined write+read (repeated-start) is not allowed for SHT3x flows.
///       Callbacks must not recursively call public APIs on the same SHT3x
///       instance. Serialize any shared bus access outside the driver. They
///       must return within timeoutMs, avoid unbounded waits, and must not
///       secretly own/configure bus pins, reset pins, global bus timeout, or
///       unrelated bus-manager policy.
using I2cWriteReadFn = Status (*)(uint8_t addr, const uint8_t* txData, size_t txLen,
                                  uint8_t* rxData, size_t rxLen, uint32_t timeoutMs,
                                  void* user);

/// Optional bus reset callback (SCL pulse sequence)
/// @param user User context pointer (Config::i2cUser)
/// @return Status indicating success or failure
/// @note Must not recursively call public APIs on the same SHT3x instance.
using BusResetFn = Status (*)(void* user);

/// Optional hard reset callback (nRESET pulse)
/// @param user User context pointer (Config::i2cUser)
/// @return Status indicating success or failure
/// @note Must not recursively call public APIs on the same SHT3x instance.
using HardResetFn = Status (*)(void* user);

/// Millisecond timestamp callback.
/// @param user User context pointer passed through from Config
/// @return Current monotonic milliseconds
using NowMsFn = uint32_t (*)(void* user);

/// Microsecond timestamp callback.
/// @param user User context pointer passed through from Config
/// @return Current monotonic microseconds
using NowUsFn = uint32_t (*)(void* user);

/// Cooperative yield callback.
/// @param user User context pointer passed through from Config
using YieldFn = void (*)(void* user);

/// Measurement repeatability
enum class Repeatability : uint8_t {
  LOW_REPEATABILITY = 0,
  MEDIUM_REPEATABILITY = 1,
  HIGH_REPEATABILITY = 2
};

/// Clock stretching mode for single-shot measurement and serial-number reads.
/// Periodic and ART modes use Fetch Data and do not use this setting.
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

/// Driver health admission policy.
enum class HealthPolicy : uint8_t {
  OBSERVE_ONLY = 0, ///< Record health diagnostics but never suppress caller-authorized I2C
  LATCH_OFFLINE = 1 ///< Suppress normal I2C after offlineThreshold consecutive failures
};

/// Configuration for SHT3x driver
struct Config {
  // === I2C Transport (required) ===
  I2cWriteFn i2cWrite = nullptr;        ///< I2C write function pointer
  I2cWriteReadFn i2cWriteRead = nullptr; ///< I2C write-read function pointer
  void* i2cUser = nullptr;               ///< User context for callbacks
  BusResetFn busReset = nullptr;         ///< Optional interface reset callback
  HardResetFn hardReset = nullptr;       ///< Optional hard reset (nRESET pulse)

  // === Timing Hooks (required by begin/runtime) ===
  NowMsFn nowMs = nullptr;               ///< Monotonic millisecond source; required for begin/runtime timing
  NowUsFn nowUs = nullptr;               ///< Monotonic microsecond source; required for command spacing
  YieldFn cooperativeYield = nullptr;    ///< Cooperative scheduler hint used while waiting for bounded deadlines
  void* timeUser = nullptr;              ///< User context for timing hooks

  // === Device Settings ===
  uint8_t i2cAddress = 0x44;             ///< 0x44 (ADDR=GND) or 0x45 (ADDR=VDD)
  uint32_t i2cTimeoutMs = 50;            ///< I2C transaction timeout in ms (1..60000)
  TransportCapability transportCapabilities = TransportCapability::NONE; ///< Transport capabilities

  // === Measurement Settings ===
  Repeatability repeatability = Repeatability::HIGH_REPEATABILITY; ///< Measurement repeatability
  ClockStretching clockStretching = ClockStretching::STRETCH_DISABLED; ///< Single-shot clock stretching
  PeriodicRate periodicRate = PeriodicRate::MPS_1;   ///< Periodic rate (if in periodic mode)
  Mode mode = Mode::SINGLE_SHOT;                      ///< Operating mode
  bool lowVdd = false;                                ///< Use low-VDD timing limits

  // === Timing ===
  uint16_t commandDelayMs = 1;                        ///< Minimum command spacing (tIDLE), 0 normalizes to 1; max 1000 ms

  /// Periodic mode not-ready timeout (0 = disabled).
  /// @note Applies only when transportCapabilities includes READ_HEADER_NACK.
  ///       Before this timeout expires, a proven read-header NACK during
  ///       periodic Fetch Data is treated as MEASUREMENT_NOT_READY and does not
  ///       increment health failures.
  uint32_t notReadyTimeoutMs = 0;                     ///< 0..600000 ms

  /// Periodic fetch margin (ms) to avoid early fetches (0 = auto, max(2, period/20))
  uint32_t periodicFetchMarginMs = 0;                 ///< 0..60000 ms (clamped to <= period)

  /// Recovery backoff to avoid bus thrashing (ms)
  uint32_t recoverBackoffMs = 100;                    ///< 0..600000 ms

  // === Health Tracking ===
  HealthPolicy healthPolicy = HealthPolicy::LATCH_OFFLINE; ///< Health observation/admission behavior
  uint8_t offlineThreshold = 5;                       ///< Consecutive failures before OFFLINE; 0 normalizes to 1

  // === Reset Safety ===
  bool allowGeneralCallReset = false;                 ///< Allow general call reset on the bus
  bool recoverUseBusReset = true;                     ///< Use bus reset in recover() if callback provided
  bool recoverUseSoftReset = true;                    ///< Use soft reset in recover()
  bool recoverUseHardReset = true;                    ///< Use hard reset in recover() if callback provided
};

} // namespace SHT3x
