# SHT3x Driver Library

Deterministic SHT3x (SHT30/SHT31/SHT35) I2C driver for ESP32 (Arduino/PlatformIO and ESP-IDF component use).

## Features

- **Injected I2C transport** - no Wire dependency in library code
- **Framework-neutral core** - Arduino and ESP-IDF integration live behind callbacks/adapters
- **CRC validation** - all 16-bit data words CRC-checked
- **Alert mode support** - read/write limits, encode/decode helpers
- **Periodic + ART modes** - 0.5/1/2/4/10 mps and ART
- **Health monitoring** - automatic state tracking (READY/DEGRADED/OFFLINE)
- **Deterministic behavior** - no unbounded loops, no heap allocations

## Current State

Branch `hardening/sht3x-industry-readiness` is software-hardened and
pre-HIL-ready. Local software checks have passed for native tests (70/70),
guard scripts, and Arduino PlatformIO builds for ESP32-S3 and ESP32-S2.

Pure ESP-IDF S2/S3 jobs are configured in CI, but local `idf.py` was unavailable
in this shell. Do not claim pure ESP-IDF validation without a real passing CI log
or local ESP-IDF build log.

Hardware validation has not run. ALERT pin behavior, humidity accuracy,
fault-injection, and soak evidence remain pending and every unexecuted hardware
row stays `Not run`. Current branch changes remain under `[Unreleased]`;
`library.json` is still `1.5.0`, and the current branch head is not a release
tag.

Next step: execute the HIL runbook and log template, or the optional host-side
serial HIL runner, on real ESP32-S2/S3 plus SHT3x hardware and attach the
resulting evidence.

## Installation

### PlatformIO (recommended)

Add to `platformio.ini`:

```ini
lib_deps = 
  https://github.com/janhavelka/SHT3x.git
```

### Manual

Copy `include/SHT3x/` and `src/` to your project.

### ESP-IDF Component

The repository root can be used as an ESP-IDF component through
`EXTRA_COMPONENT_DIRS` or your component manager workflow. The core driver owns
no I2C bus, pins, reset GPIO, logging, or scheduler policy; applications provide
transport and timing callbacks through `SHT3x::Config`.

The core component does not include Arduino or ESP-IDF framework headers.
Applications must inject `Config::nowMs`, `Config::nowUs`, and
`Config::cooperativeYield` so command spacing, reset delays, and timeout
deadlines follow the application scheduler.

See `examples/idf/basic` for a native ESP-IDF 5.4+ `i2c_master` diagnostic
adapter and an equivalent interactive CLI command surface. This example is for
bring-up and protocol diagnostics, not a production task architecture.

## Quick Start

```cpp
#include <Wire.h>
#include "SHT3x/SHT3x.h"

// Transport callbacks
static SHT3x::Status mapWireError(uint8_t result, const char* msg) {
  // Arduino Wire error codes are core-dependent. The mapping below matches ESP32 Arduino core.
  // If your core does not distinguish bus/timeout reliably, map unknown codes to I2C_ERROR
  // and carry the raw code in Status::detail.
  switch (result) {
    case 0: return SHT3x::Status::Ok();
    case 1: return SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM, "Write too long", result);
    case 2: return SHT3x::Status::Error(SHT3x::Err::I2C_NACK_ADDR, msg, result);
    case 3: return SHT3x::Status::Error(SHT3x::Err::I2C_NACK_DATA, msg, result);
    case 4: return SHT3x::Status::Error(SHT3x::Err::I2C_BUS, msg, result);
    case 5: return SHT3x::Status::Error(SHT3x::Err::I2C_TIMEOUT, msg, result);
    default: return SHT3x::Status::Error(SHT3x::Err::I2C_ERROR, msg, result);
  }
}

SHT3x::Status i2cWrite(uint8_t addr, const uint8_t* data, size_t len,
                       uint32_t timeoutMs, void* user) {
  (void)timeoutMs;  // Manager-owned in shared buses
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return SHT3x::Status::Error(SHT3x::Err::INVALID_CONFIG, "Wire instance is null");
  }
  wire->beginTransmission(addr);
  wire->write(data, len);
  uint8_t result = wire->endTransmission(true);
  return mapWireError(result, "Write failed");
}

SHT3x::Status i2cWriteRead(uint8_t addr, const uint8_t* tx, size_t txLen,
                           uint8_t* rx, size_t rxLen, uint32_t timeoutMs, void* user) {
  if (txLen > 0) {
    return SHT3x::Status::Error(SHT3x::Err::INVALID_PARAM,
                                "Combined write+read not supported");
  }
  if (rxLen == 0) {
    return SHT3x::Status::Ok();
  }
  (void)timeoutMs;  // Manager-owned in shared buses
  TwoWire* wire = static_cast<TwoWire*>(user);
  if (wire == nullptr) {
    return SHT3x::Status::Error(SHT3x::Err::INVALID_CONFIG, "Wire instance is null");
  }
  size_t received = wire->requestFrom(addr, rxLen);
  if (received != rxLen) {
    if (received == 0) {
      return SHT3x::Status::Error(SHT3x::Err::I2C_ERROR, "Read returned 0 bytes",
                                  static_cast<int32_t>(received));
    }
    for (size_t i = 0; i < received; i++) {
      (void)wire->read();
    }
    return SHT3x::Status::Error(SHT3x::Err::I2C_ERROR, "Read failed");
  }
  for (size_t i = 0; i < rxLen; i++) {
    rx[i] = wire->read();
  }
  return SHT3x::Status::Ok();
}

SHT3x::SHT3x device;

uint32_t appNowMs(void*) { return millis(); }
uint32_t appNowUs(void*) { return micros(); }
void appYield(void*) { yield(); }

void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9);  // SDA, SCL
  Wire.setClock(400000);
  Wire.setTimeOut(50);

  SHT3x::Config cfg;
  cfg.i2cWrite = i2cWrite;
  cfg.i2cWriteRead = i2cWriteRead;
  cfg.i2cUser = &Wire;
  cfg.nowMs = appNowMs;
  cfg.nowUs = appNowUs;
  cfg.cooperativeYield = appYield;
  cfg.i2cAddress = 0x44;
  cfg.transportCapabilities = SHT3x::TransportCapability::NONE;

  auto status = device.begin(cfg);
  if (!status.ok()) {
    Serial.printf("Init failed: %s\n", status.msg);
    return;
  }

  Serial.println("Device initialized!");
}

void loop() {
  device.tick(millis());

  // Example: request a single-shot measurement
  static bool pending = false;
  if (!pending) {
    if (device.requestMeasurement().code == SHT3x::Err::IN_PROGRESS) {
      pending = true;
    }
  }

  if (pending && device.measurementReady()) {
    SHT3x::Measurement m;
    if (device.getMeasurement(m).ok()) {
      Serial.printf("T=%.2f C, RH=%.2f %%\n", m.temperatureC, m.humidityPct);
    }
    pending = false;
  }
}
```

## API Overview

### Lifecycle

| Method | Description |
|--------|-------------|
| `begin(config)` | Initialize the driver, validate transport callbacks, and probe the sensor. |
| `tick(nowMs)` | Advance bounded timing gates and recovery state. |
| `end()` | End the current session and return to `UNINIT`. |
| `isInitialized()` | Return `true` after a successful `begin()` until `end()`. |
| `getConfig()` | Return the cached configuration currently held by the driver. |
| `probe()` | Raw presence check with no health tracking. |
| `recover()` | Manual recovery ladder that restores communications but leaves the driver in idle single-shot mode. |
| `resetToDefaults()` / `resetAndRestore()` | Reset-only or reset-plus-RAM-restore maintenance helpers. |

### Measurement and Scheduling

| Method | Description |
|--------|-------------|
| `requestMeasurement()` | Start a single-shot measurement or schedule the next periodic fetch. |
| `measurementReady()` | Report whether a sample is ready to be read. |
| `getMeasurement()` / `getRawSample()` / `getCompensatedSample()` | Read float, raw, or fixed-point sample data. |
| `hasSample()` | True after at least one raw/converted sample has been cached. |
| `sampleTimestampMs()` / `sampleAgeMs(nowMs)` | Cached sample timestamp helpers. |
| `missedSamplesEstimate()` | Best-effort estimate of skipped periodic samples. |
| `estimateMeasurementTimeMs()` | Return the current single-shot timing estimate from repeatability settings. |

`begin()` requires `Config::nowMs`, `Config::nowUs`, and
`Config::cooperativeYield`. Without those callbacks the driver cannot enforce
the SHT3x tIDLE command spacing or reset delays deterministically, so startup
fails with `INVALID_CONFIG`.

### Configuration and Diagnostics

| Method | Description |
|--------|-------------|
| `setMode()` / `getMode()` | Switch between single-shot, periodic, and ART modes. |
| `setRepeatability()`, `setPeriodicRate()`, `setClockStretching()` | Update core measurement policy. |
| `driverState()` | Cross-library alias for the current `DriverState`. |
| `getSettings()` / `readSettings()` | Read cached state only, or cached state plus a best-effort status-register read; snapshots include `hasSample`. |
| `writeCommand()` / `writeCommandWithData()` / `readCommand()` | Advanced command-level helpers for upper layers that need direct access to the SHT3x command set. |
| `readStatus()` / `readStatusWithModeRestore()` / `clearStatus()` / `readHeaterStatus()` | Status-register, ALERT-cause, and heater helpers. |
| `readSerialNumber()` | Read the electronic identification code. |
| `readAlertLimit*()` / `writeAlertLimit*()` / `disableAlerts()` | Physical and raw alert-threshold access. |

The low-level command helpers are intentionally narrow: they reuse the driver's tracked
transport path and tIDLE guard, but the dedicated mode/status/alert helpers remain the
preferred entry points when the command has higher-level state implications. Raw reads are
bounded to the largest documented SHT3x response (6 bytes), and invalid read buffers are
rejected before sending the command.

`getRawSample()` and `getCompensatedSample()` remain available after
`getMeasurement()` consumes the current `measurementReady()` event. Use
`hasSample()` or `SettingsSnapshot::hasSample` to check whether those cached
sample helpers can return data.

When repeatability or periodic rate changes require restarting an active periodic/ART mode,
the cached configuration is updated only after that restart succeeds.

### Status Semantics

- `Status::is(Err)` checks whether the status matches a specific error code.
- `Status::operator bool()` returns `true` for success, usable in `if (st)` idiom.
- `Status::inProgress()` is the convenience check for `Err::IN_PROGRESS`.
- `Err::CONVERSION_NOT_READY` is provided as an alias of `Err::MEASUREMENT_NOT_READY` for cross-library CLI/reporting uniformity.
- Expected periodic not-ready handling does not count as a failure. Validation errors and pre-`begin()` setup problems do not transition the driver into `DEGRADED` or `OFFLINE`.

### ALERT and Status Register

SHT3x ALERT mode is associated with periodic acquisition. ALERT cause is exposed
through the status register bits (`alertPending`, `rhAlert`, and `tAlert`).
The Sensirion alert application note also shows ALERT can be active after
power-up/reset until the measured value and programmed limits settle; validate
this on your hardware before relying on ALERT as a production interrupt source.

`readStatus()` is non-destructive and does not clear status flags. Local
Sensirion docs only explicitly allow Fetch Data while periodic/ART acquisition
is active, so `readStatus()` returns `BUSY` in active periodic/ART mode instead
of issuing an undocumented command sequence.

Use `readStatusWithModeRestore()` when ALERT diagnosis is needed while
periodic/ART mode is active. It sends Break, reads the status register, then
attempts to restore the previous periodic or ART mode. This aborts the current
conversion cadence and restarts timing from the restore point. Inspect
`StatusReadSnapshot::stopStatus`, `statusReadStatus`, and `restoreStatus` if the
helper returns an error.

`readSettings()` remains non-disruptive. If it cannot read status, it sets
`statusValid=false` and records the exact reason in
`SettingsSnapshot::statusReadStatus`.

`clearStatus()` is destructive for status flags 15, 11, 10, and 4. It is never
called implicitly by `readStatus()`, `readStatusWithModeRestore()`, or
`readSettings()`.

Alert-limit read/write commands are not documented as valid during active
periodic/ART acquisition. Configure alert limits before starting periodic/ART,
or stop and explicitly restart acquisition around alert-limit changes.
See `docs/SHT3X_ALERT_STATUS_FIX_REPORT.md` for the helper design and local
validation coverage; real ALERT-pin and humidity-threshold behavior still needs
hardware validation.

## Transport Contract (Required)

Your I2C callbacks **must** return specific `Err` codes so the driver can make correct decisions:

- `Err::I2C_NACK_ADDR` - address NACK
- `Err::I2C_NACK_DATA` - data NACK
- `Err::I2C_NACK_READ` - read header NACK (used for "not ready" semantics)
- `Err::I2C_TIMEOUT` - timeout
- `Err::I2C_BUS` - bus/arbitration error
- `Err::I2C_ERROR` - unspecified I2C failure

If your transport cannot distinguish these cases, return `Err::I2C_ERROR` and set `Status::detail` to the best available code.

Transport capabilities must be declared in `Config::transportCapabilities`:

- `READ_HEADER_NACK` - reliably reports read-header NACK
- `TIMEOUT` - reliably reports timeouts
- `BUS_ERROR` - reliably reports bus/arbitration errors

`Err::I2C_NACK_READ` is only valid when the transport can **prove** a read-header NACK
and declares `TransportCapability::READ_HEADER_NACK`.

For Arduino Wire adapters, set `transportCapabilities = TransportCapability::NONE` (best effort).
The driver only calls `i2cWriteRead()` with `txLen == 0` and expects a standalone read
after a prior command write (tIDLE enforced by the driver). Do not implement combined
write+read with repeated-start for SHT3x flows.
With Wire, a 0-byte `requestFrom()` must be treated as an ambiguous error
(return `Err::I2C_ERROR`), not a read-header NACK.
Wire cannot prove read-header NACK, so expected-NACK semantics are disabled.
`timeoutMs` passed to callbacks is a requested bound; in a managed bus the I2CManager
owns the actual Wire timeout and may ignore per-call changes.
I2CManager owns Wire clock/timeout configuration; library code must not mutate global
Wire settings.

## Expected NACK Semantics

Only **read-header NACK** (`Err::I2C_NACK_READ`) is treated as "measurement not ready,"
and only on periodic Fetch Data reads **when** `transportCapabilities` includes
`READ_HEADER_NACK`. All other I2C errors are treated as failures and update health counters.

Use `Config::notReadyTimeoutMs` to bound how long repeated "not ready" NACKs are tolerated
before being treated as a fault.
Use `Config::periodicFetchMarginMs` to avoid early Fetch Data reads
(0 = auto, max(2ms, period/20)).

## Blocking Behavior (Max Time)

All public APIs are synchronous and bounded by config timeouts:

- I2C write/read phases: `i2cTimeoutMs`
- Command spacing gate: `commandDelayMs + i2cTimeoutMs` (guarded)
- Reset/Break waits: `RESET_DELAY_MS` (2 ms), `BREAK_DELAY_MS` (1 ms), both guarded

The driver has no unbounded loops.

### Public API Transaction and Latency Summary

Timing below excludes application-level bus arbitration outside the callback.
Each listed command observes the configured tIDLE spacing before the next SHT3x
command or read phase.

| API | Sensor transactions | Bounded wait behavior | Notes |
|-----|---------------------|-----------------------|-------|
| `begin()` | Best-effort Break + soft reset, status read, optional periodic/ART start | Break 1 ms, reset 2 ms, each I2C phase <= `i2cTimeoutMs` | Missing callbacks/config are rejected before I2C. |
| `requestMeasurement()` single-shot | 1 command write | command spacing + write timeout; sample readiness is later driven by `tick()` | Returns `IN_PROGRESS`; it does not block for conversion. |
| `requestMeasurement()` periodic/ART | 0 transactions | none | Schedules next Fetch Data for `tick()`. |
| `tick()` single-shot pending | 1 receive-only read when ready | command spacing + read timeout | CRC-checks temperature and humidity words. |
| `tick()` periodic/ART pending | Fetch Data command + receive-only read when ready | command spacing + write/read timeout | Read-header NACK is "not ready" only if the transport can prove it. |
| `getMeasurement()` / cached sample getters | 0 transactions | none | Reads cached data only. |
| `setMode(SINGLE_SHOT)` / `stopPeriodic()` | Break command if periodic/ART active | command spacing + write timeout + 1 ms break wait | No sensor command when already idle. |
| `startPeriodic()` / `startArt()` | Optional Break, then start command | command spacing + write timeout, plus break wait if needed | Updates cached desired settings only after success. |
| `setRepeatability()` / `setPeriodicRate()` | 0 when idle; restart sequence when active | bounded by `startPeriodic()` / `startArt()` when active | Active restart failures leave the last fully applied cache intact. |
| `setClockStretching()` | 0 transactions | none | Applies to single-shot and serial-number command selection only. |
| `readStatus()` / `readHeaterStatus()` | Status command + receive-only read | command spacing + write/read timeout | Returns `BUSY` during active periodic/ART. |
| `readStatusWithModeRestore()` | Break, status read, periodic/ART restart | bounded multi-step sequence | Interrupts cadence; inspect step statuses on failure. |
| `clearStatus()` | Clear-status command | command spacing + write timeout | Destructive for status flags 15, 11, 10, and 4. |
| `setHeater()` | Heater command | command spacing + write timeout | Blocked while periodic/ART is active. Heater can affect measurements by self-heating. |
| `readSerialNumber()` | Serial command + receive-only 6-byte read | command spacing + write/read timeout | CRC-checks both serial words. |
| `readAlertLimit*()` | Alert-limit read command + receive-only 3-byte read | command spacing + write/read timeout | Blocked while periodic/ART is active. |
| `writeAlertLimit*()` | Alert-limit write command + status read | command spacing + write/read timeout | Writes append CRC and then check command/checksum status bits. |
| `disableAlerts()` | 2 alert-limit writes, each followed by status read | bounded by two `writeAlertLimitRaw()` calls | Partial success is possible; returned status identifies the failing step. |
| `writeCommand()` / `writeCommandWithData()` | 1 command write | command spacing + write timeout | Advanced direct access; normal helpers are preferred. |
| `readCommand()` | Command write + receive-only read | command spacing + write/read timeout | Read length is capped to documented SHT3x frame sizes. |
| `probe()` | Status command + read via raw transport | command spacing + write/read timeout | Diagnostic only; does not update health. |
| `softReset()` / `generalCallReset()` | Reset command/write | command spacing + write timeout + 2 ms reset wait | General-call reset is bus-wide and opt-in. |
| `interfaceReset()` | Application callback only | bounded by callback contract | Callback must implement the SCL sequence and return a `Status`. |
| `recover()` / reset-and-restore helpers | Multi-step recovery/reset ladder | bounded by configured ladder steps and backoff | Manual recovery only; no automatic retry loop in `tick()`. |

## Recovery Ladder

`recover()` performs a configurable ladder and restores **comms only**:

1. Interface/bus reset (`busReset` callback), then probe
2. Soft reset, then probe
3. Hard reset (`hardReset` callback), then probe
4. General call reset (only if `allowGeneralCallReset` is `true`)

Recovery uses `recoverBackoffMs` to avoid bus thrashing and does **not** run automatically inside `tick()`--the orchestrator triggers it.

`OFFLINE` is latched. Normal public I2C operations return `BUSY` with `Driver is offline; call recover()` and do not touch the bus until `recover()` succeeds. `probe()` remains raw/diagnostic and does not update health counters.

After a successful `recover()`, the driver is left in **SINGLE_SHOT** idle mode with measurement state cleared. If you need periodic/ART/heater, re-apply those settings in your orchestrator.
General-call reset is bus-wide, resets every supporting device on the bus, and
is disabled unless `allowGeneralCallReset` is set. Treat it as a bus-manager or
application decision, not a sensor-driver default.

Two explicit reset APIs are available:

- `resetToDefaults()` resets the sensor and clears cached settings to library defaults (no restore).
- `resetAndRestore()` resets the sensor and restores cached settings from RAM (no NVM on SHT3x).
SHT3x settings are volatile; restore uses RAM cache only.

Cached settings (RAM only):
- mode (SINGLE_SHOT / PERIODIC / ART)
- repeatability
- periodicRate
- clockStretching
- heaterEnabled
- alert limits (raw, per register)

Example usage:
```cpp
sensor.setRepeatability(SHT3x::Repeatability::MEDIUM_REPEATABILITY);
sensor.setHeater(true);
sensor.writeAlertLimitRaw(SHT3x::AlertLimitKind::HIGH_SET, 0x2222);
sensor.startPeriodic(SHT3x::PeriodicRate::MPS_2,
                     SHT3x::Repeatability::MEDIUM_REPEATABILITY);

// On fault:
sensor.resetToDefaults();   // app must reconfigure
// or
sensor.resetAndRestore();   // restores cached settings
```

## Thread-Safety / ISR-Safety

The driver is **not** thread-safe and must be externally serialized. Do not call any public API from an ISR; use interrupts only to signal work to normal task/loop context.

## ESP32 Notes

- Default is **no clock stretching**. If you enable stretching, set `i2cTimeoutMs` >=
  worst-case tMEAS (15.5 ms at low-Vdd, high repeatability) + margin.
- Clock stretching affects only single-shot measurement commands and
  serial-number reads. Periodic/ART modes use their own command families and
  Fetch Data readout.
- On ESP32 Wire, a 0-byte `requestFrom` is **ambiguous** (could be not-ready or bus fault).
  Treat it as an error unless your transport can prove read-header NACK.
- Use `Wire.setTimeOut()` and keep bus pull-ups and clock speed within datasheet limits.
- Transport callbacks must not recursively call public APIs on the same driver
  instance. Serialize shared buses and multi-task access outside the driver.

## Running Tests

Host tests (requires a native compiler like `g++`):

```
pio test -e native
```

Firmware build (ESP32-S3 example):

```
pio run -e esp32s3dev
pio run -e esp32s2dev
```

Native ESP-IDF example build (requires an ESP-IDF 5.4+ shell):

```
python tools/check_idf_example_contract.py
idf.py -C examples/idf/basic set-target esp32s3 build
idf.py -C examples/idf/basic set-target esp32s2 build
```

## Health Monitoring

The driver tracks I2C communication health:

```cpp
// Check state
if (device.state() == SHT3x::DriverState::OFFLINE) {
  Serial.println("Device offline!");
  device.recover();  // Try to reconnect
}

// Get statistics
Serial.printf("Failures: %u consecutive, %lu total\n",
              device.consecutiveFailures(), device.totalFailures());
Serial.printf("Last bus activity: %lu ms\n",
              static_cast<unsigned long>(device.lastBusActivityMs()));
```

## Settings Snapshot

Use `getSettings()` for a cached snapshot or `readSettings()` to also attempt a status-register read:

```cpp
SHT3x::SettingsSnapshot snap;
if (device.readSettings(snap).ok()) {
  Serial.printf("Mode: %d, Heater: %d\n",
                static_cast<int>(snap.mode),
                snap.statusValid ? static_cast<int>(snap.status.heaterOn) : -1);
  if (!snap.statusValid) {
    Serial.printf("Status unavailable: %d\n",
                  static_cast<int>(snap.statusReadStatus.code));
  }
}
```

For ALERT diagnosis while periodic/ART acquisition is active:

```cpp
SHT3x::StatusReadSnapshot status;
SHT3x::Status st = device.readStatusWithModeRestore(status);
if (status.statusValid) {
  Serial.printf("ALERT=%d RH=%d T=%d raw=0x%04X\n",
                status.status.alertPending ? 1 : 0,
                status.status.rhAlert ? 1 : 0,
                status.status.tAlert ? 1 : 0,
                status.status.raw);
}
if (!st.ok()) {
  Serial.printf("status=%d restore=%d\n",
                static_cast<int>(status.statusReadStatus.code),
                static_cast<int>(status.restoreStatus.code));
}
```

## Examples

- `01_basic_bringup_cli/` - Arduino diagnostic bring-up CLI for protocol and board testing
- `idf/basic/` - native ESP-IDF diagnostic bring-up CLI using the `i2c_master` driver

The Arduino bringup CLI covers the full driver surface, including mode control,
serial-number readout, alert-limit helpers, recovery/reset flows, cached
settings snapshots, direct command helpers (`command write`,
`command write_data`, `command read`), and stress/self-test commands. The
ESP-IDF example uses a separate native fixed-buffer command loop with the same
driver scenarios, native `i2c_master` ownership, ESP-IDF logging, FreeRTOS
timing, and no Arduino compatibility facades in the IDF build path.

Both examples are diagnostic/bring-up CLIs. They are useful for proving wiring,
I2C transport behavior, SHT3x protocol handling, and command parity. A
production application should provide its own task ownership, bus serialization,
configuration storage, telemetry, and recovery policy.

## Hardware Validation

Software tests and CI do not prove electrical behavior, board layout, fixture
quality, or sensor accuracy. Maintain real hardware results in
`docs/HARDWARE_VALIDATION.md` before making production claims.

Minimum hardware coverage:

| Area | Required evidence |
|------|-------------------|
| ESP-IDF build and smoke | ESP32-S2 and ESP32-S3 board, ESP-IDF version, bus speed, address, log, date. |
| Addressing | `0x44` and `0x45` probe/read behavior. |
| Measurement modes | Single-shot no-stretch, optional stretch, periodic rates, ART, Fetch Data, Break. |
| ALERT/status | Alert-limit write/read, ALERT pin observation, `readStatusWithModeRestore()` while periodic/ART is active, explicit `clearStatus()`. |
| Resets/recovery | Soft reset, interface reset callback, optional hard reset, opt-in general-call reset on an isolated bus. |
| Fault handling | Timeout/NACK injection, CRC mismatch injection, offline/recovery transition. |
| Humidity fixture | Reference sensor, settling time, coupling method, local gradients, reflow-offset allowance, MSA/Cpk or equivalent production evidence. |

Sensirion's ambient-test guidance treats ambient production testing as a
throughput compromise. Fixture coupling, temperature/humidity gradients,
settling time, airflow, prestaging, product housings, and reflow-related RH
offset can dominate the result. Temperature validation should be stabilized
first because relative humidity is temperature-dependent.

## Documentation

- `CHANGELOG.md` - full release history
- `docs/README.md` - documentation index and authoritative-document map
- `docs/SHT3x_datasheet.pdf` - Sensirion device datasheet
- `docs/SHT3x_HT_AN_AlertMode.pdf` - Sensirion alert-mode application note
- `docs/Sensirion_Humidity_Sensors_Testing_at_Ambient_Conditions.pdf` - ambient humidity production-test guidance
- `docs/Sensirion_electronic_identification_code_SHT3x.pdf` - serial-number / EIC reference
- `docs/SHT3x_driver_extraction.md` - driver split and extraction notes

## License

MIT License. See [LICENSE](LICENSE).
