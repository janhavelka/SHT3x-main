# SHT3x Documentation

Last updated: 2026-06-16

This directory keeps only maintained project documentation and source reference
material. Historical audit reports, prompt captures, implementation journals,
and generated PDF text dumps are intentionally not part of the active docs tree.

## Guides

| File | Purpose |
| --- | --- |
| [hardware.md](hardware.md) | Hardware validation status, serial HIL runner procedure, evidence checklist, and claim boundary. |
| [esp-idf.md](esp-idf.md) | ESP-IDF component/example boundary, adapter contract, and validation commands. |
| [reference/README.md](reference/README.md) | Vendor source-document inventory and local chip notes. |
| [hil/](hil/) | Curated hardware evidence summaries only. Generated `hil_logs/` output stays local unless deliberately curated. |

## Current Status

- Version metadata is `1.6.0` in `library.json`, `idf_component.yml`, Doxyfile,
  and generated `include/SHT3x/Version.h`.
- Latest curated hardware evidence is
  [hil/20260601_arduino_esp32s3_com17_7847ed0_default_hil.md](hil/20260601_arduino_esp32s3_com17_7847ed0_default_hil.md).
- That evidence covers only the default automated ESP32-S3 serial command
  sequence at address `0x44`.
- Pure ESP-IDF S2/S3 builds are configured in CI. Use a passing CI log or local
  ESP-IDF build log before claiming pure ESP-IDF validation.

## Reference Material

- [reference/sht3x-chip-notes.md](reference/sht3x-chip-notes.md) preserves the
  compact `00` through `08` SHT3x chip-documentation notes in one maintained
  file.
- `reference/vendor/` contains the primary Sensirion PDFs and the alert
  bit-conversion spreadsheet. These are source material, not package payload.

## Package Boundary

The PlatformIO package is library-focused. It includes source, public headers,
examples, README/changelog, component metadata, maintained docs, the compact
chip notes, curated HIL evidence, and the host-side HIL runner entrypoints. It
excludes generated HIL logs, build output, Doxygen output, bulky vendor PDFs,
spreadsheets, audit leftovers, and prompt history.

## Claim Boundary

Safe wording today: software-hardened, locally software-tested, CI-configured,
and default ESP32-S3 serial HIL smoke passed for the curated `7847ed0` evidence
summary.

Do not claim full hardware validation, physical ALERT pin validation, humidity
accuracy validation, pure ESP-IDF validation, fault-injection validation,
long-soak stability, release publication, field-proven behavior, or
industrial-grade status until the corresponding evidence exists.
