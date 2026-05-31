# SHT3x ESP-IDF CI and Docs Hardening Report

Date: 2026-05-31
Branch: `hardening/sht3x-industry-readiness`
Mode: implementation and local software validation

## Scope

Prompt 03 requested ESP-IDF CI coverage, direct execution of the IDF guard,
clearer ESP-IDF/example documentation, public API latency documentation, and a
hardware validation matrix with ALERT/status and humidity ambient-test coverage.

## CI and Guard Changes

- `.github/workflows/ci.yml` now runs `tools/check_idf_example_contract.py`
  directly in `validate-library`.
- CI now has an `idf-example-build` matrix for `esp32s3` and `esp32s2` using
  `espressif/idf:release-v5.4`.
- CI validates generated `Version.h` with `python scripts/generate_version.py check`.
- `tools/check_idf_example_contract.py` now also verifies:
  - core `include/SHT3x/` and `src/` do not include Arduino/ESP-IDF framework headers;
  - the IDF example scans `IdfI2cTransport.h` as well as `.cpp` and CMake files;
  - the example does not depend on checkout-directory component name `SHT3x-main`;
  - root ESP-IDF component metadata, S2/S3 targets, and IDF 5.4 floor are present.
- `examples/idf/basic/main/CMakeLists.txt` no longer names the SHT3x component in
  `REQUIRES`, avoiding checkout path fragility.

## Example Honesty

- README labels both shipped examples as diagnostic/bring-up CLIs, not
  production-style task architectures.
- The native IDF example comment states it is single-owner and that shared-bus
  or multi-task applications need external serialization.
- The IDF adapter no longer exposes a misleading general-call opt-in without a
  dedicated `0x00` device handle. General-call reset remains an application
  responsibility on an isolated bus.
- `examples/common/Sht3xCli.h` now describes the Arduino diagnostic CLI
  accurately; IDF has its own native fixed-buffer CLI.

## Documentation Changes

- README now includes a public API transaction/latency table.
- README and IDF docs state the ESP-IDF baseline as 5.4+ and avoid claiming
  local IDF build success.
- `docs/HARDWARE_VALIDATION.md` tracks board, mode, ALERT/status, reset,
  fault-injection, and humidity fixture evidence.
- `docs/IDF_PORT_IMPLEMENTATION.md` and `docs/IDF_PORT.md` describe the CI
  matrix, single-owner IDF example model, and hardware work still required.
- `docs/SHT3X_IDF_MERGED_INDUSTRY_READINESS_AUDIT.md` is marked as historical
  because its ALERT/status blocker was addressed by the later helper.

## Local Validation Results

| Check | Result | Notes |
|-------|--------|-------|
| `python tools/check_core_timing_guard.py` | PASS | Core timing/framework guard passed. |
| `python tools/check_cli_contract.py` | PASS | Arduino CLI contract passed. |
| `python tools/check_idf_example_contract.py` | PASS | Native IDF/core contract passed. |
| `python scripts/generate_version.py check` | PASS | `Version.h` was up to date. |
| `python -m platformio test -e native` | PASS | 70/70 native tests passed. |
| `python -m platformio run -e esp32s3dev` | PASS | Arduino ESP32-S3 firmware built. |
| `python -m platformio run -e esp32s2dev` | PASS | Arduino ESP32-S2 firmware built. |
| `python -m platformio pkg pack` | PASS | Wrote `SHT3x-1.5.0.tar.gz`; tarball removed after validation. |
| `idf.py --version` | NOT RUN | Local `idf.py` is not installed or not on `PATH`; native IDF builds are covered by CI configuration, not local execution. |

## Remaining Hardware Work

No physical ESP32-S2/S3 board, SHT3x device, ALERT pin capture, humidity fixture,
or fault-injection jig was used in this prompt. Hardware validation remains
pending and must be recorded in `docs/HARDWARE_VALIDATION.md` before claiming
board-level or production humidity accuracy.

Reference links used for the CI/design update:

- ESP-IDF Docker image documentation: https://hub.docker.com/r/espressif/idf/
- ESP-IDF build-system documentation: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html
