# SHT3x Industry-Readiness Hardening Final Report

Date: 2026-05-31
Branch: `hardening/sht3x-industry-readiness`

## Starting Audit Verdict

The starting audit classified the merged IDF branch as engineering-grade but
not industry-grade. The core architecture was good, but the audit identified a
high-priority SHT3x-specific blocker: ALERT status diagnosis was unavailable in
the same periodic/ART modes that produce ALERT events. Pure ESP-IDF CI proof and
hardware validation were also missing.

## Prompt and Commit Summary

| Commit | Scope |
| --- | --- |
| `c4192d3 Add SHT3X IDF-Merged Industry-Readiness Audit documentation` | Baseline audit report. |
| `02f52ea docs: plan SHT3x industry-readiness hardening` | Prompt 00 planning, risk breakdown, and AGENTS updates. |
| `457bcf1 fix: allow SHT3x alert status diagnosis in periodic mode` | Prompt 01 ALERT/status contract and helper. |
| `79d218e test: harden SHT3x core contracts and partial-state coverage` | Prompt 02 public contract, partial-state, and native-test hardening. |
| `1e554a1 ci: validate SHT3x ESP-IDF port and document contracts` | Prompt 03 ESP-IDF CI, guard, docs, example honesty, validation matrix. |
| This report commit | Prompt 04 final matrix, final integration report, changelog correction. |

## Subagents Used

Prompt 04 used three read-only subagents:

- `final-integration-review-agent`: reviewed full branch diff, artifacts, docs/API agreement, and overclaims.
- `hardware-validation-agent`: reviewed actual Arduino/IDF CLI surfaces and proposed hardware validation procedures.
- `release-readiness-agent`: assessed merge, release, and industry-grade claim readiness.

Earlier prompts used domain-focused read-only agents for datasheet/status
review, core contracts, tests, IDF CI, documentation, examples, and final guard
checks. Their findings are preserved in the prompt-specific reports.

## Core Changes

- Added explicit periodic/ART ALERT diagnosis path through
  `readStatusWithModeRestore()`.
- Preserved non-disruptive `readStatus()` behavior: it returns `BUSY` while
  periodic/ART acquisition is active instead of sending undocumented commands.
- Added partial-state reporting for break/status/restore status reads.
- Tightened multi-step behavior around periodic restarts, alert writes,
  reset/restore, and recovery.
- Kept framework-neutral core boundaries and injected transport/timing.
- Explicitly deleted copy and move construction/assignment for `SHT3x`.

## Public API Changes

- Added `StatusReadSnapshot`.
- Added `SHT3x::readStatusWithModeRestore(StatusReadSnapshot&)`.
- Added `SettingsSnapshot::statusReadStatus`.
- Updated public Doxygen for thread/ISR safety, callback recursion limits,
  clock-stretch scope, heater caveats, status-bit clearability, cached-settings
  semantics, and partial-state expectations.
- Copy/move deletion is release-visible and can break by-value usage.

## Test Changes

- Native tests now cover ALERT/status helper success and failure paths,
  status/read-settings behavior, status clear semantics, public API invalid
  address handling, single-shot timing, periodic not-ready behavior, cache
  mutation rules, alert-limit partial failures, reset/restore partial failures,
  OFFLINE latch behavior, and copy/move compile-time traits.
- Final native validation passed 70/70 tests.

## ESP-IDF Example and CI Changes

- CI now runs `tools/check_idf_example_contract.py` directly.
- CI is configured to build `examples/idf/basic` for `esp32s3` and `esp32s2`
  using `espressif/idf:release-v5.4`; live workflow proof still depends on the
  repository CI run.
- `idf_component.yml` declares `idf: ">=5.4"`.
- The IDF example no longer depends on checkout-directory component name
  `SHT3x-main`.
- The IDF example remains native IDF: `app_main`, `driver/i2c_master.h`,
  `esp_timer`, FreeRTOS task/queue timing, fixed C buffers, and no Arduino
  compatibility facade.
- The example is explicitly labeled diagnostic/bring-up, not production-style.

## Documentation Changes

- README documents ALERT/status behavior, status clear semantics,
  thread/ISR contract, clock stretching, heater caveats, general-call reset,
  transport callback recursion, API transaction/latency model, and hardware
  validation requirements.
- Added `docs/SHT3X_ALERT_STATUS_FIX_REPORT.md`.
- Added `docs/SHT3X_CORE_CONTRACTS_PARTIAL_STATE_REPORT.md`.
- Added `docs/SHT3X_IDF_CI_DOCS_REPORT.md`.
- Added `docs/HARDWARE_VALIDATION.md`.
- Added `docs/SHT3X_HARDWARE_VALIDATION_MATRIX.md`.

## ALERT/Status Final Behavior

- `readStatus()` is non-destructive and returns `BUSY` during active
  periodic/ART acquisition.
- `clearStatus()` is explicit and destructive for status flags 15, 11, 10, and 4.
- `readStatusWithModeRestore()` is the explicit helper for ALERT diagnosis while
  periodic/ART is active. It sends Break, reads status, and attempts to restore
  the previous acquisition mode.
- The helper is intentionally disruptive to cadence. Callers must inspect
  `StatusReadSnapshot::stopStatus`, `statusReadStatus`, `restoreStatus`,
  `statusValid`, and `restored` on failure.
- Real ALERT pin and humidity/temperature threshold behavior remains hardware
  validation work.

## Partial-State and Cache Assessment

The branch now documents and tests the highest-risk partial-state behavior:

- periodic restart can leave hardware stopped if Break succeeds and restart fails;
- alert-limit verification failure can mean hardware changed but cache did not;
- `disableAlerts()` can partially apply the first threshold before the second fails;
- `resetAndRestore()` can partially restore cached settings;
- `readStatusWithModeRestore()` exposes stop/read/restore partial state.

No generic dirty flag was added. The branch uses explicit result fields and
cache-update-after-success rules instead.

## Thread, ISR, Copy, and Move Contracts

- Public driver APIs are synchronous, not ISR-safe, and not internally thread-safe.
- Applications must serialize shared-bus or multi-task access externally.
- Transport and reset callbacks must not recursively call public APIs on the same
  driver instance.
- `SHT3x` is neither copyable nor movable.

## Final Local Validation

| Command | Result |
| --- | --- |
| `git status --short` | PASS, clean before final docs edits |
| `git diff --stat` | PASS, no output before final docs edits |
| `git diff --check` | PASS |
| `python tools/check_core_timing_guard.py` | PASS |
| `python tools/check_cli_contract.py` | PASS |
| `python tools/check_idf_example_contract.py` | PASS |
| `python scripts/generate_version.py check` | PASS, `Version.h` up to date |
| `python -m platformio test -e native` | PASS, 70/70 tests |
| `python -m platformio run -e esp32s3dev` | PASS |
| `python -m platformio run -e esp32s2dev` | PASS |
| `python -m platformio pkg pack` | PASS, wrote `SHT3x-1.5.0.tar.gz`; tarball removed |

## Commands Not Run

| Command/procedure | Reason |
| --- | --- |
| `idf.py --version` | Failed locally: `idf.py` is not recognized as a cmdlet, function, script file, or operable program. |
| `idf.py -C examples/idf/basic set-target esp32s3 build` | Not run because local `idf.py` is unavailable. Covered by CI configuration, pending CI result. |
| `idf.py -C examples/idf/basic set-target esp32s2 build` | Not run because local `idf.py` is unavailable. Covered by CI configuration, pending CI result. |
| Arduino/IDF upload and monitor commands | Not run because no hardware run was requested or available in this workspace. |
| ALERT pin, humidity fixture, fault-injection, and long-soak tests | Not run; require real boards, sensor hardware, fixtures, or fault-injection jig. |

## Hardware Validation Matrix Status

`docs/SHT3X_HARDWARE_VALIDATION_MATRIX.md` defines the hardware procedure. All
hardware scenarios are `Not run`. Software tests and CI do not prove board
electrical behavior, ALERT pin wiring, production humidity accuracy, or fault
behavior under real bus failures.

## Remaining Future Work

- Run GitHub CI and confirm the new pure ESP-IDF S2/S3 jobs pass.
- Execute the hardware validation matrix on ESP32-S2 and ESP32-S3 boards.
- Capture ALERT pin evidence with periodic mode and controlled threshold crossings.
- Validate clock stretching, heater cooldown, reset paths, recovery, and
  CRC/fault injection on real hardware or a controlled jig.
- For release: bump version, promote `[Unreleased]` changelog entries, regenerate
  version metadata, package, tag, and publish release notes.

## Merge Readiness Verdict

Ready to merge if GitHub CI passes. No high-severity software blocker remains in
the local branch after final validation.

## Release Readiness Verdict

Not ready to release as a tagged public release. The work is still under
`[Unreleased]`, version metadata remains `1.5.0`, no release tag points at HEAD,
and pure ESP-IDF build proof depends on CI.

## Industry-Grade Claim Verdict

Not fully field-proven industry-grade. The software contracts are substantially
hardened, but hardware ALERT validation, humidity fixture validation,
fault-injection evidence, and soak data are still missing.
