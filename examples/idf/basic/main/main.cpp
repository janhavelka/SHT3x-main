#include <cstdint>

#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "IdfI2cTransport.h"
#include "SHT3x/SHT3x.h"

namespace {

constexpr char TAG[] = "sht3x_basic";
constexpr i2c_port_num_t I2C_PORT = I2C_NUM_0;
constexpr gpio_num_t I2C_SDA = GPIO_NUM_8;
constexpr gpio_num_t I2C_SCL = GPIO_NUM_9;
constexpr uint32_t I2C_FREQ_HZ = 400000;
constexpr uint8_t SHT3X_ADDR = 0x44;

uint32_t nowMs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
}

uint32_t nowUs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time());
}

void cooperativeYield(void*) {
  taskYIELD();
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

}  // namespace

extern "C" void app_main(void) {
  i2c_master_bus_handle_t bus = nullptr;
  i2c_master_dev_handle_t dev = nullptr;

  ESP_ERROR_CHECK(createBus(&bus));
  ESP_ERROR_CHECK(addDevice(bus, &dev));

  IdfI2cContext i2cCtx{};
  i2cCtx.device = dev;
  i2cCtx.address = SHT3X_ADDR;

  SHT3x::Config cfg{};
  cfg.i2cAddress = SHT3X_ADDR;
  cfg.i2cWrite = idfI2cWrite;
  cfg.i2cWriteRead = idfI2cWriteRead;
  cfg.i2cUser = &i2cCtx;
  cfg.nowMs = nowMs;
  cfg.nowUs = nowUs;
  cfg.cooperativeYield = cooperativeYield;
  cfg.i2cTimeoutMs = 50;
  cfg.mode = SHT3x::Mode::SINGLE_SHOT;
  cfg.clockStretching = SHT3x::ClockStretching::STRETCH_DISABLED;
  cfg.transportCapabilities = SHT3x::TransportCapability::TIMEOUT;

  SHT3x::SHT3x sensor;
  SHT3x::Status st = sensor.begin(cfg);
  if (!st.ok()) {
    ESP_LOGE(TAG, "begin failed: code=%u detail=%ld msg=%s",
             static_cast<unsigned>(st.code), static_cast<long>(st.detail), st.msg);
    i2c_master_bus_rm_device(dev);
    i2c_del_master_bus(bus);
    return;
  }

  while (true) {
    st = sensor.requestMeasurement();
    if (!st.ok() && !st.inProgress()) {
      ESP_LOGW(TAG, "request failed: code=%u msg=%s",
               static_cast<unsigned>(st.code), st.msg);
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    while (!sensor.measurementReady()) {
      sensor.tick(nowMs(nullptr));
      vTaskDelay(pdMS_TO_TICKS(5));
    }

    SHT3x::Measurement m{};
    st = sensor.getMeasurement(m);
    if (st.ok()) {
      ESP_LOGI(TAG, "temperature=%.2f C humidity=%.2f %%",
               static_cast<double>(m.temperatureC), static_cast<double>(m.humidityPct));
    } else {
      ESP_LOGW(TAG, "read failed: code=%u msg=%s", static_cast<unsigned>(st.code), st.msg);
    }

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}
