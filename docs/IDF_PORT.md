# SHT3x ESP-IDF v6.0.1 Port Status

Last updated: 2026-05-19

Scope: implemented core portability changes plus an ESP-IDF component/example. Arduino/PlatformIO support remains intact.

Official ESP-IDF references:
- I2C master driver: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/i2c.html
- Build system and components: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html
- ESP-IDF v6 peripheral migration notes: https://docs.espressif.com/projects/esp-idf/en/release-v6.0/esp32c3/migration-guides/release-6.x/6.0/peripherals.html

## Current State

- Public API is in `include/SHT3x/`; implementation is in `src/SHT3x.cpp`.
- `library.json` and `platformio.ini` remain Arduino/PlatformIO package files.
- Root `CMakeLists.txt` registers the core as an ESP-IDF component.
- The driver already uses injected I2C callbacks from `Config`; library code does not call `Wire` directly.
- Supported addresses are `0x44` and `0x45`.
- The driver supports single-shot measurement, periodic measurement, ART acceleration, stop periodic, status read/clear, heater control, soft reset, interface reset, optional general-call reset, serial number read, alert limits, raw command helpers, and health tracking.
- Timing-sensitive work is deadline-driven and advanced by `tick(uint32_t nowMs)`.
- The driver validates SHT3x CRC bytes on temperature, humidity, and serial/status data.

## Arduino Dependencies

- `src/SHT3x.cpp` no longer includes `<Arduino.h>`.
- `src/PlatformTime.h` is the private fallback timing/yield shim for Arduino, ESP-IDF, and native tests.
- `include/SHT3x/Config.h` exposes framework-neutral callbacks:
  - `i2cWrite`
  - `i2cWriteRead`
  - `nowMs`
  - `nowUs`
  - `cooperativeYield`
  - optional `busReset`
  - optional `hardReset`
- `examples/common/I2cTransport.h`, `I2cScanner.h`, `BoardConfig.h`, `CommandHandler.h`, and `Log.h` are Arduino example glue. They use `Wire`, `Serial`, `String`, GPIO helpers, `delay()`, and `yield()`.
- `platformio.ini` builds Arduino examples and native tests. It is not an IDF project file.
- `library.json` declares only the Arduino framework and must remain the PlatformIO package manifest.
- `include/SHT3x/Version.h` is generated from `library.json`; do not edit it by hand.

## Portability Status

Implemented:

1. The core driver compiles through a private platform timing/yield shim instead of direct Arduino includes.
2. ESP-IDF fallback timing uses `esp_timer_get_time()` and `taskYIELD()`.
3. Root `CMakeLists.txt` provides `idf_component_register`.
4. `examples/idf/basic` provides an ESP-IDF v6 `i2c_master` adapter.
5. Arduino examples remain separate and are not part of the IDF component target.
6. The IDF example maps `esp_err_t` values to library `Status` codes and advertises only the timeout capability.

Still application-owned:

1. I2C bus/device creation and destruction.
2. SDA/SCL pins, pullups, clock speed, reset pins, alert pins, and timeout policy.
3. Hardware smoke testing on target boards.

## Exact Files and APIs to Change

- `src/SHT3x.cpp`
  - Replace direct `<Arduino.h>` usage with a private platform/timing shim.
  - Preserve the existing bounded wait logic and `MAX_SPIN_ITERS` guard.
  - Keep all SHT3x commands, CRC handling, and health tracking in the core driver.
- `include/SHT3x/Config.h`
  - No API break is required for the first IDF port.
  - Keep transport, timing, yield, bus-reset, and hard-reset callbacks as the portability boundary.
  - Under IDF, examples should always supply `nowMs`, `nowUs`, and `cooperativeYield` so timing is explicit.
- Private shim `src/PlatformTime.h`
  - Under Arduino, call `millis()`, `micros()`, and `yield()`.
  - Under ESP-IDF, use `esp_timer_get_time()` for time and `taskYIELD()` or `vTaskDelay(1)` only in the optional cooperative-yield path.
  - Keep this file private; public headers must stay framework-neutral.
- ESP-IDF example files under `examples/idf/basic/main/`
  - Own the I2C bus, device handle, optional reset GPIO, and optional bus recovery GPIOs.
  - Fill `Config` with IDF adapter callbacks.
  - Call `tick()` frequently enough for pending measurement operations.
- CMake files
  - Root component `CMakeLists.txt`.
  - Example project files under `examples/idf/basic`.

## Dual Arduino and ESP-IDF Architecture

- Keep the SHT3x core as a framework-neutral C++17 component.
- Keep Arduino-only adapters in `examples/common/`; they are not part of the library.
- Add IDF-only adapters under an IDF example or an optional `extras/idf/` style directory.
- The library must never own the ESP-IDF I2C bus, pins, pullups, clock speed, reset pin, or alert pin.
- Optional `busReset` and `hardReset` remain application callbacks. The IDF implementation may use `driver/gpio.h`, but that code must stay outside the library core.
- The application owns bus lifetime:
  - create an `i2c_master_bus_handle_t`;
  - add an `i2c_master_dev_handle_t` for `0x44` or `0x45`;
  - optionally add a handle for general-call address `0x00` only when general-call reset is enabled;
  - pass an adapter context through `Config.i2cUser`;
  - destroy handles after `driver.end()` and after all driver calls stop.
- Do not call public driver APIs from an ISR. If ALERT is wired to a GPIO, notify a task and call the driver there.

## IDF Transport Adapter Contract

Use ESP-IDF v6.0.1's new I2C master driver only:

```cpp
#include <driver/i2c_master.h>
```

The adapter should provide the existing callback shape:

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

- `addr` is a 7-bit address. Accept the configured sensor address and, only if explicitly enabled, general-call `0x00`.
- `idfWrite()` calls `i2c_master_transmit()`.
- `idfWriteRead()` calls:
  - `i2c_master_receive()` when `txLen == 0`;
  - `i2c_master_transmit()` when `rxLen == 0`;
  - reject `txLen != 0` for SHT3x reads. The current `Config` contract uses a
    separate command write followed by a receive-only read; do not collapse that
    into a repeated-start transaction.
- Many SHT3x reads are intentionally split: command write first, wait, then
  read with `txLen == 0`. Do not collapse these into a repeated-start
  transaction in the current adapter contract.
- `timeoutMs` is passed as the ESP-IDF transfer timeout in milliseconds.
- Clamp or reject `timeoutMs` before passing it to ESP-IDF's signed
  `xfer_timeout_ms`; never allow overflow to become `-1` because `-1` waits
  forever.
- Map `ESP_OK` to `Err::OK`.
- Map `ESP_ERR_TIMEOUT` to `Err::I2C_TIMEOUT`.
- Map `ESP_ERR_INVALID_ARG` to `Err::INVALID_PARAM`.
- Map `ESP_ERR_INVALID_RESPONSE` to an I2C NACK-related status. A normal
  `i2c_master_receive()` adapter cannot prove address-vs-data phase, so use
  `Err::I2C_ERROR` with `Status.detail = ESP_ERR_INVALID_RESPONSE` unless a
  custom adapter proves the phase.
- Map address/data NACKs to `Err::I2C_NACK_ADDR` or `Err::I2C_NACK_DATA` only when the adapter can distinguish them. Otherwise use `Err::I2C_ERROR` and place the raw `esp_err_t` value in `Status.detail`.
- Do not register `i2c_master_register_event_callbacks()` on the handle used by
  these callbacks unless the adapter waits for transfer completion before
  returning. The driver expects callbacks to be complete and blocking.
- Do not set `TransportCapability::READ_HEADER_NACK` for a normal IDF adapter.
  Set it only if a custom adapter can distinguish read-header NACK from other
  transfer failures.
- Clock-stretching single-shot commands may require a transfer timeout long enough for the selected repeatability. Non-stretch commands should remain deadline-driven through the driver.

## CMake and Component Plan

Minimal core component:

```cmake
idf_component_register(
  SRCS "src/SHT3x.cpp"
  INCLUDE_DIRS "include"
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
```

If the private timing shim uses ESP-IDF fallback APIs, add private requirements:

```cmake
idf_component_register(
  SRCS "src/SHT3x.cpp"
  INCLUDE_DIRS "include"
  PRIV_REQUIRES esp_timer freertos
)
```

If an IDF adapter is built into an example component, that example should declare:

```cmake
idf_component_register(
  SRCS "main.cpp" "IdfI2cTransport.cpp"
  INCLUDE_DIRS "."
  REQUIRES SHT3x-main esp_driver_i2c esp_timer esp_driver_gpio
)
```

The component name above assumes the repository directory remains
`SHT3x-main`. If the port renames or copies the component directory to `SHT3x`,
update the `REQUIRES` value accordingly. `esp_driver_gpio` is only for example
GPIO reset/bus-recovery code; do not require GPIO from the core component unless
the core starts using ESP-IDF GPIO APIs, which it should not.

Do not compile `examples/common/*.h` into ESP-IDF targets.

## IDF and Arduino Example Plan

Arduino examples:

- Keep existing examples using `examples/common/BoardConfig.h` and `I2cTransport.h`.
- Do not replace Arduino `Wire` examples with IDF code.

ESP-IDF examples:

- Add `examples/idf/basic` with a normal ESP-IDF `main` component.
- Configure SDA, SCL, pullups, and bus frequency in the example only.
- Use `i2c_new_master_bus()`, `i2c_master_bus_add_device()`, and the callback adapter.
- Provide `nowMs`, `nowUs`, and `cooperativeYield` callbacks.
- Build an `SHT3x::Config`, set callbacks and timeout, and call `begin(config)`.
- Demonstrate single-shot non-stretch measurement first.
- Add a separate periodic-mode example or mode switch that calls `tick()` from a task loop.
- Demonstrate optional hard reset and bus reset only when board pins are configured by the example.
- Print results with ESP-IDF logging from the example, not from the library.

## Test and Validation Plan

- Compile the existing Arduino PlatformIO environments.
- Compile native tests to preserve framework-neutral behavior.
- Add an ESP-IDF example build for ESP32-S2 and ESP32-S3.
- Hardware smoke test both valid addresses, `0x44` and `0x45`.
- Verify command CRC and read CRC failure injection.
- Verify single-shot non-stretch and, if enabled, clock-stretch modes.
- Verify periodic mode start, read, ART, and stop periodic.
- Verify heater enable/disable and status bits.
- Verify alert limit write/read round trip without corrupting reserved data.
- Verify soft reset, interface reset, optional general-call reset, optional hard reset, and bus-reset recovery.
- Verify health transitions on injected timeout/NACK and recovery on later success.

## ESP-IDF v6.0.1 Hazards

- Do not include `<driver/i2c.h>` or use legacy APIs such as `i2c_param_config()`, `i2c_driver_install()`, `i2c_cmd_link_create()`, or command-link transactions.
- Use `<driver/i2c_master.h>` and the `esp_driver_i2c` component dependency.
- IDF builds commonly surface warnings that Arduino builds hide. Keep casts explicit for enum, integer-width, `size_t`, and signed/unsigned conversions.
- Avoid global constructors that create ESP-IDF bus handles before the scheduler/runtime is initialized.
- `esp_timer_get_time()` returns microseconds as `int64_t`; down-convert deliberately for existing wrap-safe `uint32_t` logic.
- Do not add blocking `vTaskDelay()` calls inside the core driver for measurement wait paths.
- General-call reset affects every compatible device on the bus. Keep `allowGeneralCallReset` opt-in and make the IDF example explicit.
- If the adapter cannot identify read-header NACKs, do not advertise that capability.

## Ordered Checklist

1. Add a private timing/yield shim and remove direct `<Arduino.h>` use from `src/SHT3x.cpp`. Done.
2. Add a minimal component `CMakeLists.txt` for the core library. Done.
3. Add an IDF I2C adapter using `<driver/i2c_master.h>` outside the core driver. Done.
4. Add optional IDF hard-reset and bus-reset callback examples outside the library. Documented as application-owned.
5. Add `examples/idf/basic` with bus setup, adapter callbacks, timing callbacks, and a polling task. Done.
6. Build with ESP-IDF v6.0.1 for ESP32-S2 and ESP32-S3. Pending local ESP-IDF environment.
7. Run Arduino and native builds to confirm existing users are unaffected. Pending local validation.
8. Run hardware tests for probe, CRC, single-shot, periodic mode, resets, alerts, heater, and fault injection. Pending hardware.
9. Update README and changelog for the implemented port. Done.
