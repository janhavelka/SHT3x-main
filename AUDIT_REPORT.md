# SHT3x Driver Robustness Audit Report
Date: 2026-02-01
Version: 1.2.0 (library.json)

## Scope
- Target: ESP32-S2 / ESP32-S3, Arduino framework, PlatformIO
- Files reviewed: include/SHT3x/*.h, src/SHT3x.cpp, examples/01_basic_bringup_cli, examples/common, docs/AUDIT.md, docs/SHT3x_driver_extraction.md

## Summary (what is true today)
- All datasheet commands are implemented and exposed via public API (see command map in docs/AUDIT.md).
- Transport is callback-based; the driver does not touch Wire directly.
- Transport contract now includes explicit I2C error taxonomy and capability flags.
- Health tracking is implemented via tracked wrappers and updated for pre-init bus activity.
- Expected NACK handling is **capability-gated** (READ_HEADER_NACK required).
- Periodic not-ready is bounded by notReadyTimeoutMs when enabled.
- Recovery ladder is implemented and is **comms-only** (does not restore mode/heater/alert settings).
- Host tests exist (Unity) and CI runs ESP32 build + native tests.

## A) Health tracking trustworthiness
### Status
Mostly compliant.

### What is implemented
- Tracked wrappers update health for all normal public I2C operations.
- Pre-init bus activity updates lastOk/lastError timestamps without changing driver state.
- lastBusActivityMs records any bus interaction (including expected NACK).
- General-call reset uses tracked wrapper.
- Explicit diagnostic operation probe() bypasses health by design.

### Remaining gaps / design choices
- Expected-NACK reads update lastBusActivityMs but do not increment success/failure counters.
- probe() remains untracked (explicit diagnostic).

### Public API -> Health tracking table
Legend: Yes = updates health on success/failure, No = does not update health, N/A = no I2C

| API | I2C | Health update | Notes |
| --- | --- | --- | --- |
| begin | Yes | Yes (timestamps) | Pre-init activity updates lastOk/lastError only |
| tick | Yes | Yes | Reads measurement or fetches periodic |
| end | No | N/A | State reset only |
| probe | Yes | No | Explicit diagnostic |
| recover | Yes | Yes | Ladder operations tracked |
| requestMeasurement | Yes/No | Yes | Single-shot writes command; periodic schedules |
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
| getSettings | No | N/A | Cached snapshot only |
| readSettings | Yes/No | Yes/No | Attempts status read; returns OK if BUSY |
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
Implemented with capability gating.

### What is implemented
- Read-header NACK is treated as MEASUREMENT_NOT_READY **only** if `transportCapabilities` includes READ_HEADER_NACK.
- Periodic not-ready is time-bounded using notReadyTimeoutMs (optional).
- tick() backs off by commandDelayMs on MEASUREMENT_NOT_READY.

### Current behavior summary
- **Wire (default capabilities NONE):** read-header NACK is not treated as not-ready; ambiguous 0-byte reads are tracked as errors.
- **Rich transports:** expected NACK can be used safely.

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
    if read MEASUREMENT_NOT_READY (capability gated) -> pending, ready time += commandDelayMs
getMeasurement -> returns cached sample, clears ready

## C) ESP32 transport contract (timeouts, clock stretching)
### Status
Documented in README.

### Known ESP32 risks
- Clock stretching can fail if controller timeout is too short.
- Wire.requestFrom() returning 0 bytes is ambiguous; capability gating avoids misclassification.

### Contract summary
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
- Guard behavior validated by unit test (command delay guard).

## E) Recovery ladder and state transitions
### Status
Implemented (comms-only).

### Current behavior
recover() performs a ladder:
1) interfaceReset() if callback exists
2) softReset()
3) hardReset() if callback exists
4) generalCallReset() if explicitly enabled

On success, the driver returns to SINGLE_SHOT idle with measurement state cleared and **does not restore** prior mode/heater/alert limits.

## F) Interface reset (SCL toggles)
### Status
Callback-only.

- Driver provides Config::busReset callback hook; no built-in GPIO-based reset.
- This is intentional to keep the library board-agnostic (no pin ownership).
- If built-in reset is required, add optional pins to Config and implement GPIO toggle in a portable layer.

## G) Alert mode edge cases
### Status
Implemented in code; documentation gap remains.

- Alert limit encoding/decoding matches app note (RH7/T9 packing).
- CRC for writes is generated; status bits COMMAND_ERROR and WRITE_CRC_ERROR are checked.
- Documentation should mention ALERT pin behavior after reset and default limit restoration on brown-out.

## H) Thread-safety / orchestration assumptions
### Status
Documented in README.

- Driver is not thread-safe and not ISR-safe; external serialization is required.
- I2C callbacks must be atomic with respect to the bus manager.
- Public APIs are synchronous and can block up to configured timeouts.

## Public API timing and behavior contract (max blocking)
All times are upper bounds in terms of configuration, not absolute CPU time.

- I2C write or read phase: <= i2cTimeoutMs
- Command spacing gate: <= commandDelayMs + i2cTimeoutMs (guarded)
- Fixed waits: BREAK_DELAY_MS (1 ms) and RESET_DELAY_MS (2 ms), both guarded

Approximate worst-case per API:
- begin(): status read (write + read with tIDLE)
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
- Unity host tests now cover:
  - CRC8 example (0xBEEF → 0x92)
  - Conversion helpers (float + fixed-point)
  - Alert limit encode/decode
  - Wraparound-safe time comparison
  - Command delay guard timeout
  - Expected NACK mapping (with and without capability)
  - Recovery success/failure behavior
- GitHub Actions CI runs:
  - `pio run -e ex_bringup_s3`
  - `pio test -e native`

## Fixes applied since docs/AUDIT.md
- Capability-gated expected NACK handling
- Pre-init health timestamp tracking and lastBusActivityMs
- Periodic notReadyTimeoutMs escalation
- Comms-only recovery ladder with backoff
- Settings snapshot helper (getSettings/readSettings)
- Unity host tests + CI workflow

## Remaining risks / open questions
- Wire’s error reporting remains best-effort; richer transports should set capability flags.
- No built-in GPIO bus reset (callback only).
- Alert mode behavior after reset still under-documented.

## Recommended next steps
1) If you need full restore after recover(), implement it in the orchestrator (not in the driver).
2) Add an optional GPIO-based bus reset helper in examples (board-specific).
3) Expand alert-mode documentation and add a test fixture for limit defaults.
