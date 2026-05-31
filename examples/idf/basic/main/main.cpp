#include <cctype>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

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
constexpr int PROBE_TIMEOUT_MS = 50;

struct AppContext {
  IdfI2cContext i2c = {};
  QueueHandle_t lineQueue = nullptr;
};

struct CliLine {
  char text[LINE_LEN] = {};
};

AppContext gApp;
SHT3x::SHT3x gDevice;
SHT3x::Config gConfig;
bool gVerbose = false;

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
  char* end = nullptr;
  const unsigned long parsed = std::strtoul(text, &end, 0);
  if (end == text) {
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
  const float parsed = std::strtof(text, &end);
  if (end == text) {
    return false;
  }
  *value = parsed;
  return true;
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
  for (uint8_t addr = 1; addr < 127; ++addr) {
    if (i2c_master_probe(gApp.i2c.bus, addr, PROBE_TIMEOUT_MS) == ESP_OK) {
      std::printf("found 0x%02X%s\n", addr, addr == SHT3X_ADDR ? " (configured)" : "");
      ++count;
    }
  }
  std::printf("scan: %d device(s)\n", count);
}

void printHelp() {
  std::puts("Commands:");
  std::puts("  help / ? | version / ver | scan");
  std::puts("  begin | end | probe | recover | drv | cfg / settings | state | online");
  std::puts("  read | request | fetch | raw | comp | meastime");
  std::puts("  mode [single|periodic|art] | repeat [low|med|high] | rate [0.5|1|2|4|10] | stretch [0|1]");
  std::puts("  start_periodic <rate> <rep> | start_art | stop_periodic");
  std::puts("  status | status_raw | clearstatus | heater [on|off|status] | serial [stretch|nostretch]");
  std::puts("  command write <hex> | command write_data <cmd> <data> | command read <cmd> <len>");
  std::puts("  alert read <hs|hc|lc|ls> | alert write <kind> <T> <RH>");
  std::puts("  alert raw read <kind> | alert raw write <kind> <hex> | alert encode <T> <RH> | alert decode <hex> | alert disable");
  std::puts("  convert <rawT> <rawRH> | reset | defaults | restore | iface_reset | greset");
  std::puts("  verbose [0|1] | stress [N] | stress_mix [N] | selftest");
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
  if (snap.statusValid) {
    std::printf("status=0x%04X alert=%d heater=%d rh_alert=%d t_alert=%d reset=%d cmd_err=%d crc_err=%d\n",
                snap.status.raw, snap.status.alertPending ? 1 : 0,
                snap.status.heaterOn ? 1 : 0, snap.status.rhAlert ? 1 : 0,
                snap.status.tAlert ? 1 : 0, snap.status.resetDetected ? 1 : 0,
                snap.status.commandError ? 1 : 0, snap.status.writeCrcError ? 1 : 0);
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

void requestAndWait() {
  SHT3x::Status st = gDevice.requestMeasurement();
  printStatus("request", st);
  if (!(st.ok() || st.inProgress())) {
    return;
  }
  const uint32_t deadline = nowMs(nullptr) + gDevice.estimateMeasurementTimeMs() + 50U;
  while (!gDevice.measurementReady() &&
         static_cast<int32_t>(nowMs(nullptr) - deadline) < 0) {
    gDevice.tick(nowMs(nullptr));
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  if (!gDevice.measurementReady()) {
    printStatus("measurement", SHT3x::Status::Error(SHT3x::Err::TIMEOUT,
                                                    "Timed out waiting for sample"));
    return;
  }
  printSample();
}

void runStress(uint32_t count) {
  uint32_t ok = 0;
  uint32_t fail = 0;
  for (uint32_t i = 0; i < count; ++i) {
    SHT3x::Status st = gDevice.requestMeasurement();
    if (!(st.ok() || st.inProgress())) {
      ++fail;
      continue;
    }
    const uint32_t deadline = nowMs(nullptr) + gDevice.estimateMeasurementTimeMs() + 50U;
    while (!gDevice.measurementReady() &&
           static_cast<int32_t>(nowMs(nullptr) - deadline) < 0) {
      gDevice.tick(nowMs(nullptr));
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    SHT3x::Measurement measurement{};
    st = gDevice.getMeasurement(measurement);
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

void handleCommandLine(char* line) {
  char* cmd = nullptr;
  char* args = nullptr;
  splitCommand(line, &cmd, &args);
  if (cmd == nullptr || *cmd == '\0') {
    return;
  }

  if (std::strcmp(cmd, "help") == 0 || std::strcmp(cmd, "?") == 0) {
    printHelp();
  } else if (std::strcmp(cmd, "version") == 0 || std::strcmp(cmd, "ver") == 0) {
    std::printf("SHT3x library %s (%s)\n", SHT3x::VERSION, SHT3x::BUILD_TIMESTAMP);
  } else if (std::strcmp(cmd, "scan") == 0) {
    scanBus();
  } else if (std::strcmp(cmd, "begin") == 0) {
    printStatus("begin", gDevice.begin(gConfig));
  } else if (std::strcmp(cmd, "end") == 0) {
    gDevice.end();
    std::puts("end: OK");
  } else if (std::strcmp(cmd, "probe") == 0) {
    printStatus("probe", gDevice.probe());
  } else if (std::strcmp(cmd, "recover") == 0) {
    printStatus("recover", gDevice.recover());
  } else if (std::strcmp(cmd, "drv") == 0 || std::strcmp(cmd, "cfg") == 0 ||
             std::strcmp(cmd, "settings") == 0) {
    printSettings(std::strcmp(cmd, "settings") == 0);
  } else if (std::strcmp(cmd, "state") == 0 || std::strcmp(cmd, "online") == 0) {
    std::printf("state=%s online=%d initialized=%d\n", stateToStr(gDevice.state()),
                gDevice.isOnline() ? 1 : 0, gDevice.isInitialized() ? 1 : 0);
  } else if (std::strcmp(cmd, "read") == 0) {
    requestAndWait();
  } else if (std::strcmp(cmd, "request") == 0) {
    printStatus("request", gDevice.requestMeasurement());
  } else if (std::strcmp(cmd, "fetch") == 0) {
    gDevice.tick(nowMs(nullptr));
    printSample();
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
      printStatus("stretch", gDevice.setClockStretching(
                                 std::strcmp(args, "1") == 0
                                     ? SHT3x::ClockStretching::STRETCH_ENABLED
                                     : SHT3x::ClockStretching::STRETCH_DISABLED));
    }
  } else if (std::strcmp(cmd, "start_periodic") == 0) {
    char* rateToken = args;
    char* repToken = std::strchr(args, ' ');
    if (repToken != nullptr) {
      *repToken++ = '\0';
      repToken = trim(repToken);
    }
    SHT3x::PeriodicRate rate{};
    SHT3x::Repeatability rep{};
    printStatus("start_periodic",
                (repToken != nullptr && parseRate(rateToken, &rate) &&
                 parseRepeatability(repToken, &rep))
                    ? gDevice.startPeriodic(rate, rep)
                    : SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM, "usage: start_periodic <rate> <rep>"));
  } else if (std::strcmp(cmd, "start_art") == 0) {
    printStatus("start_art", gDevice.startArt());
  } else if (std::strcmp(cmd, "stop_periodic") == 0) {
    printStatus("stop_periodic", gDevice.stopPeriodic());
  } else if (std::strcmp(cmd, "status") == 0) {
    SHT3x::StatusRegister reg{};
    SHT3x::Status st = gDevice.readStatus(reg);
    printStatus("status", st);
    if (st.ok()) {
      std::printf("raw=0x%04X alert=%d heater=%d rh_alert=%d t_alert=%d reset=%d cmd_err=%d crc_err=%d\n",
                  reg.raw, reg.alertPending ? 1 : 0, reg.heaterOn ? 1 : 0,
                  reg.rhAlert ? 1 : 0, reg.tAlert ? 1 : 0, reg.resetDetected ? 1 : 0,
                  reg.commandError ? 1 : 0, reg.writeCrcError ? 1 : 0);
    }
  } else if (std::strcmp(cmd, "status_raw") == 0) {
    uint16_t raw = 0;
    SHT3x::Status st = gDevice.readStatus(raw);
    printStatus("status_raw", st);
    if (st.ok()) {
      std::printf("status=0x%04X\n", raw);
    }
  } else if (std::strcmp(cmd, "clearstatus") == 0) {
    printStatus("clearstatus", gDevice.clearStatus());
  } else if (std::strcmp(cmd, "heater") == 0) {
    if (*args == '\0' || std::strcmp(args, "status") == 0) {
      bool enabled = false;
      SHT3x::Status st = gDevice.readHeaterStatus(enabled);
      printStatus("heater", st);
      if (st.ok()) {
        std::printf("heater=%d\n", enabled ? 1 : 0);
      }
    } else if (std::strcmp(args, "on") == 0 || std::strcmp(args, "1") == 0) {
      printStatus("heater", gDevice.setHeater(true));
    } else if (std::strcmp(args, "off") == 0 || std::strcmp(args, "0") == 0) {
      printStatus("heater", gDevice.setHeater(false));
    } else {
      std::puts("Usage: heater [on|off|status]");
    }
  } else if (std::strcmp(cmd, "serial") == 0) {
    uint32_t serial = 0;
    const bool stretch = std::strcmp(args, "stretch") == 0;
    SHT3x::Status st = gDevice.readSerialNumber(
        serial, stretch ? SHT3x::ClockStretching::STRETCH_ENABLED
                        : SHT3x::ClockStretching::STRETCH_DISABLED);
    printStatus("serial", st);
    if (st.ok()) {
      std::printf("serial=0x%08lX\n", static_cast<unsigned long>(serial));
    }
  } else if (std::strcmp(cmd, "reset") == 0) {
    printStatus("reset", gDevice.softReset());
  } else if (std::strcmp(cmd, "defaults") == 0) {
    printStatus("defaults", gDevice.resetToDefaults());
  } else if (std::strcmp(cmd, "restore") == 0) {
    printStatus("restore", gDevice.resetAndRestore());
  } else if (std::strcmp(cmd, "iface_reset") == 0) {
    printStatus("iface_reset", gDevice.interfaceReset());
  } else if (std::strcmp(cmd, "greset") == 0) {
    printStatus("greset", gDevice.generalCallReset());
  } else if (std::strcmp(cmd, "command") == 0) {
    char* sub = nullptr;
    char* tail = nullptr;
    splitCommand(args, &sub, &tail);
    if (sub != nullptr && std::strcmp(sub, "write") == 0) {
      uint32_t command = 0;
      printStatus("command write", parseU32(tail, &command)
                                       ? gDevice.writeCommand(static_cast<uint16_t>(command))
                                       : SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM, "bad command"));
    } else if (sub != nullptr && std::strcmp(sub, "write_data") == 0) {
      char* dataToken = std::strchr(tail, ' ');
      uint32_t command = 0;
      uint32_t data = 0;
      if (dataToken != nullptr) {
        *dataToken++ = '\0';
        dataToken = trim(dataToken);
      }
      printStatus("command write_data",
                  (dataToken != nullptr && parseU32(tail, &command) && parseU32(dataToken, &data))
                      ? gDevice.writeCommandWithData(static_cast<uint16_t>(command),
                                                     static_cast<uint16_t>(data))
                      : SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM, "usage: command write_data <cmd> <data>"));
    } else if (sub != nullptr && std::strcmp(sub, "read") == 0) {
      char* lenToken = std::strchr(tail, ' ');
      uint32_t command = 0;
      uint32_t len = 0;
      if (lenToken != nullptr) {
        *lenToken++ = '\0';
        lenToken = trim(lenToken);
      }
      uint8_t buf[6] = {};
      SHT3x::Status st =
          (lenToken != nullptr && parseU32(tail, &command) && parseU32(lenToken, &len) &&
           len <= sizeof(buf))
              ? gDevice.readCommand(static_cast<uint16_t>(command), buf, len)
              : SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM, "usage: command read <cmd> <len<=6>");
      printStatus("command read", st);
      if (st.ok()) {
        for (uint32_t i = 0; i < len; ++i) {
          std::printf("%02X ", buf[i]);
        }
        std::putchar('\n');
      }
    } else {
      std::puts("Usage: command write|write_data|read ...");
    }
  } else if (std::strcmp(cmd, "alert") == 0) {
    char* sub = nullptr;
    char* tail = nullptr;
    splitCommand(args, &sub, &tail);
    if (sub != nullptr && std::strcmp(sub, "disable") == 0) {
      printStatus("alert disable", gDevice.disableAlerts());
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
      if (parseU32(tail, &raw)) {
        SHT3x::SHT3x::decodeAlertLimit(static_cast<uint16_t>(raw), temp, rh);
        std::printf("temperature=%.2f humidity=%.2f\n", temp, rh);
      } else {
        std::puts("Usage: alert decode <hex>");
      }
    } else if (sub != nullptr && std::strcmp(sub, "raw") == 0) {
      char* op = nullptr;
      char* rest = nullptr;
      splitCommand(tail, &op, &rest);
      char* valueToken = std::strchr(rest, ' ');
      if (valueToken != nullptr) {
        *valueToken++ = '\0';
        valueToken = trim(valueToken);
      }
      SHT3x::AlertLimitKind kind{};
      if (op != nullptr && std::strcmp(op, "read") == 0 && parseAlertKind(rest, &kind)) {
        uint16_t value = 0;
        SHT3x::Status st = gDevice.readAlertLimitRaw(kind, value);
        printStatus("alert raw read", st);
        if (st.ok()) {
          std::printf("raw=0x%04X\n", value);
        }
      } else if (op != nullptr && std::strcmp(op, "write") == 0 &&
                 valueToken != nullptr && parseAlertKind(rest, &kind)) {
        uint32_t value = 0;
        printStatus("alert raw write", parseU32(valueToken, &value)
                                           ? gDevice.writeAlertLimitRaw(kind, static_cast<uint16_t>(value))
                                           : SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM, "bad value"));
      } else {
        std::puts("Usage: alert raw read|write <kind> [hex]");
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
    } else if (sub != nullptr && std::strcmp(sub, "write") == 0) {
      char* tempToken = std::strchr(tail, ' ');
      SHT3x::AlertLimitKind kind{};
      if (tempToken != nullptr) {
        *tempToken++ = '\0';
        tempToken = trim(tempToken);
      }
      char* rhToken = tempToken != nullptr ? std::strchr(tempToken, ' ') : nullptr;
      if (rhToken != nullptr) {
        *rhToken++ = '\0';
        rhToken = trim(rhToken);
      }
      float temp = 0.0f;
      float rh = 0.0f;
      printStatus("alert write",
                  (tempToken != nullptr && rhToken != nullptr && parseAlertKind(tail, &kind) &&
                   parseFloat(tempToken, &temp) && parseFloat(rhToken, &rh))
                      ? gDevice.writeAlertLimit(kind, temp, rh)
                      : SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM, "usage: alert write <kind> <T> <RH>"));
    } else {
      std::puts("Usage: alert read|write|raw|encode|decode|disable ...");
    }
  } else if (std::strcmp(cmd, "convert") == 0) {
    char* rhToken = std::strchr(args, ' ');
    uint32_t rawT = 0;
    uint32_t rawRh = 0;
    if (rhToken != nullptr) {
      *rhToken++ = '\0';
      rhToken = trim(rhToken);
    }
    if (rhToken != nullptr && parseU32(args, &rawT) && parseU32(rhToken, &rawRh)) {
      std::printf("temperature=%.2f humidity=%.2f tempC_x100=%ld humidity_x100=%lu\n",
                  SHT3x::SHT3x::convertTemperatureC(static_cast<uint16_t>(rawT)),
                  SHT3x::SHT3x::convertHumidityPct(static_cast<uint16_t>(rawRh)),
                  static_cast<long>(SHT3x::SHT3x::convertTemperatureC_x100(static_cast<uint16_t>(rawT))),
                  static_cast<unsigned long>(SHT3x::SHT3x::convertHumidityPct_x100(static_cast<uint16_t>(rawRh))));
    } else {
      std::puts("Usage: convert <rawT> <rawRH>");
    }
  } else if (std::strcmp(cmd, "verbose") == 0) {
    if (*args != '\0') {
      gVerbose = std::strcmp(args, "0") != 0;
    } else {
      gVerbose = !gVerbose;
    }
    std::printf("verbose=%d\n", gVerbose ? 1 : 0);
  } else if (std::strcmp(cmd, "stress") == 0 || std::strcmp(cmd, "stress_mix") == 0) {
    uint32_t count = 10;
    (void)parseU32(args, &count);
    runStress(count);
  } else if (std::strcmp(cmd, "selftest") == 0) {
    runSelftest();
  } else {
    std::puts("Unknown command. Try 'help'.");
  }
}

void inputTask(void* arg) {
  QueueHandle_t queue = static_cast<QueueHandle_t>(arg);
  char buffer[LINE_LEN] = {};
  while (true) {
    if (std::fgets(buffer, sizeof(buffer), stdin) == nullptr) {
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    size_t len = std::strlen(buffer);
    while (len > 0U && (buffer[len - 1U] == '\n' || buffer[len - 1U] == '\r')) {
      buffer[--len] = '\0';
    }
    if (len == 0U) {
      continue;
    }
    CliLine line{};
    std::strncpy(line.text, buffer, sizeof(line.text) - 1U);
    (void)xQueueSend(queue, &line, portMAX_DELAY);
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

  printStatus("begin", gDevice.begin(gConfig));
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
    gDevice.tick(nowMs(nullptr));
    CliLine line{};
    while (xQueueReceive(gApp.lineQueue, &line, 0) == pdTRUE) {
      handleCommandLine(line.text);
    }
    vTaskDelay(pdMS_TO_TICKS(CLI_TICK_MS));
  }
}
