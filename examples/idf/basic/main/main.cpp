/// @file main.cpp
/// @brief Native ESP-IDF diagnostic bringup CLI example for SHT3x.
/// @note This is example/application glue, not part of the library API.

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>

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

// This diagnostic example owns one I2C bus/device handle. The driver and shared
// CLI are called only by app_main; production multi-task/shared-bus use must
// serialize driver access externally.
#include "IdfI2cTransport.h"
#include "Sht3xCli.h"

namespace {

constexpr const char* TAG = "sht3x_cli";
constexpr i2c_port_num_t I2C_PORT = I2C_NUM_0;
constexpr gpio_num_t I2C_SDA = GPIO_NUM_8;
constexpr gpio_num_t I2C_SCL = GPIO_NUM_9;
constexpr uint32_t I2C_FREQ_HZ = 400000U;
constexpr uint8_t SHT3X_ADDR = 0x44U;
constexpr size_t LINE_LEN = 128U;
constexpr size_t INPUT_CHUNK_LEN = 32U;
constexpr int CLI_QUEUE_DEPTH = 4;
constexpr uint32_t CLI_TICK_MS = 5U;
constexpr uint32_t CLI_INPUT_RETRY_MS = 20U;
constexpr uint32_t CLI_QUEUE_SEND_TIMEOUT_MS = 50U;
constexpr int PROBE_TIMEOUT_MS = 50;

struct CliLine {
  char text[LINE_LEN] = {};
};

struct AppContext {
  IdfI2cContext i2c{};
  QueueHandle_t lineQueue = nullptr;
};

AppContext gApp;

void idfVprintf(void*, const char* format, va_list args) {
  (void)std::vprintf(format, args);
}

uint32_t idfNowMs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}

uint32_t idfNowUs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time());
}

void idfYield(void*) {
  taskYIELD();
}

const char* addressLabel(uint8_t address) {
  if (address == 0x44U || address == 0x45U) {
    return "0x44/0x45=SHT3x";
  }
  if (address == 0x76U || address == 0x77U) {
    return "0x76/0x77=BME280/BMP280";
  }
  return "";
}

void idfScanBus(void* user) {
  AppContext* app = static_cast<AppContext*>(user);
  if (app == nullptr || app->i2c.bus == nullptr) {
    std::puts("I2C scan unavailable");
    return;
  }

  std::puts("Scanning I2C bus...");
  int count = 0;
  for (uint8_t address = 0x08U; address <= 0x77U; ++address) {
    if (i2c_master_probe(app->i2c.bus, address, PROBE_TIMEOUT_MS) != ESP_OK) {
      continue;
    }
    const char* label = addressLabel(address);
    if (label[0] != '\0') {
      std::printf("  Found device at 0x%02X (%s)\n", address, label);
    } else {
      std::printf("  Found device at 0x%02X\n", address);
    }
    ++count;
  }

  if (count == 0) {
    std::puts("No I2C devices found");
  } else {
    std::printf("Scan complete. Found %d device(s).\n", count);
  }
  std::puts("Known: 0x44/0x45=SHT3x, 0x76/0x77=BME280/BMP280");
}

sht3x_cli::TransferStats idfTransferStats(void* user) {
  AppContext* app = static_cast<AppContext*>(user);
  return app != nullptr ? app->i2c.transferStats : sht3x_cli::TransferStats{};
}

void idfResetTransferStats(void* user) {
  AppContext* app = static_cast<AppContext*>(user);
  if (app != nullptr) {
    app->i2c.transferStats = {};
  }
}

esp_err_t createBus(i2c_master_bus_handle_t* bus) {
  i2c_master_bus_config_t busConfig{};
  busConfig.i2c_port = I2C_PORT;
  busConfig.sda_io_num = I2C_SDA;
  busConfig.scl_io_num = I2C_SCL;
  busConfig.clk_source = I2C_CLK_SRC_DEFAULT;
  busConfig.glitch_ignore_cnt = 7;
  // Internal pull-ups make bench bringup more forgiving. For reliable 400 kHz
  // operation, fit external pull-ups sized for the bus voltage/capacitance.
  busConfig.flags.enable_internal_pullup = true;
  return i2c_new_master_bus(&busConfig, bus);
}

esp_err_t addDevice(i2c_master_bus_handle_t bus,
                    i2c_master_dev_handle_t* device) {
  i2c_device_config_t deviceConfig{};
  deviceConfig.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  deviceConfig.device_address = SHT3X_ADDR;
  deviceConfig.scl_speed_hz = I2C_FREQ_HZ;
  return i2c_master_bus_add_device(bus, &deviceConfig, device);
}

void configureCli() {
  sht3x_cli::Platform platform{};
  platform.vprintf = idfVprintf;
  platform.nowMs = idfNowMs;
  platform.yield = idfYield;
  platform.scanBus = idfScanBus;
  platform.getTransferStats = idfTransferStats;
  platform.resetTransferStats = idfResetTransferStats;
  platform.user = &gApp;
  platform.framework = "native-esp-idf";
  platform.arduinoCoreVersion = "not-applicable";
  platform.espIdfVersion = esp_get_idf_version();
  platform.buildTarget = CONFIG_IDF_TARGET;
  platform.buildDate = __DATE__;
  platform.buildTime = __TIME__;
  sht3x_cli::setPlatform(platform);

  SHT3x::Config& config = sht3x_cli::config();
  config = {};
  config.i2cAddress = SHT3X_ADDR;
  config.i2cWrite = idfI2cWrite;
  config.i2cWriteRead = idfI2cWriteRead;
  config.i2cUser = &gApp.i2c;
  config.nowMs = idfNowMs;
  config.nowUs = idfNowUs;
  config.cooperativeYield = idfYield;
  config.i2cTimeoutMs = 50U;
  config.mode = SHT3x::Mode::SINGLE_SHOT;
  config.clockStretching = SHT3x::ClockStretching::STRETCH_DISABLED;
  // mapEspError() distinguishes ESP_ERR_TIMEOUT exactly; every other IDF error
  // is reported as the generic Err::I2C_ERROR, so this adapter can advertise
  // TIMEOUT and nothing else.
  config.transportCapabilities = SHT3x::TransportCapability::TIMEOUT;
  config.offlineThreshold = 5U;
  sht3x_cli::configReady() = true;
}

bool queueLine(QueueHandle_t queue, const char* text) {
  CliLine line{};
  size_t index = 0U;
  while (index < (sizeof(line.text) - 1U) && text[index] != '\0') {
    line.text[index] = text[index];
    ++index;
  }
  line.text[index] = '\0';
  return xQueueSend(queue, &line,
                    pdMS_TO_TICKS(CLI_QUEUE_SEND_TIMEOUT_MS)) == pdPASS;
}

void inputTask(void* arg) {
  QueueHandle_t queue = static_cast<QueueHandle_t>(arg);
  char chunk[INPUT_CHUNK_LEN]{};
  char line[LINE_LEN]{};
  size_t lineLength = 0U;
  bool discardingOverflow = false;

  while (true) {
    if (std::fgets(chunk, sizeof(chunk), stdin) == nullptr) {
      // Some IDF VFS configurations expose UART stdin as nonblocking. Clear the
      // sticky stdio EOF/error state so a later partial read is accepted.
      std::clearerr(stdin);
      vTaskDelay(pdMS_TO_TICKS(CLI_INPUT_RETRY_MS));
      continue;
    }

    for (size_t i = 0U; chunk[i] != '\0'; ++i) {
      const char c = chunk[i];
      if (c == '\b' || c == 0x7F) {
        if (!discardingOverflow && lineLength > 0U) {
          line[--lineLength] = '\0';
        }
        continue;
      }

      if (c == '\n' || c == '\r') {
        if (discardingOverflow) {
          sht3x_cli::logWarn(
              "Input line too long (maximum %u characters); command discarded",
              static_cast<unsigned>(sizeof(line) - 1U));
          discardingOverflow = false;
          lineLength = 0U;
          line[0] = '\0';
        } else if (lineLength > 0U) {
          line[lineLength] = '\0';
          if (!queueLine(queue, line)) {
            sht3x_cli::logWarn("Input queue full; command discarded");
          }
          lineLength = 0U;
          line[0] = '\0';
        }
        continue;
      }

      if (discardingOverflow) {
        continue;
      }
      if (lineLength < (sizeof(line) - 1U)) {
        line[lineLength++] = c;
        line[lineLength] = '\0';
      } else {
        lineLength = 0U;
        line[0] = '\0';
        discardingOverflow = true;
      }
    }
  }
}

void releaseI2c() {
  if (gApp.i2c.device != nullptr) {
    (void)i2c_master_bus_rm_device(gApp.i2c.device);
    gApp.i2c.device = nullptr;
  }
  if (gApp.i2c.bus != nullptr) {
    (void)i2c_del_master_bus(gApp.i2c.bus);
    gApp.i2c.bus = nullptr;
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
    releaseI2c();
    return;
  }
  gApp.i2c.address = SHT3X_ADDR;
  configureCli();

  sht3x_cli::logInfo("I2C initialized (SDA=%d, SCL=%d, address=0x%02X)",
                     static_cast<int>(I2C_SDA), static_cast<int>(I2C_SCL),
                     SHT3X_ADDR);
  sht3x_cli::logWarn(
      "Use external I2C pull-ups sized for reliable 400 kHz operation; "
      "internal pull-ups are enabled only for diagnostic bringup");
  idfScanBus(&gApp);

  const SHT3x::Status status = sht3x_cli::beginOwnerSafe();
  if (status.code != SHT3x::Err::IN_PROGRESS) {
    sht3x_cli::logError(
        "Failed to initialize device: code=%u detail=%ld msg=%s",
        static_cast<unsigned>(status.code), static_cast<long>(status.detail),
        status.msg != nullptr ? status.msg : "");
    releaseI2c();
    return;
  }
  sht3x_cli::logInfo(
      "Device bound; owner-safe ensure-idle reconciliation scheduled");
  sht3x_cli::printDriverHealth();
  sht3x_cli::printHelp();
  sht3x_cli::printPrompt();

  gApp.lineQueue = xQueueCreate(CLI_QUEUE_DEPTH, sizeof(CliLine));
  if (gApp.lineQueue == nullptr) {
    ESP_LOGE(TAG, "failed to create CLI queue");
    releaseI2c();
    return;
  }
  if (xTaskCreate(inputTask, "sht3x_cli_input", 4096, gApp.lineQueue, 5,
                  nullptr) != pdPASS) {
    ESP_LOGE(TAG, "failed to create CLI input task");
    vQueueDelete(gApp.lineQueue);
    gApp.lineQueue = nullptr;
    releaseI2c();
    return;
  }

  while (true) {
    CliLine line{};
    while (xQueueReceive(gApp.lineQueue, &line, 0) == pdTRUE) {
      sht3x_cli::processCommand(line.text);
      sht3x_cli::printPrompt();
    }
    sht3x_cli::tick();
    vTaskDelay(pdMS_TO_TICKS(CLI_TICK_MS));
  }
}
