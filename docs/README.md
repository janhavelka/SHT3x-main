# SHT3x Documentation

Last updated: 2026-07-22

This directory keeps maintained project documentation and source reference
material. Historical prompt captures, implementation journals, and generated
PDF text dumps are intentionally not part of the active docs tree. The current
TunnelMonitor suitability audit remains maintained until its external adoption
and hardware gates close; older audit material is historical evidence only.

## Guides

| File | Purpose |
| --- | --- |
| [hardware.md](hardware.md) | Hardware validation status, serial HIL runner procedure, evidence checklist, and claim boundary. |
| [esp-idf.md](esp-idf.md) | ESP-IDF component/example boundary, adapter contract, and validation commands. |
| [reference/README.md](reference/README.md) | Vendor source-document inventory and local chip notes. |
| [reports/hil-validation-COM20-20260629.md](reports/hil-validation-COM20-20260629.md) | Final maintained COM20 ESP32-S3 hardware report; superseded blocked-attempt reports were removed. |
| `TUNNELMONITOR_NODE_SUITABILITY_AUDIT.md` | Repository-only traceable suitability findings, resolutions, and remaining external gates. |
| `../Doxyfile` | Repository-only strict public API and maintained-documentation reference build. |

## Current Status

- Version metadata is `1.7.0` in `library.json`, `idf_component.yml`, Doxyfile,
  and generated `include/SHT3x/Version.h`.
- Version 1.7.0 is the owner-safe feature release. Its audited code baseline
  passed 116/116 native tests, strict
  framework-neutral compilation, repository guards, pinned ESP32-S2/S3 Arduino
  builds, Doxygen-as-error validation, and package inspection. The
  repository-only `TUNNELMONITOR_NODE_SUITABILITY_AUDIT.md` records exact scope.
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
  compact `00` through `08` SHT3x chip-documentation notes in one maintained
  file.
- `reference/vendor/` contains the primary Sensirion PDFs and the alert
  bit-conversion spreadsheet. These are source material, not package payload.

## Package Boundary

The PlatformIO package is library-focused. It includes source, public headers,
examples, README/changelog, component metadata, package-facing guides, the
compact chip notes, curated HIL evidence, and the host-side HIL runner
entrypoints. It excludes generated HIL logs, build output, Doxygen output,
bulky vendor PDFs/spreadsheets, prompt history, and the repository-only
TunnelMonitor suitability audit.

## Claim Boundary

Safe wording today: v1.7.0 is software-hardened and locally software-tested.
The historical v1.6.1 COM20 ESP32-S3 destructive serial HIL plus post-reboot
smoke passed at address `0x44`. No v1.7.0 physical-hardware run was performed.

Do not claim full hardware validation, physical ALERT pin validation, humidity
accuracy validation, pure ESP-IDF validation,
long-soak stability, release publication, field-proven behavior, or
industrial-grade status until the corresponding evidence exists.
