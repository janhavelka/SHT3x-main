# SHT3x Documentation

Last updated: 2026-08-05

This directory keeps maintained project documentation and source reference
material. Completed audits, dated validation reports, prompt captures,
implementation journals, and generated HIL run directories are not active
documentation. This checkout intentionally keeps none of them. Accepted raw
evidence belongs in an external archive; only stable fingerprints and claim
boundaries belong in the evergreen guides below.

## Guides

| File | Purpose |
| --- | --- |
| [hardware.md](hardware.md) | Hardware validation status, serial HIL runner procedure, evidence checklist, and claim boundary. |
| [esp-idf.md](esp-idf.md) | ESP-IDF component/example boundary, adapter contract, and validation commands. |
| [tunnelmonitor-integration.md](tunnelmonitor-integration.md) | Current TunnelMonitor owner/adapter contract, observed consumer status, and remaining physical validation. |
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
- The two framework-native diagnostic CLIs are governed by the single ordered
  `tools/sht3x_cli_contract.py` contract. It covers strict parsing, confirmation
  safety, runtime framework identity, retained job results, cancellation, and
  example-owned transfer assertions without sharing framework implementation.
- CI covers native ESP-IDF S2/S3 builds, but physical pure-IDF execution remains
  untested.
- Post-upgrade Arduino ESP32-S3 functional evidence is recorded in
  [hardware.md](hardware.md); raw run artifacts remain in the external evidence
  archive rather than this checkout.

## API Reference

Public API comments are maintained in `include/SHT3x/`. From the root of a
full repository checkout, run:

```bash
doxygen Doxyfile
```

The ignored HTML output is `.doxygen/html/index.html`. Extraction is limited to
documented entities, while undocumented public symbols, missing or incomplete
parameter/return documentation, malformed references, and all other Doxygen
warnings fail the build.

`python tools/check_docs_contract.py` additionally rejects broken local Markdown
links, tracked prompt/report/HIL scratch artifacts, and weakened strict-Doxygen
settings.

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
runner with its CLI-contract module. It excludes local HIL artifacts and
transcripts, build output, Doxygen output, bulky vendor PDFs/spreadsheets, and
historical working documents. Generated HIL output is disposable local scratch;
accepted raw evidence is archived separately before cleanup.

## Claim Boundary

Safe wording and exact accepted metrics are maintained only in
[hardware.md](hardware.md); do not restate them in parallel status documents.

Do not claim full hardware validation, physical ALERT pin validation, humidity
accuracy validation, pure ESP-IDF validation,
multi-day stability, release publication, field-proven behavior, or
industrial-grade status until the corresponding evidence exists.
