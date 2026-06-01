# SHT3x Release-Readiness Gaps Fix Report

Date: 2026-06-01
Branch: `hardening/sht3x-release-readiness-gaps`
Base reviewed: `origin/main` at merge-base `87213c686df2ea92240a660da5641eb51f22bc67`
Head reviewed before this final report commit: `e8af2f535ed57253a3a134eca31270e250ad2f1d`

This report closes the AI-programmer-achievable release-readiness gap review for
the branch. It is not a release certificate and does not tag a release.

This pass did not run full HIL and does not validate physical ALERT pin behavior,
humidity accuracy, fault injection, or long-soak stability.

## Commit List

- `8661593` / `8661593669c792dd362326b8eda1a77d50818235` - docs: plan SHT3x release-readiness gap closure
- `f072c21` / `f072c21ef74312ed56b74b2c84041184c4bdcd8a` - docs: plan SHT3x release-readiness gap closure
- `870feae` / `870feaec7e5290630a3b4814ae8cab1b3f484284` - fix: verify SHT3x alert limit encoding vectors
- `221d6c6` / `221d6c6a162fb5059363104c2b424f8478bc301c` - docs: clarify SHT3x API side-effect contracts
- `5744e5d` / `5744e5df15ee1e88710626d4959eb8276c594b03` - test: extend SHT3x automatic HIL evidence runner
- `cfb5ae0` / `cfb5ae06b670b6dec562bdf1dd8668e9433573f9` - docs: update SHT3x automatic HIL runner report
- `e8af2f5` / `e8af2f535ed57253a3a134eca31270e250ad2f1d` - docs: record SHT3x HIL package inspection

## Version Selected

Current metadata remains `1.5.0`:

- `library.json`: `1.5.0`
- `idf_component.yml`: `1.5.0`
- `Doxyfile`: `PROJECT_NUMBER = "1.5.0"`
- `include/SHT3x/Version.h`: generated `1.5.0`, verified up to date

Release version selection remains pending. A local `v1.5.0` tag already exists,
and this branch describes as `v1.5.0-31-ge8af2f5` before the final report commit.
Do not tag a release from this branch without choosing the intended next SemVer
version and regenerating metadata.

## Files Changed

Branch delta reviewed before this report:

- `.gitignore`
- `.pioignore`
- `README.md`
- `docs/HARDWARE_VALIDATION.md`
- `docs/IDF_PORT.md`
- `docs/README.md`
- `docs/SHT3X_ALERT_ENCODING_FIX_REPORT.md`
- `docs/SHT3X_API_CONTRACT_CLEANUP_REPORT.md`
- `docs/SHT3X_AUTOMATIC_HIL_RUNNER_REPORT.md`
- `docs/SHT3X_CORE_CONTRACTS_PARTIAL_STATE_REPORT.md`
- `docs/SHT3X_HARDWARE_VALIDATION_MATRIX.md`
- `docs/SHT3X_HIL_EVIDENCE_PACKAGE_CLEANUP_REPORT.md`
- `docs/SHT3X_HIL_RUNBOOK.md`
- `docs/SHT3X_I2C_HIL_RUNBOOK.md`
- `docs/SHT3X_I2C_HIL_SELFTEST_REPORT.md`
- `docs/SHT3X_RELEASE_READINESS_GAPS_PLAN.md`
- `docs/SHT3x_driver_extraction.md`
- `include/SHT3x/SHT3x.h`
- `include/SHT3x/Status.h`
- `library.json`
- `src/SHT3x.cpp`
- `test/test_basic.cpp`
- `tools/run_i2c_hil.py`
- `tools/test_run_i2c_hil_parser.py`

This final report commit updates:

- `docs/SHT3X_RELEASE_READINESS_GAPS_FIX_REPORT.md`

## Audit Findings Addressed

- Version metadata consistency: addressed for the current `1.5.0` metadata.
  `scripts/generate_version.py check` passed.
- Alert-limit encoding: addressed in software. Native tests cover Sensirion
  app-note words `0xCD33`, `0xC92D`, `0x3869`, and `0x3466`, including write
  payload CRC behavior.
- Raw command, reset, and `tick()` contracts: addressed through README/API
  contract documentation and native public API tests.
- Package/export hygiene: addressed. `library.json` has explicit export
  include/exclude rules; `.pioignore` and `.gitignore` exclude generated
  package/build/scratch artifacts.
- HIL docs/evidence consistency: addressed. Documentation now scopes historical
  smoke-HIL evidence and keeps unrun hardware evidence as pending.
- HIL runner device tester: addressed as a host-side runner/parser and evidence
  packaging tool. Local runner contract/parser checks passed.

## Audit Findings Still Pending

- Release version selection: pending. Current metadata is still `1.5.0`; do not
  reuse the existing `v1.5.0` release tag.
- Changelog/migration notes: partially addressed. README/API notes document the
  behavior changes, but `CHANGELOG.md` still needs a final release section before
  tagging.
- Pure ESP-IDF CI/build proof: pending in this local environment because `gh`
  and `idf.py` are unavailable from PATH.
- Full current-head HIL: pending by request; this final pass did not run full
  hardware-in-the-loop validation.

## ESP-IDF CI Status

Live GitHub CI status was not retrievable from this shell because `gh` is not
installed or not on PATH:

`gh : The term 'gh' is not recognized as the name of a cmdlet, function, script file, or operable program.`

The CI workflow contains ESP-IDF example build coverage for ESP32-S3 and
ESP32-S2, but the live run status for this branch must be checked externally in
GitHub or from an environment with `gh` installed and authenticated.

Local pure ESP-IDF builds were also not run because `idf.py` is not installed or
not on PATH:

`idf.py : The term 'idf.py' is not recognized as the name of a cmdlet, function, script file, or operable program.`

Therefore this report does not claim pure ESP-IDF validation.

## Alert Vector Test Status

Status: passed as software-only validation.

- `python -m platformio test -e native` passed 76/76 tests.
- Alert app-note encode/decode vector tests passed for `0xCD33`, `0xC92D`,
  `0x3869`, and `0x3466`.
- Write payload CRC behavior is covered by native tests.

These tests do not validate physical ALERT pin behavior or humidity-threshold
trip/clear behavior on hardware.

## Local Checks And Results

- `git status --short`: passed; clean before validation.
- `git diff --stat`: passed; no unstaged diff before validation.
- `git diff --check`: passed.
- `python tools/check_core_timing_guard.py`: passed.
- `python tools/check_cli_contract.py`: passed.
- `python tools/check_idf_example_contract.py`: passed.
- `python scripts/generate_version.py check`: passed; `Version.h` is up to date.
- `python -m platformio test -e native`: passed; 76/76 tests.
- `python -m platformio run -e esp32s3dev`: passed.
- `python -m platformio run -e esp32s2dev`: passed.
- `python -m platformio pkg pack`: passed; generated `SHT3x-1.5.0.tar.gz`.
- Package artifact cleanup: passed; `SHT3x-1.5.0.tar.gz` was removed.
- `idf.py --version`: unavailable; `idf.py` not found on PATH.
- `idf.py -C examples/idf/basic set-target esp32s3 build`: not run because
  `idf.py` is unavailable.
- `idf.py -C examples/idf/basic set-target esp32s2 build`: not run because
  `idf.py` is unavailable.

Additional local checks run for this report:

- `python tools/check_hil_contract.py`: passed.
- `python tools/test_run_i2c_hil_parser.py`: passed.

## Package Artifact Status

`python -m platformio pkg pack` produced
`C:\Users\HonzovoSpectre\Documents\Projects\SHT3x-main\SHT3x-1.5.0.tar.gz`.
The generated tarball was removed after the packaging check and is not tracked.

## Automatic HIL Runner Status

The automatic HIL runner/device tester is present as a host-side evidence runner
with parser tests and contract checks. It supports default smoke evidence,
selected measurement modes, alert encode/decode vectors, status restoration,
operator-gated destructive groups, JSON/Markdown summaries, and fixture/operator
evidence files.

Current status: tooling validated locally; full hardware execution not run in
this pass.

Historical smoke-HIL evidence is limited to `hil_logs/i2c_20260531T155925Z`,
COM17, address `0x44`, branch `hardening/sht3x-industry-readiness`, host commit
`8661a38cc70e629cd337ac45c42a1885aefb0cfc`; flashed firmware reported git
commit `unknown`, so it is not current-head release evidence.

## Hardware-Only Blockers Remaining

- Full current-head HIL run.
- Physical ALERT pin transition and threshold trip/clear validation.
- Humidity accuracy validation against a controlled/reference fixture.
- Fault injection evidence for bus/device failures.
- ESP32-S2 hardware smoke run.
- Clock-stretching hardware behavior.
- Long-soak stability.

## Merge Verdict

Conditionally merge-ready for the AI-programmer-achievable gap closure, provided
external GitHub CI for the branch is confirmed green. Local native tests, guard
scripts, Arduino S2/S3 builds, package pack, and package cleanup passed.

## Release Verdict

No-go for release. Required release work remains: choose the next SemVer version,
update release metadata and `CHANGELOG.md`, confirm live CI status, obtain pure
ESP-IDF build evidence, and complete the required hardware evidence boundary for
the claims intended in the release.

## Industrial-Grade Verdict

No-go for industrial-grade validation. The software-side gaps are closed or
bounded, but hardware-only evidence remains for ALERT behavior, humidity
accuracy, fault injection, ESP32-S2 hardware, clock stretching, full HIL, and
long-soak stability.
