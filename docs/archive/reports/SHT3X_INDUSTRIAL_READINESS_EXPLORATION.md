# SHT3x Industrial-Grade Readiness Exploration

Date: 2026-05-31
Branch: `main`
Commit: `ba71d1ab12fa8796e24a6fc8344fa3562282999f`
Audit mode: exploration-only / no implementation

## Executive Verdict

Not release-ready.

The library is materially stronger than the last released `v1.5.0`, and the
default Arduino smoke-HIL sequence passed on real ESP32-S3-class hardware at
SHT3x address `0x44`. That is useful evidence.

It is not enough for a release or industrial-grade claim. The current `main`
still reports version `1.5.0`, while `v1.5.0` already exists. Live GitHub CI for
this exact commit is failed because both pure ESP-IDF example jobs failed.
Hardware evidence is narrow: one automated smoke-HIL pass, no completed target
template, no ESP32-S2 hardware log, no ALERT pin evidence, no fault-injection
evidence, no clock-stretch evidence, no soak, and no humidity accuracy fixture.

There are also remaining API/datasheet risks. The largest is alert-limit
encoding: local app-note default vectors do not all match the current helper's
computed values. That is a release blocker for any release that claims ALERT
limit write support is production-ready.

## Evidence Reviewed

- Current branch and history:
  - `main` at `ba71d1ab12fa8796e24a6fc8344fa3562282999f`.
  - Latest release tag found locally: `v1.5.0`.
  - Recent merged hardening commit: `8661a38cc70e629cd337ac45c42a1885aefb0cfc`.
- Required baseline checks:
  - `git status --short`: clean before report creation.
  - `git branch --show-current`: `main`.
  - `git log --oneline --decorate -15`.
  - `git diff --check`: pass.
- Release and package files:
  - `library.json`
  - `idf_component.yml`
  - `include/SHT3x/Version.h`
  - `scripts/generate_version.py`
  - `CHANGELOG.md`
  - `README.md`
  - `.gitignore`
- Public API and implementation:
  - `include/SHT3x/*.h`
  - `src/SHT3x.cpp`
  - `examples/common/*`
  - `examples/idf/basic/*`
- HIL and docs:
  - `hil_logs/i2c_20260531T152842Z/*`
  - `hil_logs/i2c_20260531T155925Z/*`
  - `docs/README.md`
  - `docs/HARDWARE_VALIDATION.md`
  - `docs/SHT3X_HARDWARE_VALIDATION_MATRIX.md`
  - `docs/SHT3X_HIL_RUNBOOK.md`
  - `docs/SHT3X_I2C_HIL_RUNBOOK.md`
  - `docs/SHT3X_I2C_HIL_SELFTEST_REPORT.md`
- Datasheet extracts:
  - `docs/pdf-extracted-md/SHT3x_datasheet.md`
  - `docs/pdf-extracted-md/SHT3x_HT_AN_AlertMode.md`
  - `docs/pdf-extracted-md/Sensirion_electronic_identification_code_SHT3x.md`
  - `docs/extracted-md/*`
- CI:
  - `.github/workflows/ci.yml`
  - Public GitHub Actions API for run `26720609506`:
    `https://github.com/janhavelka/SHT3x-main/actions/runs/26720609506`

## What Is Actually Proven

### Proven by local software tests

- `python tools/check_core_timing_guard.py`: pass.
- `python tools/check_cli_contract.py`: pass.
- `python tools/check_idf_example_contract.py`: pass.
- `python scripts/generate_version.py check`: pass.
- `python -m platformio test -e native`: pass, 70/70.

These prove host-side fake-transport behavior and repository contract guards.
They do not prove physical electrical behavior.

### Proven by Arduino ESP32-S2/S3 builds

- `python -m platformio run -e esp32s3dev`: first non-verbose run failed at the
  link step without a useful diagnostic; immediate verbose rerun passed.
- `python -m platformio run -e esp32s2dev`: pass.

This proves Arduino firmware buildability in this shell. It does not prove
upload, boot, or hardware behavior for ESP32-S2.

### Proven by smoke-HIL

Latest automated smoke-HIL log:
`hil_logs/i2c_20260531T155925Z/summary.md`.

Proven by that run:

- Serial CLI booted on ESP32-S3-class target.
- I2C scan found SHT3x default address `0x44`.
- `probe` returned OK.
- `settings` and `drv` reported initialized `READY`.
- `status` and `status_raw` returned `0x8010`.
- One high-repeatability no-stretch single-shot read succeeded.
- Raw and compensated cached reads succeeded.
- No-stretch serial/EIC read succeeded: `0x29075EB0`.
- Heater status read returned `OFF`.
- All four alert limits were read.
- `status_restore` returned OK.
- One `periodic start 1 high`, `periodic fetch`, and `periodic stop` sequence
  succeeded.
- Final health was `READY`, 24 successes, 0 failures.

Boundary: the HIL log itself records branch
`hardening/sht3x-industry-readiness` and commit `8661a38...`, not current
`main` commit `ba71d1a...`. Current `main` has the logs committed, but no
smoke-HIL log for the exact current HEAD was found.

### Proven by manual HIL

No completed manual operator artifact was found in the repository. The pasted
instructions say manual read, selftest, recover, clear_status, status_restore,
and drv sanity checks were performed, but there is no completed `docs/hil/`
record or target template in-tree.

Treat those manual checks as operator-reported context, not attachable release
evidence.

### Configured but not yet proven

- Pure ESP-IDF CI jobs are configured for `esp32s3` and `esp32s2`, but the live
  run for `ba71d1a...` failed.
- ESP-IDF local build is not proven because `idf.py --version` is unavailable in
  this shell:
  `CommandNotFoundException`.
- ESP32-S2 hardware HIL is planned but no ESP32-S2 hardware log was found.
- Address `0x45` HIL is planned but not run.
- Clock-stretching HIL is planned but not run.
- Full periodic matrix across 0.5/1/2/4/10 mps is planned but not run.
- ART-mode HIL is planned but not run.
- ALERT threshold and physical ALERT pin HIL are planned but not run.
- Fault-injection HIL is planned but not run.
- Soak/stress HIL is planned but not run.
- Humidity/temperature accuracy fixture validation is planned but not run.

### Not tested

- Humidity accuracy against a reference sensor and controlled fixture.
- ALERT pin assertion/deassertion on threshold crossing.
- ALERT pin stuck/asserted behavior.
- Fault injection for read CRC corruption on measurement, serial, and alert
  frames.
- Physical timeout/NACK/stuck-bus/unplug/replug behavior.
- Bad pull-up / edge-rate behavior.
- Long soak stability.
- ESP32-S2 hardware behavior.
- Pure ESP-IDF target hardware behavior.

## Remaining P0 Blockers

P0 = must fix or validate before release or industrial-grade claim.

### P0-1: Version and release metadata are not release-ready

Finding: `library.json`, `idf_component.yml`, `include/SHT3x/Version.h`, and
`Doxyfile` still say `1.5.0`, while `v1.5.0` already exists. `[Unreleased]`
contains API and behavior changes.

Evidence:

- `library.json`: version `1.5.0`.
- `idf_component.yml`: version `1.5.0`.
- `include/SHT3x/Version.h`: `SHT3X_VERSION_STRING "1.5.0"`.
- `CHANGELOG.md`: active `[Unreleased]` section.
- Latest local release tag: `v1.5.0`.

Impact: a package or tag from this state would collide with the existing release
version and hide compatibility-sensitive changes.

Recommended action: choose the next SemVer version, likely `2.0.0` if required
timing callbacks/copy-move deletion are treated as breaking. Promote the
changelog, update generated version files, update compare refs, and tag only
after CI and release gates pass.

Type: code metadata and release documentation.

### P0-2: Breaking and migration-sensitive changes are not labeled clearly

Finding: copy/move construction and assignment are deleted, timing callbacks are
required, and `readStatus()` now returns `BUSY` in active periodic/ART modes.
These are described, but not under a clear `Breaking Changes` or migration
section.

Evidence:

- `CHANGELOG.md` documents copy/move deletion under `Changed`.
- `README.md` documents required `nowMs`, `nowUs`, and `cooperativeYield`.
- `SHT3x.h` deletes copy/move operations.
- `README.md` documents `readStatusWithModeRestore()`.

Impact: existing users can break at compile time or runtime without clear
upgrade instructions.

Recommended action: add a release migration section before release. State what
changed, why, and what users must do.

Type: documentation and release notes.

### P0-3: Live pure ESP-IDF CI is failing for current `main`

Finding: CI is configured, but the live GitHub Actions run for current HEAD
`ba71d1ab12fa8796e24a6fc8344fa3562282999f` has conclusion `failure`.
`idf-example-build (esp32s3)` and `idf-example-build (esp32s2)` both failed.

Evidence:

- Workflow config: `.github/workflows/ci.yml` defines `idf-example-build` with
  `espressif/idf:release-v5.4`, targets `esp32s3` and `esp32s2`, and runs
  `idf.py -C examples/idf/basic set-target ... build`.
- Public run: `https://github.com/janhavelka/SHT3x-main/actions/runs/26720609506`
  conclusion `failure`.
- Jobs:
  - `idf-example-build (esp32s3)`: failure.
  - `idf-example-build (esp32s2)`: failure.
- Local `idf.py --version`: unavailable.

Impact: do not claim pure ESP-IDF validation. Do not release with ESP-IDF
framework support claims stronger than "configured but currently failing live
CI".

Recommended action: inspect CI logs with authenticated GitHub access, fix the
job, and require a passing CI run for both IDF targets.

Type: CI/build validation.

### P0-4: Alert-limit encoding has a datasheet/app-note mismatch

Finding: local Sensirion alert app-note defaults list:

- `80% / 60 C -> 0xCD33`
- `79% / 58 C -> 0xC92D`
- `22% / -9 C -> 0x3869`
- `20% / -10 C -> 0x3466`

The current `encodeAlertLimit()` uses rounded full-scale conversion and then
drops bits. Read-only audit found this does not reproduce all listed vectors
for `79/58` and `20/-10`.

Evidence:

- `docs/pdf-extracted-md/SHT3x_HT_AN_AlertMode.md` table lines for initial
  alert values.
- `src/SHT3x.cpp` `encodeAlertLimit()` implementation.
- Smoke-HIL read defaults matching the app-note values, but did not write or
  verify new physical threshold values.

Impact: ALERT threshold writes can be off relative to the only concrete local
vendor vectors. For a driver advertising alert-limit write helpers, this is a
release blocker unless proven intentional by formula/spec and tests.

Recommended action: add vendor-vector tests and either fix the packing/rounding
or document why the app-note physical values are approximate labels and the
current formula is correct.

Type: likely code and test fix.

### P0-5: Hardware status docs contradict the new HIL logs

Finding: `docs/README.md` still says no physical HIL run has been performed,
and `docs/HARDWARE_VALIDATION.md` still marks all hardware rows `Not run`.
Tracked `hil_logs/i2c_20260531T155925Z` proves one automated smoke-HIL pass.

Evidence:

- `docs/README.md`: no physical HIL run wording.
- `docs/HARDWARE_VALIDATION.md`: hardware rows remain `Not run`.
- `hil_logs/i2c_20260531T155925Z/summary.md`: final verdict `PASS`.

Impact: auditors will see contradictory evidence. It also becomes unclear which
hardware rows are actually proven and which remain pending.

Recommended action: update docs to mark only the exact smoke-HIL rows as passed,
with commit/board/address boundaries. Leave all unproven rows `Not run`.

Type: documentation/evidence bookkeeping.

## Remaining P1 Blockers

P1 = should fix before public release or production-oriented claim.

### P1-1: HIL pass is not tied to current HEAD

The committed PASS log records `8661a38...`, not `ba71d1a...`. Current `main`
adds the logs and merge commits after that. If release evidence must be exact,
rerun the HIL runner after the release candidate commit is final and record that
commit.

### P1-2: HIL logs are tracked while package export rules are incomplete

`.gitignore` ignores `hil_logs/`, but `git ls-files` shows `hil_logs/i2c_*`
artifacts are tracked. `library.json` has no explicit export include/exclude
rules. Source archives or packages may include audit logs unless package
export is controlled.

### P1-3: Public raw command helpers can violate datasheet mode restrictions

Datasheet text says periodic acquisition should be stopped before sending
another command except Fetch Data. High-level helpers mostly enforce this.
Public low-level helpers `writeCommand()`, `writeCommandWithData()`, and
`readCommand()` do not guard active periodic/ART state and can desynchronize
driver state from hardware. If these remain escape hatches, docs must clearly
transfer mode legality and CRC responsibility to the caller.

### P1-4: Reset preconditions and side effects need tightening

`softReset()` blocks periodic/ART but not pending single-shot work.
`generalCallReset()` has no pending or periodic/ART guard. `end()` only clears
local state and does not send Break, so hardware periodic/ART acquisition may
continue after the driver reports `UNINIT`.

### P1-5: `tick()` hides failures through side channels only

`tick()` may perform I2C but returns `void`. Failures are visible only through
health state and last-error fields. This can be acceptable, but public docs need
to make this explicit because callers cannot handle a `Status` directly.

### P1-6: Manual HIL evidence is not attached

The prompt says manual read, selftest, recover, clear_status, status_restore,
and drv sanity checks were run. No completed operator target template, manual
log, or `docs/hil/` evidence set was found. Keep these as operator-reported
until artifacted.

### P1-7: ESP32-S2 hardware is unproven

Arduino ESP32-S2 firmware builds locally, but no ESP32-S2 physical HIL log was
found.

### P1-8: Clock stretching is unproven

The smoke-HIL run used no-stretch paths. Clock-stretch commands and timeout
behavior need either a real pass or an explicit unsupported/timeout record for
the target transport.

## Remaining P2 Improvements

- Add native tests for `I2C_NACK_ADDR` injection if not already covered.
- Broaden CRC corruption tests beyond status restore to measurement, serial,
  and alert frames.
- Add target-template completion checks for HIL artifacts.
- Add package export rules to keep generated logs and local docs out of package
  payloads if that is desired.
- Add `library.properties` only if Arduino Library Manager distribution is an
  actual goal.
- Clarify that `totalSuccess()` and `totalFailures()` are per begin/end session,
  not lifetime across `begin()`.
- Clarify that `Status::operator bool()` is false for `IN_PROGRESS`.
- Remove or justify the heater wording about "condensation mitigation" because
  local datasheet extracts only support plausibility checking/self-heating.

## API / ABI / Migration Assessment

Public API changed since `v1.5.0`:

- New `StatusReadSnapshot`.
- New `readStatusWithModeRestore()`.
- New `SettingsSnapshot::statusReadStatus`.
- Copy/move construction and assignment for `SHT3x` are explicitly deleted.
- `begin()` now requires timing/yield callbacks.
- `readStatus()` behavior during periodic/ART is intentionally non-disruptive
  and returns `BUSY`; `readStatusWithModeRestore()` is the disruptive helper.
- ESP-IDF framework support is declared in package metadata.

Compatibility impact:

- Copying/moving `SHT3x` instances now fails at compile time.
- Existing config setup that omitted timing callbacks now fails at `begin()`.
- Existing code that expected `readStatus()` to work during periodic/ART must
  switch to `readStatusWithModeRestore()` and handle cadence interruption.
- Existing code that uses `if (status)` must remember that `IN_PROGRESS` is not
  boolean true.

Docs status:

- README documents the new status helper and the timing callback requirement.
- Changelog mentions the changes, but not as a clear breaking/migration block.
- Public Doxygen is generally strong, but side effects around `end()`,
  `recover()`, low-level raw command helpers, reset APIs, and `tick()` need
  sharper wording.

## Datasheet-Risk Assessment

- Periodic/ART command legality: high-level helpers mostly avoid unsupported
  command sequences, but raw public command helpers can bypass the rule.
- Break behavior: supported and used for periodic stop/status restore.
- Status clear behavior: parser and docs align with clearable bits 15, 11, 10,
  and 4.
- ALERT encoding: unresolved risk. App-note default vectors do not all match
  the current encode helper's computed values.
- ALERT threshold behavior: not physically validated. No GPIO or logic-analyzer
  evidence exists.
- Heater behavior: status read is smoke-tested, but enable/cooldown behavior is
  not. Public wording should avoid unsupported claims.
- Serial/EIC: no-stretch serial read passed smoke-HIL, and implementation
  CRC-checks both words.
- Clock stretching: configured but untested on hardware.
- Soft reset/general-call reset: local side effects and preconditions need
  clearer docs or stricter guards.

## HIL Evidence Assessment

| Scenario | Evidence exists? | Status | Notes |
| --- | --- | --- | --- |
| smoke read | yes | pass | ESP32-S3-class target, COM17, address `0x44`, commit `8661a38...`; not exact current HEAD. |
| selftest | operator-reported only | not artifacted | Prompt says it ran manually; no completed manual log found. |
| recover | operator-reported only | not artifacted | Prompt says it ran manually; no completed manual log found. |
| status clear | operator-reported only | not artifacted | Prompt says it ran manually; no completed manual log found. |
| status_restore | yes | pass | Automated smoke-HIL command passed. |
| periodic fetch | yes | pass | One `1 mps high` start/fetch/stop sequence passed. Other rates not run. |
| ALERT pin threshold | no | not run | Requires GPIO/logic-analyzer evidence. |
| heater enable/cooldown | no | not run | Only heater status OFF was read. |
| clock stretching | no | not run | Smoke-HIL used no-stretch commands. |
| fault injection | no | not run | Requires safe jig/interposer/emulator. |
| soak | no | not run | `stress 10` and longer soak were skipped. |
| ESP32-S2 hardware | no | not run | Build passed; no hardware log found. |
| ESP32-S3 hardware | yes | pass, limited | Smoke-HIL passed on ESP32-S3-class target. |

## ESP-IDF Readiness

ESP-IDF status: configured only, not proven.

- Pure ESP-IDF S2/S3 jobs are configured in GitHub Actions.
- Live CI for current `main` is failing in both IDF example jobs.
- Local `idf.py` is unavailable in this shell.
- No local `examples/idf/basic/build` evidence was found.
- No ESP-IDF hardware HIL log was found.
- The IDF example is diagnostic-only, not a production task architecture.
- Bus ownership is correctly external to the library.
- IDF error mapping is intentionally coarse for NACK phase; it preserves raw
  `esp_err_t` in `Status.detail` but does not prove address/data/read-header
  NACK phase.
- The example keeps `tick()` alive from the main owner loop while stdin is read
  in a separate task.
- ESP-IDF `>=5.4` is justified as the declared support floor for the new
  `i2c_master` and split driver component path, but older versions were not
  tested to prove that 5.4 is minimal.

## Documentation Readiness

The docs are coherent enough for continued development and HIL work. They are
not coherent enough for release as-is.

Main issues:

- Hardware validation docs still say no physical HIL was performed, despite a
  tracked smoke-HIL PASS log.
- Release docs still describe this as `[Unreleased]` with version `1.5.0`.
- Migration/breaking changes are not clearly separated.
- Some API side effects are underdocumented.
- HIL evidence is split between generated `hil_logs/` and stale status docs.

Overclaim scan:

- No tracked project doc was found claiming full industry-grade, field-proven,
  release-ready, or full hardware validation status.
- Hits under `docs/pdf-extracted-md/*` are vendor disclaimer text.
- `docs/README.md` uses claim-boundary language correctly.

## Release Readiness Checklist

| Item | Status | Notes |
| --- | --- | --- |
| worktree clean | pass before report | Report creation is the only intended tracked change. |
| tests pass | pass | Guard scripts and native tests passed locally. |
| Arduino S2/S3 builds pass | pass with note | S2 passed; S3 passed on verbose rerun after one unexplained non-verbose link failure. |
| pure ESP-IDF CI pass | fail | Live GitHub run `26720609506` failed IDF S2/S3 jobs. |
| version updated | fail | Metadata still says `1.5.0`; latest tag is `v1.5.0`. |
| changelog promoted | fail | Changes remain under `[Unreleased]`. |
| package verified | partial | `pio pkg pack` passed earlier, but export rules/log inclusion need review. |
| tag created | fail | No tag for current HEAD. |
| smoke-HIL attached | partial | Tracked PASS log exists, but for commit `8661a38...`, not current HEAD. |
| full HIL attached | fail | No ALERT pin, fault, soak, accuracy, S2 hardware, or full matrix evidence. |

## Final Recommendations

Do not release yet.

For merge status, `main` already contains the hardening branch. Treat current
`main` as an unreleased integration state. Before any release or
industrial-grade claim, fix the P0 blockers: version/migration metadata, live
ESP-IDF CI, ALERT encode/vector proof, and stale HIL status docs. Then rerun the
smoke-HIL at the final release-candidate commit.

## Proposed Follow-Up Prompts

1. Release/version/changelog cleanup:

```text
Prepare the SHT3x repository for a SemVer release without changing runtime
behavior. Decide whether the current unreleased changes require 2.0.0, update
library.json, idf_component.yml, generated Version.h, Doxyfile, README release
wording, and CHANGELOG with a clear Breaking Changes / Migration section.
Do not tag until CI and HIL evidence gates pass.
```

2. ALERT physical-pin HIL:

```text
Run ALERT physical-pin HIL on the SHT3x target. Use GPIO or logic-analyzer
capture, configure safe thresholds, prove ALERT assertion/deassertion,
read status via status_restore while periodic mode is active, clear status
explicitly, restore safe limits, and save the transcript plus target template
under docs/hil/.
```

3. Fault-injection HIL:

```text
Design and run SHT3x fault-injection HIL for address NACK, timeout, unplug/replug,
stuck bus or bus-reset callback, and read/write CRC corruption using a safe jig,
interposer, emulator, or test transport. Record exact Status codes, health
transitions, recovery behavior, and final restored state.
```

4. Soak HIL:

```text
Run SHT3x soak HIL after the default smoke sequence passes. Execute a bounded
stress/periodic logging plan with stable power, known ambient assumptions,
defined stop conditions, final drv/settings/status evidence, and no hidden
manual recovery.
```

5. ESP-IDF CI confirmation:

```text
Debug the failing pure ESP-IDF GitHub Actions jobs for current main. Use the
GitHub run for ba71d1a, inspect esp32s3 and esp32s2 idf-example-build logs,
fix only the build/CI issue, and prove a passing CI run before claiming ESP-IDF
validation.
```

6. Code/doc fixes discovered:

```text
Address the industrial-readiness audit findings without broad refactors:
add alert-limit vendor-vector tests and fix or justify encodeAlertLimit(),
document or guard raw command helpers during periodic/ART, clarify end(),
recover(), tick(), reset side effects, and callback boundedness, then rerun
native tests, Arduino builds, and HIL smoke.
```
