# SHT3x ESP-IDF Port Implementation

Last updated: 2026-05-19

## Implemented

- The library core no longer includes `<Arduino.h>` from `src/SHT3x.cpp`.
- `src/PlatformTime.h` is the only private framework fallback shim:
  - Arduino builds use `millis()`, `micros()`, and `yield()` when timing hooks are not supplied.
  - ESP-IDF builds use `esp_timer_get_time()` and `taskYIELD()` when timing hooks are not supplied.
- Root `CMakeLists.txt` registers the core as an ESP-IDF component with C++17 enabled.
- `examples/idf/basic` provides:
  - an ESP-IDF project CMake file;
  - a `main` component;
  - an `i2c_master` transport adapter;
  - explicit bus/device ownership in the example;
  - `Config::nowMs`, `nowUs`, `cooperativeYield`, `i2cWrite`, and `i2cWriteRead` wiring;
  - the same user-visible CLI contract as `examples/01_basic_bringup_cli` through
    `examples/common/Sht3xCli.*`.

## Core Boundary

The core driver still owns no bus resources. Applications must create and
destroy the ESP-IDF I2C bus/device handles and pass a transport context through
`Config::i2cUser`.

The IDF adapter preserves the SHT3x command model: command writes use
`i2cWrite()`, and data reads use `i2cWriteRead()` with `txLen == 0`. It rejects
combined write-read transactions because the current driver contract separates
command write, tIDLE/measurement wait, and receive-only read phases.

The IDF example maps `esp_err_t` values to library `Status` codes:

- `ESP_OK` -> `Err::OK`
- `ESP_ERR_TIMEOUT` -> `Err::I2C_TIMEOUT`
- `ESP_ERR_INVALID_ARG` -> `Err::INVALID_PARAM`
- `ESP_ERR_INVALID_RESPONSE` -> `Err::I2C_ERROR`
- other ESP-IDF failures -> `Err::I2C_BUS`

The example advertises only `TransportCapability::TIMEOUT`; it does not
advertise `READ_HEADER_NACK` because the standard IDF receive API does not prove
the NACK phase.

## Validation

Run these checks from the repository root:

```bash
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
pio test -e native
pio run -e esp32s3dev
pio run -e esp32s2dev
```

Run these checks from `examples/idf/basic` in an ESP-IDF v6 environment:

```bash
idf.py set-target esp32s3
idf.py build
idf.py set-target esp32s2
idf.py build
```

## Remaining Hardware Work

- Smoke test addresses `0x44` and `0x45` on real ESP32-S2/S3 boards.
- Verify single-shot non-stretch and periodic fetch behavior against expected ranges.
- Verify CRC fault handling, reset callbacks, alert limit round trips, and heater status.
- Verify recovery behavior with injected I2C timeout and NACK failures.
