# SHT3x Docs Cleanup Before HIL Report

Date: 2026-05-31
Branch: `hardening/sht3x-industry-readiness`

## Scope

This cleanup pass organizes the documentation set for auditor handoff before
real hardware-in-loop execution. It does not record a hardware pass.

## What Changed

- Added `docs/README.md` as the documentation index and authoritative-document
  map.
- Added a README current-state section clarifying local software validation,
  pending live ESP-IDF proof, pending release/tag work, and pending hardware
  evidence.
- Clarified ESP-IDF wording so configured CI is not described as proof without
  current workflow logs.
- Expanded the HIL log template with target metadata, artifact paths, restore
  evidence, and per-section `Not run` defaults.
- Tightened the hardware validation matrix with evidence paths and explicit
  pass/fail boundaries.
- Updated HIL runbook wording for the current `status_restore` output fields.

## Claim Boundary

Current safe wording is software-hardened, locally tested, pre-HIL-ready, and CI
configured. Do not claim hardware validation, ALERT pin validation, humidity
accuracy validation, pure ESP-IDF validation, release/tag publication,
field-proven behavior, or industry-grade status until the corresponding
evidence exists.

## Verdict

`ready for HIL documentation handoff`
