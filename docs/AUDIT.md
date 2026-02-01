# SHT3x Library Audit
Date: 2026-02-01
Version: 1.0.0 (library.json)

## Scope
- Target: ESP32-S2 / ESP32-S3, Arduino framework, PlatformIO
- Public API headers: include/SHT3x/*.h
- Implementation: src/SHT3x.cpp
- Examples: examples/01_basic_bringup_cli and examples/common
- Transport: user-provided I2C callbacks via Config

## High level architecture
- Single driver class: SHT3x::SHT3x
- Managed synchronous design: all public I2C operations are blocking
- Non-blocking measurement lifecycle: requestMeasurement() + tick() + getMeasurement()
- Health tracking is layered below public APIs through tracked I2C wrappers

## Lifecycle and call flow
### begin(const Config&)
- Resets internal state and health counters
- Validates config: callbacks, timeout, address, enums
- Enforces minimum command spacing (tIDLE >= 1 ms)
- Probes device by reading status (raw, untracked)
- Applies initial mode:
  - SINGLE_SHOT: no additional command
  - PERIODIC: _enterPeriodic(rate, repeatability, art=false)
  - ART: _enterPeriodic(rate, repeatability, art=true)
- Sets state to READY on success

### tick(uint32_t nowMs)
- Returns immediately if not initialized or no pending measurement
- SINGLE_SHOT:
  - Waits until measurementReadyMs
  - Reads measurement (tracked I2C)
  - Converts and latches data, clears pending flag
- PERIODIC / ART:
  - Waits until measurementReadyMs
  - Fetches data (tracked I2C) via Fetch Data command
  - Converts and latches data, clears pending flag

### requestMeasurement()
- SINGLE_SHOT:
  - Sends single-shot command
  - Sets measurementReadyMs = now + estimateMeasurementTimeMs()
  - Returns IN_PROGRESS
- PERIODIC / ART:
  - Validates periodic active
  - Schedules next fetch window using periodic interval
  - Returns IN_PROGRESS

### getMeasurement(Measurement&)
- Requires measurementReady == true
- Returns float temperature and humidity
- Clears measurementReady on success

### startPeriodic(rate, repeatability)
- Validates state, stops prior periodic if needed
- Sends periodic command based on rate and repeatability
- Sets _periodicActive, _periodicStartMs, _periodMs

### startArt()
- Sends ART command (4 Hz periodic mode)
- Sets _periodicActive and mode to ART

### stopPeriodic()
- Sends Break command and waits BREAK_DELAY_MS
- Clears periodic state and returns to SINGLE_SHOT

### softReset() / generalCallReset() / interfaceReset()
- softReset(): device command, waits RESET_DELAY_MS, clears internal measurement state
- generalCallReset(): writes 0x06 to general-call address, waits RESET_DELAY_MS, clears state
- interfaceReset(): calls busReset callback (no device state change guaranteed)

## Transport and health tracking
I2C operations are layered:
- Public API -> register helpers -> tracked wrappers -> raw wrappers -> transport callbacks
- Only tracked wrappers call _updateHealth()
- Raw wrappers are used for probe() and diagnostics without health updates

Health fields:
- _lastOkMs, _lastErrorMs, _lastError
- _consecutiveFailures, _totalFailures, _totalSuccess
- DriverState: UNINIT, READY, DEGRADED, OFFLINE

## Command coverage mapping
All datasheet commands are implemented. Mapping below shows command to API:

Single shot (stretch):
- 0x2C06 / 0x2C0D / 0x2C10 -> requestMeasurement() with repeatability + stretch

Single shot (no stretch):
- 0x2400 / 0x240B / 0x2416 -> requestMeasurement() with repeatability + no stretch

Periodic measurement:
- 0x2032 0x2024 0x202F (0.5 mps)
- 0x2130 0x2126 0x212D (1 mps)
- 0x2236 0x2220 0x222B (2 mps)
- 0x2334 0x2322 0x2329 (4 mps)
- 0x2737 0x2721 0x272A (10 mps)
- startPeriodic() / startArt() -> _commandForPeriodic()

Fetch data:
- 0xE000 -> _fetchPeriodic()

ART:
- 0x2B32 -> startArt()

Break:
- 0x3093 -> stopPeriodic()

Status:
- 0xF32D -> readStatus()
- 0x3041 -> clearStatus()

Heater:
- 0x306D / 0x3066 -> setHeater(true/false)

Resets:
- 0x30A2 -> softReset()
- general call addr 0x00, byte 0x06 -> generalCallReset()
- interface reset -> interfaceReset() callback

Alert limits:
- Read: 0xE11F 0xE114 0xE109 0xE102 -> readAlertLimitRaw/readAlertLimit
- Write: 0x611D 0x6116 0x610B 0x6100 -> writeAlertLimitRaw/writeAlertLimit

Serial number:
- 0x3780 (stretch) / 0x3682 (no stretch) -> readSerialNumber()

## CRC and conversions
- CRC8: poly 0x31, init 0xFF, no reflect, xor 0x00
- CRC is checked for every 16-bit word read (temp, RH, status, serial, alert)
- CRC is generated for every 16-bit word written (alert limits)

Conversions:
- Temperature C: -45 + 175 * raw / 65535
- Humidity RH%: 100 * raw / 65535
- Fixed point helpers: *_x100 for integer outputs

## Example coverage
The CLI example exposes all public API calls and helpers:
- Lifecycle: begin, end
- Measurement: request, tick, get, raw, compensated, estimate time
- Modes: setMode, startPeriodic, startArt, stopPeriodic
- Config: repeatability, clock stretching, periodic rate
- Status: read, clear, raw status bits
- Heater: set/read
- Resets: soft reset, interface reset, general call reset
- Serial number: read
- Alert: read/write, encode/decode, disable alerts
- Diagnostics: probe, recover, health counters

## Operational notes
- Library code does not own I2C hardware or pins
- All blocking time is bounded by config timeouts and safety guards
- Measurement readiness is handled by requestMeasurement() + tick()
- For periodic mode, user must respect rate and call tick() regularly
