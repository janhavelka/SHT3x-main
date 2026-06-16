# SHT3x I2C HIL Summary

Date/time UTC: 20260531T155937Z

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
| `version` | Record firmware/library version. | `PASS` | `N/A` | completion-token+idle | 0.454 |  |
| `help` | Capture CLI command surface. | `PASS` | `N/A` | completion-token+idle | 0.486 |  |
| `scan` | I2C address ACK scan. | `PASS` | `N/A` | completion-token+idle | 0.529 | ACK alone is not chip identity. |
| `probe` | Driver probe using SHT3x status-frame path. | `PASS` | `N/A` | completion-token+idle | 0.45 |  |
| `settings` | Record configuration and state. | `PASS` | `N/A` | completion-token+idle | 0.452 |  |
| `drv` | Record health and last-error state. | `PASS` | `N/A` | completion-token+idle | 0.452 |  |
| `status` | Read parsed status bits without clearing them. | `PASS` | `N/A` | completion-token+idle | 0.456 |  |
| `status_raw` | Read raw status word. | `PASS` | `N/A` | completion-token+idle | 0.451 |  |
| `single high` | Run high-repeatability no-stretch measurement. | `PASS` | `N/A` | completion-token+idle | 0.454 |  |
| `raw` | Read cached raw sample. | `PASS` | `N/A` | completion-token+idle | 0.453 |  |
| `comp` | Read cached compensated sample. | `PASS` | `N/A` | completion-token+idle | 0.45 |  |
| `serial nostretch` | Read CRC-protected SHT3x serial/EIC value. | `PASS` | `N/A` | completion-token+idle | 0.456 |  |
| `heater status` | Read heater state without enabling heater. | `PASS` | `N/A` | completion-token+idle | 0.453 |  |
| `alert show` | Read alert-limit configuration while idle. | `PASS` | `N/A` | completion-token+idle | 0.453 |  |
| `status_restore` | Exercise readStatusWithModeRestore() and log sub-statuses. | `PASS` | `N/A` | completion-token+idle | 0.449 |  |
| `periodic start 1 high` | Start volatile 1 mps periodic acquisition. | `PASS` | `N/A` | completion-token+idle | 0.45 |  |
| `periodic fetch` | Fetch one periodic sample. | `PASS` | `N/A` | completion-token+idle | 0.45 |  |
| `periodic stop` | Stop periodic acquisition. | `PASS` | `N/A` | completion-token+idle | 0.45 |  |
| `drv` | Final health snapshot. | `PASS` | `N/A` | completion-token+idle | 0.45 |  |

## Skipped / Manual Checks

- `selftest`: Arduino selftest performs softReset(). (--include-destructive)
- `recover`:  (--include-destructive)
- `stress 10`:  (--include-soak)
- `ALERT threshold procedure`: Requires GPIO or logic-analyzer evidence. (--include-output-tests)
- `fault/unplug/CRC-injection procedure`: Requires a safe jig or interposer. (--include-fault-tests)

## Final Verdict

`PASS`

A PASS verdict is limited to the automated serial command sequence. It does not prove humidity accuracy, physical ALERT pin behavior, fault injection, long soak stability, or production readiness unless those groups were run with evidence.

## Artifacts

- `hil_logs\i2c_20260531T155925Z\serial_transcript.txt`
- `hil_logs\i2c_20260531T155925Z\summary.json`
- `hil_logs\i2c_20260531T155925Z\operator_checklist.md`
