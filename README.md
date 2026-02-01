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
SHT3x::Status i2cWrite(uint8_t addr, const uint8_t* data, size_t len,
                       uint32_t timeoutMs, void* user) {
  Wire.beginTransmission(addr);
  Wire.write(data, len);
  return Wire.endTransmission() == 0
    ? SHT3x::Status::Ok()
    : SHT3x::Status::Error(SHT3x::Err::I2C_ERROR, "Write failed");
}

SHT3x::Status i2cWriteRead(uint8_t addr, const uint8_t* tx, size_t txLen,
                           uint8_t* rx, size_t rxLen, uint32_t timeoutMs, void* user) {
  if (txLen > 0) {
    Wire.beginTransmission(addr);
    Wire.write(tx, txLen);
    if (Wire.endTransmission(false) != 0) {
      return SHT3x::Status::Error(SHT3x::Err::I2C_ERROR, "Write failed");
    }
  }
  if (rxLen == 0) {
    return SHT3x::Status::Ok();
  }
  if (Wire.requestFrom(addr, rxLen) != rxLen) {
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

  SHT3x::Config cfg;
  cfg.i2cWrite = i2cWrite;
  cfg.i2cWriteRead = i2cWriteRead;
  cfg.i2cAddress = 0x44;

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
```

## Examples

- `01_basic_bringup_cli/` - Interactive CLI for testing

## License

MIT License. See [LICENSE](LICENSE).
