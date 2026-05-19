#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include "IdfI2cTransport.h"
#include "SHT3x/SHT3x.h"
#include "Sht3xCli.h"

namespace {

constexpr i2c_port_num_t I2C_PORT = I2C_NUM_0;
constexpr gpio_num_t I2C_SDA = GPIO_NUM_8;
constexpr gpio_num_t I2C_SCL = GPIO_NUM_9;
constexpr uint32_t I2C_FREQ_HZ = 400000;
constexpr uint8_t SHT3X_ADDR = 0x44;
constexpr size_t CLI_LINE_LEN = 128U;
constexpr int CLI_QUEUE_DEPTH = 4;
constexpr uint32_t CLI_TICK_MS = 5;
constexpr int PROBE_TIMEOUT_MS = 50;

constexpr const char* COLOR_RESET = "\033[0m";
constexpr const char* COLOR_CYAN = "\033[36m";
constexpr const char* COLOR_YELLOW = "\033[33m";

struct AppContext {
  IdfI2cContext i2c = {};
  QueueHandle_t lineQueue = nullptr;
};

struct CliLine {
  char text[CLI_LINE_LEN] = {};
};

void idfVprintf(void*, const char* fmt, va_list args) {
  vprintf(fmt, args);
  fflush(stdout);
}

uint32_t nowMs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}

uint32_t nowUs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time());
}

void cooperativeYield(void*) {
  taskYIELD();
}

void printTagged(const char* color, const char* tag, const char* fmt, ...) {
  printf("%s[%s]%s ", color, tag, COLOR_RESET);
  va_list args;
  va_start(args, fmt);
  vprintf(fmt, args);
  va_end(args);
  printf("\n");
  fflush(stdout);
}

void scanBus(void* user) {
  AppContext* app = static_cast<AppContext*>(user);
  if (app == nullptr || app->i2c.bus == nullptr) {
    printTagged(COLOR_YELLOW, "W", "I2C scan unavailable");
    return;
  }

  printTagged(COLOR_CYAN, "I", "Scanning I2C bus...");
  int count = 0;
  for (uint8_t addr = 1; addr < 127; ++addr) {
    const esp_err_t err = i2c_master_probe(app->i2c.bus, addr, PROBE_TIMEOUT_MS);
    if (err == ESP_OK) {
      printf("  Found device at 0x%02X\n", static_cast<unsigned>(addr));
      count++;
    }
  }

  if (count == 0) {
    printTagged(COLOR_YELLOW, "W", "No I2C devices found");
  } else {
    printTagged(COLOR_CYAN, "I", "Found %d device(s)", count);
  }
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

void inputTask(void* arg) {
  QueueHandle_t queue = static_cast<QueueHandle_t>(arg);
  char buffer[CLI_LINE_LEN] = {};

  while (true) {
    if (fgets(buffer, sizeof(buffer), stdin) == nullptr) {
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

void configureCli(AppContext& app) {
  sht3x_cli::Platform cliPlatform{};
  cliPlatform.vprintf = idfVprintf;
  cliPlatform.nowMs = nowMs;
  cliPlatform.yield = cooperativeYield;
  cliPlatform.scanBus = scanBus;
  cliPlatform.user = &app;
  cliPlatform.buildDate = __DATE__;
  cliPlatform.buildTime = __TIME__;
  sht3x_cli::setPlatform(cliPlatform);
}

void configureDriver(AppContext& app) {
  SHT3x::Config& cfg = sht3x_cli::config();
  cfg.i2cAddress = SHT3X_ADDR;
  cfg.i2cWrite = idfI2cWrite;
  cfg.i2cWriteRead = idfI2cWriteRead;
  cfg.i2cUser = &app.i2c;
  cfg.nowMs = nowMs;
  cfg.nowUs = nowUs;
  cfg.cooperativeYield = cooperativeYield;
  cfg.i2cTimeoutMs = 50;
  cfg.mode = SHT3x::Mode::SINGLE_SHOT;
  cfg.clockStretching = SHT3x::ClockStretching::STRETCH_DISABLED;
  cfg.transportCapabilities = SHT3x::TransportCapability::TIMEOUT;
  cfg.offlineThreshold = 5;
  sht3x_cli::configReady() = true;
}

}  // namespace

extern "C" void app_main(void) {
  static AppContext app;

  sht3x_cli::logInfo("=== SHT3x Bringup Example ===");

  esp_err_t err = createBus(&app.i2c.bus);
  if (err != ESP_OK) {
    sht3x_cli::logError("Failed to initialize I2C bus: %s", esp_err_to_name(err));
    return;
  }

  err = addDevice(app.i2c.bus, &app.i2c.device);
  if (err != ESP_OK) {
    sht3x_cli::logError("Failed to add I2C device: %s", esp_err_to_name(err));
    i2c_del_master_bus(app.i2c.bus);
    return;
  }
  app.i2c.address = SHT3X_ADDR;

  configureCli(app);
  configureDriver(app);

  sht3x_cli::logInfo("I2C initialized (SDA=%d, SCL=%d)",
                     static_cast<int>(I2C_SDA),
                     static_cast<int>(I2C_SCL));
  scanBus(&app);

  SHT3x::Status st = sht3x_cli::device().begin(sht3x_cli::config());
  if (!st.ok()) {
    sht3x_cli::logError("Failed to initialize device: code=%u detail=%ld msg=%s",
                        static_cast<unsigned>(st.code),
                        static_cast<long>(st.detail),
                        st.msg ? st.msg : "");
    i2c_master_bus_rm_device(app.i2c.device);
    i2c_del_master_bus(app.i2c.bus);
    return;
  }

  app.lineQueue = xQueueCreate(CLI_QUEUE_DEPTH, sizeof(CliLine));
  if (app.lineQueue == nullptr) {
    sht3x_cli::logError("Failed to create CLI input queue");
    i2c_master_bus_rm_device(app.i2c.device);
    i2c_del_master_bus(app.i2c.bus);
    return;
  }

  (void)xTaskCreate(inputTask, "sht3x_cli_input", 4096, app.lineQueue, 5, nullptr);

  sht3x_cli::logInfo("Device initialized successfully");
  sht3x_cli::printDriverHealth();
  sht3x_cli::printHelp();
  sht3x_cli::printPrompt();

  while (true) {
    sht3x_cli::tick();

    CliLine line{};
    while (xQueueReceive(app.lineQueue, &line, 0) == pdTRUE) {
      sht3x_cli::processCommand(line.text);
      sht3x_cli::printPrompt();
    }

    vTaskDelay(pdMS_TO_TICKS(CLI_TICK_MS));
  }
}
