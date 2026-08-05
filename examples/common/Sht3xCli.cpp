#include "Sht3xCli.h"

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace sht3x_cli {

static SHT3x::Status cancelPending(void);

namespace {

static constexpr size_t MAX_STRING_LEN = 160U;
static constexpr uint32_t STRESS_PROGRESS_UPDATES = 10U;
static constexpr uint32_t I2C_SOAK_MAX_SECONDS = 24UL * 60UL * 60UL;
static constexpr uint32_t MEASUREMENT_JOB_TIMEOUT_MS = 500U;
static constexpr uint32_t MANUAL_JOB_TIMEOUT_MS = 5000U;
static constexpr size_t MAX_CLI_ARGS = 8U;

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
  explicit CliString(const char* value) { assign(value); }

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

  bool operator==(const char* rhs) const {
    return rhs != nullptr && std::strcmp(_buf, rhs) == 0;
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

struct ParsedArgs {
  char storage[MAX_STRING_LEN] = {};
  const char* values[MAX_CLI_ARGS] = {};
  size_t count = 0U;
  bool tooMany = false;
};

void parseArguments(const CliString& command, ParsedArgs& out) {
  out = ParsedArgs{};
  std::strncpy(out.storage, command.c_str(), sizeof(out.storage) - 1U);
  char* cursor = out.storage;
  while (*cursor != '\0') {
    while (*cursor != '\0' &&
           std::isspace(static_cast<unsigned char>(*cursor)) != 0) {
      ++cursor;
    }
    if (*cursor == '\0') {
      break;
    }
    if (out.count >= MAX_CLI_ARGS) {
      out.tooMany = true;
      out.count = 0U;
      return;
    }
    out.values[out.count++] = cursor;
    while (*cursor != '\0' &&
           std::isspace(static_cast<unsigned char>(*cursor)) == 0) {
      ++cursor;
    }
    if (*cursor != '\0') {
      *cursor++ = '\0';
    }
  }
}

bool tokenEquals(const ParsedArgs& args, size_t index, const char* value) {
  return index < args.count && value != nullptr &&
         std::strcmp(args.values[index], value) == 0;
}

bool countIs(const ParsedArgs& args, size_t first, size_t second = 0U) {
  return args.count == first || (second != 0U && args.count == second);
}

bool isOneOf(const char* value, const char* const* choices, size_t count) {
  if (value == nullptr) {
    return false;
  }
  for (size_t i = 0; i < count; ++i) {
    if (std::strcmp(value, choices[i]) == 0) {
      return true;
    }
  }
  return false;
}

bool validCommandArity(const ParsedArgs& args, bool& knownCommand) {
  knownCommand = true;
  if (args.count == 0U) {
    return true;
  }
  const char* command = args.values[0];
  static constexpr const char* NO_ARG_COMMANDS[] = {
      "help", "?", "version", "ver", "scan", "request", "fetch",
      "result", "cancel", "xfer_reset", "xfer_stats", "read", "raw",
      "comp", "meastime", "cfg", "settings", "start_art",
      "stop_periodic", "status", "status_raw", "online", "stats", "begin",
      "end", "drv", "state", "probe"};
  if (isOneOf(command, NO_ARG_COMMANDS,
              sizeof(NO_ARG_COMMANDS) / sizeof(NO_ARG_COMMANDS[0]))) {
    return args.count == 1U;
  }
  if (std::strcmp(command, "job") == 0) {
    if (args.count == 1U) return true;
    if (args.count == 2U) {
      return tokenEquals(args, 1U, "current") || tokenEquals(args, 1U, "last") ||
             tokenEquals(args, 1U, "cancel");
    }
    return args.count == 3U && tokenEquals(args, 1U, "step");
  }
  if (std::strcmp(command, "xfer_assert") == 0) return args.count == 4U;
  if (std::strcmp(command, "command") == 0) {
    if (tokenEquals(args, 1U, "write")) {
      return args.count == 3U || (args.count == 4U && tokenEquals(args, 3U, "confirm"));
    }
    if (tokenEquals(args, 1U, "write_data") || tokenEquals(args, 1U, "read")) {
      return args.count == 4U || (args.count == 5U && tokenEquals(args, 4U, "confirm"));
    }
    return args.count == 2U;
  }
  if (std::strcmp(command, "mode") == 0 || std::strcmp(command, "repeat") == 0 ||
      std::strcmp(command, "rate") == 0 || std::strcmp(command, "stretch") == 0 ||
      std::strcmp(command, "serial") == 0 || std::strcmp(command, "verbose") == 0 ||
      std::strcmp(command, "stress") == 0 || std::strcmp(command, "stress_mix") == 0) {
    return countIs(args, 1U, 2U);
  }
  if (std::strcmp(command, "single") == 0 ||
      std::strcmp(command, "i2c_soak") == 0) return args.count == 2U;
  if (std::strcmp(command, "periodic") == 0) {
    return tokenEquals(args, 1U, "start") ? args.count == 4U : args.count == 2U;
  }
  if (std::strcmp(command, "art") == 0) return args.count == 2U;
  if (std::strcmp(command, "start_periodic") == 0) return args.count == 3U;
  if (std::strcmp(command, "status_restore") == 0 ||
      std::strcmp(command, "clearstatus") == 0 ||
      std::strcmp(command, "clear_status") == 0 ||
      std::strcmp(command, "reset") == 0 || std::strcmp(command, "defaults") == 0 ||
      std::strcmp(command, "restore") == 0 ||
      std::strcmp(command, "iface_reset") == 0 ||
      std::strcmp(command, "recover") == 0 ||
      std::strcmp(command, "selftest") == 0) {
    return args.count == 1U || (args.count == 2U && tokenEquals(args, 1U, "confirm"));
  }
  if (std::strcmp(command, "heater") == 0) {
    if (tokenEquals(args, 1U, "on")) {
      return args.count == 2U || (args.count == 3U && tokenEquals(args, 2U, "confirm"));
    }
    return countIs(args, 1U, 2U);
  }
  if (std::strcmp(command, "alert") == 0) {
    if (tokenEquals(args, 1U, "show")) return args.count == 2U;
    if (tokenEquals(args, 1U, "read") || tokenEquals(args, 1U, "decode")) {
      return args.count == 3U;
    }
    if (tokenEquals(args, 1U, "encode")) return args.count == 4U;
    if (tokenEquals(args, 1U, "disable")) {
      return args.count == 2U || (args.count == 3U && tokenEquals(args, 2U, "confirm"));
    }
    if (tokenEquals(args, 1U, "set") || tokenEquals(args, 1U, "write")) {
      return args.count == 5U || (args.count == 6U && tokenEquals(args, 5U, "confirm"));
    }
    if (tokenEquals(args, 1U, "raw") && tokenEquals(args, 2U, "read")) {
      return args.count == 4U;
    }
    if (tokenEquals(args, 1U, "raw") && tokenEquals(args, 2U, "write")) {
      return args.count == 5U || (args.count == 6U && tokenEquals(args, 5U, "confirm"));
    }
    return args.count == 2U;
  }
  if (std::strcmp(command, "convert") == 0) return args.count == 3U;
  if (std::strcmp(command, "greset") == 0) {
    return args.count == 2U &&
           (tokenEquals(args, 1U, "arm") || tokenEquals(args, 1U, "disarm") ||
            tokenEquals(args, 1U, "confirm"));
  }

  knownCommand = false;
  return true;
}

const char* confirmationEffect(const ParsedArgs& args) {
  if (args.count == 0U) return nullptr;
  const char* command = args.values[0];
  if (std::strcmp(command, "command") == 0) return "issue an unrestricted raw sensor command";
  if (std::strcmp(command, "status_restore") == 0) return "interrupt and restore the active acquisition mode";
  if (std::strcmp(command, "clearstatus") == 0 || std::strcmp(command, "clear_status") == 0) return "clear sticky sensor status flags";
  if (std::strcmp(command, "heater") == 0 && tokenEquals(args, 1U, "on")) return "enable the sensor heater";
  if (std::strcmp(command, "alert") == 0 && args.count > 1U) {
    if (tokenEquals(args, 1U, "set") || tokenEquals(args, 1U, "write") ||
        tokenEquals(args, 1U, "disable") ||
        (tokenEquals(args, 1U, "raw") && tokenEquals(args, 2U, "write"))) {
      return "change persistent alert-limit state";
    }
  }
  if (std::strcmp(command, "reset") == 0 || std::strcmp(command, "defaults") == 0 ||
      std::strcmp(command, "restore") == 0 || std::strcmp(command, "iface_reset") == 0) {
    return "reset or reconfigure sensor hardware state";
  }
  if (std::strcmp(command, "recover") == 0) return "run a sensor recovery sequence";
  if (std::strcmp(command, "selftest") == 0) return "run a diagnostic that includes reset and restore operations";
  return nullptr;
}

bool requireConfirmation(CliString& command, const ParsedArgs& args,
                         const char* effect) {
  if (effect == nullptr) return true;
  const bool confirmed = args.count > 0U && tokenEquals(args, args.count - 1U, "confirm");
  if (!confirmed) {
    logWarn("Would %s", effect);
    logWarn("Confirmation required before any command parameters are applied");
    logWarn("Use exactly: %s confirm", command.c_str());
    return false;
  }
  const size_t length = command.length();
  size_t tokenStart = length;
  while (tokenStart > 0U &&
         std::isspace(static_cast<unsigned char>(command.c_str()[tokenStart - 1U])) == 0) {
    --tokenStart;
  }
  command = command.substring(0, static_cast<int>(tokenStart));
  command.trim();
  return true;
}

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
bool ownerJobActive = false;
bool manualJobControl = false;
SHT3x::JobType pendingJobType = SHT3x::JobType::NONE;
const char* pendingTerminalLabel = "job";
uint32_t pendingStartMs = 0;
uint32_t pendingRequestId = 0;
uint32_t nextRequestId = 1;
bool lastJobValid = false;
SHT3x::PollJobResult lastJobResult;
SHT3x::Status lastJobPollStatus = SHT3x::Status::Ok();
bool generalCallArmed = false;
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
    case Err::CANCELLED: return "CANCELLED";
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
  if (!verboseMode) {
    return;
  }
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

const char* statusKindToStr(const SHT3x::Status& st) {
  if (st.ok()) {
    return "OK";
  }
  if (st.inProgress()) {
    return "IN_PROGRESS";
  }
  if (st.code == SHT3x::Err::CANCELLED) {
    return "CANCELLED";
  }
  if (st.code == SHT3x::Err::TIMEOUT) {
    return "TIMEOUT";
  }
  return "ERR";
}

void printLabeledStatus(const char* label, const SHT3x::Status& st) {
  Serial.printf("%s: %s code=%u detail=%ld msg=%s\n",
                label,
                statusKindToStr(st),
                static_cast<unsigned>(st.code),
                static_cast<long>(st.detail),
                st.msg ? st.msg : "");
}

void printStatusRegisterLine(const char* label, const SHT3x::StatusRegister& reg) {
  Serial.printf("%s: raw=0x%04X alert=%d heater=%d rh_alert=%d t_alert=%d reset=%d cmd_err=%d crc_err=%d\n",
                label,
                static_cast<unsigned>(reg.raw),
                reg.alertPending ? 1 : 0,
                reg.heaterOn ? 1 : 0,
                reg.rhAlert ? 1 : 0,
                reg.tAlert ? 1 : 0,
                reg.resetDetected ? 1 : 0,
                reg.commandError ? 1 : 0,
                reg.writeCrcError ? 1 : 0);
}

const char* modeToStr(SHT3x::Mode mode);

void printStatusRestoreSnapshot(const SHT3x::Status& result,
                                const SHT3x::StatusReadSnapshot& snap) {
  Serial.println("status_restore:");
  printLabeledStatus("result", result);
  Serial.printf("initialMode=%s finalMode=%s modeInterrupted=%d statusValid=%d restored=%d\n",
                modeToStr(snap.initialMode),
                modeToStr(snap.finalMode),
                snap.modeInterrupted ? 1 : 0,
                snap.statusValid ? 1 : 0,
                snap.restored ? 1 : 0);
  printLabeledStatus("stopStatus", snap.stopStatus);
  printLabeledStatus("statusReadStatus", snap.statusReadStatus);
  printLabeledStatus("restoreStatus", snap.restoreStatus);
  if (snap.statusValid) {
    printStatusRegisterLine("status", snap.status);
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

void printConfig(bool readSensorStatus = false) {
  SHT3x::SettingsSnapshot snap;
  SHT3x::Status st = readSensorStatus ? deviceInstance.readSettings(snap)
                                      : deviceInstance.getSettings(snap);
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
    static constexpr SHT3x::AlertLimitKind ALERT_KINDS[] = {
        SHT3x::AlertLimitKind::HIGH_SET,
        SHT3x::AlertLimitKind::HIGH_CLEAR,
        SHT3x::AlertLimitKind::LOW_CLEAR,
        SHT3x::AlertLimitKind::LOW_SET,
    };
    for (size_t i = 0; i < (sizeof(ALERT_KINDS) / sizeof(ALERT_KINDS[0])); ++i) {
      Serial.printf("    alert %s: valid=%s raw=0x%04X\n",
                    alertKindToStr(ALERT_KINDS[i]),
                    cached.alertValid[i] ? "true" : "false",
                    static_cast<unsigned>(cached.alertRaw[i]));
    }
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
  stressStats.endMs = millis();
  const uint32_t successDelta = deviceInstance.totalSuccess() - stressStats.successBefore;
  const uint32_t failDelta = deviceInstance.totalFailures() - stressStats.failBefore;
  const uint32_t durationMs = stressStats.endMs - stressStats.startMs;
  const float successPct =
      (stressStats.attempts > 0)
          ? (100.0f * static_cast<float>(stressStats.success) /
             static_cast<float>(stressStats.attempts))
          : 0.0f;

  Serial.printf("stress: ok=%d fail=%lu attempts=%d target=%d duration_ms=%lu\n",
                stressStats.success,
                static_cast<unsigned long>(stressStats.errors),
                stressStats.attempts,
                stressStats.target,
                static_cast<unsigned long>(durationMs));
  if (!verboseMode) {
    return;
  }

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

uint32_t allocateRequestId() {
  const uint32_t requestId = nextRequestId++;
  if (nextRequestId == 0U) {
    nextRequestId = 1U;
  }
  return requestId;
}

bool isExpectedMeasurementTerminal(const SHT3x::PollJobResult& result,
                                   uint32_t requestId) {
  return result.terminal && result.requestId == requestId &&
         result.type == SHT3x::JobType::MEASUREMENT;
}

void clearPendingOwner() {
  ownerJobActive = false;
  manualJobControl = false;
  pendingRequestId = 0U;
  pendingJobType = SHT3x::JobType::NONE;
  pendingTerminalLabel = "job";
}

void quarantinePendingOwner() {
  SHT3x::PollJobResult ignored;
  (void)deviceInstance.cancelJob(SHT3x::CancelReason::REQUESTED, ignored);
  lastJobValid = false;
  clearPendingOwner();
}

SHT3x::Status validateOwnedResult(const SHT3x::PollJobResult& result,
                                  uint8_t maxInstructions,
                                  const SHT3x::Status& callStatus) {
  if (!ownerJobActive || pendingRequestId == 0U ||
      result.requestId != pendingRequestId || result.type != pendingJobType) {
    return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                "CLI owner job identity mismatch");
  }
  const uint8_t instructionLimit = maxInstructions == 0U ? 0U : 1U;
  if (result.instructionsUsed > instructionLimit) {
    return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                "CLI owner job exceeded transfer budget");
  }
  if (result.status.code != callStatus.code ||
      result.status.detail != callStatus.detail) {
    return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                "CLI owner call/result status mismatch");
  }
  if (!result.terminal) {
    return result.active && !result.completed &&
                   result.outcome == SHT3x::JobOutcome::ACTIVE &&
                   result.status.code == SHT3x::Err::IN_PROGRESS
               ? SHT3x::Status::Ok()
               : SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                      "Invalid active CLI owner result");
  }
  if (result.active || result.outcome == SHT3x::JobOutcome::NONE ||
      result.outcome == SHT3x::JobOutcome::ACTIVE) {
    return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                "Invalid terminal CLI owner result");
  }
  if (result.outcome == SHT3x::JobOutcome::SUCCEEDED) {
    const bool measurementValid =
        result.type == SHT3x::JobType::MEASUREMENT && result.completed &&
        result.effect == SHT3x::JobEffect::NONE;
    const bool ensureValid =
        result.type == SHT3x::JobType::ENSURE_IDLE && !result.completed &&
        result.effect == SHT3x::JobEffect::DEVICE_STATE_CHANGED;
    if (!result.status.ok() || (!measurementValid && !ensureValid)) {
      return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                  "Invalid successful CLI owner result");
    }
  } else if (result.completed) {
    return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                "Failed CLI owner result completed a sample");
  } else if (result.outcome == SHT3x::JobOutcome::CANCELLED &&
             result.status.code != SHT3x::Err::CANCELLED) {
    return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                "Cancelled CLI owner status mismatch");
  } else if (result.outcome == SHT3x::JobOutcome::TIMED_OUT &&
             result.status.code != SHT3x::Err::TIMEOUT &&
             result.status.code != SHT3x::Err::I2C_TIMEOUT) {
    return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                "Timed-out CLI owner status mismatch");
  } else if (result.outcome == SHT3x::JobOutcome::FAILED && result.status.ok()) {
    return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                "Failed CLI owner result has OK status");
  }
  return SHT3x::Status::Ok();
}

bool rejectActiveOwnerJob(const char* label) {
  if (!ownerJobActive) {
    return false;
  }
  printLabeledStatus(
      label,
      SHT3x::Status::Error(
          SHT3x::Err::BUSY,
          "Complete or explicitly cancel the active owner job"));
  return true;
}

void rememberJobResult(const SHT3x::Status& pollStatus,
                       const SHT3x::PollJobResult& result) {
  if (result.requestId == 0U && !result.terminal) {
    return;
  }
  lastJobPollStatus = pollStatus;
  lastJobResult = result;
  lastJobValid = true;
}

void printJobResult(const char* label, const SHT3x::Status& pollStatus,
                    const SHT3x::PollJobResult& result) {
  Serial.printf(
      "%s: request=%lu type=%u phase=%u outcome=%u effect=%u active=%u "
      "completed=%u terminal=%u instructions=%u status=%s code=%u detail=%ld\n",
      label,
      static_cast<unsigned long>(result.requestId),
      static_cast<unsigned>(result.type),
      static_cast<unsigned>(result.phase),
      static_cast<unsigned>(result.outcome),
      static_cast<unsigned>(result.effect),
      result.active ? 1U : 0U,
      result.completed ? 1U : 0U,
      result.terminal ? 1U : 0U,
      static_cast<unsigned>(result.instructionsUsed),
      statusKindToStr(pollStatus),
      static_cast<unsigned>(pollStatus.code),
      static_cast<long>(pollStatus.detail));
}

void printLastJobResult() {
  if (!lastJobValid) {
    Serial.println("result: none");
    return;
  }
  printJobResult("result", lastJobPollStatus, lastJobResult);
}

SHT3x::Status readTerminalMeasurementMilli(const SHT3x::PollJobResult& result,
                                           uint32_t requestId,
                                           SHT3x::MeasurementMilli& out) {
  if (!isExpectedMeasurementTerminal(result, requestId)) {
    return SHT3x::Status::Error(SHT3x::Err::BUSY,
                                "Unexpected measurement job identity");
  }
  if (result.outcome != SHT3x::JobOutcome::SUCCEEDED || !result.completed ||
      result.effect != SHT3x::JobEffect::NONE || !result.status.ok()) {
    return result.status.ok()
               ? SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                      "Invalid measurement terminal result")
               : result.status;
  }
  return deviceInstance.getMeasurementMilli(out);
}

SHT3x::Status performMeasurementMilliBlocking(SHT3x::MeasurementMilli& out,
                                               uint32_t timeoutMs =
                                                   MEASUREMENT_JOB_TIMEOUT_MS) {
  const uint32_t startMs = millis();
  const uint32_t requestId = allocateRequestId();
  SHT3x::JobRequest request;
  request.requestId = requestId;
  request.deadlineMs = startMs + timeoutMs;
  request.hasDeadline = true;

  SHT3x::Status st = deviceInstance.requestMeasurement(request);
  if (st.code != SHT3x::Err::IN_PROGRESS) {
    return st;
  }

  while (true) {
    const uint32_t nowMs = millis();
    SHT3x::PollJobResult result;
    st = deviceInstance.pollJob(nowMs, 1, result);
    if (result.terminal) {
      return readTerminalMeasurementMilli(result, requestId, out);
    }
    if (st.code != SHT3x::Err::IN_PROGRESS) {
      return st;
    }
    if ((nowMs - startMs) >= timeoutMs) {
      SHT3x::PollJobResult cancelled;
      const SHT3x::Status cancelStatus =
          deviceInstance.cancelJob(SHT3x::CancelReason::DEADLINE_EXPIRED,
                                   cancelled);
      if (!isExpectedMeasurementTerminal(cancelled, requestId)) {
        return SHT3x::Status::Error(SHT3x::Err::BUSY,
                                    "Unexpected cancelled job identity");
      }
      return cancelStatus;
    }
    yield();
  }
}

SHT3x::Status performMeasurementBlocking(
    SHT3x::Measurement& out,
    uint32_t timeoutMs = MEASUREMENT_JOB_TIMEOUT_MS) {
  SHT3x::MeasurementMilli milli;
  const SHT3x::Status st = performMeasurementMilliBlocking(milli, timeoutMs);
  if (!st.ok()) {
    return st;
  }
  out.temperatureC = static_cast<float>(milli.temperatureMilliCelsius) / 1000.0f;
  out.humidityPct = static_cast<float>(milli.humidityMilliPercent) / 1000.0f;
  return SHT3x::Status::Ok();
}

SHT3x::Status performNoStretchMeasurementBlocking(SHT3x::Measurement& out,
                                                  uint32_t timeoutMs = 500) {
  SHT3x::Status st =
      deviceInstance.setClockStretching(SHT3x::ClockStretching::STRETCH_DISABLED);
  if (!st.ok()) {
    return st;
  }
  return performMeasurementBlocking(out, timeoutMs);
}

void runStress(int count) {
  if (rejectActiveOwnerJob("stress")) {
    return;
  }
  resetStressStats(count);
  if (verboseMode) {
    logInfo("Starting stress test: %d cycles", count);
  }

  for (int i = 0; i < count; ++i) {
    SHT3x::Measurement measurement;
    const SHT3x::Status st = performMeasurementBlocking(measurement);
    if (st.ok()) {
      updateStressStats(measurement);
    } else {
      noteStressError(st);
    }
    stressStats.attempts++;
    printStressProgress(static_cast<uint32_t>(stressStats.attempts),
                        static_cast<uint32_t>(stressStats.target),
                        static_cast<uint32_t>(stressStats.success),
                        stressStats.errors);
    yield();
  }

  finishStressStats();
}

void runI2cSoak(uint32_t durationS) {
  if (rejectActiveOwnerJob("i2c_soak")) {
    return;
  }
  const uint32_t durationMs = durationS * 1000UL;
  uint32_t okCount = 0;
  uint32_t failCount = 0;
  bool hasSample = false;
  float minTemp = 0.0f;
  float maxTemp = 0.0f;
  float minHumidity = 0.0f;
  float maxHumidity = 0.0f;

  SHT3x::Status st = deviceInstance.setMode(SHT3x::Mode::SINGLE_SHOT);
  if (!st.ok()) {
    failCount++;
  }
  if (st.ok()) {
    st = deviceInstance.setClockStretching(SHT3x::ClockStretching::STRETCH_DISABLED);
    if (!st.ok()) {
      failCount++;
    }
  }

  const uint32_t startMs = millis();
  const uint32_t successBefore = deviceInstance.totalSuccess();
  const uint32_t failBefore = deviceInstance.totalFailures();
  const uint32_t transportSuccessBefore = deviceInstance.transportSuccess();
  const uint32_t transportFailBefore = deviceInstance.transportFailures();
  const uint32_t protocolFailBefore = deviceInstance.protocolFailures();
  const uint32_t notReadyBefore = deviceInstance.totalNotReady();

  while (st.ok() && (millis() - startMs) < durationMs) {
    SHT3x::Measurement measurement;
    st = performMeasurementBlocking(measurement);
    if (st.ok()) {
      okCount++;
      if (!hasSample) {
        minTemp = measurement.temperatureC;
        maxTemp = measurement.temperatureC;
        minHumidity = measurement.humidityPct;
        maxHumidity = measurement.humidityPct;
        hasSample = true;
      } else {
        if (measurement.temperatureC < minTemp) minTemp = measurement.temperatureC;
        if (measurement.temperatureC > maxTemp) maxTemp = measurement.temperatureC;
        if (measurement.humidityPct < minHumidity) minHumidity = measurement.humidityPct;
        if (measurement.humidityPct > maxHumidity) maxHumidity = measurement.humidityPct;
      }
    } else {
      failCount++;
    }
    yield();
  }

  const uint32_t elapsedMs = millis() - startMs;
  const uint32_t successDelta = deviceInstance.totalSuccess() - successBefore;
  const uint32_t failDelta = deviceInstance.totalFailures() - failBefore;
  const uint32_t transportSuccessDelta =
      deviceInstance.transportSuccess() - transportSuccessBefore;
  const uint32_t transportFailDelta =
      deviceInstance.transportFailures() - transportFailBefore;
  const uint32_t protocolFailDelta =
      deviceInstance.protocolFailures() - protocolFailBefore;
  const uint32_t notReadyDelta = deviceInstance.totalNotReady() - notReadyBefore;
  // Keep every record below OutputProxy's fixed formatting buffer. Splitting
  // the evidence also makes truncation fail visibly at the host token checks.
  Serial.printf(
      "i2c_soak: ok=%lu fail=%lu duration_ms=%lu\n",
      static_cast<unsigned long>(okCount),
      static_cast<unsigned long>(failCount),
      static_cast<unsigned long>(elapsedMs));
  Serial.printf(
      "i2c_soak: temp_min=%.2f temp_max=%.2f humidity_min=%.2f "
      "humidity_max=%.2f\n",
      static_cast<double>(hasSample ? minTemp : 0.0f),
      static_cast<double>(hasSample ? maxTemp : 0.0f),
      static_cast<double>(hasSample ? minHumidity : 0.0f),
      static_cast<double>(hasSample ? maxHumidity : 0.0f));
  Serial.printf(
      "i2c_soak: health_ok_delta=%lu health_fail_delta=%lu "
      "transport_ok_delta=%lu transport_fail_delta=%lu\n",
      static_cast<unsigned long>(successDelta),
      static_cast<unsigned long>(failDelta),
      static_cast<unsigned long>(transportSuccessDelta),
      static_cast<unsigned long>(transportFailDelta));
  Serial.printf(
      "i2c_soak: protocol_fail_delta=%lu not_ready_delta=%lu state=%s "
      "consec=%u owner_api=pollJob milli=1\n",
      static_cast<unsigned long>(protocolFailDelta),
      static_cast<unsigned long>(notReadyDelta),
      stateToStr(deviceInstance.state()),
      static_cast<unsigned>(deviceInstance.consecutiveFailures()));
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

  if (rejectActiveOwnerJob("stress_mix")) {
    return;
  }
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
        st = performNoStretchMeasurementBlocking(m);
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
    yield();
  }

  const uint32_t elapsed = millis() - startMs;
  HealthSnapshot<SHT3x::SHT3x> healthAfter;
  healthAfter.capture(deviceInstance);

  (void)deviceInstance.setClockStretching(SHT3x::ClockStretching::STRETCH_DISABLED);
  Serial.printf("stress_mix: ok=%lu fail=%lu duration_ms=%lu\n",
                static_cast<unsigned long>(okTotal),
                static_cast<unsigned long>(failTotal),
                static_cast<unsigned long>(elapsed));
  if (!verboseMode) {
    return;
  }

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
  if (rejectActiveOwnerJob("selftest")) {
    Serial.println("Selftest result: pass=0 fail=1 skip=0");
    return;
  }

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

SHT3x::Status scheduleMeasurement(bool manual = false) {
  const uint32_t startMs = millis();
  const uint32_t requestId = allocateRequestId();
  SHT3x::JobRequest request;
  request.requestId = requestId;
  request.deadlineMs =
      startMs + (manual ? MANUAL_JOB_TIMEOUT_MS : MEASUREMENT_JOB_TIMEOUT_MS);
  request.hasDeadline = true;
  SHT3x::Status st = deviceInstance.requestMeasurement(request);
  if (st.code == SHT3x::Err::IN_PROGRESS) {
    ownerJobActive = true;
    manualJobControl = manual;
    pendingJobType = SHT3x::JobType::MEASUREMENT;
    pendingStartMs = startMs;
    pendingRequestId = requestId;
    Serial.printf("request: IN_PROGRESS request=%lu deadline_ms=%lu scheduled_ms=%lu\n",
                  static_cast<unsigned long>(pendingRequestId),
                  static_cast<unsigned long>(request.deadlineMs),
                  static_cast<unsigned long>(pendingStartMs));
  }
  return st;
}

SHT3x::Status scheduleEnsureIdle(const char* terminalLabel,
                                 bool manual = false) {
  if (ownerJobActive) {
    return SHT3x::Status::Error(SHT3x::Err::BUSY, "CLI owner job already active");
  }
  const uint32_t startMs = millis();
  const uint32_t requestId = allocateRequestId();
  SHT3x::JobRequest request;
  request.requestId = requestId;
  request.deadlineMs = startMs + MANUAL_JOB_TIMEOUT_MS;
  request.hasDeadline = true;
  const SHT3x::Status st = deviceInstance.requestEnsureIdle(request);
  if (st.code == SHT3x::Err::IN_PROGRESS) {
    ownerJobActive = true;
    manualJobControl = manual;
    pendingJobType = SHT3x::JobType::ENSURE_IDLE;
    pendingTerminalLabel = terminalLabel != nullptr ? terminalLabel : "ensure_idle";
    pendingStartMs = startMs;
    pendingRequestId = requestId;
    Serial.printf("ensure_idle: IN_PROGRESS request=%lu deadline_ms=%lu scheduled_ms=%lu\n",
                  static_cast<unsigned long>(pendingRequestId),
                  static_cast<unsigned long>(request.deadlineMs),
                  static_cast<unsigned long>(pendingStartMs));
  }
  return st;
}

void handleMeasurementTerminal(const SHT3x::PollJobResult& result) {
  if (!ownerJobActive || !result.terminal) {
    return;
  }

  SHT3x::MeasurementMilli milli;
  const SHT3x::Status st =
      readTerminalMeasurementMilli(result, pendingRequestId, milli);
  rememberJobResult(result.status, result);
  clearPendingOwner();

  if (!st.ok()) {
    printStatus(st);
    printJobResult("job terminal", result.status, result);
    return;
  }

  SHT3x::Measurement m;
  m.temperatureC = static_cast<float>(milli.temperatureMilliCelsius) / 1000.0f;
  m.humidityPct = static_cast<float>(milli.humidityMilliPercent) / 1000.0f;

  printMeasurement(m);
  printJobResult("job terminal", result.status, result);
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
  if (str[0] == '\0' || str[0] == '-' || str[0] == '+') {
    return false;
  }
  char* end = nullptr;
  errno = 0;
  unsigned long value = std::strtoul(str, &end, 0);
  if (errno == ERANGE || end == str || *end != '\0') {
    return false;
  }
  if (value > 0xFFFFUL) {
    return false;
  }
  out = static_cast<uint16_t>(value);
  return true;
}

bool parseU32(const CliString& token, uint32_t& out) {
  const char* str = token.c_str();
  if (str[0] == '\0' || str[0] == '-' || str[0] == '+') {
    return false;
  }
  char* end = nullptr;
  errno = 0;
  const unsigned long value = std::strtoul(str, &end, 0);
  if (errno == ERANGE || end == str || *end != '\0' ||
      value > static_cast<unsigned long>(std::numeric_limits<uint32_t>::max())) {
    return false;
  }
  out = static_cast<uint32_t>(value);
  return true;
}

bool parseFiniteFloat(const CliString& token, float& out) {
  const char* str = token.c_str();
  if (str[0] == '\0') {
    return false;
  }
  char* end = nullptr;
  errno = 0;
  const float value = std::strtof(str, &end);
  if (errno == ERANGE || end == str || *end != '\0' || !std::isfinite(value)) {
    return false;
  }
  out = value;
  return true;
}

void printAlertLimit(SHT3x::AlertLimitKind kind) {
  SHT3x::AlertLimit limit;
  const SHT3x::Status st = deviceInstance.readAlertLimit(kind, limit);
  printLabeledStatus("alert read", st);
  if (st.ok()) {
    Serial.printf("alert %s: raw=0x%04X T=%.2fC RH=%.2f%%\n",
                  alertKindToStr(kind),
                  static_cast<unsigned>(limit.raw),
                  static_cast<double>(limit.temperatureC),
                  static_cast<double>(limit.humidityPct));
  }
}

void printAllAlertLimits() {
  static constexpr SHT3x::AlertLimitKind ALERT_KINDS[] = {
      SHT3x::AlertLimitKind::HIGH_SET,
      SHT3x::AlertLimitKind::HIGH_CLEAR,
      SHT3x::AlertLimitKind::LOW_CLEAR,
      SHT3x::AlertLimitKind::LOW_SET,
  };
  for (size_t i = 0; i < (sizeof(ALERT_KINDS) / sizeof(ALERT_KINDS[0])); ++i) {
    printAlertLimit(ALERT_KINDS[i]);
  }
}

void startPeriodicFromArgs(const CliString& args, const char* label) {
  const int split = args.indexOf(' ');
  if (split < 0) {
    logWarn("Usage: %s <rate> <rep>", label);
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

  const SHT3x::Status st = deviceInstance.startPeriodic(rate, rep);
  printLabeledStatus(label, st);
}

void requestMeasurementCommand(const char* label) {
  if (rejectActiveOwnerJob(label)) {
    return;
  }
  const SHT3x::Status st = scheduleMeasurement();
  printLabeledStatus(label, st);
}

void singleShotCommand(const CliString& arg) {
  SHT3x::Repeatability rep;
  if (!parseRepeatability(arg, rep)) {
    logWarn("Usage: single <low|medium|high>");
    return;
  }

  if (rejectActiveOwnerJob("single")) {
    return;
  }
  SHT3x::Status st = deviceInstance.setMode(SHT3x::Mode::SINGLE_SHOT);
  printLabeledStatus("single mode", st);
  if (!st.ok()) {
    return;
  }
  st = deviceInstance.setRepeatability(rep);
  printLabeledStatus("single repeat", st);
  if (!st.ok()) {
    return;
  }
  st = deviceInstance.setClockStretching(SHT3x::ClockStretching::STRETCH_DISABLED);
  printLabeledStatus("single stretch", st);
  if (!st.ok()) {
    return;
  }
  st = scheduleMeasurement();
  printLabeledStatus("single request", st);
}

void pollJobCommand(uint8_t budget, const char* label) {
  if (!ownerJobActive || pendingRequestId == 0U) {
    Serial.printf("%s: none\n", label);
    return;
  }
  SHT3x::PollJobResult result;
  const SHT3x::Status st = deviceInstance.pollJob(millis(), budget, result);
  const SHT3x::Status validation = validateOwnedResult(result, budget, st);
  if (!validation.ok()) {
    printLabeledStatus(
        "job_validation", validation);
    quarantinePendingOwner();
    return;
  }
  if (result.terminal) {
    if (result.type == SHT3x::JobType::MEASUREMENT) {
      handleMeasurementTerminal(result);
    } else {
      const char* terminalLabel = pendingTerminalLabel;
      rememberJobResult(st, result);
      clearPendingOwner();
      printJobResult(label, st, result);
      printLabeledStatus(terminalLabel, result.status);
    }
    return;
  }
  printJobResult(label, st, result);
}

TransferStats transferStatsSnapshot() {
  return platform.getTransferStats != nullptr
             ? platform.getTransferStats(platform.user)
             : TransferStats{};
}

void printTransferStats() {
  if (platform.getTransferStats == nullptr) {
    logWarn("Transfer counters are not available");
    return;
  }
  const TransferStats stats = transferStatsSnapshot();
  const uint32_t total =
      (std::numeric_limits<uint32_t>::max() - stats.readCallbacks < stats.writeCallbacks)
          ? std::numeric_limits<uint32_t>::max()
          : stats.readCallbacks + stats.writeCallbacks;
  Serial.printf("xfer_stats: read=%lu write=%lu total=%lu ok=%lu fail=%lu tx_bytes=%lu rx_bytes=%lu\n",
                static_cast<unsigned long>(stats.readCallbacks),
                static_cast<unsigned long>(stats.writeCallbacks),
                static_cast<unsigned long>(total),
                static_cast<unsigned long>(stats.successes),
                static_cast<unsigned long>(stats.failures),
                static_cast<unsigned long>(stats.txBytes),
                static_cast<unsigned long>(stats.rxBytes));
}

void assertTransferStats(uint32_t expectedRead, uint32_t expectedWrite,
                         uint32_t expectedTotal) {
  if (platform.getTransferStats == nullptr) {
    logWarn("Transfer counters are not available");
    return;
  }
  const TransferStats stats = transferStatsSnapshot();
  const uint32_t total =
      (std::numeric_limits<uint32_t>::max() - stats.readCallbacks < stats.writeCallbacks)
          ? std::numeric_limits<uint32_t>::max()
          : stats.readCallbacks + stats.writeCallbacks;
  const bool passed = stats.readCallbacks == expectedRead &&
                      stats.writeCallbacks == expectedWrite && total == expectedTotal;
  Serial.printf("xfer_assert: %s expected_read=%lu actual_read=%lu expected_write=%lu actual_write=%lu expected_total=%lu actual_total=%lu\n",
                passed ? "PASS" : "FAIL",
                static_cast<unsigned long>(expectedRead),
                static_cast<unsigned long>(stats.readCallbacks),
                static_cast<unsigned long>(expectedWrite),
                static_cast<unsigned long>(stats.writeCallbacks),
                static_cast<unsigned long>(expectedTotal),
                static_cast<unsigned long>(total));
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

  ParsedArgs parsed;
  parseArguments(cmd, parsed);
  const bool gresetControl = cmd == "greset arm" || cmd == "greset disarm" ||
                             cmd == "greset confirm";
  if (generalCallArmed && !gresetControl) {
    generalCallArmed = false;
    Serial.println("greset armed=0 zero_i2c=1 reason=intervening_command");
  }
  if (parsed.tooMany) {
    logWarn("Too many arguments (maximum %u)", static_cast<unsigned>(MAX_CLI_ARGS));
    return;
  }
  bool knownCommand = false;
  if (!validCommandArity(parsed, knownCommand)) {
    logWarn("Invalid command arity: %s", cmd.c_str());
    logWarn("Use 'help' for the exact command synopsis");
    return;
  }
  const char* effect = confirmationEffect(parsed);
  if (!requireConfirmation(cmd, parsed, effect)) {
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

  if (cmd == "request") {
    const SHT3x::Status st = scheduleMeasurement(true);
    printLabeledStatus("request", st);
    return;
  }

  if (cmd == "fetch") {
    pollJobCommand(1U, "fetch");
    return;
  }

  if (cmd == "job" || cmd == "job current") {
    pollJobCommand(0U, "job current");
    return;
  }

  if (cmd == "job last" || cmd == "result") {
    printLastJobResult();
    return;
  }

  if (cmd.startsWith("job step ")) {
    uint32_t budget = 0U;
    if (!parseU32(cmd.substring(9), budget) || budget > 255U) {
      logWarn("Usage: job step <0..255>");
      return;
    }
    pollJobCommand(static_cast<uint8_t>(budget), "job step");
    return;
  }

  if (cmd == "job cancel" || cmd == "cancel") {
    if (!ownerJobActive || pendingRequestId == 0U) {
      Serial.println("job cancel: none");
      return;
    }
    const SHT3x::Status st = cancelPending();
    printLabeledStatus("job cancel", st);
    if (lastJobValid) {
      printLastJobResult();
    }
    return;
  }

  if (cmd == "xfer_reset") {
    if (platform.resetTransferStats == nullptr) {
      logWarn("Transfer counter reset is not available");
    } else {
      platform.resetTransferStats(platform.user);
      Serial.println("xfer_reset: OK");
    }
    return;
  }

  if (cmd == "xfer_stats") {
    printTransferStats();
    return;
  }

  if (cmd.startsWith("xfer_assert ")) {
    uint32_t expectedRead = 0U;
    uint32_t expectedWrite = 0U;
    uint32_t expectedTotal = 0U;
    if (!parseU32(CliString(parsed.values[1]), expectedRead) ||
        !parseU32(CliString(parsed.values[2]), expectedWrite) ||
        !parseU32(CliString(parsed.values[3]), expectedTotal)) {
      logWarn("Usage: xfer_assert <read> <write> <total>");
      return;
    }
    assertTransferStats(expectedRead, expectedWrite, expectedTotal);
    return;
  }

  if (cmd == "read") {
    if (rejectActiveOwnerJob("read")) {
      return;
    }
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

  if (cmd == "cfg") {
    printConfig(false);
    return;
  }

  if (cmd == "settings") {
    printConfig(true);
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

    if (rejectActiveOwnerJob("mode")) {
      return;
    }
    SHT3x::Status st = deviceInstance.setMode(mode);
    printStatus(st);
    return;
  }

  if (cmd.startsWith("single ")) {
    CliString arg = cmd.substring(7);
    arg.trim();
    singleShotCommand(arg);
    return;
  }

  if (cmd.startsWith("periodic ")) {
    CliString args = cmd.substring(9);
    args.trim();
    const int split = args.indexOf(' ');
    CliString sub = args;
    CliString rest;
    if (split >= 0) {
      sub = args.substring(0, split);
      rest = args.substring(split + 1);
      rest.trim();
    }
    if (sub == "start") {
      startPeriodicFromArgs(rest, "periodic start");
      return;
    }
    if (sub == "fetch") {
      requestMeasurementCommand("periodic fetch");
      return;
    }
    if (sub == "stop") {
      const SHT3x::Status st = deviceInstance.stopPeriodic();
      printLabeledStatus("periodic stop", st);
      return;
    }
    logWarn("Usage: periodic start <rate> <rep> | periodic fetch | periodic stop");
    return;
  }

  if (cmd.startsWith("art ")) {
    CliString sub = cmd.substring(4);
    sub.trim();
    if (sub == "start") {
      const SHT3x::Status st = deviceInstance.startArt();
      printLabeledStatus("art start", st);
      return;
    }
    if (sub == "fetch") {
      requestMeasurementCommand("art fetch");
      return;
    }
    if (sub == "stop") {
      const SHT3x::Status st = deviceInstance.stopPeriodic();
      printLabeledStatus("art stop", st);
      return;
    }
    logWarn("Usage: art start | art fetch | art stop");
    return;
  }

  if (cmd.startsWith("start_periodic ")) {
    CliString args = cmd.substring(15);
    args.trim();
    startPeriodicFromArgs(args, "start_periodic");
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
    SHT3x::Status st = deviceInstance.getSettings(snap);
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

    printStatusRegisterLine("status", stReg);
    return;
  }

  if (cmd == "status_restore") {
    SHT3x::StatusReadSnapshot snap;
    const SHT3x::Status st = deviceInstance.readStatusWithModeRestore(snap);
    printStatusRestoreSnapshot(st, snap);
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

  if (cmd == "clearstatus" || cmd == "clear_status") {
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

  if (cmd == "serial" || cmd.startsWith("serial ")) {
    SHT3x::ClockStretching stretch = SHT3x::ClockStretching::STRETCH_DISABLED;
    if (cmd.length() > 6U) {
      CliString arg = cmd.substring(6);
      arg.trim();
      if (arg == "stretch") {
        stretch = SHT3x::ClockStretching::STRETCH_ENABLED;
      } else if (arg == "nostretch") {
        stretch = SHT3x::ClockStretching::STRETCH_DISABLED;
      } else {
        logWarn("Usage: serial [stretch|nostretch]");
        return;
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

    if (sub == "show") {
      printAllAlertLimits();
      return;
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

    if (sub == "write" || sub == "set") {
      const int split2 = rest.indexOf(' ');
      if (split2 < 0) {
        logWarn("Usage: alert set <kind> <T> <RH>");
        return;
      }
      const CliString kindStr = rest.substring(0, split2);
      CliString rest2 = rest.substring(split2 + 1);
      rest2.trim();
      const int split3 = rest2.indexOf(' ');
      if (split3 < 0) {
        logWarn("Usage: alert set <kind> <T> <RH>");
        return;
      }
      const CliString tempStr = rest2.substring(0, split3);
      const CliString rhStr = rest2.substring(split3 + 1);

      SHT3x::AlertLimitKind kind;
      if (!parseAlertKind(kindStr, kind)) {
        logWarn("Invalid alert kind");
        return;
      }

      float tempC = 0.0f;
      float rh = 0.0f;
      if (!parseFiniteFloat(tempStr, tempC) || !parseFiniteFloat(rhStr, rh)) {
        logWarn("Temperature and humidity must be finite whole-token numbers");
        return;
      }
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
      float tempC = 0.0f;
      float rh = 0.0f;
      if (!parseFiniteFloat(rest.substring(0, split2), tempC) ||
          !parseFiniteFloat(rest.substring(split2 + 1), rh)) {
        logWarn("Temperature and humidity must be finite whole-token numbers");
        return;
      }
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

    logWarn("Usage: alert show|read|set|write|raw|encode|decode|disable ...");
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
    if (rejectActiveOwnerJob("reset")) {
      return;
    }
    SHT3x::Status st = deviceInstance.softReset();
    printStatus(st);
    return;
  }

  if (cmd == "defaults") {
    if (rejectActiveOwnerJob("defaults")) {
      return;
    }
    SHT3x::Status st = deviceInstance.resetToDefaults();
    printStatus(st);
    return;
  }

  if (cmd == "restore") {
    if (rejectActiveOwnerJob("restore")) {
      return;
    }
    SHT3x::Status st = deviceInstance.resetAndRestore();
    printStatus(st);
    return;
  }

  if (cmd == "iface_reset") {
    SHT3x::Status st = deviceInstance.interfaceReset();
    printStatus(st);
    return;
  }

  if (cmd == "greset arm") {
    generalCallArmed = true;
    logWarn("General-call reset armed for one confirmed broadcast command");
    Serial.println("greset armed=1 zero_i2c=1");
    return;
  }

  if (cmd == "greset disarm") {
    generalCallArmed = false;
    logInfo("General-call reset disarmed");
    Serial.println("greset armed=0 zero_i2c=1");
    return;
  }

  if (cmd == "greset") {
    logWarn("General-call reset is bus-wide and may affect every compatible device");
    logWarn("Use 'greset arm', then 'greset confirm'");
    return;
  }

  if (cmd == "greset confirm") {
    if (!generalCallArmed) {
      logWarn("General-call reset is not armed; use 'greset arm' first");
      return;
    }
    generalCallArmed = false;
    Serial.println("greset armed=0 zero_i2c=1");
    SHT3x::Status st = deviceInstance.generalCallReset();
    printStatus(st);
    logInfo("General-call reset disarmed");
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
    printLabeledStatus("begin", beginOwnerSafe());
    return;
  }

  if (cmd == "end") {
    if (ownerJobActive) {
      printLabeledStatus("end", SHT3x::Status::Error(
                                    SHT3x::Err::BUSY,
                                    "Active owner job must be cancelled explicitly"));
      return;
    }
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
    const SHT3x::Status st = scheduleEnsureIdle("recover", false);
    printLabeledStatus("recover", st);
    return;
  }

  if (cmd == "verbose") {
    printVerboseState();
    return;
  }

  if (cmd.startsWith("verbose ")) {
    uint32_t val = 0U;
    if (!parseU32(cmd.substring(8), val) || val > 1U) {
      logWarn("Usage: verbose [0|1]");
      return;
    }
    verboseMode = val != 0U;
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

  if (cmd.startsWith("i2c_soak ")) {
    uint32_t seconds = 0U;
    if (!parseU32(cmd.substring(9), seconds) || seconds == 0U ||
        seconds > I2C_SOAK_MAX_SECONDS) {
      logWarn("Invalid i2c_soak seconds");
      return;
    }
    runI2cSoak(seconds);
    return;
  }

  if (cmd == "stress_mix") {
    runStressMix(50);
    return;
  }

  if (cmd.startsWith("stress_mix ")) {
    uint32_t count = 0U;
    if (!parseU32(cmd.substring(11), count) || count == 0U || count > 100000U) {
      logWarn("Invalid stress_mix count");
      return;
    }
    runStressMix(static_cast<int>(count));
    return;
  }

  if (cmd == "stress" || cmd.startsWith("stress ")) {
    uint32_t count = 10U;
    if (cmd.length() > 6U &&
        (!parseU32(cmd.substring(6), count) || count == 0U || count > 100000U)) {
      logWarn("Invalid stress count");
      return;
    }

    runStress(static_cast<int>(count));
    return;
  }

  logWarn("Unknown command: %s", cmd.c_str());
}

}  // namespace

SHT3x::Config& config() {
  return configInstance;
}

bool& configReady() {
  return configIsReady;
}

SHT3x::Status beginOwnerSafe() {
  if (!configIsReady) {
    return SHT3x::Status::Error(SHT3x::Err::INVALID_CONFIG,
                                "CLI config is not ready");
  }
  if (ownerJobActive) {
    return SHT3x::Status::Error(SHT3x::Err::BUSY,
                                "Active owner job must be cancelled explicitly");
  }
  const SHT3x::Status bindStatus = deviceInstance.bind(configInstance);
  return bindStatus.ok() ? scheduleEnsureIdle("begin", false) : bindStatus;
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
  cli::printHelpItem("version / ver", "Print runtime framework and firmware/library provenance");
  cli::printHelpItem("scan", "Scan I2C ACKs; an ACK is not SHT3x identity");
  cli::printHelpItem("read", "Run one bounded owner-safe measurement job");
  cli::printHelpItem("request", "Schedule an owner-safe measurement without I2C");
  cli::printHelpItem("fetch", "Advance the active owner job by at most one I2C transfer");
  cli::printHelpItem("raw", "Print the last cached raw sample");
  cli::printHelpItem("comp", "Print the last cached compensated sample");
  cli::printHelpItem("meastime", "Show estimated measurement time");
  cli::printHelpItem("job [current|last]", "Show active zero-I2C progress or the retained terminal result");
  cli::printHelpItem("job step <0..255>", "Poll once with an explicit I2C-transfer budget");
  cli::printHelpItem("job cancel / cancel", "Cancel the active job locally with zero I2C");
  cli::printHelpItem("result", "Show the retained terminal job result");

  cli::printHelpSection("Operating Mode");
  cli::printHelpItem("mode [single|periodic|art]", "Set or show operating mode");
  cli::printHelpItem("single <low|medium|high>", "Run one no-stretch single-shot measurement");
  cli::printHelpItem("periodic start <rate> <rep>", "Start periodic mode");
  cli::printHelpItem("periodic fetch", "Run one bounded periodic fetch job");
  cli::printHelpItem("periodic stop", "Stop periodic or ART mode");
  cli::printHelpItem("art start", "Start ART mode");
  cli::printHelpItem("art fetch", "Run one bounded ART fetch job");
  cli::printHelpItem("art stop", "Stop ART mode");
  cli::printHelpItem("start_periodic <rate> <rep>", "Alias for periodic start");
  cli::printHelpItem("start_art", "Alias for art start");
  cli::printHelpItem("stop_periodic", "Alias for periodic/art stop");
  cli::printHelpItem("repeat [low|med|high]", "Set or show repeatability");
  cli::printHelpItem("rate [0.5|1|2|4|10]", "Set or show periodic rate");
  cli::printHelpItem("stretch [0|1]", "Set or show clock stretching");

  cli::printHelpSection("Status And Alerts");
  cli::printHelpItem("status", "Read decoded status while idle");
  cli::printHelpItem("status_restore confirm", "Interrupt active acquisition, read status, and restore mode");
  cli::printHelpItem("status_raw", "Read raw status while idle");
  cli::printHelpItem("clearstatus confirm", "Clear sticky status flags");
  cli::printHelpItem("clear_status confirm", "Alias for clearstatus");
  cli::printHelpItem("heater [status|off|on confirm]", "Inspect or control the heater; enabling requires confirmation");
  cli::printHelpItem("serial [stretch|nostretch]", "Read the CRC-protected serial number");
  cli::printHelpItem("command write <hex> confirm", "Issue an arbitrary raw 16-bit command");
  cli::printHelpItem("command write_data <cmd> <data> confirm", "Issue an arbitrary command with packed data");
  cli::printHelpItem("command read <cmd> <len> confirm", "Issue an arbitrary command and raw read");
  cli::printHelpItem("alert show", "Read all alert limits");
  cli::printHelpItem("alert set <kind> <T> <RH> confirm", "Write an alert limit");
  cli::printHelpItem("alert read <hs|hc|lc|ls>", "Read an alert limit");
  cli::printHelpItem("alert write <kind> <T> <RH> confirm", "Alias for alert set");
  cli::printHelpItem("alert raw read <kind>", "Read a raw packed alert word");
  cli::printHelpItem("alert raw write <kind> <hex> confirm", "Write a raw packed alert word");
  cli::printHelpItem("alert encode <T> <RH>", "Encode an alert word without I2C");
  cli::printHelpItem("alert decode <hex>", "Decode an alert word without I2C");
  cli::printHelpItem("alert disable confirm", "Disable alerts by writing limits");
  cli::printHelpItem("convert <rawT> <rawRH>", "Convert measurement words without I2C");

  cli::printHelpSection("Lifecycle And Diagnostics");
  cli::printHelpItem("reset confirm", "Soft-reset the sensor");
  cli::printHelpItem("defaults confirm", "Reset command-mode defaults");
  cli::printHelpItem("restore confirm", "Reset the sensor and restore cached settings");
  cli::printHelpItem("iface_reset confirm", "Run the injected interface-reset callback");
  cli::printHelpItem("greset arm / greset disarm", "Arm or disarm one bus-wide general-call reset; no I2C");
  cli::printHelpItem("greset confirm", "Attempt one armed bus-wide reset when application transport enables it");
  cli::printHelpItem("stats", "Show runtime counters and cached settings");
  cli::printHelpItem("cfg / settings", "Show current config; settings also reads status");
  cli::printHelpItem("drv", "Show driver state and health");
  cli::printHelpItem("online", "Show online state");
  cli::printHelpItem("begin", "Owner-safe bind and bounded ensure-idle reconciliation");
  cli::printHelpItem("end", "End the local driver session; rejects an active job");
  cli::printHelpItem("state", "Show compact one-line health summary");
  cli::printHelpItem("probe", "Probe the sensor without health tracking");
  cli::printHelpItem("recover confirm", "Run bounded owner-safe ensure-idle recovery");
  cli::printHelpItem("verbose [0|1]", "Show or set verbose output");
  cli::printHelpItem("stress [N]", "Run N bounded measurement jobs");
  cli::printHelpItem("stress_mix [N]", "Run N mixed operations");
  cli::printHelpItem("i2c_soak <seconds>", "Run a bounded low-output measurement soak");
  cli::printHelpItem("xfer_reset", "Reset example-owned transport counters");
  cli::printHelpItem("xfer_stats", "Show example-owned transport counters");
  cli::printHelpItem("xfer_assert <read> <write> <total>", "Assert exact transport callback totals");
  cli::printHelpItem("selftest confirm", "Run diagnostic I2C self-test commands");
  Serial.println("\nSafety: run a guarded command without 'confirm' to preview its exact confirmed form.");
}

void printVersionInfo() {
  const char* date = platform.buildDate != nullptr ? platform.buildDate : __DATE__;
  const char* time = platform.buildTime != nullptr ? platform.buildTime : __TIME__;
  Serial.printf("framework=%s target=%s arduino_core=%s idf_version=%s\n",
                platform.framework ? platform.framework : "unknown",
                platform.buildTarget ? platform.buildTarget : "unknown",
                platform.arduinoCoreVersion ? platform.arduinoCoreVersion : "unknown",
                platform.espIdfVersion ? platform.espIdfVersion : "unknown");
  Serial.println("=== Version Info ===");
  Serial.printf("  Framework: %s\n", platform.framework ? platform.framework : "unknown");
  Serial.printf("  Arduino-ESP32: %s\n",
                platform.arduinoCoreVersion ? platform.arduinoCoreVersion : "unknown");
  Serial.printf("  ESP-IDF: %s\n",
                platform.espIdfVersion ? platform.espIdfVersion : "unknown");
  Serial.printf("  Build target: %s\n",
                platform.buildTarget ? platform.buildTarget : "unknown");
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
  if (ownerJobActive && !manualJobControl) {
    SHT3x::PollJobResult result;
    const SHT3x::Status st = deviceInstance.pollJob(millis(), 1, result);
    const SHT3x::Status validation = validateOwnedResult(result, 1U, st);
    if (!validation.ok()) {
      printLabeledStatus("job_validation", validation);
      quarantinePendingOwner();
    } else if (result.terminal) {
      if (result.type == SHT3x::JobType::MEASUREMENT) {
        handleMeasurementTerminal(result);
      } else {
        const char* terminalLabel = pendingTerminalLabel;
        rememberJobResult(st, result);
        clearPendingOwner();
        printJobResult("job terminal", st, result);
        printLabeledStatus(terminalLabel, result.status);
      }
    } else if (st.code != SHT3x::Err::IN_PROGRESS) {
      clearPendingOwner();
      printStatus(st);
    }
  }

}

static SHT3x::Status cancelPending() {
  if (!ownerJobActive || pendingRequestId == 0U) {
    return SHT3x::Status::Ok();
  }

  SHT3x::PollJobResult current;
  SHT3x::Status st = deviceInstance.pollJob(millis(), 0, current);
  const SHT3x::Status currentValidation = validateOwnedResult(current, 0U, st);
  if (!currentValidation.ok()) {
    quarantinePendingOwner();
    return currentValidation;
  }
  if (current.terminal) {
    if (current.type == SHT3x::JobType::MEASUREMENT) {
      handleMeasurementTerminal(current);
    } else {
      rememberJobResult(st, current);
      clearPendingOwner();
    }
    return st;
  }
  SHT3x::PollJobResult cancelled;
  st = deviceInstance.cancelJob(SHT3x::CancelReason::REQUESTED, cancelled);
  const SHT3x::Status cancelValidation = validateOwnedResult(cancelled, 0U, st);
  if (!cancelValidation.ok() ||
      cancelled.outcome != SHT3x::JobOutcome::CANCELLED) {
    quarantinePendingOwner();
    return cancelValidation.ok()
               ? SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                      "Unexpected cancellation outcome")
               : cancelValidation;
  }
  rememberJobResult(st, cancelled);
  clearPendingOwner();
  return st.code == SHT3x::Err::CANCELLED ? SHT3x::Status::Ok() : st;
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
