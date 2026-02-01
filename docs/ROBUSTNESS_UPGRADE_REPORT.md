# SHT3x Robustness Upgrade Report
Date: 2026-02-01
Version: 1.1.0

## Summary
This upgrade tightens the I2C transport contract, improves health tracking fidelity, adds measurement staleness tracking, and implements a real recovery ladder with backoff. It also adds host-side tests for critical edge cases and documents timing/error semantics in README and AUDIT.md.

## Changes Applied (by area)

### Transport contract + error taxonomy
- Added explicit I2C error codes: address NACK, data NACK, read-header NACK, timeout, bus error.
  - File: `include/SHT3x/Status.h`
- Documented required callback semantics in Config comments.
  - File: `include/SHT3x/Config.h`
- Example Wire transport updated to map Wire error codes to new Err values.
  - File: `examples/common/I2cTransport.h`

### Expected NACK handling
- “Not ready” is only triggered on `Err::I2C_NACK_READ` during read-only fetches.
- Periodic `Fetch Data` backs off on `MEASUREMENT_NOT_READY` and escalates to failure after `notReadyTimeoutMs`.
  - File: `src/SHT3x.cpp` (`_i2cWriteReadTrackedAllowNoData`, `_fetchPeriodic`, `tick`)

### Health tracking
- Added `lastBusActivityMs` to record any bus interaction (including expected NACKs).
- Pre-init bus activity updates `lastOkMs`/`lastErrorMs` without changing driver state.
  - Files: `include/SHT3x/SHT3x.h`, `src/SHT3x.cpp`

### Measurement lifecycle
- Sample timestamp and `sampleAgeMs()` helper added.
- Missed sample estimate in periodic/ART mode.
  - Files: `include/SHT3x/SHT3x.h`, `src/SHT3x.cpp`

### Recovery ladder
- `recover()` implements: bus reset ? soft reset ? hard reset ? general call reset (opt-in), with backoff.
- Successful recovery re-applies the requested mode.
  - Files: `include/SHT3x/Config.h`, `src/SHT3x.cpp`

### Configuration additions
- `notReadyTimeoutMs`, `recoverBackoffMs`, and optional `hardReset` callback.
  - File: `include/SHT3x/Config.h`

### Tests
- Added unit tests: CRC8, conversions, alert limit pack/unpack, time wrap logic, expected NACK mapping, recovery paths.
  - File: `test/native/test_basic.cpp`
- Native test env added to PlatformIO.
  - File: `platformio.ini`

## Updated Behavioral Contracts
- **Transport contract**: callbacks must return distinct I2C error codes; `I2C_NACK_READ` is the only “expected NACK.”
- **Blocking behavior**: bounded by `i2cTimeoutMs`, command spacing guard, and reset/break delays.
- **Recovery**: explicitly orchestrated via `recover()`; never auto-invoked in `tick()`.
- **Thread-safety**: not thread-safe; external serialization required.

## Documentation Updates
- README expanded with transport contract, expected NACK semantics, recovery ladder, blocking bounds, and ESP32 notes.
- AUDIT.md updated for new health tracking and recovery behavior.

## New/Updated Config Flags
- `notReadyTimeoutMs` (0 disables)
- `recoverBackoffMs`
- `recoverUseBusReset`, `recoverUseSoftReset`, `recoverUseHardReset`
- `hardReset` callback

## Remaining Known Limitations
- Example Wire transport cannot always distinguish NACK vs bus error on read; a production transport should use ESP-IDF I2C APIs or a richer adapter.
- Recovery ladder does not re-apply heater state (not tracked in config).

## Test Coverage Added
- CRC8 example (0xBEEF ? 0x92)
- Alert limit encode/decode round-trip
- Conversion helpers and fixed-point outputs
- Time wrap helper (`_timeElapsed`)
- Expected NACK mapping and not-ready timeout escalation
- Recovery success and failure paths
