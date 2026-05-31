# SHT3x Prompts 00-05 Auditor Summary

Date: 2026-05-31
Branch: `hardening/sht3x-industry-readiness`
Current HEAD at summary creation: `92f87a3082e05b138ef640a5f4e376f1377f46dc`

## Purpose

This document summarizes the six Codex hardening prompts, numbered 00 through
05, and the resulting repository state for auditor handoff. It is a summary of
the requested work, implemented changes, validation evidence, and remaining
claim boundaries. It is not a hardware validation certificate.

## Executive Status

- Software hardening sequence completed through pre-HIL readiness.
- Main software blocker was addressed by adding an explicit
  `readStatusWithModeRestore()` path for ALERT/status diagnosis while
  periodic/ART acquisition is active.
- Native tests pass: final local run reported 70/70 passing tests.
- Arduino PlatformIO builds for ESP32-S3 and ESP32-S2 pass locally.
- Pure ESP-IDF CI jobs are configured for ESP32-S3 and ESP32-S2, but local
  `idf.py` is unavailable in this shell and local pure-IDF builds are not
  claimed.
- Full hardware-in-loop validation has not run. Hardware scenarios remain
  `Not run`.
- Release is not tagged. Version remains `1.5.0` with work recorded under
  `[Unreleased]`.
- Industry-grade or field-proven claims remain explicitly out of scope until
  ALERT pin, humidity fixture, fault-injection, and soak evidence exist.

## Commit Timeline

| Prompt | Commit | Commit message |
| --- | --- | --- |
| Baseline audit | `c4192d3` | `Add SHT3X IDF-Merged Industry-Readiness Audit documentation` |
| 00 | `02f52ea` | `docs: plan SHT3x industry-readiness hardening` |
| 01 | `457bcf1` | `fix: allow SHT3x alert status diagnosis in periodic mode` |
| 02 | `79d218e` | `test: harden SHT3x core contracts and partial-state coverage` |
| 03 | `1e554a1` | `ci: validate SHT3x ESP-IDF port and document contracts` |
| 04 | `58fd3b1` | `docs: finalize SHT3x industry-readiness hardening report` |
| 05 | `92f87a3` | `docs: prepare SHT3x HIL runbook and readiness gate` |

## Prompt 00: Branch, Plan, AGENTS, And Baseline

### Requested Scope

Prompt 00 established the hardening branch and required a staged plan rather
than one broad refactor. It required:

- checking the starting worktree and recent history;
- creating or switching to `hardening/sht3x-industry-readiness`;
- updating `AGENTS.md` with SHT3x-specific hardening rules;
- spawning read-only review agents for datasheet, core contracts, tests, and
  IDF CI readiness;
- running baseline checks;
- creating `docs/SHT3X_HARDENING_PLAN.md`;
- committing and pushing only planning and `AGENTS.md` changes.

### Outcome

- Branch created: `hardening/sht3x-industry-readiness`.
- Added SHT3x hardening rules to `AGENTS.md`.
- Created `docs/SHT3X_HARDENING_PLAN.md`.
- Baseline audit conclusion was preserved: the library was engineering-grade
  with strong architecture, but not industry-grade.
- No production code fix was made in this prompt.

### Baseline Validation

Prompt 00 baseline checks recorded:

- core timing guard passed;
- CLI contract passed;
- IDF example contract passed;
- generated `Version.h` was up to date;
- native PlatformIO tests passed, 46/46;
- Arduino ESP32-S3 build passed;
- Arduino ESP32-S2 build passed;
- `idf.py` was unavailable in the local shell and was not counted as a pass.

## Prompt 01: ALERT/Status Semantics In Periodic And ART Modes

### Requested Scope

Prompt 01 addressed the main production blocker:

- `readStatus()` returned `BUSY` while periodic acquisition was active;
- `readSettings()` hid this as `statusValid=false`;
- ALERT mode is associated with periodic acquisition;
- status bits identify humidity and temperature alert causes;
- therefore a design using ALERT could not diagnose why ALERT asserted while
  the relevant mode was active.

The prompt required datasheet-backed behavior, native tests, documentation, and
a clean public contract.

### Datasheet Conclusion

Local Sensirion references did not explicitly allow status-register reads,
status clear, or alert-limit commands while periodic/ART acquisition remains
active. The documented periodic-mode exception is Fetch Data, and the datasheet
recommends stopping periodic acquisition before sending other commands.

### Outcome

- Direct `readStatus()` remains non-disruptive and returns `BUSY` during active
  periodic/ART mode.
- Added `StatusReadSnapshot`.
- Added `SHT3x::readStatusWithModeRestore(StatusReadSnapshot&)`.
- Added `SettingsSnapshot::statusReadStatus`.
- The helper stops periodic/ART using Break, reads status, and attempts to
  restore the previous acquisition mode.
- The helper exposes all partial-state information:
  `stopStatus`, `statusReadStatus`, `restoreStatus`, `statusValid`,
  `restored`, initial/final mode, and whether acquisition was interrupted.
- `clearStatus()` remains explicit and destructive.
- Alert-limit access remains blocked while periodic/ART is active.
- README and public Doxygen comments were updated.

### Validation

Prompt 01 validation recorded:

- core timing guard passed;
- CLI contract passed;
- IDF example contract passed;
- generated `Version.h` was up to date;
- native PlatformIO tests passed, 58/58;
- Arduino ESP32-S3 build passed;
- Arduino ESP32-S2 build passed;
- local `idf.py` unavailable.

## Prompt 02: Core Contracts, Copy/Move, Partial-State Tests, Public API Coverage

### Requested Scope

Prompt 02 strengthened medium-severity contract and test issues:

- explicitly delete copy and move operations;
- audit multi-step operations for partial hardware/cache divergence;
- add failure-position tests;
- add more public API black-box tests;
- mirror thread/ISR contracts in public headers.

### Outcome

- `SHT3x` copy and move construction/assignment were explicitly deleted.
- Native tests added compile-time assertions for non-copyable/non-movable traits.
- Public headers now state that the driver is synchronous, not ISR-safe, and
  not internally thread-safe.
- Callback recursion limits were documented.
- `tick(nowMs)` timebase expectations were documented.
- General-call reset was documented as an application or bus-manager decision.
- A multi-step operation matrix was created in
  `docs/SHT3X_CORE_CONTRACTS_PARTIAL_STATE_REPORT.md`.
- No generic dirty flag was added. The decision was documented: SHT3x is
  command-oriented, and partial-state risks are better exposed through precise
  statuses, cached-setting rules, and `StatusReadSnapshot`.

### Tests Added

Coverage was added for:

- public invalid-address validation;
- public single-shot request/tick/read timing;
- periodic not-ready behavior;
- status read and clear separation;
- recover status preservation;
- periodic restart failure points;
- alert-limit write and verification failure points;
- partial `disableAlerts()` behavior;
- reset-and-restore partial alert restore failure;
- existing Prompt 01 stop/read/restore helper failure cases.

### Validation

Prompt 02 validation recorded:

- core timing guard passed;
- CLI contract passed;
- IDF example contract passed;
- generated `Version.h` was up to date;
- native PlatformIO tests passed, 70/70;
- Arduino ESP32-S3 build passed;
- Arduino ESP32-S2 build passed;
- local `idf.py` unavailable.

## Prompt 03: ESP-IDF CI, Docs, Guard Scripts, And Example Honesty

### Requested Scope

Prompt 03 handled CI and documentation gaps:

- add or strengthen pure ESP-IDF CI for ESP32-S2 and ESP32-S3;
- run the IDF example contract guard directly in CI;
- keep ESP-IDF documentation honest about what is and is not validated;
- label examples as diagnostic/bring-up unless truly production-style;
- add public API latency and transaction documentation;
- expand hardware validation documentation, especially ALERT and humidity tests.

### Outcome

- `.github/workflows/ci.yml` runs `tools/check_idf_example_contract.py`.
- CI includes an `idf-example-build` matrix for `esp32s3` and `esp32s2` using
  `espressif/idf:release-v5.4`.
- `tools/check_idf_example_contract.py` verifies:
  - no Arduino/compat tokens in the IDF example;
  - native `app_main`;
  - `driver/i2c_master.h`;
  - ESP timer and FreeRTOS timing;
  - core headers and `src/` remain framework-neutral;
  - root ESP-IDF component metadata exists;
  - ESP32-S2/S3 and IDF 5.4 metadata are present.
- The IDF example no longer depends on a checkout-directory component name.
- README labels examples as diagnostic/bring-up CLIs, not production task
  architectures.
- README gained the public API transaction/latency table.
- ESP-IDF documentation and hardware validation documentation were expanded.

### Validation

Prompt 03 validation recorded:

- core timing guard passed;
- CLI contract passed;
- IDF example contract passed;
- generated `Version.h` was up to date;
- native PlatformIO tests passed, 70/70;
- Arduino ESP32-S3 build passed;
- Arduino ESP32-S2 build passed;
- PlatformIO package pack passed and generated tarball was removed;
- local `idf.py` unavailable.

### Auditor Note

The CI job is configured, but a passing live GitHub CI run is external evidence
that must be checked separately.

## Prompt 04: Hardware Validation Checklist, Final Integration Review, Final Report

### Requested Scope

Prompt 04 was the originally planned final hardening prompt. It required:

- final local validation;
- hardware validation commands/checklists;
- full diff review for artifacts and overclaims;
- a comprehensive final branch report;
- final docs commit and push;
- no hardware validation claim without hardware logs.

### Outcome

- Created `docs/SHT3X_HARDWARE_VALIDATION_MATRIX.md`.
- Created `docs/SHT3X_HARDENING_FINAL_REPORT.md`.
- Documented final software behavior, public API changes, tests, ESP-IDF CI
  configuration, and remaining future work.
- All hardware validation rows were left as `Not run`.
- Final report concluded:
  - ready to merge if GitHub CI passes;
  - not ready for tagged public release;
  - not fully field-proven industry-grade.

### Validation

Prompt 04 final validation recorded:

- `git diff --check` passed;
- core timing guard passed;
- CLI contract passed;
- IDF example contract passed;
- generated `Version.h` was up to date;
- native PlatformIO tests passed, 70/70;
- Arduino ESP32-S3 build passed;
- Arduino ESP32-S2 build passed;
- PlatformIO package pack passed and generated tarball was removed;
- local `idf.py` unavailable;
- no hardware upload/monitor, ALERT pin, humidity fixture, fault injection, or
  soak tests were run.

## Prompt 05: Pre-HIL Readiness Gate

### Requested Scope

Prompt 05 was a focused pre-HIL readiness pass, not full HIL. It required:

- verifying CI/release readiness state;
- making the HIL runbook exact;
- ensuring CLI/API surfaces expose HIL data;
- adding acceptance criteria per test;
- adding a log-capture template;
- preventing overclaims;
- committing and syncing the final pre-HIL preparation.

### Outcome

- Added `docs/SHT3X_HIL_RUNBOOK.md`.
- Added `docs/SHT3X_HIL_LOG_TEMPLATE.md`.
- Added `docs/SHT3X_PRE_HIL_READINESS_REPORT.md`.
- Updated hardware validation docs so unexecuted HIL rows remain `Not run`.
- Added HIL-friendly CLI aliases to Arduino and ESP-IDF examples:
  - `single <low|medium|high>`;
  - `periodic start <rate> <rep>`;
  - `periodic fetch`;
  - `periodic stop`;
  - `art start`;
  - `art fetch`;
  - `art stop`;
  - `clear_status`;
  - `alert show`;
  - `alert set <kind> <T> <RH>`.
- Added `status_restore` to both CLIs. It calls
  `readStatusWithModeRestore()` and prints:
  - result status;
  - `initialMode`;
  - `finalMode`;
  - `modeInterrupted`;
  - `stopStatus`;
  - `statusReadStatus`;
  - `restoreStatus`;
  - `statusValid`;
  - `restored`;
  - parsed status bits when valid.
- Added IDF CLI visibility for:
  - full library version;
  - build timestamp;
  - git commit/status;
  - last error;
  - cached alert-limit settings.
- Strengthened CLI contract checks so HIL command exposure is guarded.
- Softened public wording that could imply production or field proof before HIL.

### Validation

Prompt 05 validation recorded:

- `git diff --check` passed with only a CRLF normalization warning for
  `docs/CODEX_PROMPT_SHT3X_DRIVER.md`;
- core timing guard passed;
- CLI contract passed;
- IDF example contract passed;
- generated `Version.h` was up to date;
- native PlatformIO tests passed, 70/70;
- Arduino ESP32-S3 build passed;
- Arduino ESP32-S2 build passed;
- PlatformIO package pack passed and generated tarball was removed;
- `idf.py --version` unavailable with PowerShell `CommandNotFoundException`.

### Readiness Result

The repository is ready to enter full HIL from a documentation and diagnostic
CLI perspective. It is not hardware-validated yet.

## Consolidated Validation Evidence

| Evidence area | Current status |
| --- | --- |
| Native unit tests | PASS, 70/70 in final local runs |
| Arduino ESP32-S3 build | PASS in final local runs |
| Arduino ESP32-S2 build | PASS in final local runs |
| Core timing/framework guard | PASS |
| Arduino/CLI contract guard | PASS |
| ESP-IDF example/core guard | PASS |
| Generated version check | PASS |
| PlatformIO package pack | PASS, artifact removed |
| Local `idf.py` | Unavailable in this shell |
| Pure ESP-IDF CI | Configured, live run must be verified externally |
| Hardware validation | Not run |
| ALERT pin validation | Not run |
| Humidity accuracy/fixture validation | Not run |
| Fault injection and soak | Not run |
| Release tag | Not created |

## Auditor-Facing Artifact Index

Key files to inspect:

- `AGENTS.md`
- `README.md`
- `.github/workflows/ci.yml`
- `include/SHT3x/SHT3x.h`
- `src/SHT3x.cpp`
- `test/test_basic.cpp`
- `examples/common/Sht3xCli.cpp`
- `examples/idf/basic/main/main.cpp`
- `tools/check_core_timing_guard.py`
- `tools/check_cli_contract.py`
- `tools/check_idf_example_contract.py`
- `docs/SHT3X_HARDENING_PLAN.md`
- `docs/SHT3X_ALERT_STATUS_FIX_REPORT.md`
- `docs/SHT3X_CORE_CONTRACTS_PARTIAL_STATE_REPORT.md`
- `docs/SHT3X_IDF_CI_DOCS_REPORT.md`
- `docs/SHT3X_HARDENING_FINAL_REPORT.md`
- `docs/SHT3X_HARDWARE_VALIDATION_MATRIX.md`
- `docs/HARDWARE_VALIDATION.md`
- `docs/SHT3X_HIL_RUNBOOK.md`
- `docs/SHT3X_HIL_LOG_TEMPLATE.md`
- `docs/SHT3X_PRE_HIL_READINESS_REPORT.md`

## Claim Boundaries For Audit

The following claims are supported by local work:

- The core library remains framework-neutral under repository guard checks.
- I2C ownership remains externally injected.
- Fallible APIs use `Status`.
- Timing hooks are required and guarded.
- Public APIs document synchronous, non-ISR-safe, not-internally-thread-safe use.
- The ALERT/status periodic-mode software blocker has an explicit helper with
  partial-state reporting.
- Native tests cover the major contract and partial-failure paths added in this
  sequence.
- Arduino S2/S3 PlatformIO builds pass locally.
- Pure ESP-IDF CI coverage is configured in GitHub Actions.
- The HIL runbook and log template are ready for execution.

The following claims are not supported yet:

- Hardware validation passed.
- ALERT pin behavior is proven on real hardware.
- Humidity accuracy is production-validated.
- Local pure ESP-IDF builds passed in this shell.
- The branch is field-proven or fully industry-grade.
- The work is a tagged public release.

## Recommended Auditor Review Sequence

1. Inspect the commit timeline from `02f52ea` through `92f87a3`.
2. Review `docs/SHT3X_ALERT_STATUS_FIX_REPORT.md` for the datasheet-backed
   ALERT/status design decision.
3. Review `include/SHT3x/SHT3x.h` and `src/SHT3x.cpp` for
   `StatusReadSnapshot` and `readStatusWithModeRestore()`.
4. Review native tests in `test/test_basic.cpp`, especially
   ALERT/status, partial-state, copy/move, and public API tests.
5. Review CI and guard scripts, especially `.github/workflows/ci.yml` and
   `tools/check_idf_example_contract.py`.
6. Review `docs/SHT3X_HIL_RUNBOOK.md` and
   `docs/SHT3X_HIL_LOG_TEMPLATE.md` before executing HIL.
7. Treat all HIL rows as `Not run` until logs are attached.

## Bottom Line

The prompt sequence moved the repository from an audited engineering-grade state
with a known SHT3x ALERT/status blocker to a locally tested, documented,
pre-HIL-ready state. The core software blocker is addressed, public contracts
and tests are stronger, and diagnostic CLIs now expose the HIL evidence needed
to validate the behavior on real hardware. The remaining gap is evidence, not
runbook preparation: full HIL, ALERT pin capture, pure ESP-IDF CI/run proof,
humidity fixture validation, fault injection, and soak testing still need to be
performed before stronger production or industry-grade claims are defensible.
