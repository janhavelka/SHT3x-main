# README Transport Fix Report

## Findings
- The Quick Start Wire transport example used a combined write+read (repeated-start) with no idle gap, which can violate SHT3x tIDLE timing.
- Periodic fetch could be attempted early; with Wire (no READ_HEADER_NACK capability), an early 0-byte read is ambiguous and can be treated as a hard error.

## Fix Strategy Chosen
Option 2: keep `i2cWriteRead`, but enforce read-only usage for SHT3x flows (txLen==0). The driver always writes the command first using `i2cWrite`, enforces tIDLE, then performs a standalone read.

## Changes Applied (files/functions)
- `include/SHT3x/Config.h`: clarified `i2cWriteRead` as read-only for SHT3x; `periodicFetchMarginMs` default set to auto (0 = max(2ms, period/20)).
- `src/SHT3x.cpp`: added `_periodicFetchMarginMs`, `_periodicReadyMs`, `_periodicRetryMs`; `requestMeasurement()` schedules with margin; `tick()` backs off to the next periodic window; `_i2cWriteReadRaw()` rejects combined write+read.
- `examples/common/I2cTransport.h`: Wire adapter now enforces read-only `i2cWriteRead` (txLen>0 returns INVALID_PARAM).
- `README.md`: Quick Start transport example updated; transport contract and ESP32 notes clarified; periodic fetch margin documented.
- `docs/AUDIT.md`: periodic backoff description updated to match new behavior.

## Behavioral Contract Going Forward
- SHT3x reads are two-phase: command write via `i2cWrite` (STOP), tIDLE enforced by the driver, then `i2cWriteRead` with `txLen==0`.
- Combined write+read with repeated-start is not allowed for SHT3x flows; the driver will reject it.
- Periodic/ART fetch is time-gated to `nextSampleDue + periodicFetchMarginMs`, and retries align to the periodic interval.

## Tests Added/Updated
- `test/test_basic.cpp`: `test_read_paths_no_combined_and_respect_delay` verifies no combined write+read and that read occurs after command delay.
- `test/test_basic.cpp`: `test_periodic_fetch_margin_blocks_early_fetch` ensures early tick does not touch the bus and that margin scheduling is applied.
- `test/test_basic.cpp`: config defaults updated for `periodicFetchMarginMs`.

## README Excerpts (updated)
- Transport contract now states `i2cWriteRead()` is read-only for SHT3x (txLen==0) and combined write+read is not allowed.
- ESP32 notes now state that a 0-byte `requestFrom` on Wire is ambiguous and must not be assumed as a read-header NACK.
