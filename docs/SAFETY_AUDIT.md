# SHT3x User Safety Audit
Date: 2026-02-01
Version: 1.0.0 (library.json)

## Goal
Ensure public API usage cannot crash, hard-hang, or stall the MCU. This audit focuses on blocking paths and unbounded waits.

## Safety properties already present
- All public I2C operations return Status and propagate errors
- No heap allocation in steady state
- No logging inside library code
- Transport is caller-owned; the driver does not touch Wire directly
- Measurement is non-blocking via requestMeasurement() + tick()

## Findings and fixes

1) High: Unbounded busy-wait loops in command spacing and wait helpers
- Functions: _ensureCommandDelay(), _waitMs()
- Risk: If micros()/millis() stop advancing (timer stalled or interrupts disabled), loops could spin forever and stall the MCU.
- Fix implemented:
  - Added timeout guard based on commandDelayMs/ delayMs and i2cTimeoutMs
  - Added spin guard that exits if millis() does not advance for too long
- Result: waits are bounded and return Err::TIMEOUT instead of stalling

2) Medium: General call reset bypassed health tracking
- Function: generalCallReset()
- Risk: I2C failure during general call reset did not update health counters, masking bus issues.
- Fix implemented: generalCallReset() now uses tracked I2C wrapper to update health state.

3) Medium: Periodic Fetch Data NACK treated as I2C error
- Function: _fetchPeriodic()
- Risk: Fetching too early can NACK; this is normal but was treated as I2C_ERROR and could degrade driver health or go OFFLINE.
- Fix implemented:
  - Added a tracked read path that maps read-no-data (0 bytes) to Err::MEASUREMENT_NOT_READY without health penalty
  - tick() now backs off by commandDelayMs on MEASUREMENT_NOT_READY to avoid tight retry loops
- Notes:
  - The no-data mapping uses the transport detail == 0 convention (Wire returns 0 bytes on NACK)

4) Low: Interface reset does not clear internal measurement state
- Function: interfaceReset()
- Risk: After bus recovery, cached state may be stale and next measurement might be inconsistent.
- Fix implemented:
  - Clears measurement pending flags and ready timestamp
  - Resets last fetch time and re-anchors periodic start time if periodic mode is active

5) Low: Callback behavior can still block longer than timeout
- Functions: i2cWrite / i2cWriteRead callbacks (Config)
- Risk: If user callbacks block, public APIs will block. This cannot be enforced inside the driver.
- Mitigation:
  - Use Wire timeouts and return Err::I2C_ERROR on timeout
  - Do not call public APIs from ISR context

## Summary
The main stall risk has been fixed by bounding internal busy waits. Remaining risks are operational or depend on user-supplied transport behavior. The library now meets the user-safety target as long as callbacks respect timeouts.
