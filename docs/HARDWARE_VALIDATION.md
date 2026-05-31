# SHT3x Hardware Validation Matrix

Last updated: 2026-05-31

This file is the maintained place for real board and fixture evidence. Software
tests, CI builds, and fake-transport unit tests are not hardware validation.
Do not mark a row as pass unless the named board, sensor, bus, firmware, and
evidence artifact were actually used.

Detailed command-level procedures live in
`docs/SHT3X_HARDWARE_VALIDATION_MATRIX.md`.

## Results Matrix

| Area | Target | Setup required | Expected result | Current result | Evidence |
|------|--------|----------------|-----------------|----------------|----------|
| ESP-IDF build | ESP32-S3, ESP-IDF 5.4+ | `idf.py -C examples/idf/basic set-target esp32s3 build` | Build succeeds with native IDF example and no Arduino symbols | CI coverage added; local `idf.py` result recorded in `docs/SHT3X_IDF_CI_DOCS_REPORT.md` | CI logs after workflow run |
| ESP-IDF build | ESP32-S2, ESP-IDF 5.4+ | `idf.py -C examples/idf/basic set-target esp32s2 build` | Build succeeds with native IDF example and no Arduino symbols | CI coverage added; local `idf.py` result recorded in `docs/SHT3X_IDF_CI_DOCS_REPORT.md` | CI logs after workflow run |
| Arduino build | ESP32-S3/S2 PlatformIO | `pio run -e esp32s3dev`, `pio run -e esp32s2dev` | Firmware builds with Arduino diagnostic example | Local build PASS on 2026-05-31; hardware smoke still pending | Build logs |
| Address probe | `0x44` and `0x45` | ADDR strapped low/high, I2C scan/probe | Correct address ACKs, other address does not | Pending hardware | Serial log |
| Single-shot | No stretch | Stable supply, known ambient | Request, tick, read sample with valid CRC and plausible T/RH | Pending hardware | Serial/IDF log |
| Single-shot | Clock stretching | Transport timeout >= worst-case tMEAS plus margin | Stretch command completes without timeout | Pending hardware | Serial/IDF log |
| Periodic fetch | 0.5/1/2/4/10 mps | Known bus speed and pullups | Fetch Data returns CRC-valid samples; Break stops mode | Pending hardware | Serial/IDF log |
| ART mode | ESP32-S2/S3 | ART start, fetch, Break | ART cadence works and mode restores after Break | Pending hardware | Serial/IDF log |
| ALERT/status | Periodic mode, ALERT pin wired | Alert thresholds, GPIO capture, status read helper | ALERT pin assertion matches status bits; `readStatusWithModeRestore()` restores mode | Pending hardware | Logic analyzer/log |
| Status clear | Periodic stopped | Known status flags | `clearStatus()` clears bits 15, 11, 10, and 4 only | Pending hardware | Register log |
| Alert limits | All four limits | Stop periodic before access | Raw and physical read/write round trips, write CRC errors detected | Pending hardware | Register log |
| Heater | Controlled ambient | Heater on/off/status | Heater bit changes; self-heating impact is visible and documented | Pending hardware | Temperature/RH log |
| Soft reset | Sensor idle | `softReset()` | Sensor returns to single-shot defaults; status reset bit behavior recorded | Pending hardware | Serial/IDF log |
| Interface reset | Bus-reset callback | SCL toggle implementation | Callback succeeds and later probe/read works | Pending hardware | Logic analyzer/log |
| General-call reset | Isolated bus only | Dedicated `0x00` handle in application adapter | Every supporting device reset is intentional and documented | Not implemented in shipped IDF diagnostic example | Application evidence |
| Fault injection | Fake or jig | Timeout, NACK, CRC mismatch | Specific `Status` codes, health transition, manual recovery | Pending hardware/jig | Test log |
| Humidity production fixture | DUT plus reference sensor(s) | Controlled jig, prestaging, coupling, settling, MSA/Cpk | Limits account for reference accuracy, DUT accuracy, setup variation, and RH offset | Pending fixture validation | Fixture report |

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
