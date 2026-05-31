# SHT3x Documentation Index

Date: 2026-05-31
Branch: `hardening/sht3x-industry-readiness`

This index maps the documentation set by purpose. It does not record a HIL pass;
all hardware evidence remains pending until logs from real ESP32-S2/S3 plus
SHT3x hardware are attached.

## Authoritative Current Documents

- Current software readiness: `SHT3X_PRE_HIL_READINESS_REPORT.md`.
- Final software-hardening summary: `SHT3X_HARDENING_FINAL_REPORT.md`.
- Current docs cleanup status: `SHT3X_DOCS_CLEANUP_BEFORE_HIL_REPORT.md`.
- HIL execution: `SHT3X_HIL_RUNBOOK.md`.
- HIL logging: `SHT3X_HIL_LOG_TEMPLATE.md`.
- Hardware validation status: `HARDWARE_VALIDATION.md`.
- Hardware scenario planning: `SHT3X_HARDWARE_VALIDATION_MATRIX.md`.

## Audit / Planning

- `SHT3X_IDF_MERGED_INDUSTRY_READINESS_AUDIT.md` - historical pre-hardening audit.
- `SHT3X_HARDENING_PLAN.md` - prompt 00 hardening plan.
- `SHT3X_PROMPTS_00_05_AUDITOR_SUMMARY.md` - auditor summary of prompts 00-05.
- `CODEX_PROMPT_SHT3X_DRIVER.md` - original driver prompt/instruction capture.

## Prompt-Specific Hardening Reports

- `SHT3X_ALERT_STATUS_FIX_REPORT.md` - ALERT/status behavior and helper design.
- `SHT3X_CORE_CONTRACTS_PARTIAL_STATE_REPORT.md` - core contracts and partial-state coverage.
- `SHT3X_IDF_CI_DOCS_REPORT.md` - ESP-IDF CI/docs hardening report.

## Final Software Hardening Status

- `SHT3X_HARDENING_FINAL_REPORT.md` - final software-hardening summary.
- `SHT3X_PRE_HIL_READINESS_REPORT.md` - pre-HIL readiness gate.
- `SHT3X_I2C_HIL_SELFTEST_REPORT.md` - host-side serial runner self-test; software preparation only.

## HIL Execution

- `SHT3X_HIL_RUNBOOK.md` - authoritative manual HIL procedure.
- `SHT3X_HIL_LOG_TEMPLATE.md` - template for real HIL evidence capture.
- `SHT3X_HARDWARE_VALIDATION_MATRIX.md` - scenario matrix and planning index.
- `HARDWARE_VALIDATION.md` - maintained hardware result status; unexecuted rows stay `Not run`.
- `SHT3X_I2C_HIL_RUNBOOK.md` - optional host-side serial runner procedure.
- `SHT3X_I2C_HIL_TARGET_TEMPLATE.md` - target profile for runner evidence.

## ESP-IDF And Porting

- `IDF_PORT.md` - ESP-IDF port guidance.
- `IDF_PORT_IMPLEMENTATION.md` - implemented ESP-IDF component/example notes.

## Reference Extraction

- `SHT3x_driver_extraction.md` - extraction and split notes.
- `pdf-extracted-md/` - local extracted Sensirion reference text used for audit.

## Claim Boundary

Safe current wording is software-hardened, locally tested, pre-HIL-ready, and CI
configured. Do not claim hardware validation, ALERT pin validation, humidity
accuracy validation, local pure ESP-IDF success, release/tag publication,
field-proven behavior, or industry-grade status until the corresponding evidence
exists.
