# SHT3x Automatic HIL Runner Report

Date: 2026-06-01
Branch: `hardening/sht3x-release-readiness-gaps`

## Scope

This chunk validates the Python serial HIL runner as an evidence generator. No
destructive, soak, output/GPIO, fault, or hardware HIL groups were run.

Runner paths:

- `tools/run_sht3x_hil.py` - compatibility entrypoint.
- `tools/run_i2c_hil.py` - implementation.

## Safe Default Groups

The default runner plan supports the expanded safe smoke set:

- version/help, scan, probe, settings, and driver health
- status and status_raw
- no-stretch single-shot low, medium, and high repeatability
- cached raw and compensated sample reads
- serial/EIC no-stretch read
- heater status read with heater left off
- alert limit read/show
- alert encode/decode vectors:
  - 80 %RH / 60 C -> `0xCD33`
  - 79 %RH / 58 C -> `0xC92D`
  - 22 %RH / -9 C -> `0x3869`
  - 20 %RH / -10 C -> `0x3466`
- `status_restore` while periodic mode is active
- selected periodic paths: 0.5 mps high, 1 mps high, 2 mps medium
- ART start/fetch/stop
- final driver health snapshot

The default sequence avoids status clearing, heater enable, alert writes, reset
commands, raw write commands, fault injection, output/GPIO checks, and soak.
If a command with a configured cleanup action fails during a real serial run,
the runner now issues the configured recovery command and records that recovery
row in the evidence.

## Opt-In Flags

Confirmed runner flags:

- `--include-destructive`
- `--include-bus-wide-reset`
- `--include-soak`
- `--soak-count`
- `--include-clock-stretch`
- `--include-alert-write`
- `--include-all-periodic-rates`
- `--include-output-tests`
- `--include-fault-tests`
- `--expect-address`
- `--board`
- `--target-name`
- `--operator`

Bus-wide reset requires `--include-destructive` and is still a shared-bus policy
decision. Output/GPIO and fault-test groups remain fixture-gated.

## Generated Evidence Files

A run creates:

- `serial_transcript.txt`
- `summary.md`
- `summary.json`
- `operator_checklist.md`
- `environment.txt`

Operator-assisted groups may also create notes, GPIO capture, logic-analyzer
reference, and evidence-manifest templates. When those files exist, the JSON
artifact map includes them.

## Parsing And Acceptance

The runner parses or validates:

- I2C scan addresses and missing expected address
- firmware/library version strings
- measurement temperature and humidity
- raw and compensated samples
- status word and status bits
- serial/EIC
- heater status
- driver state, online flag, consecutive failures, total successes, and total
  failures
- mode, repeatability, periodic rate, and clock-stretch setting
- alert encode/decode values and raw alert limits
- `status_restore` snapshot fields, sub-statuses, and active-mode restoration

Acceptance fails on these tokens or states:

- `Unknown command`
- `FAIL` / `FAILED`
- `ERR` / `ERROR`
- `CRC_ERROR` / `CRC_MISMATCH`
- `I2C_TIMEOUT`
- `I2C_NACK`
- `DEVICE_NOT_FOUND`
- `OFFLINE`
- `DEGRADED`
- missing expected address
- missing `status_restore` fields
- final driver failure counters
- malformed driver state output
- configured driver address mismatch
- active `status_restore` that does not return to the interrupted mode

Expected nonterminal states are allowed where appropriate:

- `IN_PROGRESS`
- `Measurement scheduled`
- operator/fixture groups that are not selected

## Parser Tests Added

`tools/test_run_i2c_hil_parser.py` now covers:

- status parsing
- measurement parsing
- driver health parsing
- `status_restore` parsing
- boolean/numeric `status_restore` fields
- unknown command failure
- failure-token detection
- missing expected address
- missing configured address
- missing `status_restore` fields
- active `status_restore` mode-restore validation
- final driver failures
- malformed driver state
- configured-address mismatch
- IDF status, measurement, and health formats
- `IN_PROGRESS` / scheduled-measurement acceptance
- unsupported live serial responses classified as `SKIP_UNSUPPORTED`
- dry-run `INCOMPLETE` verdict

Validation command:

```bash
python tools/test_run_i2c_hil_parser.py
```

Result: pass.

## Validation Commands

Software validation run in this chunk:

- `python tools/check_cli_contract.py`: pass.
- `python tools/check_idf_example_contract.py`: pass.
- `python tools/check_hil_contract.py`: pass.
- `python tools/test_run_i2c_hil_parser.py`: pass.
- `python -m platformio test -e native`: pass, 76/76 tests.
- `python -m platformio run -e esp32s3dev`: pass.
- `python -m platformio run -e esp32s2dev`: pass.
- `python -m platformio pkg pack`: pass; tarball inspection found the runner
  scripts included and no generated HIL logs or build/reference artifacts.

Remaining automation gap: CI does not yet run
`python tools/test_run_i2c_hil_parser.py` or
`python tools/check_hil_contract.py`.

## Dry-Run Result

Command:

```bash
python tools/run_sht3x_hil.py --dry-run --expect-address 0x44 --board esp32s3 --target-name dry-run
```

Result:

- Generated log directory: `hil_logs/i2c_20260601T085607Z`
- Final verdict: `INCOMPLETE`
- Generated files: `serial_transcript.txt`, `summary.md`, `summary.json`,
  `operator_checklist.md`, `environment.txt`
- Boundary: dry-run only; no physical HIL validation was performed.
- Worktree in the dry-run summary was `dirty` because documentation and runner
  test edits were in progress.

## Hardware Run Result

Not run. Hardware safe-default HIL requires explicit approval and a connected
target before running:

```bash
python tools/run_sht3x_hil.py --port COM17 --baud 115200 --expect-address 0x44 --board esp32s3 --target-name release-readiness-safe
```

## Remaining Fixture-Only Evidence

- Physical ALERT pin validation: Not run.
- Fault injection: Not run.
- Clock stretching: Not run.
- Alert writes/readback on hardware: Not run.
- All periodic rates beyond the default selected set: Not run.
- Long soak: Not run.
- ESP32-S2 hardware run: Not run.
- Humidity accuracy or production fixture validation: Not run.
