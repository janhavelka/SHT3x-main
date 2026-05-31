# SHT3x ESP-IDF Port Status

Last updated: 2026-05-31

Scope: implemented core portability changes plus an ESP-IDF component and a
native diagnostic example. Arduino/PlatformIO support remains intact.

Official ESP-IDF references:
- I2C master driver: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/i2c.html
- Build system and components: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html
- ESP-IDF v5.4 Docker image / CI usage: https://hub.docker.com/r/espressif/idf/

## Current State

- Public API is in `include/SHT3x/`; implementation is in `src/SHT3x.cpp`.
- `library.json` and `platformio.ini` remain Arduino/PlatformIO package files.
- `idf_component.yml` declares the ESP-IDF component floor as 5.4 because the
  example and CI use the new `i2c_master` driver and split driver components.
- Root `CMakeLists.txt` registers the core as an ESP-IDF component.
- The driver already uses injected I2C callbacks from `Config`; library code does not call `Wire` directly.
- Supported addresses are `0x44` and `0x45`.
- The driver supports single-shot measurement, periodic measurement, ART acceleration, stop periodic, status read/clear, heater control, soft reset, interface reset, optional general-call reset, serial number read, alert limits, raw command helpers, and health tracking.
- Timing-sensitive work is deadline-driven and advanced by `tick(uint32_t nowMs)`.
- The driver validates SHT3x CRC bytes on temperature, humidity, and serial/status data.

## Arduino Dependencies

- `src/SHT3x.cpp` no longer includes `<Arduino.h>`.
- `src/PlatformTime.h` is framework-neutral and intentionally inert; `begin()` requires application-supplied timing/yield callbacks through `Config`.
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
- `library.json` declares Arduino and ESP-IDF framework compatibility and remains the PlatformIO package manifest.
- `include/SHT3x/Version.h` is generated from `library.json`; do not edit it by hand.

## Portability Status

Implemented:

1. The core driver compiles without Arduino or ESP-IDF framework headers.
2. ESP-IDF timing/yield behavior is injected by the example through `Config::nowMs`, `Config::nowUs`, and `Config::cooperativeYield`.
3. Root `CMakeLists.txt` provides `idf_component_register`.
4. `examples/idf/basic` provides an ESP-IDF 5.4+ `i2c_master` adapter.
5. Arduino examples remain separate and are not part of the IDF component target.
6. The IDF example maps `esp_err_t` values to library `Status` codes and advertises only the timeout capability.
7. CI is configured to build `examples/idf/basic` with ESP-IDF `release-v5.4`
   for `esp32s3` and `esp32s2`.

Still application-owned:

1. I2C bus/device creation and destruction.
2. SDA/SCL pins, pullups, clock speed, reset pins, alert pins, and timeout policy.
3. Hardware smoke testing on target boards.
4. Production task ownership, shared-bus locking, telemetry, and recovery
   policy. The shipped IDF example is a diagnostic bring-up CLI.

## Exact Files and APIs to Change

- `src/SHT3x.cpp`
  - Keep framework headers out of the core implementation.
  - Preserve the existing bounded wait logic and `MAX_SPIN_ITERS` guard.
  - Keep all SHT3x commands, CRC handling, and health tracking in the core driver.
- `include/SHT3x/Config.h`
  - Keep transport, timing, yield, bus-reset, and hard-reset callbacks as the portability boundary.
  - Applications must supply `nowMs`, `nowUs`, and `cooperativeYield`; `begin()` rejects missing timing hooks before I2C.
- Private shim `src/PlatformTime.h`
  - Do not include Arduino or ESP-IDF headers.
  - Keep the fallback inert; examples/applications must inject real timing through `Config`.
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

Use ESP-IDF 5.4+ with the new I2C master driver:

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

If an IDF adapter is built into an example component, that example should declare:

```cmake
idf_component_register(
  SRCS "main.cpp" "IdfI2cTransport.cpp"
  INCLUDE_DIRS "."
  REQUIRES esp_driver_i2c esp_timer esp_driver_gpio
)
```

The example `main` component intentionally does not name the SHT3x component in
`REQUIRES`; ESP-IDF's `main` component can require project components without
tying the build to a checkout directory name. `esp_driver_gpio` is only for
example GPIO reset/bus-recovery code; do not require GPIO from the core
component unless the core starts using ESP-IDF GPIO APIs, which it should not.

Do not compile Arduino-only helpers from `examples/common/` into ESP-IDF
targets. The ESP-IDF example now uses its own native fixed-buffer CLI; keep
`examples/common/Sht3xCli.*` on the Arduino/example-helper side unless it is
kept free of Arduino-named facades and IDF-only APIs.

## IDF and Arduino Example Plan

Arduino examples:

- Keep existing examples using `examples/common/BoardConfig.h` and `I2cTransport.h`.
- Do not replace Arduino `Wire` examples with IDF code.

ESP-IDF examples:

- Keep `examples/idf/basic` as a normal ESP-IDF `main` component.
- Configure SDA, SCL, pullups, and bus frequency in the example only.
- Use `i2c_new_master_bus()`, `i2c_master_bus_add_device()`, and the callback adapter.
- Provide `nowMs`, `nowUs`, and `cooperativeYield` callbacks.
- Build an `SHT3x::Config`, set callbacks and timeout, and call `begin(config)`.
- Demonstrate single-shot non-stretch measurement first.
- Keep `tick()` running outside blocking stdin input. The current example uses a
  separate input task and a main owner loop that calls `tick()`.
- Demonstrate optional hard reset and bus reset only when board pins are configured by the example.
- Print results with ESP-IDF logging from the example, not from the library.
- Keep the example single-owner. Shared bus or multi-task applications must add
  external serialization and any general-call device handle themselves.

## Test and Validation Plan

- Compile the existing Arduino PlatformIO environments.
- Compile native tests to preserve framework-neutral behavior.
- CI is configured to compile the ESP-IDF example for ESP32-S2 and ESP32-S3 with
  `espressif/idf:release-v5.4`.
- Run `tools/check_idf_example_contract.py` in CI and locally before IDF builds.
- Hardware smoke test both valid addresses, `0x44` and `0x45`.
- Verify command CRC and read CRC failure injection.
- Verify single-shot non-stretch and, if enabled, clock-stretch modes.
- Verify periodic mode start, read, ART, and stop periodic.
- Verify heater enable/disable and status bits.
- Verify alert limit write/read round trip without corrupting reserved data.
- Verify soft reset, interface reset, optional general-call reset, optional hard reset, and bus-reset recovery.
- Verify health transitions on injected timeout/NACK and recovery on later success.

## ESP-IDF Hazards

- Do not include `<driver/i2c.h>` or use legacy APIs such as `i2c_param_config()`, `i2c_driver_install()`, `i2c_cmd_link_create()`, or command-link transactions.
- Use `<driver/i2c_master.h>` and the `esp_driver_i2c` component dependency.
- IDF builds commonly surface warnings that Arduino builds hide. Keep casts explicit for enum, integer-width, `size_t`, and signed/unsigned conversions.
- Avoid global constructors that create ESP-IDF bus handles before the scheduler/runtime is initialized.
- `esp_timer_get_time()` returns microseconds as `int64_t`; down-convert deliberately for existing wrap-safe `uint32_t` logic.
- Do not add blocking `vTaskDelay()` calls inside the core driver for measurement wait paths.
- General-call reset affects every compatible device on the bus. Keep `allowGeneralCallReset` opt-in and make the IDF example explicit.
- If the adapter cannot identify read-header NACKs, do not advertise that capability.

## Ordered Checklist

1. Add a framework-neutral timing/yield shim and remove direct framework headers from the core. Done.
2. Add a minimal component `CMakeLists.txt` for the core library. Done.
3. Add an IDF I2C adapter using `<driver/i2c_master.h>` outside the core driver. Done.
4. Add optional IDF hard-reset and bus-reset callback examples outside the library. Documented as application-owned.
5. Add `examples/idf/basic` with bus setup, adapter callbacks, timing callbacks, and CLI parity with the Arduino bringup example. Done.
6. ESP-IDF 5.4 CI build jobs for ESP32-S2 and ESP32-S3 are added; verify the
   latest workflow logs before claiming validation.
7. Run Arduino and native builds to confirm existing users are unaffected. See
   `docs/IDF_PORT_IMPLEMENTATION.md` and `docs/README.md` for the current
   validation boundary.
8. Run hardware tests for probe, CRC, single-shot, periodic mode, resets, alerts, heater, and fault injection. Pending hardware.
9. Update README and changelog for the implemented port. Done.
