# SHT3x API Contract Cleanup Report

Date: 2026-06-01
Branch: `hardening/sht3x-release-readiness-gaps`

## Scope

This chunk clarifies public API side-effect contracts identified by
`docs/SHT3X_INDUSTRIAL_READINESS_EXPLORATION.md`.

No physical ALERT pin validation, humidity accuracy validation, fault injection,
soak test, full HIL, release metadata change, or CI workflow fix was run or
claimed in this chunk.

## Raw Command Decision

Decision: keep `writeCommand()`, `writeCommandWithData()`, and `readCommand()`
available as expert escape hatches.

Behavior was not changed. The helpers already use the tracked transport path,
respect tIDLE spacing, reject pending single-shot work, validate raw read
buffers, and cap raw reads to the largest documented SHT3x response frame. They
do not guard active periodic/ART state.

Contract now documented:

- raw helpers bypass high-level periodic/ART mode safety,
- caller owns datasheet command legality,
- caller owns raw response length and CRC interpretation,
- caller owns status side effects, local cache coherence, and desynchronization
  risk,
- stop periodic/ART before raw commands unless the datasheet explicitly permits
  the command in that active mode.

## Reset, End, And Tick Wording Changes

- `end()`
  - Documented as local-only.
  - It clears runtime/session state and returns to `UNINIT`.
  - It does not clear cached configuration, cached restore plan, health
    counters, or last error.
  - It does not send Break or reset and does not guarantee physical
    periodic/ART acquisition stopped.
  - Applications should call `stopPeriodic()` before `end()` when hardware
    acquisition state matters.

- `softReset()`
  - Documented as blocked during active periodic/ART.
  - Documented that a pending single-shot conversion is not preserved on reset
    success.
  - Documented that reset success clears pending measurement/sample state,
    leaves local mode as `SINGLE_SHOT`, and does not rewrite the restore cache.

- `generalCallReset()`
  - Documented as bus-wide and dangerous on shared buses.
  - Documented as disabled by default and guarded by
    `Config::allowGeneralCallReset`.
  - Documented that `recover()` uses it only when that explicit opt-in is set.
  - Documented that reset success clears local measurement state and leaves
    local mode as `SINGLE_SHOT`.

- `tick()`
  - Documented that it can perform a bounded synchronous I2C transaction.
  - Documented that it returns `void`.
  - Documented that transport failures are observed through `lastError()`,
    health counters, `driverState()`, and retry scheduling.
  - Documented that CRC/protocol failures after a successful bus transaction do
    not update transport health; they leave no ready sample and are retried
    through normal scheduling.
  - Recommended explicit APIs when the caller needs a synchronous returned
    `Status`.

## Status And Mode-Restore Wording Changes

- `Status::operator bool()`
  - Documented as true only for `Err::OK`.
  - Documented that `Err::IN_PROGRESS` converts to false.
  - `Status::inProgress()` remains the explicit check for scheduled work.

- `readStatusWithModeRestore()`
  - Clarified that `statusValid` is true only when the status-read step
    succeeds.
  - Clarified `modeInterrupted`, `finalMode`, `restored`, and the step status
    fields used to diagnose partial stop/read/restore outcomes.
  - Clarified that if status read and restore both fail, the top-level return
    reports `restoreStatus` and the earlier status failure remains in
    `statusReadStatus`.

- `readSettings()`
  - Clarified that periodic/ART status unavailability is reported in a returned
    OK snapshot with `statusValid=false`.
  - Clarified that pending single-shot status unavailability follows the same OK
    snapshot behavior.
  - Clarified that OFFLINE returns `BUSY` like other normal public I2C APIs.

## Behavior Changes

None.

Runtime behavior did not change.

## Tests Added

- Added a native contract test proving the low-level command helpers
  intentionally bypass the active periodic/ART guard, still send the requested
  raw command, and do not update driver periodic mode state. This pins the
  expert escape-hatch contract while leaving high-level helper behavior
  unchanged.

## Files Changed

- `include/SHT3x/Status.h`
- `include/SHT3x/SHT3x.h`
- `README.md`
- `test/test_basic.cpp`
- `docs/IDF_PORT.md`
- `docs/SHT3X_CORE_CONTRACTS_PARTIAL_STATE_REPORT.md`
- `docs/SHT3x_driver_extraction.md`
- `docs/SHT3X_API_CONTRACT_CLEANUP_REPORT.md`

## Validation Commands

| Command | Result |
| --- | --- |
| `python tools/check_core_timing_guard.py` | PASS |
| `python tools/check_cli_contract.py` | PASS |
| `python tools/check_idf_example_contract.py` | PASS |
| `python scripts/generate_version.py check` | PASS, `Version.h` up to date |
| `python -m platformio test -e native` | PASS, 76/76 native tests |
| `python -m platformio run -e esp32s3dev` | PASS |
| `python -m platformio run -e esp32s2dev` | PASS |

## Remaining API Caveats

- The raw command helpers remain powerful and intentionally unsafe if used with
  undocumented command sequences.
- `end()` remains local-only by design.
- `resetToDefaults()` and `resetAndRestore()` run the recovery ladder; they do
  not guarantee a physical sensor reset if the initial recovery probe succeeds.
- `tick()` remains a void polling method; callers that need direct error
  handling must use explicit public APIs.
- General-call reset remains an application or bus-manager policy decision.
