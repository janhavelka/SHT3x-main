# SHT3X IDF-Merged Industry-Readiness Audit

Historical note, 2026-05-31: this report is retained as a snapshot of the
pre-hardening audit. The ALERT/status blocker described below was addressed by
`readStatusWithModeRestore()` and follow-up tests; see
`docs/SHT3X_ALERT_STATUS_FIX_REPORT.md`. Current ESP-IDF CI/docs status is in
`docs/SHT3X_IDF_CI_DOCS_REPORT.md`, and hardware evidence is tracked in
`docs/HARDWARE_VALIDATION.md`.

Date: 2026-05-29
Repository: `C:\Users\HonzovoSpectre\Documents\Projects\SHT3x-main`
Branch: `audit/sht3x-idf-merged-industry-readiness`
Audit mode: report-only / no implementation
IDF merge classification: `QUALIFYING_IDF_MERGED`

## Executive Summary

The ESP-IDF port is merged into `main`, and the core architecture has
production-oriented traits: framework-neutral, injected I2C transport, required
monotonic timing hooks, CRC validation, bounded waits, and a native IDF example
with a task/queue CLI that keeps `tick()` running.

At audit time, the major device-specific blocker was alert/status behavior in
periodic mode. SHT3x ALERT mode is active in periodic acquisition, but
`readStatus()` returned `BUSY` whenever `_periodicActive` was true and
`readSettings()` hid that as `statusValid=false`. That prevented normal ALERT
interrupt diagnosis while the mode that produces alerts was active. Later
hardening added the explicit `readStatusWithModeRestore()` helper; hardware
ALERT validation remains pending.

## IDF Merge Evidence

- Default branch evidence: `origin/HEAD -> refs/heads/main`.
- Merge commit: `b92a530b1082608117aef15439059e081d43252a`.
- Merge message: `Merge pull request #1 from janhavelka:codex/idf-port`.
- Merged branch tip: `c6cde0958d62aed1f4bebc98ef94133cb1923baa`.
- Branch ancestry: `codex/idf-port` is an ancestor of `main`.
- IDF artifacts present on `main`:
  - `CMakeLists.txt`
  - `idf_component.yml`
  - `examples/idf/basic/main/main.cpp`
  - `examples/idf/basic/main/IdfI2cTransport.cpp`
  - `examples/idf/basic/main/IdfI2cTransport.h`
  - `examples/idf/basic/main/CMakeLists.txt`
  - `tools/check_idf_example_contract.py`
  - `docs/IDF_PORT.md`
  - `docs/IDF_PORT_IMPLEMENTATION.md`
- Limitation: the native ESP-IDF example was not built with `idf.py` in this audit because `idf.py` is not installed or not on `PATH`.

## Readiness Classification

Engineering-grade with major gaps.

The driver is well structured and substantially tested, but the alert/status semantic conflict is a production blocker for any design using ALERT. Pure ESP-IDF build proof and hardware validation are also missing.

## Scope Reviewed

- `include/SHT3x/`
- `src/`
- `examples/01_basic_bringup_cli/`
- `examples/idf/basic/`
- `examples/common/`
- `test/`
- `tools/`
- `docs/`
- `README.md`
- `platformio.ini`
- `library.json`
- `CMakeLists.txt`
- `idf_component.yml`
- `.github/workflows/ci.yml`

## Datasheet / Documentation Sources Found

- `docs/SHT3x_datasheet.pdf`
- `docs/SHT3x_driver_extraction.md`
- `docs/SHT3x_HT_AN_AlertMode.pdf`
- `docs/HT_AlertMode_BitConversion.xlsx`
- `docs/Sensirion_AppNotes_Humidity_Sensors_at_a_Glance.pdf`
- `docs/Sensirion_electronic_identification_code_SHT3x.pdf`
- `docs/Sensirion_Humidity_Sensors_Testing_at_Ambient_Conditions.pdf`
- `docs/SHT3x_membrane_option_datasheet.pdf`
- `docs/IDF_PORT.md`
- `docs/IDF_PORT_IMPLEMENTATION.md`
- `docs/extracted-md/*.md`
- `docs/pdf-extracted-md/*.md`

## Scorecard

| Area | Rating | Notes |
| --- | --- | --- |
| IDF merge evidence | Strong | Merge commit, branch ancestry, IDF artifacts, and docs are present. |
| Core framework neutrality | Strong | No Arduino/Wire/ESP-IDF core dependency found. |
| I2C ownership/injection | Strong | I2C, reset, time, and yield callbacks are injected. |
| ESP-IDF component correctness | Medium | Component files exist; no pure IDF build proof. |
| ESP-IDF example correctness | Good | Native IDF APIs and queue-based CLI keep `tick()` alive. |
| Status/error model | Good | Precise statuses and CRC errors exist. Alert/status BUSY behavior is wrong for periodic use. |
| Timing/determinism | Good | `begin()` requires `nowMs`, `nowUs`, and `cooperativeYield`; waits are bounded. |
| Device-specific correctness | Medium | CRC and measurement timing are strong; ALERT/status behavior fails. |
| Partial hardware state handling | Medium | Some restart/config paths are multi-step and need explicit partial-state tests. |
| Health/recovery behavior | Good | Offline/recovery tests exist. |
| Thread/ISR contract | Good | README documents not thread-safe and not ISR-safe; public headers should be equally explicit. |
| Tests/fault injection | Good | 46 native tests with fake/scripted transports. |
| ESP-IDF build coverage | Weak | Pure IDF build missing locally and in CI. |
| Arduino ESP32-S2/S3 readiness | Good | `esp32s3dev` and `esp32s2dev` PlatformIO builds passed. |
| Documentation honesty | Good | Timing, mode, Wire caveats, and ISR warning are present. |
| Hardware validation | Unknown | No hardware commands were run in this audit. |

## What Is Strong

- Core avoids Arduino and ESP-IDF headers; `src/PlatformTime.h` is explicitly framework-neutral.
- `begin()` rejects missing `nowMs`, `nowUs`, or `cooperativeYield`, so bounded waits do not silently degrade (`src/SHT3x.cpp:190`).
- I2C timeout is validated and propagated (`include/SHT3x/Config.h:135`, `src/SHT3x.cpp:1420`).
- CRC constants and CRC validation are present (`include/SHT3x/CommandTable.h:23`, `include/SHT3x/CommandTable.h:26`).
- The IDF transport maps `ESP_ERR_TIMEOUT` to `I2C_TIMEOUT` (`examples/idf/basic/main/IdfI2cTransport.cpp:18`, `examples/idf/basic/main/IdfI2cTransport.cpp:22`).
- The IDF example isolates blocking stdin in a task and keeps the main loop ticking (`examples/idf/basic/main/main.cpp:804`, `examples/idf/basic/main/main.cpp:806`, `examples/idf/basic/main/main.cpp:812`).
- README explicitly states the driver is not thread-safe and not ISR-safe (`README.md:328`, `README.md:330`).

## High-Severity Findings

### H1. ALERT/status semantics are blocked in periodic mode

Severity: High

Evidence:
- `readStatus()` returns `BUSY` whenever `_periodicActive` is true (`src/SHT3x.cpp:777`, `src/SHT3x.cpp:778`).
- `readSettings()` calls `readStatus()` and hides `BUSY` as `statusValid=false` unless the driver is offline (`src/SHT3x.cpp:544`, `src/SHT3x.cpp:549`, `src/SHT3x.cpp:553`).
- Local docs state alert mode is active whenever the sensor operates in periodic data acquisition mode and status bits identify humidity/temperature alert causes (`docs/extracted-md/06_modes_interrupts_status_and_faults.md:13`, `docs/extracted-md/06_modes_interrupts_status_and_faults.md:14`).

Impact:
- A production design using ALERT cannot read status to determine why ALERT asserted without first stopping periodic acquisition. That is the opposite of the normal ALERT operating mode.

Recommended remediation:
- Add a status-read path that is allowed during periodic/ART mode if the datasheet permits it.
- If a break is required before status read for a specific command class, document it and provide an explicit helper that stops/fetches/restores safely.
- Do not hide this as `statusValid=false` in settings snapshots without surfacing the reason.

Suggested tests:
- Native fake test: start periodic, simulate ALERT status bits, verify status can be read or that a documented stop/read/restore helper works.
- Hardware test: configure alert limits, start periodic, trigger ALERT, read status cause without losing production semantics.

### H2. Pure ESP-IDF build is not validated locally or in CI

Severity: High

Evidence:
- `CMakeLists.txt`, `idf_component.yml`, and `examples/idf/basic/` exist on `main`.
- CI runs PlatformIO workflows but no `idf.py build` job was found.
- Local command `idf.py --version` failed: `The term 'idf.py' is not recognized as the name of a cmdlet, function, script file, or operable program.`

Impact:
- Native ESP-IDF users can receive a component/example that passes Arduino/PlatformIO but fails in IDF CMake or target-specific IDF dependencies.

Recommended remediation:
- Add CI jobs:
  - `idf.py -C examples/idf/basic set-target esp32s3 build`
  - `idf.py -C examples/idf/basic set-target esp32s2 build`

Suggested tests:
- Add IDF S2/S3 build matrix and record ESP-IDF version.

## Medium-Severity Findings

### M1. Public copy/move operations are not explicitly disabled

Severity: Medium

Evidence:
- `class SHT3x` starts at `include/SHT3x/SHT3x.h:102`.
- No deleted copy or move constructor/assignment was found.
- The object stores callback context, cached settings, measurement state, health counters, and pending timing state.

Impact:
- Accidental copies can duplicate pending measurement state while sharing one bus/user context.

Recommended remediation:
- Delete copy and move operations.

Suggested tests:
- Add `static_assert` tests that the class is not copyable or movable.

### M2. Multi-step mode/configuration operations need explicit partial-state tests

Severity: Medium

Evidence:
- `begin()` and reset/config paths perform multiple command/write/read steps (`src/SHT3x.cpp:123`, `src/SHT3x.cpp:203`).
- Alert writes are command-with-data plus status command/read paths (`src/SHT3x.cpp:1063`).
- Native tests include cache mutation checks, but the audit did not find a complete matrix for every partial failure position.

Impact:
- A failure while restarting periodic/ART or writing alert limits can leave hardware and cached mode/configuration diverged unless every path rolls back or reports uncertainty.

Recommended remediation:
- Audit every multi-step setter and document whether hardware may be partially changed.
- Add dirty/unknown state only where rollback cannot be proven.

Suggested tests:
- Fail every transaction position in mode switch, alert-limit write, reset/recover, and periodic restart paths.

### M3. Native tests use private-member exposure

Severity: Medium

Evidence:
- Tests use private internals to get strong coverage, according to the tests/CI audit.

Impact:
- Internal tests are useful, but they can miss public API contract gaps or lock tests to implementation details.

Recommended remediation:
- Keep internal tests, but add public API contract tests for alert/status, timing hooks, recovery, and partial-state behavior.

Suggested tests:
- Public API black-box tests around periodic ALERT, reset, recover, and stale sample behavior.

## Low-Severity Findings

### L1. Dedicated IDF example contract is not run directly in CI

Severity: Low

Evidence:
- `tools/check_idf_example_contract.py` exists and passes locally.
- CI currently emphasizes `tools/check_cli_contract.py`; the dedicated checker should be a named step.

Impact:
- The IDF boundary could regress if CI command coverage changes.

Recommended remediation:
- Add direct CI step: `python tools/check_idf_example_contract.py`.

Suggested tests:
- CI-only guard.

## Device-Specific Correctness Checklist

| Item | Status | Notes |
| --- | --- | --- |
| CRC-8 verification | PASS | Data words are CRC-checked. |
| Single-shot commands | PASS | Timing hooks and conversion waits are modeled. |
| Periodic commands/fetch | PASS | Periodic fetch and expected not-ready handling exist. |
| ALERT mode semantics | FAIL | Status read is blocked in periodic mode, where ALERT is active. |
| Heater safety/status | PARTIAL | APIs exist; hardware validation missing. |
| Reset/general-call handling | PARTIAL | APIs exist; hardware validation missing. |
| Status register clear semantics | PARTIAL | Needs hardware/fault validation. |
| Alert limits and CRC writes | PASS/PARTIAL | CRC path exists; partial failure matrix needed. |
| RH/T conversion formulas | PASS | Local docs and tests cover conversions. |
| Clock stretching policy | PASS/PARTIAL | Documented and transport capability-aware; hardware validation missing. |
| Self-heating/high-rate caveats | PARTIAL | Docs mention warnings; needs validation notes. |
| Hardware validation | UNKNOWN | No hardware commands were run. |

## API Latency / Blocking Model

| API | I2C transactions | Other waits | Worst-case bound | Notes |
| --- | ---: | --- | --- | --- |
| `begin()` | BREAK write, soft-reset write, status command/read, optional periodic/ART start | 1 ms + 2 ms waits | Bounded by waits + `i2cTimeoutMs` per transaction | Requires time/yield hooks. |
| `tick(nowMs)` | 0 or fetch command + 6-byte read | None beyond I2C | Bounded by `i2cTimeoutMs` per transaction | Drives measurement completion/fetch. |
| `requestMeasurement()` single-shot | 1 command write | None | `i2cTimeoutMs` | Conversion wait deferred. |
| `requestMeasurement()` periodic/ART | Usually scheduling only | None | O(1) unless start command needed | `tick()` fetches. |
| `readStatus()` | command + 3-byte read | Command spacing gate | `commandDelayMs + i2cTimeoutMs` | Currently blocked in periodic mode. |
| `readSerialNumber()` | command + 6-byte read | Command spacing gate | `commandDelayMs + i2cTimeoutMs` | CRC checked. |
| Alert limit read | command + 3-byte read | Command spacing gate | `commandDelayMs + i2cTimeoutMs` | CRC checked. |
| Alert limit write | command+data write + status command/read | Command spacing gate | Multiple `i2cTimeoutMs` windows | Needs partial-failure matrix. |
| Reset APIs | command writes | 1-2 ms waits | Wait + `i2cTimeoutMs` | Uses cooperative yield. |
| `recover()` | Probe/reset/config operations | Bounded waits | Multi-transaction | Leaves driver in documented idle mode per README. |

## Partial-State / Cache Consistency Assessment

The driver has explicit cache and health concepts, and native tests cover some cache mutation behavior. The highest-risk partial-state surfaces are mode changes that stop/restart periodic/ART and alert-limit writes that combine data writes, CRC, and status readback. A hardening pass should enumerate each multi-step API and add tests that fail at every transaction position.

## Tests and Build Coverage

Run locally:
- `git status --short`: clean before report edits.
- `python --version`: `Python 3.13.12`.
- `python -m platformio --version`: `PlatformIO Core, version 6.1.19`.
- `python tools/check_core_timing_guard.py`: PASS, `Core timing guard PASSED`.
- `python tools/check_cli_contract.py`: PASS, `CLI contract PASSED`.
- `python tools/check_idf_example_contract.py`: PASS, `IDF example contract PASSED`.
- `python scripts/generate_version.py check`: PASS, `include\SHT3x\Version.h` up to date.
- `python -m platformio test -e native`: PASS, `46 test cases: 46 succeeded`.
- `python -m platformio run -e esp32s3dev`: PASS, `SUCCESS`.
- `python -m platformio run -e esp32s2dev`: PASS, `SUCCESS`.
- `python -m platformio pkg pack`: PASS, wrote `SHT3x-1.5.0.tar.gz`; tarball removed after audit.
- `idf.py --version`: FAIL, command not found.

Present in CI:
- PlatformIO S2/S3 builds.
- Native tests.
- Core timing guard.
- CLI contract.
- `pio pkg pack`.

Not run:
- `idf.py -C examples/idf/basic set-target esp32s3 build`: not run because `idf.py` is unavailable.
- `idf.py -C examples/idf/basic set-target esp32s2 build`: not run because `idf.py` is unavailable.
- Hardware validation: not run.

Missing:
- Pure IDF build in CI.
- Direct CI invocation of `tools/check_idf_example_contract.py`.
- Public API test for ALERT/status in periodic mode.
- Full partial-failure transaction matrix for multi-step setters.

## ESP-IDF Port Assessment

- Pure ESP-IDF component: Present.
- Pure ESP-IDF example: Present at `examples/idf/basic`.
- Native IDF APIs, not Arduino: Yes, uses `app_main`, `driver/i2c_master.h`, `esp_timer`, FreeRTOS tasking, fixed buffers, and no Arduino facade.
- External bus ownership: Yes. Example creates and owns the IDF bus/device and injects callbacks.
- Error mapping: Good. Timeout maps to `I2C_TIMEOUT`; invalid args map to `INVALID_CONFIG` or related statuses.
- Locking: Not a production shared-bus manager. External serialization is still required.
- Task/tick behavior: Good diagnostic pattern; input is queued and main loop keeps ticking.
- CI IDF build: Missing.

## Documentation Assessment

Missing or incomplete documentation contracts:
- Periodic ALERT/status behavior must be corrected and documented.
- Public headers should mirror README thread/ISR contract.
- Partial-state behavior for multi-step setters needs explicit docs.
- Hardware validation matrix needs actual results.
- Pure IDF build status and ESP-IDF version are not proven.

## Hardware Validation Needed

| Scenario | Target | Expected evidence |
| --- | --- | --- |
| Single-shot low/medium/high repeatability | ESP32-S2/S3 + SHT3x | Data-ready timing and CRC pass. |
| Periodic fetch rates | 0.5/1/2/4/10 mps | Fetch timing, not-ready mapping, no stale sample confusion. |
| ALERT thresholds | Temperature/RH threshold setup | ALERT cause readable while periodic is active or documented helper works. |
| Clock stretching | Stretch enabled/disabled | Timeout and read behavior match docs. |
| CRC fault injection | Fake or fault jig | CRC mismatch status and no stale success. |
| Heater | Controlled bench | Heater status and self-heating caveats documented. |
| Reset/recover | Device unplug/replug | Offline/recover behavior and cache state. |

## Recommended Implementation Plan

### P0 - Must fix before production claim

- Fix or explicitly redesign `readStatus()` behavior in periodic/ART mode.
- Add public API and hardware tests for ALERT cause handling.
- Add pure ESP-IDF S2/S3 CI builds.
- Delete copy/move operations.

### P1 - Should fix before release/merge

- Add partial-failure tests for multi-step mode and alert-limit APIs.
- Add direct CI step for `tools/check_idf_example_contract.py`.
- Mirror thread/ISR contract in public headers.

### P2 - Nice hardening / later

- Add black-box tests that avoid private-member exposure for core public contracts.
- Expand production humidity test notes from the Sensirion ambient testing document.

## Proposed Branch for Future Hardening

`hardening/sht3x-industry-readiness`

## Final Verdict

Not ready for an industry-grade claim as-is. The architecture is good, but the periodic ALERT/status issue is a high-priority production blocker.
