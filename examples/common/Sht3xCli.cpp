#include "Sht3xCli.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace sht3x_cli {
namespace {

static constexpr size_t MAX_STRING_LEN = 160U;
static constexpr uint32_t STRESS_PROGRESS_UPDATES = 10U;

static constexpr const char* LOG_COLOR_RESET = "\033[0m";
static constexpr const char* LOG_COLOR_RED = "\033[31m";
static constexpr const char* LOG_COLOR_GREEN = "\033[32m";
static constexpr const char* LOG_COLOR_YELLOW = "\033[33m";
static constexpr const char* LOG_COLOR_CYAN = "\033[36m";

static const char* LOG_COLOR_RESULT(bool ok) {
  return ok ? LOG_COLOR_GREEN : LOG_COLOR_RED;
}

static const char* LOG_COLOR_STATE(bool online, uint8_t failures) {
  return online ? ((failures > 0U) ? LOG_COLOR_YELLOW : LOG_COLOR_GREEN) : LOG_COLOR_RED;
}

static const char* log_bool_str(bool value) {
  return value ? "yes" : "no";
}

Platform platform;

class CliString {
public:
  CliString() { _buf[0] = '\0'; }
  CliString(const char* value) { assign(value); }

  const char* c_str() const { return _buf; }
  size_t length() const { return std::strlen(_buf); }

  void trim() {
    size_t start = 0;
    size_t end = length();
    while (start < end && std::isspace(static_cast<unsigned char>(_buf[start])) != 0) {
      ++start;
    }
    while (end > start && std::isspace(static_cast<unsigned char>(_buf[end - 1U])) != 0) {
      --end;
    }
    if (start > 0U && end > start) {
      std::memmove(_buf, _buf + start, end - start);
    }
    _buf[end - start] = '\0';
  }

  void toLowerCase() {
    for (size_t i = 0; _buf[i] != '\0'; ++i) {
      _buf[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(_buf[i])));
    }
  }

  bool startsWith(const char* prefix) const {
    if (prefix == nullptr) {
      return false;
    }
    const size_t n = std::strlen(prefix);
    return std::strncmp(_buf, prefix, n) == 0;
  }

  int indexOf(char ch) const {
    const char* found = std::strchr(_buf, ch);
    if (found == nullptr) {
      return -1;
    }
    return static_cast<int>(found - _buf);
  }

  CliString substring(int start) const {
    return substring(start, static_cast<int>(length()));
  }

  CliString substring(int start, int end) const {
    const int len = static_cast<int>(length());
    if (start < 0) {
      start = 0;
    }
    if (end < start) {
      end = start;
    }
    if (start > len) {
      start = len;
    }
    if (end > len) {
      end = len;
    }
    CliString out;
    out.assign(_buf + start, static_cast<size_t>(end - start));
    return out;
  }

  long toInt() const {
    return std::strtol(_buf, nullptr, 10);
  }

  float toFloat() const {
    return std::strtof(_buf, nullptr);
  }

  bool operator==(const char* rhs) const {
    return rhs != nullptr && std::strcmp(_buf, rhs) == 0;
  }

  bool operator!=(const char* rhs) const {
    return !(*this == rhs);
  }

private:
  void assign(const char* value) {
    assign(value, value == nullptr ? 0U : std::strlen(value));
  }

  void assign(const char* value, size_t len) {
    if (value == nullptr) {
      _buf[0] = '\0';
      return;
    }
    if (len >= sizeof(_buf)) {
      len = sizeof(_buf) - 1U;
    }
    std::memcpy(_buf, value, len);
    _buf[len] = '\0';
  }

  char _buf[MAX_STRING_LEN] = {};
};

struct OutputProxy {
  void printf(const char* fmt, ...) const {
    va_list args;
    va_start(args, fmt);
    if (platform.vprintf != nullptr) {
      platform.vprintf(platform.user, fmt, args);
    } else {
      std::vprintf(fmt, args);
    }
    va_end(args);
  }

  void print(const char* text) const {
    printf("%s", text ? text : "");
  }

  void println() const {
    print("\n");
  }

  void println(const char* text) const {
    printf("%s\n", text ? text : "");
  }
};

struct StressStats {
  bool active = false;
  uint32_t startMs = 0;
  uint32_t endMs = 0;
  uint32_t successBefore = 0;
  uint32_t failBefore = 0;
  int target = 0;
  int attempts = 0;
  int success = 0;
  uint32_t errors = 0;
  bool hasFailure = false;
  bool hasSample = false;
  float minTemp = 0.0f;
  float maxTemp = 0.0f;
  float minHumidity = 0.0f;
  float maxHumidity = 0.0f;
  double sumTemp = 0.0;
  double sumHumidity = 0.0;
  SHT3x::Status firstError = SHT3x::Status::Ok();
  SHT3x::Status lastError = SHT3x::Status::Ok();
};

OutputProxy Serial;
SHT3x::SHT3x deviceInstance;
SHT3x::Config configInstance;
bool configIsReady = false;
bool verboseMode = false;
bool pendingRead = false;
uint32_t pendingStartMs = 0;
int stressRemaining = 0;
StressStats stressStats;

uint32_t millis() {
  return platform.nowMs != nullptr ? platform.nowMs(platform.user) : 0U;
}

void yield() {
  if (platform.yield != nullptr) {
    platform.yield(platform.user);
  }
}

void scanBus() {
  if (platform.scanBus != nullptr) {
    platform.scanBus(platform.user);
  } else {
    logWarn("I2C scan not available");
  }
}

void vlogTagged(const char* color, const char* tag, const char* fmt, va_list args) {
  Serial.printf("%s[%s]%s ", color, tag, LOG_COLOR_RESET);
  if (platform.vprintf != nullptr) {
    platform.vprintf(platform.user, fmt, args);
  } else {
    std::vprintf(fmt, args);
  }
  Serial.println();
}

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
                static_cast<double>(pct),
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
  if (data == nullptr || len == 0U) {
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

template <typename DriverT>
struct HealthSnapshot {
  int state = 0;
  bool online = false;
  uint8_t consecutiveFailures = 0;
  uint32_t totalFailures = 0;
  uint32_t totalSuccess = 0;

  void capture(const DriverT& driver) {
    state = static_cast<int>(driver.state());
    online = driver.isOnline();
    consecutiveFailures = driver.consecutiveFailures();
    totalFailures = driver.totalFailures();
    totalSuccess = driver.totalSuccess();
  }
};

const char* healthBoolColor(bool value) {
  return value ? LOG_COLOR_GREEN : LOG_COLOR_RED;
}

const char* healthFailureColor(uint32_t failures) {
  if (failures == 0U) return LOG_COLOR_GREEN;
  if (failures < 3U) return LOG_COLOR_YELLOW;
  return LOG_COLOR_RED;
}

const char* healthSuccessColor(uint32_t successes) {
  return (successes > 0U) ? LOG_COLOR_GREEN : LOG_COLOR_RESET;
}

template <typename DriverT>
void printHealthView(const DriverT& driver) {
  HealthSnapshot<DriverT> snap;
  snap.capture(driver);
  const uint32_t total = snap.totalSuccess + snap.totalFailures;
  const float pct = (total > 0U)
                        ? (100.0f * static_cast<float>(snap.totalSuccess) /
                           static_cast<float>(total))
                        : 0.0f;

  Serial.printf("Health: state=%s%s%s online=%s%s%s consec=%s%u%s ok=%s%lu%s fail=%s%lu%s rate=%s%.1f%%%s\n",
                healthFailureColor(static_cast<uint32_t>(snap.consecutiveFailures)),
                stateToStr(static_cast<SHT3x::DriverState>(snap.state)),
                LOG_COLOR_RESET,
                healthBoolColor(snap.online),
                snap.online ? "true" : "false",
                LOG_COLOR_RESET,
                healthFailureColor(static_cast<uint32_t>(snap.consecutiveFailures)),
                static_cast<unsigned>(snap.consecutiveFailures),
                LOG_COLOR_RESET,
                healthSuccessColor(snap.totalSuccess),
                static_cast<unsigned long>(snap.totalSuccess),
                LOG_COLOR_RESET,
                healthFailureColor(snap.totalFailures),
                static_cast<unsigned long>(snap.totalFailures),
                LOG_COLOR_RESET,
                successRateColor(pct),
                static_cast<double>(pct),
                LOG_COLOR_RESET);
}

template <typename DriverT>
void printHealthDiff(const HealthSnapshot<DriverT>& before,
                     const HealthSnapshot<DriverT>& after) {
  bool changed = false;

  if (before.state != after.state) {
    Serial.printf("  State: %s%s%s -> %s%s%s\n",
                  healthFailureColor(static_cast<uint32_t>(before.consecutiveFailures)),
                  stateToStr(static_cast<SHT3x::DriverState>(before.state)),
                  LOG_COLOR_RESET,
                  healthFailureColor(static_cast<uint32_t>(after.consecutiveFailures)),
                  stateToStr(static_cast<SHT3x::DriverState>(after.state)),
                  LOG_COLOR_RESET);
    changed = true;
  }
  if (before.online != after.online) {
    Serial.printf("  Online: %s%s%s -> %s%s%s\n",
                  healthBoolColor(before.online),
                  before.online ? "true" : "false",
                  LOG_COLOR_RESET,
                  healthBoolColor(after.online),
                  after.online ? "true" : "false",
                  LOG_COLOR_RESET);
    changed = true;
  }
  if (before.consecutiveFailures != after.consecutiveFailures) {
    Serial.printf("  ConsecFail: %s%u -> %u%s\n",
                  healthFailureColor(static_cast<uint32_t>(after.consecutiveFailures)),
                  static_cast<unsigned>(before.consecutiveFailures),
                  static_cast<unsigned>(after.consecutiveFailures),
                  LOG_COLOR_RESET);
    changed = true;
  }
  if (before.totalSuccess != after.totalSuccess) {
    Serial.printf("  TotalOK: %lu -> %s%lu (+%lu)%s\n",
                  static_cast<unsigned long>(before.totalSuccess),
                  LOG_COLOR_GREEN,
                  static_cast<unsigned long>(after.totalSuccess),
                  static_cast<unsigned long>(after.totalSuccess - before.totalSuccess),
                  LOG_COLOR_RESET);
    changed = true;
  }
  if (before.totalFailures != after.totalFailures) {
    Serial.printf("  TotalFail: %lu -> %s%lu (+%lu)%s\n",
                  static_cast<unsigned long>(before.totalFailures),
                  LOG_COLOR_RED,
                  static_cast<unsigned long>(after.totalFailures),
                  static_cast<unsigned long>(after.totalFailures - before.totalFailures),
                  LOG_COLOR_RESET);
    changed = true;
  }
  if (!changed) {
    Serial.println("  (no health changes)");
  }
}

void printConfig() {
  SHT3x::SettingsSnapshot snap;
  SHT3x::Status st = deviceInstance.getSettings(snap);
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
  Serial.printf("  Has sample: %s\n", snap.hasSample ? "true" : "false");
  Serial.printf("  Est. meas time: %lu ms\n",
                static_cast<unsigned long>(deviceInstance.estimateMeasurementTimeMs()));
  Serial.printf("  Verbose: %s%s%s\n",
                onOffColor(verboseMode),
                verboseMode ? "ON" : "OFF",
                LOG_COLOR_RESET);
}

void printRuntimeStats() {
  const uint32_t now = millis();
  Serial.println("=== Runtime Stats ===");
  Serial.printf("  periodicActive: %s\n", deviceInstance.isPeriodicActive() ? "true" : "false");
  Serial.printf("  measurementReady: %s\n", deviceInstance.measurementReady() ? "true" : "false");
  Serial.printf("  hasCachedSettings: %s\n", deviceInstance.hasCachedSettings() ? "true" : "false");
  Serial.printf("  lastBusActivityMs: %lu\n",
                static_cast<unsigned long>(deviceInstance.lastBusActivityMs()));
  Serial.printf("  sampleTimestampMs: %lu\n",
                static_cast<unsigned long>(deviceInstance.sampleTimestampMs()));
  Serial.printf("  sampleAgeMs: %lu\n",
                static_cast<unsigned long>(deviceInstance.sampleAgeMs(now)));
  Serial.printf("  notReadyCount: %lu\n",
                static_cast<unsigned long>(deviceInstance.notReadyCount()));
  Serial.printf("  missedSamplesEstimate: %lu\n",
                static_cast<unsigned long>(deviceInstance.missedSamplesEstimate()));

  if (deviceInstance.hasCachedSettings()) {
    const SHT3x::CachedSettings cached = deviceInstance.getCachedSettings();
    Serial.println("  Cached settings:");
    Serial.printf("    mode: %s\n", modeToStr(cached.mode));
    Serial.printf("    repeatability: %s\n", repToStr(cached.repeatability));
    Serial.printf("    periodicRate: %s mps\n", rateToStr(cached.periodicRate));
    Serial.printf("    stretching: %s\n", stretchToStr(cached.clockStretching));
    Serial.printf("    heaterEnabled: %s\n", cached.heaterEnabled ? "true" : "false");
  }
}

void printMeasurement(const SHT3x::Measurement& m) {
  Serial.printf("Temp: %.2f C, Humidity: %.2f %%\n",
                static_cast<double>(m.temperatureC),
                static_cast<double>(m.humidityPct));
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

void resetStressStats(int target) {
  stressStats = StressStats{};
  stressStats.active = true;
  stressStats.startMs = millis();
  stressStats.successBefore = deviceInstance.totalSuccess();
  stressStats.failBefore = deviceInstance.totalFailures();
  stressStats.target = target;
  stressStats.minTemp = std::numeric_limits<float>::max();
  stressStats.maxTemp = std::numeric_limits<float>::lowest();
  stressStats.minHumidity = std::numeric_limits<float>::max();
  stressStats.maxHumidity = std::numeric_limits<float>::lowest();
}

void noteStressError(const SHT3x::Status& st) {
  stressStats.errors++;
  if (!stressStats.hasFailure) {
    stressStats.firstError = st;
    stressStats.hasFailure = true;
  }
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
    if (m.temperatureC < stressStats.minTemp) stressStats.minTemp = m.temperatureC;
    if (m.temperatureC > stressStats.maxTemp) stressStats.maxTemp = m.temperatureC;
    if (m.humidityPct < stressStats.minHumidity) stressStats.minHumidity = m.humidityPct;
    if (m.humidityPct > stressStats.maxHumidity) stressStats.maxHumidity = m.humidityPct;
  }

  stressStats.sumTemp += m.temperatureC;
  stressStats.sumHumidity += m.humidityPct;
  stressStats.success++;
}

void finishStressStats() {
  stressStats.active = false;
  stressStats.endMs = millis();
  const uint32_t successDelta = deviceInstance.totalSuccess() - stressStats.successBefore;
  const uint32_t failDelta = deviceInstance.totalFailures() - stressStats.failBefore;
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
                static_cast<double>(successPct),
                LOG_COLOR_RESET);
  Serial.printf("  Duration: %lu ms\n", static_cast<unsigned long>(durationMs));
  if (durationMs > 0U) {
    const float rate = 1000.0f * static_cast<float>(stressStats.attempts) /
                       static_cast<float>(durationMs);
    Serial.printf("  Rate: %.2f samples/s\n", static_cast<double>(rate));
  }
  Serial.printf("  Health delta: %ssuccess +%lu%s, %sfailures +%lu%s\n",
                goodIfNonZeroColor(successDelta),
                static_cast<unsigned long>(successDelta),
                LOG_COLOR_RESET,
                goodIfZeroColor(failDelta),
                static_cast<unsigned long>(failDelta),
                LOG_COLOR_RESET);

  if (stressStats.success > 0) {
    const float avgTemp = static_cast<float>(stressStats.sumTemp / stressStats.success);
    const float avgHumidity = static_cast<float>(stressStats.sumHumidity / stressStats.success);
    Serial.printf("  Temp C: min=%.2f avg=%.2f max=%.2f\n",
                  static_cast<double>(stressStats.minTemp),
                  static_cast<double>(avgTemp),
                  static_cast<double>(stressStats.maxTemp));
    Serial.printf("  Humidity %%: min=%.2f avg=%.2f max=%.2f\n",
                  static_cast<double>(stressStats.minHumidity),
                  static_cast<double>(avgHumidity),
                  static_cast<double>(stressStats.maxHumidity));
  } else {
    Serial.println("  No valid samples");
  }

  if (stressStats.hasFailure) {
    Serial.println("  First failure:");
    printStatus(stressStats.firstError);
    if (stressStats.errors > 1U) {
      Serial.println("  Last failure:");
      printStatus(stressStats.lastError);
    }
  }
}

SHT3x::Status performMeasurementBlocking(SHT3x::Measurement& out, uint32_t timeoutMs = 500) {
  SHT3x::Status st = deviceInstance.requestMeasurement();
  if (st.code != SHT3x::Err::IN_PROGRESS && st.code != SHT3x::Err::BUSY) {
    return st;
  }

  const uint32_t start = millis();
  while ((millis() - start) < timeoutMs) {
    deviceInstance.tick(millis());
    if (deviceInstance.measurementReady()) {
      return deviceInstance.getMeasurement(out);
    }
    yield();
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
  HealthSnapshot<SHT3x::SHT3x> healthBefore;
  healthBefore.capture(deviceInstance);
  const uint32_t succBefore = deviceInstance.totalSuccess();
  const uint32_t failBefore = deviceInstance.totalFailures();
  const uint32_t startMs = millis();
  uint32_t okTotal = 0;
  uint32_t failTotal = 0;
  bool hasFailure = false;
  SHT3x::Status firstFailure = SHT3x::Status::Ok();
  SHT3x::Status lastFailure = SHT3x::Status::Ok();

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
        st = deviceInstance.readStatus(reg);
        break;
      }
      case 2: {
        uint32_t serial = 0;
        st = deviceInstance.readSerialNumber(serial, SHT3x::ClockStretching::STRETCH_DISABLED);
        break;
      }
      case 3:
        st = deviceInstance.setRepeatability(
            static_cast<SHT3x::Repeatability>((i / opCount) % 3));
        break;
      case 4:
        st = deviceInstance.setPeriodicRate(static_cast<SHT3x::PeriodicRate>((i / opCount) % 5));
        break;
      case 5:
        st = deviceInstance.setClockStretching(((i / opCount) % 2) ?
                                                   SHT3x::ClockStretching::STRETCH_ENABLED :
                                                   SHT3x::ClockStretching::STRETCH_DISABLED);
        break;
      case 6: {
        bool enabled = false;
        st = deviceInstance.readHeaterStatus(enabled);
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
      if (!hasFailure) {
        firstFailure = st;
        hasFailure = true;
      }
      lastFailure = st;
      if (verboseMode) {
        Serial.printf("  [%d] %s failed: %s\n", i, stats[op].name, errToStr(st.code));
      }
    }

    printStressProgress(static_cast<uint32_t>(i + 1), static_cast<uint32_t>(count),
                        okTotal, failTotal);
  }

  const uint32_t elapsed = millis() - startMs;
  HealthSnapshot<SHT3x::SHT3x> healthAfter;
  healthAfter.capture(deviceInstance);

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
                static_cast<double>(successPct),
                LOG_COLOR_RESET);
  Serial.printf("  Duration: %lu ms\n", static_cast<unsigned long>(elapsed));
  if (elapsed > 0U) {
    Serial.printf("  Rate: %.2f ops/s\n",
                  static_cast<double>((1000.0f * static_cast<float>(count)) / elapsed));
  }
  for (int i = 0; i < opCount; ++i) {
    const uint32_t opTotal = stats[i].ok + stats[i].fail;
    const float opPct = (opTotal > 0U)
                            ? (100.0f * static_cast<float>(stats[i].ok) /
                               static_cast<float>(opTotal))
                            : 0.0f;
    Serial.printf("  %-10s %sok=%lu%s %sfail=%lu%s (%s%.1f%%%s)\n",
                  stats[i].name,
                  goodIfNonZeroColor(stats[i].ok),
                  static_cast<unsigned long>(stats[i].ok),
                  LOG_COLOR_RESET,
                  goodIfZeroColor(stats[i].fail),
                  static_cast<unsigned long>(stats[i].fail),
                  LOG_COLOR_RESET,
                  successRateColor(opPct),
                  static_cast<double>(opPct),
                  LOG_COLOR_RESET);
  }
  const uint32_t successDelta = deviceInstance.totalSuccess() - succBefore;
  const uint32_t failDelta = deviceInstance.totalFailures() - failBefore;
  Serial.printf("  Health delta: %ssuccess +%lu%s, %sfailures +%lu%s\n",
                goodIfNonZeroColor(successDelta),
                static_cast<unsigned long>(successDelta),
                LOG_COLOR_RESET,
                goodIfZeroColor(failDelta),
                static_cast<unsigned long>(failDelta),
                LOG_COLOR_RESET);
  Serial.println("  Health changes:");
  printHealthDiff(healthBefore, healthAfter);
  if (hasFailure) {
    Serial.println("  First failure:");
    printStatus(firstFailure);
    if (failTotal > 1U) {
      Serial.println("  Last failure:");
      printStatus(lastFailure);
    }
  }
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
  bool haveBaseline = deviceInstance.readSettings(baseline).ok();
  reportCheck("capture baseline settings", haveBaseline, haveBaseline ? "" : "readSettings failed");

  const uint32_t succBefore = deviceInstance.totalSuccess();
  const uint32_t failBefore = deviceInstance.totalFailures();
  const uint8_t consBefore = deviceInstance.consecutiveFailures();

  SHT3x::Status st = deviceInstance.probe();
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
  const bool probeNoTrack = deviceInstance.totalSuccess() == succBefore &&
                            deviceInstance.totalFailures() == failBefore &&
                            deviceInstance.consecutiveFailures() == consBefore;
  reportCheck("probe no-health-side-effects", probeNoTrack, "");

  st = deviceInstance.setMode(SHT3x::Mode::SINGLE_SHOT);
  reportCheck("setMode(SINGLE_SHOT)", st.ok(), st.ok() ? "" : errToStr(st.code));
  SHT3x::Mode mode = SHT3x::Mode::SINGLE_SHOT;
  st = deviceInstance.getMode(mode);
  reportCheck("getMode", st.ok(), st.ok() ? "" : errToStr(st.code));
  reportCheck("verify mode", st.ok() && mode == SHT3x::Mode::SINGLE_SHOT, "");

  st = deviceInstance.setRepeatability(SHT3x::Repeatability::HIGH_REPEATABILITY);
  reportCheck("setRepeatability(HIGH)", st.ok(), st.ok() ? "" : errToStr(st.code));
  SHT3x::Repeatability rep = SHT3x::Repeatability::LOW_REPEATABILITY;
  st = deviceInstance.getRepeatability(rep);
  reportCheck("verify repeatability", st.ok() && rep == SHT3x::Repeatability::HIGH_REPEATABILITY,
              st.ok() ? "" : errToStr(st.code));

  st = deviceInstance.setPeriodicRate(SHT3x::PeriodicRate::MPS_1);
  reportCheck("setPeriodicRate(1mps)", st.ok(), st.ok() ? "" : errToStr(st.code));
  SHT3x::PeriodicRate rate = SHT3x::PeriodicRate::MPS_0_5;
  st = deviceInstance.getPeriodicRate(rate);
  reportCheck("verify periodic rate", st.ok() && rate == SHT3x::PeriodicRate::MPS_1,
              st.ok() ? "" : errToStr(st.code));

  st = deviceInstance.setClockStretching(SHT3x::ClockStretching::STRETCH_ENABLED);
  reportCheck("setClockStretching(ON)", st.ok(), st.ok() ? "" : errToStr(st.code));
  SHT3x::ClockStretching stretch = SHT3x::ClockStretching::STRETCH_DISABLED;
  st = deviceInstance.getClockStretching(stretch);
  reportCheck("verify stretching", st.ok() && stretch == SHT3x::ClockStretching::STRETCH_ENABLED,
              st.ok() ? "" : errToStr(st.code));

  uint16_t statusRaw = 0;
  st = deviceInstance.readStatus(statusRaw);
  reportCheck("readStatus(raw)", st.ok(), st.ok() ? "" : errToStr(st.code));

  bool heaterOn = false;
  st = deviceInstance.readHeaterStatus(heaterOn);
  reportCheck("readHeaterStatus", st.ok(), st.ok() ? "" : errToStr(st.code));

  SHT3x::Measurement m;
  st = performMeasurementBlocking(m);
  reportCheck("measurement cycle", st.ok(), st.ok() ? "" : errToStr(st.code));
  const bool mRange = (m.temperatureC > -60.0f && m.temperatureC < 130.0f) &&
                      (m.humidityPct >= 0.0f && m.humidityPct <= 100.0f);
  reportCheck("measurement in plausible range", st.ok() && mRange, "");

  st = deviceInstance.softReset();
  reportCheck("softReset", st.ok(), st.ok() ? "" : errToStr(st.code));

  st = deviceInstance.recover();
  reportCheck("recover", st.ok(), st.ok() ? "" : errToStr(st.code));
  reportCheck("isOnline", deviceInstance.isOnline(), "");

  if (haveBaseline) {
    deviceInstance.setMode(baseline.mode);
    deviceInstance.setRepeatability(baseline.repeatability);
    deviceInstance.setPeriodicRate(baseline.periodicRate);
    deviceInstance.setClockStretching(baseline.clockStretching);
  }

  Serial.printf("Selftest result: pass=%s%lu%s fail=%s%lu%s skip=%s%lu%s\n",
                goodIfNonZeroColor(result.pass), static_cast<unsigned long>(result.pass), LOG_COLOR_RESET,
                goodIfZeroColor(result.fail), static_cast<unsigned long>(result.fail), LOG_COLOR_RESET,
                skipCountColor(result.skip), static_cast<unsigned long>(result.skip), LOG_COLOR_RESET);
}

SHT3x::Status scheduleMeasurement() {
  SHT3x::Status st = deviceInstance.requestMeasurement();
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
  if (!pendingRead || !deviceInstance.measurementReady()) {
    return;
  }

  SHT3x::Measurement m;
  SHT3x::Status st = deviceInstance.getMeasurement(m);
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

bool parseRepeatability(const CliString& token, SHT3x::Repeatability& out) {
  CliString t = token;
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

bool parseRate(const CliString& token, SHT3x::PeriodicRate& out) {
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

bool parseStretch(const CliString& token, SHT3x::ClockStretching& out) {
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

bool parseAlertKind(const CliString& token, SHT3x::AlertLimitKind& out) {
  CliString t = token;
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

bool parseU16(const CliString& token, uint16_t& out) {
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

namespace cli {
static constexpr size_t HELP_COMMAND_WIDTH = 32U;

void printHelpHeader(const char* title) {
  Serial.printf("%s=== %s ===%s\n", LOG_COLOR_CYAN, title, LOG_COLOR_RESET);
}

void printHelpSection(const char* title) {
  Serial.printf("\n%s[%s]%s\n", LOG_COLOR_GREEN, title, LOG_COLOR_RESET);
}

void printHelpItem(const char* command, const char* description) {
  Serial.printf("  %s%-*s%s - %s\n",
                LOG_COLOR_CYAN,
                static_cast<int>(HELP_COMMAND_WIDTH),
                command,
                LOG_COLOR_RESET,
                description);
}
}  // namespace cli

void processCommandString(const CliString& cmdLine) {
  CliString cmd = cmdLine;
  cmd.trim();
  if (cmd.length() == 0U) {
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
    scanBus();
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
    const SHT3x::Status st = deviceInstance.getRawSample(sample);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    printRawSample(sample);
    return;
  }

  if (cmd == "comp") {
    SHT3x::CompensatedSample sample;
    const SHT3x::Status st = deviceInstance.getCompensatedSample(sample);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    printCompSample(sample);
    return;
  }

  if (cmd == "meastime") {
    Serial.printf("Estimated measurement time: %lu ms\n",
                  static_cast<unsigned long>(deviceInstance.estimateMeasurementTimeMs()));
    return;
  }

  if (cmd.startsWith("command ")) {
    CliString args = cmd.substring(8);
    args.trim();
    const int split = args.indexOf(' ');
    CliString sub = args;
    CliString rest;
    if (split >= 0) {
      sub = args.substring(0, split);
      rest = args.substring(split + 1);
      rest.trim();
    }

    if (sub == "write") {
      uint16_t command = 0;
      if (!parseU16(rest, command)) {
        logWarn("Usage: command write <hex>");
        return;
      }
      SHT3x::Status st = deviceInstance.writeCommand(command);
      printStatus(st);
      return;
    }

    if (sub == "write_data") {
      const int split2 = rest.indexOf(' ');
      if (split2 < 0) {
        logWarn("Usage: command write_data <cmd> <data>");
        return;
      }
      const CliString cmdStr = rest.substring(0, split2);
      CliString dataStr = rest.substring(split2 + 1);
      dataStr.trim();

      uint16_t command = 0;
      uint16_t data = 0;
      if (!parseU16(cmdStr, command) || !parseU16(dataStr, data)) {
        logWarn("Invalid command or data");
        return;
      }
      SHT3x::Status st = deviceInstance.writeCommandWithData(command, data);
      printStatus(st);
      return;
    }

    if (sub == "read") {
      const int split2 = rest.indexOf(' ');
      if (split2 < 0) {
        logWarn("Usage: command read <cmd> <len>");
        return;
      }
      const CliString cmdStr = rest.substring(0, split2);
      CliString lenStr = rest.substring(split2 + 1);
      lenStr.trim();

      uint16_t command = 0;
      uint16_t lenValue = 0;
      if (!parseU16(cmdStr, command) || !parseU16(lenStr, lenValue)) {
        logWarn("Invalid command or length");
        return;
      }
      if (lenValue == 0U || lenValue > SHT3x::cmd::MEASUREMENT_DATA_LEN) {
        logWarn("Length must be between 1 and 6");
        return;
      }

      uint8_t buf[8] = {};
      SHT3x::Status st = deviceInstance.readCommand(command, buf, lenValue);
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

    logWarn("Usage: command write|write_data|read ...");
    return;
  }

  if (cmd == "cfg" || cmd == "settings") {
    printConfig();
    return;
  }

  if (cmd == "mode") {
    SHT3x::SettingsSnapshot snap;
    SHT3x::Status st = deviceInstance.readSettings(snap);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    Serial.printf("Mode: %s\n", modeToStr(snap.mode));
    printVerboseState();
    return;
  }

  if (cmd.startsWith("mode ")) {
    CliString arg = cmd.substring(5);
    arg.trim();

    SHT3x::Mode mode;
    if (arg == "single") {
      mode = SHT3x::Mode::SINGLE_SHOT;
    } else if (arg == "periodic") {
      mode = SHT3x::Mode::PERIODIC;
    } else if (arg == "art") {
      mode = SHT3x::Mode::ART;
    } else {
      logWarn("Invalid mode: %s", arg.c_str());
      return;
    }

    cancelPending();
    SHT3x::Status st = deviceInstance.setMode(mode);
    printStatus(st);
    return;
  }

  if (cmd.startsWith("start_periodic ")) {
    CliString args = cmd.substring(15);
    args.trim();
    const int split = args.indexOf(' ');
    if (split < 0) {
      logWarn("Usage: start_periodic <rate> <rep>");
      return;
    }
    const CliString rateStr = args.substring(0, split);
    CliString repStr = args.substring(split + 1);
    repStr.trim();

    SHT3x::PeriodicRate rate;
    if (!parseRate(rateStr, rate)) {
      logWarn("Invalid rate");
      return;
    }
    SHT3x::Repeatability rep;
    if (!parseRepeatability(repStr, rep)) {
      logWarn("Invalid repeatability");
      return;
    }

    SHT3x::Status st = deviceInstance.startPeriodic(rate, rep);
    printStatus(st);
    return;
  }

  if (cmd == "start_art") {
    SHT3x::Status st = deviceInstance.startArt();
    printStatus(st);
    return;
  }

  if (cmd == "stop_periodic") {
    SHT3x::Status st = deviceInstance.stopPeriodic();
    printStatus(st);
    return;
  }

  if (cmd == "repeat") {
    SHT3x::SettingsSnapshot snap;
    SHT3x::Status st = deviceInstance.readSettings(snap);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    Serial.printf("Repeatability: %s\n", repToStr(snap.repeatability));
    printVerboseState();
    return;
  }

  if (cmd.startsWith("repeat ")) {
    CliString arg = cmd.substring(7);
    arg.trim();
    SHT3x::Repeatability rep;
    if (!parseRepeatability(arg, rep)) {
      logWarn("Invalid repeatability: %s", arg.c_str());
      return;
    }

    SHT3x::Status st = deviceInstance.setRepeatability(rep);
    printStatus(st);
    return;
  }

  if (cmd == "rate") {
    SHT3x::SettingsSnapshot snap;
    SHT3x::Status st = deviceInstance.readSettings(snap);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    Serial.printf("Periodic rate: %s mps\n", rateToStr(snap.periodicRate));
    printVerboseState();
    return;
  }

  if (cmd.startsWith("rate ")) {
    CliString arg = cmd.substring(5);
    arg.trim();
    SHT3x::PeriodicRate rate;
    if (!parseRate(arg, rate)) {
      logWarn("Invalid rate: %s", arg.c_str());
      return;
    }

    SHT3x::Status st = deviceInstance.setPeriodicRate(rate);
    printStatus(st);
    return;
  }

  if (cmd == "stretch") {
    SHT3x::SettingsSnapshot snap;
    SHT3x::Status st = deviceInstance.readSettings(snap);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    Serial.printf("Clock stretching: %s\n", stretchToStr(snap.clockStretching));
    printVerboseState();
    return;
  }

  if (cmd.startsWith("stretch ")) {
    CliString arg = cmd.substring(8);
    arg.trim();
    SHT3x::ClockStretching stretch;
    if (!parseStretch(arg, stretch)) {
      logWarn("Invalid stretch: %s", arg.c_str());
      return;
    }

    SHT3x::Status st = deviceInstance.setClockStretching(stretch);
    printStatus(st);
    return;
  }

  if (cmd == "status") {
    SHT3x::StatusRegister stReg;
    SHT3x::Status st = deviceInstance.readStatus(stReg);
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
    SHT3x::Status st = deviceInstance.readStatus(raw);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    Serial.printf("Status raw: 0x%04X\n", raw);
    return;
  }

  if (cmd == "clearstatus") {
    SHT3x::Status st = deviceInstance.clearStatus();
    printStatus(st);
    return;
  }

  if (cmd == "heater") {
    bool enabled = false;
    SHT3x::Status st = deviceInstance.readHeaterStatus(enabled);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    Serial.printf("Heater: %s\n", enabled ? "ON" : "OFF");
    return;
  }

  if (cmd.startsWith("heater ")) {
    CliString arg = cmd.substring(7);
    arg.trim();
    bool enable = false;
    if (arg == "on") {
      enable = true;
    } else if (arg == "off") {
      enable = false;
    } else if (arg == "status") {
      bool enabled = false;
      SHT3x::Status st = deviceInstance.readHeaterStatus(enabled);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      Serial.printf("Heater: %s\n", enabled ? "ON" : "OFF");
      return;
    } else {
      logWarn("Usage: heater on|off|status");
      return;
    }

    SHT3x::Status st = deviceInstance.setHeater(enable);
    printStatus(st);
    return;
  }

  if (cmd.startsWith("serial")) {
    SHT3x::ClockStretching stretch = SHT3x::ClockStretching::STRETCH_DISABLED;
    if (cmd.length() > 6U) {
      CliString arg = cmd.substring(6);
      arg.trim();
      if (arg == "stretch") {
        stretch = SHT3x::ClockStretching::STRETCH_ENABLED;
      } else if (arg == "nostretch") {
        stretch = SHT3x::ClockStretching::STRETCH_DISABLED;
      }
    }
    uint32_t sn = 0;
    SHT3x::Status st = deviceInstance.readSerialNumber(sn, stretch);
    if (!st.ok()) {
      printStatus(st);
      return;
    }
    Serial.printf("Serial: 0x%08lX\n", static_cast<unsigned long>(sn));
    return;
  }

  if (cmd.startsWith("alert ")) {
    CliString args = cmd.substring(6);
    args.trim();
    const int split = args.indexOf(' ');
    CliString sub = args;
    CliString rest;
    if (split >= 0) {
      sub = args.substring(0, split);
      rest = args.substring(split + 1);
      rest.trim();
    }

    if (sub == "read") {
      SHT3x::AlertLimitKind kind;
      if (!parseAlertKind(rest, kind)) {
        logWarn("Usage: alert read <hs|hc|lc|ls>");
        return;
      }
      SHT3x::AlertLimit limit;
      SHT3x::Status st = deviceInstance.readAlertLimit(kind, limit);
      if (!st.ok()) {
        printStatus(st);
        return;
      }
      Serial.printf("Alert %s: raw=0x%04X T=%.2fC RH=%.2f%%\n",
                    alertKindToStr(kind), limit.raw,
                    static_cast<double>(limit.temperatureC),
                    static_cast<double>(limit.humidityPct));
      return;
    }

    if (sub == "raw") {
      if (rest.startsWith("read ")) {
        CliString kindStr = rest.substring(5);
        kindStr.trim();
        SHT3x::AlertLimitKind kind;
        if (!parseAlertKind(kindStr, kind)) {
          logWarn("Usage: alert raw read <hs|hc|lc|ls>");
          return;
        }
        uint16_t raw = 0;
        SHT3x::Status st = deviceInstance.readAlertLimitRaw(kind, raw);
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
        CliString args2 = rest.substring(6);
        args2.trim();
        const int split2 = args2.indexOf(' ');
        if (split2 < 0) {
          logWarn("Usage: alert raw write <kind> <hex>");
          return;
        }
        const CliString kindStr = args2.substring(0, split2);
        CliString valueStr = args2.substring(split2 + 1);
        valueStr.trim();
        SHT3x::AlertLimitKind kind;
        if (!parseAlertKind(kindStr, kind)) {
          logWarn("Invalid alert kind");
          return;
        }
        uint16_t raw = 0;
        if (!parseU16(valueStr, raw)) {
          logWarn("Invalid raw value");
          return;
        }
        SHT3x::Status st = deviceInstance.writeAlertLimitRaw(kind, raw);
        printStatus(st);
        return;
      }
      logWarn("Usage: alert raw read|write ...");
      return;
    }

    if (sub == "write") {
      const int split2 = rest.indexOf(' ');
      if (split2 < 0) {
        logWarn("Usage: alert write <kind> <T> <RH>");
        return;
      }
      const CliString kindStr = rest.substring(0, split2);
      CliString rest2 = rest.substring(split2 + 1);
      rest2.trim();
      const int split3 = rest2.indexOf(' ');
      if (split3 < 0) {
        logWarn("Usage: alert write <kind> <T> <RH>");
        return;
      }
      const CliString tempStr = rest2.substring(0, split3);
      const CliString rhStr = rest2.substring(split3 + 1);

      SHT3x::AlertLimitKind kind;
      if (!parseAlertKind(kindStr, kind)) {
        logWarn("Invalid alert kind");
        return;
      }

      const float tempC = tempStr.toFloat();
      const float rh = rhStr.toFloat();
      SHT3x::Status st = deviceInstance.writeAlertLimit(kind, tempC, rh);
      printStatus(st);
      return;
    }

    if (sub == "encode") {
      const int split2 = rest.indexOf(' ');
      if (split2 < 0) {
        logWarn("Usage: alert encode <T> <RH>");
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
        logWarn("Usage: alert decode <hex>");
        return;
      }
      float tempC = 0.0f;
      float rh = 0.0f;
      SHT3x::SHT3x::decodeAlertLimit(raw, tempC, rh);
      Serial.printf("Alert decoded: T=%.2fC RH=%.2f%%\n",
                    static_cast<double>(tempC),
                    static_cast<double>(rh));
      return;
    }

    if (sub == "disable") {
      SHT3x::Status st = deviceInstance.disableAlerts();
      printStatus(st);
      return;
    }

    logWarn("Usage: alert read|write|raw|encode|decode|disable ...");
    return;
  }

  if (cmd.startsWith("convert ")) {
    CliString args = cmd.substring(8);
    args.trim();
    const int split = args.indexOf(' ');
    if (split < 0) {
      logWarn("Usage: convert <rawT> <rawRH>");
      return;
    }
    const CliString tStr = args.substring(0, split);
    CliString rhStr = args.substring(split + 1);
    rhStr.trim();

    uint16_t rawT = 0;
    uint16_t rawRh = 0;
    if (!parseU16(tStr, rawT) || !parseU16(rhStr, rawRh)) {
      logWarn("Invalid raw values");
      return;
    }

    const float tempC = SHT3x::SHT3x::convertTemperatureC(rawT);
    const float rh = SHT3x::SHT3x::convertHumidityPct(rawRh);
    const int32_t tempC_x100 = SHT3x::SHT3x::convertTemperatureC_x100(rawT);
    const uint32_t rh_x100 = SHT3x::SHT3x::convertHumidityPct_x100(rawRh);
    Serial.printf("Converted: T=%.2fC (%ld) RH=%.2f%% (%lu)\n",
                  static_cast<double>(tempC), static_cast<long>(tempC_x100),
                  static_cast<double>(rh), static_cast<unsigned long>(rh_x100));
    return;
  }

  if (cmd == "reset") {
    cancelPending();
    SHT3x::Status st = deviceInstance.softReset();
    printStatus(st);
    return;
  }

  if (cmd == "defaults") {
    cancelPending();
    SHT3x::Status st = deviceInstance.resetToDefaults();
    printStatus(st);
    return;
  }

  if (cmd == "restore") {
    cancelPending();
    SHT3x::Status st = deviceInstance.resetAndRestore();
    printStatus(st);
    return;
  }

  if (cmd == "iface_reset") {
    SHT3x::Status st = deviceInstance.interfaceReset();
    printStatus(st);
    return;
  }

  if (cmd == "greset") {
    SHT3x::Status st = deviceInstance.generalCallReset();
    printStatus(st);
    return;
  }

  if (cmd == "online") {
    Serial.printf("Online: %s\n", deviceInstance.isOnline() ? "YES" : "NO");
    return;
  }

  if (cmd == "stats") {
    printRuntimeStats();
    return;
  }

  if (cmd == "begin") {
    if (!configIsReady) {
      logWarn("Config not ready");
      return;
    }
    cancelPending();
    SHT3x::Status st = deviceInstance.begin(configInstance);
    printStatus(st);
    return;
  }

  if (cmd == "end") {
    cancelPending();
    deviceInstance.end();
    logInfo("Driver ended");
    return;
  }

  if (cmd == "drv") {
    printDriverHealth();
    printConfig();
    return;
  }

  if (cmd == "state") {
    printHealthView(deviceInstance);
    return;
  }

  if (cmd == "probe") {
    logInfo("Probing device (no health tracking)...");
    HealthSnapshot<SHT3x::SHT3x> before;
    before.capture(deviceInstance);
    SHT3x::Status st = deviceInstance.probe();
    printStatus(st);
    HealthSnapshot<SHT3x::SHT3x> after;
    after.capture(deviceInstance);
    Serial.println("  Health changes:");
    printHealthDiff(before, after);
    return;
  }

  if (cmd == "recover") {
    logInfo("Attempting recovery...");
    HealthSnapshot<SHT3x::SHT3x> before;
    before.capture(deviceInstance);
    SHT3x::Status st = deviceInstance.recover();
    printStatus(st);
    HealthSnapshot<SHT3x::SHT3x> after;
    after.capture(deviceInstance);
    Serial.println("  Health changes:");
    printHealthDiff(before, after);
    printDriverHealth();
    return;
  }

  if (cmd == "verbose") {
    printVerboseState();
    return;
  }

  if (cmd.startsWith("verbose ")) {
    const int val = static_cast<int>(cmd.substring(8).toInt());
    verboseMode = (val != 0);
    logInfo("Verbose mode: %s%s%s",
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
    int count = static_cast<int>(cmd.substring(11).toInt());
    if (count <= 0 || count > 100000) {
      logWarn("Invalid stress_mix count");
      return;
    }
    runStressMix(count);
    return;
  }

  if (cmd.startsWith("stress")) {
    int count = 10;
    if (cmd.length() > 6U) {
      count = static_cast<int>(cmd.substring(6).toInt());
    }
    if (count <= 0) {
      logWarn("Invalid stress count");
      return;
    }

    cancelPending();
    stressRemaining = count;
    resetStressStats(count);
    logInfo("Starting stress test: %d cycles", count);
    return;
  }

  logWarn("Unknown command: %s", cmd.c_str());
}

}  // namespace

SHT3x::SHT3x& device() {
  return deviceInstance;
}

SHT3x::Config& config() {
  return configInstance;
}

bool& configReady() {
  return configIsReady;
}

void setPlatform(const Platform& nextPlatform) {
  platform = nextPlatform;
}

void printPrompt() {
  Serial.print("> ");
}

void printHelp() {
  Serial.println();
  cli::printHelpHeader("SHT3x CLI Help");
  cli::printHelpSection("Common");
  cli::printHelpItem("help / ?", "Show this help");
  cli::printHelpItem("version / ver", "Print firmware and library version info");
  cli::printHelpItem("scan", "Scan I2C bus");
  cli::printHelpItem("read", "Request measurement (single-shot or periodic)");
  cli::printHelpItem("raw", "Print last raw sample");
  cli::printHelpItem("comp", "Print last compensated sample");
  cli::printHelpItem("meastime", "Show estimated measurement time");

  cli::printHelpSection("Operating Mode");
  cli::printHelpItem("mode [single|periodic|art]", "Set or show operating mode");
  cli::printHelpItem("start_periodic <rate> <rep>", "Start periodic mode");
  cli::printHelpItem("start_art", "Start ART mode");
  cli::printHelpItem("stop_periodic", "Stop periodic or ART mode");
  cli::printHelpItem("repeat [low|med|high]", "Set or show repeatability");
  cli::printHelpItem("rate [0.5|1|2|4|10]", "Set or show periodic rate");
  cli::printHelpItem("stretch [0|1]", "Set or show clock stretching");

  cli::printHelpSection("Status And Alerts");
  cli::printHelpItem("status", "Read status register");
  cli::printHelpItem("status_raw", "Read raw status (16-bit)");
  cli::printHelpItem("clearstatus", "Clear status flags");
  cli::printHelpItem("heater [on|off|status]", "Control heater");
  cli::printHelpItem("serial [stretch|nostretch]", "Read serial number");
  cli::printHelpItem("command write <hex>", "Issue a raw 16-bit command");
  cli::printHelpItem("command write_data <cmd> <data>", "Issue a command with a packed data word");
  cli::printHelpItem("command read <cmd> <len>", "Issue a command and read raw response bytes");
  cli::printHelpItem("alert read <hs|hc|lc|ls>", "Read alert limit");
  cli::printHelpItem("alert write <kind> <T> <RH>", "Write alert limit");
  cli::printHelpItem("alert raw read <kind>", "Read raw alert limit word");
  cli::printHelpItem("alert raw write <kind> <hex>", "Write raw alert limit word");
  cli::printHelpItem("alert encode <T> <RH>", "Encode alert limit word");
  cli::printHelpItem("alert decode <hex>", "Decode alert limit word");
  cli::printHelpItem("alert disable", "Disable alerts (LowSet > HighSet)");
  cli::printHelpItem("convert <rawT> <rawRH>", "Convert raw values");

  cli::printHelpSection("Lifecycle And Diagnostics");
  cli::printHelpItem("reset", "Soft reset device");
  cli::printHelpItem("defaults", "Reset command-mode defaults");
  cli::printHelpItem("restore", "Reset sensor and restore cached settings");
  cli::printHelpItem("iface_reset", "Interface reset (SCL pulse)");
  cli::printHelpItem("greset", "General-call reset (bus-wide)");
  cli::printHelpItem("stats", "Runtime counters and cached settings");
  cli::printHelpItem("cfg / settings", "Show current config");
  cli::printHelpItem("drv", "Show driver state and health");
  cli::printHelpItem("online", "Show online state");
  cli::printHelpItem("begin", "Re-initialize device");
  cli::printHelpItem("end", "End driver session");
  cli::printHelpItem("state", "Show compact one-line health summary");
  cli::printHelpItem("probe", "Probe device (no health tracking)");
  cli::printHelpItem("recover", "Manual recovery attempt");
  cli::printHelpItem("verbose [0|1]", "Enable/disable verbose output");
  cli::printHelpItem("stress [N]", "Run N measurement cycles");
  cli::printHelpItem("stress_mix [N]", "Run N mixed-operation cycles");
  cli::printHelpItem("selftest", "Run safe command self-test report");
}

void printVersionInfo() {
  const char* date = platform.buildDate != nullptr ? platform.buildDate : __DATE__;
  const char* time = platform.buildTime != nullptr ? platform.buildTime : __TIME__;
  Serial.println("=== Version Info ===");
  Serial.printf("  Example firmware build: %s %s\n", date, time);
  Serial.printf("  SHT3x library version: %s\n", SHT3x::VERSION);
  Serial.printf("  SHT3x library full: %s\n", SHT3x::VERSION_FULL);
  Serial.printf("  SHT3x library build: %s\n", SHT3x::BUILD_TIMESTAMP);
  Serial.printf("  SHT3x library commit: %s (%s)\n", SHT3x::GIT_COMMIT, SHT3x::GIT_STATUS);
}

void printDriverHealth() {
  const uint32_t now = millis();
  const uint32_t totalOk = deviceInstance.totalSuccess();
  const uint32_t totalFail = deviceInstance.totalFailures();
  const uint32_t total = totalOk + totalFail;
  const float successRate = (total > 0U)
                                ? (100.0f * static_cast<float>(totalOk) / static_cast<float>(total))
                                : 0.0f;
  const SHT3x::Status lastErr = deviceInstance.lastError();
  const SHT3x::DriverState st = deviceInstance.state();
  const bool online = deviceInstance.isOnline();

  Serial.println("=== Driver Health ===");
  Serial.printf("  State: %s%s%s\n",
                stateColor(st, online, deviceInstance.consecutiveFailures()),
                stateToStr(st),
                LOG_COLOR_RESET);
  Serial.printf("  Online: %s%s%s\n",
                online ? LOG_COLOR_GREEN : LOG_COLOR_RED,
                log_bool_str(online),
                LOG_COLOR_RESET);
  Serial.printf("  Consecutive failures: %s%u%s\n",
                goodIfZeroColor(deviceInstance.consecutiveFailures()),
                deviceInstance.consecutiveFailures(),
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
                static_cast<double>(successRate),
                LOG_COLOR_RESET);

  const uint32_t lastOkMs = deviceInstance.lastOkMs();
  if (lastOkMs > 0U) {
    Serial.printf("  Last OK: %lu ms ago (at %lu ms)\n",
                  static_cast<unsigned long>(now - lastOkMs),
                  static_cast<unsigned long>(lastOkMs));
  } else {
    Serial.println("  Last OK: never");
  }

  const uint32_t lastErrorMs = deviceInstance.lastErrorMs();
  if (lastErrorMs > 0U) {
    Serial.printf("  Last error: %lu ms ago (at %lu ms)\n",
                  static_cast<unsigned long>(now - lastErrorMs),
                  static_cast<unsigned long>(lastErrorMs));
  } else {
    Serial.println("  Last error: never");
  }

  if (!lastErr.ok()) {
    Serial.printf("  Error code: %s%s%s\n", LOG_COLOR_RED, errToStr(lastErr.code), LOG_COLOR_RESET);
    Serial.printf("  Error detail: %ld\n", static_cast<long>(lastErr.detail));
    if (lastErr.msg && lastErr.msg[0]) {
      Serial.printf("  Error msg: %s\n", lastErr.msg);
    }
  }
}

void processCommand(const char* line) {
  processCommandString(CliString(line));
}

void tick() {
  deviceInstance.tick(millis());

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
}

void cancelPending() {
  pendingRead = false;
  stressRemaining = 0;
  stressStats.active = false;
}

void logInfo(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlogTagged(LOG_COLOR_CYAN, "I", fmt, args);
  va_end(args);
}

void logWarn(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlogTagged(LOG_COLOR_YELLOW, "W", fmt, args);
  va_end(args);
}

void logError(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vlogTagged(LOG_COLOR_RED, "E", fmt, args);
  va_end(args);
}

}  // namespace sht3x_cli
