# SHT3x Hardware Validation And HIL

Last updated: 2026-08-05

This file is the maintained hardware evidence status and HIL procedure. Software
tests, CI builds, dry runs, and fake transports do not prove electrical
behavior, board layout, fixture quality, or sensor accuracy.

No physical HIL validation was performed by a dry run. The exact accepted
metrics and artifact hashes are maintained below. Generated HIL directories,
serial transcripts, summaries, progress logs, and operator worksheets are not
maintained documentation or package content. Archive deliberately accepted raw
evidence outside the checkout, then remove local scratch runs. Dry runs remain
parser/planning checks only.

ACK alone is not chip identity. A bus scan proves only that something
acknowledged an address. Stronger SHT3x evidence is a CRC-checked status read,
CRC-checked measurement, and CRC-checked serial/EIC read captured in the
transcript. Those still do not prove humidity accuracy or ALERT pin behavior.

## Software Build Status

| Area | Current status | Stronger evidence needed |
| --- | --- | --- |
| Native tests | PASS, 118/118 on the current source. | Repeat on a future changed core. |
| Framework-neutral core | PASS under C++17 with `-Wall -Wextra -Wpedantic -Werror`. | Repeat on a future changed core. |
| Arduino PlatformIO ESP32-S3/S2 builds | PASS locally on the exact v1.8.0 main release candidate with pioarduino `55.03.311` and PlatformIO Core `6.1.19`. | Require main-branch CI before tagging; physical ESP32-S2 execution remains open. |
| Pure ESP-IDF ESP32-S3/S2 builds | PASS for the current implementation in GitHub Actions. | Require main-branch CI before tagging; physical pure-IDF execution remains open. |
| Documentation/package validation | Strict Doxygen and package validation pass locally on the exact v1.8.0 release candidate. | Require main-branch CI before tagging. |

These software results do not expand the boundaries of the physical evidence
below. TunnelMonitor-specific ownership and adapter rules are maintained in
[tunnelmonitor-integration.md](tunnelmonitor-integration.md).

## Current Curated Evidence

Latest maintained serial HIL evidence is the exact v1.8.0 main release-candidate
run:

- Exact diagnostic commit: `e156047e392df25b7af1e153dfdbe01f3ce9a8d9`;
  firmware identified itself as library version `1.8.0`, commit
  `e156047e392d`, and clean.
- Fixture: COM19 at 115200 baud, ESP32-S3, Arduino PlatformIO `esp32s3dev`,
  Arduino-ESP32 `3.3.11`, ESP-IDF `5.5.5`, SHT3x at `0x44`, and serial/EIC
  `0x29075EB0`. Other ACK addresses were `0x3C`, `0x41`, `0x50`, and `0x51`.
- Functional selection: 104 command rows. 101 passed, zero failed, and three
  were explicit skips. The application-provided `iface_reset` callback and the
  deliberately disabled general-call transport were `SKIP_UNSUPPORTED`; fault
  injection was `SKIP_REQUIRES_FIXTURE`. The runner therefore retained an
  honest `INCOMPLETE` aggregate verdict.
- Coverage included owner-safe request/job/result/cancel and zero-transfer
  assertions; single-shot low/medium/high with and without clock stretching;
  periodic fetch at 0.5/1/2/4/10 mps; ART; CRC-protected measurement, status,
  and EIC paths; status restore/clear; alert vectors and write/readback cleanup;
  heater on/status/off; soft reset, restoration, self-test, and recovery.
- The 4 mps and 10 mps fetches returned 26.16 C/45.07 %RH and
  26.10 C/45.06 %RH respectively. Final health was `READY`, online, with 86
  successful logical operations, zero logical failures, single-shot mode,
  high repeatability, clock stretching disabled, heater off, and alerts
  disabled. No new soak was requested for this release smoke test.

Recorded SHA-256 fingerprints for the v1.8.0 run:

| Artifact | SHA-256 |
| --- | --- |
| Release-candidate `summary.json` | `0aef8d6938173dc1ce043e97cc6e03acc5e28153f28d17df072deee23546e588` |
| Release-candidate `serial_transcript.txt` | `3d8447f4ec843c0ad6fb12260c2458569aafd918b9fca0a3ee66ab10e5cab049` |
| Release-candidate `progress.jsonl` | `a2c60d5c5264c612177d5e5483fe5044cef212a63431d20dc39faea2984a832d` |

The raw v1.8.0 artifacts are archived outside the checkout under the run ID
`sht3x_20260805T160254Z`; only the durable result and fingerprints are
maintained here.

Earlier one-hour stability evidence was collected with v1.7.0 commit
`524001cad59510aca21003e3c6a738224d640507` on the same COM19 ESP32-S3 fixture:

- Strict one-hour soak: 514,286 measurements and 1,028,572 transfers in
  3,600,003 ms firmware time (3,600.485 s observed by the host), with zero
  logical, transport, protocol, or expected-not-ready failures. Temperature
  remained 26.51..27.03 C and RH 33.35..34.35 %. The run used nonzero request
  identity, `pollJob()`, and milli-unit readout, then finished `READY` in
  single-shot/high-repeatability/no-stretch mode.

Recorded SHA-256 fingerprints for the earlier accepted runs:

| Artifact | SHA-256 |
| --- | --- |
| One-hour `summary.json` | `7ba58e6898c3c01348043bb143bfef052822365cd6366b492f72c9b0339224ab` |
| One-hour `serial_transcript.txt` | `5c03cf1172f2016df2e19979fdf9def626262fdf11f56ed2f447164454f252db` |
| One-hour `progress.jsonl` | `07082a08243142e4584c7370902eca697d3a9208a9565ea2108d334d9a9f260a` |
| Functional `summary.json` | `d619b68cf92c52b9a55d798a4222e00d8adc42dc8c1c5dc759710dcfd3766702` |
| Functional `serial_transcript.txt` | `4ab2852499d8a270fb88b9304c4b07109436b72bbe0e21f3512f7a94e6de105d` |

The files behind these hashes are not present in this checkout and were not
found elsewhere in the local Projects workspace during the 2026-08-01 cleanup.
Only their recorded fingerprints remain here; direct transcript review requires
recovering the external archive.

Together, the COM19 evidence covers the current automatic command surface and
an earlier uninterrupted owner-safe hour. It does not validate physical ALERT pin behavior,
calibrated humidity/temperature accuracy, safe fault injection, ESP32-S2
hardware, address `0x45`, or multi-day/field stability.

## Evidence Status

| Area | Current result | Evidence |
| --- | --- | --- |
| Address probe `0x44` | PASS on COM19 ESP32-S3 | 2026-08-05 v1.8.0 run above |
| Address probe `0x45` | Not run | Needs serial log |
| Single-shot low/medium/high no-stretch | PASS on COM19 ESP32-S3 | 2026-08-05 v1.8.0 run above |
| Single-shot clock stretching | PASS on COM19 ESP32-S3 | 2026-08-05 v1.8.0 run above |
| Periodic fetch 0.5/1/2 mps | PASS on COM19 ESP32-S3 | 2026-08-05 v1.8.0 run above |
| Periodic fetch 4/10 mps | PASS on COM19 ESP32-S3 | 2026-08-05 v1.8.0 run above |
| ART mode | PASS on COM19 ESP32-S3 | 2026-08-05 v1.8.0 run above |
| Status read/status restore | PASS on COM19 ESP32-S3, without induced ALERT | 2026-08-05 v1.8.0 run above |
| Status clear | PASS on COM19 ESP32-S3 | 2026-08-05 v1.8.0 run above |
| Alert read and encode/decode vectors | PASS on COM19 ESP32-S3 | 2026-08-05 v1.8.0 run above |
| Alert write/read round trip | PASS on COM19 ESP32-S3 with exact readback and cleanup | 2026-08-05 v1.8.0 run above |
| Physical ALERT pin | Not run | Needs GPIO or logic-analyzer evidence |
| Heater status read | PASS on COM19 ESP32-S3 | 2026-08-05 v1.8.0 run above |
| Heater enable/disable command/status | PASS on COM19 ESP32-S3; controlled self-heating not measured | 2026-08-05 v1.8.0 run above |
| Soft reset/recover/restore | PASS on COM19 ESP32-S3 | 2026-08-05 v1.8.0 run above |
| Interface reset | Unsupported by current firmware callback | 2026-08-05 explicit `SKIP_UNSUPPORTED` |
| General-call reset | Arm gate PASS; transport-disabled confirmation skipped because other devices share the bus | Needs isolated bus evidence and an application-supplied bus-wide transport |
| ESP32-S2 hardware smoke | Not run | Needs ESP32-S2 serial log |
| Fault injection | Fixture procedure selected but `SKIP_REQUIRES_FIXTURE` | Needs safe jig/interposer/emulator or documented manual fault evidence |
| Long soak | Strict uninterrupted one-hour PASS: 514,286 measurements, 1,028,572 transfers, zero failure deltas | Earlier v1.7.0 run above; no new soak was requested |
| Humidity production fixture | Not run | Needs reference fixture report |

## Serial Runner

The reusable host runner is `tools/run_sht3x_hil.py`. It drives the Arduino or
ESP-IDF diagnostic CLI over a serial port from the host. It does not talk
directly to I2C and does not flash firmware.

Dry-run only:

```bash
python tools/run_sht3x_hil.py --dry-run --expect-address 0x44 --board esp32s3 --target-name desk --operator <name>
```

Hardware run:

```bash
python tools/run_sht3x_hil.py --port COMx --baud 115200 --expect-address 0x44 --board esp32s3 --target-name desk --operator <name>
```

By default the runner derives the expected library version from `library.json`,
expects the current checkout commit, and requires firmware built from a clean
worktree, and a live run is refused when tracked checkout files are dirty.
Override the identity only deliberately with
`--expect-library-version`, `--expect-library-commit`, or
`--allow-dirty-firmware`; the selected and observed values are retained in the
environment and summary artifacts. A failed version/commit preflight stops the
run before optional destructive commands.

The `version` response also records the runtime framework, build target, and
Arduino-core/ESP-IDF version where applicable. This prevents a command-compatible
but wrong framework image from being accepted as the intended target.

The PlatformIO package includes the HIL runner named above.
Parser tests, repository guards, and other maintenance-only tooling still
require a full repository checkout.

The runner creates `hil_logs/sht3x_<UTC_TIMESTAMP>/` and writes:

- `serial_transcript.txt`
- `summary.md`
- `summary.json`
- `progress.jsonl`
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
tests. It explicitly confirms the bounded `status_restore` interruption and
proves that request inspection and cancellation perform zero I2C callbacks.
This block is checked against `tools/run_sht3x_hil.py` by
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

The common minimum serial contract is `version`, `scan`, `probe`, `settings`,
and `drv`; `drv` is the health snapshot command.

Expected evidence includes version text, help output, scan output containing
the expected address, `READY` or online driver state, parseable status/status
raw, plausible single-shot measurements for low/medium/high repeatability,
raw/comp cached samples, serial/EIC, heater OFF, alert limit reads, alert
encode/decode vectors, periodic-mode status-restore fields, selected periodic
start/fetch/stop paths, ART start/fetch/stop or explicit unsupported status,
zero-transfer request/progress/cancel/result evidence, and final READY health
with zero unexplained failures.

## Opt-In Groups

| Flag | Coverage | Evidence boundary |
| --- | --- | --- |
| `--include-destructive` | confirmed selftest, recover, clear status, soft reset, restore, interface reset | Alters device/status state; not part of default smoke. |
| `--include-bus-wide-reset` | `greset arm` followed immediately by `greset confirm` | Also requires `--include-destructive` and an isolated bus. Examples keep general-call transport disabled by default, so unsupported is an honest skip unless the application deliberately supplies a bus-wide transport. |
| `--include-soak --soak-count N` | bounded stress and mixed-operation stress | Only proves the configured count. |
| `--include-soak --soak-duration-s N` | firmware-side low-USB `i2c_soak N` measurement loop | Only proves the configured duration when the compact summary and final health pass. |
| `--include-clock-stretch` | stretch-enabled read and serial/EIC | Unsupported or timeout behavior must be recorded explicitly. |
| `--include-alert-write` | software-visible alert write/readback and cleanup | Does not prove physical ALERT pin transitions. |
| `--include-heater` | brief heater enable/status/disable plus cleanup verification | Proves the command/status path only, not controlled self-heating performance. |
| `--include-all-periodic-rates` | additional 4 and 10 mps periodic fetches | Needs final health review and self-heating notes for stronger claims. |
| `--include-output-tests` | ALERT output operator/GPIO procedure | PASS requires GPIO or logic-analyzer evidence. |
| `--include-fault-tests` | fault/unplug/CRC-injection procedure | PASS requires safe jig/interposer/emulator or documented manual fault evidence. |

`--include-output-tests` can produce `OPERATOR_REVIEW_REQUIRED` when operator
or GPIO evidence is missing. `--include-fault-tests` records
`SKIP_REQUIRES_FIXTURE` without a suitable fixture. Unsupported behavior should
be recorded as `SKIP_UNSUPPORTED`, not hidden as a pass.

Final runner verdict values are `PASS`, `FAIL`, `OPERATOR_REVIEW_REQUIRED`, and
`INCOMPLETE`. A `PASS` verdict is limited to the selected automated serial
groups and attached artifacts. All verdicts except `PASS` return nonzero by
default; `--allow-incomplete` is an explicit planning-only override for
`INCOMPLETE` or operator-review runs.

Every firmware mutation in an automated plan uses the same literal `confirm`
syntax shown by both CLIs. General-call reset additionally uses a one-shot
`greset arm`/`greset confirm` pair; the runner disarms during cleanup if an
armed step fails. A `--commands <file>` plan is classified before serial output
or artifacts are created. Unknown and mutation-like commands are rejected,
known mutations retain their required opt-in and cleanup policy, and raw
command words cannot bypass heater, alert-write, or 4/10 mps opt-ins.
The first executable custom-plan line must be exact `version`; no probe, read,
or mutation is allowed before runtime framework/library identity succeeds.
Recognized noncanonical read-only commands are accepted only with
`--allow-custom-read-only-review`, and remain operator-review rows rather than
automatic passes.

The firmware-side duration path uses nonzero request IDs, absolute deadlines,
one-callback `pollJob()` steps, zero-I2C cancellation, and milli-unit sample
readout. Its compact summary passes only when the measurement loop ran for at
least the requested time, every sample succeeded, logical and transport counts
agree, protocol/not-ready/transport failures are zero, extrema remain broadly
plausible, and final driver state is `READY`. This validates the exercised
sensor/transport path; it does not substitute for a consumer-specific adapter
test. `xfer_assert` covers injected driver-adapter callbacks only and does not
count application-owned bus traffic that bypasses that transport, such as the
separate scanner.

## Target Record Checklist

Record these fields before treating a hardware run as evidence:

- Operator, date/time, branch, commit hash, and worktree state.
- MCU board, framework, build environment, serial port, and baud rate.
- Firmware version, device/module, chip marking, I2C address, and sensor variant.
- Supply voltage, bus speed, pull-ups, SDA/SCL pins, cable length, and reset/ALERT wiring.
- Fixture details, reference sensor if used, ambient conditions, and deviations.
- Exact build, upload, monitor, and runner commands.
- Attached `serial_transcript.txt`, `summary.md`, `summary.json`,
  `progress.jsonl`, `operator_checklist.md`, and `environment.txt`.
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
alert disable confirm
clear_status confirm
mode single
stretch 0
repeat high
drv
settings
```

The runner performs the applicable cleanup automatically after every built-in
run, including failure paths when serial communication remains available.
Pass the restore step only if the final state is `READY`, `online` is true,
settings show single-shot/high-repeatability/no-stretch, and there are no new
unexplained failures. If a fault test leaves the sensor disconnected, reconnect
first, run `recover confirm`, then run the restore step.

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

Source material relevant to the driver is summarized in
[the maintained chip notes](reference/sht3x-chip-notes.md). The documentation
index lists the retained vendor source files separately.
