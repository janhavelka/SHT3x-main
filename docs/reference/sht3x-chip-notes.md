# SHT3x Chip Notes

Last updated: 2026-06-16

This file preserves the original compact chip-documentation notes that were
previously split across `00` through `08` Markdown files. Main protocol facts
come from the SHT3x-DIS datasheet unless another Sensirion source is named.
Units use ASCII forms such as `degC`, `uA`, `+/-`, and `kOhm`.

## Source Inventory

| Source PDF | Role | Version / date | Pages | Notes |
| --- | --- | --- | ---: | --- |
| `vendor/SHT3x_datasheet.pdf` | Primary datasheet | Version 6, February 2019 | 22 | Electrical specs, pinout, I2C protocol, commands, status, CRC, conversions, packaging. |
| `vendor/SHT3x_HT_AN_AlertMode.pdf` | Alert-mode application note | Version 3.1, November 2023 | 5 | ALERT pin behavior and alert-limit command encodings. |
| `vendor/Sensirion_electronic_identification_code_SHT3x.pdf` | Serial-number application note | Version 3, January 2023 | 3 | 32-bit serial number readout commands. |
| `vendor/SHT3x_membrane_option_datasheet.pdf` | Variant option datasheet | Version 4.1, March 2025 | 4 | PTFE membrane option, protection, orderable variants. |
| `vendor/Sensirion_Humidity_Sensors_Testing_at_Ambient_Conditions.pdf` | Test guidance | Version 3, October 2022 | 10 | Ambient-condition fixture guidance for SHT3x verification after assembly; no I2C command additions. |
| `vendor/Sensirion_AppNotes_Humidity_Sensors_at_a_Glance.pdf` | Supplemental humidity formulas | May 2025 | 3 | Dew point and humidity formulas for application code; no SHT3x command/register additions. |

The previous `00_document_inventory.md` also listed generated raw Markdown
extracts under `docs/reference/extracted/vendor/`. Those generated dumps were
removed from the active docs tree; the vendor PDFs above are the retained
authoritative sources. Inspect the PDFs when exact vendor wording, legal
notices, figures, or mechanical drawings matter.

## Original Compact Note Map

| Original file | Purpose preserved here |
| --- | --- |
| `00_document_inventory.md` | Source inventory, scope notes, and original note map. |
| `01_chip_overview.md` | Sensor family, outputs, accuracy anchors, and package. |
| `02_pinout_and_signals.md` | 8-pin DFN pinout, address selection, ALERT, reset, and pull-ups. |
| `03_electrical_and_timing.md` | Supply, current, reset/measurement timing, I2C limits, and absolute ratings. |
| `04_protocol_commands_and_transactions.md` | I2C command framing, data readout, command table, CRC, and conversions. |
| `05_register_map.md` | Command/status pseudo-register map for the command-based device. |
| `06_modes_interrupts_status_and_faults.md` | Single-shot, periodic, ART, ALERT, heater, status, and CRC faults. |
| `07_initialization_reset_and_operational_notes.md` | Startup, reset, measurement sequences, serial number, validation, and handling notes. |
| `08_variant_differences_and_open_questions.md` | SHT30/SHT31/SHT35 and membrane-option differences plus open questions. |

## Chip Overview

SHT3x-DIS is a digital humidity and temperature sensor family with
factory-calibrated, linearized, and temperature-compensated outputs. It uses
I2C, supports two selectable addresses, and is packaged in a 2.5 mm x 2.5 mm x
0.9 mm 8-pin DFN. Source: datasheet, p. 1.

| Variant | Humidity accuracy anchor | Temperature accuracy anchor | Source |
| --- | --- | --- | --- |
| SHT30 | Typ. 2 %RH | Typ. 0.2 degC over 0-65 degC | Datasheet, p. 2 |
| SHT31 | Typ. 2 %RH | Typ. 0.2 degC over 0-90 degC | Datasheet, p. 2 |
| SHT35 | Typ. +/-1.5 %RH | Typ. +/-0.1 degC over 20-60 degC | Datasheet, p. 2 |

| Parameter | Value | Source |
| --- | ---: | --- |
| Humidity specified range | 0 to 100 %RH | Datasheet, p. 2 |
| Temperature specified range | -40 to 125 degC | Datasheet, p. 2 |
| Humidity repeatability | Low 0.21 %RH, medium 0.15 %RH, high 0.08 %RH typ. | Datasheet, p. 2 |
| Temperature repeatability | Low 0.15 degC, medium 0.08 degC, high 0.04 degC typ. | Datasheet, p. 2 |
| Humidity resolution | 0.01 %RH typ. | Datasheet, p. 2 |
| Temperature resolution | 0.01 degC typ. | Datasheet, p. 2 |
| RH response time | 8 s to 63% humidity step at 25 degC and 1 m/s airflow | Datasheet, p. 2 |

The sensor supports single-shot measurements, periodic measurements at
0.5/1/2/4/10 measurements per second, ART mode at 4 Hz, status readout, soft
reset, heater control, and alert limits. Source: datasheet, pp. 10-13.

## Pinout And Signals

| Pin | Name | Driver/wiring meaning | Source |
| ---: | --- | --- | --- |
| 1 | `SDA` | I2C serial data, input/output. | Datasheet, p. 8 |
| 2 | `ADDR` | I2C address select; connect high or low, do not float. | Datasheet, pp. 8-9 |
| 3 | `ALERT` | Alarm output; leave floating if unused. | Datasheet, pp. 8-9 |
| 4 | `SCL` | I2C serial clock, input/output. | Datasheet, p. 8 |
| 5 | `VDD` | Supply voltage. | Datasheet, p. 8 |
| 6 | `nRESET` | Active-low reset input. If unused, leave floating or connect to `VDD` through >=2 kOhm. | Datasheet, pp. 8-9 |
| 7 | `R` | No electrical function; connect to `VSS`. | Datasheet, p. 8 |
| 8 | `VSS` | Ground. | Datasheet, p. 8 |

| `ADDR` level | 7-bit address | Source |
| --- | ---: | --- |
| Logic low | `0x44` (default) | Datasheet, p. 9 |
| Logic high | `0x45` | Datasheet, p. 9 |

The `ADDR` level may be changed dynamically, but it must remain constant from
I2C START until the communication finishes. Source: datasheet, p. 8.

Signal notes:

- `SCL` and `SDA` are open-drain I/Os with diodes to `VDD` and `VSS`; use
  external pull-ups such as 10 kOhm, sized for bus capacitance and speed.
  Source: datasheet, p. 8.
- `ALERT` switches high when alert conditions are met. The alert application
  note says the ALERT pin is push-pull. Source: datasheet, p. 9; alert note,
  p. 1.
- `nRESET` needs a low pulse of at least 1 us to reset the sensor; it has an
  internal typ. 50 kOhm pull-up to `VDD`. Source: datasheet, p. 9.
- Decouple `VDD` with 100 nF placed close to the sensor. Source: datasheet,
  p. 8.

## Electrical And Timing

| Parameter | Value | Source |
| --- | ---: | --- |
| `VDD` operating range | 2.15 V to 5.5 V | Datasheet, p. 6 |
| Power-on reset level `VPOR` | 1.8 V min, 2.10 V typ., 2.15 V max | Datasheet, p. 6 |
| Supply slew rate | 20 V/ms max between `VDD,min` and `VDD,max` | Datasheet, p. 6 |
| Idle current, single-shot mode at 25 degC | typ. 0.2 uA, max 2.0 uA | Datasheet, p. 6 |
| Idle current, periodic mode | typ. 45 uA | Datasheet, p. 6 |
| Measuring current | typ. 600 uA, max 1500 uA | Datasheet, p. 6 |
| Average current, 1 Hz low-repeatability single-shot | typ. 1.7 uA | Datasheet, p. 6 |
| Heater power | 3.6 mW to 33 mW depending on supply voltage | Datasheet, p. 6 |

| Parameter | 2.4-5.5 V value | 2.15-<2.4 V value | Source |
| --- | ---: | ---: | --- |
| Power-up time | typ. 0.5 ms, max 1 ms | typ. 0.5 ms, max 1.5 ms | Datasheet, p. 7 |
| Soft reset time | typ. 0.5 ms, max 1.5 ms | not repeated in low-voltage table | Datasheet, p. 7 |
| `nRESET` low pulse | min 1 us | min 1 us | Datasheet, p. 7 |
| Low repeatability measurement | typ. 2.5 ms, max 4 ms | typ. 2.5 ms, max 4.5 ms | Datasheet, p. 7 |
| Medium repeatability measurement | typ. 4.5 ms, max 6 ms | typ. 4.5 ms, max 6.5 ms | Datasheet, p. 7 |
| High repeatability measurement | typ. 12.5 ms, max 15 ms | typ. 12.5 ms, max 15.5 ms | Datasheet, p. 7 |
| Minimum time after command before next command | 1 ms | 1 ms | Datasheet, p. 9 |

I2C clock can be selected from 0 to 1000 kHz; Fast Mode up to 400 kHz follows
the I2C Fast Mode standard, and up to 1 MHz follows the datasheet timing table.
The I2C timing table lists `fSCL` max 1000 kHz, SDA setup min 100 ns,
rise/fall max 300 ns, and SDA valid time max 0.9 us. Absolute max `VDD` is
-0.3 V to 6 V; pin voltages on SDA/ADDR/ALERT/SCL/nRESET are -0.3 V to
`VDD + 0.3 V`; input current on any pin is +/-100 mA. Source: datasheet,
pp. 7-8, 21.

## Protocol Commands And Transactions

SHT3x-DIS uses 16-bit I2C commands. Measurement data is returned as 16-bit
temperature followed by CRC, then 16-bit humidity followed by CRC. Source:
datasheet, pp. 9-10.

| Item | Behavior | Source |
| --- | --- | --- |
| Addresses | `0x44` with `ADDR` low, `0x45` with `ADDR` high. | Datasheet, p. 9 |
| Commands | 16-bit command words already include a 3-bit CRC in the command encoding. | Datasheet, p. 9 |
| Data CRC | Every 16-bit data word is followed by 8-bit CRC. Write CRC is mandatory; read CRC processing is up to the master. | Datasheet, pp. 9, 14 |
| CRC parameters | CRC-8 polynomial `0x31`, init `0xff`, no reflection, final XOR `0x00`; example CRC of `0xbeef` is `0x92`. | Datasheet, p. 14 |
| Read order | Temperature word first, then relative humidity word. | Datasheet, p. 10 |

The datasheet states raw words are unsigned 16-bit, already linearized and
compensated. Source: datasheet, p. 14.

| Output | Conversion |
| --- | --- |
| Temperature | `T_degC = -45 + 175 * rawT / (2^16 - 1)` |
| Relative humidity | `RH_percent = 100 * rawRH / (2^16 - 1)` |

| Operation | Bytes after address phase | Notes | Source |
| --- | ---: | --- | --- |
| Send command | 2 write bytes | Command MSB then LSB. | Datasheet, p. 9 |
| Single-shot read | 6 read bytes | Temperature MSB/LSB/CRC, then humidity MSB/LSB/CRC. | Datasheet, p. 10 |
| Periodic fetch `0xE000` | 6 read bytes | Same temperature/RH order; the data memory is reset after fetch. | Datasheet, p. 11 |
| Read status `0xF32D` | 3 read bytes | 16-bit status word plus CRC. | Datasheet, p. 13 |
| Write alert limit | 5 write bytes | 16-bit command, 16-bit reduced-limit value, CRC. | Alert note, p. 2 |
| Read alert limit | 3 read bytes | 16-bit reduced-limit value plus CRC. | Alert note, p. 2 |
| Get serial number | 6 read bytes | Two 16-bit words with CRCs; `SNB_3` is MSB and `SNB_0` is LSB. | Serial-number note, p. 1 |

| Command | Hex | Notes | Source |
| --- | ---: | --- | --- |
| Single-shot high repeatability, clock stretching | `0x2C06` | One RH/T pair. | Datasheet, p. 10 |
| Single-shot medium repeatability, clock stretching | `0x2C0D` | One RH/T pair. | Datasheet, p. 10 |
| Single-shot low repeatability, clock stretching | `0x2C10` | One RH/T pair. | Datasheet, p. 10 |
| Single-shot high repeatability, no clock stretching | `0x2400` | Master reads later; NACK while data not ready. | Datasheet, p. 10 |
| Single-shot medium repeatability, no clock stretching | `0x240B` | One RH/T pair. | Datasheet, p. 10 |
| Single-shot low repeatability, no clock stretching | `0x2416` | One RH/T pair. | Datasheet, p. 10 |
| Periodic ART | `0x2B32` | Starts ART mode at 4 Hz. | Datasheet, p. 11 |
| Fetch data | `0xE000` | Reads periodic/ART buffered data; NACK if no data is present; buffer clears after fetch. | Datasheet, p. 11 |
| Break periodic | `0x3093` | Stops periodic acquisition; takes 1 ms. | Datasheet, pp. 11-12 |
| Soft reset | `0x30A2` | Reinitializes system controller and reloads calibration data. | Datasheet, p. 12 |
| General-call reset | address `0x00`, byte `0x06` | Resets all devices on bus that support I2C general call. | Datasheet, p. 12 |
| Heater enable / disable | `0x306D` / `0x3066` | Heater is for plausibility checking only; disabled after reset. | Datasheet, pp. 12-13 |
| Read status / clear status | `0xF32D` / `0x3041` | Clear affects flags 15, 11, 10, and 4. | Datasheet, p. 13 |
| Get serial number, stretching / no stretching | `0x3780` / `0x3682` | Returns two words plus CRCs; 32-bit serial number. | Serial-number note, p. 1 |

| MPS | High | Medium | Low | Source |
| ---: | ---: | ---: | ---: | --- |
| 0.5 | `0x2032` | `0x2024` | `0x202F` | Datasheet, p. 11 |
| 1 | `0x2130` | `0x2126` | `0x212D` | Datasheet, p. 11 |
| 2 | `0x2236` | `0x2220` | `0x222B` | Datasheet, p. 11 |
| 4 | `0x2334` | `0x2322` | `0x2329` | Datasheet, p. 11 |
| 10 | `0x2737` | `0x2721` | `0x272A` | Datasheet, p. 11 |

## Pseudo-Registers And Status Bits

SHT3x-DIS exposes command-addressed state rather than byte registers:
measurement output, a 16-bit status word, heater state, alert limits, and the
32-bit serial number are reached through 16-bit I2C commands. Source:
datasheet, pp. 9-14; alert note, pp. 1-2; serial-number note, p. 1.

| State / function | Access command(s) | Driver notes | Source |
| --- | --- | --- | --- |
| Measurement output | Single-shot command or fetch data `0xE000` | Returns temperature word + CRC, then humidity word + CRC. | Datasheet, pp. 10-11 |
| Status register | Read `0xF32D`; clear `0x3041` | 16-bit status word. Clear resets flags bit 15, 11, 10, and 4. | Datasheet, p. 13 |
| Heater state | Enable `0x306D`, disable `0x3066`; status bit 13 reports state | Heater disabled after reset. | Datasheet, pp. 12-13 |
| Alert limits | Read/write high set, high clear, low clear, low set | Alert note defines command encodings and reduced limit data format. | Alert note, pp. 1-2 |
| Serial number | `0x3780` with stretching or `0x3682` without stretching | Returns two words plus CRCs; assemble 32-bit serial number as MSW then LSW. | Serial-number note, p. 1 |

| Bit(s) | Meaning | Default | Source |
| --- | --- | ---: | --- |
| 15 | Alert pending (`1` means at least one pending alert) | `1` | Datasheet, p. 13 |
| 13 | Heater status (`1` on) | `0` | Datasheet, p. 13 |
| 11 | RH tracking alert | `0` | Datasheet, p. 13 |
| 10 | Temperature tracking alert | `0` | Datasheet, p. 13 |
| 4 | System reset detected | `1` | Datasheet, p. 13 |
| 1 | Command status (`1` means last command not processed) | `0` | Datasheet, p. 13 |
| 0 | Write data checksum status (`1` means last write checksum failed) | `0` | Datasheet, p. 13 |

## Alert Limits

| Limit | Read | Write | Initial physical value | Initial hex | Source |
| --- | ---: | ---: | --- | ---: | --- |
| High set | `0xE11F` | `0x611D` | 80 %RH / 60 degC | `0xCD33` | Alert note, p. 2 |
| High clear | `0xE114` | `0x6116` | 79 %RH / 58 degC | `0xC92D` | Alert note, p. 2 |
| Low clear | `0xE109` | `0x610B` | 22 %RH / -9 degC | `0x3869` | Alert note, p. 2 |
| Low set | `0xE102` | `0x6100` | 20 %RH / -10 degC | `0x3466` | Alert note, p. 2 |

SHT3x alert-limit storage uses reduced precision: 7 bits for humidity and
9 bits for temperature. Approximate limit resolution is 1 %RH and 0.5 degC.
The 16-bit alert limit word packs humidity and temperature thresholds as
`RH[6:0]` and `T[8:0]`; the application note shows the initial high-set limit
`80 %RH / 60 degC` as `0xCD33`. Convert physical thresholds with the alert-note
equations before writing the reduced value and its CRC. Source: alert note,
pp. 1-3.

## Modes, Interrupts, Status, And Faults

| Mode | Behavior | Readout | Source |
| --- | --- | --- | --- |
| Single-shot | One command triggers one RH/T pair. Commands select repeatability and clock stretching. | Read sensor after measurement; with no stretching, read header NACKs until data is ready. | Datasheet, p. 10 |
| Periodic | One command starts a stream of RH/T pairs at selected mps and repeatability. | Use fetch data `0xE000`; data memory clears after fetch. | Datasheet, pp. 10-11 |
| ART | Accelerated response-time mode starts acquisition at 4 Hz. | Same fetch and break flow as periodic mode. | Datasheet, p. 11 |

ALERT behavior:

- Alert mode is active whenever the sensor operates in periodic data
  acquisition mode. Source: alert note, p. 1.
- The ALERT pin changes state when humidity or temperature crosses programmable
  limits; status bits identify RH and temperature alert causes. Source:
  datasheet, p. 13; alert note, p. 1.
- Alert can be deactivated per channel by setting the minimum set point higher
  than the maximum set point. Source: alert note, p. 1.
- Separate set and clear thresholds provide hysteresis to avoid fast ALERT
  oscillation. Source: alert note, p. 1.

| Indicator | Meaning | Source |
| --- | --- | --- |
| Status bit 1 | Last command was not processed, invalid, or failed integrated command checksum. | Datasheet, p. 13 |
| Status bit 0 | Last write data checksum failed. | Datasheet, p. 13 |
| Status bit 4 | Reset detected since last clear-status command. | Datasheet, p. 13 |
| Read NACK in periodic fetch | No measurement data present. | Datasheet, p. 11 |
| Read NACK after no-stretch single-shot | Measurement result not ready yet. | Datasheet, p. 10 |

The internal heater is intended for plausibility checking only. It raises
temperature by a few degrees depending on conditions and is disabled after
reset. Source: datasheet, pp. 12-13.

## Startup, Reset, And Operational Notes

- After `VDD` reaches `VPOR`, the sensor enters idle after the specified
  power-up time. Source: datasheet, p. 7.
- A minimum 1 ms wait is needed after sending a command before another command
  can be received. Source: datasheet, p. 9.
- Soft reset is `0x30A2`; send it while SHT3x-DIS is idle. It resets the system
  controller and reloads calibration data. Source: datasheet, p. 12.
- Interface reset can be attempted by leaving `SDA` high and toggling `SCL`
  nine or more times, followed by a transmission start. This resets the serial
  interface only; the status register is preserved. Source: datasheet, p. 12.
- Full hard reset means cycling `VDD`; remove voltage from `SDA`, `SCL`, and
  `ADDR` too to avoid powering through ESD diodes. Source: datasheet, p. 12.

| Flow | Steps | Source |
| --- | --- | --- |
| Single-shot no stretching | Send one of `0x24xx`, wait measurement time, read six bytes: T MSB/LSB/CRC, RH MSB/LSB/CRC. | Datasheet, p. 10 |
| Single-shot with stretching | Send one of `0x2Cxx`, then read; sensor ACKs and holds SCL low until measurement is complete. | Datasheet, p. 10 |
| Periodic | Send periodic command, poll/fetch with `0xE000`, stop with break `0x3093` before most other commands. | Datasheet, pp. 10-12 |
| Serial number | Send `0x3780` or `0x3682`, wait 1 ms, read two 16-bit words with CRC. | Serial-number note, p. 1 |

Operating and validation notes:

- Best performance is in 5 to 60 degC and 20 to 80 %RH. Long exposure outside
  normal range, especially high humidity, can temporarily offset RH; returning
  to normal range allows slow recovery. Source: datasheet, p. 6.
- Sensirion says sensors are factory-tested/calibrated and customer calibration
  verification is not required, though after-assembly tests may be useful to
  verify storage, handling, and assembly. Source: ambient testing note, p. 1.
- For ambient production tests, Sensirion's fixture guidance minimizes
  temperature/RH gradients and places an accurate reference sensor close to the
  DUT. Source: ambient testing note, pp. 1-2.
- Heater use is a plausibility check: it raises sensor temperature by a few
  degrees depending on conditions, has 3.6 mW to 33 mW power depending on
  supply, and is disabled after reset. Source: datasheet, pp. 6, 12-13.

## Variants And Open Questions

| Variant / option | Driver-visible facts | Source |
| --- | --- | --- |
| SHT30, SHT31, SHT35 | Same command protocol; accuracy differs by variant. | Datasheet, pp. 1-2 |
| Membrane option | PTFE membrane protects against water/dust according to IP67 and keeps RH response time within uncovered-sensor specification. Protocol is in the main datasheet. | Membrane datasheet, p. 1 |
| ALERT mode | Applies to SHT3x-DIS in periodic mode; STS3x has temperature-only limits, but SHT3x supports humidity and temperature limits. | Alert note, pp. 1-2 |

Revision notes:

- Main datasheet source is Version 6, February 2019. Source: datasheet, p. 1.
- Serial-number application note source is Version 3, January 2023; it changed
  `tIDLE` to 1 ms in an earlier revision and currently documents
  `0x3780`/`0x3682`. Source: serial-number note, pp. 1-2.
- Membrane option source is Version 4.1, March 2025. Source: membrane
  datasheet, p. 1.

Open implementation questions:

- These compact notes do not encode all package/tape dimensions; inspect the
  PDF drawings for footprint or production tooling. Source: datasheet,
  pp. 16-18; membrane datasheet, p. 2.
- The main datasheet references several handling/design guides that are not in
  this compact set; use them before adding manufacturing or cleaning guidance
  to the library. Source: datasheet, pp. 6, 8.
- The supplemental humidity-formula note provides dew point and
  absolute-humidity application equations; it adds no SHT3x-DIS I2C command,
  status-bit, or alert-limit definitions.
