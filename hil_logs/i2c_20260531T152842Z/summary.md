# SHT3x I2C HIL Summary

Date/time UTC: 20260531T153134Z

Branch: `hardening/sht3x-industry-readiness`

Commit: `8661a38cc70e629cd337ac45c42a1885aefb0cfc`

Worktree state: `clean`

Serial port: `COM17`

Baud rate: `115200`

Dry run: `False`

Firmware/framework: `see version/help transcript if reported`

I2C address: `configured by target firmware; expected SHT3x default is 0x44 unless ADDR high uses 0x45`

Device/module info: `operator target template and transcript`

Initial serial output is captured in `serial_transcript.txt` before the first command.

Runner boundary: host controls the repository ESP32 diagnostic CLI over serial. ACK alone is not chip identity.

## Configured Command Sequence

- `version`
- `help`
- `scan`
- `probe`
- `settings`
- `drv`
- `status`
- `status_raw`
- `single high`
- `raw`
- `comp`
- `serial nostretch`
- `heater status`
- `alert show`
- `status_restore`
- `periodic start 1 high`
- `periodic fetch`
- `periodic stop`
- `drv`

## Per-Command Results

| Command | Purpose | Serial result | Operator result | Completion | Elapsed s | Notes |
| --- | --- | --- | --- | --- | ---: | --- |
| `version` | Record firmware/library version. | `TIMEOUT` | `N/A` | timeout | 8.037 |  |
| `help` | Capture CLI command surface. | `TIMEOUT` | `N/A` | timeout | 8.057 |  |
| `scan` | I2C address ACK scan. | `TIMEOUT` | `N/A` | timeout | 8.036 | ACK alone is not chip identity. |
| `probe` | Driver probe using SHT3x status-frame path. | `TIMEOUT` | `N/A` | timeout | 8.035 |  |
| `settings` | Record configuration and state. | `TIMEOUT` | `N/A` | timeout | 8.04 |  |
| `drv` | Record health and last-error state. | `TIMEOUT` | `N/A` | timeout | 8.011 |  |
| `status` | Read parsed status bits without clearing them. | `TIMEOUT` | `N/A` | timeout | 8.012 |  |
| `status_raw` | Read raw status word. | `TIMEOUT` | `N/A` | timeout | 8.008 |  |
| `single high` | Run high-repeatability no-stretch measurement. | `TIMEOUT` | `N/A` | timeout | 12.014 |  |
| `raw` | Read cached raw sample. | `TIMEOUT` | `N/A` | timeout | 8.028 |  |
| `comp` | Read cached compensated sample. | `TIMEOUT` | `N/A` | timeout | 8.009 |  |
| `serial nostretch` | Read CRC-protected SHT3x serial/EIC value. | `TIMEOUT` | `N/A` | timeout | 8.038 |  |
| `heater status` | Read heater state without enabling heater. | `TIMEOUT` | `N/A` | timeout | 8.034 |  |
| `alert show` | Read alert-limit configuration while idle. | `TIMEOUT` | `N/A` | timeout | 12.071 |  |
| `status_restore` | Exercise readStatusWithModeRestore() and log sub-statuses. | `TIMEOUT` | `N/A` | timeout | 12.043 |  |
| `periodic start 1 high` | Start volatile 1 mps periodic acquisition. | `TIMEOUT` | `N/A` | timeout | 8.024 |  |
| `periodic fetch` | Fetch one periodic sample. | `TIMEOUT` | `N/A` | timeout | 12.06 |  |
| `periodic stop` | Stop periodic acquisition. | `TIMEOUT` | `N/A` | timeout | 8.004 |  |
| `drv` | Final health snapshot. | `TIMEOUT` | `N/A` | timeout | 8.05 |  |

## Skipped / Manual Checks

- `selftest`: Arduino selftest performs softReset(). (--include-destructive)
- `recover`:  (--include-destructive)
- `stress 10`:  (--include-soak)
- `ALERT threshold procedure`: Requires GPIO or logic-analyzer evidence. (--include-output-tests)
- `fault/unplug/CRC-injection procedure`: Requires a safe jig or interposer. (--include-fault-tests)

## Final Verdict

`FAIL`

A PASS verdict is limited to the automated serial command sequence. It does not prove humidity accuracy, physical ALERT pin behavior, fault injection, long soak stability, or production readiness unless those groups were run with evidence.

## Artifacts

- `hil_logs\i2c_20260531T152842Z\serial_transcript.txt`
- `hil_logs\i2c_20260531T152842Z\summary.json`
- `hil_logs\i2c_20260531T152842Z\operator_checklist.md`
