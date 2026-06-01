# SHT3x Hardware Validation Status

Last updated: 2026-05-31

This file tracks evidence status. It is not a procedure. Detailed procedures
live in `docs/SHT3X_HARDWARE_VALIDATION_MATRIX.md`,
`docs/SHT3X_HIL_RUNBOOK.md`, and `docs/SHT3X_I2C_HIL_RUNBOOK.md`.

Software tests, CI builds, and fake-transport unit tests are not hardware
validation. Do not mark a hardware row as pass unless the named board, sensor,
bus, firmware, and evidence artifact were actually used.

## Software Build Status

| Area | Current status | Evidence needed before stronger claim |
| --- | --- | --- |
| Native tests | Passed locally, 70/70. | Test log from the target commit. |
| Arduino PlatformIO ESP32-S3/S2 builds | Passed locally. | Build logs from the target commit. |
| Pure ESP-IDF ESP32-S3/S2 builds | CI configured; local `idf.py` unavailable in this shell. | Passing GitHub CI log or local ESP-IDF 5.4+ build log. |
| Package validation | `platformio pkg pack` passed locally; generated tarball removed. | Package command log from the target commit. |

These rows prove build/test coverage only. They do not prove electrical behavior
or sensor correctness.

## Hardware Evidence Status

| Area | Target | Setup required | Expected result | Current result | Evidence |
| --- | --- | --- | --- | --- | --- |
| Address probe | `0x44` and `0x45` | ADDR strapped low/high, I2C scan/probe | Correct address ACKs, other address does not | Partial: prior smoke-HIL saw `0x44`; `0x45` not run | `hil_logs/i2c_20260531T155925Z/summary.md` |
| Single-shot | No stretch | Stable supply, known ambient | Request, tick, read sample with valid CRC and plausible T/RH | Partial: prior smoke-HIL ran high repeatability only | `hil_logs/i2c_20260531T155925Z/summary.md` |
| Single-shot | Clock stretching | Transport timeout >= worst-case tMEAS plus margin | Stretch command completes without timeout | Not run | Serial log |
| Periodic fetch | 0.5/1/2/4/10 mps | Known bus speed and pull-ups | Fetch Data returns CRC-valid samples; Break stops mode | Partial: prior smoke-HIL ran one `1 mps high` start/fetch/stop | `hil_logs/i2c_20260531T155925Z/summary.md` |
| ART mode | ESP32-S2/S3 | ART start, fetch, Break | ART cadence works and mode stops cleanly | Not run | Serial log |
| ALERT/status | Periodic mode, ALERT pin wired | Alert thresholds, GPIO or logic capture, status-restore helper | ALERT pin assertion matches status bits | Not run | Logic analyzer and serial log |
| Status clear | Periodic stopped | Known status flags | `clearStatus()` clears bits 15, 11, 10, and 4 only | Not run | Register log |
| Alert limits | All four limits | Stop periodic before access | Raw and physical read/write round trips; write CRC errors detected | Partial: prior smoke-HIL read all four limits; writes not run | `hil_logs/i2c_20260531T155925Z/summary.md` |
| Heater | Controlled ambient | Heater on/off/status | Heater bit changes; self-heating effect is visible and documented | Partial: prior smoke-HIL read heater OFF; enable/cooldown not run | `hil_logs/i2c_20260531T155925Z/summary.md` |
| Soft reset | Sensor idle | Reset command | Sensor returns to usable idle state; status reset behavior recorded | Not run | Serial log |
| Interface reset | Bus-reset callback | SCL toggle implementation | Callback succeeds and later probe/read works | Not run | Logic analyzer/log |
| General-call reset | Isolated bus only | Application adapter with explicit opt-in | Every supporting device reset is intentional and documented | Not run; blocked for shipped IDF diagnostic example without adapter evidence | Application evidence |
| Fault injection | Safe jig or emulator | Timeout, NACK, CRC mismatch | Specific `Status` codes, health transition, manual recovery | Not run | Fault test log |
| Humidity production fixture | DUT plus reference sensor(s) | Controlled jig, prestaging, coupling, settling, MSA/Cpk | Limits account for reference accuracy, DUT accuracy, setup variation, and RH offset | Not run | Fixture report |

## Automatic Runner Status

The reusable host runner is `tools/run_sht3x_hil.py`, backed by
`tools/run_i2c_hil.py`. It creates `serial_transcript.txt`, `summary.md`,
`summary.json`, `operator_checklist.md`, and `environment.txt` under
`hil_logs/i2c_<UTC_TIMESTAMP>/`.

Default safe groups now cover version/help, scan/probe/settings/health,
status/status_raw, low/medium/high no-stretch single-shot measurements,
raw/comp cache checks, no-stretch serial/EIC, heater status, alert
read/encode/decode, `status_restore`, selected periodic modes, ART, and final
health. Optional groups are selected with `--include-destructive`,
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
