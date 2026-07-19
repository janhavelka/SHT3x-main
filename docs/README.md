# SHT3x Documentation

Last updated: 2026-07-19

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
| [reports/](reports/) | Detailed hardware validation and audit reports. |

## Current Status

- Version metadata is `1.7.0` in `library.json`, `idf_component.yml`, Doxyfile,
  and generated `include/SHT3x/Version.h`.
- The owner-safe production surface is passive `bind()`, zero-I2C request/cancel,
  and one-callback `pollJob()` with deadlines, identity, phase, outcome, and
  partial/indeterminate-effect reporting. Synchronous APIs remain bounded
  convenience/diagnostic/maintenance operations.
- Latest maintained hardware evidence is
  [reports/hil-validation-COM20-20260629.md](reports/hil-validation-COM20-20260629.md).
- That evidence covers COM20 ESP32-S3 destructive serial HIL and post-reboot
  smoke at address `0x44`; long-soak stability remains incomplete.
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

Safe wording today: v1.7.0 is software-hardened and locally software-tested;
the historical v1.6.1 COM20 ESP32-S3 destructive serial HIL plus post-reboot
smoke passed at address `0x44`. No v1.7.0 physical-hardware run was performed.

Do not claim full hardware validation, physical ALERT pin validation, humidity
accuracy validation, pure ESP-IDF validation,
long-soak stability, release publication, field-proven behavior, or
industrial-grade status until the corresponding evidence exists.
