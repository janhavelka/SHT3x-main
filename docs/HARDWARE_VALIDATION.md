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
| Arduino PlatformIO ESP32-S3/S2 builds | Passed locally on the release-readiness branch. | Build logs from the target commit. |
| Pure ESP-IDF ESP32-S3/S2 builds | CI configured; local `idf.py` unavailable in this shell. | Passing GitHub CI log or local ESP-IDF 5.4+ build log. |
| Package validation | `platformio pkg pack` passed locally in release-readiness validation; generated tarball was removed. | Package command log and package content inspection from the final target commit. |

These rows prove build/test coverage only. They do not prove electrical behavior
or sensor correctness.

## Evidence Boundary

Current curated default serial HIL evidence:

- Summary: `docs/hil/20260601_arduino_esp32s3_com17_7847ed0_default_hil.md`
- Generated local source run: `hil_logs/i2c_20260601T183017Z/summary.md`
- Branch: `hardening/sht3x-release-readiness-gaps`
- Code commit recorded by runner: `7847ed0eb83fbeeb9f08c4f5ea14c8a8b24756c9`
- Firmware metadata: `1.5.0 (7847ed0eb83f, Jun  1 2026 20:13:07, clean)`
- Port/target: COM17, ESP32-S3, Arduino PlatformIO `esp32s3dev`
- Expected SHT3x address: `0x44`
- Final verdict: `PASS`

This evidence covers only the default automated serial command sequence listed
in the curated summary. It does not validate physical ALERT pin behavior,
humidity accuracy, fault injection, clock stretching, ESP32-S2 hardware, address
`0x45`, alert writes, destructive reset flows, all periodic rates, or long-soak
stability.

Historical automated smoke-HIL evidence also exists at
`hil_logs/i2c_20260531T155925Z/summary.md`. It is useful only as a historical
run for branch `hardening/sht3x-industry-readiness`, host commit
`8661a38cc70e629cd337ac45c42a1885aefb0cfc`, COM17, and address `0x44`; the
flashed firmware in that older run reported git commit `unknown`.

Operator-reported manual checks are operator context only unless a committed
log, target template, fixture note, GPIO capture, logic-analyzer file, or other
artifact is attached. Dry-run output is runner self-test evidence only.

## Hardware Evidence Status

| Area | Target | Setup required | Expected result | Current result | Evidence |
| --- | --- | --- | --- | --- | --- |
| Address probe | `0x44` | ADDR strapped low, I2C scan/probe | Correct address ACKs and driver is usable | PASS, current default serial HIL | `docs/hil/20260601_arduino_esp32s3_com17_7847ed0_default_hil.md` |
| Address probe | `0x45` | ADDR strapped high, I2C scan/probe | Correct address ACKs and `0x44` absence is understood | Not run | Serial log |
| Single-shot | Low/medium/high repeatability, no stretch | Stable supply, known ambient | CRC-valid plausible T/RH for each repeatability | PASS, current default serial HIL | `docs/hil/20260601_arduino_esp32s3_com17_7847ed0_default_hil.md` |
| Single-shot | Clock stretching | Transport timeout >= worst-case tMEAS plus margin | Stretch command completes without timeout | Not run | Serial log |
| Periodic fetch | 0.5/1/2 mps selected default groups | Known bus speed and pull-ups | Fetch Data returns CRC-valid samples; Break stops mode | PASS, current default serial HIL | `docs/hil/20260601_arduino_esp32s3_com17_7847ed0_default_hil.md` |
| Periodic fetch | 4/10 mps | Known bus speed and pull-ups | Fetch Data returns CRC-valid samples; Break stops mode | Not run | Serial log |
| ART mode | ESP32-S3 default runner | ART start, fetch, Break | ART sample arrives and mode stops cleanly | PASS, current default serial HIL | `docs/hil/20260601_arduino_esp32s3_com17_7847ed0_default_hil.md` |
| ALERT/status | Physical ALERT pin | Alert thresholds, GPIO or logic capture, status-restore helper | ALERT pin assertion matches status bits | Not run | Logic analyzer and serial log |
| Status read | Idle/status_raw/status_restore smoke | Sensor reachable | Status words are parsed and status-restore command returns success | PASS, current default serial HIL; no induced ALERT | `docs/hil/20260601_arduino_esp32s3_com17_7847ed0_default_hil.md` |
| Status clear | Periodic stopped | Known status flags | `clearStatus()` clears bits 15, 11, 10, and 4 only | Not run | Register log |
| Alert limits | Read/show and encode/decode vectors | Stop periodic before access | Alert limits can be read/decoded and app-note vectors match | PASS, current default serial HIL; writes not run | `docs/hil/20260601_arduino_esp32s3_com17_7847ed0_default_hil.md` |
| Alert limits | Write/read round trip | Idle sensor and cleanup plan | Raw and physical write/read round trips; write CRC errors detected | Not run on hardware | Serial log |
| Heater | Status read only | Controlled ambient | Heater status read completes and reports current bit | PASS, current default serial HIL; enable/cooldown not run | `docs/hil/20260601_arduino_esp32s3_com17_7847ed0_default_hil.md` |
| Heater | Enable/disable behavior | Controlled ambient | Heater bit changes; self-heating effect is visible and documented | Not run | Serial log and fixture note |
| Soft reset | Sensor idle | Reset command | Sensor returns to usable idle state; status reset behavior recorded | Not run | Serial log |
| Interface reset | Bus-reset callback | SCL toggle implementation | Callback succeeds and later probe/read works | Not run | Logic analyzer/log |
| General-call reset | Isolated bus only | Application adapter with explicit opt-in | Every supporting device reset is intentional and documented | Not run | Application evidence |
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
health.

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

- `docs/reference/extracted/vendor/Sensirion_Humidity_Sensors_Testing_at_Ambient_Conditions.md`
- `docs/reference/extracted/vendor/SHT3x_datasheet.md`
- `docs/reference/extracted/vendor/SHT3x_HT_AN_AlertMode.md`
