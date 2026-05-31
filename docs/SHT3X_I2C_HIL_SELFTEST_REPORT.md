# SHT3x I2C HIL Self-Test Report

Date: 2026-05-31
Branch: `hardening/sht3x-industry-readiness`
Start commit: `46855ce445b3124fda693037eb805455cc1813e5`
New branch created: No

Auditor-facing verdict: `software-prepared only`

No physical HIL validation was performed.

## Files Changed

- `.gitignore`
- `tools/run_i2c_hil.py`
- `tools/check_hil_contract.py`
- `docs/SHT3X_I2C_HIL_RUNBOOK.md`
- `docs/SHT3X_I2C_HIL_TARGET_TEMPLATE.md`
- `docs/SHT3X_I2C_HIL_SELFTEST_REPORT.md`
- `docs/SHT3X_HARDWARE_VALIDATION_MATRIX.md`
- `README.md`
- `CHANGELOG.md`

## Default Command Sequence

```text
version
help
scan
probe
settings
drv
status
status_raw
single high
raw
comp
serial nostretch
heater status
alert show
status_restore
periodic start 1 high
periodic fetch
periodic stop
drv
```

## Safety Exclusions

The default runner sequence excludes `clear_status`, heater enable, alert
writes/disable, raw command writes, resets, recovery, fault injection, and soak
or stress loops. Those remain opt-in/manual because they can change sensor
state, destroy status evidence, perturb measurements, affect a shared bus, or
require a safe fixture.

## Checks Run

| Check | Result |
| --- | --- |
| `python -m py_compile tools/run_i2c_hil.py tools/check_hil_contract.py` | Pass |
| `python tools/run_i2c_hil.py --dry-run` | Pass; final runner verdict `INCOMPLETE`; generated ignored log directory `hil_logs/i2c_20260531T132137Z` |
| `python tools/check_hil_contract.py` | Pass |
| `python tools/check_core_timing_guard.py` | Pass |
| `python tools/check_cli_contract.py` | Pass |
| `python tools/check_idf_example_contract.py` | Pass |
| `python scripts/generate_version.py check` | Pass |
| `python -m platformio --version` | PlatformIO Core 6.1.19 |
| `python -m platformio test -e native` | Pass, 70/70 |
| `python -m platformio run -e esp32s3dev` | Pass |
| `python -m platformio run -e esp32s2dev` | Pass |
| `python -m platformio pkg pack` | Pass, package tarball removed |
| `git diff --check` | Pass |
| `idf.py --version` | Not run; PowerShell reported `CommandNotFoundException` |
| `gh --version` | Not run; PowerShell reported `CommandNotFoundException` |

## Dry-Run Result

The runner dry-run exercised command planning and artifact generation only. It
created `hil_logs/i2c_20260531T132137Z` and returned final verdict
`INCOMPLETE` because no serial hardware was used.

## Hardware Run Result

No physical HIL validation was performed. The operator must run the hardware
command below on a wired ESP32-S2/S3 plus SHT3x target before making any
hardware result claim.

## PR And CI Status

Local PlatformIO and guard checks were run. GitHub PR/CI status was not checked
because `gh` was unavailable in this shell. Manual UI steps:

1. Open the repository on GitHub.
2. Select branch `hardening/sht3x-industry-readiness`.
3. Confirm the pushed commit and inspect all required checks.
4. Attach the runner log directory from a real hardware run to the audit record.

## Operator HIL Command

```bash
python tools/run_i2c_hil.py --port COMx --baud 115200 --out hil_logs
```

## Remaining Blockers

- Real serial HIL run on ESP32-S2 and/or ESP32-S3 hardware.
- Completed target template with board, wiring, supply, pull-up, and sensor
  module details.
- ALERT pin evidence, if ALERT behavior is claimed.
- Fault-injection evidence, if NACK/timeout/CRC recovery claims are required.
- Humidity/temperature fixture evidence, if accuracy or production screening is
  claimed.
