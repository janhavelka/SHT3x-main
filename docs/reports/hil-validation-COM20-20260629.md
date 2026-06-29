# SHT3x HIL Validation Report - COM20 - 2026-06-29

## Verdict

Automated hardware validation on `COM20` exercised the ESP32-S3 Arduino
diagnostic firmware against an SHT3x responding at `0x44`.

The destructive/all-round command set completed without executable command
failures: 79 `PASS`, 1 `SKIP_UNSUPPORTED` for `iface_reset`. The same bus also
ACKed devices at `0x3C`, `0x50`, and `0x51`, so bus-wide general-call reset was
not run.

The requested 8-hour soak did not complete. The longest clean soak segment
produced 2853 `PASS` progress rows over about 65.8 minutes, reaching driver
health `READY`, `online=true`, `consecutive_failures=0`,
`total_success=381702`, and `total_failures=0`. That run then stopped because
the Windows/pyserial host link raised `ClearCommError failed
(PermissionError(13, 'The device does not recognize the command.', None, 22))`.
This is a host/serial interruption, not a recorded SHT3x driver failure, but it
still makes the 8-hour soak `INCOMPLETE`.

A post-reboot smoke run after the user rebooted the board passed 42/42 commands
and ended with driver health `READY`, `online=true`,
`consecutive_failures=0`, `total_success=42`, `total_failures=0`.

No production-readiness claim is made.

## Repository And Environment

| Field | Value |
| --- | --- |
| Repository | `C:\Users\Honza\Documents\Projects\SHT3x-main` |
| Branch | `main` |
| Commit | `df3ba5c45df552dce74629e363b0dc55e1a13776` |
| Firmware library string | `1.6.0 (df3ba5c45df5, Jun 29 2026 15:40:53, clean)` |
| Report generated | `2026-06-29T17:18:34+02:00` |
| Timezone | Europe/Prague, UTC+02:00 |
| Host OS | Microsoft Windows 11 Education, OS version `10.0.26200` |
| Python | `Python 3.12.10` |
| PlatformIO | `PlatformIO Core, version 6.1.18` |
| Target environment | `esp32s3dev` |
| Target framework | Arduino |
| Serial port | `COM20` |
| Baud rate | `115200` |
| I2C pins from boot transcript | `SDA=8`, `SCL=9` |
| Expected SHT3x address | `0x44` |
| I2C addresses observed | `0x3C`, `0x44`, `0x50`, `0x51` |
| SHT3x serial/EIC | `0x29075EB0` |
| Final report worktree | Dirty: `tools/run_i2c_hil.py`, `tools/test_run_i2c_hil_parser.py`, this report |

PlatformIO printed the usual obsolete-core warning for local Core `6.1.18`.

## Hardware Safety Notes

The user put the board into upload mode, later closed `COM20`, allowed
destructive testing, and finally rebooted the board before the post-reboot
smoke run.

The HIL runner confirmed other devices on the same I2C bus. Because this was
not an isolated SHT3x-only bus, general-call reset was intentionally not run.
Physical ALERT pin assertion, logic-analyzer capture, hot-unplug/fault
injection, reference humidity/temperature accuracy validation, address `0x45`,
and ESP32-S2 hardware were not validated.

## Exact Commands

Firmware build and upload:

```powershell
pio run -e esp32s3dev -t clean
pio run -e esp32s3dev -t upload --upload-port COM20
```

Destructive/all-round HIL:

```powershell
python tools\run_i2c_hil.py --port COM20 --baud 115200 --expect-address 0x44 --board esp32s3dev --target-name SHT3x-COM20 --operator codex --include-destructive --include-clock-stretch --include-alert-write --include-all-periodic-rates --benchmark-count 100 --boot-settle-s 2 --reconnect-attempts 3 --reconnect-delay-s 1
```

8-hour soak attempts:

```powershell
python tools\run_i2c_hil.py --port COM20 --baud 115200 --expect-address 0x44 --board esp32s3dev --target-name SHT3x-COM20 --operator codex --include-soak --soak-duration-s 28800 --soak-chunk-count 500 --soak-recover-every 0 --benchmark-count 0 --boot-settle-s 2 --reconnect-attempts 3 --reconnect-delay-s 1
```

Post-reboot smoke run:

```powershell
python tools\run_i2c_hil.py --port COM20 --baud 115200 --expect-address 0x44 --board esp32s3dev --target-name SHT3x-COM20 --operator codex --benchmark-count 0 --boot-settle-s 2 --reconnect-attempts 3 --reconnect-delay-s 1
```

Software verification:

```powershell
python tools\run_i2c_hil.py --parser-self-test
python tools\test_run_i2c_hil_parser.py
python tools\check_hil_contract.py
python tools\check_cli_contract.py
python tools\check_core_timing_guard.py
python tools\check_idf_example_contract.py
pio test -e native
pio run -e esp32s3dev
pio run -e esp32s2dev
pio pkg pack
git diff --check
```

## HIL Results

| Run | Artifact directory | Result | Key metrics |
| --- | --- | --- | --- |
| Destructive/all-round | `hil_logs\i2c_20260629T134150Z` | `INCOMPLETE` only because `iface_reset` was unsupported and fixture-only groups were not selected; executable commands had no FAIL | 80 commands: 79 `PASS`, 1 `SKIP`; final health `READY`, `online=true`, `consecutive_failures=0`, `total_success=330`, `total_failures=0`; stress `100` elapsed `2.015 s` |
| Soak attempt 1 | `hil_logs\i2c_20260629T134605Z` | `FAIL` | 207 rows: 206 `PASS`, 1 timeout at cycle 18 on `periodic fetch`; last health before failure `READY`, `total_success=21976`, `total_failures=0`; temp `27.55..27.91 C`, RH `42.95..44.86 %` |
| Soak attempt 2 | `hil_logs\i2c_20260629T135450Z` | `FAIL` | 237 rows: 236 `PASS`, 1 timeout at cycle 21 on `art fetch`; last health before failure `READY`, `total_success=25846`, `total_failures=0`; temp `27.58..27.81 C`, RH `42.98..43.75 %` |
| Soak attempt 3 | `hil_logs\i2c_20260629T140211Z` | `INCOMPLETE` due host serial exception | 2853 rows, all `PASS`, cycles through 296; first row `2026-06-29T14:02:14Z`, last row `2026-06-29T15:08:02Z`; temp `27.41..28.03 C`, RH `43.53..47.87 %`; last health `READY`, `online=true`, `consecutive_failures=0`, `total_success=381702`, `total_failures=0` |
| Post-reboot smoke | `hil_logs\i2c_20260629T151741Z` | `PASS` | 42 commands: 42 `PASS`; final health `READY`, `online=true`, `consecutive_failures=0`, `total_success=42`, `total_failures=0` |

The first two soak failures exposed a HIL runner automation problem around the
Arduino diagnostic CLI's async measurement scheduling: the CLI printed a prompt
after `Measurement scheduled` / `request: IN_PROGRESS`, while the measurement
sample appeared only after later serial activity. The runner originally treated
that as a timeout. The fix was to require an actual measurement for
measurement-validating commands and to send a bounded read-only `online`
command as a nudge while waiting.

The third soak used that fix and had no HIL command failures before pyserial
raised the Windows serial exception.

## Sensitive Metrics Watched

| Metric | Observation |
| --- | --- |
| Bus identity | Scan saw `0x3C`, `0x44`, `0x50`, `0x51`; SHT3x probe and CRC-protected serial path confirmed the SHT3x at `0x44` |
| Sensor serial/EIC | `0x29075EB0`, read with no-stretch and stretch paths |
| Driver health | All completed HIL runs ended with `READY`, `online=true`, `consecutive_failures=0`, and `total_failures=0` |
| Driver counters | Longest soak segment reached `total_success=381702`, `total_failures=0` before host serial exception |
| Status register | Default/status paths returned `0x0000`; destructive clear/status cycle ended at `0x0000` |
| Measurement plausibility | Destructive run around `27.45..27.57 C`, `44.53..44.76 %RH`; longest soak `27.41..28.03 C`, `43.53..47.87 %RH` |
| Periodic modes | Automated fetch covered 0.5, 1, 2 mps by default; opt-in all-rate run also covered 4 and 10 mps |
| ART mode | `art start`, `art fetch`, `art stop` passed in destructive/all-round and post-reboot smoke runs |
| Clock stretching | `read` and `serial stretch` passed in the opt-in clock-stretch group |
| Alert limit path | Encode/decode vectors and read/write/readback of all four limits passed; cleanup `alert disable` passed |
| Heater | Status read passed; heater was not intentionally enabled for thermal stress |
| Recovery/reset | `selftest`, `recover`, soft `reset`, and `restore` passed; `iface_reset` skipped as unsupported |
| Host stability | Windows serial link interrupted the long soak with pyserial `ClearCommError`; future runner now records serial exceptions as FAIL rows instead of losing artifacts |

## Audit Findings And Fixes

### Fixed: scan parser over-counted documented addresses

The HIL scan parser originally collected every hex address from scan output,
including explanatory text like `0x44/0x45=SHT3x`. That could falsely report
devices not present on the bus.

Fix: `tools/run_i2c_hil.py` now ignores `common addresses` and `known:` lines
and treats `Found device at 0xNN` as authoritative for that line. Test added:
`test_scan_parser_ignores_known_address_documentation()`.

### Fixed: async CLI measurement completion handling

The HIL runner previously accepted prompt/idle behavior around scheduled async
measurements too early or timed out while the CLI was waiting for loop-driven
sample completion.

Fix: measurement-validating commands must see `Temp:` or `temperature=`.
When the CLI reports a scheduled measurement but no sample yet, the runner sends
bounded `online` nudges every 250 ms. Test added:
`test_async_measurement_wait_sends_online_nudge()`.

### Fixed: host serial exceptions lost final artifacts

The long soak exposed a Windows pyserial exception during `read()`. At that
time, the runner crashed before writing normal final summaries.

Fix: `run_serial()` now converts write/read/nudge exceptions into structured
`FAIL` rows with `completion_reason="serial-exception"`. Test added:
`test_serial_read_exception_returns_fail_row()`.

### Fixed: long-soak progress visibility

Before the final runner change, a host interruption could leave only partial
stdout/stderr and no final summary for a long run.

Fix: the runner now writes compact incremental command rows to
`progress.jsonl`. Test added: `test_append_progress_writes_compact_jsonl()`.

### Open: diagnostic CLI async UX

The Arduino diagnostic CLI can print a prompt after scheduling an async sample
and print the sample later. Automation now handles this. A cleaner firmware UX
would be an explicit bounded wait command for periodic/ART fetch diagnostics,
but that was not changed during this validation to avoid reflashing different
test firmware mid-run.

### Open: host serial reliability for 8-hour soak

The library/firmware did not record an I2C failure in the longest soak segment,
but the host serial link was not stable for 8 hours. A future full soak should
use the current exception-hardened runner, a stable USB path, and ideally
resume/reconnect support that preserves progress after transient COM errors.

## Software Verification Results

| Command | Result |
| --- | --- |
| `python tools\run_i2c_hil.py --parser-self-test` | PASS |
| `python tools\test_run_i2c_hil_parser.py` | PASS |
| `python tools\check_hil_contract.py` | PASS |
| `python tools\check_cli_contract.py` | PASS |
| `python tools\check_core_timing_guard.py` | PASS |
| `python tools\check_idf_example_contract.py` | PASS |
| `pio test -e native` | PASS, 85/85 tests |
| `pio run -e esp32s3dev` | PASS, RAM 6.9%, flash 30.4% |
| `pio run -e esp32s2dev` | PASS, RAM 11.1%, flash 28.3% |
| `pio pkg pack` | PASS; generated `SHT3x-1.6.0.tar.gz` was removed after verification |
| `git diff --check` | PASS |

Parallel PlatformIO builds emitted transient `.pio\build` cleanup warnings, so
the ESP32-S3 and ESP32-S2 builds were rerun sequentially and passed.

## Not Run / Not Proven

- Full 8-hour soak: not completed.
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

## Artifacts

- `hil_logs\i2c_20260629T134150Z\summary.md`
- `hil_logs\i2c_20260629T134150Z\summary.json`
- `hil_logs\i2c_20260629T134150Z\serial_transcript.txt`
- `hil_logs\i2c_20260629T134605Z\progress.jsonl`
- `hil_logs\i2c_20260629T135450Z\progress.jsonl`
- `hil_logs\i2c_20260629T140211Z\progress.jsonl`
- `hil_logs\long_soak_20260629T140211Z_stderr.txt`
- `hil_logs\i2c_20260629T151741Z\summary.md`
- `hil_logs\i2c_20260629T151741Z\summary.json`
- `hil_logs\i2c_20260629T151741Z\serial_transcript.txt`
