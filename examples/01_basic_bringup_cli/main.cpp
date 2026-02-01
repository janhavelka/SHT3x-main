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

void printStatus(const SHT3x::Status& st) {
  Serial.printf("  Status: %s (code=%u, detail=%ld)\n",
                errToStr(st.code),
                static_cast<unsigned>(st.code),
                static_cast<long>(st.detail));
  if (st.msg && st.msg[0]) {
    Serial.printf("  Message: %s\n", st.msg);
  }
}

void printDriverHealth() {
  Serial.println("=== Driver State ===");
  Serial.printf("  State: %s\n", stateToStr(device.state()));
  Serial.printf("  Online: %s\n", device.isOnline() ? "YES" : "NO");
  Serial.printf("  Consecutive failures: %u\n", device.consecutiveFailures());
  Serial.printf("  Total failures: %lu\n", static_cast<unsigned long>(device.totalFailures()));
  Serial.printf("  Total success: %lu\n", static_cast<unsigned long>(device.totalSuccess()));
  Serial.printf("  Last OK at: %lu ms\n", static_cast<unsigned long>(device.lastOkMs()));
  Serial.printf("  Last error at: %lu ms\n", static_cast<unsigned long>(device.lastErrorMs()));
  if (device.lastError().code != SHT3x::Err::OK) {
    Serial.printf("  Last error: %s\n", errToStr(device.lastError().code));
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
  Serial.printf("  Verbose: %s\n", verboseMode ? "ON" : "OFF");
}

void printConfig() {
  SHT3x::Mode mode;
  SHT3x::Repeatability rep;
  SHT3x::PeriodicRate rate;
  SHT3x::ClockStretching stretch;

  if (device.getMode(mode).ok() && device.getRepeatability(rep).ok() &&
      device.getPeriodicRate(rate).ok() && device.getClockStretching(stretch).ok()) {
    Serial.println("=== Config ===");
    Serial.printf("  Mode: %s\n", modeToStr(mode));
    Serial.printf("  Repeatability: %s\n", repToStr(rep));
    Serial.printf("  Periodic rate: %s mps\n", rateToStr(rate));
    Serial.printf("  Clock stretching: %s\n", stretchToStr(stretch));
    Serial.printf("  Est. meas time: %lu ms\n",
                  static_cast<unsigned long>(device.estimateMeasurementTimeMs()));
  }
  printVerboseState();
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

  Serial.println("=== Stress Summary ===");
  Serial.printf("  Target: %d\n", stressStats.target);
  Serial.printf("  Attempts: %d\n", stressStats.attempts);
  Serial.printf("  Success: %d\n", stressStats.success);
  Serial.printf("  Errors: %lu\n", static_cast<unsigned long>(stressStats.errors));
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
  Serial.println("=== Commands ===");
  Serial.println("  help                     - Show this help");
  Serial.println("  scan                     - Scan I2C bus");
  Serial.println("  read                     - Request measurement (single-shot or periodic fetch)");
  Serial.println("  raw                      - Print last raw sample");
  Serial.println("  comp                     - Print last compensated sample");
  Serial.println("  meastime                 - Show estimated measurement time");
  Serial.println("  mode [single|periodic|art]- Set or show operating mode");
  Serial.println("  start_periodic <rate> <rep> - Start periodic mode");
  Serial.println("  start_art                - Start ART mode");
  Serial.println("  stop_periodic            - Stop periodic/ART mode");
  Serial.println("  repeat [low|med|high]     - Set or show repeatability");
  Serial.println("  rate [0.5|1|2|4|10]       - Set or show periodic rate");
  Serial.println("  stretch [0|1]             - Set or show clock stretching");
  Serial.println("  status                   - Read status register");
  Serial.println("  status_raw               - Read raw status (16-bit)");
  Serial.println("  clearstatus              - Clear status flags");
  Serial.println("  heater [on|off|status]    - Control heater");
  Serial.println("  serial [stretch|nostretch]- Read serial number");
  Serial.println("  alert read <hs|hc|lc|ls>  - Read alert limit");
  Serial.println("  alert write <kind> <T> <RH>- Write alert limit");
  Serial.println("  alert raw read <kind>     - Read raw alert limit word");
  Serial.println("  alert raw write <kind> <hex> - Write raw alert limit word");
  Serial.println("  alert encode <T> <RH>     - Encode alert limit word");
  Serial.println("  alert decode <hex>        - Decode alert limit word");
  Serial.println("  alert disable             - Disable alerts (LowSet > HighSet)");
  Serial.println("  convert <rawT> <rawRH>    - Convert raw values");
  Serial.println("  reset                    - Soft reset device");
  Serial.println("  iface_reset              - Interface reset (SCL pulse)");
  Serial.println("  greset                   - General call reset (bus-wide)");
  Serial.println("  cfg                      - Show current config");
  Serial.println("  drv                      - Show driver state and health");
  Serial.println("  online                   - Show online state");
  Serial.println("  begin                    - Re-initialize device");
  Serial.println("  end                      - End driver session");
  Serial.println("  probe                    - Probe device (no health tracking)");
  Serial.println("  recover                  - Manual recovery attempt");
  Serial.println("  verbose [0|1]             - Enable/disable verbose output");
  Serial.println("  stress [N]               - Run N measurement cycles");
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

  if (cmd == "cfg") {
    printConfig();
    return;
  }

  if (cmd == "mode") {
    SHT3x::Mode mode;
    if (device.getMode(mode).ok()) {
      Serial.printf("Mode: %s\n", modeToStr(mode));
    }
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
    SHT3x::Repeatability rep;
    if (device.getRepeatability(rep).ok()) {
      Serial.printf("Repeatability: %s\n", repToStr(rep));
    }
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
    SHT3x::PeriodicRate rate;
    if (device.getPeriodicRate(rate).ok()) {
      Serial.printf("Periodic rate: %s mps\n", rateToStr(rate));
    }
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
    SHT3x::ClockStretching stretch;
    if (device.getClockStretching(stretch).ok()) {
      Serial.printf("Clock stretching: %s\n", stretchToStr(stretch));
    }
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
    LOGI("Verbose mode: %s", verboseMode ? "ON" : "OFF");
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
  gConfig.i2cAddress = 0x44;
  gConfig.i2cTimeoutMs = board::I2C_TIMEOUT_MS;
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
