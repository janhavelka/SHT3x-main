# SHT3x Pre-HIL Readiness Report

Date: 2026-05-31
Branch: `hardening/sht3x-industry-readiness`
Base commit before this preparation: `58fd3b1454feff6556370fdc6174dac25e25ffb1`
Final preparation commit: `92f87a3082e05b138ef640a5f4e376f1377f46dc`.
Later auditor-summary and serial-runner preparation commits are
`46855ce445b3124fda693037eb805455cc1813e5` and
`5049e847371bbf3abdfdbbc0ec2c2a01261b05f0`. Documentation hierarchy and
line-ending cleanup are recorded in `SHT3X_DOCS_CLEANUP_BEFORE_HIL_REPORT.md`.

## Summary

The repository is prepared to enter full HIL. No full HIL was run during this
pass. All hardware scenarios remain `Not run` until a real ESP32-S2/S3 plus
SHT3x setup produces logs and fixture evidence.

Note: this report remains the pre-HIL readiness gate. The later docs cleanup
report supersedes it only for document hierarchy, link, and line-ending status.

## What Changed

- Added `docs/SHT3X_HIL_RUNBOOK.md` with exact hardware requirements, build and
  upload commands, serial settings, smoke/full sequences, expected output
  patterns, pass/fail criteria, log naming, restore steps, honest `Not run`
  handling, ALERT procedure, clock-stretch/no-stretch procedure, and fixture
  realism notes.
- Added `docs/SHT3X_HIL_LOG_TEMPLATE.md`.
- Updated hardware validation docs to point at the runbook and keep unexecuted
  rows as `Not run`.
- Added Arduino and ESP-IDF diagnostic CLI surface for HIL-friendly aliases:
  `single <low|medium|high>`, `periodic start/fetch/stop`, `art start/fetch/stop`,
  `clear_status`, `alert show`, and `alert set`.
- Added `status_restore` to both CLIs. It calls `readStatusWithModeRestore()`
  and prints `stopStatus`, `statusReadStatus`, `restoreStatus`, `statusValid`,
  `restored`, mode interruption fields, and parsed status bits when valid.
- Added IDF CLI visibility for full version/build/commit output, last error
  status, and cached alert-limit settings.
- Strengthened CLI contract checks so the HIL command surface cannot disappear
  silently.
- Softened public wording that implied production or field proof before HIL.

## Completeness

HIL runbook complete: yes.

HIL log template complete: yes.

CLI changes needed: yes. Both Arduino and ESP-IDF diagnostic examples were
missing `readStatusWithModeRestore()` exposure and HIL-friendly command aliases.
Those were added without changing the core library API.

CI pure IDF proof: configured, not locally confirmed. The GitHub workflow has
pure ESP-IDF S2/S3 jobs, but live CI for this branch/HEAD is not proven in this
local pass. Local `idf.py` is unavailable in this shell.

## Local Checks

Results from this pass:

| Command | Result |
| --- | --- |
| `git status --short` | Dirty as expected before commit; only focused pre-HIL files changed. |
| `git diff --check` | Pass, exit 0. Git emitted a CRLF normalization warning for `docs/CODEX_PROMPT_SHT3X_DRIVER.md`; the later docs cleanup pass normalized that file. |
| `python tools/check_core_timing_guard.py` | Pass: `Core timing guard PASSED`. |
| `python tools/check_cli_contract.py` | Pass: `CLI contract PASSED`. |
| `python tools/check_idf_example_contract.py` | Pass: `IDF example contract PASSED`. |
| `python scripts/generate_version.py check` | Pass: `Version.h` up to date. |
| `python -m platformio test -e native` | Pass: 70/70 tests succeeded. |
| `python -m platformio run -e esp32s3dev` | Pass: PlatformIO build succeeded. |
| `python -m platformio run -e esp32s2dev` | Pass: PlatformIO build succeeded. |
| `python -m platformio pkg pack` | Pass: wrote `SHT3x-1.5.0.tar.gz`; artifact removed. |
| `idf.py --version` | Not available: `idf.py : The term 'idf.py' is not recognized as the name of a cmdlet, function, script file, or operable program.` |

## Commands Not Run

- Full HIL: intentionally not run in this prompt.
- Local pure ESP-IDF build/flash: not run because `idf.py` is unavailable in
  this shell.
- Live GitHub CI confirmation: not proven locally; workflow is configured but a
  PR or workflow run still needs to supply evidence.
- Real ALERT pin, clock-stretch hardware behavior, humidity accuracy, and fault
  injection: not run; all require hardware and/or fixtures.

## Ready To Enter Full HIL

Yes, for a full HIL session using the new runbook and log template. Remaining
preconditions are external: connect the ESP32-S2/S3 boards, SHT3x board, ALERT
capture wiring, reference/fixture equipment where needed, and use an ESP-IDF
environment or CI run for pure-IDF proof.

Remaining blockers before claiming hardware readiness:

- No hardware evidence has been captured yet.
- Pure ESP-IDF local tooling is unavailable in this shell.
- ALERT validation requires real ALERT wiring and GPIO or logic-analyzer evidence.
- Fault/CRC scenarios require a safe jig, emulator, interposer, or temporary
  fault transport.
- Humidity accuracy requires a reference sensor or controlled fixture; desk
  readings remain smoke evidence only.
