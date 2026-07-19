/// @file Status.h
/// @brief Error codes and status handling for SHT3x driver
#pragma once

#include <cstdint>

namespace SHT3x {

/// Error codes for all SHT3x operations
enum class Err : uint8_t {
  OK = 0,                 ///< Operation successful
  NOT_INITIALIZED,        ///< bind()/begin() not called or end() called
  INVALID_CONFIG,         ///< Invalid configuration parameter
  I2C_ERROR,              ///< I2C communication failure (unspecified)
  TIMEOUT,                ///< Driver-side timeout (internal wait/guard)
  INVALID_PARAM,          ///< Invalid parameter value
  DEVICE_NOT_FOUND,       ///< Device not responding on I2C bus
  CRC_MISMATCH,           ///< CRC check failed
  MEASUREMENT_NOT_READY,  ///< Sample not yet available
  CONVERSION_NOT_READY = MEASUREMENT_NOT_READY, ///< Alias for cross-library uniformity
  BUSY,                   ///< Device or driver busy
  IN_PROGRESS,            ///< Operation scheduled; call tick() to complete
  COMMAND_FAILED,         ///< Sensor reported last command failed
  WRITE_CRC_ERROR,        ///< Sensor reported write checksum error
  UNSUPPORTED,            ///< Operation not supported (missing callback)
  I2C_NACK_ADDR,           ///< I2C NACK on address
  I2C_NACK_DATA,           ///< I2C NACK on data
  I2C_NACK_READ,           ///< I2C NACK on read header / no data
  I2C_TIMEOUT,             ///< I2C transaction timeout
  I2C_BUS,                 ///< I2C bus error (SDA stuck, arbitration, etc.)
  CANCELLED                ///< Cooperative job cancelled locally without I2C
};

/// True for errors returned directly by an injected I2C transport callback.
constexpr bool isTransportError(Err error) {
  return error == Err::I2C_ERROR || error == Err::I2C_NACK_ADDR ||
         error == Err::I2C_NACK_DATA || error == Err::I2C_NACK_READ ||
         error == Err::I2C_TIMEOUT || error == Err::I2C_BUS;
}

/// True for CRC failures or errors explicitly reported by the sensor protocol.
constexpr bool isProtocolError(Err error) {
  return error == Err::CRC_MISMATCH || error == Err::COMMAND_FAILED ||
         error == Err::WRITE_CRC_ERROR;
}

/// True only for a proven expected measurement-not-ready condition.
constexpr bool isExpectedNotReady(Err error) {
  return error == Err::MEASUREMENT_NOT_READY;
}

/// True only for a presence probe that proved an address was absent.
constexpr bool isAbsent(Err error) {
  return error == Err::DEVICE_NOT_FOUND;
}

/// Status structure returned by all fallible operations
struct Status {
  Err code = Err::OK;
  int32_t detail = 0;        ///< Implementation-specific detail (e.g., I2C error code)
  const char* msg = "";      ///< Static string describing the error

  constexpr Status() = default;
  constexpr Status(Err c, int32_t d, const char* m) : code(c), detail(d), msg(m) {}

  /// @return true if operation succeeded
  constexpr bool ok() const { return code == Err::OK; }

  /// @return true if the status matches the requested error code
  constexpr bool is(Err expected) const { return code == expected; }

  /// @return true if operation is scheduled but not complete
  constexpr bool inProgress() const { return code == Err::IN_PROGRESS; }

  /// @return true only if operation succeeded; IN_PROGRESS converts to false
  explicit constexpr operator bool() const { return ok(); }

  /// Create a success status
  static constexpr Status Ok() { return Status{Err::OK, 0, "OK"}; }

  /// Create an error status
  static constexpr Status Error(Err err, const char* message, int32_t detailCode = 0) {
    return Status{err, detailCode, message};
  }
};

} // namespace SHT3x
