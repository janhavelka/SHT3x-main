# SHT3x Engineering Guidelines

Binding conventions for changes to this repository. They apply to human and
automated contributors alike. Workflow, commit format, and the release
procedure live in [CONTRIBUTING.md](CONTRIBUTING.md).

## Target And Goals

- Framework-neutral C++17 core, delivered as a PlatformIO library and an
  ESP-IDF component.
- CI builds ESP32-S2 / ESP32-S3 under both Arduino and native ESP-IDF; the core
  itself has no framework dependency. Hardware coverage is a separate claim and
  is tracked only in [docs/hardware.md](docs/hardware.md).
- Goals: deterministic behavior, long-term stability, clean API contracts,
  portability, no surprises in the field.

---

## Repository Model (Single Library)

```
include/SHT3x/    Public API headers only (Doxygen-documented)
  CommandTable.h    Command words and bit masks
  Config.h          Config struct, callback typedefs, enums
  Status.h          Err codes and Status
  SHT3x.h           Driver class
  Version.h         Auto-generated from library.json - do not edit
src/              Implementation
test/             Native Unity test suite plus Arduino/Wire stubs
examples/
  01_basic_bringup_cli/   Arduino diagnostic CLI
  common/                 Example-only helpers: BoardConfig.h, I2cTransport.h,
                          I2cScanner.h, TransferStats.h, Sht3xCli.h/.cpp
  idf/basic/              Native ESP-IDF diagnostic CLI
docs/             Maintained guides and vendor reference material
tools/            Repository contract gates and the host-side HIL runner
scripts/          generate_version.py, pio.cmd (Windows PlatformIO wrapper)
.github/          CI workflow
platformio.ini    Arduino/native build environments
library.json      PlatformIO manifest and the single source of version truth
idf_component.yml ESP-IDF component manifest (generated version field)
CMakeLists.txt    ESP-IDF component registration
Doxyfile          Strict API documentation build
```

Rules:
- `examples/common/` is NOT part of the library. It simulates project glue and keeps examples self-contained.
- No board-specific pins/bus in library code; only in `Config`.
- Public headers only in `include/SHT3x/`.
- Examples demonstrate usage and may use `examples/common/BoardConfig.h`.
- Keep the layout boring and predictable.

---

## Core Engineering Rules (Mandatory)

- Prefer simplicity, clarity, correctness, robustness, safety, and readability over clever abstractions or speculative flexibility.
- Before coding, inspect whether existing code can be simplified, reused, or deleted.
- Prefer deleting unnecessary code over adding new code.
- Prefer extending existing owners, modules, APIs, and contracts over creating parallel abstractions.
- Add a new service, class, file, interface, manager, registry, or abstraction only for a concrete current need with a clear caller or test.
- Do not add placeholder classes, future stubs, empty managers, broad frameworks, plugin systems, registries, generic layers, or speculative extension points unless the current task explicitly requires them.
- Deterministic: no unbounded loops/waits; all timeouts via deadlines, never `delay()` in library code.
- Owner-safe lifecycle: zero-I2C `bind(const Config&)`, zero-I2C request/cancel,
  one-callback `pollJob(nowMs, budget, result)`, and local `end()`. `begin()` is
  retained only as a bounded synchronous compatibility convenience.
- Owner-safe I/O that can exceed ~1-2 ms must be split into state-machine steps
  driven by `pollJob()`/`tick()`. Retained synchronous compatibility and
  maintenance helpers must publish finite callback/wait bounds.
- No heap allocation in steady state (no `String`, `std::vector`, `new` in normal ops).
- Avoid dynamic allocation, unbounded queues, and variable-size buffers in steady embedded paths unless already accepted locally and the bound is explicit.
- Every hardware operation that can block must have a timeout and an observable failure path.
- Recovery logic must be bounded, deterministic, and testable.
- Prefer explicit state, explicit ownership, and small local helpers over hidden global state.
- Do not hide hardware failures behind silent retries or fake success.
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

---

## I2C Manager + Transport (Required)

- The library MUST NOT own I2C. It never touches `Wire` directly.
- The I2C bus must have one clear owner outside the chip driver.
- Device drivers must not directly own or reconfigure a shared bus unless this repository's architecture explicitly says so.
- `Config` MUST accept a transport adapter (function pointers or abstract interface).
- Transport errors MUST map to `Status` (no leaking `Wire`, `esp_err_t`, etc.).
- I2C transactions must be timeout-bounded and report errors clearly.
- The library MUST NOT configure bus timeouts or pins.
- Keep chip-level protocol code inside the driver/wrapper. Keep application policy outside the chip driver.
- Do not add fake devices, simulated buses, or test doubles to production paths.
- Do not implement a chip protocol manually when an existing hardened project library already provides the needed timeout, recovery, and testability behavior.

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
  - Helper to disable alerts by setting the low set point above the high set
    point, which is the vendor-documented deactivation rule
- Serial number (EIC) readout (both stretch and no-stretch commands).
- Measurement time calculation based on repeatability and VDD range.

---

## Driver Architecture: Owner-Safe Cooperative Jobs

The production surface follows a **passive binding plus cooperative job** model:

- `bind()` validates and stores injected callbacks without I2C or waits. It does
  not prove presence or hardware state.
- `requestMeasurement()` and `requestEnsureIdle()` only schedule fixed-memory
  state and return `IN_PROGRESS`; they perform zero I2C.
- `pollJob()` accepts a caller budget, performs at most one transport callback
  per call, exposes zero-I2C wait phases, and returns request identity, phase,
  outcome, and partial/indeterminate effect.
- `cancelJob()` is zero-I2C and returns the active job's terminal identity once.
- `tick()` is a compatibility wrapper around one-instruction polling and drops
  detailed result provenance; external owners use `pollJob()` directly.
- Bounded synchronous mode/status/heater/alert/reset helpers remain for callers
  that explicitly accept their documented callback/wait bounds. They reject an
  active cooperative job and share the same transport/cache rules.
- Recovery remains caller-controlled. External owners normally use staged
  `requestEnsureIdle()`; synchronous `recover()` is a maintenance convenience.

### DriverState (4 states only)

```cpp
enum class DriverState : uint8_t {
  UNINIT,    // bind()/begin() not called or end() called
  READY,     // Operational, consecutiveFailures == 0
  DEGRADED,  // 1 <= consecutiveFailures < offlineThreshold
  OFFLINE    // consecutiveFailures >= offlineThreshold
};
```

State transitions:
- `bind()`/`begin()` success -> READY (local admission/health state, not proof of presence)
- Any completed logical I2C failure in READY -> DEGRADED
- Success in DEGRADED/OFFLINE -> READY
- Failures reach `offlineThreshold` -> OFFLINE
- `end()` -> UNINIT

### Transport Wrapper Architecture

All I2C goes through layered wrappers:

```
Public API (pollJob, readStatus, setHeater, readSerialNumber, ...)
    -> _completeLogicalOperation() after CRC/status validation
Command helpers (_writeCommand / _readAfterCommand)
    ->
TRACKED wrappers (_i2cWriteTracked, _i2cWriteReadTracked, ...)
    -> _updateHealth() called here ONLY
RAW wrappers (_i2cWriteRaw, _i2cWriteReadRaw)
    ->
Transport callbacks (Config::i2cWrite, i2cWriteRead)
```

**Rules:**
- `_updateHealth()` is called ONLY from tracked transport wrappers. It records
  transport facts (`_transportSuccess` / `_transportFailures`, bus activity) and
  delegates the logical verdict to `_completeLogicalOperation()`.
- `_completeLogicalOperation()` is the single owner of `_totalSuccess`,
  `_totalFailures`, `_consecutiveFailures`, `_lastOkMs`, `_lastErrorMs`,
  `_lastError` and `DriverState`. It runs exactly once per logical operation.
- A multi-callback operation (command + read, or command + status verification)
  passes `logicalComplete = false` to its intermediate callbacks. The public API
  calls `_completeLogicalOperation()` itself once the CRC and the sensor's status
  bits have been checked, so a frame that fails validation is a logical failure
  even though its transport callback succeeded.
- Public API methods NEVER call `_updateHealth()` directly.
- `probe()` uses RAW wrappers -> no health tracking (diagnostic only).
- `recover()` tracks probe failures (driver is initialized, so failures count).

### Health Tracking Rules

- Logical completion happens after protocol validation, never before it. CRC and
  sensor command-rejection failures are logical failures that increment
  `_protocolFailures` and never `_transportFailures`.
- State transitions guarded by `_initialized` (no DEGRADED/OFFLINE before
  `bind()`/`begin()` succeeds).
- NOT recorded for config/param validation errors (INVALID_CONFIG,
  INVALID_PARAM) returned by an admitted callback, nor for precondition errors
  (NOT_INITIALIZED), admission rejections, cancellation, or caller deadlines.
- `probe()` uses raw I2C and does NOT update logical, transport, protocol, or
  state health (diagnostic only).

### Health Tracking Fields

- `_lastOkMs` - timestamp of last successful complete logical transport operation
- `_lastErrorMs` - timestamp of last failed tracked logical transport operation
- `_lastError` - most recent error Status
- `_consecutiveFailures` - failures since last success (resets on success)
- `_totalFailures` / `_totalSuccess` - session logical-operation counters
- `_transportFailures` / `_transportSuccess` - physical callback counters
- `_protocolFailures` - CRC/checksum and sensor command-rejection counter
- `_totalNotReady` - proven periodic read-header NACK counter
- `_totalInferredNotReady` - bounded no-data retries inferred from an ambiguous
  read failure when the transport cannot prove a read-header NACK
- All counters saturate at their integer maximum; they never wrap.

---

## Versioning and Releases

Single source of truth: `library.json`. `Version.h` is auto-generated and must never be edited.

SemVer:
- MAJOR: breaking API/Config/enum changes.
- MINOR: new backward-compatible features or error codes (append only).
- PATCH: bug fixes, refactors, docs.

`scripts/generate_version.py` propagates the version from `library.json` into
`Version.h`, `idf_component.yml`, and `Doxyfile`; `generate_version.py check`
enforces it in CI. Prose that repeats the version string is not generated and
should be avoided.

The release procedure is in [CONTRIBUTING.md](CONTRIBUTING.md); do not duplicate
it here.

---

## Naming Conventions

- Member variables: `_camelCase`
- Methods/Functions: `camelCase`
- Constants: `CAPS_CASE`
- Enum values: `CAPS_CASE` or `X1`, `X2` for short forms
- Locals/params: `camelCase`
- Config fields: `camelCase`
