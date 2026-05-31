# SHT3x Serial I2C HIL Runbook

Date: 2026-05-31
Branch: `hardening/sht3x-industry-readiness`

No physical HIL validation was performed while preparing this runbook.

This procedure drives the existing Arduino or ESP-IDF diagnostic CLI over a
serial port from the host. The Python runner does not talk directly to I2C and
does not flash firmware. It records an auditor-ready serial transcript and
summaries for the command surface already implemented by this repository.

ACK alone is not chip identity. A bus scan proves only that something
acknowledged an address. For SHT3x, stronger evidence is a CRC-checked status
read, CRC-checked measurement, and CRC-checked serial/EIC read captured in the
transcript. Those still do not prove humidity accuracy or ALERT pin behavior.

## Hardware Preflight

Fill these fields before running:

- Operator:
- Date/time:
- Branch:
- Commit hash:
- Worktree state, clean or dirty:
- MCU board:
- Framework, Arduino/PlatformIO or native ESP-IDF:
- Build target:
- Serial port:
- Baud rate: `115200`
- I2C address: `0x44` default or `0x45` when ADDR is high
- I2C bus speed:
- Supply voltage:
- Pull-up values:
- Reset wiring:
- ALERT/interrupt pin wiring:
- Device/module model:
- Chip marking:
- Fixture details:
- Firmware version:

## Build And Upload

Arduino / PlatformIO build:

```bash
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s2dev
```

Arduino / PlatformIO upload, replace `COMx` with the operator-confirmed port:

```bash
python -m platformio run -e esp32s3dev -t upload --upload-port COMx
python -m platformio run -e esp32s2dev -t upload --upload-port COMx
```

Arduino serial monitor:

```bash
python -m platformio device monitor -e esp32s3dev --port COMx
python -m platformio device monitor -e esp32s2dev --port COMx
```

Native ESP-IDF build and upload, from an ESP-IDF 5.4+ shell:

```bash
idf.py -C examples/idf/basic set-target esp32s3 build
idf.py -C examples/idf/basic -p COMx flash monitor
idf.py -C examples/idf/basic set-target esp32s2 build
idf.py -C examples/idf/basic -p COMx flash monitor
```

## Runner Command

Dry-run only, no serial hardware:

```bash
python tools/run_i2c_hil.py --dry-run
```

Hardware run, replace `COMx` after wiring and firmware are confirmed:

```bash
python tools/run_i2c_hil.py --port COMx --baud 115200 --out hil_logs
```

The runner creates a unique `hil_logs/i2c_<timestamp>/` directory and writes:

- `serial_transcript.txt`
- `summary.md`
- `summary.json`
- `operator_checklist.md`

`hil_logs/` is local scratch output and is ignored by git. For an audit record,
copy the selected transcript, summaries, target template, and operator notes
into the curated `docs/hil/` evidence set described by
`docs/SHT3X_HIL_RUNBOOK.md`.

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
<!-- END DEFAULT_HIL_COMMANDS -->

Expected serial evidence includes version text, help output, scan output,
`READY` or online driver state, CRC-checked status/measurement/serial paths,
periodic start/fetch/stop success, and a final health snapshot.

## Classification Rules

The runner uses command metadata and precise text tokens:

- `PASS` means the expected serial text was observed and no precise failure
  token was seen.
- `FAIL` means a precise failure token was observed.
- `SERIAL_OK_OR_REVIEW` means serial output exists but does not exactly match
  the expected command tokens.
- `REVIEW_REQUIRED` means output was absent or ambiguous.
- `OPERATOR_CHECK_REQUIRED` means physical evidence is required and the runner
  must not mark it as a software pass.
- `SKIPPED_UNSAFE` is reserved for future/manual commands intentionally excluded
  from the default sequence.
- `TIMEOUT` means the command exceeded its deadline.
- `NOT_IMPLEMENTED` is used for dry-run command rows.

The default run does not treat ordinary configuration text such as
`timeout=50ms` or `fail=0` as a failure.

## Manual And Unsafe Checks

Run these only with explicit operator authorization and suitable fixtures:

- ALERT pin validation: requires `--include-output-tests` or a manual procedure,
  threshold planning, and GPIO/logic-analyzer evidence.
- Heater enable behavior: do not run by default because it changes temperature
  readings.
- `clear_status`: do not run by default because it destroys status evidence.
- Alert writes, raw alert writes, `alert disable`: configure limits only during
  planned ALERT validation and restore safe values afterward.
- `reset`, `restore`, `iface_reset`, `greset`, and `recover`: run only when
  reset wiring and bus isolation are documented.
- `command write`, `command write_data`, `command read`: raw command paths are
  advanced diagnostics and must be reviewed before use.
- Fault tests: require a safe jig, emulator, interposer, or temporary
  test-only transport.
- Soak/stress tests: require stable power, environmental assumptions, and a
  defined stop condition.

## Evidence Capture

Attach these artifacts to the HIL record:

- `serial_transcript.txt` from the runner log directory.
- `summary.md` and `summary.json`.
- Completed target template.
- Photos or video of wiring and board labels.
- Logic analyzer capture for ALERT, reset, or fault tests when used.
- Scope or meter readings when supply, bus edges, or pull-ups are part of the
  claim.
- Operator notes for physical observations and deviations.

## Verdict Boundaries

A runner `PASS` is limited to the executed serial command sequence. It does not
prove humidity accuracy, ALERT pin operation, fault injection, long-soak
stability, or production readiness unless those exact scenario groups were run
on real hardware and their evidence is attached.
