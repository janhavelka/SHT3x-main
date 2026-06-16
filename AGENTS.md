# AGENTS.md - SHT3x Production Embedded Guidelines

## Role and Target
You are a professional embedded software engineer building a production-grade SHT3x (SHT30/SHT31/SHT35) environmental sensor library.

- Target: ESP32-S2 / ESP32-S3, Arduino framework, PlatformIO.
- Goals: deterministic behavior, long-term stability, clean API contracts, portability, no surprises in the field.
- These rules are binding.

---

## Repository Model (Single Library)

```
include/SHT3x/         - Public API headers only (Doxygen)
  CommandTable.h        - Command definitions and bit masks
  Status.h
  Config.h
  SHT3x.h
  Version.h             - Auto-generated (do not edit)
src/                    - Implementation (.cpp)
examples/
  01_*/
  common/               - Example-only helpers (Log.h, BoardConfig.h, I2cTransport.h,
                          I2cScanner.h, CommandHandler.h)
platformio.ini
library.json
README.md
CHANGELOG.md
AGENTS.md
```

Rules:
- `examples/common/` is NOT part of the library. It simulates project glue and keeps examples self-contained.
- No board-specific pins/bus in library code; only in `Config`.
- Public headers only in `include/SHT3x/`.
- Examples demonstrate usage and may use `examples/common/BoardConfig.h`.
- Keep the layout boring and predictable.

---

## Core Engineering Rules (Mandatory)

- Deterministic: no unbounded loops/waits; all timeouts via deadlines, never `delay()` in library code.
- Non-blocking lifecycle: `Status begin(const Config&)`, `void tick(uint32_t nowMs)`, `void end()`.
- Any I/O that can exceed ~1-2 ms must be split into state machine steps driven by `tick()`.
- No heap allocation in steady state (no `String`, `std::vector`, `new` in normal ops).
- No logging in library code; examples may log.
- No macros for constants; use `static constexpr`. Macros only for conditional compile or logging helpers.
- Core/public headers and `src/` must be framework-neutral: no Arduino or ESP-IDF framework headers unless a rare exception is justified in docs and enforced by tooling.
- Arduino APIs (`Arduino.h`, `Wire.h`, `Serial`, `String`, `TwoWire`) are allowed only in Arduino examples or example-only Arduino adapters.
- ESP-IDF examples must be native IDF examples using `app_main`, `driver/i2c_master.h`, `esp_timer`, FreeRTOS timing, and fixed C buffers or native console APIs.
- ESP-IDF examples must not include Arduino CLI source or use `ArduinoCompat`, `IdfArduinoCompat`, `Arduino.h`, `Wire.h`, `String`, `Serial`, or `TwoWire` facades.
- Preserve Arduino/ESP-IDF CLI parity through a repo-local command contract/checker or a framework-neutral command layer, not by sharing Arduino implementation in IDF builds.

---

# SHT3x hardening rules

- Core library code in `include/` and `src/` must remain framework-neutral: no Arduino, Wire, ESP-IDF, FreeRTOS, logging framework, heap-heavy framework types, or platform timing calls.
- I2C ownership must stay external/injected. The core driver must not own the bus, pins, global `Wire`, IDF bus/device handles, locks, or OS tasks.
- Public fallible APIs must return `Status`; do not add exceptions or hidden fatal behavior.
- Timing hooks (`nowMs`, `nowUs`, `cooperativeYield`) are required for bounded waits; do not silently degrade timing behavior.
- Public APIs are not ISR-safe and the driver instance is not internally thread-safe unless explicitly changed and tested.
- SHT3x status/ALERT behavior must follow the datasheet, especially periodic and ART acquisition modes.
- Do not hide ALERT/status diagnostics behind `statusValid=false` without exposing the reason.
- General-call reset is a bus-manager/application policy, not an automatic shared-bus recovery action.
- Multi-step operations must either be proven rollback-safe or expose/document possible partial hardware state.
- Examples must be labeled honestly as diagnostic, bring-up, or production-style.
- Do not claim hardware validation, ALERT validation, or pure ESP-IDF validation unless the commands/builds actually ran.

## Planned hardening subagents

- `sht3x-datasheet-agent`: Re-check datasheet and local extracted docs for status, ALERT, periodic, ART, and command-validity facts without inferring undocumented behavior.
- `core-contracts-agent`: Inspect public API and core implementation for framework neutrality, timing, health/offline behavior, copy/move semantics, and thread/ISR contracts.
- `tests-agent`: Inspect native tests and fake transports, then identify focused public API and partial-transaction tests.
- `idf-ci-agent`: Inspect ESP-IDF example, component metadata, guard scripts, and CI gaps for pure ESP-IDF validation.

---

## I2C Manager + Transport (Required)

- The library MUST NOT own I2C. It never touches `Wire` directly.
- `Config` MUST accept a transport adapter (function pointers or abstract interface).
- Transport errors MUST map to `Status` (no leaking `Wire`, `esp_err_t`, etc.).
- The library MUST NOT configure bus timeouts or pins.

---

## Status / Error Handling (Mandatory)

All fallible APIs return `Status`:

```cpp
struct Status {
  Err code;
  int32_t detail;
  const char* msg;  // static string only
};
```

- Silent failure is unacceptable.
- No exceptions.

---

## SHT3x Driver Requirements

- I2C address 0x44 (default) or 0x45 (ADDR pin high).
- All commands are 16-bit, MSB-first.
- Always CRC-check each 16-bit data word returned (measurement, status, serial, alert limits).
- When writing a 16-bit data word (alert limits), compute and append CRC; treat checksum errors as failures.
- Enforce minimum command spacing tIDLE >= 1 ms between commands.
- Support measurement modes:
  - Single-shot (with and without clock stretching)
  - Periodic (0.5/1/2/4/10 mps)
  - ART mode (accelerated response time)
  - Fetch Data readout for periodic modes
  - Break command to stop periodic mode
- Implement resets:
  - Soft reset
  - Interface reset sequence (SCL toggle) via callback
  - Optional general call reset (bus-wide)
- Status register support:
  - Read status + CRC
  - Clear status flags
  - Parse alert, heater, reset, command, and checksum bits
- Heater enable/disable and status inspection.
- Alert mode support:
  - Read/write all four alert limits
  - Encode/decode limit words (RH7/T9 packing)
  - Helper to disable alerts (LowSet > HighSet)
- Serial number (EIC) readout (both stretch and no-stretch commands).
- Measurement time calculation based on repeatability and VDD range.

---

## Driver Architecture: Managed Synchronous Driver

The driver follows a **managed synchronous** model with health tracking:

- All public I2C operations are **blocking** (no async state machine needed - SHT3x has no EEPROM writes).
- `tick()` may be used for periodic polling or measurement-ready checks.
- Health is tracked via **tracked transport wrappers** ? public API never calls `_updateHealth()` directly.
- Recovery is **manual** via `recover()` - the application controls retry strategy.

### DriverState (4 states only)

```cpp
enum class DriverState : uint8_t {
  UNINIT,    // begin() not called or end() called
  READY,     // Operational, consecutiveFailures == 0
  DEGRADED,  // 1 <= consecutiveFailures < offlineThreshold
  OFFLINE    // consecutiveFailures >= offlineThreshold
};
```

State transitions:
- `begin()` success -> READY
- Any I2C failure in READY -> DEGRADED
- Success in DEGRADED/OFFLINE -> READY
- Failures reach `offlineThreshold` -> OFFLINE
- `end()` -> UNINIT

### Transport Wrapper Architecture

All I2C goes through layered wrappers:

```
Public API (readMeasurement, setMode, etc.)
    ?
Command helpers (writeCommand/readAfterCommand)
    ?
TRACKED wrappers (_i2cWriteReadTracked, _i2cWriteTracked)
    ?  ? _updateHealth() called here ONLY
RAW wrappers (_i2cWriteReadRaw, _i2cWriteRaw)
    ?
Transport callbacks (Config::i2cWrite, i2cWriteRead)
```

**Rules:**
- Public API methods NEVER call `_updateHealth()` directly
- Command helpers use TRACKED wrappers -> health updated automatically
- `probe()` uses RAW wrappers -> no health tracking (diagnostic only)
- `recover()` tracks probe failures (driver is initialized, so failures count)

### Health Tracking Rules

- `_updateHealth()` called ONLY inside tracked transport wrappers.
- State transitions guarded by `_initialized` (no DEGRADED/OFFLINE before `begin()` succeeds).
- NOT called for config/param validation errors (INVALID_CONFIG, INVALID_PARAM).
- NOT called for precondition errors (NOT_INITIALIZED).
- `probe()` uses raw I2C and does NOT update health (diagnostic only).

### Health Tracking Fields

- `_lastOkMs` - timestamp of last successful I2C operation
- `_lastErrorMs` - timestamp of last failed I2C operation
- `_lastError` - most recent error Status
- `_consecutiveFailures` - failures since last success (resets on success)
- `_totalFailures` / `_totalSuccess` - lifetime counters (wrap at max)

---

## Versioning and Releases

Single source of truth: `library.json`. `Version.h` is auto-generated and must never be edited.

SemVer:
- MAJOR: breaking API/Config/enum changes.
- MINOR: new backward-compatible features or error codes (append only).
- PATCH: bug fixes, refactors, docs.

Release steps:
1. Update `library.json`.
2. Update `CHANGELOG.md` (Added/Changed/Fixed/Removed).
3. Update `README.md` if API or examples changed.
4. Commit and tag: `Release vX.Y.Z`.

---

## Naming Conventions

- Member variables: `_camelCase`
- Methods/Functions: `camelCase`
- Constants: `CAPS_CASE`
- Enum values: `CAPS_CASE` or `X1`, `X2` for short forms
- Locals/params: `camelCase`
- Config fields: `camelCase`
