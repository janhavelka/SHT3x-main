# SHT3x Driver Library

Production-grade SHT3x (SHT30/SHT31/SHT35) I2C driver for ESP32 (Arduino/PlatformIO).

## Features

- **Injected I2C transport** - no Wire dependency in library code
- **CRC validation** - all 16-bit data words verified
- **Alert mode support** - read/write limits, encode/decode helpers
- **Periodic + ART modes** - 0.5/1/2/4/10 mps and ART
- **Health monitoring** - automatic state tracking (READY/DEGRADED/OFFLINE)
- **Deterministic behavior** - no unbounded loops, no heap allocations

## Installation

### PlatformIO (recommended)

Add to `platformio.ini`:

```ini
lib_deps = 
  https://github.com/janhavelka/SHT3x.git
```

### Manual

Copy `include/SHT3x/` and `src/` to your project.

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
  Wire.setTimeOut(timeoutMs);
  Wire.beginTransmission(addr);
  Wire.write(data, len);
  uint8_t result = Wire.endTransmission(true);
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
  Wire.setTimeOut(timeoutMs);
  size_t received = Wire.requestFrom(addr, rxLen);
  if (received != rxLen) {
    if (received == 0) {
      return SHT3x::Status::Error(SHT3x::Err::I2C_ERROR, "Read returned 0 bytes",
                                  static_cast<int32_t>(received));
    }
    for (size_t i = 0; i < received; i++) {
      (void)Wire.read();
    }
    return SHT3x::Status::Error(SHT3x::Err::I2C_ERROR, "Read failed");
  }
  for (size_t i = 0; i < rxLen; i++) {
    rx[i] = Wire.read();
  }
  return SHT3x::Status::Ok();
}

SHT3x::SHT3x device;

void setup() {
  Serial.begin(115200);
  Wire.begin(8, 9);  // SDA, SCL
  Wire.setClock(400000);
  Wire.setTimeOut(50);

  SHT3x::Config cfg;
  cfg.i2cWrite = i2cWrite;
  cfg.i2cWriteRead = i2cWriteRead;
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
If you change `Wire.setTimeOut(timeoutMs)` per call, ensure the adapter is single-threaded
or externally serialized so the timeout change is safe.

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

## Recovery Ladder

`recover()` performs a configurable ladder and restores **comms only**:

1. Interface/bus reset (`busReset` callback), then probe
2. Soft reset, then probe
3. Hard reset (`hardReset` callback), then probe
4. General call reset (only if `allowGeneralCallReset` is `true`)

Recovery uses `recoverBackoffMs` to avoid bus thrashing and does **not** run automatically inside `tick()`--the orchestrator triggers it.

After a successful `recover()`, the driver is left in **SINGLE_SHOT** idle mode with measurement state cleared. If you need periodic/ART/heater, re-apply those settings in your orchestrator.

## Thread-Safety / ISR-Safety

The driver is **not** thread-safe and must be externally serialized. Do not call any public API from an ISR.

## ESP32 Notes

- Default is **no clock stretching**. If you enable stretching, set `i2cTimeoutMs` >=
  worst-case tMEAS (15.5 ms at low-Vdd, high repeatability) + margin.
- On ESP32 Wire, a 0-byte `requestFrom` is **ambiguous** (could be not-ready or bus fault).
  Treat it as an error unless your transport can prove read-header NACK.
- Use `Wire.setTimeOut()` and keep bus pull-ups and clock speed within datasheet limits.

## Running Tests

Host tests (requires a native compiler like `g++`):

```
pio test -e native
```

Firmware build (ESP32-S3 example):

```
pio run -e ex_bringup_s3
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
}
```

## Examples

- `01_basic_bringup_cli/` - Interactive CLI for testing

## License

MIT License. See [LICENSE](LICENSE).
