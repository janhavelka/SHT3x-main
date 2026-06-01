# SHT3x Documentation Index

Last updated: 2026-06-01

Branch: `hardening/sht3x-release-readiness-gaps`

This file is the documentation map for the repository. It separates maintained
status/procedure docs from historical reports and raw reference material.

## Current Status

- Current branch changes are still under `CHANGELOG.md` `[Unreleased]`.
- `library.json` is still version `1.5.0`; this branch head is not a release tag.
- Latest curated default serial HIL evidence is
  `docs/hil/20260601_arduino_esp32s3_com17_7847ed0_default_hil.md`.
  It summarizes a local generated run at `hil_logs/i2c_20260601T183017Z`:
  ESP32-S3 on COM17, SHT3x address `0x44`, final verdict `PASS`, branch
  `hardening/sht3x-release-readiness-gaps`, code commit
  `7847ed0eb83fbeeb9f08c4f5ea14c8a8b24756c9`, and clean firmware metadata.
- That HIL evidence is limited to the default automated serial command sequence.
  It does not validate physical ALERT pin behavior, humidity accuracy, fault
  injection, clock stretching, ESP32-S2 hardware, address `0x45`, alert writes,
  destructive reset flows, or long-soak stability.
- Pure ESP-IDF S2/S3 builds are configured in CI. Use a passing CI log or local
  ESP-IDF build log before claiming pure ESP-IDF validation.
- Hardware status is tracked only in `HARDWARE_VALIDATION.md`.

## Active Documents

- `HARDWARE_VALIDATION.md` - maintained hardware and build evidence status.
- `SHT3X_HARDWARE_VALIDATION_MATRIX.md` - detailed HIL scenario matrix.
- `SHT3X_HIL_RUNBOOK.md` - manual HIL procedure.
- `SHT3X_HIL_LOG_TEMPLATE.md` - manual HIL evidence template.
- `SHT3X_I2C_HIL_RUNBOOK.md` - host-side serial runner procedure.
- `SHT3X_I2C_HIL_TARGET_TEMPLATE.md` - target profile for serial runner runs.
- `SHT3X_I2C_HIL_SELFTEST_REPORT.md` - serial runner software self-test report.
- `IDF_PORT.md` - ESP-IDF porting guidance.
- `IDF_PORT_IMPLEMENTATION.md` - implemented ESP-IDF component/example notes.

## Rationale Notes

These are maintained technical rationale notes. They are not release status
documents.

- `rationale/SHT3X_ALERT_STATUS_FIX_REPORT.md` - ALERT/status restore design.
- `rationale/SHT3X_ALERT_ENCODING_FIX_REPORT.md` - alert-limit vector fix and coverage.
- `rationale/SHT3X_API_CONTRACT_CLEANUP_REPORT.md` - public API contract cleanup.
- `rationale/SHT3X_CORE_CONTRACTS_PARTIAL_STATE_REPORT.md` - partial-state behavior.

## HIL Evidence

- `hil/20260601_arduino_esp32s3_com17_7847ed0_default_hil.md` - curated summary
  of the latest default ESP32-S3 serial HIL run.

Generated `hil_logs/` directories remain local scratch output by default. Commit
only curated summaries or fixture artifacts that are intended to become project
evidence.

## Archive

Historical audit, planning, and branch reports live under `archive/`. They are
kept for provenance and may describe old branches, old CI failures, old HIL
state, or superseded release-gate decisions. Do not use archived reports as the
current project status; use this index plus `HARDWARE_VALIDATION.md`.

- `archive/reports/` - historical readiness, runner, package, and prompt reports.
- `archive/prompts/` - original task/prompt captures.
- `archive/notes/` - historical extraction and split notes.

## Reference Material

Local reference material lives under `reference/` and is excluded from the
PlatformIO package payload.

- `reference/vendor/` - vendor PDFs and the alert bit-conversion spreadsheet.
- `reference/extracted/sht3x/` - compact local SHT3x protocol notes.
- `reference/extracted/vendor/` - extracted vendor PDF/application-note text.

## Package Boundary

The package export is library-focused. It keeps source, public headers,
examples, README/changelog, component metadata, the maintained active docs,
rationale notes, and the host-side serial HIL runner entrypoints. It excludes
generated HIL logs, build outputs, Doxygen output, vendor PDFs, extracted
reference text, and archived reports.

## Claim Boundary

Safe wording today: software-hardened, locally software-tested, CI-configured,
and default ESP32-S3 serial HIL-smoke passed for the curated `7847ed0` evidence
summary.

Do not claim full hardware validation, physical ALERT pin validation, humidity
accuracy validation, pure ESP-IDF validation, fault-injection validation,
long-soak stability, release publication, field-proven behavior, or
industrial-grade status until the corresponding evidence exists.
