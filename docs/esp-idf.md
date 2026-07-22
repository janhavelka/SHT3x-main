# SHT3x ESP-IDF Notes

Last updated: 2026-07-21

Scope: framework-neutral core component plus a native ESP-IDF diagnostic
example. Arduino/PlatformIO support remains separate and intact.

## Current State

- Public API lives in `include/SHT3x/`; implementation lives in `src/SHT3x.cpp`.
- The core driver has no Arduino or ESP-IDF framework headers and owns no bus
  resources.
- `idf_component.yml` declares ESP-IDF `>=5.4`.
- Root `CMakeLists.txt` registers the core with `idf_component_register`.
- `examples/idf/basic` uses the ESP-IDF 5.4+ `driver/i2c_master.h` API.
- The ESP-IDF example owns the I2C bus/device handles, reset/bus-recovery GPIOs
  when configured, timing hooks, and CLI loop.
- The ESP-IDF example is a diagnostic bring-up CLI, not a production task model.
- Production bus-owner tasks should use `bind()`, `requestEnsureIdle()` or
  `requestMeasurement(JobRequest)`, and `pollJob(..., 1, ...)`; each poll uses
  zero or one transport callback and exposes deadline/identity/provenance.

## Core Boundary

Applications must create and destroy ESP-IDF I2C handles and pass a transport
context through `Config::i2cUser`. The library does not own pins, pull-ups, bus
frequency, global handles, locks, OS tasks, reset GPIOs, or ALERT GPIOs.

Required framework-neutral callbacks:

- `i2cWrite`
- `i2cWriteRead`
- `nowMs`
- `nowUs`
- `cooperativeYield`
- optional `busReset`
- optional `hardReset`

`bind()` and `begin()` reject missing timing/yield hooks as `INVALID_CONFIG`
before I2C is touched. `bind()` itself performs no I2C or wait. `begin()` is a
bounded synchronous compatibility API; it is not the recommended lifecycle
entry point inside an externally scheduled bus-owner task.

## IDF Transport Adapter

Use ESP-IDF 5.4+:

```cpp
#include <driver/i2c_master.h>
```

The adapter should implement the existing callback shape:

```cpp
Status idfWrite(uint8_t addr,
                const uint8_t* data,
                size_t len,
                uint32_t timeoutMs,
                void* user);

Status idfWriteRead(uint8_t addr,
                    const uint8_t* txData,
                    size_t txLen,
                    uint8_t* rxData,
                    size_t rxLen,
                    uint32_t timeoutMs,
                    void* user);
```

Expected behavior:

- Accept the configured 7-bit sensor address and, only if explicitly enabled,
  general-call `0x00`.
- `idfWrite()` calls `i2c_master_transmit()`.
- `idfWriteRead()` calls `i2c_master_receive()` when `txLen == 0`.
- Reject combined write/read transactions for current SHT3x reads. The driver
  contract separates command write, tIDLE/measurement wait, and receive-only
  read phases.
- Pass `timeoutMs` as the ESP-IDF transfer timeout after clamping or rejecting
  values that could overflow into an unbounded wait.
- Map `ESP_OK` to `Err::OK`.
- Map `ESP_ERR_TIMEOUT` to `Err::I2C_TIMEOUT`.
- Map `ESP_ERR_INVALID_ARG` to `Err::INVALID_PARAM`.
- Map `ESP_ERR_INVALID_RESPONSE` to `Err::I2C_ERROR` unless a custom adapter can
  distinguish address, data, or read-header NACK phases.
- Do not advertise `TransportCapability::READ_HEADER_NACK` unless the adapter
  can prove that phase.

## CMake Shape

Core component:

```cmake
idf_component_register(
  SRCS "src/SHT3x.cpp"
  INCLUDE_DIRS "include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
```

Example component:

```cmake
idf_component_register(
  SRCS "main.cpp" "IdfI2cTransport.cpp"
  INCLUDE_DIRS "."
  REQUIRES esp_driver_i2c esp_driver_gpio esp_timer freertos vfs
)
```

The example must not compile Arduino-only helpers from `examples/common/` into
ESP-IDF targets.

## Validation

Current v1.7.0 evidence: the repository-local IDF example/CLI contract guards
pass, and CI defines pinned ESP-IDF 5.4.2 builds for ESP32-S2 and ESP32-S3.
No local `idf.py` build or inspected live-CI result is claimed by the current
audit, so pure ESP-IDF build/hardware validation remains open.

Run from the repository root:

```bash
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
python -m platformio test -e native
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
```

Run from an ESP-IDF 5.4+ environment:

```bash
idf.py -C examples/idf/basic set-target esp32s3
idf.py -C examples/idf/basic build
idf.py -C examples/idf/basic set-target esp32s2
idf.py -C examples/idf/basic build
```

Confirm live CI logs or local `idf.py` logs before claiming ESP-IDF validation.

## Remaining Hardware Work

- Smoke test addresses `0x44` and `0x45` on real ESP32-S2/S3 boards.
- Verify single-shot non-stretch, clock-stretch if enabled, periodic fetch, ART,
  alert limits, reset callbacks, heater status, and CRC fault handling.
- Verify recovery behavior with injected I2C timeout and NACK failures.
- Verify ALERT pin/status behavior during periodic mode with
  `readStatusWithModeRestore()` and explicit `clearStatus()`.
- Capture production humidity fixture data before making accuracy claims.
