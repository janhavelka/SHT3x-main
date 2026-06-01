# SHT3x I2C HIL Self-Test Report

Date: 2026-05-31
Branch: `main`
Start commit: `46855ce445b3124fda693037eb805455cc1813e5`
New branch created: No

Auditor-facing verdict: `software-prepared only`

No physical HIL validation was performed.

## Files Changed

- `README.md`
- `docs/HARDWARE_VALIDATION.md`
- `docs/README.md`
- `docs/SHT3X_HARDWARE_VALIDATION_MATRIX.md`
- `docs/SHT3X_HIL_RUNBOOK.md`
- `docs/SHT3X_I2C_HIL_RUNBOOK.md`
- `docs/SHT3X_I2C_HIL_SELFTEST_REPORT.md`
- `docs/SHT3X_I2C_HIL_TARGET_TEMPLATE.md`
- `docs/SHT3X_RELEASE_READINESS_GAPS_FIX_REPORT.md`
- `tools/check_hil_contract.py`
- `tools/run_i2c_hil.py`
- `tools/run_sht3x_hil.py`
- `tools/test_run_i2c_hil_parser.py`

The default command sequence, safety exclusions, and operator command live in
`docs/SHT3X_I2C_HIL_RUNBOOK.md`. Evidence blockers and hardware status live in
`docs/HARDWARE_VALIDATION.md`.

## Checks Run

| Check | Result |
| --- | --- |
| `python -m py_compile tools/run_i2c_hil.py tools/run_sht3x_hil.py tools/check_hil_contract.py` | Pass |
| `python tools/run_sht3x_hil.py --dry-run` | Pass; final runner verdict `INCOMPLETE`; creates an ignored `hil_logs/i2c_<timestamp>` directory |
| `python tools/test_run_i2c_hil_parser.py` | Pass |
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

The runner dry-run exercises command planning and artifact generation only. It
creates an ignored `hil_logs/i2c_<timestamp>` directory and returns final
verdict `INCOMPLETE` because no serial hardware is used.

## Hardware Run Result

No physical HIL validation was performed. The operator must run the hardware
runner on a wired ESP32-S2/S3 plus SHT3x target before making any hardware
result claim.

## PR And CI Status

Local PlatformIO and guard checks were run. GitHub PR/CI status was not checked
because `gh` was unavailable in this shell. Manual UI steps:

1. Open the repository on GitHub.
2. Select branch `hardening/sht3x-industry-readiness`.
3. Confirm the pushed commit and inspect all required checks.
4. Attach the runner log directory from a real hardware run to the audit record.
