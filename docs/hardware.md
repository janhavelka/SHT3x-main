# SHT3x Hardware Validation And HIL

Last updated: 2026-06-16

This file is the maintained hardware evidence status and HIL procedure. Software
tests, CI builds, dry runs, and fake transports do not prove electrical
behavior, board layout, fixture quality, or sensor accuracy.

No physical HIL validation was performed by a dry run.

ACK alone is not chip identity. A bus scan proves only that something
acknowledged an address. Stronger SHT3x evidence is a CRC-checked status read,
CRC-checked measurement, and CRC-checked serial/EIC read captured in the
transcript. Those still do not prove humidity accuracy or ALERT pin behavior.

## Software Build Status

| Area | Current status | Stronger evidence needed |
| --- | --- | --- |
| Native tests | Passed locally during `v1.6.0` release preparation. | Test log from the target commit. |
| Arduino PlatformIO ESP32-S3/S2 builds | Passed locally during `v1.6.0` release preparation. | Build logs from the target commit. |
| Pure ESP-IDF ESP32-S3/S2 builds | CI configured; local `idf.py` availability depends on the shell. | Passing GitHub CI log or local ESP-IDF 5.4+ build log. |
| Package validation | `platformio pkg pack` passed locally during `v1.6.0` release preparation. | Package command log and content inspection from the final target commit. |

## Current Curated Evidence

Latest curated default serial HIL evidence:

- Summary: [hil/20260601_arduino_esp32s3_com17_7847ed0_default_hil.md](hil/20260601_arduino_esp32s3_com17_7847ed0_default_hil.md)
- Source run: `hil_logs/i2c_20260601T183017Z/summary.md`
- Branch recorded by runner: `hardening/sht3x-release-readiness-gaps`
- Code commit recorded by runner: `7847ed0eb83fbeeb9f08c4f5ea14c8a8b24756c9`
- Firmware metadata: `1.5.0 (7847ed0eb83f, Jun  1 2026 20:13:07, clean)`
- Port/target: COM17, ESP32-S3, Arduino PlatformIO `esp32s3dev`
- Expected SHT3x address: `0x44`
- Final runner verdict: `PASS`

This evidence covers only the default automated serial command sequence. It
does not validate physical ALERT pin behavior, humidity accuracy, fault
injection, clock stretching, ESP32-S2 hardware, address `0x45`, alert writes,
destructive reset flows, all periodic rates, or long-soak stability.

## Evidence Status

| Area | Current result | Evidence |
| --- | --- | --- |
| Address probe `0x44` | PASS in current default serial HIL | Curated ESP32-S3 COM17 summary |
| Address probe `0x45` | Not run | Needs serial log |
| Single-shot low/medium/high no-stretch | PASS in current default serial HIL | Curated ESP32-S3 COM17 summary |
| Single-shot clock stretching | Not run | Needs serial log and timeout policy |
| Periodic fetch 0.5/1/2 mps | PASS in current default serial HIL | Curated ESP32-S3 COM17 summary |
| Periodic fetch 4/10 mps | Not run | Needs serial log |
| ART mode | PASS in current default serial HIL | Curated ESP32-S3 COM17 summary |
| Status read/status restore | PASS in current default serial HIL, without induced ALERT | Curated ESP32-S3 COM17 summary |
| Status clear | Not run | Needs register log |
| Alert read and encode/decode vectors | PASS in current default serial HIL | Curated ESP32-S3 COM17 summary |
| Alert write/read round trip | Not run on hardware | Needs serial log and cleanup record |
| Physical ALERT pin | Not run | Needs GPIO or logic-analyzer evidence |
| Heater status read | PASS in current default serial HIL; heater enable not run | Curated ESP32-S3 COM17 summary |
| Heater enable/disable behavior | Not run | Needs controlled ambient log |
| Soft/interface/general-call reset | Not run | Needs serial and, where relevant, logic evidence |
| ESP32-S2 hardware smoke | Not run | Needs ESP32-S2 serial log |
| Fault injection | Not run | Needs safe jig/interposer/emulator or documented manual fault evidence |
| Long soak | Not run | Needs soak log and fixture notes |
| Humidity production fixture | Not run | Needs reference fixture report |

## Serial Runner

The reusable host runner is `tools/run_sht3x_hil.py`, backed by
`tools/run_i2c_hil.py`. It drives the Arduino or ESP-IDF diagnostic CLI over a
serial port from the host. It does not talk directly to I2C and does not flash
firmware.

Dry-run only:

```bash
python tools/run_sht3x_hil.py --dry-run --expect-address 0x44 --board esp32s3 --target-name desk --operator <name>
```

Hardware run:

```bash
python tools/run_sht3x_hil.py --port COMx --baud 115200 --expect-address 0x44 --board esp32s3 --target-name desk --operator <name>
```

These commands require a full repository checkout. The PlatformIO package
payload may exclude `tools/` because it is meant for library consumption, not
host-side evidence generation.

The runner creates `hil_logs/i2c_<UTC_TIMESTAMP>/` and writes:

- `serial_transcript.txt`
- `summary.md`
- `summary.json`
- `operator_checklist.md`
- `environment.txt`

Operator-assisted groups may also create `operator_notes.md`,
`alert_gpio_capture.csv`, `logic_analyzer_reference.txt`, and an evidence
manifest. Generated `hil_logs/` directories remain local scratch output by
default. Commit only curated summaries or fixture artifacts that are intended
to become project evidence.

## Default Executable Command Sequence

The default sequence is safe by design: it avoids status clearing, heater
enable, alert writes, resets, raw command writes, fault injection, and soak
tests. This block is checked against `tools/run_i2c_hil.py` by
`tools/check_hil_contract.py`.

<!-- BEGIN DEFAULT_HIL_COMMANDS -->
```text
version
help
scan
probe
settings
drv
status
status_raw
single low
raw
comp
single medium
raw
comp
single high
raw
comp
serial nostretch
heater status
alert show
alert encode 60 80
alert decode 0xCD33
alert encode 58 79
alert decode 0xC92D
alert encode -9 22
alert decode 0x3869
alert encode -10 20
alert decode 0x3466
periodic start 0.5 high
periodic fetch
periodic stop
periodic start 1 high
status_restore
periodic fetch
periodic stop
periodic start 2 medium
periodic fetch
periodic stop
art start
art fetch
art stop
drv
```
<!-- END DEFAULT_HIL_COMMANDS -->

Expected evidence includes version text, help output, scan output containing
the expected address, `READY` or online driver state, parseable status/status
raw, plausible single-shot measurements for low/medium/high repeatability,
raw/comp cached samples, serial/EIC, heater OFF, alert limit reads, alert
encode/decode vectors, periodic-mode status-restore fields, selected periodic
start/fetch/stop paths, ART start/fetch/stop or explicit unsupported status,
and final READY health with zero unexplained failures.

## Opt-In Groups

| Flag | Coverage | Evidence boundary |
| --- | --- | --- |
| `--include-destructive` | selftest, recover, clear status, soft reset, restore, interface reset | Alters device/status state; not part of default smoke. |
| `--include-bus-wide-reset` | general-call reset | Requires isolated bus; can reset other supporting devices. |
| `--include-soak --soak-count N` | bounded stress and mixed-operation stress | Only proves the configured duration/count. |
| `--include-clock-stretch` | stretch-enabled read and serial/EIC | Unsupported or timeout behavior must be recorded explicitly. |
| `--include-alert-write` | software-visible alert write/readback and cleanup | Does not prove physical ALERT pin transitions. |
| `--include-all-periodic-rates` | additional 4 and 10 mps periodic fetches | Needs final health review and self-heating notes for stronger claims. |
| `--include-output-tests` | ALERT output operator/GPIO procedure | PASS requires GPIO or logic-analyzer evidence. |
| `--include-fault-tests` | fault/unplug/CRC-injection procedure | PASS requires safe jig/interposer/emulator or documented manual fault evidence. |

`--include-output-tests` can produce `OPERATOR_REVIEW_REQUIRED` when operator
or GPIO evidence is missing. `--include-fault-tests` records
`SKIP_REQUIRES_FIXTURE` without a suitable fixture. Unsupported behavior should
be recorded as `SKIP_UNSUPPORTED`, not hidden as a pass.

Final runner verdict values are `PASS`, `FAIL`, `OPERATOR_REVIEW_REQUIRED`, and
`INCOMPLETE`. A `PASS` verdict is limited to the selected automated serial
groups and attached artifacts.

## Target Record Checklist

Record these fields before treating a hardware run as evidence:

- Operator, date/time, branch, commit hash, and worktree state.
- MCU board, framework, build environment, serial port, and baud rate.
- Firmware version, device/module, chip marking, I2C address, and sensor variant.
- Supply voltage, bus speed, pull-ups, SDA/SCL pins, cable length, and reset/ALERT wiring.
- Fixture details, reference sensor if used, ambient conditions, and deviations.
- Exact build, upload, monitor, and runner commands.
- Attached `serial_transcript.txt`, `summary.md`, `summary.json`, and `environment.txt`.
- Photos of board, sensor/module, wiring, and any fixture used.
- Logic-analyzer/GPIO/scope evidence whenever ALERT, reset, bus edges, pull-ups,
  or fault behavior is claimed.

## Build And Flash Commands

Arduino PlatformIO:

```bash
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
python -m platformio run -e esp32s3dev -t upload --upload-port COMx
python -m platformio device monitor -e esp32s3dev --port COMx
```

Native ESP-IDF:

```bash
idf.py -C examples/idf/basic set-target esp32s3
idf.py -C examples/idf/basic build
idf.py -C examples/idf/basic -p COMx flash monitor
idf.py -C examples/idf/basic set-target esp32s2
idf.py -C examples/idf/basic build
```

If `idf.py` is unavailable, record the exact shell error and leave pure-IDF
local HIL build/run as blocked or not run. Do not infer it from PlatformIO
builds.

## Common Restore Step

Run this after every disruptive scenario:

```text
periodic stop
heater off
alert disable
clear_status
mode single
stretch 0
repeat high
drv
```

Pass the restore step only if the final state is `READY`, `online` is true, and
there are no new unexplained consecutive failures. If a fault test leaves the
sensor disconnected, reconnect first, run `recover`, then run the restore step.

## Ambient Humidity Test Notes

Sensirion's ambient testing guidance treats ambient production testing as a
practical alternative to a climate chamber for pre-calibrated sensors after
assembly. It is still a fixture-quality exercise, not a generic room-air check.

Key constraints:

- Use an accurate humidity and temperature reference sensor; two references can
  reduce reference-value fluctuation by averaging.
- Keep the DUT and reference at the same absolute temperature. RH is strongly
  temperature-dependent, so thermal mismatch appears as humidity error.
- Optimize thermal coupling through the jig and keep humidity coupling volume
  small and shielded from turbulence.
- Housings slow response because they restrict airflow; expect longer settling
  times than with a bare sensor on a PCB.
- Prestaging units near the jig reduces local temperature/RH step changes when
  a unit enters the fixture.
- Avoid local heat sources, direct sunlight, strong lighting, HVAC drafts,
  operator breath/body heat, and fast moving air over the sensor.
- Reflow or other high-temperature assembly steps can cause a temporary RH
  offset, while temperature remains unaffected. Account for that in limits or
  use a documented reconditioning process.
- If measurement-system analysis shows both temperature and RH problems, fix
  temperature first because better temperature agreement improves RH agreement.

Source material is listed in [reference/README.md](reference/README.md).
