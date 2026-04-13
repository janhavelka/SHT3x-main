/// @file main.cpp
/// @brief Basic bringup example for SHT3x
/// @note This is an EXAMPLE, not part of the library

#include <Arduino.h>
#include <limits>
#include <cstdlib>
#include "common/Log.h"
#include "common/BoardConfig.h"
#include "common/I2cTransport.h"
#include "common/I2cScanner.h"

#include "SHT3x/SHT3x.h"

// ============================================================================
// Globals
// ============================================================================

struct StressStats {
  bool active = false;
  uint32_t startMs = 0;
  uint32_t endMs = 0;
  int target = 0;
  int attempts = 0;
  int success = 0;
  uint32_t errors = 0;
  bool hasSample = false;
  float minTemp = 0.0f;
  float maxTemp = 0.0f;
  float minHumidity = 0.0f;
  float maxHumidity = 0.0f;
  double sumTemp = 0.0;
  double sumHumidity = 0.0;
  SHT3x::Status lastError = SHT3x::Status::Ok();
};

SHT3x::SHT3x device;
SHT3x::Config gConfig;
bool gConfigReady = false;
bool verboseMode = false;
bool pendingRead = false;
uint32_t pendingStartMs = 0;
int stressRemaining = 0;
StressStats stressStats;
static constexpr uint32_t STRESS_PROGRESS_UPDATES = 10U;

void cancelPending();

// ============================================================================
// Helper Functions
// ============================================================================

const char* errToStr(SHT3x::Err err) {
  using namespace SHT3x;
  switch (err) {
    case Err::OK: return "OK";
    case Err::NOT_INITIALIZED: return "NOT_INITIALIZED";
    case Err::INVALID_CONFIG: return "INVALID_CONFIG";
    case Err::I2C_ERROR: return "I2C_ERROR";
    case Err::TIMEOUT: return "TIMEOUT";
    case Err::INVALID_PARAM: return "INVALID_PARAM";
    case Err::DEVICE_NOT_FOUND: return "DEVICE_NOT_FOUND";
    case Err::CRC_MISMATCH: return "CRC_MISMATCH";
    case Err::MEASUREMENT_NOT_READY: return "MEASUREMENT_NOT_READY";
    case Err::BUSY: return "BUSY";
    case Err::IN_PROGRESS: return "IN_PROGRESS";
    case Err::COMMAND_FAILED: return "COMMAND_FAILED";
    case Err::WRITE_CRC_ERROR: return "WRITE_CRC_ERROR";
    case Err::UNSUPPORTED: return "UNSUPPORTED";
    case Err::I2C_NACK_ADDR: return "I2C_NACK_ADDR";
    case Err::I2C_NACK_DATA: return "I2C_NACK_DATA";
    case Err::I2C_NACK_READ: return "I2C_NACK_READ";
    case Err::I2C_TIMEOUT: return "I2C_TIMEOUT";
    case Err::I2C_BUS: return "I2C_BUS";
    default: return "UNKNOWN";
  }
}

const char* stateToStr(SHT3x::DriverState st) {
  using namespace SHT3x;
  switch (st) {
    case DriverState::UNINIT: return "UNINIT";
    case DriverState::READY: return "READY";
    case DriverState::DEGRADED: return "DEGRADED";
    case DriverState::OFFLINE: return "OFFLINE";
    default: return "UNKNOWN";
  }
}

const char* stateColor(SHT3x::DriverState st, bool online, uint8_t consecutiveFailures) {
  if (st == SHT3x::DriverState::UNINIT) {
    return LOG_COLOR_RESET;
  }
  return LOG_COLOR_STATE(online, consecutiveFailures);
}

const char* goodIfZeroColor(uint32_t value) {
  return (value == 0U) ? LOG_COLOR_GREEN : LOG_COLOR_RED;
}

const char* goodIfNonZeroColor(uint32_t value) {
  return (value > 0U) ? LOG_COLOR_GREEN : LOG_COLOR_YELLOW;
}

const char* onOffColor(bool enabled) {
  return enabled ? LOG_COLOR_GREEN : LOG_COLOR_RESET;
}

const char* skipCountColor(uint32_t value) {
  return (value > 0U) ? LOG_COLOR_YELLOW : LOG_COLOR_RESET;
}

const char* successRateColor(float pct) {
  if (pct >= 99.9f) return LOG_COLOR_GREEN;
  if (pct >= 80.0f) return LOG_COLOR_YELLOW;
  return LOG_COLOR_RED;
}

uint32_t stressProgressStep(uint32_t total) {
  if (total == 0U) {
    return 0U;
  }
  const uint32_t step = total / STRESS_PROGRESS_UPDATES;
  return (step == 0U) ? 1U : step;
}

void printStressProgress(uint32_t completed, uint32_t total, uint32_t okCount, uint32_t failCount) {
  if (completed == 0U || total == 0U) {
    return;
  }
  const uint32_t step = stressProgressStep(total);
  if (step == 0U || (completed != total && (completed % step) != 0U)) {
    return;
  }
  const float pct = (100.0f * static_cast<float>(completed)) / static_cast<float>(total);
  Serial.printf("  Progress: %lu/%lu (%s%.0f%%%s, ok=%s%lu%s, fail=%s%lu%s)\n",
                static_cast<unsigned long>(completed),
                static_cast<unsigned long>(total),
                successRateColor(pct),
                pct,
                LOG_COLOR_RESET,
                goodIfNonZeroColor(okCount),
                static_cast<unsigned long>(okCount),
                LOG_COLOR_RESET,
                goodIfZeroColor(failCount),
                static_cast<unsigned long>(failCount),
                LOG_COLOR_RESET);
}

const char* modeToStr(SHT3x::Mode mode) {
  using namespace SHT3x;
  switch (mode) {
    case Mode::SINGLE_SHOT: return "SINGLE_SHOT";
    case Mode::PERIODIC: return "PERIODIC";
    case Mode::ART: return "ART";
    default: return "UNKNOWN";
  }
}

const char* repToStr(SHT3x::Repeatability rep) {
  using namespace SHT3x;
  switch (rep) {
    case Repeatability::LOW_REPEATABILITY: return "LOW";
    case Repeatability::MEDIUM_REPEATABILITY: return "MEDIUM";
    case Repeatability::HIGH_REPEATABILITY: return "HIGH";
    default: return "UNKNOWN";
  }
}

const char* rateToStr(SHT3x::PeriodicRate rate) {
  using namespace SHT3x;
  switch (rate) {
    case PeriodicRate::MPS_0_5: return "0.5";
    case PeriodicRate::MPS_1: return "1";
    case PeriodicRate::MPS_2: return "2";
    case PeriodicRate::MPS_4: return "4";
    case PeriodicRate::MPS_10: return "10";
    default: return "UNKNOWN";
  }
}

const char* stretchToStr(SHT3x::ClockStretching stretch) {
  return (stretch == SHT3x::ClockStretching::STRETCH_ENABLED) ? "ENABLED" : "DISABLED";
}

const char* alertKindToStr(SHT3x::AlertLimitKind kind) {
  using namespace SHT3x;
  switch (kind) {
    case AlertLimitKind::HIGH_SET: return "HIGH_SET";
    case AlertLimitKind::HIGH_CLEAR: return "HIGH_CLEAR";
    case AlertLimitKind::LOW_CLEAR: return "LOW_CLEAR";
    case AlertLimitKind::LOW_SET: return "LOW_SET";
    default: return "UNKNOWN";
  }
}

void printBytes(const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0) {
    Serial.println("Bytes: <empty>");
    return;
  }

  Serial.print("Bytes:");
  for (size_t i = 0; i < len; ++i) {
    Serial.printf(" %02X", static_cast<unsigned>(data[i]));
  }
  Serial.println();
}

void printStatus(const SHT3x::Status& st) {
  Serial.printf("  Status: %s%s%s (code=%u, detail=%ld)\n",
                LOG_COLOR_RESULT(st.ok()),
                errToStr(st.code),
                LOG_COLOR_RESET,
                static_cast<unsigned>(st.code),
                static_cast<long>(st.detail));
  if (st.msg && st.msg[0]) {
    Serial.printf("  Message: %s%s%s\n", LOG_COLOR_YELLOW, st.msg, LOG_COLOR_RESET);
  }
}

void printDriverHealth() {
  const uint32_t now = millis();
  const uint32_t totalOk = device.totalSuccess();
  const uint32_t totalFail = device.totalFailures();
  const uint32_t total = totalOk + totalFail;
  const float successRate = (total > 0U)
                                ? (100.0f * static_cast<float>(totalOk) / static_cast<float>(total))
                                : 0.0f;
  const SHT3x::Status lastErr = device.lastError();
  const SHT3x::DriverState st = device.state();
  const bool online = device.isOnline();

  Serial.println("=== Driver Health ===");
  Serial.printf("  State: %s%s%s\n",
                stateColor(st, online, device.consecutiveFailures()),
                stateToStr(st),
                LOG_COLOR_RESET);
  Serial.printf("  Online: %s%s%s\n",
                online ? LOG_COLOR_GREEN : LOG_COLOR_RED,
                log_bool_str(online),
                LOG_COLOR_RESET);
  Serial.printf("  Consecutive failures: %s%u%s\n",
                goodIfZeroColor(device.consecutiveFailures()),
                device.consecutiveFailures(),
                LOG_COLOR_RESET);
  Serial.printf("  Total success: %s%lu%s\n",
                goodIfNonZeroColor(totalOk),
                static_cast<unsigned long>(totalOk),
                LOG_COLOR_RESET);
  Serial.printf("  Total failures: %s%lu%s\n",
                goodIfZeroColor(totalFail),
                static_cast<unsigned long>(totalFail),
                LOG_COLOR_RESET);
  Serial.printf("  Success rate: %s%.1f%%%s\n",
                successRateColor(successRate),
                successRate,
                LOG_COLOR_RESET);

  const uint32_t lastOkMs = device.lastOkMs();
  if (lastOkMs > 0U) {
    Serial.printf("  Last OK: %lu ms ago (at %lu ms)\n",
                  static_cast<unsigned long>(now - lastOkMs),
                  static_cast<unsigned long>(lastOkMs));
  } else {
    Serial.println("  Last OK: never");
  }

  const uint32_t lastErrorMs = device.lastErrorMs();
  if (lastErrorMs > 0U) {
    Serial.printf("  Last error: %lu ms ago (at %lu ms)\n",
                  static_cast<unsigned long>(now - lastErrorMs),
                  static_cast<unsigned long>(lastErrorMs));
  } else {
    Serial.println("  Last error: never");
  }

  if (!lastErr.ok()) {
    Serial.printf("  Error code: %s%s%s\n",
                  LOG_COLOR_RED,
                  errToStr(lastErr.code),
                  LOG_COLOR_RESET);
    Serial.printf("  Error detail: %ld\n", static_cast<long>(lastErr.detail));
    if (lastErr.msg && lastErr.msg[0]) {
      Serial.printf("  Error msg: %s\n", lastErr.msg);
    }
  }
}

void printMeasurement(const SHT3x::Measurement& m) {
  Serial.printf("Temp: %.2f C, Humidity: %.2f %%\n", m.temperatureC, m.humidityPct);
}

void printRawSample(const SHT3x::RawSample& s) {
  Serial.printf("Raw: T=0x%04X RH=0x%04X\n",
                static_cast<unsigned>(s.rawTemperature),
                static_cast<unsigned>(s.rawHumidity));
}

void printCompSample(const SHT3x::CompensatedSample& s) {
  Serial.printf("Comp: T=%ld (x100), RH=%lu (x100)\n",
                static_cast<long>(s.tempC_x100),
                static_cast<unsigned long>(s.humidityPct_x100));
}

void printVerboseState() {
  Serial.printf("  Verbose: %s%s%s\n",
                onOffColor(verboseMode),
                verboseMode ? "ON" : "OFF",
                LOG_COLOR_RESET);
}

void printConfig() {
  SHT3x::SettingsSnapshot snap;
  SHT3x::Status st = device.getSettings(snap);
  if (!st.ok()) {
    printStatus(st);
    return;
  }

  Serial.println("=== Config ===");
  Serial.printf("  Initialized: %s\n", snap.initialized ? "true" : "false");
  Serial.printf("  State: %s\n", stateToStr(snap.state));
  Serial.printf("  I2C address: 0x%02X\n", snap.i2cAddress);
  Serial.printf("  I2C timeout: %lu ms\n", static_cast<unsigned long>(snap.i2cTimeoutMs));
  Serial.printf("  Offline threshold: %u\n", static_cast<unsigned>(snap.offlineThreshold));
  Serial.printf("  Has nowMs hook: %s\n", snap.hasNowMsHook ? "true" : "false");
  Serial.printf("  Mode: %s\n", modeToStr(snap.mode));
  Serial.printf("  Repeatability: %s\n", repToStr(snap.repeatability));
  Serial.printf("  Periodic rate: %s mps\n", rateToStr(snap.periodicRate));
  Serial.printf("  Clock stretching: %s\n", stretchToStr(snap.clockStretching));
  Serial.printf("  Est. meas time: %lu ms\n",
                static_cast<unsigned long>(device.estimateMeasurementTimeMs()));
  printVerboseState();
}

void printRuntimeStats() {
  const uint32_t now = millis();
  Serial.println("=== Runtime Stats ===");
  Serial.printf("  periodicActive: %s\n", device.isPeriodicActive() ? "true" : "false");
  Serial.printf("  measurementReady: %s\n", device.measurementReady() ? "true" : "false");
  Serial.printf("  hasCachedSettings: %s\n", device.hasCachedSettings() ? "true" : "false");
  Serial.printf("  lastBusActivityMs: %lu\n",
                static_cast<unsigned long>(device.lastBusActivityMs()));
  Serial.printf("  sampleTimestampMs: %lu\n",
                static_cast<unsigned long>(device.sampleTimestampMs()));
  Serial.printf("  sampleAgeMs: %lu\n",
                static_cast<unsigned long>(device.sampleAgeMs(now)));
  Serial.printf("  notReadyCount: %lu\n",
                static_cast<unsigned long>(device.notReadyCount()));
  Serial.printf("  missedSamplesEstimate: %lu\n",
                static_cast<unsigned long>(device.missedSamplesEstimate()));

  if (device.hasCachedSettings()) {
    const SHT3x::CachedSettings cached = device.getCachedSettings();
    Serial.println("  Cached settings:");
    Serial.printf("    mode: %s\n", modeToStr(cached.mode));
    Serial.printf("    repeatability: %s\n", repToStr(cached.repeatability));
    Serial.printf("    periodicRate: %s mps\n", rateToStr(cached.periodicRate));
    Serial.printf("    stretching: %s\n", stretchToStr(cached.clockStretching));
    Serial.printf("    heaterEnabled: %s\n", cached.heaterEnabled ? "true" : "false");
  }
}

void resetStressStats(int target) {
  stressStats.active = true;
  stressStats.startMs = millis();
  stressStats.endMs = 0;
  stressStats.target = target;
  stressStats.attempts = 0;
  stressStats.success = 0;
  stressStats.errors = 0;
  stressStats.hasSample = false;
  stressStats.minTemp = std::numeric_limits<float>::max();
  stressStats.maxTemp = std::numeric_limits<float>::lowest();
  stressStats.minHumidity = std::numeric_limits<float>::max();
  stressStats.maxHumidity = std::numeric_limits<float>::lowest();
  stressStats.sumTemp = 0.0;
  stressStats.sumHumidity = 0.0;
  stressStats.lastError = SHT3x::Status::Ok();
}

void noteStressError(const SHT3x::Status& st) {
  stressStats.errors++;
  stressStats.lastError = st;
}

void updateStressStats(const SHT3x::Measurement& m) {
  if (!stressStats.hasSample) {
    stressStats.minTemp = m.temperatureC;
    stressStats.maxTemp = m.temperatureC;
    stressStats.minHumidity = m.humidityPct;
    stressStats.maxHumidity = m.humidityPct;
    stressStats.hasSample = true;
  } else {
    if (m.temperatureC < stressStats.minTemp) {
      stressStats.minTemp = m.temperatureC;
    }
    if (m.temperatureC > stressStats.maxTemp) {
      stressStats.maxTemp = m.temperatureC;
    }
    if (m.humidityPct < stressStats.minHumidity) {
      stressStats.minHumidity = m.humidityPct;
    }
    if (m.humidityPct > stressStats.maxHumidity) {
      stressStats.maxHumidity = m.humidityPct;
    }
  }

  stressStats.sumTemp += m.temperatureC;
  stressStats.sumHumidity += m.humidityPct;
  stressStats.success++;
}

void finishStressStats() {
  stressStats.active = false;
  stressStats.endMs = millis();
  const uint32_t durationMs = stressStats.endMs - stressStats.startMs;
  const float successPct =
      (stressStats.attempts > 0)
          ? (100.0f * static_cast<float>(stressStats.success) /
             static_cast<float>(stressStats.attempts))
          : 0.0f;

  Serial.println("=== Stress Summary ===");
  Serial.printf("  Target: %d\n", stressStats.target);
  Serial.printf("  Attempts: %d\n", stressStats.attempts);
  Serial.printf("  Success: %s%d%s\n",
                goodIfNonZeroColor(static_cast<uint32_t>(stressStats.success)),
                stressStats.success,
                LOG_COLOR_RESET);
  Serial.printf("  Errors: %s%lu%s\n",
                goodIfZeroColor(stressStats.errors),
                static_cast<unsigned long>(stressStats.errors),
                LOG_COLOR_RESET);
  Serial.printf("  Success rate: %s%.2f%%%s\n",
                successRateColor(successPct),
                successPct,
                LOG_COLOR_RESET);
  Serial.printf("  Duration: %lu ms\n", static_cast<unsigned long>(durationMs));
  if (durationMs > 0) {
    const float rate = 1000.0f * static_cast<float>(stressStats.attempts) /
                       static_cast<float>(durationMs);
    Serial.printf("  Rate: %.2f samples/s\n", rate);
  }

  if (stressStats.success > 0) {
    const float avgTemp = static_cast<float>(stressStats.sumTemp / stressStats.success);
    const float avgHumidity = static_cast<float>(stressStats.sumHumidity / stressStats.success);
    Serial.printf("  Temp C: min=%.2f avg=%.2f max=%.2f\n",
                  stressStats.minTemp, avgTemp, stressStats.maxTemp);
    Serial.printf("  Humidity %%: min=%.2f avg=%.2f max=%.2f\n",
                  stressStats.minHumidity, avgHumidity, stressStats.maxHumidity);
  } else {
    Serial.println("  No valid samples");
  }

  if (!stressStats.lastError.ok()) {
    Serial.printf("  Last error: %s\n", errToStr(stressStats.lastError.code));
    if (stressStats.lastError.msg && stressStats.lastError.msg[0]) {
      Serial.printf("  Message: %s\n", stressStats.lastError.msg);
    }
  }
}

SHT3x::Status performMeasurementBlocking(SHT3x::Measurement& out, uint32_t timeoutMs = 500) {
  SHT3x::Status st = device.requestMeasurement();
  if (st.code != SHT3x::Err::IN_PROGRESS && st.code != SHT3x::Err::BUSY) {
    return st;
  }

  const uint32_t start = millis();
  while ((millis() - start) < timeoutMs) {
    device.tick(millis());
    if (device.measurementReady()) {
      return device.getMeasurement(out);
    }
    delay(1);
  }
  return SHT3x::Status::Error(SHT3x::Err::TIMEOUT, "measurement timeout", timeoutMs);
}

void runStressMix(int count) {
  struct OpStats {
    const char* name;
    uint32_t ok;
    uint32_t fail;
  };

  OpStats stats[] = {
      {"measure", 0, 0},
      {"readStatus", 0, 0},
      {"readSerial", 0, 0},
      {"setRepeat", 0, 0},
      {"setRate", 0, 0},
      {"setStretch", 0, 0},
      {"heaterStat", 0, 0},
  };
  const int opCount = static_cast<int>(sizeof(stats) / sizeof(stats[0]));

  cancelPending();
  const uint32_t succBefore = device.totalSuccess();
  const uint32_t failBefore = device.totalFailures();
  const uint32_t startMs = millis();
  uint32_t okTotal = 0;
  uint32_t failTotal = 0;

  for (int i = 0; i < count; ++i) {
    const int op = i % opCount;
    SHT3x::Status st = SHT3x::Status::Ok();

    switch (op) {
      case 0: {
        SHT3x::Measurement m;
        st = performMeasurementBlocking(m);
        break;
      }
      case 1: {
        SHT3x::StatusRegister reg;
        st = device.readStatus(reg);
        break;
      }
      case 2: {
        uint32_t serial = 0;
        st = device.readSerialNumber(serial, SHT3x::ClockStretching::STRETCH_DISABLED);
        break;
      }
      case 3: {
        st = device.setRepeatability(static_cast<SHT3x::Repeatability>((i / opCount) % 3));
        break;
      }
      case 4: {
        st = device.setPeriodicRate(static_cast<SHT3x::PeriodicRate>((i / opCount) % 5));
        break;
      }
      case 5: {
        st = device.setClockStretching(((i / opCount) % 2) ?
                                           SHT3x::ClockStretching::STRETCH_ENABLED :
                                           SHT3x::ClockStretching::STRETCH_DISABLED);
        break;
      }
      case 6: {
        bool enabled = false;
        st = device.readHeaterStatus(enabled);
        break;
      }
      default:
        break;
    }

    if (st.ok()) {
      stats[op].ok++;
      okTotal++;
    } else {
      stats[op].fail++;
      failTotal++;
      if (verboseMode) {
        Serial.printf("  [%d] %s failed: %s\n", i, stats[op].name, errToStr(st.code));
      }
    }

    printStressProgress(static_cast<uint32_t>(i + 1),
                        static_cast<uint32_t>(count),
                        okTotal,
                        failTotal);
  }

  const uint32_t elapsed = millis() - startMs;

  Serial.println("=== stress_mix summary ===");
  const float successPct =
      (count > 0) ? (100.0f * static_cast<float>(okTotal) / static_cast<float>(count)) : 0.0f;
  Serial.printf("  Total: %sok=%lu%s %sfail=%lu%s (%s%.2f%%%s)\n",
                goodIfNonZeroColor(okTotal),
                static_cast<unsigned long>(okTotal),
                LOG_COLOR_RESET,
                goodIfZeroColor(failTotal),
                static_cast<unsigned long>(failTotal),
                LOG_COLOR_RESET,
                successRateColor(successPct),
                successPct,
                LOG_COLOR_RESET);
  Serial.printf("  Duration: %lu ms\n", static_cast<unsigned long>(elapsed));
  if (elapsed > 0) {
    Serial.printf("  Rate: %.2f ops/s\n", (1000.0f * static_cast<float>(count)) / elapsed);
  }
  for (int i = 0; i < opCount; ++i) {
    Serial.printf("  %-10s %sok=%lu%s %sfail=%lu%s\n",
                  stats[i].name,
                  goodIfNonZeroColor(stats[i].ok),
                  static_cast<unsigned long>(stats[i].ok),
                  LOG_COLOR_RESET,
                  goodIfZeroColor(stats[i].fail),
                  static_cast<unsigned long>(stats[i].fail),
                  LOG_COLOR_RESET);
  }
  const uint32_t successDelta = device.totalSuccess() - succBefore;
  const uint32_t failDelta = device.totalFailures() - failBefore;
  Serial.printf("  Health delta: %ssuccess +%lu%s, %sfailures +%lu%s\n",
                goodIfNonZeroColor(successDelta),
                static_cast<unsigned long>(successDelta),
                LOG_COLOR_RESET,
                goodIfZeroColor(failDelta),
                static_cast<unsigned long>(failDelta),
                LOG_COLOR_RESET);
}

void runSelfTest() {
  struct Result {
    uint32_t pass = 0;
    uint32_t fail = 0;
    uint32_t skip = 0;
  } result;

  enum class SelftestOutcome : uint8_t { PASS, FAIL, SKIP };
  auto report = [&](const char* name, SelftestOutcome outcome, const char* note) {
    const bool ok = (outcome == SelftestOutcome::PASS);
    const bool skip = (outcome == SelftestOutcome::SKIP);
    const char* color = skip ? LOG_COLOR_YELLOW : LOG_COLOR_RESULT(ok);
    const char* tag = skip ? "SKIP" : (ok ? "PASS" : "FAIL");
    Serial.printf("  [%s%s%s] %s", color, tag, LOG_COLOR_RESET, name);
    if (note && note[0]) {
      Serial.printf(" - %s", note);
    }
    Serial.println();
    if (skip) {
      result.skip++;
    } else if (ok) {
      result.pass++;
    } else {
      result.fail++;
    }
  };
  auto reportCheck = [&](const char* name, bool ok, const char* note) {
    report(name, ok ? SelftestOutcome::PASS : SelftestOutcome::FAIL, note);
  };
  auto reportSkip = [&](const char* name, const char* note) {
    report(name, SelftestOutcome::SKIP, note);
  };

  Serial.println("=== SHT3x selftest (safe commands) ===");
  cancelPending();

  SHT3x::SettingsSnapshot baseline;
  bool haveBaseline = device.readSettings(baseline).ok();
  reportCheck("capture baseline settings", haveBaseline, haveBaseline ? "" : "readSettings failed");

  const uint32_t succBefore = device.totalSuccess();
  const uint32_t failBefore = device.totalFailures();
  const uint8_t consBefore = device.consecutiveFailures();

  SHT3x::Status st = device.probe();
  if (st.code == SHT3x::Err::NOT_INITIALIZED) {
    reportSkip("probe responds", "driver not initialized");
    reportSkip("remaining checks", "selftest aborted");
    Serial.printf("Selftest result: pass=%s%lu%s fail=%s%lu%s skip=%s%lu%s\n",
                  goodIfNonZeroColor(result.pass), static_cast<unsigned long>(result.pass), LOG_COLOR_RESET,
                  goodIfZeroColor(result.fail), static_cast<unsigned long>(result.fail), LOG_COLOR_RESET,
                  skipCountColor(result.skip), static_cast<unsigned long>(result.skip), LOG_COLOR_RESET);
    return;
  }
  reportCheck("probe responds", st.ok(), st.ok() ? "" : errToStr(st.code));
  const bool probeNoTrack = device.totalSuccess() == succBefore &&
                            device.totalFailures() == failBefore &&
                            device.consecutiveFailures() == consBefore;
  reportCheck("probe no-health-side-effects", probeNoTrack, "");

  st = device.setMode(SHT3x::Mode::SINGLE_SHOT);
  reportCheck("setMode(SINGLE_SHOT)", st.ok(), st.ok() ? "" : errToStr(st.code));
  SHT3x::Mode mode = SHT3x::Mode::SINGLE_SHOT;
  st = device.getMode(mode);
  reportCheck("getMode", st.ok(), st.ok() ? "" : errToStr(st.code));
  reportCheck("verify mode", st.ok() && mode == SHT3x::Mode::SINGLE_SHOT, "");

  st = device.setRepeatability(SHT3x::Repeatability::HIGH_REPEATABILITY);
  reportCheck("setRepeatability(HIGH)", st.ok(), st.ok() ? "" : errToStr(st.code));
  SHT3x::Repeatability rep = SHT3x::Repeatability::LOW_REPEATABILITY;
  st = device.getRepeatability(rep);
  reportCheck("verify repeatability", st.ok() && rep == SHT3x::Repeatability::HIGH_REPEATABILITY,
         st.ok() ? "" : errToStr(st.code));

  st = device.setPeriodicRate(SHT3x::PeriodicRate::MPS_1);
  reportCheck("setPeriodicRate(1mps)", st.ok(), st.ok() ? "" : errToStr(st.code));
  SHT3x::PeriodicRate rate = SHT3x::PeriodicRate::MPS_0_5;
  st = device.getPeriodicRate(rate);
  reportCheck("verify periodic rate", st.ok() && rate == SHT3x::PeriodicRate::MPS_1,
         st.ok() ? "" : errToStr(st.code));

  st = device.setClockStretching(SHT3x::ClockStretching::STRETCH_ENABLED);
  reportCheck("setClockStretching(ON)", st.ok(), st.ok() ? "" : errToStr(st.code));
  SHT3x::ClockStretching stretch = SHT3x::ClockStretching::STRETCH_DISABLED;
  st = device.getClockStretching(stretch);
  reportCheck("verify stretching", st.ok() && stretch == SHT3x::ClockStretching::STRETCH_ENABLED,
         st.ok() ? "" : errToStr(st.code));

  uint16_t statusRaw = 0;
  st = device.readStatus(statusRaw);
  reportCheck("readStatus(raw)", st.ok(), st.ok() ? "" : errToStr(st.code));

  bool heaterOn = false;
  st = device.readHeaterStatus(heaterOn);
  reportCheck("readHeaterStatus", st.ok(), st.ok() ? "" : errToStr(st.code));

  SHT3x::Measurement m;
  st = performMeasurementBlocking(m);
  reportCheck("measurement cycle", st.ok(), st.ok() ? "" : errToStr(st.code));
  const bool mRange = (m.temperatureC > -60.0f && m.temperatureC < 130.0f) &&
                      (m.humidityPct >= 0.0f && m.humidityPct <= 100.0f);
  reportCheck("measurement in plausible range", st.ok() && mRange, "");

  st = device.softReset();
  reportCheck("softReset", st.ok(), st.ok() ? "" : errToStr(st.code));

  st = device.recover();
  reportCheck("recover", st.ok(), st.ok() ? "" : errToStr(st.code));
  reportCheck("isOnline", device.isOnline(), "");

  if (haveBaseline) {
    device.setMode(baseline.mode);
    device.setRepeatability(baseline.repeatability);
    device.setPeriodicRate(baseline.periodicRate);
    device.setClockStretching(baseline.clockStretching);
  }

  Serial.printf("Selftest result: pass=%s%lu%s fail=%s%lu%s skip=%s%lu%s\n",
                goodIfNonZeroColor(result.pass), static_cast<unsigned long>(result.pass), LOG_COLOR_RESET,
                goodIfZeroColor(result.fail), static_cast<unsigned long>(result.fail), LOG_COLOR_RESET,
                skipCountColor(result.skip), static_cast<unsigned long>(result.skip), LOG_COLOR_RESET);
}

void cancelPending() {
  pendingRead = false;
  stressRemaining = 0;
  stressStats.active = false;
}

SHT3x::Status scheduleMeasurement() {
  SHT3x::Status st = device.requestMeasurement();
  if (st.code == SHT3x::Err::IN_PROGRESS) {
    pendingRead = true;
    pendingStartMs = millis();
    if (verboseMode && !stressStats.active) {
      Serial.printf("Measurement requested at %lu ms\n",
                    static_cast<unsigned long>(pendingStartMs));
    }
  }
  return st;
}

void handleMeasurementReady() {
  if (!pendingRead || !device.measurementReady()) {
    return;
  }

  SHT3x::Measurement m;
  SHT3x::Status st = device.getMeasurement(m);
  pendingRead = false;

  if (!st.ok()) {
    if (stressStats.active) {
      noteStressError(st);
      stressStats.attempts++;
      if (stressRemaining > 0) {
        stressRemaining--;
      }
      printStressProgress(static_cast<uint32_t>(stressStats.attempts),
                          static_cast<uint32_t>(stressStats.target),
                          static_cast<uint32_t>(stressStats.success),
                          stressStats.errors);
      if (stressRemaining == 0 && stressStats.active) {
        finishStressStats();
      }
    } else {
      printStatus(st);
    }
    return;
  }

  if (stressStats.active) {
    updateStressStats(m);
    stressStats.attempts++;
    if (stressRemaining > 0) {
      stressRemaining--;
    }
    printStressProgress(static_cast<uint32_t>(stressStats.attempts),
                        static_cast<uint32_t>(stressStats.target),
                        static_cast<uint32_t>(stressStats.success),
                        stressStats.errors);
    if (stressRemaining == 0 && stressStats.active) {
      finishStressStats();
    }
    return;
  }

  printMeasurement(m);
}

bool parseRepeatability(const String& token, SHT3x::Repeatability& out) {
  String t = token;
  t.toLowerCase();
  if (t == "low") {
    out = SHT3x::Repeatability::LOW_REPEATABILITY;
    return true;
  }
  if (t == "med" || t == "medium") {
    out = SHT3x::Repeatability::MEDIUM_REPEATABILITY;
    return true;
  }
  if (t == "high") {
    out = SHT3x::Repeatability::HIGH_REPEATABILITY;
    return true;
  }
  return false;
}

bool parseRate(const String& token, SHT3x::PeriodicRate& out) {
  if (token == "0.5" || token == "0_5") {
    out = SHT3x::PeriodicRate::MPS_0_5;
    return true;
  }
  if (token == "1") {
    out = SHT3x::PeriodicRate::MPS_1;
    return true;
  }
  if (token == "2") {
    out = SHT3x::PeriodicRate::MPS_2;
    return true;
  }
  if (token == "4") {
    out = SHT3x::PeriodicRate::MPS_4;
    return true;
  }
  if (token == "10") {
    out = SHT3x::PeriodicRate::MPS_10;
    return true;
  }
  return false;
}

bool parseStretch(const String& token, SHT3x::ClockStretching& out) {
  if (token == "1" || token == "on" || token == "enable") {
    out = SHT3x::ClockStretching::STRETCH_ENABLED;
    return true;
  }
  if (token == "0" || token == "off" || token == "disable") {
    out = SHT3x::ClockStretching::STRETCH_DISABLED;
    return true;
  }
  return false;
}

bool parseAlertKind(const String& token, SHT3x::AlertLimitKind& out) {
  String t = token;
  t.toLowerCase();
  if (t == "hs" || t == "high_set" || t == "highset") {
    out = SHT3x::AlertLimitKind::HIGH_SET;
    return true;
  }
  if (t == "hc" || t == "high_clear" || t == "highclear") {
    out = SHT3x::AlertLimitKind::HIGH_CLEAR;
    return true;
  }
  if (t == "lc" || t == "low_clear" || t == "lowclear") {
    out = SHT3x::AlertLimitKind::LOW_CLEAR;
    return true;
  }
  if (t == "ls" || t == "low_set" || t == "lowset") {
    out = SHT3x::AlertLimitKind::LOW_SET;
    return true;
  }
  return false;
}

bool parseU16(const String& token, uint16_t& out) {
  const char* str = token.c_str();
  char* end = nullptr;
  unsigned long value = std::strtoul(str, &end, 0);
  if (end == str || *end != '\0') {
    return false;
  }
  if (value > 0xFFFFUL) {
    return false;
  }
  out = static_cast<uint16_t>(value);
  return true;
}

void printHelp() {
  auto helpSection = [](const char* title) {
    Serial.printf("\n%s[%s]%s\n", LOG_COLOR_GREEN, title, LOG_COLOR_RESET);
  };
  auto helpItem = [](const char* cmd, const char* desc) {
    Serial.printf("  %s%-32s%s - %s\n", LOG_COLOR_CYAN, cmd, LOG_COLOR_RESET, desc);
  };

  Serial.println();
  Serial.printf("%s=== SHT3x CLI Help ===%s\n", LOG_COLOR_CYAN, LOG_COLOR_RESET);
  helpSection("Common");
  helpItem("help / ?", "Show this help");
  helpItem("version / ver", "Print firmware and library version info");
  helpItem("scan", "Scan I2C bus");
  helpItem("read", "Request measurement (single-shot or periodic)");
  helpItem("raw", "Print last raw sample");
  helpItem("comp", "Print last compensated sample");
  helpItem("meastime", "Show estimated measurement time");

  helpSection("Operating Mode");
  helpItem("mode [single|periodic|art]", "Set or show operating mode");
  helpItem("start_periodic <rate> <rep>", "Start periodic mode");
  helpItem("start_art", "Start ART mode");
  helpItem("stop_periodic", "Stop periodic or ART mode");
  helpItem("repeat [low|med|high]", "Set or show repeatability");
  helpItem("rate [0.5|1|2|4|10]", "Set or show periodic rate");
  helpItem("stretch [0|1]", "Set or show clock stretching");

  helpSection("Status And Alerts");
  helpItem("status", "Read status register");
  helpItem("status_raw", "Read raw status (16-bit)");
  helpItem("clearstatus", "Clear status flags");
  helpItem("heater [on|off|status]", "Control heater");
  helpItem("serial [stretch|nostretch]", "Read serial number");
  helpItem("command write <hex>", "Issue a raw 16-bit command");
  helpItem("command write_data <cmd> <data>", "Issue a command with a packed data word");
  helpItem("command read <cmd> <len>", "Issue a command and read raw response bytes");
  helpItem("alert read <hs|hc|lc|ls>", "Read alert limit");
  helpItem("alert write <kind> <T> <RH>", "Write alert limit");
  helpItem("alert raw read <kind>", "Read raw alert limit word");
  helpItem("alert raw write <kind> <hex>", "Write raw alert limit word");
  helpItem("alert encode <T> <RH>", "Encode alert limit word");
  helpItem("alert decode <hex>", "Decode alert limit word");
  helpItem("alert disable", "Disable alerts (LowSet > HighSet)");
  helpItem("convert <rawT> <rawRH>", "Convert raw values");

  helpSection("Lifecycle And Diagnostics");
  helpItem("reset", "Soft reset device");
  helpItem("defaults", "Reset command-mode defaults");
  helpItem("restore", "Reset sensor and restore cached settings");
  helpItem("iface_reset", "Interface reset (SCL pulse)");
  helpItem("greset", "General-call reset (bus-wide)");
  helpItem("stats", "Runtime counters and cached settings");
  helpItem("cfg / settings", "Show current config");
  helpItem("drv", "Show driver state and health");
  helpItem("online", "Show online state");
  helpItem("begin", "Re-initialize device");
  helpItem("end", "End driver session");
  helpItem("probe", "Probe device (no health tracking)");
  helpItem("recover", "Manual recovery attempt");
  helpItem("verbose [0|1]", "Enable/disable verbose output");
  helpItem("stress [N]", "Run N measurement cycles");
  helpItem("stress_mix [N]", "Run N mixed-operation cycles");
  helpItem("selftest", "Run safe command self-test report");
}

void printVersionInfo() {
  Serial.println("=== Version Info ===");
  Serial.printf("  Example firmware build: %s %s\n", __DATE__, __TIME__);
  Serial.printf("  SHT3x library version: %s\n", SHT3x::VERSION);
  Serial.printf("  SHT3x library full: %s\n", SHT3x::VERSION_FULL);
  Serial.printf("  SHT3x library build: %s\n", SHT3x::BUILD_TIMESTAMP);
  Serial.printf("  SHT3x library commit: %s (%s)\n", SHT3x::GIT_COMMIT, SHT3x::GIT_STATUS);
}

// ============================================================================
// Command Processing
// ============================================================================

void processCommand(const String& cmdLine) {
  String cmd = cmdLine;
  cmd.trim();
  if (cmd.length() == 0) {
    return;
  }

  if (cmd == "help" || cmd == "?") {
    printHelp();
    return;
  }

  if (cmd == "version" || cmd == "ver") {
    printVersionInfo();
    return;
  }

  if (cmd == "scan") {
    i2c::scan();
    return;
  }

  if (cmd == "read") {
    cancelPending();
    const SHT3x::Status st = scheduleMeasurement();
    if (st.code != SHT3x::Err::IN_PROGRESS) {
      printStatus(st);
    }
    return;
  }

  if (cmd == "raw") {
    SHT3x::RawSample sample;
    const SHT3x::Status st = device.getRawSample(sample);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    printRawSample(sample);
    return;
  }

  if (cmd == "comp") {
    SHT3x::CompensatedSample sample;
    const SHT3x::Status st = device.getCompensatedSample(sample);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    printCompSample(sample);
    return;
  }

  if (cmd == "meastime") {
    Serial.printf("Estimated measurement time: %lu ms\n",
                  static_cast<unsigned long>(device.estimateMeasurementTimeMs()));
    return;
  }

  if (cmd.startsWith("command ")) {
    String args = cmd.substring(8);
    args.trim();
    const int split = args.indexOf(' ');
    String sub = args;
    String rest;
    if (split >= 0) {
      sub = args.substring(0, split);
      rest = args.substring(split + 1);
      rest.trim();
    }

    if (sub == "write") {
      uint16_t command = 0;
      if (!parseU16(rest, command)) {
        LOGW("Usage: command write <hex>");
        return;
      }
      SHT3x::Status st = device.writeCommand(command);
      printStatus(st);
      return;
    }

    if (sub == "write_data") {
      const int split2 = rest.indexOf(' ');
      if (split2 < 0) {
        LOGW("Usage: command write_data <cmd> <data>");
        return;
      }
      const String cmdStr = rest.substring(0, split2);
      String dataStr = rest.substring(split2 + 1);
      dataStr.trim();

      uint16_t command = 0;
      uint16_t data = 0;
      if (!parseU16(cmdStr, command) || !parseU16(dataStr, data)) {
        LOGW("Invalid command or data");
        return;
      }
      SHT3x::Status st = device.writeCommandWithData(command, data);
      printStatus(st);
      return;
    }

    if (sub == "read") {
      const int split2 = rest.indexOf(' ');
      if (split2 < 0) {
        LOGW("Usage: command read <cmd> <len>");
        return;
      }
      const String cmdStr = rest.substring(0, split2);
      String lenStr = rest.substring(split2 + 1);
      lenStr.trim();

      uint16_t command = 0;
      uint16_t lenValue = 0;
      if (!parseU16(cmdStr, command) || !parseU16(lenStr, lenValue)) {
        LOGW("Invalid command or length");
        return;
      }
      if (lenValue == 0 || lenValue > 8U) {
        LOGW("Length must be between 1 and 8");
        return;
      }

      uint8_t buf[8] = {};
      SHT3x::Status st = device.readCommand(command, buf, lenValue);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      Serial.printf("Command 0x%04X response (%u bytes):\n",
                    static_cast<unsigned>(command),
                    static_cast<unsigned>(lenValue));
      printBytes(buf, lenValue);
      return;
    }

    LOGW("Usage: command write|write_data|read ...");
    return;
  }

  if (cmd == "cfg" || cmd == "settings") {
    printConfig();
    return;
  }

  if (cmd == "mode") {
    SHT3x::SettingsSnapshot snap;
    SHT3x::Status st = device.readSettings(snap);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    Serial.printf("Mode: %s\n", modeToStr(snap.mode));
    printVerboseState();
    return;
  }

  if (cmd.startsWith("mode ")) {
    String arg = cmd.substring(5);
    arg.trim();

    SHT3x::Mode mode;
    if (arg == "single") {
      mode = SHT3x::Mode::SINGLE_SHOT;
    } else if (arg == "periodic") {
      mode = SHT3x::Mode::PERIODIC;
    } else if (arg == "art") {
      mode = SHT3x::Mode::ART;
    } else {
      LOGW("Invalid mode: %s", arg.c_str());
      return;
    }

    cancelPending();
    SHT3x::Status st = device.setMode(mode);
    printStatus(st);
    return;
  }

  if (cmd.startsWith("start_periodic ")) {
    String args = cmd.substring(15);
    args.trim();
    const int split = args.indexOf(' ');
    if (split < 0) {
      LOGW("Usage: start_periodic <rate> <rep>");
      return;
    }
    const String rateStr = args.substring(0, split);
    String repStr = args.substring(split + 1);
    repStr.trim();

    SHT3x::PeriodicRate rate;
    if (!parseRate(rateStr, rate)) {
      LOGW("Invalid rate");
      return;
    }
    SHT3x::Repeatability rep;
    if (!parseRepeatability(repStr, rep)) {
      LOGW("Invalid repeatability");
      return;
    }

    SHT3x::Status st = device.startPeriodic(rate, rep);
    printStatus(st);
    return;
  }

  if (cmd == "start_art") {
    SHT3x::Status st = device.startArt();
    printStatus(st);
    return;
  }

  if (cmd == "stop_periodic") {
    SHT3x::Status st = device.stopPeriodic();
    printStatus(st);
    return;
  }

  if (cmd == "repeat") {
    SHT3x::SettingsSnapshot snap;
    SHT3x::Status st = device.readSettings(snap);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    Serial.printf("Repeatability: %s\n", repToStr(snap.repeatability));
    printVerboseState();
    return;
  }

  if (cmd.startsWith("repeat ")) {
    String arg = cmd.substring(7);
    arg.trim();
    SHT3x::Repeatability rep;
    if (!parseRepeatability(arg, rep)) {
      LOGW("Invalid repeatability: %s", arg.c_str());
      return;
    }

    SHT3x::Status st = device.setRepeatability(rep);
    printStatus(st);
    return;
  }

  if (cmd == "rate") {
    SHT3x::SettingsSnapshot snap;
    SHT3x::Status st = device.readSettings(snap);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    Serial.printf("Periodic rate: %s mps\n", rateToStr(snap.periodicRate));
    printVerboseState();
    return;
  }

  if (cmd.startsWith("rate ")) {
    String arg = cmd.substring(5);
    arg.trim();
    SHT3x::PeriodicRate rate;
    if (!parseRate(arg, rate)) {
      LOGW("Invalid rate: %s", arg.c_str());
      return;
    }

    SHT3x::Status st = device.setPeriodicRate(rate);
    printStatus(st);
    return;
  }

  if (cmd == "stretch") {
    SHT3x::SettingsSnapshot snap;
    SHT3x::Status st = device.readSettings(snap);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    Serial.printf("Clock stretching: %s\n", stretchToStr(snap.clockStretching));
    printVerboseState();
    return;
  }

  if (cmd.startsWith("stretch ")) {
    String arg = cmd.substring(8);
    arg.trim();
    SHT3x::ClockStretching stretch;
    if (!parseStretch(arg, stretch)) {
      LOGW("Invalid stretch: %s", arg.c_str());
      return;
    }

    SHT3x::Status st = device.setClockStretching(stretch);
    printStatus(st);
    return;
  }

  if (cmd == "status") {
    SHT3x::StatusRegister stReg;
    SHT3x::Status st = device.readStatus(stReg);
    if (!st.ok()) {
      printStatus(st);
      return;
    }

    Serial.printf("Status: 0x%04X (alert=%d heater=%d rh_alert=%d t_alert=%d reset=%d cmd_err=%d crc_err=%d)\n",
                  stReg.raw,
                  stReg.alertPending ? 1 : 0,
                  stReg.heaterOn ? 1 : 0,
                  stReg.rhAlert ? 1 : 0,
                  stReg.tAlert ? 1 : 0,
                  stReg.resetDetected ? 1 : 0,
                  stReg.commandError ? 1 : 0,
                  stReg.writeCrcError ? 1 : 0);
    return;
  }

  if (cmd == "status_raw") {
    uint16_t raw = 0;
    SHT3x::Status st = device.readStatus(raw);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    Serial.printf("Status raw: 0x%04X\n", raw);
    return;
  }

  if (cmd == "clearstatus") {
    SHT3x::Status st = device.clearStatus();
    printStatus(st);
    return;
  }

  if (cmd == "heater") {
    bool enabled = false;
    SHT3x::Status st = device.readHeaterStatus(enabled);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    Serial.printf("Heater: %s\n", enabled ? "ON" : "OFF");
    return;
  }

  if (cmd.startsWith("heater ")) {
    String arg = cmd.substring(7);
    arg.trim();
    bool enable = false;
    if (arg == "on") {
      enable = true;
    } else if (arg == "off") {
      enable = false;
    } else if (arg == "status") {
      bool enabled = false;
      SHT3x::Status st = device.readHeaterStatus(enabled);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      Serial.printf("Heater: %s\n", enabled ? "ON" : "OFF");
      return;
    } else {
      LOGW("Usage: heater on|off|status");
      return;
    }

    SHT3x::Status st = device.setHeater(enable);
    printStatus(st);
    return;
  }

  if (cmd.startsWith("serial")) {
    SHT3x::ClockStretching stretch = SHT3x::ClockStretching::STRETCH_DISABLED;
    if (cmd.length() > 6) {
      String arg = cmd.substring(6);
      arg.trim();
      if (arg == "stretch") {
        stretch = SHT3x::ClockStretching::STRETCH_ENABLED;
      } else if (arg == "nostretch") {
        stretch = SHT3x::ClockStretching::STRETCH_DISABLED;
      }
    }
    uint32_t sn = 0;
    SHT3x::Status st = device.readSerialNumber(sn, stretch);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    Serial.printf("Serial: 0x%08lX\n", static_cast<unsigned long>(sn));
    return;
  }

  if (cmd.startsWith("alert ")) {
    String args = cmd.substring(6);
    args.trim();
    const int split = args.indexOf(' ');
    String sub = args;
    String rest;
    if (split >= 0) {
      sub = args.substring(0, split);
      rest = args.substring(split + 1);
      rest.trim();
    }

    if (sub == "read") {
      SHT3x::AlertLimitKind kind;
      if (!parseAlertKind(rest, kind)) {
        LOGW("Usage: alert read <hs|hc|lc|ls>");
        return;
      }
      SHT3x::AlertLimit limit;
      SHT3x::Status st = device.readAlertLimit(kind, limit);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      Serial.printf("Alert %s: raw=0x%04X T=%.2fC RH=%.2f%%\n",
                    alertKindToStr(kind), limit.raw,
                    limit.temperatureC, limit.humidityPct);
      return;
    }

    if (sub == "raw") {
      if (rest.startsWith("read ")) {
        String kindStr = rest.substring(5);
        kindStr.trim();
        SHT3x::AlertLimitKind kind;
        if (!parseAlertKind(kindStr, kind)) {
          LOGW("Usage: alert raw read <hs|hc|lc|ls>");
          return;
        }
        uint16_t raw = 0;
        SHT3x::Status st = device.readAlertLimitRaw(kind, raw);
        if (!st.ok()) {
          printStatus(st);
          return;
        }
        Serial.printf("Alert raw %s: 0x%04X\n",
                      alertKindToStr(kind),
                      static_cast<unsigned>(raw));
        return;
      }
      if (rest.startsWith("write ")) {
        String args2 = rest.substring(6);
        args2.trim();
        const int split2 = args2.indexOf(' ');
        if (split2 < 0) {
          LOGW("Usage: alert raw write <kind> <hex>");
          return;
        }
        const String kindStr = args2.substring(0, split2);
        String valueStr = args2.substring(split2 + 1);
        valueStr.trim();
        SHT3x::AlertLimitKind kind;
        if (!parseAlertKind(kindStr, kind)) {
          LOGW("Invalid alert kind");
          return;
        }
        uint16_t raw = 0;
        if (!parseU16(valueStr, raw)) {
          LOGW("Invalid raw value");
          return;
        }
        SHT3x::Status st = device.writeAlertLimitRaw(kind, raw);
        printStatus(st);
        return;
      }
      LOGW("Usage: alert raw read|write ...");
      return;
    }

    if (sub == "write") {
      const int split2 = rest.indexOf(' ');
      if (split2 < 0) {
        LOGW("Usage: alert write <kind> <T> <RH>");
        return;
      }
      const String kindStr = rest.substring(0, split2);
      String rest2 = rest.substring(split2 + 1);
      rest2.trim();
      const int split3 = rest2.indexOf(' ');
      if (split3 < 0) {
        LOGW("Usage: alert write <kind> <T> <RH>");
        return;
      }
      const String tempStr = rest2.substring(0, split3);
      const String rhStr = rest2.substring(split3 + 1);

      SHT3x::AlertLimitKind kind;
      if (!parseAlertKind(kindStr, kind)) {
        LOGW("Invalid alert kind");
        return;
      }

      const float tempC = tempStr.toFloat();
      const float rh = rhStr.toFloat();
      SHT3x::Status st = device.writeAlertLimit(kind, tempC, rh);
      printStatus(st);
      return;
    }

    if (sub == "encode") {
      const int split2 = rest.indexOf(' ');
      if (split2 < 0) {
        LOGW("Usage: alert encode <T> <RH>");
        return;
      }
      const float tempC = rest.substring(0, split2).toFloat();
      const float rh = rest.substring(split2 + 1).toFloat();
      const uint16_t raw = SHT3x::SHT3x::encodeAlertLimit(tempC, rh);
      Serial.printf("Alert encoded: 0x%04X\n", static_cast<unsigned>(raw));
      return;
    }

    if (sub == "decode") {
      uint16_t raw = 0;
      if (!parseU16(rest, raw)) {
        LOGW("Usage: alert decode <hex>");
        return;
      }
      float tempC = 0.0f;
      float rh = 0.0f;
      SHT3x::SHT3x::decodeAlertLimit(raw, tempC, rh);
      Serial.printf("Alert decoded: T=%.2fC RH=%.2f%%\n", tempC, rh);
      return;
    }

    if (sub == "disable") {
      SHT3x::Status st = device.disableAlerts();
      printStatus(st);
      return;
    }

    LOGW("Usage: alert read|write|raw|encode|decode|disable ...");
    return;
  }

  if (cmd.startsWith("convert ")) {
    String args = cmd.substring(8);
    args.trim();
    const int split = args.indexOf(' ');
    if (split < 0) {
      LOGW("Usage: convert <rawT> <rawRH>");
      return;
    }
    const String tStr = args.substring(0, split);
    String rhStr = args.substring(split + 1);
    rhStr.trim();

    uint16_t rawT = 0;
    uint16_t rawRh = 0;
    if (!parseU16(tStr, rawT) || !parseU16(rhStr, rawRh)) {
      LOGW("Invalid raw values");
      return;
    }

    const float tempC = SHT3x::SHT3x::convertTemperatureC(rawT);
    const float rh = SHT3x::SHT3x::convertHumidityPct(rawRh);
    const int32_t tempC_x100 = SHT3x::SHT3x::convertTemperatureC_x100(rawT);
    const uint32_t rh_x100 = SHT3x::SHT3x::convertHumidityPct_x100(rawRh);
    Serial.printf("Converted: T=%.2fC (%ld) RH=%.2f%% (%lu)\n",
                  tempC, static_cast<long>(tempC_x100),
                  rh, static_cast<unsigned long>(rh_x100));
    return;
  }

  if (cmd == "reset") {
    cancelPending();
    SHT3x::Status st = device.softReset();
    printStatus(st);
    return;
  }

  if (cmd == "defaults") {
    cancelPending();
    SHT3x::Status st = device.resetToDefaults();
    printStatus(st);
    return;
  }

  if (cmd == "restore") {
    cancelPending();
    SHT3x::Status st = device.resetAndRestore();
    printStatus(st);
    return;
  }

  if (cmd == "iface_reset") {
    SHT3x::Status st = device.interfaceReset();
    printStatus(st);
    return;
  }

  if (cmd == "greset") {
    SHT3x::Status st = device.generalCallReset();
    printStatus(st);
    return;
  }

  if (cmd == "online") {
    Serial.printf("Online: %s\n", device.isOnline() ? "YES" : "NO");
    return;
  }

  if (cmd == "stats") {
    printRuntimeStats();
    return;
  }

  if (cmd == "begin") {
    if (!gConfigReady) {
      LOGW("Config not ready");
      return;
    }
    cancelPending();
    SHT3x::Status st = device.begin(gConfig);
    printStatus(st);
    return;
  }

  if (cmd == "end") {
    cancelPending();
    device.end();
    LOGI("Driver ended");
    return;
  }

  if (cmd == "drv") {
    printDriverHealth();
    printConfig();
    return;
  }

  if (cmd == "probe") {
    LOGI("Probing device (no health tracking)...");
    SHT3x::Status st = device.probe();
    printStatus(st);
    return;
  }

  if (cmd == "recover") {
    LOGI("Attempting recovery...");
    SHT3x::Status st = device.recover();
    printStatus(st);
    printDriverHealth();
    return;
  }

  if (cmd == "verbose") {
    printVerboseState();
    return;
  }

  if (cmd.startsWith("verbose ")) {
    const int val = cmd.substring(8).toInt();
    verboseMode = (val != 0);
    LOGI("Verbose mode: %s%s%s",
         onOffColor(verboseMode),
         verboseMode ? "ON" : "OFF",
         LOG_COLOR_RESET);
    return;
  }

  if (cmd == "selftest") {
    runSelfTest();
    return;
  }

  if (cmd == "stress_mix") {
    runStressMix(50);
    return;
  }

  if (cmd.startsWith("stress_mix ")) {
    int count = cmd.substring(11).toInt();
    if (count <= 0 || count > 100000) {
      LOGW("Invalid stress_mix count");
      return;
    }
    runStressMix(count);
    return;
  }

  if (cmd.startsWith("stress")) {
    int count = 10;
    if (cmd.length() > 6) {
      count = cmd.substring(6).toInt();
    }
    if (count <= 0) {
      LOGW("Invalid stress count");
      return;
    }

    cancelPending();
    stressRemaining = count;
    resetStressStats(count);
    LOGI("Starting stress test: %d cycles", count);
    return;
  }

  LOGW("Unknown command: %s", cmd.c_str());
}

// ============================================================================
// Setup and Loop
// ============================================================================

void setup() {
  log_begin(115200);

  LOGI("=== SHT3x Bringup Example ===");

  if (!board::initI2c()) {
    LOGE("Failed to initialize I2C");
    return;
  }
  LOGI("I2C initialized (SDA=%d, SCL=%d)", board::I2C_SDA, board::I2C_SCL);

  i2c::scan();

  gConfig.i2cWrite = transport::wireWrite;
  gConfig.i2cWriteRead = transport::wireWriteRead;
  gConfig.i2cUser = &Wire;
  gConfig.i2cAddress = 0x44;
  gConfig.i2cTimeoutMs = board::I2C_TIMEOUT_MS;
  gConfig.transportCapabilities = SHT3x::TransportCapability::NONE;
  gConfig.offlineThreshold = 5;
  gConfigReady = true;

  SHT3x::Status st = device.begin(gConfig);
  if (!st.ok()) {
    LOGE("Failed to initialize device");
    printStatus(st);
    return;
  }

  LOGI("Device initialized successfully");
  printDriverHealth();
  printHelp();
  Serial.print("> ");
}

void loop() {
  device.tick(millis());

  if (stressStats.active && stressRemaining > 0 && !pendingRead) {
    const SHT3x::Status st = scheduleMeasurement();
    if (st.code != SHT3x::Err::IN_PROGRESS && st.code != SHT3x::Err::BUSY) {
      noteStressError(st);
      stressStats.attempts++;
      stressRemaining--;
      printStressProgress(static_cast<uint32_t>(stressStats.attempts),
                          static_cast<uint32_t>(stressStats.target),
                          static_cast<uint32_t>(stressStats.success),
                          stressStats.errors);
      if (stressRemaining == 0) {
        finishStressStats();
      }
    }
  }

  handleMeasurementReady();

  static String inputBuffer;
  while (Serial.available()) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (inputBuffer.length() > 0) {
        processCommand(inputBuffer);
        inputBuffer = "";
        Serial.print("> ");
      }
    } else {
      inputBuffer += c;
    }
  }
}
