# SHT3x Serial I2C HIL Runbook

Date: 2026-05-31
Branch: `main`

This procedure drives the Arduino or ESP-IDF diagnostic CLI over a serial port
from the host. The Python runner does not talk directly to I2C and does not
flash firmware. Updating this runbook or running `--dry-run` does not create
physical HIL evidence. No physical HIL validation was performed by a dry run.

ACK alone is not chip identity. A bus scan proves only that something
acknowledged an address. Stronger SHT3x evidence is a CRC-checked status read,
CRC-checked measurement, and CRC-checked serial/EIC read captured in the
transcript. Those still do not prove humidity accuracy or ALERT pin behavior.

## Hardware Preflight

Complete `docs/SHT3X_I2C_HIL_TARGET_TEMPLATE.md` before a hardware run. Record
operator, commit, board, framework, serial port, I2C wiring, supply, pull-ups,
sensor identity, reset/ALERT wiring, and fixture details.

Use `docs/SHT3X_HIL_RUNBOOK.md` for firmware build, upload, and manual monitor
commands. This runner starts after diagnostic CLI firmware is already flashed
and responding on the selected serial port.

## Runner Command

Dry-run only, no serial hardware:

```bash
python tools/run_sht3x_hil.py --dry-run --expect-address 0x44 --board esp32s3 --target-name desk
```

Hardware run, replace `COMx` after wiring and firmware are confirmed:

```bash
python tools/run_sht3x_hil.py --port COMx --baud 115200 --expect-address 0x44 --board esp32s3 --target-name desk
```

`tools/run_sht3x_hil.py` is a compatibility entrypoint for
`tools/run_i2c_hil.py`.

The runner creates `hil_logs/i2c_<UTC_TIMESTAMP>/` and writes:

- `serial_transcript.txt`
- `summary.md`
- `summary.json`
- `operator_checklist.md`
- `environment.txt`

Operator-assisted groups may also create:

- `operator_notes.md`
- `alert_gpio_capture.csv`
- `logic_analyzer_reference.txt`
- `photos_or_evidence_manifest.md`

`hil_logs/` is local scratch output and is ignored by git. For an audit record,
copy selected transcripts, summaries, target template, operator notes, and
fixture evidence into the curated `docs/hil/` evidence set described by
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
status_restore
periodic start 0.5 high
periodic fetch
periodic stop
periodic start 1 high
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
encode/decode vectors, status-restore fields, selected periodic start/fetch/stop
paths, ART start/fetch/stop or explicit unsupported status, and final READY
health with zero failures.

## Flags

Safe metadata and selection flags:

```bash
--expect-address 0x44
--board esp32s3
--target-name <name>
--operator <name>
--commands <file>
```

Opt-in groups:

```bash
--include-destructive
--include-bus-wide-reset
--include-soak --soak-count 100
--include-clock-stretch
--include-alert-write
--include-all-periodic-rates
--include-output-tests
--include-fault-tests
```

`--include-bus-wide-reset` requires `--include-destructive` and prints a
warning because general-call reset can affect every supporting device on the
I2C bus.

## Opt-In Group Boundaries

`--include-destructive` may run `selftest`, `recover`, `clear_status`, `reset`,
`restore`, and `iface_reset`. These alter sensor or status state and must not be
part of the safe default.

`--include-soak` runs bounded `stress`, configured `stress <N>`, `stress_mix`,
and final `drv/settings` capture. This is a short functional stress group, not
long-soak evidence unless the configured duration actually matches the claim.

`--include-clock-stretch` runs `mode single`, `stretch 1`, `meastime`, `read`,
`serial stretch`, `stretch 0`, and no-stretch recovery checks. Unsupported or
timeout behavior is recorded as `SKIP_UNSUPPORTED` when the selected transport
cannot prove stretching.

`--include-alert-write` writes and reads software-visible alert thresholds and
then attempts `alert disable` cleanup. It does not prove physical ALERT pin
behavior.

`--include-output-tests` creates operator/GPIO evidence placeholders. Without
GPIO or logic-analyzer capture, physical ALERT output remains
`OPERATOR_REVIEW_REQUIRED`.

`--include-fault-tests` does not fake faults. Without a jig, interposer,
emulator, or deliberate unplug/replug evidence, it records
`SKIP_REQUIRES_FIXTURE`.

## Classification Rules

Per-command result values:

- `PASS`: expected serial text and parser acceptance criteria were met.
- `FAIL`: failure token, timeout, missing expected text, or parser acceptance
  failure.
- `SKIP`: command was not run, dry-run skipped it, unsupported behavior was
  detected, or a required fixture is absent.
- `OPERATOR_REVIEW_REQUIRED`: operator evidence or manual transcript review is
  required.

Skip reasons include `SKIP_DRY_RUN`, `NOT_RUN`, `SKIP_UNSUPPORTED`, and
`SKIP_REQUIRES_FIXTURE`.

Final verdict values:

- `PASS`
- `FAIL`
- `OPERATOR_REVIEW_REQUIRED`
- `INCOMPLETE`

A `PASS` verdict is limited to the selected automated serial command groups. It
does not prove humidity accuracy, physical ALERT pin behavior, fault injection,
long-soak stability, or production readiness unless those exact scenario groups
were run on real hardware and their evidence is attached.

The parser detects obvious failures from `FAIL`, `ERROR`, `CRC_ERROR`,
`CRC_MISMATCH`, `I2C_TIMEOUT`, `I2C_NACK`, `DEVICE_NOT_FOUND`, unexpected
`BUSY`, `OFFLINE`, `DEGRADED`, and `Unknown command`. It does not treat normal
`IN_PROGRESS` measurement scheduling as a failure.

## Parsed Evidence

`summary.md` and `summary.json` include:

- UTC timestamp, branch, commit, and worktree state.
- Serial port, baud, board, target name, operator, and expected address.
- Firmware and library version parsed from `version` output when available.
- I2C addresses parsed from `scan`.
- Per-command result, elapsed time, notes, and parsed fields.
- Parsed temperature, humidity, raw sample, status word/bits, serial/EIC,
  health counters, mode, repeatability, periodic rate, and clock-stretching
  state when present.
- Skipped tests and why.
- Final verdict and claim boundary.

## Evidence Capture

Attach these artifacts to a release or audit record:

- `serial_transcript.txt` from the runner log directory.
- `summary.md`, `summary.json`, and `environment.txt`.
- Completed target template.
- Photos or video of wiring and board labels.
- Logic analyzer or GPIO capture for ALERT, reset, or fault tests when used.
- Scope or meter readings when supply, bus edges, or pull-ups are part of the
  claim.
- Operator notes for physical observations and deviations.
