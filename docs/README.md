# SHT3x Documentation

Last updated: 2026-07-22

This directory keeps maintained project documentation and source reference
material. Completed audits, dated validation reports, prompt captures,
implementation journals, and full HIL transcripts are not active documentation.
Current contracts and accepted evidence live in the evergreen guides below.

## Guides

| File | Purpose |
| --- | --- |
| [hardware.md](hardware.md) | Hardware validation status, serial HIL runner procedure, evidence checklist, and claim boundary. |
| [esp-idf.md](esp-idf.md) | ESP-IDF component/example boundary, adapter contract, and validation commands. |
| [tunnelmonitor-integration.md](tunnelmonitor-integration.md) | Current TunnelMonitor owner/adapter contract and external work still open. |
| [reference/README.md](reference/README.md) | Vendor source-document inventory and local chip notes. |
| `../Doxyfile` | Repository-only strict public API and maintained-documentation reference build. |

## Current Status

- Version metadata is `1.7.0` in `library.json`, `idf_component.yml`, Doxyfile,
  and generated `include/SHT3x/Version.h`.
- Version 1.7.0 is the owner-safe feature release. Its tested baseline passed
  116/116 native tests, strict framework-neutral compilation, repository
  guards, pinned ESP32-S2/S3 Arduino builds, Doxygen-as-error validation, and
  package inspection.
- The owner-safe production surface is passive `bind()`, zero-I2C request/cancel,
  and one-callback `pollJob()` with deadlines, identity, phase, outcome, and
  partial/indeterminate-effect reporting. Synchronous APIs remain bounded
  convenience/diagnostic/maintenance operations.
- [hardware.md](hardware.md) owns the latest maintained evidence: 99 executable
  functional commands passed, one selected interface-reset row was explicitly
  unsupported, zero commands failed, and a strict uninterrupted one-hour soak
  passed at address `0x44`.
- Pure ESP-IDF S2/S3 software builds pass in CI. Physical pure-IDF execution
  remains untested.

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
runner entrypoints. It excludes generated HIL logs and full transcripts, build
output, Doxygen output, bulky vendor PDFs/spreadsheets, and historical working
documents.

## Claim Boundary

Safe wording today: v1.7.0 is software-hardened and its selected COM19
ESP32-S3 functional matrix plus one-hour owner-safe soak passed at address
`0x44`.

Do not claim full hardware validation, physical ALERT pin validation, humidity
accuracy validation, pure ESP-IDF validation,
multi-day stability, release publication, field-proven behavior, or
industrial-grade status until the corresponding evidence exists.
