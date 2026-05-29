/// @file I2cScanner.h
/// @brief I2C bus scanner utility for examples.
///
/// NOT part of the library API. This is an invasive diagnostic tool for
/// examples: it can reset Wire and mutate bus timeout settings. Do not use it as
/// a production shared-bus scanner without adapting ownership and locking.
#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "Log.h"

namespace i2c_scanner {

/// Attempt to recover a stuck I2C bus by toggling SCL.
inline void recoverBus(int sda, int scl) {
  Wire.end();

  pinMode(scl, OUTPUT);
  pinMode(sda, INPUT_PULLUP);

  for (int i = 0; i < 9; ++i) {
    digitalWrite(scl, LOW);
    delayMicroseconds(5);
    digitalWrite(scl, HIGH);
    delayMicroseconds(5);
    if (digitalRead(sda) != 0) {
      break;
    }
  }

  pinMode(sda, OUTPUT);
  digitalWrite(sda, LOW);
  delayMicroseconds(5);
  digitalWrite(scl, HIGH);
  delayMicroseconds(5);
  digitalWrite(sda, HIGH);
  delayMicroseconds(5);

  Wire.begin(sda, scl);
}

/// Scan I2C bus and print found devices.
inline void scan(TwoWire& wire, uint16_t timeoutMs = 50) {
  LOGI("Scanning I2C bus (timeout=%dms)...", timeoutMs);
  LOG_SERIAL.flush();

#if defined(ARDUINO_ARCH_ESP32)
  wire.setTimeOut(timeoutMs);
#else
  (void)timeoutMs;
#endif

  LOGI("     0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F");
  LOG_SERIAL.flush();

  uint8_t count = 0;
  for (uint8_t row = 0; row < 8; ++row) {
    LOG_SERIAL.printf("%02X: ", row * 16);
    LOG_SERIAL.flush();

    for (uint8_t col = 0; col < 16; ++col) {
      const uint8_t addr = static_cast<uint8_t>((row * 16U) + col);
      if (addr < 0x08U || addr > 0x77U) {
        LOG_SERIAL.print("   ");
        continue;
      }

      wire.beginTransmission(addr);
      const uint8_t error = wire.endTransmission(true);

      if (error == 0U) {
        LOG_SERIAL.printf("%02X ", addr);
        ++count;
      } else if (error == 5U) {
        LOG_SERIAL.print("TO ");
      } else {
        LOG_SERIAL.print("-- ");
      }

      yield();
      delay(1);
    }
    LOG_SERIAL.println();
    LOG_SERIAL.flush();
  }

  LOGI("Scan complete. Found %d device(s).", count);
  LOG_SERIAL.flush();

  if (count > 0U) {
    LOGI("Common addresses: 0x3C/0x3D=OLED, 0x44/0x45=SHT3x, 0x48-0x4B=ADS1115, 0x51=RV3032, 0x76/0x77=BME280");
  }
}

inline void scanDefault(uint16_t timeoutMs = 50) {
  scan(Wire, timeoutMs);
}

}  // namespace i2c_scanner
