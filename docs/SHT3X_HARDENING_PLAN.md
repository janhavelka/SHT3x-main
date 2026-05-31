# SHT3x Industry-Readiness Hardening Plan

## Branch

- Active hardening branch: `hardening/sht3x-industry-readiness`
- Starting branch before creation: `audit/sht3x-idf-merged-industry-readiness`
- Starting worktree: clean (`git status --short` produced no output)
- Recent baseline history:
  - `c4192d3 Add SHT3X IDF-Merged Industry-Readiness Audit documentation`
  - `b92a530 Merge pull request #1 from janhavelka:codex/idf-port`
  - `c6cde09 Enhance timing callback requirements and improve documentation for SHT3x driver`
  - `09cc977 Refactor CLI contract checks and add component metadata`
  - `283ed59 Enforce framework-neutral core boundary`

## Audit Summary

The IDF-merged audit classifies the driver as engineering-grade with major gaps, not yet industry-grade. The architecture is strong: framework-neutral core code, injected I2C transport, mandatory monotonic timing hooks, CRC validation, bounded waits, tracked health/offline behavior, and a native ESP-IDF example with CLI/tick separation. The primary production blocker is SHT3x ALERT/status behavior in periodic and ART modes. Pure ESP-IDF CI proof and hardware validation remain missing.

This prompt did not implement production fixes. Only `AGENTS.md` and this planning document were changed.

## Subagents Used

### `sht3x-datasheet-agent`

Read-only sources checked:
- `docs/SHT3x_datasheet.pdf`
- `docs/pdf-extracted-md/SHT3x_datasheet.md`
- `docs/SHT3x_HT_AN_AlertMode.pdf`
- `docs/pdf-extracted-md/SHT3x_HT_AN_AlertMode.md`
- `docs/extracted-md/04_protocol_commands_and_transactions.md`
- `docs/extracted-md/05_register_map.md`
- `docs/extracted-md/06_modes_interrupts_status_and_faults.md`
- `docs/extracted-md/07_initialization_reset_and_operational_notes.md`

Factual findings:
- Status register read command is `0xF32D`: datasheet section 4.11, Table 17, page 13; alert note section 3.1, Table 6, page 3.
- Status clear command is `0x3041`: datasheet section 4.11, Table 19, page 13; alert note section 3.2, Table 7, page 3. It clears status bits 15, 11, 10, and 4.
- Periodic mode starts a stream of temperature/RH pairs at 0.5, 1, 2, 4, or 10 mps; clock stretching is not selectable.
- Fetch Data is `0xE000`; if no data is present the read header is NACKed, and after fetch the data memory is cleared.
- ART command is `0x2B32`; ART follows the same start, fetch, and break flow as periodic mode.
- Break command is `0x3093`; it stops periodic acquisition and enters single-shot mode after about 1 ms.
- ALERT mode is active whenever the sensor operates in periodic data acquisition mode.
- ALERT cause is represented in status bits 15, 11, and 10.
- Strict local-doc conclusion: only Fetch Data is explicitly documented as valid while periodic acquisition remains active. Status read, status clear, alert-limit commands, heater commands, reset commands, serial commands, and new measurement-mode commands are not documented as valid without breaking periodic first.

### `core-contracts-agent`

Confirmed:
- `include/SHT3x/` and `src/` remain framework-neutral: no Arduino, Wire, ESP-IDF, FreeRTOS, `String`, heap containers, or `delay()` usage.
- `begin()` requires `nowMs`, `nowUs`, and `cooperativeYield`.
- Command spacing and waits are bounded.
- `readStatus()`, `readSettings()`, `clearStatus()`, and alert-limit APIs exist.
- Alert-limit writes CRC-protect the payload and check sensor command/status bits afterward.
- Health tracking is centralized in tracked wrappers; `probe()` uses raw/no-health I2C; `recover()` uses tracked paths with offline I2C allowance.

Smallest clean changes identified:
- Explicitly delete `SHT3x` copy and move construction/assignment.
- Mirror README thread/ISR safety contracts in public Doxygen.
- Document that `tick(nowMs)` must use the same wrapping monotonic millisecond source as `Config::nowMs`.
- Check the OFFLINE state before periodic-mode preconditions in normal public I2C APIs so offline reporting is canonical.
- Clarify or tighten `writeAlertLimit()` behavior for finite out-of-range physical values, which are currently clamped by `encodeAlertLimit()`.

### `tests-agent`

Native tests are centralized in `test/test_basic.cpp` and already have useful fakes:
- `FakeTransport`
- `ScriptedTransport`
- `CountTransport`
- `LogTransport`
- `TimingTransport`
- `CommandCaptureTransport`

Test gaps and approach:
- Add a small test-local frame transport that can return arbitrary 16-bit words with correct CRC, especially for status-register tests.
- Add public API tests for status and clear behavior while periodic mode is active.
- Add `readSettings()` tests for `statusValid` in periodic, pending single-shot, offline, and successful-status cases.
- Add status-bit parsing tests for ALERT, heater, reset, command, and write-CRC bits.
- Add `clearStatus()` command-only tests, including transport failure and busy/no-I2C cases.
- Add compile-time tests that `SHT3x` is not copy/move constructible or assignable.
- Add partial transaction failure tests for `readStatus()` and `writeAlertLimitRaw()` phases.

### `idf-ci-agent`

Findings:
- `.github/workflows/ci.yml` has PlatformIO Arduino builds, native tests, package validation, and CLI guard checks, but no pure ESP-IDF job.
- `tools/check_idf_example_contract.py` exists and passed locally, but is not wired into CI.
- `idf_component.yml` declares `esp32s2` and `esp32s3`; ESP-IDF CI should build both targets.
- The IDF version should be pinned deliberately. The example uses `driver/i2c_master.h` and `REQUIRES esp_driver_i2c`, so CI should use a known-compatible ESP-IDF release.
- Resolved on 2026-05-31: `examples/idf/basic/main/CMakeLists.txt` no longer
  requires checkout-derived component `SHT3x-main`.
- Pure IDF CI should run `python scripts/generate_version.py check` because `Version.h` is generated and included by public headers.

Recommended future CI shape:
- Add a separate `esp-idf-build` job.
- Set up and pin ESP-IDF.
- Run `python scripts/generate_version.py check`.
- Run `python tools/check_idf_example_contract.py`.
- Build `examples/idf/basic` for `esp32s3` and `esp32s2`.
- Resolve the component-name/checkout-path fragility before relying on CI.

## Baseline Commands And Results

- `git status --short`
  - Result: clean, no output.
- `git branch --show-current`
  - Result before branch creation: `audit/sht3x-idf-merged-industry-readiness`.
- `git log --oneline -5`
  - Result listed under Branch above.
- `git checkout -b hardening/sht3x-industry-readiness`
  - Result: switched to new branch `hardening/sht3x-industry-readiness`.
- `python --version`
  - Result: `Python 3.13.12`.
- `python -m platformio --version`
  - Result: `PlatformIO Core, version 6.1.19`.
- `python tools/check_core_timing_guard.py`
  - Result: `Core timing guard PASSED`.
- `python tools/check_cli_contract.py`
  - Result: `CLI contract PASSED`.
- `python tools/check_idf_example_contract.py`
  - Result: `IDF example contract PASSED`.
- `python scripts/generate_version.py check`
  - Result: `include/SHT3x/Version.h` is up to date.
- `python -m platformio test -e native`
  - Result: passed. `46 test cases: 46 succeeded`.
- `python -m platformio run -e esp32s3dev`
  - Result: success.
- `python -m platformio run -e esp32s2dev`
  - Result: success.
- `idf.py --version`
  - Result: unavailable in this shell. PowerShell reported `idf.py` is not recognized as a command. This is not counted as an ESP-IDF validation pass.

## Planned Prompt Sequence

1. ALERT/status periodic-mode fix.
2. Copy/move, partial-state tests, public API contract tests.
3. ESP-IDF CI/build documentation and docs hardening.
4. Hardware validation checklist and final report.

## Scope Boundary For Prompt 00

Prompt 00 made no production driver fixes. The only intended changes are:
- `AGENTS.md`: hardening rules and planned subagent roles.
- `docs/SHT3X_HARDENING_PLAN.md`: branch, baseline, agent findings, and prompt plan.
