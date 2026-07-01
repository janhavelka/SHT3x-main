# SHT3x HIL Validation Report - COM20 - 2026-06-30

## Verdict

Automated hardware validation on `COM20` exercised the ESP32-S3 Arduino
diagnostic firmware against an SHT3x responding at `0x44`.

Short smoke, destructive/reset, clock-stretch, alert write/readback, all
periodic rates, ART, parser, package, and build checks passed. The requested
16-hour HIL soak did **not** complete: every long attempt eventually timed out
on the diagnostic CLI `stress 500` command. The last driver-health snapshot
before the best final-commit timeout was still `READY`, `online=true`,
`consecutive_failures=0`, `total_success=862912`, and `total_failures=0`.

2026-07-01 release addendum: the v1.6.1 release criterion is reduced to SHT3x
core firmware functionality and I2C stability. Under that criterion, the
diagnostic CLI timeout is not treated as a SHT3x core/I2C release blocker. This
report still does not claim an uninterrupted 16-hour HIL transcript, host CLI
liveness, physical ALERT pin behavior, humidity accuracy, fault injection, or
production fixture validation.

## Repository And Environment

| Field | Value |
| --- | --- |
| Repository | `C:\Users\Honza\Documents\Projects\SHT3x-main` |
| Branch | `main` |
| Final firmware commit tested | `56ad12e98d43` |
| Firmware library string | `1.6.1 (56ad12e98d43, Jun 30 2026 11:41:13, clean)` |
| Report updated | `2026-07-01` |
| Host OS | Microsoft Windows 11 Education, OS version `10.0.26200` |
| Python | `Python 3.12.10` |
| PlatformIO | `PlatformIO Core, version 6.1.18` |
| Target environment | `esp32s3dev` |
| Target framework | Arduino |
| Serial port | `COM20` |
| Baud rate | `115200` |
| I2C pins from firmware config | `SDA=8`, `SCL=9` |
| Expected SHT3x address | `0x44` |
| I2C addresses observed | `0x3C`, `0x44`, `0x50`, `0x51` |
| SHT3x serial/EIC | `0x29075EB0` |

PlatformIO printed the usual obsolete-core warning for local Core `6.1.18`.

## Hardware Safety Notes

The user allowed destructive testing on `COM20`. The runner saw other devices
on the same I2C bus (`0x3C`, `0x50`, `0x51`), so bus-wide general-call reset was
not run. Physical ALERT pin assertion, logic-analyzer capture,
hot-unplug/fault injection, calibrated humidity/temperature accuracy, address
`0x45`, ESP32-S2 hardware, and pure ESP-IDF hardware execution were not
validated.

## Exact Commands

Final firmware build and upload:

```powershell
pio run -e esp32s3dev
pio run -e esp32s3dev --target upload --upload-port COM20
```

Final short HIL smoke:

```powershell
python tools\run_i2c_hil.py --port COM20 --baud 115200 --expect-address 0x44 --board esp32s3dev --target-name SHT3x-COM20-v1.6.1-stressmix-smoke-56ad12e --operator codex --include-soak --soak-count 500 --benchmark-count 0 --boot-settle-s 2 --reconnect-attempts 3 --reconnect-delay-s 1
```

Final 16-hour HIL attempt:

```powershell
python tools\run_i2c_hil.py --port COM20 --baud 115200 --expect-address 0x44 --board esp32s3dev --target-name SHT3x-COM20-v1.6.1-16h-56ad12e --operator codex --include-destructive --include-clock-stretch --include-alert-write --include-all-periodic-rates --include-soak --soak-duration-s 57600 --soak-chunk-count 500 --soak-recover-every 0 --benchmark-count 100 --boot-settle-s 2 --reconnect-attempts 3 --reconnect-delay-s 1
```

Software verification:

```powershell
python tools\test_run_i2c_hil_parser.py
python tools\run_i2c_hil.py --parser-self-test
python tools\check_cli_contract.py
python tools\check_hil_contract.py
python tools\check_core_timing_guard.py
python tools\check_idf_example_contract.py
pio test -e native
pio run -e esp32s3dev
pio run -e esp32s2dev
pio pkg pack
git diff --check
```

## HIL Results

| Run | Commit | Artifact directory | Result | Key metrics |
| --- | --- | --- | --- | --- |
| Smoke after final serial-output change | `56ad12e` | `hil_logs\i2c_20260630T094139Z` | `PASS` | 47/47 `PASS`; final health `READY`, `consecutive_failures=0`, `total_success=1348`, `total_failures=0`; `stress 500` = 500/0; `stress_mix 250` = 250/0 |
| 16-hour attempt, final commit | `56ad12e` | `hil_logs\i2c_20260630T094228Z` | `FAIL` | 5765 `PASS`, 1 `SKIP_UNSUPPORTED`, 1 timeout at `duration-cycle=670` on `stress 500`; 669 successful `stress 500` chunks and 669 successful `stress_mix 250` chunks; last health `READY`, `consecutive_failures=0`, `total_success=862912`, `total_failures=0`; observed temp `26.52..27.72 C`, RH `44.80..48.17 %` |
| 16-hour attempt before bytewise serial writer | `6bff3be` | `hil_logs\i2c_20260630T083657Z` | `FAIL` | 920 `PASS`, 1 `SKIP_UNSUPPORTED`, 1 timeout at `duration-cycle=100` on `stress 500`; last health `READY`, `total_success=127992`, `total_failures=0` |
| 16-hour attempt before final serial-output changes | `7ca2cfc` | `hil_logs\i2c_20260630T050155Z` | `FAIL` | 7277 `PASS`, 1 `SKIP_UNSUPPORTED`, 1 timeout at `duration-cycle=848` on `stress 500`; last health `READY`, `total_success=1092412`, `total_failures=0` |

All listed 16-hour attempts failed because the serial transcript stopped at a
new `stress 500` command. The final transcript tail for `56ad12e` ends with:

```text
===== COMMAND: stress 500 =====
s
===== RESULT: FAIL (timeout) =====
===== NOTES: duration-cycle=670 timeout =====
```

The preceding `drv` command reported `READY`, `online=yes`,
`Consecutive failures: 0`, `Total success: 862912`, and `Total failures: 0`.

## Sensitive Metrics Watched

| Metric | Observation |
| --- | --- |
| Bus identity | Scan saw `0x3C`, `0x44`, `0x50`, `0x51`; SHT3x probe and CRC-protected serial path confirmed SHT3x at `0x44` |
| Sensor serial/EIC | `0x29075EB0`, read in no-stretch and clock-stretch serial paths |
| Driver health | Final-commit long run stayed `READY` with `online=true`, `consecutive_failures=0`, `total_failures=0` through the last completed health snapshot |
| Driver counters | Final-commit long run reached `total_success=862912`, `total_failures=0` before the serial-output timeout |
| Stress totals | Final-commit long run completed 669 `stress 500` chunks and 669 `stress_mix 250` chunks with exact requested totals and zero failures |
| Status register | Status paths and clear/status cycle passed; no command/checksum status fault was recorded in the final health snapshot |
| Measurement plausibility | Final-commit long run observed `26.52..27.72 C`, `44.80..48.17 %RH` in parsed measurement/compensated rows |
| Periodic modes | Default 0.5/1/2 mps and opt-in 4/10 mps periodic fetch paths passed before the soak timeout |
| ART mode | `art start`, `art fetch`, `art stop` passed repeatedly before the soak timeout |
| Clock stretching | Clock-stretch single-shot read and serial read passed in the opt-in group |
| Alert limits | Encode/decode vectors and write/readback/cleanup paths passed |
| Recovery/reset | `selftest`, `recover`, soft `reset`, and `restore` passed; `iface_reset` was skipped as unsupported |
| Host/serial liveness | Not cleared: old multi-command Arduino diagnostics repeatedly timed out on `stress 500`; not treated as a SHT3x core/I2C blocker under the reduced v1.6.1 criterion |

## Audit Findings And Fixes

- Fixed release metadata for v1.6.1 and regenerated `Version.h`.
- Hardened HIL completion parsing so multi-line `drv`/`settings` snapshots
  complete only when validators pass.
- Forced mixed stress measurements onto no-stretch mode and restored
  no-stretch after mixed stress.
- Taught the parser to prefer final stress summaries over progress lines.
- Restored previous Wire timeouts in example transport helpers and mapped
  late-returning I2C calls to `I2C_TIMEOUT`.
- Suppressed verbose stress progress by default and emitted compact stress
  summaries first.
- Rejected incomplete or failing stress summaries in the HIL runner.
- Changed Arduino diagnostic `stress N` to run synchronously, avoiding stale
  async summaries after the prompt.
- Added bounded Arduino diagnostic serial writes and then changed them to
  bytewise writes to reduce USB CDC partial-output stalls.
- Added a low-USB Arduino diagnostic `i2c_soak <seconds>` command and changed
  duration-based HIL soaks to use that single firmware-side measurement loop
  instead of thousands of host-issued serial commands.

The serial-output changes reduced early serial-output failures but did not make
the old multi-command 16-hour serial diagnostic path reliable enough for
uninterrupted-transcript evidence. Future duration soaks should use
`tools\run_i2c_hil.py --include-soak --soak-duration-s <seconds>`, which now
routes to `i2c_soak <seconds>` and emits one compact final summary.

## Software Verification Results

| Command | Result |
| --- | --- |
| `python tools\test_run_i2c_hil_parser.py` | PASS |
| `python tools\run_i2c_hil.py --parser-self-test` | PASS |
| `python tools\run_i2c_hil.py --dry-run --include-soak --soak-duration-s 57600 --expect-address 0x44 --board esp32s3dev --target-name dry-low-usb --operator codex` | PASS as dry-run plan check; generated temporary dry-run artifacts were removed |
| `python tools\check_cli_contract.py` | PASS |
| `python tools\check_hil_contract.py` | PASS |
| `python tools\check_core_timing_guard.py` | PASS |
| `python tools\check_idf_example_contract.py` | PASS |
| `pio test -e native` | PASS, 85/85 tests |
| `pio run -e esp32s3dev` | PASS, RAM 6.9%, flash 30.5% |
| `pio run -e esp32s2dev` | PASS, RAM 11.1%, flash 28.4% |
| `pio pkg pack` | PASS; generated `SHT3x-1.6.1.tar.gz` was removed after verification |
| `git diff --check` | PASS |

## Not Run / Not Proven

- Uninterrupted 16-hour HIL transcript: not completed.
- Physical ALERT pin behavior: not validated without GPIO or logic-analyzer
  capture.
- Fault injection: no safe unplug, CRC corruption, forced NACK, stuck bus, or
  interposer test was run.
- Humidity/temperature accuracy: no calibrated reference was used.
- ESP32-S2 hardware: only the `esp32s2dev` build was verified.
- Address `0x45`: not tested.
- General-call reset: intentionally not run because other devices were present
  on the I2C bus.
- Pure ESP-IDF execution on hardware: repository contract check passed, but no
  `idf.py` hardware run was performed.

## Release Decision

Do not publish v1.6.1 as fully 16-hour hardware-validated from this evidence.
With the 2026-07-01 reduced criterion, this report supports release only for the
core SHT3x firmware functionality and I2C-stability scope described above.
Uninterrupted 16-hour HIL, host CLI liveness, physical ALERT pin behavior,
humidity accuracy, fault injection, ESP32-S2 hardware execution, and address
`0x45` remain outside the release claim.

## Artifacts

- `hil_logs\i2c_20260630T094139Z\summary.md`
- `hil_logs\i2c_20260630T094139Z\summary.json`
- `hil_logs\i2c_20260630T094139Z\serial_transcript.txt`
- `hil_logs\i2c_20260630T094228Z\summary.md`
- `hil_logs\i2c_20260630T094228Z\summary.json`
- `hil_logs\i2c_20260630T094228Z\serial_transcript.txt`
- `hil_logs\i2c_20260630T094228Z\progress.jsonl`
- `hil_logs\i2c_20260630T083657Z\summary.md`
- `hil_logs\i2c_20260630T050155Z\summary.md`
