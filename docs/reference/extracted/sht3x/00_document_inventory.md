# Document Inventory

Compact notes in `docs/reference/extracted/sht3x/` summarize SHT3x-DIS I2C commands, status bits, alert limits, timing, conversion, and variant facts. Raw PDF text remains in `docs/reference/extracted/vendor/` for exact Sensirion wording and package figures.

| Source PDF | Raw extract | Role | Version / date | Pages | Notes |
|---|---|---|---|---:|---|
| `docs/reference/vendor/SHT3x_datasheet.pdf` | `docs/reference/extracted/vendor/SHT3x_datasheet.md` | Primary datasheet | Version 6, February 2019 | 22 | Electrical specs, pinout, I2C protocol, commands, status, CRC, conversions, packaging. |
| `docs/reference/vendor/SHT3x_HT_AN_AlertMode.pdf` | `docs/reference/extracted/vendor/SHT3x_HT_AN_AlertMode.md` | Alert-mode application note | Version 3.1, November 2023 | 5 | ALERT pin behavior and alert-limit command encodings. |
| `docs/reference/vendor/Sensirion_electronic_identification_code_SHT3x.pdf` | `docs/reference/extracted/vendor/Sensirion_electronic_identification_code_SHT3x.md` | Serial-number application note | Version 3, January 2023 | 3 | 32-bit serial number readout commands. |
| `docs/reference/vendor/SHT3x_membrane_option_datasheet.pdf` | `docs/reference/extracted/vendor/SHT3x_membrane_option_datasheet.md` | Variant option datasheet | Version 4.1, March 2025 | 4 | PTFE membrane option, protection, orderable variants. |
| `docs/reference/vendor/Sensirion_Humidity_Sensors_Testing_at_Ambient_Conditions.pdf` | `docs/reference/extracted/vendor/Sensirion_Humidity_Sensors_Testing_at_Ambient_Conditions.md` | Test guidance | Version 3, October 2022 | 10 | Ambient-condition fixture guidance for SHT3x verification after assembly; no I2C command additions. |
| `docs/reference/vendor/Sensirion_AppNotes_Humidity_Sensors_at_a_Glance.pdf` | `docs/reference/extracted/vendor/Sensirion_AppNotes_Humidity_Sensors_at_a_Glance.md` | Supplemental humidity formulas | May 2025 | 3 | Dew point and humidity formulas for application code; no SHT3x command/register additions. |

## Compact Note Set

| File | Purpose |
|---|---|
| `01_chip_overview.md` | Sensor family, outputs, accuracy anchors, and package. |
| `02_pinout_and_signals.md` | 8-pin DFN pinout, address selection, ALERT, reset, and pull-ups. |
| `03_electrical_and_timing.md` | Supply, current, reset/measurement timing, I2C limits, absolute ratings. |
| `04_protocol_commands_and_transactions.md` | I2C command framing, data readout, command table, CRC, conversions. |
| `05_register_map.md` | Command/status pseudo-register map for the command-based device. |
| `06_modes_interrupts_status_and_faults.md` | Single-shot, periodic, ART, ALERT, heater, status and CRC faults. |
| `07_initialization_reset_and_operational_notes.md` | Startup, reset, measurement sequences, serial number, validation and handling notes. |
| `08_variant_differences_and_open_questions.md` | SHT30/SHT31/SHT35 and membrane-option differences plus open questions. |

## Scope Notes

- Legal notices and long page dumps were intentionally removed from compact notes.
- Main protocol facts come from the SHT3x-DIS datasheet unless another source is named.
- Units use ASCII forms such as `degC`, `uA`, `+/-`, and `kOhm`.
