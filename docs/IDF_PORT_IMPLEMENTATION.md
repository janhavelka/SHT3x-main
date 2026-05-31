# SHT3x ESP-IDF Port Implementation

Last updated: 2026-05-31

## Implemented

- The library core no longer includes `<Arduino.h>` from `src/SHT3x.cpp`.
- `src/PlatformTime.h` is framework-neutral and does not include Arduino or ESP-IDF headers.
- Root `CMakeLists.txt` registers the core as an ESP-IDF component with C++17 enabled.
- `examples/idf/basic` provides:
  - an ESP-IDF project CMake file;
  - a `main` component;
  - an `i2c_master` transport adapter;
  - explicit bus/device ownership in the example;
  - `Config::nowMs`, `nowUs`, `cooperativeYield`, `i2cWrite`, and `i2cWriteRead` wiring;
  - a native fixed-buffer command loop that covers the same driver scenarios as
    `examples/01_basic_bringup_cli` without compiling `examples/common/Sht3xCli.*`
    or Arduino compatibility facades into the IDF example.
  - a single-owner `app_main` loop that keeps calling `tick()` while a separate
    input task waits on stdin.
  - no general-call reset handle; production applications that enable
    general-call reset must create and route an application-owned `0x00` device
    handle.

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

The example advertises `TransportCapability::TIMEOUT` and
`TransportCapability::BUS_ERROR`; it does not advertise `READ_HEADER_NACK`
because the standard IDF receive API does not prove the NACK phase.

## Validation

CI now runs `tools/check_idf_example_contract.py` directly and builds
`examples/idf/basic` for `esp32s3` and `esp32s2` using
`espressif/idf:release-v5.4`. Local validation should still record the exact
ESP-IDF version and whether `idf.py` was available.

Run these checks from the repository root:

```bash
python tools/check_core_timing_guard.py
python tools/check_cli_contract.py
python tools/check_idf_example_contract.py
pio test -e native
pio run -e esp32s3dev
pio run -e esp32s2dev
```

Run these checks from `examples/idf/basic` in an ESP-IDF 5.4+ environment:

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
- Verify ALERT pin/status behavior during periodic mode with
  `readStatusWithModeRestore()` and explicit `clearStatus()`.
- Capture production humidity fixture data as described in
  `docs/HARDWARE_VALIDATION.md`; ambient testing alone is not an accuracy claim.
