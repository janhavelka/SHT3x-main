# SHT3x Driver Library

Deterministic SHT3x (SHT30/SHT31/SHT35) I2C driver for ESP32 (Arduino/PlatformIO and ESP-IDF component use).

## Features

- **Injected I2C transport** - no Wire dependency in library code
- **Framework-neutral core** - Arduino and ESP-IDF integration live behind callbacks/adapters
- **CRC validation** - all 16-bit data words CRC-checked
- **Alert mode support** - read/write limits, encode/decode helpers
- **Periodic + ART modes** - 0.5/1/2/4/10 mps and ART
- **Owner-safe jobs** - passive binding, one-transfer polling, deadlines, identity, and zero-I2C cancellation
- **Health monitoring** - logical-operation and transfer diagnostics with selectable admission policy
- **Deterministic behavior** - no unbounded loops, no heap allocations

## Current State

This tree contains the `1.7.0` owner-safe API. Local verification passes the
116-test native fault/boundary suite, strict framework-neutral core compile,
repository guards, and pinned Arduino PlatformIO builds for ESP32-S3 and
ESP32-S2. Hardware evidence remains the historical v1.6.1 evidence described
in the [hardware validation guide](docs/hardware.md).

Pure ESP-IDF S2/S3 jobs are configured in CI, but local `idf.py` was unavailable
in this shell. Do not claim pure ESP-IDF validation without a real passing CI log
or local ESP-IDF build log.

Hardware validation has explicit boundaries. No new physical-hardware run was
performed for v1.7.0. The latest maintained COM20 report
is [the 2026-06-30 COM20 report](docs/reports/hil-validation-COM20-20260629.md):
destructive/all-round ESP32-S3 serial HIL passed all executable commands at
SHT3x address `0x44`, and a post-reboot smoke run passed. The best long attempt
reached 862,912
successful driver operations with zero recorded driver failures before a
host/USB diagnostic CLI timeout. Under the v1.6.1 release criterion, that
timeout is treated as host diagnostic liveness evidence, not a SHT3x core/I2C
failure. ALERT pin behavior, humidity accuracy, fault injection, ESP32-S2
hardware, address `0x45`, and an uninterrupted 16-hour transcript remain
pending. Every unexecuted hardware row stays `Not run`.

Version metadata is `1.7.0` in `library.json`, `idf_component.yml`, Doxyfile,
and generated `include/SHT3x/Version.h`.

Next long HIL run should use the low-USB `i2c_soak <seconds>` firmware command
through `tools/run_i2c_hil.py --include-soak --soak-duration-s <seconds>`.

A full repository checkout also keeps the repository-only
`docs/TUNNELMONITOR_NODE_SUITABILITY_AUDIT.md`. It records the owner-safe
design rationale, completed v1.7.0 findings, and remaining external
integration/hardware gates. It is audit evidence, not a consumer-package
dependency or a claim that TunnelMonitor was modified.

## Installation

### PlatformIO (recommended)

Add to `platformio.ini`:

```ini
lib_deps = 
  https://github.com/janhavelka/SHT3x-main.git
```

Production firmware should exact-pin the reviewed immutable commit rather than
tracking a branch:

```ini
lib_deps =
  https://github.com/janhavelka/SHT3x-main.git#<reviewed-full-commit-sha>
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

## Owner-safe production operation model

Use `bind()` in an external I2C-owner task when lifecycle work must not perform
hidden bus traffic. Binding validates and stores callbacks, normalizes local
configuration, enters local `SINGLE_SHOT` state, and performs zero I2C. It does
not claim that the sensor is present or that acquisition state matches the
local state. `CachedSettings` remains a desired restore plan, never a live
readback of heater/alert configuration. Rebinding returns `BUSY` while a job is
active so identity cannot be silently discarded; cancel and consume its
terminal result first.

```cpp
SHT3x::Config cfg = makeApplicationOwnedConfig();
cfg.healthPolicy = SHT3x::HealthPolicy::OBSERVE_ONLY;
cfg.i2cTimeoutMs = 20;  // The owner/adapter must enforce this callback bound.

SHT3x::Status st = device.bind(cfg);  // zero I2C

SHT3x::JobRequest startup{requestId, nowMs + 1000U, true};
st = device.requestEnsureIdle(startup);  // zero I2C

SHT3x::PollJobResult step;
st = device.pollJob(nowMs, 1, step);     // zero or one transport callback
if (step.terminal) {
  publishOnce(step.requestId, step.outcome, step.phase, step.effect, st);
}
```

`requestEnsureIdle()` is the destructive reconciliation job: Break, a zero-I2C
settle phase, soft reset, another zero-I2C settle phase, then status command and
CRC-checked status read. It uses at most four transport callbacks across polls
and leaves the sensor in verified single-shot idle state on success.

`requestMeasurement(JobRequest)` also performs zero I2C. Each `pollJob()` call
uses at most one callback even if a larger budget is supplied. `maxInstructions
== 0` and all conversion/settle/command-spacing wait phases are bus-silent.
The caller-provided `nowMs` and `Config::nowMs` must be the same wrapping timebase;
absolute deadlines must be within `INT32_MAX` milliseconds of the request.
Both injected clocks are monotonic unsigned scheduler clocks that may wrap modulo
2^32: `nowMs` supplies milliseconds and `nowUs` independently supplies
microseconds for command spacing.

Terminal identity is emitted on exactly one `pollJob()` or `cancelJob()` call.
Cancellation is cooperative between polls: an injected transport callback is
externally timeout-bounded but atomic from the driver's perspective, so the
driver cannot interrupt it after a poll has entered that callback.
The caller must consume that returned `PollJobResult` immediately; it is not an
internal queue. Later polls have `terminal=false` and `requestId=0`, preventing
a stale completion from being attributed to a new request. Cancellation is
local and always performs zero I2C. `JobEffect` distinguishes no known effect,
an unread measurement that may still be pending, a changed device state, and an
indeterminate state after an ambiguous command transfer.

While any cooperative job is active, synchronous/advanced I/O and configuration
mutation APIs return `BUSY`; finish or cancel the job before calling them.

The operation classes are intentionally small:

| Class | APIs | Bound and intended context |
|---|---|---|
| Owner-safe steady state | measurement request + `pollJob()` | Zero or one callback per poll; normal I2C-owner scheduling. |
| Owner-safe reconciliation | `requestEnsureIdle()` + `pollJob()` | Four callbacks maximum, two bus-silent waits, caller deadline/cancel; startup, hotplug, or explicit recovery. |
| Synchronous convenience/configuration | `begin()`, mode/status/heater/alert/reset helpers | Bounded callbacks and waits documented below; use only where the caller accepts that latency. An owner can reconcile first, then issue an idle one-command setting change. |
| Diagnostic/maintenance | raw commands, `probe()`, recovery ladder, general-call reset | Explicit expert/application policy; may invalidate hardware-state verification or affect the shared bus. |

SHT3x has no nonvolatile programming, calibration storage, or endurance-limited
write procedure, so there is no rare NVM operation class in this library.

## API Overview

### Lifecycle

| Method | Description |
|--------|-------------|
| `bind(config)` | Validate/store configuration with zero I2C and no wait; hardware state remains unverified. |
| `begin(config)` | Synchronous compatibility initialization: bind, Break/reset/status CRC/diagnostic validation, and optional acquisition start. |
| `requestEnsureIdle()` / `pollJob()` | Owner-safe destructive reconciliation with identity, deadline, phase, effect, and one-callback polling. |
| `cancelJob()` | Cancel the active job locally with zero I2C and return its terminal result. |
| `tick(nowMs)` | Compatibility one-step poll that discards detailed job results. |
| `end()` | Clear runtime/session state and return to `UNINIT`; no sensor command is sent. |
| `isInitialized()` | Return `true` after a successful `bind()`/`begin()` until `end()`. |
| `getConfig()` | Return the cached configuration currently held by the driver. |
| `probe()` | Raw presence check with no health tracking. |
| `recover()` | Manual recovery ladder that restores communications but leaves the driver in idle single-shot mode. |
| `resetToDefaults()` / `resetAndRestore()` | Reset-only or reset-plus-RAM-restore maintenance helpers. |

### Measurement and Scheduling

| Method | Description |
|--------|-------------|
| `requestMeasurement()` / `requestMeasurement(JobRequest)` | Schedule with zero I2C; the overload carries caller identity/deadline. |
| `pollJob()` | Advance at most one transport callback and return active or exactly-once terminal provenance. |
| `cancelMeasurement()` | Cancel a measurement locally with zero I2C. |
| `measurementReady()` | Report whether a sample is ready to be read. |
| `getMeasurement()` / `getRawSample()` / `getCompensatedSample()` / `getMeasurementMilli()` | Read float, raw, centi-unit, or signed milli-unit sample data. |
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
| `driverState()` / `isOnline()` | Local health/admission diagnostics, not presence proof; I2C admission is selected by `HealthPolicy`. |
| `hardwareStateValid()` | Whether typed reconciliation established a known acquisition baseline since the last raw/ambiguous operation. |
| `getSettings()` / `readSettings()` | Read cached state only, or cached state plus a best-effort status-register read; snapshots expose cache/hardware validity. |
| `writeCommand()` / `writeCommandWithData()` / `readCommand()` | Advanced command-level helpers for upper layers that need direct access to the SHT3x command set. |
| `readStatus()` / `readStatusWithModeRestore()` / `clearStatus()` / `readHeaterStatus()` | Status-register, ALERT-cause, and heater helpers. |
| `readSerialNumber()` | Read the electronic identification code. |
| `readAlertLimit*()` / `writeAlertLimit*()` / `disableAlerts()` | Physical and raw alert-threshold access. |

The low-level command helpers are expert escape hatches. They reuse the
driver's tracked transport path and tIDLE guard, and they reject pending
cooperative jobs, but they intentionally bypass high-level periodic/ART mode
safety. The caller owns datasheet command legality, response length, raw
response CRC interpretation, status side effects, local cache coherence, and
desynchronization risk. Raw helpers do not update mode/heater/alert caches and
invalidate `hardwareStateValid()` after an attempted raw command.
Stop periodic/ART before raw commands unless the datasheet explicitly permits
the command in the active mode. Dedicated mode/status/alert helpers remain the
preferred entry points when the command has higher-level state implications.
Raw reads are bounded to the largest documented SHT3x response (6 bytes), and
invalid read buffers are rejected before sending the command.

`getRawSample()` and `getCompensatedSample()` remain available after
`getMeasurement()` consumes the current `measurementReady()` event. Use
`hasSample()` or `SettingsSnapshot::hasSample` to check whether those cached
sample helpers can return data.

When repeatability or periodic rate changes require restarting an active periodic/ART mode,
the cached configuration is updated only after that restart succeeds.

### Status Semantics

- `Status::is(Err)` checks whether the status matches a specific error code.
- `Status::operator bool()` returns `true` only for `Err::OK`, usable in the
  `if (st)` idiom. `Err::IN_PROGRESS` converts to `false`.
- `Status::inProgress()` is the convenience check for `Err::IN_PROGRESS`.
- `isTransportError()`, `isProtocolError()`, `isExpectedNotReady()`, and
  `isAbsent()` are pure `constexpr` helpers for phase-aware owner mapping.
- `Err::CONVERSION_NOT_READY` is provided as an alias of `Err::MEASUREMENT_NOT_READY` for cross-library CLI/reporting uniformity.
- `Err::CANCELLED` is a local terminal result and never implies an I2C attempt.
- Expected periodic not-ready handling does not count as a failure. Validation errors and pre-bind setup problems do not transition the driver into `DEGRADED` or `OFFLINE`.
- `totalSuccess()`/`totalFailures()` and `consecutiveFailures()` describe complete logical transport operations. `transportSuccess()`/`transportFailures()`, `protocolFailures()`, and `totalNotReady()` keep physical transfer, CRC/checksum or sensor command-rejection, and expected-not-ready diagnostics separate; expected read-header NACKs are excluded from `transportFailures()`, and all counters saturate.

### ALERT and Status Register

SHT3x ALERT mode is associated with periodic acquisition. ALERT cause is exposed
through the status register bits (`alertPending`, `rhAlert`, and `tAlert`).
The Sensirion alert application note also shows ALERT can be active after
power-up/reset until the measured value and programmed limits settle; validate
this on your hardware before relying on ALERT as a production interrupt source.

`readStatus()` is non-destructive and does not clear status flags. It returns
`BUSY` while any cooperative job is active. Local Sensirion docs only
explicitly allow Fetch Data while periodic/ART acquisition is active, so
`readStatus()` also returns `BUSY` in active periodic/ART mode instead of
issuing an undocumented command sequence.

Use `readStatusWithModeRestore()` when ALERT diagnosis is needed while
periodic/ART mode is active. It sends Break, reads the status register, then
attempts to restore the previous periodic or ART mode. This aborts the current
conversion cadence and restarts timing from the restore point. Inspect
`StatusReadSnapshot::stopStatus`, `statusReadStatus`, and `restoreStatus` if the
helper returns an error. `statusValid` is true only when the status-read step
succeeded, `modeInterrupted` reports whether Break was sent, `finalMode` reports
the driver mode after all attempted steps, and `restored` is true only when no
restore was needed or the previous periodic/ART mode was restarted. If both the
status-read step and restore step fail, the top-level return reports the restore
failure; inspect `statusReadStatus` for the earlier status-read failure.

`readSettings()` remains non-disruptive. If it cannot read status, it sets
`statusValid=false` and records the exact reason in
`SettingsSnapshot::statusReadStatus`. That OK snapshot behavior covers active
periodic/ART and any active cooperative job. Under
`LATCH_OFFLINE`, OFFLINE is the exception: `readSettings()` returns `BUSY` and
does not issue a bus transaction. Under `OBSERVE_ONLY`, health remains visible
but never suppresses an owner-authorized attempt.

`clearStatus()` is destructive for status flags 15, 11, 10, and 4. It is never
called implicitly by `readStatus()`, `readStatusWithModeRestore()`, or
`readSettings()`.

Alert-limit read/write commands are not documented as valid during active
periodic/ART acquisition. Configure alert limits before starting periodic/ART,
or stop and explicitly restart acquisition around alert-limit changes.
`encodeAlertLimit()` accepts arguments as `(temperatureC, humidityPct)`. The
four Sensirion app-note reset-default labels, listed as RH/T in the note,
encode to the documented raw words: `80% / 60 C -> 0xCD33`,
`79% / 58 C -> 0xC92D`, `22% / -9 C -> 0x3869`, and
`20% / -10 C -> 0x3466`. Other physical values are quantized into the reduced
RH7/T9 alert-limit format, so decoded values are approximate.
See `docs/reference/sht3x-chip-notes.md` for the preserved alert app-note
vectors and `docs/hardware.md` for the hardware validation boundary; real
ALERT-pin and humidity-threshold behavior still needs hardware validation.

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

## Bounded work and synchronous latency

Owner-safe job APIs do not spin. `bind()`, request, cancellation, cached getters,
and zero-budget/waiting polls perform zero I2C. A non-waiting `pollJob()` performs
at most one callback, bounded by `i2cTimeoutMs`; the adapter remains responsible
for enforcing that bound.

Synchronous convenience/advanced APIs are bounded by:

- I2C write/read phases: `i2cTimeoutMs`
- Command spacing gate: `commandDelayMs + i2cTimeoutMs` (guarded)
- Reset/Break waits: `RESET_DELAY_MS` (2 ms), `BREAK_DELAY_MS` (1 ms), both guarded

Legacy command-spacing/reset waits use deadline and stall guards plus
`cooperativeYield`; they are not used by the owner-safe wait phases. The driver
has no unbounded retry loop and performs no steady-state heap allocation.

### Public API Transaction and Latency Summary

Timing below excludes application-level bus arbitration outside the callback.
Each listed command observes the configured tIDLE spacing before the next SHT3x
command or read phase.

| API | Sensor transactions | Bounded wait behavior | Notes |
|-----|---------------------|-----------------------|-------|
| `bind()` | 0 | None | Passive local binding; `hardwareStateValid()==false`. |
| `requestMeasurement()` / `requestEnsureIdle()` | 0 | None | Only schedules state with nonzero identity for `JobRequest`. |
| `pollJob(..., 0, ...)` or a wait phase | 0 | None | Returns active progress without bus access. |
| `pollJob(..., >=1, ...)` | 0 or 1 | One callback timeout maximum | Never consumes more than one instruction per call; terminal identity is returned once. |
| `cancelJob()` / `cancelMeasurement()` | 0 | None | Local cancellation; effect reports pending/changed/indeterminate hardware state. |
| `requestEnsureIdle()` complete job | 4 maximum across polls | Two bus-silent settle phases | Break, reset, status command, status read; caller deadline/cancel applies. |
| `begin()` | 4, or 5 with periodic/ART start | Break 1 ms + reset 2 ms + command-spacing guards | Synchronous compatibility API: Break, reset, status command/read, optional start. Best-effort startup Break/reset failures are superseded by the verified status result. |
| `getMeasurement()` / cached sample getters | 0 transactions | none | Reads cached data only. |
| `setMode(SINGLE_SHOT)` / `stopPeriodic()` | Break command if periodic/ART active | command spacing + write timeout + 1 ms break wait | No sensor command when already idle. |
| `startPeriodic()` / `startArt()` | Optional Break, then start command | command spacing + write timeout, plus break wait if needed | Updates cached desired settings only after success. |
| `setRepeatability()` / `setPeriodicRate()` | 0 when idle; restart sequence when active | bounded by `startPeriodic()` / `startArt()` when active | Active restart failures leave the last fully applied cache intact. |
| `setClockStretching()` | 0 transactions | none | Applies to single-shot and serial-number command selection only. |
| `readStatus()` / `readHeaterStatus()` | Status command + receive-only read | command spacing + write/read timeout | Returns `BUSY` during any cooperative job or active periodic/ART. |
| `readStatusWithModeRestore()` | Break, status read, periodic/ART restart | bounded multi-step sequence | Interrupts cadence; inspect step statuses on failure. |
| `clearStatus()` | Clear-status command | command spacing + write timeout | Destructive for status flags 15, 11, 10, and 4. |
| `setHeater()` | Heater command | command spacing + write timeout | Blocked while periodic/ART is active. Heater can affect measurements by self-heating. |
| `readSerialNumber()` | Serial command + receive-only 6-byte read | command spacing + write/read timeout | CRC-checks both serial words. |
| `readAlertLimit*()` | Alert-limit read command + receive-only 3-byte read | command spacing + write/read timeout | Blocked while periodic/ART is active. |
| `writeAlertLimit*()` | 3: alert write + status command/read | command spacing + three callback bounds | Writes append CRC and then check command/checksum status bits. Cache changes only after verification. |
| `disableAlerts()` | 6 maximum | bounded by two `writeAlertLimitRaw()` calls | Partial success is possible; returned status identifies the failing call and the per-limit cache exposes confirmed writes. |
| `writeCommand()` / `writeCommandWithData()` | 1 command write | command spacing + write timeout | Advanced direct access; normal helpers are preferred. |
| `readCommand()` | Command write + receive-only read | command spacing + write/read timeout | Read length is capped to documented SHT3x frame sizes. |
| `probe()` | Status command + read via raw transport | command spacing + write/read timeout | Diagnostic only; does not update logical, transport, protocol, or state health. |
| `softReset()` | Soft-reset command | command spacing + write timeout + 2 ms reset wait | Blocked while periodic/ART is active. Success clears pending measurement/sample state and leaves local mode as single-shot. |
| `generalCallReset()` | General-call write to address `0x00` | command spacing + write timeout + 2 ms reset wait | Bus-wide, disabled by default, and an application/bus-manager policy. Success clears local measurement state and leaves local mode as single-shot. |
| `interfaceReset()` | Application callback only | bounded by callback contract | Callback must implement the SCL sequence and return a `Status`; every attempt invalidates verified state and starts tIDLE, even on failure. |
| `recover()` | 2–15 callbacks maximum when every ladder option is enabled (at most 13 I2C callbacks plus interface/hard-reset callbacks) | Each enabled reset wait is bounded; no retry loop | A probe can short-circuit only when hardware state was already verified idle. Unknown state requires Break+soft reset or a hard/general-call reset plus validated status; interface reset alone proves only communication. Use `requestEnsureIdle()` for owner-safe startup/reconciliation. |
| `resetToDefaults()` / `resetAndRestore()` | Recovery bound; restore adds at most 14 I2C callbacks | Same finite ladder plus fixed restore plan (heater + up to four three-callback alert writes + optional acquisition start) | Maintenance convenience APIs; partial restore is reported and invalidates verified hardware state. |

`tick()` returns `void`; use `pollJob()` for exact failure, phase, identity, and
effect at the call site. CRC failure is counted separately from a successful
transport operation and never publishes the previous sample as a newly
completed frame.

`end()` is local-only. It clears runtime/session state, but does not clear the
cached configuration, cached restore plan, health counters, or last error. It
does not send Break or reset and does not guarantee that the physical sensor has
stopped periodic/ART acquisition. Call `stopPeriodic()` before `end()` when
hardware acquisition state matters, and cancel an active job first when its
terminal identity must be consumed; `end()` emits no result.

## Recovery Ladder

`recover()` performs one tracked status probe, then a configurable ladder, and
leaves a single-shot local baseline on success:

1. Interface/bus reset (`busReset` callback), then probe
2. Soft reset, then probe
3. Hard reset (`hardReset` callback), then probe
4. General call reset (only if `allowGeneralCallReset` is `true`), then probe

Recovery uses `recoverBackoffMs` to avoid bus thrashing and does **not** run automatically inside `tick()`--the orchestrator triggers it.

With `HealthPolicy::LATCH_OFFLINE`, normal public I2C operations return `BUSY`
after the threshold and do not touch the bus until recovery or the cooperative
ensure-idle job succeeds. With `OBSERVE_ONLY`, the same diagnostic state and
counters are visible but no caller-authorized I2C attempt is suppressed.
`probe()` remains raw/diagnostic and does not update health counters.

After a successful `recover()`, the driver is left in **SINGLE_SHOT** idle mode with measurement state cleared. If you need periodic/ART/heater, re-apply those settings in your orchestrator.
General-call reset is bus-wide, resets every supporting device on the bus, and
is disabled unless `allowGeneralCallReset` is set. Treat it as a bus-manager or
application decision, not a sensor-driver default.

Two explicit reset APIs are available:

- `resetToDefaults()` runs recovery, sets the driver to a safe single-shot
  baseline, and clears cached settings to library defaults. The recovery ladder
  may stop after a successful probe without issuing a reset.
- `resetAndRestore()` runs recovery and restores cached settings from RAM when
  recovery succeeds. The recovery ladder may stop after a successful probe
  without issuing a reset.
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

```bash
pio test -e native
```

Host HIL parser/contract checks (stdlib Python; no pytest required):

```bash
python tools/test_run_i2c_hil_parser.py
python tools/run_i2c_hil.py --parser-self-test
python tools/check_cli_contract.py
python tools/check_hil_contract.py
python tools/check_core_timing_guard.py
python tools/check_idf_example_contract.py
```

Arduino firmware builds and package validation:

```bash
pio run -e esp32s3dev
pio run -e esp32s2dev
pio pkg pack
```

Native ESP-IDF example build (requires an ESP-IDF 5.4+ shell):

```bash
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
quality, or sensor accuracy. `docs/hardware.md` is the maintained evidence
status and HIL procedure.

The automatic serial runner is:

```bash
python tools/run_sht3x_hil.py --dry-run --expect-address 0x44 --board esp32s3 --target-name desk --operator <name>
python tools/run_sht3x_hil.py --port COMx --baud 115200 --expect-address 0x44 --board esp32s3 --target-name desk --operator <name>
```

These runner commands require a full repository checkout. The PlatformIO package
payload is library-focused and may exclude repository maintenance tools.

Default runner groups cover safe smoke, single-shot low/medium/high,
status/status_restore, serial/EIC, heater status, alert read/encode/decode,
selected periodic rates, and ART. Optional groups are gated by
`--include-destructive`, `--include-bus-wide-reset`, `--include-soak`,
`--include-clock-stretch`, `--include-alert-write`, `--include-heater`,
`--include-all-periodic-rates`, `--include-output-tests`, and
`--include-fault-tests`.

For duration-based soaks, `--include-soak --soak-duration-s <seconds>` uses the
firmware-side `i2c_soak <seconds>` command. That keeps USB traffic low by
running repeated SHT3x measurements on the board and emitting one compact,
parseable summary at the end.

The runner rejects a firmware image whose library version or embedded commit
does not match the checkout and requires an embedded `clean` status by default.
Live runs also require the checkout's tracked files to be clean. Use
`--allow-dirty-firmware` only when deliberately collecting non-release
development evidence from a dirty checkout or image. Duration-soak acceptance checks the requested elapsed
time, plausible extrema, logical/transport/protocol counters, and the
request-ID/`pollJob()`/milli-unit path. Built-in runs finish with a deterministic
single-shot/no-stretch/high-repeatability cleanup and verification. `FAIL`,
`INCOMPLETE`, and `OPERATOR_REVIEW_REQUIRED` are nonzero process outcomes;
`--allow-incomplete` is available for planning workflows that intentionally
leave fixture/operator rows open.

That list describes current runner capability. A hardware `PASS` claim is
limited to the selected commands and artifacts in a specific run summary.

Do not claim hardware validation, ALERT pin validation, humidity accuracy,
fault recovery on real buses, soak stability, or production readiness until
those rows have real logs and fixture evidence.

## Documentation

- [CHANGELOG.md](CHANGELOG.md) - full release history
- [Documentation index](https://github.com/janhavelka/SHT3x-main/blob/main/docs/README.md) - maintained guides and claim boundary
- [docs/hardware.md](docs/hardware.md) - hardware evidence status and HIL procedure
- [docs/esp-idf.md](docs/esp-idf.md) - ESP-IDF component/example notes
- [docs/reference/sht3x-chip-notes.md](docs/reference/sht3x-chip-notes.md) - compact SHT3x source-document notes
- `docs/TUNNELMONITOR_NODE_SUITABILITY_AUDIT.md` - repository-only integration suitability evidence
- Repository-only reference material includes vendor PDFs and the alert
  bit-conversion spreadsheet.

Public API Doxygen comments live in `include/SHT3x/`. In a full repository
checkout, generate the strict HTML reference from the root with:

```bash
doxygen Doxyfile
```

Output is written to ignored `.doxygen/html/`. CI treats malformed references,
missing parameter documentation, and undocumented public symbols as errors.

## License

MIT License. See [LICENSE](LICENSE).
