# SHT3x HIL Validation Report - COM20 - 2026-06-26

## Verdict

Hardware HIL did not run. The requested serial port, `COM20`, is present but
could not be opened from this shell for upload or serial control because Windows
returned `PermissionError(13, Access is denied)`.

No SHT3x firmware was flashed to `COM20`, no boot transcript or CLI prompt was
captured from `COM20`, and the requested 8-hour hardware soak did not start.
No hardware validation pass is claimed.

## Repository And Environment

| Field | Value |
| --- | --- |
| Repository | `C:\Users\Honza\Documents\Projects\SHT3x-main` |
| Branch | `main` |
| Commit | `52319873437d889b9889eb4825cf65b1bcd21e2d` |
| Worktree at final report | Dirty: `tools/run_i2c_hil.py`, `tools/test_run_i2c_hil_parser.py`, untracked `docs/reports/` |
| Date/time | `2026-06-26T21:22:00+02:00` |
| Timezone | Europe/Prague, UTC+02:00 |
| Host OS | Microsoft Windows 11 Education, OS version `10.0.26200` |
| Python | `Python 3.12.10` |
| PlatformIO | `PlatformIO Core, version 6.1.18` |
| Target environment | `esp32s3dev` |
| Target board | ESP32-S3 DevKitC-1, Arduino framework |
| Serial port | `COM20` |
| Baud rate | `115200` |
| Expected SHT3x address | `0x44` |

PlatformIO printed an obsolete-core warning for `6.1.18`, noting that `6.1.19`
was previously present.

## Hardware Setup And Safety Assumptions

The user reported that the board was in upload mode. From this shell, fixture
wiring, pull-ups, sensor variant, supply voltage, ALERT wiring, heater thermal
behavior, and bus sharing could not be physically confirmed.

No electrically disruptive tests were attempted. General-call reset, fault
injection, unplug tests, ALERT GPIO assertions, heater-on thermal tests, and
power cycling were not run.

Detected serial ports:

| Port | VID:PID | Serial | Observed state |
| --- | --- | --- | --- |
| `COM5` | `303A:1001` | `24:58:7C:DB:DB:AC` | Locked by another Python HIL runner |
| `COM19` | `303A:1001` | `44:1B:F6:8D:1F:74` | Opens; ESP32-S3 ROM boot text observed, not SHT3x CLI |
| `COM20` | `303A:1001` | `1C:DB:D4:80:C9:40` | Present but locked, access denied |
| `COM21` | `303A:1001` | `44:1B:F6:8D:1F:90` | Opens; ESP32-S3 ROM boot text observed, not SHT3x CLI |
| `COM27` | `303A:1001` | `1C:DB:D4:80:C9:5C` | Locked by another Python soak runner |
| `COM28` | `303A:0002` | `9&27E71F64&0&4` | Opens, no bytes captured |

Processes observed holding other ports:

```text
python.exe tools\hil_runner.py --port COM5 ... --soak-duration-s 172800 ...
python.exe tools\hil_cli_runner.py --port COM27 ... --soak-duration-s 28800 ...
```

No process command line explicitly naming `COM20` was visible, so the owner of
the COM20 handle was not identified from this shell. A VS Code or PlatformIO
serial monitor remains a likely external owner.

## Repository Inspection Summary

The repository is a single SHT3x library with public headers under
`include/SHT3x/`, implementation in `src/`, Arduino diagnostic CLI example in
`examples/01_basic_bringup_cli`, native ESP-IDF diagnostic example under
`examples/idf/basic`, and host HIL tooling in `tools/run_sht3x_hil.py` and
`tools/run_i2c_hil.py`.

The public API exposes injected I2C transport callbacks, required timing hooks,
`begin/tick/end`, single-shot, periodic, ART, status, heater, alert-limit,
serial-number, reset, recovery, low-level command helpers, health counters, and
settings snapshots. The Arduino CLI exposes those paths through bounded serial
commands such as `version`, `scan`, `probe`, `settings`, `drv`, `status`,
`single`, `raw`, `comp`, `serial`, `heater`, `alert`, `periodic`, `art`,
`reset`, `recover`, `stress`, and `stress_mix`.

## Tooling Work Implemented

`tools/run_i2c_hil.py` was extended in place. No parallel HIL framework was
created.

Implemented changes:

- Added `--parser-self-test`.
- Added `--boot-settle-s`, `--reconnect-attempts`, `--reconnect-delay-s`, and
  optional `--serial-reset`.
- Added `--benchmark-count` for bounded sample-rate benchmark commands.
- Added `--soak-duration-s`, `--soak-chunk-count`, and
  `--soak-recover-every`.
- Added a duration-bound soak loop using existing CLI commands:
  `stress`, `raw`, `comp`, `stress_mix`, periodic 10 mps fetch cycles, ART
  cycles, `probe`, `drv`, and `settings`.
- Added summary JSON/Markdown/environment fields for the new timing and soak
  options.
- Added parser tests for duration-soak planning and the runner self-test.

Reason for change: the existing runner supported count-based soak through
`--soak-count`, but not an exact duration-bound 8-hour run.

## Exact Commands And Results

### Build, Upload, And Serial Access

| Command | Expected result | Observed result | Elapsed | Result | Notes |
| --- | --- | --- | ---: | --- | --- |
| `pio run -e esp32s3dev` | Firmware builds | Build succeeded | 5.15 s | PASS | RAM 6.9%, flash 30.4% |
| `pio run -e esp32s3dev -t upload --upload-port COM20` | Flash firmware | `PermissionError(13, Access is denied)` | 1.82 s | FAIL/BLOCKED | COM20 locked |
| `pio run -e esp32s3dev -t upload --upload-port COM20` after 30 s wait | Flash firmware | Same access-denied failure | 1.58 s after wait | FAIL/BLOCKED | COM20 still locked |
| Pyserial open probe for `COM20` | Open port | `SerialException("could not open port 'COM20': PermissionError(13, 'Access is denied.', None, 5)")` | <1 s | FAIL/BLOCKED | Confirms OS handle problem outside esptool |

COM20 upload failure excerpt:

```text
A fatal error occurred: Could not open COM20, the port is busy or doesn't exist.
(could not open port 'COM20': PermissionError(13, 'Access is denied.', None, 5))
```

### HIL Runner Dry Run

```powershell
python tools\run_i2c_hil.py --dry-run --expect-address 0x44 --board esp32s3dev --target-name SHT3x-COM20 --operator codex --include-clock-stretch --include-alert-write --include-all-periodic-rates --include-soak --soak-duration-s 28800 --soak-chunk-count 500 --benchmark-count 50
```

Result: PASS as a parser/plan dry run only. Final dry-run verdict was
`INCOMPLETE`, as expected for no hardware execution.

Artifacts:

```text
hil_logs\i2c_20260626T192026Z\summary.md
hil_logs\i2c_20260626T192026Z\summary.json
hil_logs\i2c_20260626T192026Z\serial_transcript.txt
hil_logs\i2c_20260626T192026Z\environment.txt
```

### Software Verification

| Command | Observed result | Result |
| --- | --- | --- |
| `python tools\run_i2c_hil.py --parser-self-test` | `run_i2c_hil parser self-test: OK` | PASS |
| `python tools\test_run_i2c_hil_parser.py` | `test_run_i2c_hil_parser: OK` | PASS |
| `python tools\check_hil_contract.py` | `check_hil_contract: OK` | PASS |
| `python tools\check_core_timing_guard.py` | `Core timing guard PASSED` | PASS |
| `python tools\check_cli_contract.py` | `CLI contract PASSED` | PASS |
| `python tools\check_idf_example_contract.py` | `IDF example contract PASSED` | PASS |
| `pio test -e native` | 85 test cases, 85 succeeded | PASS |
| `pio run -e esp32s3dev` | Build succeeded | PASS |
| `pio run -e esp32s2dev` | Build succeeded | PASS |
| `python scripts\generate_version.py check` | Version header up to date | PASS |
| `pio pkg pack` | `SHT3x-1.6.0.tar.gz` created | PASS |
| Remove generated package archive | Archive removed | PASS |
| `git diff --check` | No whitespace errors | PASS |

## Hardware HIL Summary

Hardware HIL counts:

| Classification | Count |
| --- | ---: |
| PASS | 0 |
| FAIL/BLOCKED | 1 |
| UNKNOWN | 0 |
| NOT RUN | 27 |
| NOT APPLICABLE | 0 |

Detailed hardware matrix:

| Test id | Feature area | Command or step | Expected result | Observed result | Elapsed | Result | Notes |
| --- | --- | --- | --- | --- | ---: | --- | --- |
| HIL-001 | Upload | `pio run -e esp32s3dev -t upload --upload-port COM20` | Firmware flashed | COM20 access denied | 1.82 s, 1.58 s retry | FAIL/BLOCKED | Port locked before esptool sync |
| HIL-002 | Boot transcript | Serial open at 115200 | SHT3x boot banner and prompt | Not reachable | 0 s | NOT RUN | Blocked by HIL-001 |
| HIL-003 | CLI responsiveness | `version`, prompt detection | Version printed | Not run | 0 s | NOT RUN | No serial control |
| HIL-004 | Bus discovery | `scan` | Address `0x44` seen | Not run | 0 s | NOT RUN | No serial control |
| HIL-005 | Probe | `probe` | SHT3x status-frame path OK | Not run | 0 s | NOT RUN | No serial control |
| HIL-006 | Settings | `settings` | Config snapshot | Not run | 0 s | NOT RUN | No serial control |
| HIL-007 | Health | `drv` | READY, online, zero unexplained failures | Not run | 0 s | NOT RUN | No serial control |
| HIL-008 | Status | `status`, `status_raw` | CRC-valid status | Not run | 0 s | NOT RUN | No serial control |
| HIL-009 | Single-shot | `single low/medium/high` | Plausible T/RH | Not run | 0 s | NOT RUN | No serial control |
| HIL-010 | Cached raw/fixed samples | `raw`, `comp` | Parsed cached values | Not run | 0 s | NOT RUN | No serial control |
| HIL-011 | Serial/EIC | `serial nostretch`, `serial stretch` | CRC-valid serial | Not run | 0 s | NOT RUN | No serial control |
| HIL-012 | Clock stretching | `stretch 1`, `read`, restore | Measurement succeeds or unsupported is visible | Not run | 0 s | NOT RUN | No serial control |
| HIL-013 | Periodic modes | 0.5/1/2/4/10 mps start/fetch/stop | Fetch succeeds and stop succeeds | Not run | 0 s | NOT RUN | No serial control |
| HIL-014 | ART mode | `art start/fetch/stop` | Succeeds or unsupported is visible | Not run | 0 s | NOT RUN | No serial control |
| HIL-015 | Status restore | `status_restore` in periodic mode | Mode interrupted/restored fields valid | Not run | 0 s | NOT RUN | No serial control |
| HIL-016 | Heater status | `heater status` | Heater off or visible state | Not run | 0 s | NOT RUN | No serial control |
| HIL-017 | Heater enable | `heater on/off` | Controlled thermal behavior | Not run | 0 s | NOT RUN | No controlled thermal fixture verified |
| HIL-018 | Alert read/encode/decode | `alert show`, app-note vectors | Raw limits and vectors parse | Not run | 0 s | NOT RUN | No serial control |
| HIL-019 | Alert write/readback | `alert set/read`, cleanup | Software-visible round trip | Not run | 0 s | NOT RUN | No serial control |
| HIL-020 | Physical ALERT pin | GPIO/logic capture | Pin transition evidence | Not run | 0 s | NOT RUN | Missing GPIO or logic-analyzer fixture |
| HIL-021 | Reset/recovery | `selftest`, `reset`, `restore`, `iface_reset`, `recover` | Bounded visible status | Not run | 0 s | NOT RUN | No serial control |
| HIL-022 | General-call reset | `greset` | Isolated-bus reset | Not run | 0 s | NOT RUN | Not safe without explicit isolated bus |
| HIL-023 | Wrong address | Alternate config or fixture | Precise failure | Not run | 0 s | NOT RUN | No serial control |
| HIL-024 | Fault injection | NACK/timeout/unplug/CRC fault | Precise failure classification | Not run | 0 s | NOT RUN | Missing safe fault fixture |
| HIL-025 | Benchmark | `stress 50` | Rate and latency summary | Not run | 0 s | NOT RUN | No serial control |
| HIL-026 | Count stress | `stress`, `stress_mix` | Bounded stress summary | Not run | 0 s | NOT RUN | No serial control |
| HIL-027 | 8-hour soak | `--include-soak --soak-duration-s 28800` | 8-hour command mix completes | Not run | 0 s | NOT RUN | COM20 blocked |
| HIL-028 | Post-soak health | `drv`, `settings` | READY, zero unexplained failures | Not run | 0 s | NOT RUN | No soak |

## Timing And Sampling Results

No hardware timing or sampling result was collected.

Software/build timing observed:

| Area | Result |
| --- | --- |
| ESP32-S3 firmware build | 5.15 s |
| ESP32-S2 firmware build | 4.66 s |
| Native test run | 1.06 s |
| COM20 upload attempt | Failed before sync in 1.82 s and 1.58 s retry |
| Dry-run HIL plan | Completed in <1 s, no hardware I/O |

## 8-Hour Soak Summary

| Field | Value |
| --- | --- |
| Requested duration | 8 hours, `28800` seconds |
| Start time | N/A |
| End time | N/A |
| Actual hardware duration | 0 seconds |
| Command mix | Planned by dry run only |
| Samples | 0 |
| Errors | N/A |
| Reset/recovery count | 0 |
| Worst observed latency | N/A |
| Health-state changes | N/A |
| Result | NOT RUN |
| Reason | COM20 locked, no firmware upload or serial CLI |

The runner now supports a duration-bound soak command:

```powershell
python tools\run_i2c_hil.py --port COM20 --baud 115200 --expect-address 0x44 --board esp32s3dev --target-name SHT3x-COM20 --operator codex --include-clock-stretch --include-alert-write --include-all-periodic-rates --include-soak --soak-duration-s 28800 --soak-chunk-count 500 --benchmark-count 50
```

Add `--include-destructive` only when status clearing, soft reset, restore, and
interface reset are acceptable for the fixture. Do not add
`--include-bus-wide-reset` unless the bus is isolated.

## Failures, Anomalies, And Limitations

- `COM20` is present but locked by an external process or driver state.
- No SHT3x boot banner, firmware version, I2C address, CRC-valid status,
  measurement, serial/EIC, or health output was captured.
- Other visible ports are either locked by unrelated HIL runners or did not
  present the SHT3x CLI. They were not flashed to avoid overwriting unrelated
  boards.
- No humidity accuracy, physical ALERT pin, fault-injection, bus recovery,
  ESP32-S2 hardware, power-cycle, or long-soak claim is supported by this run.

## Audit Findings

### High - COM20 hardware validation is blocked by serial-port ownership

- File/area: HIL fixture / host serial ownership
- Evidence: both PlatformIO upload and pyserial open fail with
  `PermissionError(13, Access is denied)`.
- Current behavior: the board cannot be flashed or controlled through COM20.
- Risk: any hardware validation claim would be false.
- Simplest safe fix: close the process holding COM20, usually an IDE serial
  monitor or PlatformIO monitor, then rerun upload and HIL.
- Required native tests: none.
- Required HIL regression: full safe HIL run plus 8-hour duration soak on COM20.
- Implemented in this pass: no, external fixture/host state.

### Medium - HIL runner lacked exact duration-bound soak mode

- File: `tools/run_i2c_hil.py`
- Evidence: prior report and code inspection showed only count-based
  `--soak-count`.
- Current behavior before this pass: an exact 8-hour soak required manual count
  estimation or an external wrapper.
- Risk: long-soak evidence could be under-run, over-run, or hard to reproduce.
- Simplest safe fix: add a bounded `--soak-duration-s` loop using existing CLI
  commands and finite per-command timeouts.
- Required native tests: parser/planner tests for duration mode.
- Required HIL regression: short duration smoke, then 8-hour soak on hardware.
- Implemented in this pass: yes, with parser tests.

### Low - HIL runner could not self-test through its own entrypoint

- File: `tools/run_i2c_hil.py`
- Evidence: parser checks existed in `tools/test_run_i2c_hil_parser.py`, but
  the runner itself had no `--parser-self-test`.
- Current behavior before this pass: users had to know the separate test file.
- Risk: field operators can skip parser validation before hardware time.
- Simplest safe fix: add a small `--parser-self-test` mode.
- Required native tests: execute `python tools\run_i2c_hil.py --parser-self-test`.
- Required HIL regression: none; this is host-side parser validation.
- Implemented in this pass: yes.

### Low - COM20 lock owner was not identifiable from process command lines

- File/area: host validation workflow
- Evidence: process query showed HIL runners for COM5 and COM27, but no visible
  command line naming COM20.
- Current behavior: the operator cannot resolve the lock from this report alone.
- Risk: repeated upload attempts waste test time.
- Simplest safe fix: use Sysinternals Handle/Process Explorer, Windows Resource
  Monitor, or close active VS Code/PlatformIO serial monitors before rerun.
- Required native tests: none.
- Required HIL regression: retry COM20 upload after releasing the handle.
- Implemented in this pass: no external process was stopped.

## Files Changed

- `tools/run_i2c_hil.py`
- `tools/test_run_i2c_hil_parser.py`
- `docs/reports/hil-validation-COM20-20260626.md`

No core driver or public API files were changed.

## Final Verification Commands

The following commands passed before this report was written:

```powershell
python tools\run_i2c_hil.py --parser-self-test
python tools\test_run_i2c_hil_parser.py
python tools\check_hil_contract.py
python tools\check_core_timing_guard.py
python tools\check_cli_contract.py
python tools\check_idf_example_contract.py
python scripts\generate_version.py check
pio test -e native
pio run -e esp32s3dev
pio run -e esp32s2dev
pio pkg pack
git diff --check
```

`pio pkg pack` generated `SHT3x-1.6.0.tar.gz`; the archive was removed after
verification.

## Rerun Procedure After COM20 Is Released

```powershell
pio run -e esp32s3dev -t upload --upload-port COM20
python tools\run_i2c_hil.py --port COM20 --baud 115200 --expect-address 0x44 --board esp32s3dev --target-name SHT3x-COM20 --operator codex --include-clock-stretch --include-alert-write --include-all-periodic-rates --benchmark-count 50 --include-soak --soak-duration-s 28800 --soak-chunk-count 500 --reconnect-attempts 3 --boot-settle-s 2
```

After the run, inspect:

```text
hil_logs\i2c_<timestamp>\summary.md
hil_logs\i2c_<timestamp>\summary.json
hil_logs\i2c_<timestamp>\serial_transcript.txt
hil_logs\i2c_<timestamp>\environment.txt
```

Only then update this report with real hardware counts, timing, sample rates,
health changes, and the final 8-hour soak result.
