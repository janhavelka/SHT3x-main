# SHT3x — ESP-IDF Migration Prompt

> **Library**: SHT3x (Sensirion temperature / humidity sensor, one-shot mode)
> **Current version**: 1.3.1 → **Target**: 2.0.0
> **Namespace**: `SHT3x`
> **Include path**: `#include "SHT3x/SHT3x.h"`
> **Difficulty**: Easy — `millis()`/`micros()`/`yield()` replacement in .cpp only

---

## Pre-Migration

```bash
git tag v1.3.1   # freeze Arduino-era version
```

---

## Current State — Arduino Dependencies (exact)

| API | Count | Locations |
|-----|-------|-----------|
| `#include <Arduino.h>` | 1 | `.cpp` only (not in header) |
| `millis()` | 10 | Throughout .cpp |
| `micros()` | 5 | Throughout .cpp |
| `yield()` | 2 | Lines 1520, 1547 |

I2C already abstracted via injected callbacks. Extra callbacks:

```cpp
using BusResetFn   = Status (*)(void* user);
using HardResetFn  = Status (*)(void* user);
```

Also uses `TransportCapability` enum for feature negotiation. Config is struct-based, time via `tick(uint32_t nowMs)`.

---

## Steps

### 1. Remove `#include <Arduino.h>`

### 2. Add timing helpers at file scope in .cpp

```cpp
#include "esp_timer.h"

static inline uint32_t nowMs() {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static inline uint32_t nowUs() {
    return (uint32_t)(esp_timer_get_time());  // already µs
}
```

### 3. Replace all `millis()` → `nowMs()` (10 sites)

### 4. Replace all `micros()` → `nowUs()` (5 sites)

### 5. Replace `yield()` → no-op or `taskYIELD()`

At lines 1520, 1547 — these are cooperative yield hints inside polling loops. Replace with:

```cpp
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Replace yield() with:
taskYIELD();
```

Or if the yield is in a tight loop that waits for a sensor measurement, consider `vTaskDelay(pdMS_TO_TICKS(1))` to actually release CPU.

### 6. Add `CMakeLists.txt` (library root)

```cmake
idf_component_register(
    SRCS "src/SHT3x.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer
)
```

### 7. Add `idf_component.yml` (library root)

```yaml
version: "2.0.0"
description: "SHT3x temperature/humidity sensor driver (one-shot mode)"
targets:
  - esp32s2
  - esp32s3
dependencies:
  idf: ">=5.0"
```

### 8. Version bump

- `library.json` → `2.0.0`
- `Version.h` (if present) → `2.0.0`

### 9. Replace Arduino example with ESP-IDF example

Create `examples/espidf_basic/main/main.cpp`:

```cpp
#include <cstdio>
#include "SHT3x/SHT3x.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t dev;

static SHT3x::Status i2cWrite(uint8_t addr, const uint8_t* data, size_t len, void*) {
    esp_err_t err = i2c_master_transmit(dev, data, len, 100);
    return err == ESP_OK ? SHT3x::Status{SHT3x::Err::Ok}
                         : SHT3x::Status{SHT3x::Err::I2cNack, "transmit failed"};
}

static SHT3x::Status i2cWriteRead(uint8_t addr,
                                    const uint8_t* wdata, size_t wlen,
                                    uint8_t* rdata, size_t rlen, void*) {
    esp_err_t err = i2c_master_transmit_receive(dev, wdata, wlen, rdata, rlen, 100);
    return err == ESP_OK ? SHT3x::Status{SHT3x::Err::Ok}
                         : SHT3x::Status{SHT3x::Err::I2cNack, "xfer failed"};
}

extern "C" void app_main() {
    i2c_master_bus_config_t busCfg{};
    busCfg.i2c_port = I2C_NUM_0;
    busCfg.sda_io_num = GPIO_NUM_8;
    busCfg.scl_io_num = GPIO_NUM_9;
    busCfg.clk_source = I2C_CLK_SRC_DEFAULT;
    busCfg.glitch_ignore_cnt = 7;
    busCfg.flags.enable_internal_pullup = true;
    i2c_new_master_bus(&busCfg, &bus);

    i2c_device_config_t devCfg{};
    devCfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    devCfg.device_address = 0x44;
    devCfg.scl_speed_hz = 100000;
    i2c_master_bus_add_device(bus, &devCfg, &dev);

    SHT3x::Config cfg{};
    cfg.i2cAddr = 0x44;
    cfg.i2cWrite = i2cWrite;
    cfg.i2cWriteRead = i2cWriteRead;

    SHT3x::Sensor sensor;
    auto st = sensor.begin(cfg);
    if (st.err != SHT3x::Err::Ok) {
        printf("begin() failed: %s\n", st.msg);
    }

    while (true) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        sensor.tick(now);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
```

Create `examples/espidf_basic/main/CMakeLists.txt`:

```cmake
idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "." REQUIRES driver esp_timer)
```

Create `examples/espidf_basic/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "../..")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(sht3x_espidf_basic)
```

---

## Verification

```bash
cd examples/espidf_basic && idf.py set-target esp32s2 && idf.py build
```

- [ ] `idf.py build` succeeds
- [ ] Zero `#include <Arduino.h>` anywhere
- [ ] Zero `millis()`, `micros()`, `yield()` calls remaining
- [ ] Version bumped to 2.0.0
- [ ] `git tag v2.0.0`
