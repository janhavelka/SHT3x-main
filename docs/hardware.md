# Hardware Validation And HIL

This file is the hardware-in-the-loop (HIL) runbook and the record of which
device behaviours have been exercised on real hardware.

Software tests, CI builds, dry runs, and fake transports do not prove electrical
behavior, board layout, fixture quality, or sensor accuracy. Keep those two
claims separate.

A bus scan proves only that something acknowledged an address. Stronger evidence
that the device really is an SHT3x is a CRC-checked status read, a CRC-checked
measurement, and a CRC-checked serial/EIC read captured in the transcript. None
of those prove humidity accuracy or ALERT pin behavior.

## Coverage Status

What the serial CLI surface has and has not been exercised on real hardware.
Update a row only when a transcript backs it.

| Area | Status | What stronger evidence needs |
| --- | --- | --- |
| Address probe `0x44` | Covered on ESP32-S3 / Arduino | — |
| Address probe `0x45` | Not run | Serial log from a board strapped `ADDR=VDD` |
| Single-shot low/medium/high, no stretch | Covered on ESP32-S3 | — |
| Single-shot clock stretching | Covered on ESP32-S3 | — |
| Periodic fetch 0.5/1/2 mps | Covered on ESP32-S3 | — |
| Periodic fetch 4/10 mps | Covered on ESP32-S3 | Self-heating notes for sustained 10 mps |
| ART mode | Covered on ESP32-S3 | — |
| Status read and status restore | Covered on ESP32-S3, without an induced ALERT | Run with a real ALERT condition |
| Status clear | Covered on ESP32-S3 | — |
| Alert-limit read and encode/decode vectors | Covered on ESP32-S3 | — |
| Alert-limit write/read round trip | Covered on ESP32-S3 with exact readback and cleanup | — |
| Physical ALERT pin | Not run | GPIO capture or logic-analyzer trace |
| Heater status read | Covered on ESP32-S3 | — |
| Heater enable/disable and status | Covered on ESP32-S3 | Controlled self-heating measurement |
| Soft reset / recover / restore | Covered on ESP32-S3 | — |
| Interface reset | Not run | Firmware that supplies the `busReset` callback |
| General-call reset | Arm gate only | An isolated bus and an application-supplied bus-wide transport |
| ESP32-S2 hardware | Not run | ESP32-S2 serial log |
| Pure ESP-IDF hardware | Not run | Serial log from an `idf.py` image |
| Electrical/bus fault injection | Not run | Safe jig, interposer, or bus emulator |
| Long soak | One uninterrupted hour, zero failure deltas | Multi-day run |
| Calibrated humidity accuracy | Not run | Reference fixture report |

Raw run artifacts are not kept in this checkout. Archive the runs you decide to
accept somewhere durable, and record here only which rows they moved.

## Serial Runner

The host runner is [`tools/run_sht3x_hil.py`](../tools/run_sht3x_hil.py). It
drives the Arduino or ESP-IDF diagnostic CLI over a serial port. It does not
talk to I2C directly and does not flash firmware.

Dry run (parser and plan check only, no hardware):

```bash
python tools/run_sht3x_hil.py --dry-run --expect-address 0x44 --board esp32s3 --target-name desk --operator <name>
```

Hardware run:

```bash
python tools/run_sht3x_hil.py --port COMx --baud 115200 --expect-address 0x44 --board esp32s3 --target-name desk --operator <name>
```

By default the runner derives the expected library version from `library.json`,
expects the current checkout commit, and refuses a live run from a dirty
worktree. Override identity deliberately with `--expect-library-version`,
`--expect-library-commit`, or `--allow-dirty-firmware`; the selected and observed
values are retained in the run artifacts. A failed identity preflight stops the
run before any optional destructive command.

The `version` response also records the runtime framework, build target, and
Arduino-core/ESP-IDF version. This prevents a command-compatible but
wrong-framework image from being accepted as the intended target.

Each run creates `hil_logs/sht3x_<UTC_TIMESTAMP>/` containing
`serial_transcript.txt`, `summary.md`, `summary.json`, `progress.jsonl`,
`operator_checklist.md`, and `environment.txt`. Operator-assisted groups may
also produce `operator_notes.md`, `alert_gpio_capture.csv`,
`logic_analyzer_reference.txt`, and an evidence manifest. `hil_logs/` is ignored
by git; it is scratch output, not project content.

Serial writes use the port's finite write timeout and are checked for complete
delivery. A short write, serial exception, or response timeout stops the
remaining plan. Because command framing is then uncertain, the runner sends no
later recovery, soak, or cleanup commands. Each cleanup step not confirmed
complete is recorded as failed with `cleanup-deferred-lost-framing`; reconnect
or re-establish the serial session and perform the restore procedure manually.

## Default Command Sequence

The default sequence is safe by design: no status clearing, heater enable, alert
writes, resets, raw command writes, fault injection, or soak. It confirms the
bounded `status_restore` interruption and proves that request inspection and
cancellation perform zero I2C callbacks. This block is checked against
`tools/run_sht3x_hil.py` by `tools/check_hil_contract.py`.

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
xfer_reset
request
job current
xfer_assert 0 0 0
job cancel
result
xfer_assert 0 0 0
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
status_restore confirm
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

The minimum useful serial contract is `version`, `scan`, `probe`, `settings`,
and `drv`; `drv` prints the health snapshot.

A good run shows version text, help output, a scan containing the expected
address, a `READY`/online driver state, parseable status, plausible single-shot
measurements at all three repeatabilities, cached raw/compensated samples, a
serial/EIC read, heater off, alert-limit reads, matching alert encode/decode
vectors, periodic status-restore fields, the selected periodic and ART
start/fetch/stop paths, zero-transfer request/cancel/result evidence, and final
`READY` health with no unexplained failures.

## Opt-In Groups

| Flag | Coverage | Boundary |
| --- | --- | --- |
| `--include-destructive` | confirmed selftest, recover, clear status, soft reset, restore, interface reset | Alters device/status state. |
| `--include-bus-wide-reset` | `greset arm` then `greset confirm` | Also needs `--include-destructive` and an isolated bus. Examples keep the general-call transport disabled, so `SKIP_UNSUPPORTED` is the honest outcome unless the application supplies a bus-wide transport. |
| `--include-soak --soak-count N` | bounded stress and mixed-operation stress | Proves the configured count only. |
| `--include-soak --soak-duration-s N` | firmware-side `i2c_soak N` measurement loop | Proves the configured duration only. |
| `--include-clock-stretch` | stretch-enabled read and serial/EIC | Unsupported or timeout behavior must be recorded explicitly. |
| `--include-alert-write` | alert write/readback and cleanup | Does not prove physical ALERT pin transitions. |
| `--include-heater` | brief heater enable/status/disable plus cleanup | Proves the command/status path, not self-heating performance. |
| `--include-all-periodic-rates` | additional 4 and 10 mps fetches | Review final health and self-heating. |
| `--include-output-tests` | ALERT output operator/GPIO procedure | Needs GPIO or logic-analyzer evidence. |
| `--include-fault-tests` | fault/unplug/CRC-injection procedure | Needs a safe jig, interposer, or emulator. |

`--include-output-tests` can produce `OPERATOR_REVIEW_REQUIRED` when operator or
GPIO evidence is missing. `--include-fault-tests` records
`SKIP_REQUIRES_FIXTURE` without a suitable fixture. Behavior the firmware does
not implement is recorded as `SKIP_UNSUPPORTED`, never hidden as a pass.

Runner verdicts are `PASS`, `FAIL`, `OPERATOR_REVIEW_REQUIRED`, and
`INCOMPLETE`. A `PASS` covers only the selected automated groups and the
attached artifacts. Everything except `PASS` exits nonzero;
`--allow-incomplete` is a planning-only override.

Every firmware mutation in an automated plan uses the literal `confirm` suffix
shown by both CLIs. General-call reset additionally uses a one-shot `greset
arm`/`greset confirm` pair, and the runner disarms during cleanup if an armed
step fails. A `--commands <file>` plan is classified before any serial output is
produced: unknown and mutation-like commands are rejected, known mutations keep
their opt-in and cleanup policy, and raw command words cannot bypass the heater,
alert-write, or 4/10 mps opt-ins. The first executable line of a custom plan
must be exactly `version`, so no read or mutation runs before identity is
established. Recognized noncanonical read-only commands need
`--allow-custom-read-only-review` and stay operator-review rows.

The firmware-side duration path uses nonzero request IDs, absolute deadlines,
one-callback `pollJob()` steps, zero-I2C cancellation, and milli-unit readout.
Its summary passes only when the loop ran at least the requested time, every
sample succeeded, logical and transport counts agree, protocol/not-ready/
transport failures are zero, extrema stay plausible, and the final driver state
is `READY`. `xfer_assert` covers the injected driver transport only; it does not
count application-owned bus traffic such as the separate scanner.

## Software Read-Fault Diagnostic

The Arduino example has an opt-in diagnostic for testing application handling
after a physically successful single-shot receive. Add
`-DSHT3X_EXAMPLE_READ_FAULT=1` to the selected Arduino environment's
`build_flags`, then rebuild and flash. Remove the flag for ordinary firmware.
The native ESP-IDF example has no equivalent hook.

Only the diagnostic build accepts:

```text
fault_read arm
fault_read status
fault_read clear
```

`fault_read arm` substitutes one labelled `I2C_BUS` result after the next
successful six-byte single-shot measurement receive. It does not alter the
received bytes or the example transport's physical success counter. Status and
serial-number reads do not consume the arm, and a failed physical receive leaves
it armed. The next matching successful receive consumes it once. The `drv`
command exposes the driver's acquisition-validity and transport/protocol
counters in this build, `result` retains the terminal job provenance, and
`fault_read status` reports the separate injection count.

A minimal manual sequence is:

```text
mode single
fault_read clear
fault_read arm
read
result
drv
fault_read status
fault_read clear
```

Wait for the scheduled `read` to finish before requesting `result`. These extra
commands are deliberately outside the shared CLI and HIL-runner command
contract, so capture them manually or with a dedicated external plan. This is a
software result substitution on a connected sensor: it does not reproduce an
electrical disconnect, NACK, stuck bus, corrupt frame, or reset, and it does not
advance the real-hardware fault-injection coverage row above.

## What To Record

Record these before treating a run as evidence:

- Operator, date/time, branch, commit hash, and worktree state.
- MCU board, framework, build environment, serial port, and baud rate.
- Firmware version, module, chip marking, I2C address, and sensor variant.
- Supply voltage, bus speed, pull-ups, SDA/SCL pins, cable length, and
  reset/ALERT wiring.
- Fixture details, reference sensor if used, ambient conditions, and deviations.
- Exact build, upload, monitor, and runner commands.
- The generated `serial_transcript.txt`, `summary.md`, `summary.json`,
  `progress.jsonl`, `operator_checklist.md`, and `environment.txt`.
- Photos of board, sensor, wiring, and fixture.
- Logic-analyzer, GPIO, or scope captures whenever ALERT, reset, bus edges,
  pull-ups, or fault behavior is claimed.

## Build And Flash

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
```

If `idf.py` is unavailable, record the exact error and leave pure-IDF hardware
rows as not run. Do not infer them from PlatformIO builds.

## Restore After A Disruptive Run

```text
periodic stop
heater off
alert disable confirm
clear_status confirm
mode single
stretch 0
repeat high
drv
settings
```

The runner performs the applicable cleanup automatically after every built-in
run, including failure paths, as long as serial communication still works. The
restore passes only if the final state is `READY`, `online` is true, settings
show single-shot / high repeatability / no stretch, and no new unexplained
failures appeared. If a fault test left the sensor disconnected, reconnect
first, run `recover confirm`, then run the restore block.

## Ambient Humidity Fixtures

Sensirion treats ambient production testing as a practical alternative to a
climate chamber for pre-calibrated sensors after assembly. It is a
fixture-quality exercise, not a room-air spot check.

- Use an accurate humidity/temperature reference; two references reduce
  reference fluctuation by averaging.
- Keep the DUT and reference at the same absolute temperature. RH is strongly
  temperature-dependent, so thermal mismatch shows up as humidity error.
- Optimize thermal coupling through the jig; keep the humidity coupling volume
  small and shielded from turbulence.
- Housings restrict airflow and lengthen settling time.
- Prestage units near the jig to reduce step changes on entry.
- Avoid local heat sources, direct sunlight, strong lighting, HVAC drafts,
  operator breath, and fast-moving air over the sensor.
- Reflow and other high-temperature assembly steps can leave a temporary RH
  offset while temperature is unaffected. Budget for it or use a documented
  reconditioning process.
- If measurement-system analysis shows both temperature and RH problems, fix
  temperature first; better temperature agreement improves RH agreement.

Datasheet-level facts are summarized in
[the chip notes](reference/sht3x-chip-notes.md).
