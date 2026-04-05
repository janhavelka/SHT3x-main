/**
 * @file HealthDiag.h
 * @brief Verbose health diagnostic helpers for SHT3x examples.
 *
 * NOT part of the library API. Example-only.
 */

#pragma once

#include <Arduino.h>

#include "SHT3x/SHT3x.h"
#include "SHT3x/Status.h"
#include "examples/common/Log.h"

namespace diag {

inline const char* stateToString(SHT3x::DriverState state) {
  switch (state) {
    case SHT3x::DriverState::UNINIT: return "UNINIT";
    case SHT3x::DriverState::READY: return "READY";
    case SHT3x::DriverState::DEGRADED: return "DEGRADED";
    case SHT3x::DriverState::OFFLINE: return "OFFLINE";
    default: return "UNKNOWN";
  }
}

inline const char* errToString(SHT3x::Err err) {
  switch (err) {
    case SHT3x::Err::OK: return "OK";
    case SHT3x::Err::NOT_INITIALIZED: return "NOT_INITIALIZED";
    case SHT3x::Err::INVALID_CONFIG: return "INVALID_CONFIG";
    case SHT3x::Err::I2C_ERROR: return "I2C_ERROR";
    case SHT3x::Err::TIMEOUT: return "TIMEOUT";
    case SHT3x::Err::INVALID_PARAM: return "INVALID_PARAM";
    case SHT3x::Err::DEVICE_NOT_FOUND: return "DEVICE_NOT_FOUND";
    case SHT3x::Err::CRC_MISMATCH: return "CRC_MISMATCH";
    case SHT3x::Err::MEASUREMENT_NOT_READY: return "MEASUREMENT_NOT_READY";
    case SHT3x::Err::BUSY: return "BUSY";
    case SHT3x::Err::IN_PROGRESS: return "IN_PROGRESS";
    case SHT3x::Err::COMMAND_FAILED: return "COMMAND_FAILED";
    case SHT3x::Err::WRITE_CRC_ERROR: return "WRITE_CRC_ERROR";
    case SHT3x::Err::UNSUPPORTED: return "UNSUPPORTED";
    case SHT3x::Err::I2C_NACK_ADDR: return "I2C_NACK_ADDR";
    case SHT3x::Err::I2C_NACK_DATA: return "I2C_NACK_DATA";
    case SHT3x::Err::I2C_NACK_READ: return "I2C_NACK_READ";
    case SHT3x::Err::I2C_TIMEOUT: return "I2C_TIMEOUT";
    case SHT3x::Err::I2C_BUS: return "I2C_BUS";
    default: return "UNKNOWN";
  }
}

inline const char* stateColor(SHT3x::DriverState state) {
  switch (state) {
    case SHT3x::DriverState::READY: return LOG_COLOR_GREEN;
    case SHT3x::DriverState::DEGRADED: return LOG_COLOR_YELLOW;
    case SHT3x::DriverState::OFFLINE: return LOG_COLOR_RED;
    case SHT3x::DriverState::UNINIT: return LOG_COLOR_GRAY;
    default: return LOG_COLOR_RESET;
  }
}

inline const char* colorReset() { return LOG_COLOR_RESET; }

inline const char* boolColor(bool value) {
  return value ? LOG_COLOR_GREEN : LOG_COLOR_RED;
}

inline const char* successRateColor(float pct) {
  if (pct >= 99.9f) return LOG_COLOR_GREEN;
  if (pct >= 80.0f) return LOG_COLOR_YELLOW;
  return LOG_COLOR_RED;
}

inline const char* failureCountColor(uint32_t failures) {
  if (failures == 0U) return LOG_COLOR_GREEN;
  if (failures < 3U) return LOG_COLOR_YELLOW;
  return LOG_COLOR_RED;
}

inline const char* successCountColor(uint32_t successes) {
  return (successes > 0U) ? LOG_COLOR_GREEN : LOG_COLOR_GRAY;
}

inline const char* totalFailureColor(uint32_t failures) {
  return (failures == 0U) ? LOG_COLOR_GREEN : LOG_COLOR_RED;
}

inline void printHealthOneLine(SHT3x::SHT3x& driver) {
  SHT3x::DriverState st = driver.state();
  LOGI("Health: state=%s%s%s online=%s%s%s consecFail=%s%u%s ok=%s%lu%s fail=%s%lu%s",
       stateColor(st), stateToString(st), colorReset(),
       boolColor(driver.isOnline()), driver.isOnline() ? "true" : "false", colorReset(),
       failureCountColor(driver.consecutiveFailures()), driver.consecutiveFailures(), colorReset(),
       successCountColor(driver.totalSuccess()), static_cast<unsigned long>(driver.totalSuccess()), colorReset(),
       totalFailureColor(driver.totalFailures()), static_cast<unsigned long>(driver.totalFailures()), colorReset());
}

inline void printHealthVerbose(SHT3x::SHT3x& driver) {
  SHT3x::DriverState st = driver.state();
  SHT3x::Status lastErr = driver.lastError();
  uint32_t now = millis();

  const bool online = driver.isOnline();
  const uint32_t totalSuccess = driver.totalSuccess();
  const uint32_t totalFailures = driver.totalFailures();
  const uint32_t total = totalSuccess + totalFailures;
  const float successRate = (total > 0U) ? (100.0f * totalSuccess / total) : 0.0f;

  LOG_SERIAL.println();
  LOGI("=== Driver Health ===");
  LOGI("  State: %s%s%s", stateColor(st), stateToString(st), colorReset());
  LOGI("  Online: %s%s%s", boolColor(online), online ? "true" : "false", colorReset());
  LOGI("  Consecutive failures: %s%u%s",
       failureCountColor(driver.consecutiveFailures()),
       driver.consecutiveFailures(),
       colorReset());
  LOGI("  Total success: %s%lu%s",
       successCountColor(totalSuccess),
       static_cast<unsigned long>(totalSuccess),
       colorReset());
  LOGI("  Total failures: %s%lu%s",
       totalFailureColor(totalFailures),
       static_cast<unsigned long>(totalFailures),
       colorReset());
  LOGI("  Success rate: %s%.1f%%%s", successRateColor(successRate), successRate, colorReset());

  const uint32_t lastOk = driver.lastOkMs();
  const uint32_t lastFail = driver.lastErrorMs();
  if (lastOk > 0U) {
    LOGI("  Last success: %lu ms ago (at %lu ms)",
         static_cast<unsigned long>(now - lastOk),
         static_cast<unsigned long>(lastOk));
  } else {
    LOGI("  Last success: never");
  }
  if (lastFail > 0U) {
    LOGI("  Last failure: %lu ms ago (at %lu ms)",
         static_cast<unsigned long>(now - lastFail),
         static_cast<unsigned long>(lastFail));
  } else {
    LOGI("  Last failure: never");
  }

  if (lastErr.ok()) {
    LOGI("  Last error: %snone%s", LOG_COLOR_GREEN, colorReset());
  } else {
    LOGI("  Code: %s%s%s", LOG_COLOR_RED, errToString(lastErr.code), colorReset());
    LOGI("  Detail: %ld", static_cast<long>(lastErr.detail));
    LOGI("  Message: %s", lastErr.msg ? lastErr.msg : "(null)");
  }
  LOG_SERIAL.println();
}

struct HealthSnapshot {
  SHT3x::DriverState state = SHT3x::DriverState::UNINIT;
  uint8_t consecutiveFailures = 0;
  uint32_t totalSuccess = 0;
  uint32_t totalFailures = 0;
  uint32_t timestamp = 0;

  void capture(SHT3x::SHT3x& driver) {
    state = driver.state();
    consecutiveFailures = driver.consecutiveFailures();
    totalSuccess = driver.totalSuccess();
    totalFailures = driver.totalFailures();
    timestamp = millis();
  }
};

inline void printHealthDiff(const HealthSnapshot& before, const HealthSnapshot& after) {
  bool changed = false;

  if (before.state != after.state) {
    LOGI("  State: %s%s%s -> %s%s%s",
         stateColor(before.state), stateToString(before.state), colorReset(),
         stateColor(after.state), stateToString(after.state), colorReset());
    changed = true;
  }
  if (before.consecutiveFailures != after.consecutiveFailures) {
    const bool improved = after.consecutiveFailures < before.consecutiveFailures;
    const char* color = improved ? LOG_COLOR_GREEN : LOG_COLOR_RED;
    LOGI("  ConsecFail: %s%u -> %u%s",
         color,
         before.consecutiveFailures,
         after.consecutiveFailures,
         colorReset());
    changed = true;
  }
  if (before.totalSuccess != after.totalSuccess) {
    LOGI("  TotalOK: %lu -> %s%lu (+%lu)%s",
         static_cast<unsigned long>(before.totalSuccess),
         LOG_COLOR_GREEN,
         static_cast<unsigned long>(after.totalSuccess),
         static_cast<unsigned long>(after.totalSuccess - before.totalSuccess),
         colorReset());
    changed = true;
  }
  if (before.totalFailures != after.totalFailures) {
    LOGI("  TotalFail: %lu -> %s%lu (+%lu)%s",
         static_cast<unsigned long>(before.totalFailures),
         LOG_COLOR_RED,
         static_cast<unsigned long>(after.totalFailures),
         static_cast<unsigned long>(after.totalFailures - before.totalFailures),
         colorReset());
    changed = true;
  }
  if (!changed) {
    LOGI("  (no changes)");
  }
}

class HealthMonitor {
public:
  void begin(uint32_t intervalMs = 1000) {
    _intervalMs = intervalMs;
    _lastLogMs = 0;
    _lastState = SHT3x::DriverState::UNINIT;
    _lastConsecFail = 0;
  }

  void tick(SHT3x::SHT3x& driver, bool forceLog = false) {
    uint32_t now = millis();
    SHT3x::DriverState currentState = driver.state();
    uint8_t currentFail = driver.consecutiveFailures();

    const bool stateChanged = (currentState != _lastState);
    const bool failChanged = (currentFail != _lastConsecFail);
    const bool intervalElapsed = (_intervalMs > 0U) && ((now - _lastLogMs) >= _intervalMs);

    if (stateChanged || failChanged || intervalElapsed || forceLog) {
      if (stateChanged) {
        LOGI("[HEALTH] State transition: %s%s%s -> %s%s%s",
             stateColor(_lastState), stateToString(_lastState), colorReset(),
             stateColor(currentState), stateToString(currentState), colorReset());
      }
      if (intervalElapsed || forceLog) {
        printHealthOneLine(driver);
      }
      _lastState = currentState;
      _lastConsecFail = currentFail;
      _lastLogMs = now;
    }
  }

private:
  uint32_t _intervalMs = 1000;
  uint32_t _lastLogMs = 0;
  SHT3x::DriverState _lastState = SHT3x::DriverState::UNINIT;
  uint8_t _lastConsecFail = 0;
};

}  // namespace diag
