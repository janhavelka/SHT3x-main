# SHT3x Reference Material

Last updated: 2026-06-16

This directory contains source material and compact chip notes. It is not a
status tracker; use [../hardware.md](../hardware.md) for validation evidence.

## Maintained Local Notes

- [sht3x-chip-notes.md](sht3x-chip-notes.md) merges the original compact
  `00` through `08` SHT3x chip-documentation notes so protocol, timing, status,
  alert, reset, and variant facts stay easy to review.

## Vendor Sources

| File | Role | Version / date | Notes |
| --- | --- | --- | --- |
| `vendor/SHT3x_datasheet.pdf` | Primary datasheet | Version 6, February 2019 | Electrical specs, pinout, I2C protocol, commands, status, CRC, conversions, packaging. |
| `vendor/SHT3x_HT_AN_AlertMode.pdf` | Alert-mode application note | Version 3.1, November 2023 | ALERT behavior and alert-limit command encodings. |
| `vendor/Sensirion_electronic_identification_code_SHT3x.pdf` | Serial-number application note | Version 3, January 2023 | 32-bit serial/EIC readout commands. |
| `vendor/SHT3x_membrane_option_datasheet.pdf` | Membrane option datasheet | Version 4.1, March 2025 | PTFE membrane option, protection, orderable variants. |
| `vendor/Sensirion_Humidity_Sensors_Testing_at_Ambient_Conditions.pdf` | Test guidance | Version 3, October 2022 | Ambient-condition fixture guidance after assembly. |
| `vendor/Sensirion_AppNotes_Humidity_Sensors_at_a_Glance.pdf` | Supplemental humidity formulas | May 2025 | Dew point and humidity formulas for application code. |
| `vendor/HT_AlertMode_BitConversion.xlsx` | Alert bit conversion | Vendor spreadsheet | Alert-limit bit conversion support material. |

The generated raw Markdown extracts of vendor PDFs were removed from the active
docs tree. Keep the PDFs as the authoritative source when exact wording,
figures, package drawings, or legal notices matter.
