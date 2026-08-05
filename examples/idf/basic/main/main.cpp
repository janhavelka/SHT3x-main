#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_idf_version.h>
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdkconfig.h>

// Diagnostic bring-up example only. It owns one I2C bus/device handle and
// calls the driver from app_main; production multi-task/shared-bus use must
// serialize driver access externally and provide any general-call device handle.
#include "IdfI2cTransport.h"
#include "SHT3x/SHT3x.h"

namespace {

constexpr const char* TAG = "sht3x_cli";
constexpr i2c_port_num_t I2C_PORT = I2C_NUM_0;
constexpr gpio_num_t I2C_SDA = GPIO_NUM_8;
constexpr gpio_num_t I2C_SCL = GPIO_NUM_9;
constexpr uint32_t I2C_FREQ_HZ = 400000;
constexpr uint8_t SHT3X_ADDR = 0x44;
constexpr size_t LINE_LEN = 128U;
constexpr int CLI_QUEUE_DEPTH = 4;
constexpr uint32_t CLI_TICK_MS = 5;
constexpr uint32_t CLI_QUEUE_SEND_TIMEOUT_MS = 50;
constexpr int PROBE_TIMEOUT_MS = 50;
constexpr uint32_t STRESS_MAX_COUNT = 100000;
constexpr uint32_t I2C_SOAK_MAX_SECONDS = 24U * 60U * 60U;
constexpr uint32_t OWNER_JOB_TIMEOUT_MS = 5000;
constexpr char CONFIRM_TOKEN[] = "confirm";

struct AppContext {
  IdfI2cContext i2c = {};
  QueueHandle_t lineQueue = nullptr;
};

struct CliLine {
  char text[LINE_LEN] = {};
};

struct OwnerState {
  bool active = false;
  uint32_t requestId = 0;
  uint32_t deadlineMs = 0;
  SHT3x::JobType type = SHT3x::JobType::NONE;
  bool lastValid = false;
  SHT3x::PollJobResult last = {};
};

AppContext gApp;
SHT3x::SHT3x gDevice;
SHT3x::Config gConfig;
bool gVerbose = false;
uint32_t gNextRequestId = 1;
OwnerState gOwner;
bool gGeneralCallResetArmed = false;

uint32_t nowMs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}

uint32_t nowUs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time());
}

void cooperativeYield(void*) {
  taskYIELD();
}

void printStatus(const char* label, const SHT3x::Status& st) {
  std::printf("%s: %s code=%u detail=%ld msg=%s\n",
              label,
              st.ok() ? "OK" : (st.inProgress() ? "IN_PROGRESS" : "ERR"),
              static_cast<unsigned>(st.code),
              static_cast<long>(st.detail),
              st.msg ? st.msg : "");
}

const char* stateToStr(SHT3x::DriverState state) {
  switch (state) {
    case SHT3x::DriverState::UNINIT: return "UNINIT";
    case SHT3x::DriverState::READY: return "READY";
    case SHT3x::DriverState::DEGRADED: return "DEGRADED";
    case SHT3x::DriverState::OFFLINE: return "OFFLINE";
    default: return "UNKNOWN";
  }
}

const char* modeToStr(SHT3x::Mode mode) {
  switch (mode) {
    case SHT3x::Mode::SINGLE_SHOT: return "single";
    case SHT3x::Mode::PERIODIC: return "periodic";
    case SHT3x::Mode::ART: return "art";
    default: return "?";
  }
}

const char* repeatabilityToStr(SHT3x::Repeatability rep) {
  switch (rep) {
    case SHT3x::Repeatability::LOW_REPEATABILITY: return "low";
    case SHT3x::Repeatability::MEDIUM_REPEATABILITY: return "med";
    case SHT3x::Repeatability::HIGH_REPEATABILITY: return "high";
    default: return "?";
  }
}

const char* rateToStr(SHT3x::PeriodicRate rate) {
  switch (rate) {
    case SHT3x::PeriodicRate::MPS_0_5: return "0.5";
    case SHT3x::PeriodicRate::MPS_1: return "1";
    case SHT3x::PeriodicRate::MPS_2: return "2";
    case SHT3x::PeriodicRate::MPS_4: return "4";
    case SHT3x::PeriodicRate::MPS_10: return "10";
    default: return "?";
  }
}

const char* jobTypeToStr(SHT3x::JobType type) {
  switch (type) {
    case SHT3x::JobType::NONE: return "none";
    case SHT3x::JobType::MEASUREMENT: return "measurement";
    case SHT3x::JobType::ENSURE_IDLE: return "ensure_idle";
    default: return "unknown";
  }
}

const char* jobPhaseToStr(SHT3x::JobPhase phase) {
  switch (phase) {
    case SHT3x::JobPhase::IDLE: return "idle";
    case SHT3x::JobPhase::SINGLE_SHOT_COMMAND: return "single_command";
    case SHT3x::JobPhase::SINGLE_SHOT_CONVERSION: return "single_wait";
    case SHT3x::JobPhase::SINGLE_SHOT_READ: return "single_read";
    case SHT3x::JobPhase::PERIODIC_FETCH_COMMAND: return "periodic_fetch";
    case SHT3x::JobPhase::PERIODIC_READ: return "periodic_read";
    case SHT3x::JobPhase::ENSURE_BREAK_COMMAND: return "ensure_break";
    case SHT3x::JobPhase::ENSURE_BREAK_WAIT: return "ensure_break_wait";
    case SHT3x::JobPhase::ENSURE_RESET_COMMAND: return "ensure_reset";
    case SHT3x::JobPhase::ENSURE_RESET_WAIT: return "ensure_reset_wait";
    case SHT3x::JobPhase::ENSURE_STATUS_COMMAND: return "ensure_status_command";
    case SHT3x::JobPhase::ENSURE_STATUS_READ: return "ensure_status_read";
    default: return "unknown";
  }
}

const char* jobOutcomeToStr(SHT3x::JobOutcome outcome) {
  switch (outcome) {
    case SHT3x::JobOutcome::NONE: return "none";
    case SHT3x::JobOutcome::ACTIVE: return "active";
    case SHT3x::JobOutcome::SUCCEEDED: return "succeeded";
    case SHT3x::JobOutcome::FAILED: return "failed";
    case SHT3x::JobOutcome::CANCELLED: return "cancelled";
    case SHT3x::JobOutcome::TIMED_OUT: return "timed_out";
    default: return "unknown";
  }
}

const char* jobEffectToStr(SHT3x::JobEffect effect) {
  switch (effect) {
    case SHT3x::JobEffect::NONE: return "none";
    case SHT3x::JobEffect::RESULT_MAY_BE_PENDING: return "result_may_be_pending";
    case SHT3x::JobEffect::DEVICE_STATE_CHANGED: return "device_state_changed";
    case SHT3x::JobEffect::DEVICE_STATE_INDETERMINATE: return "device_state_indeterminate";
    default: return "unknown";
  }
}

const char* alertKindToStr(SHT3x::AlertLimitKind kind) {
  switch (kind) {
    case SHT3x::AlertLimitKind::HIGH_SET: return "HIGH_SET";
    case SHT3x::AlertLimitKind::HIGH_CLEAR: return "HIGH_CLEAR";
    case SHT3x::AlertLimitKind::LOW_CLEAR: return "LOW_CLEAR";
    case SHT3x::AlertLimitKind::LOW_SET: return "LOW_SET";
    default: return "UNKNOWN";
  }
}

void printStatusRegister(const char* label, const SHT3x::StatusRegister& reg) {
  std::printf("%s: raw=0x%04X alert=%d heater=%d rh_alert=%d t_alert=%d reset=%d cmd_err=%d crc_err=%d\n",
              label,
              reg.raw,
              reg.alertPending ? 1 : 0,
              reg.heaterOn ? 1 : 0,
              reg.rhAlert ? 1 : 0,
              reg.tAlert ? 1 : 0,
              reg.resetDetected ? 1 : 0,
              reg.commandError ? 1 : 0,
              reg.writeCrcError ? 1 : 0);
}

void printStatusRestoreSnapshot(const SHT3x::Status& result,
                                const SHT3x::StatusReadSnapshot& snap) {
  std::puts("status_restore:");
  printStatus("result", result);
  std::printf("initialMode=%s finalMode=%s modeInterrupted=%d statusValid=%d restored=%d\n",
              modeToStr(snap.initialMode),
              modeToStr(snap.finalMode),
              snap.modeInterrupted ? 1 : 0,
              snap.statusValid ? 1 : 0,
              snap.restored ? 1 : 0);
  printStatus("stopStatus", snap.stopStatus);
  printStatus("statusReadStatus", snap.statusReadStatus);
  printStatus("restoreStatus", snap.restoreStatus);
  if (snap.statusValid) {
    printStatusRegister("status", snap.status);
  }
}

void printCachedAlerts(const SHT3x::CachedSettings& cached) {
  static constexpr SHT3x::AlertLimitKind ALERT_KINDS[] = {
      SHT3x::AlertLimitKind::HIGH_SET,
      SHT3x::AlertLimitKind::HIGH_CLEAR,
      SHT3x::AlertLimitKind::LOW_CLEAR,
      SHT3x::AlertLimitKind::LOW_SET,
  };
  for (size_t i = 0; i < (sizeof(ALERT_KINDS) / sizeof(ALERT_KINDS[0])); ++i) {
    std::printf("cached_alert_%s_valid=%d cached_alert_%s_raw=0x%04X\n",
                alertKindToStr(ALERT_KINDS[i]),
                cached.alertValid[i] ? 1 : 0,
                alertKindToStr(ALERT_KINDS[i]),
                cached.alertRaw[i]);
  }
}

void printAlertLimit(SHT3x::AlertLimitKind kind) {
  SHT3x::AlertLimit limit{};
  const SHT3x::Status st = gDevice.readAlertLimit(kind, limit);
  printStatus("alert read", st);
  if (st.ok()) {
    std::printf("alert_%s raw=0x%04X temperature=%.2f humidity=%.2f\n",
                alertKindToStr(kind), limit.raw, limit.temperatureC, limit.humidityPct);
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

char* trim(char* text) {
  if (text == nullptr) {
    return text;
  }
  while (*text != '\0' && std::isspace(static_cast<unsigned char>(*text)) != 0) {
    ++text;
  }
  char* end = text + std::strlen(text);
  while (end > text && std::isspace(static_cast<unsigned char>(end[-1])) != 0) {
    *--end = '\0';
  }
  return text;
}

void splitCommand(char* line, char** command, char** args) {
  char* full = trim(line);
  char* space = full;
  while (*space != '\0' && std::isspace(static_cast<unsigned char>(*space)) == 0) {
    ++space;
  }
  if (*space != '\0') {
    *space++ = '\0';
  }
  *command = full;
  *args = trim(space);
}

bool parseU32(const char* text, uint32_t* value) {
  if (text == nullptr || value == nullptr || *text == '\0') {
    return false;
  }
  if (*text == '-' || *text == '+') {
    return false;
  }
  char* end = nullptr;
  errno = 0;
  const unsigned long parsed = std::strtoul(text, &end, 0);
  if (end == text || *end != '\0' || errno == ERANGE || parsed > UINT32_MAX) {
    return false;
  }
  *value = static_cast<uint32_t>(parsed);
  return true;
}

bool parseFloat(const char* text, float* value) {
  if (text == nullptr || value == nullptr || *text == '\0') {
    return false;
  }
  char* end = nullptr;
  errno = 0;
  const float parsed = std::strtof(text, &end);
  if (end == text || *end != '\0' || errno == ERANGE || !std::isfinite(parsed)) {
    return false;
  }
  *value = parsed;
  return true;
}

bool parseBool01(const char* text, bool* value) {
  if (text == nullptr || value == nullptr) {
    return false;
  }
  if (std::strcmp(text, "0") == 0) {
    *value = false;
    return true;
  }
  if (std::strcmp(text, "1") == 0) {
    *value = true;
    return true;
  }
  return false;
}

char* nextToken(char** cursor) {
  if (cursor == nullptr || *cursor == nullptr) {
    return nullptr;
  }
  char* token = trim(*cursor);
  if (*token == '\0') {
    *cursor = token;
    return nullptr;
  }
  char* end = token;
  while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end)) == 0) {
    ++end;
  }
  if (*end != '\0') {
    *end++ = '\0';
  }
  *cursor = trim(end);
  return token;
}

bool noMoreTokens(const char* cursor) {
  if (cursor == nullptr) {
    return true;
  }
  while (*cursor != '\0' &&
         std::isspace(static_cast<unsigned char>(*cursor)) != 0) {
    ++cursor;
  }
  return *cursor == '\0';
}

struct ArgTokens {
  char storage[LINE_LEN] = {};
  const char* values[7] = {};
  size_t count = 0U;
  bool tooMany = false;
};

void parseArgTokens(const char* args, ArgTokens& out) {
  out = ArgTokens{};
  if (args == nullptr) {
    return;
  }
  std::strncpy(out.storage, args, sizeof(out.storage) - 1U);
  char* cursor = out.storage;
  while (true) {
    char* token = nextToken(&cursor);
    if (token == nullptr) {
      return;
    }
    if (out.count >= (sizeof(out.values) / sizeof(out.values[0]))) {
      out.tooMany = true;
      out.count = 0U;
      return;
    }
    out.values[out.count++] = token;
  }
}

bool argEquals(const ArgTokens& args, size_t index, const char* value) {
  return index < args.count && value != nullptr &&
         std::strcmp(args.values[index], value) == 0;
}

bool validCommandArity(const char* command, const ArgTokens& args,
                       bool& knownCommand) {
  knownCommand = true;
  static constexpr const char* NO_ARG_COMMANDS[] = {
      "help", "?", "version", "ver", "scan", "read", "request", "fetch",
      "raw", "comp", "meastime", "result", "status", "status_raw",
      "start_art", "stop_periodic", "stats", "cfg", "settings", "drv",
      "online", "begin", "end", "state", "probe", "xfer_reset",
      "xfer_stats"};
  for (const char* noArg : NO_ARG_COMMANDS) {
    if (std::strcmp(command, noArg) == 0) {
      return args.count == 0U;
    }
  }
  if (std::strcmp(command, "job") == 0) {
    if (args.count == 0U) return true;
    if (args.count == 1U) {
      return argEquals(args, 0U, "current") || argEquals(args, 0U, "last") ||
             argEquals(args, 0U, "cancel");
    }
    return args.count == 2U && argEquals(args, 0U, "step");
  }
  if (std::strcmp(command, "cancel") == 0) return args.count == 0U;
  if (std::strcmp(command, "xfer_assert") == 0) return args.count == 3U;
  if (std::strcmp(command, "mode") == 0 || std::strcmp(command, "repeat") == 0 ||
      std::strcmp(command, "rate") == 0 || std::strcmp(command, "stretch") == 0 ||
      std::strcmp(command, "serial") == 0 || std::strcmp(command, "verbose") == 0 ||
      std::strcmp(command, "stress") == 0 || std::strcmp(command, "stress_mix") == 0) {
    return args.count <= 1U;
  }
  if (std::strcmp(command, "single") == 0 ||
      std::strcmp(command, "i2c_soak") == 0) return args.count == 1U;
  if (std::strcmp(command, "periodic") == 0) {
    return (args.count == 3U && argEquals(args, 0U, "start")) ||
           (args.count == 1U &&
            (argEquals(args, 0U, "fetch") || argEquals(args, 0U, "stop")));
  }
  if (std::strcmp(command, "art") == 0) {
    return args.count == 1U &&
           (argEquals(args, 0U, "start") || argEquals(args, 0U, "fetch") ||
            argEquals(args, 0U, "stop"));
  }
  if (std::strcmp(command, "start_periodic") == 0) return args.count == 2U;
  if (std::strcmp(command, "status_restore") == 0 ||
      std::strcmp(command, "clearstatus") == 0 ||
      std::strcmp(command, "clear_status") == 0 ||
      std::strcmp(command, "reset") == 0 || std::strcmp(command, "defaults") == 0 ||
      std::strcmp(command, "restore") == 0 ||
      std::strcmp(command, "iface_reset") == 0 ||
      std::strcmp(command, "recover") == 0 ||
      std::strcmp(command, "selftest") == 0) {
    return args.count == 0U ||
           (args.count == 1U && argEquals(args, 0U, CONFIRM_TOKEN));
  }
  if (std::strcmp(command, "heater") == 0) {
    if (args.count == 0U) return true;
    if (args.count == 1U) {
      return argEquals(args, 0U, "status") || argEquals(args, 0U, "off") ||
             argEquals(args, 0U, "on");
    }
    return args.count == 2U && argEquals(args, 0U, "on") &&
           argEquals(args, 1U, CONFIRM_TOKEN);
  }
  if (std::strcmp(command, "command") == 0) {
    if (argEquals(args, 0U, "write")) {
      return args.count == 2U ||
             (args.count == 3U && argEquals(args, 2U, CONFIRM_TOKEN));
    }
    if (argEquals(args, 0U, "write_data") || argEquals(args, 0U, "read")) {
      return args.count == 3U ||
             (args.count == 4U && argEquals(args, 3U, CONFIRM_TOKEN));
    }
    return false;
  }
  if (std::strcmp(command, "alert") == 0) {
    if (args.count == 1U && argEquals(args, 0U, "show")) return true;
    if (args.count == 2U &&
        (argEquals(args, 0U, "read") || argEquals(args, 0U, "decode"))) return true;
    if (args.count == 3U && argEquals(args, 0U, "encode")) return true;
    if (argEquals(args, 0U, "disable")) {
      return args.count == 1U ||
             (args.count == 2U && argEquals(args, 1U, CONFIRM_TOKEN));
    }
    if (argEquals(args, 0U, "set") || argEquals(args, 0U, "write")) {
      return args.count == 4U ||
             (args.count == 5U && argEquals(args, 4U, CONFIRM_TOKEN));
    }
    if (argEquals(args, 0U, "raw") && argEquals(args, 1U, "read")) {
      return args.count == 3U;
    }
    if (argEquals(args, 0U, "raw") && argEquals(args, 1U, "write")) {
      return args.count == 4U ||
             (args.count == 5U && argEquals(args, 4U, CONFIRM_TOKEN));
    }
    return false;
  }
  if (std::strcmp(command, "convert") == 0) return args.count == 2U;
  if (std::strcmp(command, "greset") == 0) {
    return args.count == 1U &&
           (argEquals(args, 0U, "arm") || argEquals(args, 0U, "disarm") ||
            argEquals(args, 0U, CONFIRM_TOKEN));
  }

  knownCommand = false;
  return true;
}

bool hasExactConfirmation(char* args, size_t valueTokenCount) {
  char* cursor = args;
  for (size_t i = 0; i < valueTokenCount; ++i) {
    if (nextToken(&cursor) == nullptr) {
      return false;
    }
  }
  const char* confirmation = nextToken(&cursor);
  return confirmation != nullptr &&
         std::strcmp(confirmation, CONFIRM_TOKEN) == 0 &&
         noMoreTokens(cursor);
}

void confirmationRequired(const char* usage) {
  std::printf("Confirmation required: %s\n", usage);
}

bool parseRepeatability(const char* text, SHT3x::Repeatability* out) {
  if (std::strcmp(text, "low") == 0 || std::strcmp(text, "l") == 0) {
    *out = SHT3x::Repeatability::LOW_REPEATABILITY;
    return true;
  }
  if (std::strcmp(text, "med") == 0 || std::strcmp(text, "medium") == 0 ||
      std::strcmp(text, "m") == 0) {
    *out = SHT3x::Repeatability::MEDIUM_REPEATABILITY;
    return true;
  }
  if (std::strcmp(text, "high") == 0 || std::strcmp(text, "h") == 0) {
    *out = SHT3x::Repeatability::HIGH_REPEATABILITY;
    return true;
  }
  return false;
}

bool parseRate(const char* text, SHT3x::PeriodicRate* out) {
  if (std::strcmp(text, "0.5") == 0) {
    *out = SHT3x::PeriodicRate::MPS_0_5;
    return true;
  }
  if (std::strcmp(text, "1") == 0) {
    *out = SHT3x::PeriodicRate::MPS_1;
    return true;
  }
  if (std::strcmp(text, "2") == 0) {
    *out = SHT3x::PeriodicRate::MPS_2;
    return true;
  }
  if (std::strcmp(text, "4") == 0) {
    *out = SHT3x::PeriodicRate::MPS_4;
    return true;
  }
  if (std::strcmp(text, "10") == 0) {
    *out = SHT3x::PeriodicRate::MPS_10;
    return true;
  }
  return false;
}

bool parseAlertKind(const char* text, SHT3x::AlertLimitKind* out) {
  if (std::strcmp(text, "hs") == 0 || std::strcmp(text, "high_set") == 0) {
    *out = SHT3x::AlertLimitKind::HIGH_SET;
    return true;
  }
  if (std::strcmp(text, "hc") == 0 || std::strcmp(text, "high_clear") == 0) {
    *out = SHT3x::AlertLimitKind::HIGH_CLEAR;
    return true;
  }
  if (std::strcmp(text, "lc") == 0 || std::strcmp(text, "low_clear") == 0) {
    *out = SHT3x::AlertLimitKind::LOW_CLEAR;
    return true;
  }
  if (std::strcmp(text, "ls") == 0 || std::strcmp(text, "low_set") == 0) {
    *out = SHT3x::AlertLimitKind::LOW_SET;
    return true;
  }
  return false;
}

esp_err_t createBus(i2c_master_bus_handle_t* bus) {
  i2c_master_bus_config_t busConfig = {};
  busConfig.i2c_port = I2C_PORT;
  busConfig.sda_io_num = I2C_SDA;
  busConfig.scl_io_num = I2C_SCL;
  busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
  busConfig.glitch_ignore_cnt = 7;
  busConfig.flags.enable_internal_pullup = true;
  return i2c_new_master_bus(&busConfig, bus);
}

esp_err_t addDevice(i2c_master_bus_handle_t bus, i2c_master_dev_handle_t* dev) {
  i2c_device_config_t devConfig = {};
  devConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  devConfig.device_address = SHT3X_ADDR;
  devConfig.scl_speed_hz = I2C_FREQ_HZ;
  return i2c_master_bus_add_device(bus, &devConfig, dev);
}

void configureDriver() {
  gConfig = {};
  gConfig.i2cAddress = SHT3X_ADDR;
  gConfig.i2cWrite = idfI2cWrite;
  gConfig.i2cWriteRead = idfI2cWriteRead;
  gConfig.i2cUser = &gApp.i2c;
  gConfig.nowMs = nowMs;
  gConfig.nowUs = nowUs;
  gConfig.cooperativeYield = cooperativeYield;
  gConfig.i2cTimeoutMs = 50;
  gConfig.mode = SHT3x::Mode::SINGLE_SHOT;
  gConfig.clockStretching = SHT3x::ClockStretching::STRETCH_DISABLED;
  gConfig.transportCapabilities = SHT3x::TransportCapability::TIMEOUT |
                                  SHT3x::TransportCapability::BUS_ERROR;
  gConfig.offlineThreshold = 5;
}

void scanBus() {
  if (gApp.i2c.bus == nullptr) {
    std::puts("I2C scan unavailable");
    return;
  }
  int count = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
    if (i2c_master_probe(gApp.i2c.bus, addr, PROBE_TIMEOUT_MS) == ESP_OK) {
      std::printf("found 0x%02X%s\n", addr, addr == SHT3X_ADDR ? " (configured)" : "");
      ++count;
    }
  }
  std::printf("scan: %d device(s)\n", count);
}

void printHelpItem(const char* synopsis, const char* description) {
  std::printf("  %-48s %s\n", synopsis, description);
}

void printHelp() {
  std::puts("SHT3x native ESP-IDF CLI Help");
  printHelpItem("help / ?", "Show this help");
  printHelpItem("version / ver", "Print runtime framework and firmware/library provenance");
  printHelpItem("scan", "Scan I2C ACKs; an ACK is not SHT3x identity");
  printHelpItem("read", "Run one bounded owner-safe measurement job");
  printHelpItem("request", "Schedule an owner-safe measurement without I2C");
  printHelpItem("fetch", "Advance the active owner job by at most one I2C transfer");
  printHelpItem("raw", "Print the last cached raw sample");
  printHelpItem("comp", "Print the last cached compensated sample");
  printHelpItem("meastime", "Show estimated measurement time");
  printHelpItem("job [current|last]", "Show active zero-I2C progress or the retained terminal result");
  printHelpItem("job step <0..255>", "Poll once with an explicit I2C-transfer budget");
  printHelpItem("job cancel / cancel", "Cancel the active job locally with zero I2C");
  printHelpItem("result", "Show the retained terminal job result");
  printHelpItem("mode [single|periodic|art]", "Set or show operating mode");
  printHelpItem("single <low|medium|high>", "Run one no-stretch single-shot measurement");
  printHelpItem("periodic start <rate> <rep>", "Start periodic mode");
  printHelpItem("periodic fetch", "Run one bounded periodic fetch job");
  printHelpItem("periodic stop", "Stop periodic or ART mode");
  printHelpItem("art start", "Start ART mode");
  printHelpItem("art fetch", "Run one bounded ART fetch job");
  printHelpItem("art stop", "Stop ART mode");
  printHelpItem("start_periodic <rate> <rep>", "Alias for periodic start");
  printHelpItem("start_art", "Alias for art start");
  printHelpItem("stop_periodic", "Alias for periodic/art stop");
  printHelpItem("repeat [low|med|high]", "Set or show repeatability");
  printHelpItem("rate [0.5|1|2|4|10]", "Set or show periodic rate");
  printHelpItem("stretch [0|1]", "Set or show clock stretching");
  printHelpItem("status", "Read decoded status while idle");
  printHelpItem("status_restore confirm", "Interrupt active acquisition, read status, and restore mode");
  printHelpItem("status_raw", "Read raw status while idle");
  printHelpItem("clearstatus confirm", "Clear sticky status flags");
  printHelpItem("clear_status confirm", "Alias for clearstatus");
  printHelpItem("heater [status|off|on confirm]", "Inspect or control the heater; enabling requires confirmation");
  printHelpItem("serial [stretch|nostretch]", "Read the CRC-protected serial number");
  printHelpItem("command write <hex> confirm", "Issue an arbitrary raw 16-bit command");
  printHelpItem("command write_data <cmd> <data> confirm", "Issue an arbitrary command with packed data");
  printHelpItem("command read <cmd> <len> confirm", "Issue an arbitrary command and raw read");
  printHelpItem("alert show", "Read all alert limits");
  printHelpItem("alert set <kind> <T> <RH> confirm", "Write an alert limit");
  printHelpItem("alert read <hs|hc|lc|ls>", "Read an alert limit");
  printHelpItem("alert write <kind> <T> <RH> confirm", "Alias for alert set");
  printHelpItem("alert raw read <kind>", "Read a raw packed alert word");
  printHelpItem("alert raw write <kind> <hex> confirm", "Write a raw packed alert word");
  printHelpItem("alert encode <T> <RH>", "Encode an alert word without I2C");
  printHelpItem("alert decode <hex>", "Decode an alert word without I2C");
  printHelpItem("alert disable confirm", "Disable alerts by writing limits");
  printHelpItem("convert <rawT> <rawRH>", "Convert measurement words without I2C");
  printHelpItem("reset confirm", "Soft-reset the sensor");
  printHelpItem("defaults confirm", "Reset command-mode defaults");
  printHelpItem("restore confirm", "Reset the sensor and restore cached settings");
  printHelpItem("iface_reset confirm", "Run the injected interface-reset callback");
  printHelpItem("greset arm / greset disarm", "Arm or disarm one bus-wide general-call reset; no I2C");
  printHelpItem("greset confirm", "Attempt one armed bus-wide reset when application transport enables it");
  printHelpItem("stats", "Show runtime counters and cached settings");
  printHelpItem("cfg / settings", "Show current config; settings also reads status");
  printHelpItem("drv", "Show driver state and health");
  printHelpItem("online", "Show online state");
  printHelpItem("begin", "Owner-safe bind and bounded ensure-idle reconciliation");
  printHelpItem("end", "End the local driver session; rejects an active job");
  printHelpItem("state", "Show compact one-line health summary");
  printHelpItem("probe", "Probe the sensor without health tracking");
  printHelpItem("recover confirm", "Run bounded owner-safe ensure-idle recovery");
  printHelpItem("verbose [0|1]", "Show or set verbose output");
  printHelpItem("stress [N]", "Run N bounded measurement jobs");
  printHelpItem("stress_mix [N]", "Run N mixed operations");
  printHelpItem("i2c_soak <seconds>", "Run a bounded low-output measurement soak");
  printHelpItem("xfer_reset", "Reset example-owned transport counters");
  printHelpItem("xfer_stats", "Show example-owned transport counters");
  printHelpItem("xfer_assert <read> <write> <total>", "Assert exact transport callback totals");
  printHelpItem("selftest confirm", "Run diagnostic I2C self-test commands");
}

void printSettings(bool readStatus) {
  SHT3x::SettingsSnapshot snap{};
  const SHT3x::Status st = readStatus ? gDevice.readSettings(snap) : gDevice.getSettings(snap);
  printStatus(readStatus ? "readSettings" : "getSettings", st);
  if (!st.ok()) {
    return;
  }
  std::printf("state=%s initialized=%d online=%d addr=0x%02X timeout=%lu\n",
              stateToStr(snap.state), snap.initialized ? 1 : 0, gDevice.isOnline() ? 1 : 0,
              snap.i2cAddress, static_cast<unsigned long>(snap.i2cTimeoutMs));
  std::printf("mode=%s repeat=%s rate=%s stretch=%d periodic=%d pending=%d ready=%d sample=%d\n",
              modeToStr(snap.mode), repeatabilityToStr(snap.repeatability),
              rateToStr(snap.periodicRate),
              snap.clockStretching == SHT3x::ClockStretching::STRETCH_ENABLED ? 1 : 0,
              snap.periodicActive ? 1 : 0, snap.measurementPending ? 1 : 0,
              snap.measurementReady ? 1 : 0, snap.hasSample ? 1 : 0);
  std::printf("health ok=%lu fail=%lu consecutive=%u lastOk=%lu lastErr=%lu\n",
              static_cast<unsigned long>(gDevice.totalSuccess()),
              static_cast<unsigned long>(gDevice.totalFailures()),
              static_cast<unsigned>(gDevice.consecutiveFailures()),
              static_cast<unsigned long>(gDevice.lastOkMs()),
              static_cast<unsigned long>(gDevice.lastErrorMs()));
  printStatus("lastError", gDevice.lastError());
  if (gDevice.hasCachedSettings()) {
    const SHT3x::CachedSettings cached = gDevice.getCachedSettings();
    std::printf("cached mode=%s repeat=%s rate=%s stretch=%d heater=%d\n",
                modeToStr(cached.mode),
                repeatabilityToStr(cached.repeatability),
                rateToStr(cached.periodicRate),
                cached.clockStretching == SHT3x::ClockStretching::STRETCH_ENABLED ? 1 : 0,
                cached.heaterEnabled ? 1 : 0);
    printCachedAlerts(cached);
  } else {
    std::puts("cached=0");
  }
  if (snap.statusValid) {
    printStatusRegister("status", snap.status);
  }
}

void printSample() {
  SHT3x::Measurement measurement{};
  SHT3x::Status st = gDevice.getMeasurement(measurement);
  printStatus("measurement", st);
  if (st.ok()) {
    std::printf("temperature=%.2f C humidity=%.2f %%RH\n",
                measurement.temperatureC, measurement.humidityPct);
  }
}

uint32_t allocateRequestId() {
  const uint32_t requestId = gNextRequestId++;
  if (gNextRequestId == 0U) {
    gNextRequestId = 1U;
  }
  return requestId;
}

bool timeReached(uint32_t currentMs, uint32_t deadlineMs) {
  return static_cast<int32_t>(currentMs - deadlineMs) >= 0;
}

void printJobResult(const char* label, const SHT3x::PollJobResult& result) {
  std::printf(
      "%s active=%d terminal=%d completed=%d request=%lu type=%s phase=%s "
      "outcome=%s effect=%s instructions=%u\n",
      label, result.active ? 1 : 0, result.terminal ? 1 : 0,
      result.completed ? 1 : 0, static_cast<unsigned long>(result.requestId),
      jobTypeToStr(result.type), jobPhaseToStr(result.phase),
      jobOutcomeToStr(result.outcome), jobEffectToStr(result.effect),
      static_cast<unsigned>(result.instructionsUsed));
  const char* statusKind = result.status.ok()
                               ? "OK"
                               : (result.status.inProgress()
                                      ? "IN_PROGRESS"
                                      : (result.status.code == SHT3x::Err::CANCELLED
                                             ? "CANCELLED"
                                             : (result.status.code == SHT3x::Err::TIMEOUT
                                                    ? "TIMEOUT"
                                                    : "ERR")));
  std::printf("job_status: %s code=%u detail=%ld msg=%s\n",
              statusKind, static_cast<unsigned>(result.status.code),
              static_cast<long>(result.status.detail),
              result.status.msg ? result.status.msg : "");
}

void clearOwnerToken() {
  gOwner.active = false;
  gOwner.requestId = 0;
  gOwner.deadlineMs = 0;
  gOwner.type = SHT3x::JobType::NONE;
}

void quarantineOwnerInvariant() {
  // A malformed/foreign result must never become the retained owned result.
  // Cancel a possibly still-active core job locally, then release the poisoned
  // token so lifecycle operations cannot be wedged indefinitely.
  SHT3x::PollJobResult ignored{};
  (void)gDevice.cancelJob(SHT3x::CancelReason::REQUESTED, ignored);
  gOwner.lastValid = false;
  clearOwnerToken();
}

SHT3x::Status validateOwnedResult(const SHT3x::PollJobResult& result,
                                  uint32_t requestId,
                                  SHT3x::JobType type,
                                  uint8_t maxInstructions,
                                  const SHT3x::Status& callStatus) {
  if (result.requestId != requestId || result.type != type) {
    return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                "Owner job identity mismatch");
  }
  const uint8_t instructionLimit = maxInstructions == 0U ? 0U : 1U;
  if (result.instructionsUsed > instructionLimit) {
    return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                "Owner job exceeded one-transfer poll bound");
  }
  if (result.status.code != callStatus.code ||
      result.status.detail != callStatus.detail) {
    return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                "Owner call/result status mismatch");
  }
  if (!result.terminal) {
    if (!result.active || result.completed ||
        result.outcome != SHT3x::JobOutcome::ACTIVE ||
        result.status.code != SHT3x::Err::IN_PROGRESS) {
      return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                  "Invalid active owner result");
    }
    return SHT3x::Status::Ok();
  }
  if (result.active || result.outcome == SHT3x::JobOutcome::NONE ||
      result.outcome == SHT3x::JobOutcome::ACTIVE) {
    return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                "Invalid terminal owner result");
  }
  if (result.outcome == SHT3x::JobOutcome::SUCCEEDED) {
    if (!result.status.ok()) {
      return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                  "Successful job has error status");
    }
    if (type == SHT3x::JobType::MEASUREMENT) {
      if (!result.completed || result.effect != SHT3x::JobEffect::NONE) {
        return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                    "Invalid measurement terminal provenance");
      }
    } else if (result.completed ||
               result.effect != SHT3x::JobEffect::DEVICE_STATE_CHANGED) {
      return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                  "Invalid ensure-idle terminal provenance");
    }
  } else if (result.completed) {
    return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                "Failed owner result completed a sample");
  } else if (result.outcome == SHT3x::JobOutcome::CANCELLED &&
             result.status.code != SHT3x::Err::CANCELLED) {
    return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                "Cancelled owner result status mismatch");
  } else if (result.outcome == SHT3x::JobOutcome::TIMED_OUT &&
             result.status.code != SHT3x::Err::TIMEOUT &&
             result.status.code != SHT3x::Err::I2C_TIMEOUT) {
    return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                "Timed-out owner result status mismatch");
  } else if (result.outcome == SHT3x::JobOutcome::FAILED && result.status.ok()) {
    return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                "Failed owner result has OK status");
  }
  return SHT3x::Status::Ok();
}

SHT3x::Status pollOwnedJob(uint8_t budget, bool publish) {
  if (!gOwner.active) {
    return SHT3x::Status::Error(SHT3x::Err::MEASUREMENT_NOT_READY,
                                "No owner job active");
  }

  const uint32_t requestId = gOwner.requestId;
  const SHT3x::JobType type = gOwner.type;
  SHT3x::PollJobResult result{};
  const SHT3x::Status pollStatus = gDevice.pollJob(nowMs(nullptr), budget, result);
  const SHT3x::Status validation =
      validateOwnedResult(result, requestId, type, budget, pollStatus);
  if (publish) {
    printJobResult("job", result);
    if (!validation.ok()) {
      printStatus("job_validation", validation);
    }
  }

  if (!validation.ok()) {
    // Never retain a foreign/invalid result. Quarantine cancels any still-active
    // core job locally and clears the poisoned owner token without accepting it.
    quarantineOwnerInvariant();
    return validation;
  }
  if (result.terminal) {
    gOwner.last = result;
    gOwner.lastValid = true;
    clearOwnerToken();
    return result.status;
  }
  return pollStatus;
}

SHT3x::Status scheduleOwnerJob(SHT3x::JobType type, const char* label,
                               bool publish = true) {
  if (gOwner.active) {
    return SHT3x::Status::Error(SHT3x::Err::BUSY,
                                "Owner job already active");
  }
  const uint32_t startMs = nowMs(nullptr);
  const uint32_t requestId = allocateRequestId();
  SHT3x::JobRequest request{};
  request.requestId = requestId;
  request.deadlineMs = startMs + OWNER_JOB_TIMEOUT_MS;
  request.hasDeadline = true;

  const SHT3x::Status st =
      type == SHT3x::JobType::MEASUREMENT
          ? gDevice.requestMeasurement(request)
          : gDevice.requestEnsureIdle(request);
  if (publish) {
    printStatus(label, st);
  }
  if (st.code == SHT3x::Err::IN_PROGRESS) {
    gOwner.active = true;
    gOwner.requestId = requestId;
    gOwner.deadlineMs = request.deadlineMs;
    gOwner.type = type;
    if (publish) {
      std::printf("job scheduled request=%lu type=%s deadline_ms=%lu zero_i2c=1\n",
                  static_cast<unsigned long>(requestId), jobTypeToStr(type),
                  static_cast<unsigned long>(request.deadlineMs));
    }
  }
  return st;
}

SHT3x::Status cancelOwnedJob(SHT3x::CancelReason reason, bool publish) {
  if (!gOwner.active) {
    return SHT3x::Status::Error(SHT3x::Err::MEASUREMENT_NOT_READY,
                                "No owner job active");
  }
  const uint32_t requestId = gOwner.requestId;
  const SHT3x::JobType type = gOwner.type;
  SHT3x::PollJobResult result{};
  const SHT3x::Status cancelStatus = gDevice.cancelJob(reason, result);
  const SHT3x::Status validation =
      validateOwnedResult(result, requestId, type, 0U, cancelStatus);
  if (publish) {
    printJobResult("job_cancel", result);
    std::puts("job_cancel zero_i2c=1");
    if (!validation.ok()) {
      printStatus("job_validation", validation);
    }
  }
  if (!validation.ok()) {
    quarantineOwnerInvariant();
    return validation;
  }
  const SHT3x::JobOutcome expectedOutcome =
      reason == SHT3x::CancelReason::DEADLINE_EXPIRED
          ? SHT3x::JobOutcome::TIMED_OUT
          : SHT3x::JobOutcome::CANCELLED;
  if (!result.terminal || result.outcome != expectedOutcome) {
    quarantineOwnerInvariant();
    return SHT3x::Status::Error(SHT3x::Err::COMMAND_FAILED,
                                "Invalid cancellation result");
  }
  gOwner.last = result;
  gOwner.lastValid = true;
  clearOwnerToken();
  return cancelStatus;
}

SHT3x::Status awaitOwnedJob(bool publish = true) {
  while (gOwner.active) {
    const uint32_t deadlineMs = gOwner.deadlineMs;
    const SHT3x::Status st = pollOwnedJob(1U, false);
    if (!gOwner.active) {
      if (publish && gOwner.lastValid) {
        printJobResult("job", gOwner.last);
      }
      return st;
    }
    if (st.code != SHT3x::Err::IN_PROGRESS) {
      return st;
    }
    if (timeReached(nowMs(nullptr), deadlineMs)) {
      const SHT3x::Status cancelStatus =
          cancelOwnedJob(SHT3x::CancelReason::DEADLINE_EXPIRED, publish);
      return cancelStatus.code == SHT3x::Err::CANCELLED
                 ? SHT3x::Status::Error(SHT3x::Err::TIMEOUT,
                                        "Owner job deadline expired")
                 : cancelStatus;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return SHT3x::Status::Error(SHT3x::Err::MEASUREMENT_NOT_READY,
                              "No owner job active");
}

SHT3x::Status performMeasurementBlocking(SHT3x::Measurement& measurement,
                                         bool publish = false) {
  SHT3x::Status st = scheduleOwnerJob(SHT3x::JobType::MEASUREMENT, "request",
                                      publish);
  if (st.code != SHT3x::Err::IN_PROGRESS) {
    return st;
  }
  st = awaitOwnedJob(publish);
  return st.ok() ? gDevice.getMeasurement(measurement) : st;
}

void requestAndWait() {
  SHT3x::Measurement measurement{};
  const SHT3x::Status st = performMeasurementBlocking(measurement, true);
  printStatus("measurement", st);
  if (st.ok()) {
    std::printf("temperature=%.2f C humidity=%.2f %%RH\n",
                measurement.temperatureC, measurement.humidityPct);
  }
}

void runSingleShotCommand(const char* args) {
  SHT3x::Repeatability rep{};
  if (!parseRepeatability(args, &rep)) {
    std::puts("Usage: single <low|medium|high>");
    return;
  }

  SHT3x::Status st = gDevice.setMode(SHT3x::Mode::SINGLE_SHOT);
  printStatus("single mode", st);
  if (!st.ok()) {
    return;
  }
  st = gDevice.setRepeatability(rep);
  printStatus("single repeat", st);
  if (!st.ok()) {
    return;
  }
  st = gDevice.setClockStretching(SHT3x::ClockStretching::STRETCH_DISABLED);
  printStatus("single stretch", st);
  if (!st.ok()) {
    return;
  }
  requestAndWait();
}

void startPeriodicCommand(char* args, const char* label) {
  char* rateToken = args;
  char* repToken = std::strchr(args, ' ');
  if (repToken != nullptr) {
    *repToken++ = '\0';
    repToken = trim(repToken);
  }
  SHT3x::PeriodicRate rate{};
  SHT3x::Repeatability rep{};
  printStatus(label,
              (repToken != nullptr && parseRate(rateToken, &rate) &&
               parseRepeatability(repToken, &rep))
                  ? gDevice.startPeriodic(rate, rep)
                  : SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM,
                                          "usage: periodic start <rate> <rep>"));
}

SHT3x::Status performNoStretchMeasurementBlocking(SHT3x::Measurement& measurement) {
  SHT3x::Status st = gDevice.setClockStretching(SHT3x::ClockStretching::STRETCH_DISABLED);
  if (!st.ok()) {
    return st;
  }
  return performMeasurementBlocking(measurement);
}

SHT3x::Status ensureIdleBlocking(const char* label) {
  SHT3x::Status st = scheduleOwnerJob(SHT3x::JobType::ENSURE_IDLE, label);
  return st.code == SHT3x::Err::IN_PROGRESS ? awaitOwnedJob() : st;
}

SHT3x::Status bindAndEnsureIdle() {
  if (gOwner.active) {
    return SHT3x::Status::Error(SHT3x::Err::BUSY,
                                "Cancel active owner job before begin");
  }
  SHT3x::Status st = gDevice.bind(gConfig);
  printStatus("bind", st);
  return st.ok() ? ensureIdleBlocking("ensure_idle") : st;
}

void runI2cSoak(uint32_t durationS) {
  if (gOwner.active) {
    const SHT3x::Status cancelStatus =
        cancelOwnedJob(SHT3x::CancelReason::REQUESTED, false);
    if (cancelStatus.code != SHT3x::Err::CANCELLED) {
      printStatus("i2c_soak cancel", cancelStatus);
      return;
    }
  }

  uint32_t okCount = 0;
  uint32_t failCount = 0;
  bool hasSample = false;
  float minTemp = 0.0F;
  float maxTemp = 0.0F;
  float minHumidity = 0.0F;
  float maxHumidity = 0.0F;

  SHT3x::Status st = gDevice.setMode(SHT3x::Mode::SINGLE_SHOT);
  if (!st.ok()) {
    ++failCount;
  }
  if (st.ok()) {
    st = gDevice.setClockStretching(SHT3x::ClockStretching::STRETCH_DISABLED);
    if (!st.ok()) {
      ++failCount;
    }
  }

  const uint32_t startMs = nowMs(nullptr);
  const uint32_t durationMs = durationS * 1000U;
  const uint32_t successBefore = gDevice.totalSuccess();
  const uint32_t failBefore = gDevice.totalFailures();
  const uint32_t transportSuccessBefore = gDevice.transportSuccess();
  const uint32_t transportFailBefore = gDevice.transportFailures();
  const uint32_t protocolFailBefore = gDevice.protocolFailures();
  const uint32_t notReadyBefore = gDevice.totalNotReady();

  while (st.ok() && (nowMs(nullptr) - startMs) < durationMs) {
    SHT3x::Measurement measurement{};
    st = performMeasurementBlocking(measurement);
    if (st.ok()) {
      ++okCount;
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
      ++failCount;
    }
    taskYIELD();
  }

  const uint32_t elapsedMs = nowMs(nullptr) - startMs;
  std::printf("i2c_soak: ok=%lu fail=%lu duration_ms=%lu\n",
              static_cast<unsigned long>(okCount),
              static_cast<unsigned long>(failCount),
              static_cast<unsigned long>(elapsedMs));
  std::printf("i2c_soak: temp_min=%.2f temp_max=%.2f humidity_min=%.2f humidity_max=%.2f\n",
              hasSample ? minTemp : 0.0F, hasSample ? maxTemp : 0.0F,
              hasSample ? minHumidity : 0.0F,
              hasSample ? maxHumidity : 0.0F);
  std::printf("i2c_soak: health_ok_delta=%lu health_fail_delta=%lu transport_ok_delta=%lu transport_fail_delta=%lu\n",
              static_cast<unsigned long>(gDevice.totalSuccess() - successBefore),
              static_cast<unsigned long>(gDevice.totalFailures() - failBefore),
              static_cast<unsigned long>(gDevice.transportSuccess() - transportSuccessBefore),
              static_cast<unsigned long>(gDevice.transportFailures() - transportFailBefore));
  std::printf("i2c_soak: protocol_fail_delta=%lu not_ready_delta=%lu state=%s consec=%u owner_api=pollJob milli=1\n",
              static_cast<unsigned long>(gDevice.protocolFailures() - protocolFailBefore),
              static_cast<unsigned long>(gDevice.totalNotReady() - notReadyBefore),
              stateToStr(gDevice.state()),
              static_cast<unsigned>(gDevice.consecutiveFailures()));
}

void runStress(uint32_t count) {
  uint32_t ok = 0;
  uint32_t fail = 0;
  for (uint32_t i = 0; i < count; ++i) {
    SHT3x::Measurement measurement{};
    SHT3x::Status st = performMeasurementBlocking(measurement);
    if (st.ok()) {
      ++ok;
    } else {
      ++fail;
      if (gVerbose) {
        printStatus("stress", st);
      }
    }
  }
  std::printf("stress: ok=%lu fail=%lu\n",
              static_cast<unsigned long>(ok), static_cast<unsigned long>(fail));
}

void runStressMix(uint32_t count) {
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
  const uint32_t opCount = static_cast<uint32_t>(sizeof(stats) / sizeof(stats[0]));
  uint32_t okTotal = 0;
  uint32_t failTotal = 0;

  for (uint32_t i = 0; i < count; ++i) {
    const uint32_t op = i % opCount;
    SHT3x::Status st = SHT3x::Status::Ok();

    switch (op) {
      case 0: {
        SHT3x::Measurement measurement{};
        st = performNoStretchMeasurementBlocking(measurement);
        break;
      }
      case 1: {
        SHT3x::StatusRegister reg{};
        st = gDevice.readStatus(reg);
        break;
      }
      case 2: {
        uint32_t serial = 0;
        st = gDevice.readSerialNumber(serial, SHT3x::ClockStretching::STRETCH_DISABLED);
        break;
      }
      case 3:
        st = gDevice.setRepeatability(
            static_cast<SHT3x::Repeatability>((i / opCount) % 3U));
        break;
      case 4:
        st = gDevice.setPeriodicRate(static_cast<SHT3x::PeriodicRate>((i / opCount) % 5U));
        break;
      case 5:
        st = gDevice.setClockStretching(((i / opCount) % 2U) != 0U
                                            ? SHT3x::ClockStretching::STRETCH_ENABLED
                                            : SHT3x::ClockStretching::STRETCH_DISABLED);
        break;
      case 6: {
        bool enabled = false;
        st = gDevice.readHeaterStatus(enabled);
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
      if (gVerbose) {
        printStatus(stats[op].name, st);
      }
    }
    taskYIELD();
  }

  std::puts("stress_mix summary");
  std::printf("Total: ok=%lu fail=%lu\n",
              static_cast<unsigned long>(okTotal),
              static_cast<unsigned long>(failTotal));
  for (uint32_t i = 0; i < opCount; ++i) {
    std::printf("%s: ok=%lu fail=%lu\n",
                stats[i].name,
                static_cast<unsigned long>(stats[i].ok),
                static_cast<unsigned long>(stats[i].fail));
  }
  (void)gDevice.setClockStretching(SHT3x::ClockStretching::STRETCH_DISABLED);
}

void runSelftest() {
  uint32_t pass = 0;
  uint32_t fail = 0;
  auto check = [&](const char* name, const SHT3x::Status& st) {
    std::printf("[%s] %s\n", st.ok() ? "OK" : "FAIL", name);
    if (st.ok()) {
      ++pass;
    } else {
      ++fail;
      if (gVerbose) {
        printStatus(name, st);
      }
    }
  };
  check("probe", gDevice.probe());
  SHT3x::SettingsSnapshot snap{};
  check("readSettings", gDevice.readSettings(snap));
  uint16_t statusRaw = 0;
  check("readStatus", gDevice.readStatus(statusRaw));
  bool heater = false;
  check("readHeaterStatus", gDevice.readHeaterStatus(heater));
  std::printf("selftest: pass=%lu fail=%lu\n",
              static_cast<unsigned long>(pass), static_cast<unsigned long>(fail));
}

void showLastJobResult() {
  if (!gOwner.lastValid) {
    std::puts("job last available=0");
    return;
  }
  std::puts("job last available=1");
  printJobResult("job_last", gOwner.last);
}

void handleJobCommand(char* args) {
  char* cursor = args;
  char* sub = nextToken(&cursor);
  if (sub == nullptr || std::strcmp(sub, "current") == 0) {
    if (sub != nullptr && !noMoreTokens(cursor)) {
      std::puts("Usage: job [current|last] | job step <0..255> | job cancel");
      return;
    }
    if (!gOwner.active) {
      std::puts("job active=0");
      return;
    }
    (void)pollOwnedJob(0U, true);
    return;
  }
  if (std::strcmp(sub, "last") == 0 && noMoreTokens(cursor)) {
    showLastJobResult();
    return;
  }
  if (std::strcmp(sub, "cancel") == 0 && noMoreTokens(cursor)) {
    const SHT3x::Status st =
        cancelOwnedJob(SHT3x::CancelReason::REQUESTED, true);
    if (st.code == SHT3x::Err::CANCELLED) {
      std::puts("cancel: OK outcome=cancelled zero_i2c=1");
    } else {
      printStatus("cancel", st);
    }
    return;
  }
  if (std::strcmp(sub, "step") == 0) {
    const char* budgetToken = nextToken(&cursor);
    uint32_t budget = 0;
    if (budgetToken == nullptr || !parseU32(budgetToken, &budget) ||
        budget > std::numeric_limits<uint8_t>::max() || !noMoreTokens(cursor)) {
      std::puts("Usage: job step <0..255>");
      return;
    }
    printStatus("job step",
                pollOwnedJob(static_cast<uint8_t>(budget), true));
    return;
  }
  std::puts("Usage: job [current|last] | job step <0..255> | job cancel");
}

void resetTransferCounters() {
  gApp.i2c.readTransfers = 0;
  gApp.i2c.writeTransfers = 0;
  gApp.i2c.totalTransfers = 0;
  std::puts("XFER_RESET read=0 write=0 total=0");
}

void printTransferCounters() {
  std::printf("XFER_STATS read=%lu write=%lu total=%lu\n",
              static_cast<unsigned long>(gApp.i2c.readTransfers),
              static_cast<unsigned long>(gApp.i2c.writeTransfers),
              static_cast<unsigned long>(gApp.i2c.totalTransfers));
}

void assertTransferCounters(char* args) {
  char* cursor = args;
  const char* readToken = nextToken(&cursor);
  const char* writeToken = nextToken(&cursor);
  const char* totalToken = nextToken(&cursor);
  uint32_t expectedRead = 0;
  uint32_t expectedWrite = 0;
  uint32_t expectedTotal = 0;
  if (readToken == nullptr || writeToken == nullptr || totalToken == nullptr ||
      !parseU32(readToken, &expectedRead) ||
      !parseU32(writeToken, &expectedWrite) ||
      !parseU32(totalToken, &expectedTotal) || !noMoreTokens(cursor)) {
    std::puts("Usage: xfer_assert <read> <write> <total>");
    return;
  }
  const bool pass = expectedRead == gApp.i2c.readTransfers &&
                    expectedWrite == gApp.i2c.writeTransfers &&
                    expectedTotal == gApp.i2c.totalTransfers;
  std::printf("XFER_ASSERT %s expected_read=%lu expected_write=%lu "
              "expected_total=%lu actual_read=%lu actual_write=%lu actual_total=%lu\n",
              pass ? "PASS" : "FAIL",
              static_cast<unsigned long>(expectedRead),
              static_cast<unsigned long>(expectedWrite),
              static_cast<unsigned long>(expectedTotal),
              static_cast<unsigned long>(gApp.i2c.readTransfers),
              static_cast<unsigned long>(gApp.i2c.writeTransfers),
              static_cast<unsigned long>(gApp.i2c.totalTransfers));
}

void handleCommandLine(char* line) {
  char* cmd = nullptr;
  char* args = nullptr;
  splitCommand(line, &cmd, &args);
  if (cmd == nullptr || *cmd == '\0') {
    return;
  }

  const bool gresetControl =
      std::strcmp(cmd, "greset") == 0 &&
      (std::strcmp(args, "arm") == 0 || std::strcmp(args, "disarm") == 0 ||
       std::strcmp(args, "confirm") == 0);
  if (gGeneralCallResetArmed && !gresetControl) {
    gGeneralCallResetArmed = false;
    std::puts("greset armed=0 zero_i2c=1 reason=intervening_command");
  }

  ArgTokens parsedArgs{};
  parseArgTokens(args, parsedArgs);
  bool knownCommand = false;
  if (parsedArgs.tooMany || !validCommandArity(cmd, parsedArgs, knownCommand)) {
    std::puts("Invalid command arity. Use 'help' for the exact synopsis.");
    return;
  }

  if (std::strcmp(cmd, "help") == 0 || std::strcmp(cmd, "?") == 0) {
    printHelp();
  } else if (std::strcmp(cmd, "version") == 0 || std::strcmp(cmd, "ver") == 0) {
    std::printf("framework=native-esp-idf target=%s idf_version=%s\n",
                CONFIG_IDF_TARGET, esp_get_idf_version());
    std::printf("example_build=%s %s\n", __DATE__, __TIME__);
    std::printf("library_version=%s\n", SHT3x::VERSION);
    std::printf("library_full=%s\n", SHT3x::VERSION_FULL);
    std::printf("library_build=%s\n", SHT3x::BUILD_TIMESTAMP);
    std::printf("library_commit=%s git_status=%s\n", SHT3x::GIT_COMMIT, SHT3x::GIT_STATUS);
  } else if (std::strcmp(cmd, "scan") == 0) {
    scanBus();
  } else if (std::strcmp(cmd, "begin") == 0) {
    printStatus("begin", bindAndEnsureIdle());
  } else if (std::strcmp(cmd, "end") == 0) {
    if (gOwner.active) {
      printStatus("end", SHT3x::Status::Error(
                             SHT3x::Err::BUSY,
                             "Cancel or complete active owner job before end"));
    } else {
      gDevice.end();
      std::puts("end: OK");
    }
  } else if (std::strcmp(cmd, "probe") == 0) {
    printStatus("probe", gDevice.probe());
  } else if (std::strcmp(cmd, "recover") == 0) {
    if (!hasExactConfirmation(args, 0U)) {
      confirmationRequired("recover confirm");
    } else {
      printStatus("recover", ensureIdleBlocking("recover request"));
    }
  } else if (std::strcmp(cmd, "drv") == 0 || std::strcmp(cmd, "cfg") == 0 ||
             std::strcmp(cmd, "settings") == 0 || std::strcmp(cmd, "stats") == 0) {
    printSettings(std::strcmp(cmd, "settings") == 0);
  } else if (std::strcmp(cmd, "state") == 0 || std::strcmp(cmd, "online") == 0) {
    std::printf("state=%s online=%d initialized=%d\n", stateToStr(gDevice.state()),
                gDevice.isOnline() ? 1 : 0, gDevice.isInitialized() ? 1 : 0);
  } else if (std::strcmp(cmd, "read") == 0) {
    requestAndWait();
  } else if (std::strcmp(cmd, "request") == 0) {
    (void)scheduleOwnerJob(SHT3x::JobType::MEASUREMENT, "request");
  } else if (std::strcmp(cmd, "fetch") == 0) {
    if (!gOwner.active) {
      std::puts("fetch: ERR no_active_job=1");
      return;
    }
    const uint32_t requestId = gOwner.requestId;
    const SHT3x::Status st = pollOwnedJob(1U, true);
    printStatus("fetch", st);
    if (!gOwner.active && gOwner.lastValid && gOwner.last.requestId == requestId &&
        gOwner.last.type == SHT3x::JobType::MEASUREMENT &&
        gOwner.last.outcome == SHT3x::JobOutcome::SUCCEEDED) {
      printSample();
    }
  } else if (std::strcmp(cmd, "job") == 0) {
    handleJobCommand(args);
  } else if (std::strcmp(cmd, "cancel") == 0) {
    if (!noMoreTokens(args)) {
      std::puts("Usage: cancel");
    } else {
      const SHT3x::Status st =
          cancelOwnedJob(SHT3x::CancelReason::REQUESTED, true);
      if (st.code == SHT3x::Err::CANCELLED) {
        std::puts("cancel: OK outcome=cancelled zero_i2c=1");
      } else {
        printStatus("cancel", st);
      }
    }
  } else if (std::strcmp(cmd, "result") == 0) {
    showLastJobResult();
  } else if (std::strcmp(cmd, "raw") == 0) {
    SHT3x::RawSample raw{};
    SHT3x::Status st = gDevice.getRawSample(raw);
    printStatus("raw", st);
    if (st.ok()) {
      std::printf("rawT=0x%04X rawRH=0x%04X\n", raw.rawTemperature, raw.rawHumidity);
    }
  } else if (std::strcmp(cmd, "comp") == 0) {
    SHT3x::CompensatedSample comp{};
    SHT3x::Status st = gDevice.getCompensatedSample(comp);
    printStatus("comp", st);
    if (st.ok()) {
      std::printf("tempC_x100=%ld humidityPct_x100=%lu\n",
                  static_cast<long>(comp.tempC_x100),
                  static_cast<unsigned long>(comp.humidityPct_x100));
    }
  } else if (std::strcmp(cmd, "meastime") == 0) {
    std::printf("measurement_time_ms=%lu\n",
                static_cast<unsigned long>(gDevice.estimateMeasurementTimeMs()));
  } else if (std::strcmp(cmd, "mode") == 0) {
    if (*args == '\0') {
      SHT3x::Mode mode{};
      printStatus("mode", gDevice.getMode(mode));
      std::printf("mode=%s\n", modeToStr(mode));
    } else if (std::strcmp(args, "single") == 0) {
      printStatus("mode", gDevice.setMode(SHT3x::Mode::SINGLE_SHOT));
    } else if (std::strcmp(args, "periodic") == 0) {
      printStatus("mode", gDevice.setMode(SHT3x::Mode::PERIODIC));
    } else if (std::strcmp(args, "art") == 0) {
      printStatus("mode", gDevice.setMode(SHT3x::Mode::ART));
    } else {
      std::puts("Usage: mode [single|periodic|art]");
    }
  } else if (std::strcmp(cmd, "repeat") == 0) {
    if (*args == '\0') {
      SHT3x::Repeatability rep{};
      printStatus("repeat", gDevice.getRepeatability(rep));
      std::printf("repeat=%s\n", repeatabilityToStr(rep));
    } else {
      SHT3x::Repeatability rep{};
      printStatus("repeat", parseRepeatability(args, &rep)
                                ? gDevice.setRepeatability(rep)
                                : SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM, "bad repeatability"));
    }
  } else if (std::strcmp(cmd, "rate") == 0) {
    if (*args == '\0') {
      SHT3x::PeriodicRate rate{};
      printStatus("rate", gDevice.getPeriodicRate(rate));
      std::printf("rate=%s\n", rateToStr(rate));
    } else {
      SHT3x::PeriodicRate rate{};
      printStatus("rate", parseRate(args, &rate)
                              ? gDevice.setPeriodicRate(rate)
                              : SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM, "bad rate"));
    }
  } else if (std::strcmp(cmd, "stretch") == 0) {
    if (*args == '\0') {
      SHT3x::ClockStretching stretch{};
      printStatus("stretch", gDevice.getClockStretching(stretch));
      std::printf("stretch=%d\n", stretch == SHT3x::ClockStretching::STRETCH_ENABLED ? 1 : 0);
    } else {
      bool enabled = false;
      printStatus("stretch",
                  parseBool01(args, &enabled)
                      ? gDevice.setClockStretching(
                            enabled ? SHT3x::ClockStretching::STRETCH_ENABLED
                                    : SHT3x::ClockStretching::STRETCH_DISABLED)
                      : SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM,
                                             "usage: stretch [0|1]"));
    }
  } else if (std::strcmp(cmd, "single") == 0) {
    runSingleShotCommand(args);
  } else if (std::strcmp(cmd, "periodic") == 0) {
    char* sub = nullptr;
    char* tail = nullptr;
    splitCommand(args, &sub, &tail);
    if (sub != nullptr && std::strcmp(sub, "start") == 0) {
      startPeriodicCommand(tail, "periodic start");
    } else if (sub != nullptr && std::strcmp(sub, "fetch") == 0) {
      requestAndWait();
    } else if (sub != nullptr && std::strcmp(sub, "stop") == 0) {
      printStatus("periodic stop", gDevice.stopPeriodic());
    } else {
      std::puts("Usage: periodic start <rate> <rep> | periodic fetch | periodic stop");
    }
  } else if (std::strcmp(cmd, "art") == 0) {
    char* sub = nullptr;
    char* tail = nullptr;
    splitCommand(args, &sub, &tail);
    (void)tail;
    if (sub != nullptr && std::strcmp(sub, "start") == 0) {
      printStatus("art start", gDevice.startArt());
    } else if (sub != nullptr && std::strcmp(sub, "fetch") == 0) {
      requestAndWait();
    } else if (sub != nullptr && std::strcmp(sub, "stop") == 0) {
      printStatus("art stop", gDevice.stopPeriodic());
    } else {
      std::puts("Usage: art start | art fetch | art stop");
    }
  } else if (std::strcmp(cmd, "start_periodic") == 0) {
    startPeriodicCommand(args, "start_periodic");
  } else if (std::strcmp(cmd, "start_art") == 0) {
    printStatus("start_art", gDevice.startArt());
  } else if (std::strcmp(cmd, "stop_periodic") == 0) {
    printStatus("stop_periodic", gDevice.stopPeriodic());
  } else if (std::strcmp(cmd, "status") == 0) {
    SHT3x::StatusRegister reg{};
    SHT3x::Status st = gDevice.readStatus(reg);
    printStatus("status", st);
    if (st.ok()) {
      printStatusRegister("status", reg);
    }
  } else if (std::strcmp(cmd, "status_restore") == 0) {
    if (!hasExactConfirmation(args, 0U)) {
      confirmationRequired("status_restore confirm");
    } else {
      SHT3x::StatusReadSnapshot snap{};
      const SHT3x::Status st = gDevice.readStatusWithModeRestore(snap);
      printStatusRestoreSnapshot(st, snap);
    }
  } else if (std::strcmp(cmd, "status_raw") == 0) {
    uint16_t raw = 0;
    SHT3x::Status st = gDevice.readStatus(raw);
    printStatus("status_raw", st);
    if (st.ok()) {
      std::printf("status=0x%04X\n", raw);
    }
  } else if (std::strcmp(cmd, "clearstatus") == 0 || std::strcmp(cmd, "clear_status") == 0) {
    if (!hasExactConfirmation(args, 0U)) {
      confirmationRequired("clearstatus confirm");
    } else {
      printStatus("clearstatus", gDevice.clearStatus());
    }
  } else if (std::strcmp(cmd, "heater") == 0) {
    if (*args == '\0' || std::strcmp(args, "status") == 0) {
      bool enabled = false;
      SHT3x::Status st = gDevice.readHeaterStatus(enabled);
      printStatus("heater", st);
      if (st.ok()) {
        std::printf("heater=%d\n", enabled ? 1 : 0);
      }
    } else if (std::strcmp(args, "on confirm") == 0) {
      printStatus("heater", gDevice.setHeater(true));
    } else if (std::strcmp(args, "on") == 0) {
      confirmationRequired("heater on confirm");
    } else if (std::strcmp(args, "off") == 0) {
      printStatus("heater", gDevice.setHeater(false));
    } else {
      std::puts("Usage: heater [status|off|on confirm]");
    }
  } else if (std::strcmp(cmd, "serial") == 0) {
    if (*args != '\0' && std::strcmp(args, "stretch") != 0 &&
        std::strcmp(args, "nostretch") != 0) {
      std::puts("Usage: serial [stretch|nostretch]");
    } else {
      uint32_t serial = 0;
      const bool stretch = std::strcmp(args, "stretch") == 0;
      SHT3x::Status st = gDevice.readSerialNumber(
          serial, stretch ? SHT3x::ClockStretching::STRETCH_ENABLED
                          : SHT3x::ClockStretching::STRETCH_DISABLED);
      printStatus("serial", st);
      if (st.ok()) {
        std::printf("serial=0x%08lX\n", static_cast<unsigned long>(serial));
      }
    }
  } else if (std::strcmp(cmd, "reset") == 0) {
    if (!hasExactConfirmation(args, 0U)) {
      confirmationRequired("reset confirm");
    } else {
      printStatus("reset", gDevice.softReset());
    }
  } else if (std::strcmp(cmd, "defaults") == 0) {
    if (!hasExactConfirmation(args, 0U)) {
      confirmationRequired("defaults confirm");
    } else {
      printStatus("defaults", gDevice.resetToDefaults());
    }
  } else if (std::strcmp(cmd, "restore") == 0) {
    if (!hasExactConfirmation(args, 0U)) {
      confirmationRequired("restore confirm");
    } else {
      printStatus("restore", gDevice.resetAndRestore());
    }
  } else if (std::strcmp(cmd, "iface_reset") == 0) {
    if (!hasExactConfirmation(args, 0U)) {
      confirmationRequired("iface_reset confirm");
    } else {
      printStatus("iface_reset", gDevice.interfaceReset());
    }
  } else if (std::strcmp(cmd, "greset") == 0) {
    if (std::strcmp(args, "arm") == 0) {
      gGeneralCallResetArmed = true;
      std::puts("greset armed=1 zero_i2c=1; next exact 'greset confirm' consumes the arm");
    } else if (std::strcmp(args, "disarm") == 0) {
      gGeneralCallResetArmed = false;
      std::puts("greset armed=0 zero_i2c=1");
    } else if (std::strcmp(args, "confirm") == 0) {
      if (!gGeneralCallResetArmed) {
        std::puts("greset rejected: run 'greset arm' first");
      } else {
        gGeneralCallResetArmed = false;
        printStatus("greset", gDevice.generalCallReset());
      }
    } else {
      std::puts("Usage: greset arm | greset disarm | greset confirm");
    }
  } else if (std::strcmp(cmd, "command") == 0) {
    char* sub = nullptr;
    char* tail = nullptr;
    splitCommand(args, &sub, &tail);
    if (sub != nullptr && std::strcmp(sub, "write") == 0) {
      char* cursor = tail;
      const char* commandToken = nextToken(&cursor);
      const char* confirm = nextToken(&cursor);
      uint32_t command = 0;
      if (commandToken == nullptr || confirm == nullptr ||
          std::strcmp(confirm, CONFIRM_TOKEN) != 0 || !noMoreTokens(cursor)) {
        confirmationRequired("command write <hex> confirm");
      } else {
        printStatus("command write",
                    parseU32(commandToken, &command) && command <= UINT16_MAX
                        ? gDevice.writeCommand(static_cast<uint16_t>(command))
                        : SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM,
                                               "bad 16-bit command"));
      }
    } else if (sub != nullptr && std::strcmp(sub, "write_data") == 0) {
      char* cursor = tail;
      const char* commandToken = nextToken(&cursor);
      const char* dataToken = nextToken(&cursor);
      const char* confirm = nextToken(&cursor);
      uint32_t command = 0;
      uint32_t data = 0;
      if (commandToken == nullptr || dataToken == nullptr || confirm == nullptr ||
          std::strcmp(confirm, CONFIRM_TOKEN) != 0 || !noMoreTokens(cursor)) {
        confirmationRequired("command write_data <cmd> <data> confirm");
      } else {
        printStatus("command write_data",
                    parseU32(commandToken, &command) && command <= UINT16_MAX &&
                            parseU32(dataToken, &data) && data <= UINT16_MAX
                        ? gDevice.writeCommandWithData(
                              static_cast<uint16_t>(command),
                              static_cast<uint16_t>(data))
                        : SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM,
                                               "bad 16-bit command or data"));
      }
    } else if (sub != nullptr && std::strcmp(sub, "read") == 0) {
      char* cursor = tail;
      const char* commandToken = nextToken(&cursor);
      const char* lenToken = nextToken(&cursor);
      const char* confirm = nextToken(&cursor);
      uint32_t command = 0;
      uint32_t len = 0;
      uint8_t buf[6] = {};
      if (commandToken == nullptr || lenToken == nullptr || confirm == nullptr ||
          std::strcmp(confirm, CONFIRM_TOKEN) != 0 || !noMoreTokens(cursor)) {
        confirmationRequired("command read <cmd> <len> confirm");
        return;
      }
      SHT3x::Status st = parseU32(commandToken, &command) && command <= UINT16_MAX &&
                                parseU32(lenToken, &len) && len > 0U && len <= sizeof(buf)
                            ? gDevice.readCommand(static_cast<uint16_t>(command), buf, len)
                            : SHT3x::Status::Error(
                                  SHT3x::Err::INVALID_PARAM,
                                  "usage: command read <cmd16> <len 1..6> confirm");
      printStatus("command read", st);
      if (st.ok()) {
        for (uint32_t i = 0; i < len; ++i) {
          std::printf("%02X ", buf[i]);
        }
        std::putchar('\n');
      }
    } else {
      std::puts("Usage: command write|write_data|read ... confirm");
    }
  } else if (std::strcmp(cmd, "alert") == 0) {
    char* sub = nullptr;
    char* tail = nullptr;
    splitCommand(args, &sub, &tail);
    if (sub != nullptr && std::strcmp(sub, "disable") == 0) {
      if (!hasExactConfirmation(tail, 0U)) {
        confirmationRequired("alert disable confirm");
      } else {
        printStatus("alert disable", gDevice.disableAlerts());
      }
    } else if (sub != nullptr && std::strcmp(sub, "show") == 0) {
      printAllAlertLimits();
    } else if (sub != nullptr && std::strcmp(sub, "encode") == 0) {
      char* rhToken = std::strchr(tail, ' ');
      float temp = 0.0f;
      float rh = 0.0f;
      if (rhToken != nullptr) {
        *rhToken++ = '\0';
        rhToken = trim(rhToken);
      }
      if (rhToken != nullptr && parseFloat(tail, &temp) && parseFloat(rhToken, &rh)) {
        std::printf("encoded=0x%04X\n", SHT3x::SHT3x::encodeAlertLimit(temp, rh));
      } else {
        std::puts("Usage: alert encode <T> <RH>");
      }
    } else if (sub != nullptr && std::strcmp(sub, "decode") == 0) {
      uint32_t raw = 0;
      float temp = 0.0f;
      float rh = 0.0f;
      if (parseU32(tail, &raw) && raw <= UINT16_MAX) {
        SHT3x::SHT3x::decodeAlertLimit(static_cast<uint16_t>(raw), temp, rh);
        std::printf("temperature=%.2f humidity=%.2f\n", temp, rh);
      } else {
        std::puts("Usage: alert decode <hex>");
      }
    } else if (sub != nullptr && std::strcmp(sub, "raw") == 0) {
      char* op = nullptr;
      char* rest = nullptr;
      splitCommand(tail, &op, &rest);
      char* cursor = rest;
      const char* kindToken = nextToken(&cursor);
      SHT3x::AlertLimitKind kind{};
      if (op != nullptr && std::strcmp(op, "read") == 0 &&
          kindToken != nullptr && parseAlertKind(kindToken, &kind) &&
          noMoreTokens(cursor)) {
        uint16_t value = 0;
        SHT3x::Status st = gDevice.readAlertLimitRaw(kind, value);
        printStatus("alert raw read", st);
        if (st.ok()) {
          std::printf("raw=0x%04X\n", value);
        }
      } else if (op != nullptr && std::strcmp(op, "write") == 0) {
        const char* valueToken = nextToken(&cursor);
        const char* confirm = nextToken(&cursor);
        uint32_t value = 0;
        if (kindToken == nullptr || valueToken == nullptr || confirm == nullptr ||
            !parseAlertKind(kindToken, &kind) ||
            std::strcmp(confirm, CONFIRM_TOKEN) != 0 || !noMoreTokens(cursor)) {
          confirmationRequired("alert raw write <kind> <hex> confirm");
        } else {
          printStatus("alert raw write",
                      parseU32(valueToken, &value) && value <= UINT16_MAX
                          ? gDevice.writeAlertLimitRaw(kind,
                                                      static_cast<uint16_t>(value))
                          : SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM,
                                                 "bad 16-bit alert value"));
        }
      } else {
        std::puts("Usage: alert raw read <kind> | alert raw write <kind> <hex> confirm");
      }
    } else if (sub != nullptr && std::strcmp(sub, "read") == 0) {
      SHT3x::AlertLimitKind kind{};
      SHT3x::AlertLimit limit{};
      SHT3x::Status st = parseAlertKind(tail, &kind)
                              ? gDevice.readAlertLimit(kind, limit)
                              : SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM, "bad kind");
      printStatus("alert read", st);
      if (st.ok()) {
        std::printf("raw=0x%04X temperature=%.2f humidity=%.2f\n",
                    limit.raw, limit.temperatureC, limit.humidityPct);
      }
    } else if (sub != nullptr && (std::strcmp(sub, "write") == 0 || std::strcmp(sub, "set") == 0)) {
      char* cursor = tail;
      const char* kindToken = nextToken(&cursor);
      const char* tempToken = nextToken(&cursor);
      const char* rhToken = nextToken(&cursor);
      const char* confirm = nextToken(&cursor);
      SHT3x::AlertLimitKind kind{};
      float temp = 0.0f;
      float rh = 0.0f;
      if (kindToken == nullptr || tempToken == nullptr || rhToken == nullptr ||
          confirm == nullptr || std::strcmp(confirm, CONFIRM_TOKEN) != 0 ||
          !noMoreTokens(cursor)) {
        confirmationRequired("alert set <kind> <T> <RH> confirm");
      } else {
        printStatus("alert write",
                    parseAlertKind(kindToken, &kind) &&
                            parseFloat(tempToken, &temp) &&
                            parseFloat(rhToken, &rh)
                        ? gDevice.writeAlertLimit(kind, temp, rh)
                        : SHT3x::Status::Error(
                              SHT3x::Err::INVALID_PARAM,
                              "usage: alert set <kind> <finite T> <finite RH> confirm"));
      }
    } else {
      std::puts("Usage: alert show|read|set|write|raw|encode|decode|disable ...");
    }
  } else if (std::strcmp(cmd, "convert") == 0) {
    char* rhToken = std::strchr(args, ' ');
    uint32_t rawT = 0;
    uint32_t rawRh = 0;
    if (rhToken != nullptr) {
      *rhToken++ = '\0';
      rhToken = trim(rhToken);
    }
    if (rhToken != nullptr && parseU32(args, &rawT) && rawT <= UINT16_MAX &&
        parseU32(rhToken, &rawRh) && rawRh <= UINT16_MAX) {
      std::printf("temperature=%.2f humidity=%.2f tempC_x100=%ld humidity_x100=%lu\n",
                  SHT3x::SHT3x::convertTemperatureC(static_cast<uint16_t>(rawT)),
                  SHT3x::SHT3x::convertHumidityPct(static_cast<uint16_t>(rawRh)),
                  static_cast<long>(SHT3x::SHT3x::convertTemperatureC_x100(static_cast<uint16_t>(rawT))),
                  static_cast<unsigned long>(SHT3x::SHT3x::convertHumidityPct_x100(static_cast<uint16_t>(rawRh))));
    } else {
      std::puts("Usage: convert <rawT> <rawRH>");
    }
  } else if (std::strcmp(cmd, "verbose") == 0) {
    if (*args != '\0' && !parseBool01(args, &gVerbose)) {
      std::puts("Usage: verbose [0|1]");
      return;
    }
    std::printf("verbose=%d\n", gVerbose ? 1 : 0);
  } else if (std::strcmp(cmd, "i2c_soak") == 0) {
    uint32_t seconds = 0;
    if (!parseU32(args, &seconds) || seconds == 0U ||
        seconds > I2C_SOAK_MAX_SECONDS) {
      std::printf("i2c_soak seconds must be 1..%lu\n",
                  static_cast<unsigned long>(I2C_SOAK_MAX_SECONDS));
      return;
    }
    runI2cSoak(seconds);
  } else if (std::strcmp(cmd, "stress") == 0 || std::strcmp(cmd, "stress_mix") == 0) {
    uint32_t count = 10;
    if (*args != '\0' && !parseU32(args, &count)) {
      std::puts("Usage: stress[_mix] [count]");
      return;
    }
    if (count == 0 || count > STRESS_MAX_COUNT) {
      std::printf("stress count must be 1..%lu\n",
                  static_cast<unsigned long>(STRESS_MAX_COUNT));
      return;
    }
    if (std::strcmp(cmd, "stress_mix") == 0) {
      runStressMix(count);
    } else {
      runStress(count);
    }
  } else if (std::strcmp(cmd, "xfer_reset") == 0) {
    if (!noMoreTokens(args)) {
      std::puts("Usage: xfer_reset");
    } else {
      resetTransferCounters();
    }
  } else if (std::strcmp(cmd, "xfer_stats") == 0) {
    if (!noMoreTokens(args)) {
      std::puts("Usage: xfer_stats");
    } else {
      printTransferCounters();
    }
  } else if (std::strcmp(cmd, "xfer_assert") == 0) {
    assertTransferCounters(args);
  } else if (std::strcmp(cmd, "selftest") == 0) {
    if (!hasExactConfirmation(args, 0U)) {
      confirmationRequired("selftest confirm");
    } else {
      runSelftest();
    }
  } else {
    std::puts("Unknown command. Try 'help'.");
  }
}

void inputTask(void* arg) {
  QueueHandle_t queue = static_cast<QueueHandle_t>(arg);
  char buffer[LINE_LEN] = {};
  bool discardingOverflow = false;
  while (true) {
    if (std::fgets(buffer, sizeof(buffer), stdin) == nullptr) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    size_t len = std::strlen(buffer);
    const bool terminated =
        len > 0U && (buffer[len - 1U] == '\n' || buffer[len - 1U] == '\r');
    if (discardingOverflow) {
      if (terminated) {
        discardingOverflow = false;
      }
      continue;
    }
    if (!terminated) {
      discardingOverflow = true;
      std::puts("Input line too long; discarded");
      continue;
    }
    while (len > 0U && (buffer[len - 1U] == '\n' || buffer[len - 1U] == '\r')) {
      buffer[--len] = '\0';
    }
    if (len == 0U) {
      continue;
    }
    CliLine line{};
    std::strncpy(line.text, buffer, sizeof(line.text) - 1U);
    if (xQueueSend(queue, &line, pdMS_TO_TICKS(CLI_QUEUE_SEND_TIMEOUT_MS)) != pdPASS) {
      std::puts("Input queue full; discarded");
    }
  }
}

}  // namespace

extern "C" void app_main(void) {
  setvbuf(stdin, nullptr, _IONBF, 0);
  setvbuf(stdout, nullptr, _IONBF, 0);

  ESP_LOGI(TAG, "SHT3x native ESP-IDF CLI");
  esp_err_t err = createBus(&gApp.i2c.bus);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "failed to initialize I2C bus: %s", esp_err_to_name(err));
    return;
  }

  err = addDevice(gApp.i2c.bus, &gApp.i2c.device);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "failed to add I2C device: %s", esp_err_to_name(err));
    i2c_del_master_bus(gApp.i2c.bus);
    return;
  }
  gApp.i2c.address = SHT3X_ADDR;

  configureDriver();
  ESP_LOGI(TAG, "I2C initialized SDA=%d SCL=%d addr=0x%02X",
           static_cast<int>(I2C_SDA), static_cast<int>(I2C_SCL), SHT3X_ADDR);
  scanBus();

  printStatus("begin", bindAndEnsureIdle());
  printSettings(false);
  printHelp();

  gApp.lineQueue = xQueueCreate(CLI_QUEUE_DEPTH, sizeof(CliLine));
  if (gApp.lineQueue == nullptr) {
    ESP_LOGE(TAG, "failed to create CLI queue");
    i2c_master_bus_rm_device(gApp.i2c.device);
    i2c_del_master_bus(gApp.i2c.bus);
    return;
  }
  (void)xTaskCreate(inputTask, "sht3x_cli_input", 4096, gApp.lineQueue, 5, nullptr);

  while (true) {
    CliLine line{};
    while (xQueueReceive(gApp.lineQueue, &line, 0) == pdTRUE) {
      handleCommandLine(line.text);
    }
    vTaskDelay(pdMS_TO_TICKS(CLI_TICK_MS));
  }
}
