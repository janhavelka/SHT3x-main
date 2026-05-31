# SHT3x Documentation Index

Date: 2026-05-31
Branch: `hardening/sht3x-industry-readiness`

This file is the documentation map for the branch. It is the current place to
check which documents are active and which ones are historical snapshots.

No physical HIL run has been performed in this workspace. All hardware rows stay
`Not run` until a real ESP32-S2/S3 and SHT3x target produce logs and fixture
evidence.

## Current Status

- Current branch changes are still under `CHANGELOG.md` `[Unreleased]`.
- `library.json` is still version `1.5.0`; the current branch HEAD is not a
  release tag.
- Last recorded local cleanup checks covered the native test suite, guard
  scripts, Doxygen, and Arduino PlatformIO ESP32-S3/S2 builds.
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
- `SHT3X_ALERT_STATUS_FIX_REPORT.md` - rationale for ALERT/status mode-restore support.
- `SHT3X_CORE_CONTRACTS_PARTIAL_STATE_REPORT.md` - core contract and partial-state notes.
- `IDF_PORT.md` - ESP-IDF porting guidance.
- `IDF_PORT_IMPLEMENTATION.md` - implemented ESP-IDF component/example notes.

## Historical Context

- `SHT3X_PROMPTS_00_05_AUDITOR_SUMMARY.md` - historical summary of prompts 00-05.

## Reference Material

- `CODEX_PROMPT_SHT3X_DRIVER.md` - original driver prompt/instruction capture.
- `SHT3x_driver_extraction.md` - extraction and split notes.
- `extracted-md/` - compact local SHT3x protocol notes.
- `pdf-extracted-md/` - local extracted Sensirion reference text.
- Vendor PDFs and `HT_AlertMode_BitConversion.xlsx` - source references.

## Removed During Cleanup

The branch cleanup removed stale planning and snapshot reports that duplicated
the active docs:

- `SHT3X_HARDENING_PLAN.md`
- `SHT3X_HARDENING_FINAL_REPORT.md`
- `SHT3X_DOCS_CLEANUP_BEFORE_HIL_REPORT.md`
- `SHT3X_IDF_CI_DOCS_REPORT.md`
- `SHT3X_IDF_MERGED_INDUSTRY_READINESS_AUDIT.md`
- `SHT3X_PRE_HIL_READINESS_REPORT.md`

Their useful current-state content is now covered by this index,
`IDF_PORT_IMPLEMENTATION.md`, `HARDWARE_VALIDATION.md`, and the changelog.

## Claim Boundary

Safe wording today: software-hardened, locally software-tested,
pre-HIL-ready, and CI-configured.

Do not claim hardware validation, ALERT pin validation, humidity accuracy
validation, pure ESP-IDF validation, release publication, field-proven behavior,
or industry-grade status until the corresponding evidence exists.
