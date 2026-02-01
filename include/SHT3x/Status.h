/// @file Status.h
/// @brief Error codes and status handling for SHT3x driver
#pragma once

#include <cstdint>

namespace SHT3x {

/// Error codes for all SHT3x operations
enum class Err : uint8_t {
  OK = 0,                 ///< Operation successful
  NOT_INITIALIZED,        ///< begin() not called
  INVALID_CONFIG,         ///< Invalid configuration parameter
  I2C_ERROR,              ///< I2C communication failure
  TIMEOUT,                ///< Operation timed out
  INVALID_PARAM,          ///< Invalid parameter value
  DEVICE_NOT_FOUND,       ///< Device not responding on I2C bus
  CRC_MISMATCH,           ///< CRC check failed
  MEASUREMENT_NOT_READY,  ///< Sample not yet available
  BUSY,                   ///< Device or driver busy
  IN_PROGRESS,            ///< Operation scheduled; call tick() to complete
  COMMAND_FAILED,         ///< Sensor reported last command failed
  WRITE_CRC_ERROR,        ///< Sensor reported write checksum error
  UNSUPPORTED             ///< Operation not supported (missing callback)
};

/// Status structure returned by all fallible operations
struct Status {
  Err code = Err::OK;
  int32_t detail = 0;        ///< Implementation-specific detail (e.g., I2C error code)
  const char* msg = "";      ///< Static string describing the error

  constexpr Status() = default;
  constexpr Status(Err c, int32_t d, const char* m) : code(c), detail(d), msg(m) {}

  /// @return true if operation succeeded
  constexpr bool ok() const { return code == Err::OK; }

  /// Create a success status
  static constexpr Status Ok() { return Status{Err::OK, 0, "OK"}; }

  /// Create an error status
  static constexpr Status Error(Err err, const char* message, int32_t detailCode = 0) {
    return Status{err, detailCode, message};
  }
};

} // namespace SHT3x
