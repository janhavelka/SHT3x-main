# SHT3x-main I2C Uniformization Prompt

Repository: `SHT3x-main`

Absolute path: `C:\Users\Honza\Documents\Projects\SHT3x-main`

## Execution Rules

You are working inside this single repository. Implement this prompt directly;
do not repeat the cross-repository audit.

You may spawn subagents for read-only inspection of APIs, tests, I2C
transactions, docs, and diagnostics. Keep final judgment, edits, and
verification in the main agent.

Prefer simple, robust, readable code. Before adding code, inspect whether
existing code can be simplified, reused, tightened, or deleted.

Preserve dirty user changes. Do not commit unless explicitly asked.

## Common Uniformization Target

Apply this shared I2C library contract: injected non-owning transport, `Status` returns, cache-only `getSettings(SettingsSnapshot&) const`, active `probe()`/diagnostics named explicitly, `DriverState` with `state()` and `driverState()`, `isOnline()`, `lastOkMs()`, `lastErrorMs()`, `lastError()`, `consecutiveFailures()`, `totalFailures()`, and `totalSuccess()`.

Keep the common `Err` vocabulary append-only where missing: `OK`, `NOT_INITIALIZED`, `INVALID_CONFIG`, `INVALID_PARAM`, `I2C_ERROR`, `I2C_NACK_ADDR`, `I2C_NACK_DATA`, `I2C_TIMEOUT`, `I2C_BUS`, `DEVICE_NOT_FOUND`, `TIMEOUT`, `BUSY`, and `IN_PROGRESS`. Preserve SHT3x-specific CRC, command, status-restore, alert, measurement, and recovery-ladder behavior.

Uniformization is not a new base class or framework. Make only local, source-compatible additions and tests.

## Current State

- Public lifecycle and health are in `include\SHT3x\SHT3x.h`: `DriverState` at line 15, `SettingsSnapshot` at line 61, `begin()` at line 150, `probe()` at line 198, `recover()` at line 207, `driverState()` at line 230, and health counters at lines 246-268.
- `getSettings(SettingsSnapshot&)` is cache-only at line 340; active refresh is `readSettings(SettingsSnapshot&)` at line 358.
- Diagnostic command helpers are explicit: `writeCommand()` at line 372, `writeCommandWithData()` at line 384, `readCommand()` at line 400, and `readStatusWithModeRestore()` at line 471.
- Recovery policy is configurable through `include\SHT3x\Config.h:185-187`.
- HIL runner exists as `tools\run_i2c_hil.py`; parser tests exist as `tools\test_run_i2c_hil_parser.py`, but `python -m pytest tools/test_run_i2c_hil_parser.py` could not run because `pytest` is not installed locally.
- The repository currently has a dirty worktree; preserve it.

## Best Sources To Adapt

- Keep SHT3x as the source implementation for command-oriented I2C diagnostics, mode-restore status reads, and cache-only vs active status separation.
- For low-dependency parser tests, adapt BME280's stdlib `unittest` runner style: `BME280\tools\test_run_i2c_hil_parser.py`.
- For display/visual HIL classification, do not copy SSD1315; SHT3x has non-visual sensor evidence.

## Implementation Tasks

1. Preserve the current core API. No health/state renaming is needed.
2. Make `tools\test_run_i2c_hil_parser.py` runnable in a clean local Python environment. Preferred fix: convert it to stdlib `unittest` or add a small `__main__` harness that does not require pytest. Alternative: declare pytest as an explicit dev/test dependency and document it.
3. Keep `probe()` raw/no-health and keep `readSettings()`/`readStatusWithModeRestore()` as active diagnostic APIs.
4. Ensure HIL runner docs and parser classification continue to cover the common minimum `version`, `scan`, `probe`, `settings`, `health`, failure-token classification, and dry-run/parser test contract; reject `OFFLINE`, `DEGRADED`, CRC, timeout, and I2C NACK tokens even when an expected text token is present.
5. Keep recovery ladder bounded and observable. Do not add hidden retries inside ordinary read/write calls.

## API Changes Required

- None expected.

## Simplifications Before Adding Code

- Prefer making the existing parser test dependency-free over adding another HIL test file.

## Tests To Add Or Update

- Host parser test must run with a documented command. Target: `python tools\test_run_i2c_hil_parser.py` or an explicit pytest dependency.
- Maintain native tests for recovery backoff, mode restore, and end-state clearing.

## Commands To Run

- `pio test -e native`
- `pio run -e esp32s3dev`
- `python tools\check_hil_contract.py`
- Parser test command chosen by task 2.
- Live HIL only with `python tools\run_i2c_hil.py --port <PORT> --expect-address 0x44` or `0x45`, matching wiring.

## Constraints And Non-Goals

- Do not add register-map abstractions; SHT3x is command/CRC based.
- Do not issue general-call reset unless the existing opt-in policy allows it.
- Injected transport only: no global `Wire`, new bus manager, pin ownership, or shared bus reset from the device driver.
- Preserve distinct timeout, address NACK/device-not-found, data NACK, read NACK, bus, CRC, command, alert, and status-restore errors. Do not collapse them into generic `I2C_ERROR` or use `DEVICE_NOT_FOUND` for timeout/data/bus failures.

## Risks And Open Questions

- Open: whether the repo wants pytest as a declared tool dependency or wants all host helper tests stdlib-only.
