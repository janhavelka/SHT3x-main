# SHT3x Driver Robustness Audit Report
Date: 2026-02-01
Version: 1.0.0 (library.json)

## Scope
- Target: ESP32-S2 / ESP32-S3, Arduino framework, PlatformIO
- Files reviewed: include/SHT3x/*.h, src/SHT3x.cpp, examples/01_basic_bringup_cli, examples/common, docs/AUDIT.md, docs/SHT3x_driver_extraction.md

## Summary (what is true today)
- All datasheet commands are implemented and exposed via public API (see command map in docs/AUDIT.md).
- Transport is callback-based; the driver does not touch Wire directly.
- Health tracking is implemented via tracked wrappers and is used by all public I2C operations, except explicit diagnostics and one expected-NACK read path.
- Deterministic busy-wait guards exist for command spacing and fixed delays; they return Err::TIMEOUT instead of stalling.
- Expected NACK for periodic Fetch Data is mapped to Err::MEASUREMENT_NOT_READY without health penalty.
- Recovery ladder is NOT implemented; recover() currently performs a single status read.

## A) Health tracking trustworthiness
### Status
Partially compliant with the auditor requirement.

### What is implemented
- Tracked wrappers update health for all normal public I2C operations.
- General-call reset now uses a tracked wrapper.
- Explicit diagnostic operations bypass health:
  - begin() initial probe uses untracked status read (pre-initialization)
  - probe() uses untracked status read by design

### What is NOT implemented / gaps
- Expected-NACK read in periodic fetch returns MEASUREMENT_NOT_READY without updating health counters (no penalty, but also no success update).
- Health counters do not change during begin() because _updateHealth is gated by _initialized; this is a deliberate design choice from AGENTS.md.

### Recommendation
If strict policy requires *every* bus transaction to update health, add an internal helper that records a success timestamp/counter for expected NACK without incrementing failure, and optionally allow begin() to record health without changing driver state.

### Public API -> Health tracking table
Legend: Yes = updates health on success/failure, No = does not update health, N/A = no I2C

| API | I2C | Health update | Notes |
| --- | --- | --- | --- |
| begin | Yes | No | Initial probe uses untracked read; _initialized false by design |
| tick | Yes | Yes | Reads measurement in SINGLE_SHOT or fetches periodic |
| end | No | N/A | State reset only |
| probe | Yes | No | Explicit diagnostic, no health tracking |
| recover | Yes | Yes | Status read only |
| requestMeasurement | Yes/No | Yes | Single-shot writes command; periodic just schedules |
| getMeasurement | No | N/A | Returns cached sample |
| getRawSample | No | N/A | Returns cached raw sample |
| getCompensatedSample | No | N/A | Returns cached compensated sample |
| setMode | Yes | Yes | May stop/start periodic |
| getMode | No | N/A | Reads cached config |
| setRepeatability | Yes | Yes | May restart periodic depending on mode |
| getRepeatability | No | N/A | Reads cached config |
| setClockStretching | No | N/A | Cache only |
| getClockStretching | No | N/A | Cache only |
| setPeriodicRate | Yes | Yes | May restart periodic depending on mode |
| getPeriodicRate | No | N/A | Cache only |
| startPeriodic | Yes | Yes | Writes periodic command |
| startArt | Yes | Yes | Writes ART command |
| stopPeriodic | Yes | Yes | Writes Break + wait |
| readStatus | Yes | Yes | Command + read |
| clearStatus | Yes | Yes | Command write |
| setHeater | Yes | Yes | Command write |
| readHeaterStatus | Yes | Yes | Delegates to readStatus |
| softReset | Yes | Yes | Command write + wait |
| interfaceReset | No | N/A | Bus reset callback only |
| generalCallReset | Yes | Yes | Tracked write to GC address + wait |
| readSerialNumber | Yes | Yes | Command + read |
| readAlertLimitRaw | Yes | Yes | Command + read |
| readAlertLimit | Yes | Yes | Delegates to readAlertLimitRaw |
| writeAlertLimitRaw | Yes | Yes | Command+data write then read status |
| writeAlertLimit | Yes | Yes | Delegates to writeAlertLimitRaw |
| disableAlerts | Yes | Yes | Writes two alert limits |
| encodeAlertLimit | No | N/A | Pure math helper |
| decodeAlertLimit | No | N/A | Pure math helper |
| convertTemperatureC/HumidityPct | No | N/A | Pure math helper |
| convertTemperatureC_x100/HumidityPct_x100 | No | N/A | Pure math helper |
| estimateMeasurementTimeMs | No | N/A | Pure math helper |

## B) Expected NACK semantics
### Status
Partially implemented.

### What is implemented
- Periodic Fetch Data uses a read path that converts a 0-byte read into Err::MEASUREMENT_NOT_READY without health penalty.
- tick() does not clear the pending measurement on MEASUREMENT_NOT_READY and backs off by commandDelayMs before retry.

### What is NOT implemented / gaps
- There is no configurable bound or escalation for repeated MEASUREMENT_NOT_READY in periodic mode.
- Single-shot no-stretch polling semantics are not implemented; the driver uses fixed tMEAS wait instead.

### State diagrams (simplified)

SINGLE_SHOT:
requestMeasurement -> write command -> set ready time -> IN_PROGRESS
  tick(now >= ready) -> read measurement -> OK -> measurementReady=true
  tick(now < ready) -> no I2C -> pending
getMeasurement -> returns cached sample, clears ready

PERIODIC / ART:
requestMeasurement -> schedule ready time -> IN_PROGRESS
  tick(now < ready) -> no I2C -> pending
  tick(now >= ready) -> write Fetch Data -> read
    if read OK -> measurementReady=true
    if read MEASUREMENT_NOT_READY -> pending, ready time += commandDelayMs
getMeasurement -> returns cached sample, clears ready

## C) ESP32 transport contract (timeouts, clock stretching)
### Status
Documented in examples; needs explicit contract in docs.

### Known ESP32 risks
- Clock stretching can fail if controller timeout is too short.
- Wire.requestFrom() returns 0 bytes on NACK, which is now mapped to MEASUREMENT_NOT_READY for periodic fetch.

### Recommended contract (to document)
- Default clock stretching is disabled (Config default is STRETCH_DISABLED).
- If clock stretching is enabled, set Wire timeout >= worst-case tMEAS + margin.
  - Worst case tMEAS: 15.5 ms (high repeatability, low Vdd)
  - Recommended timeout: >= 20 ms (example uses 50 ms)
- Wire settings in examples: Wire.setClock(400kHz), Wire.setTimeOut(timeoutMs)
- Callbacks must be non-blocking beyond timeout; do not call public APIs from ISRs.

## D) Timing correctness and wraparound safety
### Status
Implemented with bounded waits and wrap-safe comparisons.

- tIDLE enforcement: _ensureCommandDelay gates all commands and reads.
- tMEAS: estimateMeasurementTimeMs uses worst-case values; low-Vdd path is conservative.
- Wraparound: comparisons use signed delta (int32_t) for millis/micros.
- Fixed waits: BREAK_DELAY_MS and RESET_DELAY_MS are applied via _waitMs with timeouts.

## E) Recovery ladder and state transitions
### Status
NOT implemented (gap).

### Current behavior
- recover() performs a single status read; it does not reset or reconfigure the device.

### Required for full compliance
Define and implement a ladder, for example:
1) interfaceReset() if callback exists
2) softReset()
3) generalCallReset() if explicitly enabled
4) re-probe and re-apply current mode (periodic/ART) if configured
5) transition to OFFLINE after offlineThreshold

## F) Interface reset (SCL toggles)
### Status
Callback-only.

- Driver provides Config::busReset callback hook; no built-in GPIO-based reset.
- This is intentional to keep the library board-agnostic (no pin ownership).
- If built-in reset is required, add optional pins to Config and implement GPIO toggle in a portable layer.

## G) Alert mode edge cases
### Status
Implemented in code; documentation gap.

- Alert limit encoding/decoding matches app note (RH7/T9 packing).
- CRC for writes is generated; status bits COMMAND_ERROR and WRITE_CRC_ERROR are checked.
- Documentation should mention ALERT pin behavior after reset and default limit restoration on brown-out.

## H) Thread-safety / orchestration assumptions
### Status
Not explicitly documented.

- Driver is not thread-safe and not ISR-safe; external serialization is required.
- I2C callbacks must be atomic with respect to the bus manager.
- Public APIs are synchronous and can block up to configured timeouts.

## Public API timing and behavior contract (max blocking)
All times are upper bounds in terms of configuration, not absolute CPU time.

- I2C write or read phase: <= i2cTimeoutMs
- Command spacing gate: <= commandDelayMs + i2cTimeoutMs (guarded)
- Fixed waits: BREAK_DELAY_MS (1 ms) and RESET_DELAY_MS (2 ms), both guarded

Approximate worst-case per API:
- begin(): status read (write + read with tIDLE) + optional periodic start
- requestMeasurement(): single-shot write only (tIDLE + write), periodic schedule is non-blocking
- tick():
  - SINGLE_SHOT read: tIDLE + read
  - PERIODIC fetch: tIDLE + write + tIDLE + read
- readStatus/readAlert/readSerial: write + tIDLE + read
- writeAlertLimit: write+data + read status (write + read)
- stopPeriodic: write Break + wait 1 ms
- softReset/generalCallReset: write reset + wait 2 ms
- interfaceReset: callback-defined

## Tests (current state)
- Current host tests are minimal (Status/Config defaults only).
- Missing tests requested by auditor:
  - CRC8 (not directly accessible; would require test hook or friend)
  - Alert limit encode/decode (public static helpers can be tested)
  - Conversion math (public static helpers can be tested)
  - Wraparound logic (can be unit-tested with simulated millis values)
- Integration test: the CLI example includes a stress command and manual recover command, but no automated recovery stress script.

## Fixes applied since docs/AUDIT.md
- Bounded busy-wait guards in _ensureCommandDelay and _waitMs (no unbounded stalls)
- generalCallReset now updates health tracking
- Periodic Fetch Data expected NACK mapped to MEASUREMENT_NOT_READY
- interfaceReset clears pending measurement state

## Remaining risks / open questions
- No recovery ladder implementation; recover() is shallow.
- No escalation for repeated MEASUREMENT_NOT_READY in periodic mode.
- Health tracking does not update for begin() probe or expected-NACK read (deliberate exceptions).
- No built-in GPIO bus reset (callback only).
- Thread-safety contract is not documented in README.

## Recommended next steps (if auditor requires full compliance)
1) Implement recovery ladder in recover() and document state transitions.
2) Add a configurable NOT_READY retry budget for periodic fetch.
3) Add unit tests for conversion and alert packing; add a CRC test hook if required.
4) Add explicit documentation sections to README:
   - API timing and blocking behavior
   - Expected NACK semantics
   - ESP32 I2C timeout requirements
   - Thread-safety and orchestration assumptions
