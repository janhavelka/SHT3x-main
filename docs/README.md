# SHT3x Documentation Index

Date: 2026-06-01
Branch: `hardening/sht3x-release-readiness-gaps`

This file is the documentation map for the repository. It is the current place
to check which documents are active and which ones are historical snapshots.

A prior automated smoke-HIL log exists under `hil_logs/i2c_20260531T155925Z/`.
It records `PASS` for a limited ESP32-S3-class serial smoke run on address
`0x44`. The host runner/worktree commit was
`8661a38cc70e629cd337ac45c42a1885aefb0cfc`; the flashed firmware reported git
commit `unknown`. That evidence does not cover later branch changes; rerun the
runner on the final release candidate before using it as current-head hardware
evidence. All unexecuted hardware rows stay `Not run`.

## Current Status

- Current branch changes are still under `CHANGELOG.md` `[Unreleased]`.
- `library.json` is still version `1.5.0`; the current branch HEAD is not a
  release tag.
- Last recorded local cleanup checks covered the native test suite, guard
  scripts, Doxygen, and Arduino PlatformIO ESP32-S3/S2 builds.
- Pure ESP-IDF S2/S3 builds are configured in CI. Use a passing CI log or local
  ESP-IDF build log before claiming pure ESP-IDF validation.
- Hardware status is tracked only in `HARDWARE_VALIDATION.md`.
- Package export keeps the library source, examples, README, changelog, license,
  host-side HIL runner entrypoints, and maintained user docs. It excludes
  generated HIL logs, build outputs, vendor PDFs, extracted reference text, and
  internal prompt/audit artifacts.

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

## Repository-Only Reports

These branch reports are useful in the source repository but are not required in
the PlatformIO package payload:

- `SHT3X_AUTOMATIC_HIL_RUNNER_REPORT.md` - automatic runner validation report.
- `SHT3X_HIL_EVIDENCE_PACKAGE_CLEANUP_REPORT.md` - package/evidence cleanup report.
- `SHT3X_ALERT_STATUS_FIX_REPORT.md` - rationale for ALERT/status mode-restore support.
- `SHT3X_CORE_CONTRACTS_PARTIAL_STATE_REPORT.md` - core contract and partial-state notes.

## Historical Context

- `SHT3X_PROMPTS_00_05_AUDITOR_SUMMARY.md` - historical summary of prompts 00-05.
- `SHT3X_INDUSTRIAL_READINESS_EXPLORATION.md` - readiness exploration snapshot.
- `SHT3X_RELEASE_READINESS_GAPS_PLAN.md` - release-readiness branch plan.
- `SHT3X_RELEASE_READINESS_GAPS_FIX_REPORT.md` - earlier readiness fix notes.

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

Safe wording today: software-hardened, locally software-tested, limited
smoke-HIL evidence exists, and CI is configured.

Do not claim hardware validation, ALERT pin validation, humidity accuracy
validation, pure ESP-IDF validation, release publication, field-proven behavior,
or industry-grade status until the corresponding evidence exists.
