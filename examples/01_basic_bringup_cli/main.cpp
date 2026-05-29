/// @file main.cpp
/// @brief Arduino bringup CLI example for SHT3x.
/// @note This is an EXAMPLE, not part of the library.

#include <Arduino.h>

#include "common/BoardConfig.h"
#include "common/BusDiag.h"
#include "common/I2cTransport.h"
#include "common/Sht3xCli.h"

namespace {

constexpr size_t INPUT_BUFFER_LEN = 128U;

void arduinoVprintf(void*, const char* fmt, va_list args) {
  char buffer[192];
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  Serial.print(buffer);
}

uint32_t arduinoNowMs(void*) {
  return millis();
}

uint32_t arduinoNowUs(void*) {
  return micros();
}

void arduinoYield(void*) {
  yield();
}

void arduinoScanBus(void*) {
  bus_diag::scan();
}

void readSerialInput() {
  static char input[INPUT_BUFFER_LEN] = {};
  static size_t len = 0;
  static bool overflow = false;

  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\b' || c == 0x7F) {
      if (!overflow && len > 0U) {
        --len;
        input[len] = '\0';
      }
      continue;
    }

    if (c == '\n' || c == '\r') {
      if (overflow) {
        len = 0;
        input[0] = '\0';
        overflow = false;
        continue;
      }
      if (len > 0U) {
        input[len] = '\0';
        sht3x_cli::processCommand(input);
        len = 0;
        input[0] = '\0';
        sht3x_cli::printPrompt();
      }
      continue;
    }

    if (overflow) {
      continue;
    }

    if (len < (sizeof(input) - 1U)) {
      input[len++] = c;
      input[len] = '\0';
    } else {
      len = 0;
      input[0] = '\0';
      overflow = true;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);

  sht3x_cli::Platform cliPlatform{};
  cliPlatform.vprintf = arduinoVprintf;
  cliPlatform.nowMs = arduinoNowMs;
  cliPlatform.yield = arduinoYield;
  cliPlatform.scanBus = arduinoScanBus;
  cliPlatform.buildDate = __DATE__;
  cliPlatform.buildTime = __TIME__;
  sht3x_cli::setPlatform(cliPlatform);

  sht3x_cli::logInfo("=== SHT3x Bringup Example ===");

  if (!board::initI2c()) {
    sht3x_cli::logError("Failed to initialize I2C");
    return;
  }
  sht3x_cli::logInfo("I2C initialized (SDA=%d, SCL=%d)", board::I2C_SDA, board::I2C_SCL);

  bus_diag::scan();

  SHT3x::Config& cfg = sht3x_cli::config();
  cfg.i2cWrite = transport::wireWrite;
  cfg.i2cWriteRead = transport::wireWriteRead;
  cfg.i2cUser = &Wire;
  cfg.nowMs = arduinoNowMs;
  cfg.nowUs = arduinoNowUs;
  cfg.cooperativeYield = arduinoYield;
  cfg.i2cAddress = 0x44;
  cfg.i2cTimeoutMs = board::I2C_TIMEOUT_MS;
  cfg.transportCapabilities = SHT3x::TransportCapability::NONE;
  cfg.offlineThreshold = 5;
  sht3x_cli::configReady() = true;

  SHT3x::Status st = sht3x_cli::device().begin(cfg);
  if (!st.ok()) {
    sht3x_cli::logError("Failed to initialize device: code=%u detail=%ld msg=%s",
                        static_cast<unsigned>(st.code),
                        static_cast<long>(st.detail),
                        st.msg ? st.msg : "");
    return;
  }

  sht3x_cli::logInfo("Device initialized successfully");
  sht3x_cli::printDriverHealth();
  sht3x_cli::printHelp();
  sht3x_cli::printPrompt();
}

void loop() {
  sht3x_cli::tick();
  readSerialInput();
}
