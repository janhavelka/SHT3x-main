# SHT3x Documentation

Last updated: 2026-08-01

This directory keeps maintained project documentation and source reference
material. Completed audits, dated validation reports, prompt captures, and
implementation journals are not active documentation. Raw transcripts are
preserved evidence artifacts rather than maintained prose and must not be
deleted during routine cleanup. Current contracts and accepted evidence live
in the evergreen guides below.

## Guides

| File | Purpose |
| --- | --- |
| [hardware.md](hardware.md) | Hardware validation status, serial HIL runner procedure, evidence checklist, and claim boundary. |
| [esp-idf.md](esp-idf.md) | ESP-IDF component/example boundary, adapter contract, and validation commands. |
| [tunnelmonitor-integration.md](tunnelmonitor-integration.md) | Current TunnelMonitor owner/adapter contract and external work still open. |
| [reference/README.md](reference/README.md) | Vendor source-document inventory and local chip notes. |
| `../Doxyfile` | Repository-only strict public API and maintained-documentation reference build. |

## Current Status

- Version metadata is `1.8.0` in `library.json`, `idf_component.yml`, Doxyfile,
  and generated `include/SHT3x/Version.h`.
- [The root README](../README.md) owns the current software/build status;
  [hardware.md](hardware.md) owns physical evidence and its limitations.
- The owner-safe production surface is passive `bind()`, zero-I2C request/cancel,
  and one-callback `pollJob()` with deadlines, identity, phase, outcome, and
  partial/indeterminate-effect reporting. Synchronous APIs remain bounded
  convenience/diagnostic/maintenance operations.
- CI covers native ESP-IDF S2/S3 builds, but physical pure-IDF execution remains
  untested.

## API Reference

Public API comments are maintained in `include/SHT3x/`. From the root of a
full repository checkout, run:

```bash
doxygen Doxyfile
```

The ignored HTML output is `.doxygen/html/index.html`. The build treats
undocumented public symbols, missing parameter documentation, malformed
references, and other Doxygen warnings as errors.

## Reference Material

- [reference/sht3x-chip-notes.md](reference/sht3x-chip-notes.md) preserves the
  compact, AI-readable `00` through `08` SHT3x source-document notes in one
  maintained file.
- `reference/vendor/` contains the primary Sensirion PDFs and the alert
  bit-conversion spreadsheet. These are source material, not package payload.

## Package Boundary

The PlatformIO package is library-focused. It includes source, public headers,
examples, README/changelog, component metadata, package-facing guides, the
compact chip notes, the current HIL evidence summary, and the host-side HIL
runner. It excludes local HIL artifacts and transcripts, build output, Doxygen
output, bulky vendor PDFs/spreadsheets, and historical working documents.

## Claim Boundary

Safe wording and exact accepted metrics are maintained only in
[hardware.md](hardware.md); do not restate them in parallel status documents.

Do not claim full hardware validation, physical ALERT pin validation, humidity
accuracy validation, pure ESP-IDF validation,
multi-day stability, release publication, field-proven behavior, or
industrial-grade status until the corresponding evidence exists.
