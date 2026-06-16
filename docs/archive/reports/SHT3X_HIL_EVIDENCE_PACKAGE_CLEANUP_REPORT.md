# SHT3x HIL Evidence And Package Cleanup Report

Date: 2026-06-01
Branch: `hardening/sht3x-release-readiness-gaps`

## Scope

This chunk cleaned documentation boundaries and PlatformIO package export
hygiene only. Full HIL was not run. No new physical ALERT pin, humidity
accuracy, fault-injection, clock-stretching, ESP32-S2 hardware, or soak
validation is claimed.

## Documentation Changes

- `README.md` now names the historical smoke-HIL boundary and states that the
  current branch needs a rerun before using that result as current-head
  hardware evidence.
- `docs/README.md` separates active user docs from historical/internal audit
  context and records the package-export policy.
- `docs/HARDWARE_VALIDATION.md` now has an explicit evidence-boundary section
  and marks only proven smoke-HIL rows as `PASS`; unrun hardware scenarios stay
  `Not run`.
- `docs/SHT3X_HARDWARE_VALIDATION_MATRIX.md` states that it is a scenario index
  and not a pass/fail tracker.
- `docs/SHT3X_HIL_RUNBOOK.md`, `docs/SHT3X_I2C_HIL_RUNBOOK.md`, and
  `docs/SHT3X_I2C_HIL_SELFTEST_REPORT.md` distinguish scratch runner output,
  curated evidence, dry-run self-test artifacts, and operator context.

## Evidence Status Before And After

Before this cleanup, docs mixed current runner capabilities with the older
tracked smoke-HIL `PASS`, which could be read as broader hardware coverage than
the committed artifacts support.

After this cleanup, the committed evidence boundary is:

- `hil_logs/i2c_20260531T155925Z/summary.md`
- branch `hardening/sht3x-industry-readiness`
- host runner/worktree commit `8661a38cc70e629cd337ac45c42a1885aefb0cfc`
- flashed firmware reported SHT3x library version `1.5.0` and git commit
  `unknown`
- COM17, 115200 baud, dry run `False`
- expected address `0x44`
- limited automated serial smoke sequence only

The current release-readiness branch has changed since that run. The final
release candidate needs a rerun before this can be claimed as current-head
hardware evidence.

## Package Export Rules

`library.json` now uses a whitelist-style PlatformIO export policy. It includes
the distributable library surface:

- public headers and source
- examples
- README, changelog, license, PlatformIO metadata, and ESP-IDF component files
- maintained user-facing docs and runbooks
- the two host-side HIL runner scripts referenced by the shipped docs

It excludes generated and internal artifacts:

- generated build/package output: `.pio`, `build`, example IDF build output,
  `*.tar`, `*.tar.gz`, `*.tgz`, `*.zip`
- generated HIL logs: `hil_logs/**`
- tests and repo-local validation tooling other than the shipped HIL runner
- generated Doxygen output
- vendor PDFs, Excel reference sheets, and extracted PDF/reference markdown
- internal prompt, audit, release-readiness, and branch-report documents

`.pioignore` mirrors scratch-output exclusions for PlatformIO workflows.

## Package Inspection Result

Local package inspection command:

```bash
python -m platformio pkg pack
```

Result: pass. PlatformIO wrote `SHT3x-1.5.0.tar.gz`. A tarball inspection found
45 entries, included `tools/run_sht3x_hil.py` and `tools/run_i2c_hil.py`, and
found no `hil_logs/`, generated build output, PDFs, or extracted reference
trees.

The package must not contain:

- `hil_logs/`
- `.pio/`
- `build/`
- generated tarballs
- bulky internal prompt, extracted-reference, PDF, or spreadsheet artifacts

## Remaining Hardware Evidence Gaps

- Physical ALERT pin validation: Not run.
- Fault injection: Not run.
- Clock stretching: Not run.
- Long soak: Not run.
- ESP32-S2 hardware run: Not run.
- Humidity accuracy or production fixture validation: Not run.
