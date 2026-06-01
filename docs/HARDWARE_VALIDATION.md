# SHT3x Hardware Validation Status

Last updated: 2026-06-01

This file tracks evidence status. It is not a procedure. Detailed procedures
live in `docs/SHT3X_HARDWARE_VALIDATION_MATRIX.md`,
`docs/SHT3X_HIL_RUNBOOK.md`, and `docs/SHT3X_I2C_HIL_RUNBOOK.md`.

Software tests, CI builds, and fake-transport unit tests are not hardware
validation. Do not mark a hardware row as pass unless the named board, sensor,
bus, firmware, and evidence artifact were actually used.

## Software Build Status

| Area | Current status | Evidence needed before stronger claim |
| --- | --- | --- |
| Native tests | Passed locally on the release-readiness branch. | Test log from the target commit. |
| Arduino PlatformIO ESP32-S3/S2 builds | Passed locally in the current release-readiness sequence. | Build logs from the target commit. |
| Pure ESP-IDF ESP32-S3/S2 builds | CI configured; local `idf.py` unavailable in this shell. | Passing GitHub CI log or local ESP-IDF 5.4+ build log. |
| Package validation | `platformio pkg pack` passed locally in this cleanup chunk; inspected package had 45 entries and no generated HIL logs/build outputs/reference PDFs. | Package command log and package content inspection from the final target commit. |

These rows prove build/test coverage only. They do not prove electrical behavior
or sensor correctness.

## Evidence Boundary

Tracked automated smoke-HIL evidence exists at
`hil_logs/i2c_20260531T155925Z/summary.md`. It records `PASS` for a limited
serial command sequence on COM17, address `0x44`, branch
`hardening/sht3x-industry-readiness`, host runner/worktree commit
`8661a38cc70e629cd337ac45c42a1885aefb0cfc`, with `Dry run: False`.

The flashed firmware reported SHT3x library version `1.5.0` with git commit
`unknown`, so do not claim exact flashed firmware source commit without a build
or upload record that ties the binary to that commit.

That smoke run is useful evidence for the exact target, address, and commands
in the log. This release-readiness branch has changed since then, so the final
release candidate needs a rerun before the smoke-HIL result is used as
current-head hardware evidence.

Operator-reported manual checks are operator context only unless a committed
log, target template, fixture note, GPIO capture, logic-analyzer file, or other
artifact is attached. Dry-run output is runner self-test evidence only.

## Hardware Evidence Status

| Area | Target | Setup required | Expected result | Current result | Evidence |
| --- | --- | --- | --- | --- | --- |
| Address probe | `0x44` | ADDR strapped low, I2C scan/probe | Correct address ACKs and driver is usable | PASS, limited to the historical smoke-HIL target and command log | `hil_logs/i2c_20260531T155925Z/summary.md` |
| Address probe | `0x45` | ADDR strapped high, I2C scan/probe | Correct address ACKs and `0x44` absence is understood | Not run | Serial log |
| Single-shot | High repeatability, no stretch | Stable supply, known ambient | Request, tick, read sample with valid CRC and plausible T/RH | PASS, limited to the historical smoke-HIL target and command log | `hil_logs/i2c_20260531T155925Z/summary.md` |
| Single-shot | Low/medium repeatability, no stretch | Stable supply, known ambient | CRC-valid plausible T/RH for each repeatability | Not run on hardware | Serial log |
| Single-shot | Clock stretching | Transport timeout >= worst-case tMEAS plus margin | Stretch command completes without timeout | Not run | Serial log |
| Periodic fetch | 1 mps high | Known bus speed and pull-ups | Fetch Data returns CRC-valid sample; Break stops mode | PASS, limited to one historical smoke-HIL start/fetch/stop | `hil_logs/i2c_20260531T155925Z/summary.md` |
| Periodic fetch | 0.5/2/4/10 mps | Known bus speed and pull-ups | Fetch Data returns CRC-valid samples; Break stops mode | Not run on hardware | Serial log |
| ART mode | ESP32-S2/S3 | ART start, fetch, Break | ART cadence works and mode stops cleanly | Not run | Serial log |
| ALERT/status | Physical ALERT pin | Alert thresholds, GPIO or logic capture, status-restore helper | ALERT pin assertion matches status bits | Not run | Logic analyzer and serial log |
| Status read | Idle/status_raw/status_restore smoke | Sensor reachable | Status words are parsed and status-restore command returns success | PASS, limited to historical smoke-HIL command outputs; no induced ALERT | `hil_logs/i2c_20260531T155925Z/summary.md` |
| Status clear | Periodic stopped | Known status flags | `clearStatus()` clears bits 15, 11, 10, and 4 only | Not run | Register log |
| Alert limits | Read/show only | Stop periodic before access | Alert limits can be read and decoded by the diagnostic CLI | PASS, limited to historical smoke-HIL read/show; writes not run | `hil_logs/i2c_20260531T155925Z/summary.md` |
| Alert limits | Write/read round trip | Idle sensor and cleanup plan | Raw and physical write/read round trips; write CRC errors detected | Not run on hardware | Serial log |
| Heater | Status read only | Controlled ambient | Heater status read completes and reports current bit | PASS, limited to historical smoke-HIL status read; enable/cooldown not run | `hil_logs/i2c_20260531T155925Z/summary.md` |
| Heater | Enable/disable behavior | Controlled ambient | Heater bit changes; self-heating effect is visible and documented | Not run | Serial log and fixture note |
| Soft reset | Sensor idle | Reset command | Sensor returns to usable idle state; status reset behavior recorded | Not run | Serial log |
| Interface reset | Bus-reset callback | SCL toggle implementation | Callback succeeds and later probe/read works | Not run | Logic analyzer/log |
| General-call reset | Isolated bus only | Application adapter with explicit opt-in | Every supporting device reset is intentional and documented | Not run; blocked for shipped IDF diagnostic example without adapter evidence | Application evidence |
| ESP32-S2 hardware smoke | ESP32-S2 board plus SHT3x | Diagnostic firmware flashed on ESP32-S2 | Same smoke sequence passes on ESP32-S2 hardware | Not run | Serial log |
| Fault injection | Safe jig or emulator | Timeout, NACK, CRC mismatch | Specific `Status` codes, health transition, manual recovery | Not run | Fault test log |
| Long soak | Stable fixture | Timed stress/periodic run | No unexplained failures, leaks, state drift, or missed samples | Not run | Soak log and fixture note |
| Humidity production fixture | DUT plus reference sensor(s) | Controlled jig, prestaging, coupling, settling, MSA/Cpk | Limits account for reference accuracy, DUT accuracy, setup variation, and RH offset | Not run | Fixture report |

## Automatic Runner Status

The reusable host runner is `tools/run_sht3x_hil.py`, backed by
`tools/run_i2c_hil.py`. It creates `serial_transcript.txt`, `summary.md`,
`summary.json`, `operator_checklist.md`, and `environment.txt` under
`hil_logs/i2c_<UTC_TIMESTAMP>/`.

The host runner is a repository-checkout tool. It is not evidence that the
PlatformIO package payload contains `tools/`.

The current runner default can cover version/help, scan/probe/settings/health,
status/status_raw, low/medium/high no-stretch single-shot measurements,
raw/comp cache checks, no-stretch serial/EIC, heater status, alert
read/encode/decode, `status_restore`, selected periodic modes, ART, and final
health. The tracked 2026-05-31 PASS log used an older, smaller sequence, so it
must not be described as proof for current runner groups that were not in that
transcript.

Optional groups are selected with `--include-destructive`,
`--include-bus-wide-reset`, `--include-soak`, `--include-clock-stretch`,
`--include-alert-write`, `--include-all-periodic-rates`,
`--include-output-tests`, and `--include-fault-tests`.

Runner `PASS` means only the selected automated serial groups passed. ALERT pin,
humidity accuracy, fault injection, and long-soak claims still require the
matching GPIO/logic-analyzer, reference fixture, fault jig, or timed soak
evidence.

## Ambient Humidity Test Notes

Sensirion's ambient testing guidance treats ambient production testing as a
practical alternative to a climate chamber for pre-calibrated sensors after
assembly. It is still a fixture-quality exercise, not a generic room-air check.

Key production constraints:

- Use an accurate humidity and temperature reference sensor; two references can
  reduce reference-value fluctuation by averaging.
- Keep the DUT and reference at the same absolute temperature. RH is strongly
  temperature-dependent, so thermal mismatch appears as humidity error.
- Optimize thermal coupling through the jig where possible, and keep humidity
  coupling volume small and shielded from turbulence.
- Housings slow response because they restrict airflow; expect longer settling
  times than with a bare sensor on a PCB.
- Settling time trades throughput against offset and variation. Short waits
  require wider test limits.
- Prestaging units near the jig reduces local temperature/RH step changes when
  a unit enters the fixture.
- Avoid local heat sources, direct sunlight, strong lighting, HVAC drafts,
  operator breath/body heat, and fast moving air over the sensor.
- Reflow or other high-temperature assembly steps can cause a temporary RH
  offset, while temperature remains unaffected. Account for that in limits or
  use a documented reconditioning process.
- If measurement-system analysis shows both temperature and RH problems, fix
  temperature first because better temperature agreement improves RH agreement.

Source documents:

- `docs/pdf-extracted-md/Sensirion_Humidity_Sensors_Testing_at_Ambient_Conditions.md`
- `docs/pdf-extracted-md/SHT3x_datasheet.md`
- `docs/pdf-extracted-md/SHT3x_HT_AN_AlertMode.md`
