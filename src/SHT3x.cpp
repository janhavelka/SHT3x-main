/**
 * @file SHT3x.cpp
 * @brief SHT3x driver implementation.
 */

#include "SHT3x/SHT3x.h"

#include <Arduino.h>
#include <cstring>
#include <limits>

namespace SHT3x {
namespace {

static constexpr size_t MAX_WRITE_LEN = 5;
static constexpr uint32_t RESET_DELAY_MS = 2;
static constexpr uint32_t BREAK_DELAY_MS = 1;
static constexpr uint16_t MIN_COMMAND_DELAY_MS = 1;
static constexpr uint32_t MEASUREMENT_MARGIN_MS = 1;
static constexpr uint32_t ART_PERIOD_MS = 250;
static constexpr uint32_t MAX_SPIN_ITERS = 500000;

static bool isValidRepeatability(Repeatability rep) {
  return rep == Repeatability::LOW_REPEATABILITY || rep == Repeatability::MEDIUM_REPEATABILITY ||
         rep == Repeatability::HIGH_REPEATABILITY;
}

static bool isValidClockStretching(ClockStretching stretch) {
  return stretch == ClockStretching::STRETCH_DISABLED || stretch == ClockStretching::STRETCH_ENABLED;
}

static bool isValidPeriodicRate(PeriodicRate rate) {
  return rate == PeriodicRate::MPS_0_5 || rate == PeriodicRate::MPS_1 ||
         rate == PeriodicRate::MPS_2 || rate == PeriodicRate::MPS_4 ||
         rate == PeriodicRate::MPS_10;
}

static bool isValidMode(Mode mode) {
  return mode == Mode::SINGLE_SHOT || mode == Mode::PERIODIC || mode == Mode::ART;
}

static bool isI2cFailure(Err code) {
  return code == Err::I2C_ERROR || code == Err::I2C_NACK_ADDR ||
         code == Err::I2C_NACK_DATA || code == Err::I2C_NACK_READ ||
         code == Err::I2C_TIMEOUT || code == Err::I2C_BUS;
}

static uint32_t baseMeasurementMs(Repeatability rep, bool lowVdd) {
  if (lowVdd) {
    switch (rep) {
      case Repeatability::LOW_REPEATABILITY: return 5;
      case Repeatability::MEDIUM_REPEATABILITY: return 7;
      case Repeatability::HIGH_REPEATABILITY: return 16;
      default: return 16;
    }
  }

  switch (rep) {
    case Repeatability::LOW_REPEATABILITY: return 4;
    case Repeatability::MEDIUM_REPEATABILITY: return 6;
    case Repeatability::HIGH_REPEATABILITY: return 15;
    default: return 15;
  }
}

}  // namespace

Status SHT3x::begin(const Config& config) {
  _initialized = false;
  _driverState = DriverState::UNINIT;

  _lastOkMs = 0;
  _lastErrorMs = 0;
  _lastBusActivityMs = 0;
  _lastError = Status::Ok();
  _consecutiveFailures = 0;
  _totalFailures = 0;
  _totalSuccess = 0;

  _measurementRequested = false;
  _measurementReady = false;
  _measurementReadyMs = 0;
  _periodicStartMs = 0;
  _lastFetchMs = 0;
  _periodMs = 0;
  _sampleTimestampMs = 0;
  _missedSamples = 0;
  _notReadyStartMs = 0;
  _notReadyCount = 0;
  _lastRecoverMs = 0;
  _rawSample = RawSample{};
  _compSample = CompensatedSample{};
  _mode = Mode::SINGLE_SHOT;
  _periodicActive = false;
  _lastCommandUs = 0;

  if (config.i2cWrite == nullptr || config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C callbacks not set");
  }
  if (config.i2cTimeoutMs == 0) {
    return Status::Error(Err::INVALID_CONFIG, "I2C timeout must be > 0");
  }
  if (config.i2cAddress != cmd::I2C_ADDR_LOW &&
      config.i2cAddress != cmd::I2C_ADDR_HIGH) {
    return Status::Error(Err::INVALID_CONFIG, "Invalid I2C address");
  }
  if (!isValidRepeatability(config.repeatability) ||
      !isValidClockStretching(config.clockStretching) ||
      !isValidPeriodicRate(config.periodicRate) ||
      !isValidMode(config.mode)) {
    return Status::Error(Err::INVALID_CONFIG, "Invalid configuration value");
  }

  _config = config;
  if (_config.offlineThreshold == 0) {
    _config.offlineThreshold = 1;
  }
  if (_config.commandDelayMs < MIN_COMMAND_DELAY_MS) {
    _config.commandDelayMs = MIN_COMMAND_DELAY_MS;
  }

  uint16_t statusRaw = 0;
  Status st = _readStatusRaw(statusRaw, true);
  if (!st.ok()) {
    if (isI2cFailure(st.code)) {
      return Status::Error(Err::DEVICE_NOT_FOUND, "Device not responding", st.detail);
    }
    return st;
  }

  _mode = _config.mode;
  if (_mode == Mode::PERIODIC) {
    st = _enterPeriodic(_config.periodicRate, _config.repeatability, false);
    if (!st.ok()) {
      return st;
    }
  } else if (_mode == Mode::ART) {
    st = _enterPeriodic(_config.periodicRate, _config.repeatability, true);
    if (!st.ok()) {
      return st;
    }
  }

  _initialized = true;
  _driverState = DriverState::READY;

  return Status::Ok();
}

void SHT3x::tick(uint32_t nowMs) {
  if (!_initialized || !_measurementRequested) {
    return;
  }

  if (_mode == Mode::SINGLE_SHOT) {
    if (static_cast<int32_t>(nowMs - _measurementReadyMs) < 0) {
      return;
    }

    Status st = _readMeasurementRaw(_rawSample, true, false);
    if (!st.ok()) {
      return;
    }

    _compSample.tempC_x100 = convertTemperatureC_x100(_rawSample.rawTemperature);
    _compSample.humidityPct_x100 = convertHumidityPct_x100(_rawSample.rawHumidity);

    _sampleTimestampMs = nowMs;
    _measurementReady = true;
    _measurementRequested = false;
    return;
  }

  if (_mode == Mode::PERIODIC || _mode == Mode::ART) {
    if (static_cast<int32_t>(nowMs - _measurementReadyMs) < 0) {
      return;
    }

    Status st = _fetchPeriodic();
    if (!st.ok()) {
      if (st.code == Err::MEASUREMENT_NOT_READY) {
        _measurementReadyMs = nowMs + _config.commandDelayMs;
      }
      return;
    }

    if (_lastFetchMs != 0 && _periodMs > 0) {
      const uint32_t elapsed = nowMs - _lastFetchMs;
      if (elapsed > _periodMs) {
        uint32_t missed = elapsed / _periodMs;
        if (missed > 0) {
          missed -= 1;
          _missedSamples += missed;
        }
      }
    }

    _measurementReady = true;
    _measurementRequested = false;
    _lastFetchMs = nowMs;
    _sampleTimestampMs = nowMs;
  }
}

void SHT3x::end() {
  _initialized = false;
  _driverState = DriverState::UNINIT;
}

Status SHT3x::probe() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  uint16_t statusRaw = 0;
  Status st = _readStatusRaw(statusRaw, false);
  if (!st.ok()) {
    if (isI2cFailure(st.code)) {
      return Status::Error(Err::DEVICE_NOT_FOUND, "Device not responding", st.detail);
    }
    return st;
  }

  return Status::Ok();
}

Status SHT3x::recover() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  const uint32_t now = millis();
  if (_config.recoverBackoffMs > 0 &&
      !_timeElapsed(now, _lastRecoverMs + _config.recoverBackoffMs)) {
    return Status::Error(Err::BUSY, "Recovery backoff active");
  }
  _lastRecoverMs = now;

  auto probeTracked = [this]() -> Status {
    uint16_t statusRaw = 0;
    return _readStatusRaw(statusRaw, true);
  };

  auto setSafeBaseline = [this]() {
    _measurementRequested = false;
    _measurementReady = false;
    _measurementReadyMs = 0;
    _periodicActive = false;
    _periodicStartMs = 0;
    _lastFetchMs = 0;
    _periodMs = 0;
    _sampleTimestampMs = 0;
    _missedSamples = 0;
    _notReadyStartMs = 0;
    _notReadyCount = 0;
    _mode = Mode::SINGLE_SHOT;
    _config.mode = Mode::SINGLE_SHOT;
  };

  Status last = Status::Error(Err::I2C_ERROR, "Recovery failed");

  if (_config.recoverUseBusReset && _config.busReset != nullptr) {
    Status st = interfaceReset();
    if (st.ok()) {
      st = probeTracked();
      if (st.ok()) {
        setSafeBaseline();
        return Status::Ok();
      }
      last = st;
    } else {
      last = st;
    }
  }

  if (_config.recoverUseSoftReset) {
    Status stStop = Status::Ok();
    if (_periodicActive) {
      stStop = _stopPeriodicInternal();
      if (!stStop.ok()) {
        last = stStop;
      }
    }

    if (stStop.ok()) {
      Status st = softReset();
      if (st.ok()) {
        st = probeTracked();
        if (st.ok()) {
          setSafeBaseline();
          return Status::Ok();
        }
      }
      last = st;
    }
  }

  if (_config.recoverUseHardReset && _config.hardReset != nullptr) {
    Status st = _config.hardReset(_config.i2cUser);
    if (st.ok()) {
      st = _waitMs(RESET_DELAY_MS);
      if (!st.ok()) {
        return st;
      }
      st = probeTracked();
      if (st.ok()) {
        setSafeBaseline();
        return Status::Ok();
      }
    }
    last = st;
  }

  if (_config.allowGeneralCallReset) {
    Status st = generalCallReset();
    if (st.ok()) {
      st = probeTracked();
      if (st.ok()) {
        setSafeBaseline();
        return Status::Ok();
      }
    }
    last = st;
  }

  return last;
}

Status SHT3x::requestMeasurement() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_measurementRequested && !_measurementReady) {
    return Status::Error(Err::BUSY, "Measurement in progress");
  }

  _measurementReady = false;

  if (_mode == Mode::SINGLE_SHOT) {
    Status st = _startSingleShot();
    if (!st.ok()) {
      return st;
    }

    _measurementRequested = true;
    _measurementReadyMs = millis() + estimateMeasurementTimeMs();

    return Status::Error(Err::IN_PROGRESS, "Measurement started");
  }

  if (_mode == Mode::PERIODIC || _mode == Mode::ART) {
    if (!_periodicActive) {
      return Status::Error(Err::INVALID_PARAM, "Periodic mode not active");
    }

    const uint32_t now = millis();
    uint32_t readyMs = 0;
    if (_lastFetchMs == 0) {
      readyMs = _periodicStartMs + estimateMeasurementTimeMs();
    } else {
      readyMs = _lastFetchMs + _periodMs;
    }
    if (static_cast<int32_t>(now - readyMs) >= 0) {
      readyMs = now;
    }

    _measurementRequested = true;
    _measurementReadyMs = readyMs;

    return Status::Error(Err::IN_PROGRESS, "Measurement scheduled");
  }

  return Status::Error(Err::INVALID_PARAM, "Invalid mode");
}

Status SHT3x::getMeasurement(Measurement& out) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!_measurementReady) {
    return Status::Error(Err::MEASUREMENT_NOT_READY, "Measurement not ready");
  }

  out.temperatureC = static_cast<float>(_compSample.tempC_x100) / 100.0f;
  out.humidityPct = static_cast<float>(_compSample.humidityPct_x100) / 100.0f;

  _measurementReady = false;
  return Status::Ok();
}

Status SHT3x::getRawSample(RawSample& out) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!_measurementReady) {
    return Status::Error(Err::MEASUREMENT_NOT_READY, "Measurement not ready");
  }

  out = _rawSample;
  return Status::Ok();
}

Status SHT3x::getCompensatedSample(CompensatedSample& out) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!_measurementReady) {
    return Status::Error(Err::MEASUREMENT_NOT_READY, "Measurement not ready");
  }

  out = _compSample;
  return Status::Ok();
}

Status SHT3x::setMode(Mode mode) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_measurementRequested && !_measurementReady) {
    return Status::Error(Err::BUSY, "Measurement in progress");
  }
  if (!isValidMode(mode)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid mode");
  }

  if (mode == _mode) {
    return Status::Ok();
  }

  if (mode == Mode::SINGLE_SHOT) {
    Status st = stopPeriodic();
    if (!st.ok()) {
      return st;
    }
    _mode = Mode::SINGLE_SHOT;
    _config.mode = Mode::SINGLE_SHOT;
    return Status::Ok();
  }

  if (mode == Mode::PERIODIC) {
    return startPeriodic(_config.periodicRate, _config.repeatability);
  }

  return startArt();
}

Status SHT3x::getMode(Mode& out) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  out = _mode;
  return Status::Ok();
}

Status SHT3x::getSettings(SettingsSnapshot& out) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  out.mode = _mode;
  out.repeatability = _config.repeatability;
  out.periodicRate = _config.periodicRate;
  out.clockStretching = _config.clockStretching;
  out.periodicActive = _periodicActive;
  out.measurementPending = _measurementRequested && !_measurementReady;
  out.measurementReady = _measurementReady;
  out.measurementReadyMs = _measurementReadyMs;
  out.sampleTimestampMs = _sampleTimestampMs;
  out.missedSamples = _missedSamples;
  out.status = StatusRegister{};
  out.statusValid = false;
  return Status::Ok();
}

Status SHT3x::readSettings(SettingsSnapshot& out) {
  Status st = getSettings(out);
  if (!st.ok()) {
    return st;
  }

  Status stStatus = readStatus(out.status);
  if (stStatus.ok()) {
    out.statusValid = true;
    return stStatus;
  }
  if (stStatus.code == Err::BUSY) {
    out.statusValid = false;
    return Status::Ok();
  }
  return stStatus;
}

Status SHT3x::setRepeatability(Repeatability rep) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_measurementRequested && !_measurementReady) {
    return Status::Error(Err::BUSY, "Measurement in progress");
  }
  if (!isValidRepeatability(rep)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid repeatability");
  }

  _config.repeatability = rep;

  if (_mode == Mode::PERIODIC) {
    return startPeriodic(_config.periodicRate, rep);
  }

  return Status::Ok();
}

Status SHT3x::getRepeatability(Repeatability& out) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  out = _config.repeatability;
  return Status::Ok();
}

Status SHT3x::setClockStretching(ClockStretching stretch) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_measurementRequested && !_measurementReady) {
    return Status::Error(Err::BUSY, "Measurement in progress");
  }
  if (!isValidClockStretching(stretch)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid clock stretching");
  }

  _config.clockStretching = stretch;
  return Status::Ok();
}

Status SHT3x::getClockStretching(ClockStretching& out) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  out = _config.clockStretching;
  return Status::Ok();
}

Status SHT3x::setPeriodicRate(PeriodicRate rate) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_measurementRequested && !_measurementReady) {
    return Status::Error(Err::BUSY, "Measurement in progress");
  }
  if (!isValidPeriodicRate(rate)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid periodic rate");
  }

  _config.periodicRate = rate;

  if (_mode == Mode::PERIODIC) {
    return startPeriodic(rate, _config.repeatability);
  }

  return Status::Ok();
}

Status SHT3x::getPeriodicRate(PeriodicRate& out) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  out = _config.periodicRate;
  return Status::Ok();
}

Status SHT3x::startPeriodic(PeriodicRate rate, Repeatability rep) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!isValidPeriodicRate(rate) || !isValidRepeatability(rep)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid periodic settings");
  }

  return _enterPeriodic(rate, rep, false);
}

Status SHT3x::startArt() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  return _enterPeriodic(_config.periodicRate, _config.repeatability, true);
}

Status SHT3x::stopPeriodic() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }

  return _stopPeriodicInternal();
}

Status SHT3x::readStatus(uint16_t& raw) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_periodicActive) {
    return Status::Error(Err::BUSY, "Stop periodic mode before reading status");
  }

  return _readStatusRaw(raw, true);
}

Status SHT3x::readStatus(StatusRegister& out) {
  uint16_t raw = 0;
  Status st = readStatus(raw);
  if (!st.ok()) {
    return st;
  }

  out.raw = raw;
  out.alertPending = (raw & cmd::STATUS_ALERT_PENDING) != 0;
  out.heaterOn = (raw & cmd::STATUS_HEATER_ON) != 0;
  out.rhAlert = (raw & cmd::STATUS_RH_ALERT) != 0;
  out.tAlert = (raw & cmd::STATUS_T_ALERT) != 0;
  out.resetDetected = (raw & cmd::STATUS_RESET_DETECTED) != 0;
  out.commandError = (raw & cmd::STATUS_COMMAND_ERROR) != 0;
  out.writeCrcError = (raw & cmd::STATUS_WRITE_CRC_ERROR) != 0;
  return Status::Ok();
}

Status SHT3x::clearStatus() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_periodicActive) {
    return Status::Error(Err::BUSY, "Stop periodic mode before clearing status");
  }

  return _writeCommand(cmd::CMD_CLEAR_STATUS, true);
}

Status SHT3x::setHeater(bool enable) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_periodicActive) {
    return Status::Error(Err::BUSY, "Stop periodic mode before changing heater");
  }

  return _writeCommand(enable ? cmd::CMD_HEATER_ENABLE : cmd::CMD_HEATER_DISABLE, true);
}

Status SHT3x::readHeaterStatus(bool& enabled) {
  StatusRegister stReg;
  Status st = readStatus(stReg);
  if (!st.ok()) {
    return st;
  }
  enabled = stReg.heaterOn;
  return Status::Ok();
}

Status SHT3x::softReset() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_periodicActive) {
    return Status::Error(Err::BUSY, "Stop periodic mode before reset");
  }

  Status st = _writeCommand(cmd::CMD_SOFT_RESET, true);
  if (!st.ok()) {
    return st;
  }

  st = _waitMs(RESET_DELAY_MS);
  if (!st.ok()) {
    return st;
  }

  _measurementRequested = false;
  _measurementReady = false;
  _mode = Mode::SINGLE_SHOT;
  _config.mode = Mode::SINGLE_SHOT;
  _periodicActive = false;
  _periodicStartMs = 0;
  _lastFetchMs = 0;
  _periodMs = 0;
  _sampleTimestampMs = 0;
  _missedSamples = 0;
  _notReadyStartMs = 0;
  _notReadyCount = 0;

  return Status::Ok();
}

Status SHT3x::interfaceReset() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_config.busReset == nullptr) {
    return Status::Error(Err::UNSUPPORTED, "Bus reset callback not set");
  }

  Status st = _config.busReset(_config.i2cUser);
  if (!st.ok()) {
    return st;
  }

  _measurementRequested = false;
  _measurementReady = false;
  _measurementReadyMs = 0;
  _lastFetchMs = 0;
  _sampleTimestampMs = 0;
  _missedSamples = 0;
  _notReadyStartMs = 0;
  _notReadyCount = 0;
  if (_periodicActive) {
    _periodicStartMs = millis();
  }

  return Status::Ok();
}

Status SHT3x::generalCallReset() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (!_config.allowGeneralCallReset) {
    return Status::Error(Err::INVALID_CONFIG, "General call reset disabled");
  }

  Status st = _ensureCommandDelay();
  if (!st.ok()) {
    return st;
  }

  uint8_t byte = cmd::GENERAL_CALL_RESET_BYTE;
  st = _i2cWriteRawAddrTracked(cmd::GENERAL_CALL_ADDR, &byte, 1);
  if (!st.ok()) {
    return st;
  }

  _lastCommandUs = micros();
  st = _waitMs(RESET_DELAY_MS);
  if (!st.ok()) {
    return st;
  }

  _measurementRequested = false;
  _measurementReady = false;
  _measurementReadyMs = 0;
  _mode = Mode::SINGLE_SHOT;
  _config.mode = Mode::SINGLE_SHOT;
  _periodicActive = false;
  _periodicStartMs = 0;
  _lastFetchMs = 0;
  _periodMs = 0;
  _sampleTimestampMs = 0;
  _missedSamples = 0;
  _notReadyStartMs = 0;
  _notReadyCount = 0;

  return Status::Ok();
}

Status SHT3x::readSerialNumber(uint32_t& serial, ClockStretching stretch) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_periodicActive) {
    return Status::Error(Err::BUSY, "Stop periodic mode before reading serial");
  }
  if (!isValidClockStretching(stretch)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid clock stretching");
  }

  const uint16_t cmd = (stretch == ClockStretching::STRETCH_ENABLED)
      ? cmd::CMD_SERIAL_STRETCH
      : cmd::CMD_SERIAL_NO_STRETCH;

  Status st = _writeCommand(cmd, true);
  if (!st.ok()) {
    return st;
  }

  uint8_t buf[cmd::SERIAL_DATA_LEN] = {};
  st = _readAfterCommand(buf, sizeof(buf), true);
  if (!st.ok()) {
    return st;
  }

  if (_crc8(&buf[0], 2) != buf[2]) {
    return Status::Error(Err::CRC_MISMATCH, "CRC mismatch (serial word1)");
  }
  if (_crc8(&buf[3], 2) != buf[5]) {
    return Status::Error(Err::CRC_MISMATCH, "CRC mismatch (serial word2)");
  }

  const uint16_t word1 = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
  const uint16_t word2 = static_cast<uint16_t>((buf[3] << 8) | buf[4]);
  serial = (static_cast<uint32_t>(word1) << 16) | word2;

  return Status::Ok();
}

Status SHT3x::readAlertLimitRaw(AlertLimitKind kind, uint16_t& value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_periodicActive) {
    return Status::Error(Err::BUSY, "Stop periodic mode before reading alert limits");
  }

  const uint16_t cmd = _commandForAlertRead(kind);
  if (cmd == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid alert limit kind");
  }

  Status st = _writeCommand(cmd, true);
  if (!st.ok()) {
    return st;
  }

  uint8_t buf[cmd::ALERT_DATA_LEN] = {};
  st = _readAfterCommand(buf, sizeof(buf), true);
  if (!st.ok()) {
    return st;
  }

  if (_crc8(&buf[0], 2) != buf[2]) {
    return Status::Error(Err::CRC_MISMATCH, "CRC mismatch (alert limit)");
  }

  value = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
  return Status::Ok();
}

Status SHT3x::readAlertLimit(AlertLimitKind kind, AlertLimit& out) {
  uint16_t raw = 0;
  Status st = readAlertLimitRaw(kind, raw);
  if (!st.ok()) {
    return st;
  }

  out.raw = raw;
  decodeAlertLimit(raw, out.temperatureC, out.humidityPct);
  return Status::Ok();
}

Status SHT3x::writeAlertLimitRaw(AlertLimitKind kind, uint16_t value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "begin() not called");
  }
  if (_periodicActive) {
    return Status::Error(Err::BUSY, "Stop periodic mode before writing alert limits");
  }

  const uint16_t cmd = _commandForAlertWrite(kind);
  if (cmd == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid alert limit kind");
  }

  Status st = _writeCommandWithData(cmd, value, true);
  if (!st.ok()) {
    return st;
  }

  uint16_t statusRaw = 0;
  st = _readStatusRaw(statusRaw, true);
  if (!st.ok()) {
    return st;
  }

  if (statusRaw & cmd::STATUS_WRITE_CRC_ERROR) {
    return Status::Error(Err::WRITE_CRC_ERROR, "Write checksum error");
  }
  if (statusRaw & cmd::STATUS_COMMAND_ERROR) {
    return Status::Error(Err::COMMAND_FAILED, "Command rejected");
  }

  return Status::Ok();
}

Status SHT3x::writeAlertLimit(AlertLimitKind kind, float temperatureC, float humidityPct) {
  const uint16_t packed = encodeAlertLimit(temperatureC, humidityPct);
  return writeAlertLimitRaw(kind, packed);
}

Status SHT3x::disableAlerts() {
  Status st = writeAlertLimitRaw(AlertLimitKind::HIGH_SET, 0x0000);
  if (!st.ok()) {
    return st;
  }
  return writeAlertLimitRaw(AlertLimitKind::LOW_SET, 0xFFFF);
}

uint16_t SHT3x::encodeAlertLimit(float temperatureC, float humidityPct) {
  if (humidityPct < 0.0f) {
    humidityPct = 0.0f;
  }
  if (humidityPct > 100.0f) {
    humidityPct = 100.0f;
  }
  if (temperatureC < -45.0f) {
    temperatureC = -45.0f;
  }
  if (temperatureC > 130.0f) {
    temperatureC = 130.0f;
  }

  const float rawRhF = humidityPct * 65535.0f / 100.0f;
  const float rawTF = (temperatureC + 45.0f) * 65535.0f / 175.0f;

  uint32_t rawRh = static_cast<uint32_t>(rawRhF + 0.5f);
  uint32_t rawT = static_cast<uint32_t>(rawTF + 0.5f);

  if (rawRh > 65535U) {
    rawRh = 65535U;
  }
  if (rawT > 65535U) {
    rawT = 65535U;
  }

  const uint16_t rh7 = static_cast<uint16_t>(rawRh >> 9);
  const uint16_t t9 = static_cast<uint16_t>(rawT >> 7);
  return static_cast<uint16_t>((rh7 << 9) | (t9 & 0x01FF));
}

void SHT3x::decodeAlertLimit(uint16_t limit, float& temperatureC, float& humidityPct) {
  const uint16_t rh7 = static_cast<uint16_t>((limit >> 9) & 0x7F);
  const uint16_t t9 = static_cast<uint16_t>(limit & 0x01FF);

  const uint32_t rawRh = static_cast<uint32_t>(rh7) << 9;
  const uint32_t rawT = static_cast<uint32_t>(t9) << 7;

  humidityPct = (100.0f * static_cast<float>(rawRh)) / 65535.0f;
  temperatureC = -45.0f + (175.0f * static_cast<float>(rawT) / 65535.0f);
}

float SHT3x::convertTemperatureC(uint16_t raw) {
  return -45.0f + (175.0f * static_cast<float>(raw) / 65535.0f);
}

float SHT3x::convertHumidityPct(uint16_t raw) {
  return (100.0f * static_cast<float>(raw)) / 65535.0f;
}

int32_t SHT3x::convertTemperatureC_x100(uint16_t raw) {
  const int32_t numerator = static_cast<int32_t>(17500) * static_cast<int32_t>(raw);
  const int32_t temp = (numerator + 32767) / 65535;
  return temp - 4500;
}

uint32_t SHT3x::convertHumidityPct_x100(uint16_t raw) {
  const uint32_t numerator = 10000U * static_cast<uint32_t>(raw);
  return (numerator + 32767U) / 65535U;
}

uint32_t SHT3x::estimateMeasurementTimeMs() const {
  const uint32_t baseMs = baseMeasurementMs(_config.repeatability, _config.lowVdd);
  return baseMs + MEASUREMENT_MARGIN_MS;
}

Status SHT3x::_i2cWriteReadRaw(const uint8_t* txBuf, size_t txLen,
                               uint8_t* rxBuf, size_t rxLen) {
  if (_config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C write-read not set");
  }
  return _config.i2cWriteRead(_config.i2cAddress, txBuf, txLen, rxBuf, rxLen,
                              _config.i2cTimeoutMs, _config.i2cUser);
}

Status SHT3x::_i2cWriteRaw(const uint8_t* buf, size_t len) {
  if (_config.i2cWrite == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C write not set");
  }
  return _config.i2cWrite(_config.i2cAddress, buf, len, _config.i2cTimeoutMs,
                          _config.i2cUser);
}

Status SHT3x::_i2cWriteRawAddr(uint8_t addr, const uint8_t* buf, size_t len) {
  if (_config.i2cWrite == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C write not set");
  }
  return _config.i2cWrite(addr, buf, len, _config.i2cTimeoutMs, _config.i2cUser);
}

Status SHT3x::_i2cWriteRawAddrTracked(uint8_t addr, const uint8_t* buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }

  Status st = _i2cWriteRawAddr(addr, buf, len);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  return _updateHealth(st);
}

Status SHT3x::_i2cWriteReadTracked(const uint8_t* txBuf, size_t txLen,
                                   uint8_t* rxBuf, size_t rxLen) {
  if ((txLen > 0 && txBuf == nullptr) || (rxLen > 0 && rxBuf == nullptr)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }

  Status st = _i2cWriteReadRaw(txBuf, txLen, rxBuf, rxLen);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  return _updateHealth(st);
}

Status SHT3x::_i2cWriteReadTrackedAllowNoData(const uint8_t* txBuf, size_t txLen,
                                              uint8_t* rxBuf, size_t rxLen,
                                              bool allowNoData) {
  if ((txLen > 0 && txBuf == nullptr) || (rxLen > 0 && rxBuf == nullptr)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }

  const bool canReportNack = hasCapability(_config.transportCapabilities,
                                           TransportCapability::READ_HEADER_NACK);
  const bool allow = allowNoData && canReportNack;

  Status st = _i2cWriteReadRaw(txBuf, txLen, rxBuf, rxLen);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  if (allow && st.code == Err::I2C_NACK_READ && txLen == 0 && rxLen > 0) {
    _recordBusActivity(millis());
    return Status::Error(Err::MEASUREMENT_NOT_READY, "No new data", st.detail);
  }
  return _updateHealth(st);
}

Status SHT3x::_i2cWriteTracked(const uint8_t* buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }

  Status st = _i2cWriteRaw(buf, len);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  return _updateHealth(st);
}

Status SHT3x::_writeCommand(uint16_t cmd, bool tracked) {
  Status st = _ensureCommandDelay();
  if (!st.ok()) {
    return st;
  }

  uint8_t buf[2] = {static_cast<uint8_t>(cmd >> 8), static_cast<uint8_t>(cmd & 0xFF)};
  st = tracked ? _i2cWriteTracked(buf, sizeof(buf)) : _i2cWriteRaw(buf, sizeof(buf));
  if (!st.ok()) {
    return st;
  }

  _lastCommandUs = micros();
  return Status::Ok();
}

Status SHT3x::_writeCommandWithData(uint16_t cmd, uint16_t data, bool tracked) {
  Status st = _ensureCommandDelay();
  if (!st.ok()) {
    return st;
  }

  uint8_t payload[MAX_WRITE_LEN] = {};
  payload[0] = static_cast<uint8_t>(cmd >> 8);
  payload[1] = static_cast<uint8_t>(cmd & 0xFF);
  payload[2] = static_cast<uint8_t>(data >> 8);
  payload[3] = static_cast<uint8_t>(data & 0xFF);
  payload[4] = _crc8(&payload[2], 2);

  st = tracked ? _i2cWriteTracked(payload, sizeof(payload))
               : _i2cWriteRaw(payload, sizeof(payload));
  if (!st.ok()) {
    return st;
  }

  _lastCommandUs = micros();
  return Status::Ok();
}

Status SHT3x::_readAfterCommand(uint8_t* buf, size_t len, bool tracked,
                                bool allowNoData) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid read buffer");
  }

  Status st = _ensureCommandDelay();
  if (!st.ok()) {
    return st;
  }

  return _readOnly(buf, len, tracked, allowNoData);
}

Status SHT3x::_readOnly(uint8_t* buf, size_t len, bool tracked,
                        bool allowNoData) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid read buffer");
  }

  if (tracked) {
    return allowNoData
        ? _i2cWriteReadTrackedAllowNoData(nullptr, 0, buf, len, true)
        : _i2cWriteReadTracked(nullptr, 0, buf, len);
  }
  return _i2cWriteReadRaw(nullptr, 0, buf, len);
}

Status SHT3x::_updateHealth(const Status& st) {
  const uint32_t now = millis();
  const uint32_t maxU32 = std::numeric_limits<uint32_t>::max();
  const uint8_t maxU8 = std::numeric_limits<uint8_t>::max();

  _recordBusActivity(now);

  if (!_initialized) {
    if (st.ok()) {
      _lastOkMs = now;
    } else {
      _lastError = st;
      _lastErrorMs = now;
    }
    return st;
  }

  if (st.ok()) {
    _lastOkMs = now;
    if (_totalSuccess < maxU32) {
      _totalSuccess++;
    }
    _consecutiveFailures = 0;
    _driverState = DriverState::READY;
    return st;
  }

  _lastError = st;
  _lastErrorMs = now;
  if (_totalFailures < maxU32) {
    _totalFailures++;
  }
  if (_consecutiveFailures < maxU8) {
    _consecutiveFailures++;
  }

  if (_consecutiveFailures >= _config.offlineThreshold) {
    _driverState = DriverState::OFFLINE;
  } else {
    _driverState = DriverState::DEGRADED;
  }

  return st;
}

void SHT3x::_recordBusActivity(uint32_t nowMs) {
  _lastBusActivityMs = nowMs;
}

Status SHT3x::_ensureCommandDelay() {
  if (_lastCommandUs == 0) {
    return Status::Ok();
  }

  const uint32_t delayUs = static_cast<uint32_t>(_config.commandDelayMs) * 1000U;
  const uint32_t target = _lastCommandUs + delayUs;
  const uint32_t startMs = millis();
  const uint32_t timeoutMs =
      static_cast<uint32_t>(_config.commandDelayMs) + _config.i2cTimeoutMs;
  uint32_t lastMs = startMs;
  uint32_t stableLoops = 0;

  while (!_timeElapsed(micros(), target)) {
    const uint32_t nowMs = millis();
    if (static_cast<uint32_t>(nowMs - startMs) > timeoutMs) {
      return Status::Error(Err::TIMEOUT, "Command delay timeout");
    }
    if (nowMs != lastMs) {
      lastMs = nowMs;
      stableLoops = 0;
    } else if (++stableLoops >= MAX_SPIN_ITERS) {
      return Status::Error(Err::TIMEOUT, "Command delay timeout");
    }
  }

  return Status::Ok();
}

Status SHT3x::_waitMs(uint32_t delayMs) {
  if (delayMs == 0) {
    return Status::Ok();
  }

  const uint32_t startMs = millis();
  const uint32_t deadline = startMs + delayMs;
  const uint32_t timeoutMs = delayMs + _config.i2cTimeoutMs;
  uint32_t lastMs = startMs;
  uint32_t stableLoops = 0;

  while (true) {
    const uint32_t nowMs = millis();
    if (_timeElapsed(nowMs, deadline)) {
      break;
    }
    if (static_cast<uint32_t>(nowMs - startMs) > timeoutMs) {
      return Status::Error(Err::TIMEOUT, "Wait timeout");
    }
    if (nowMs != lastMs) {
      lastMs = nowMs;
      stableLoops = 0;
    } else if (++stableLoops >= MAX_SPIN_ITERS) {
      return Status::Error(Err::TIMEOUT, "Wait timeout");
    }
  }

  return Status::Ok();
}

Status SHT3x::_readStatusRaw(uint16_t& raw, bool tracked) {
  Status st = _writeCommand(cmd::CMD_READ_STATUS, tracked);
  if (!st.ok()) {
    return st;
  }

  uint8_t buf[cmd::STATUS_DATA_LEN] = {};
  st = _readAfterCommand(buf, sizeof(buf), tracked);
  if (!st.ok()) {
    return st;
  }

  if (_crc8(&buf[0], 2) != buf[2]) {
    return Status::Error(Err::CRC_MISMATCH, "CRC mismatch (status)");
  }

  raw = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
  return Status::Ok();
}

Status SHT3x::_readMeasurementRaw(RawSample& out, bool tracked, bool allowNoData) {
  uint8_t buf[cmd::MEASUREMENT_DATA_LEN] = {};
  Status st = _readAfterCommand(buf, sizeof(buf), tracked, allowNoData);
  if (!st.ok()) {
    return st;
  }

  if (_crc8(&buf[0], 2) != buf[2]) {
    return Status::Error(Err::CRC_MISMATCH, "CRC mismatch (temperature)");
  }
  if (_crc8(&buf[3], 2) != buf[5]) {
    return Status::Error(Err::CRC_MISMATCH, "CRC mismatch (humidity)");
  }

  out.rawTemperature = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
  out.rawHumidity = static_cast<uint16_t>((buf[3] << 8) | buf[4]);
  return Status::Ok();
}

Status SHT3x::_fetchPeriodic() {
  if (!_periodicActive) {
    return Status::Error(Err::INVALID_PARAM, "Periodic mode not active");
  }

  Status st = _writeCommand(cmd::CMD_FETCH_DATA, true);
  if (!st.ok()) {
    return st;
  }

  bool allowNoData = hasCapability(_config.transportCapabilities,
                                   TransportCapability::READ_HEADER_NACK);
  const uint32_t now = millis();
  if (allowNoData && _config.notReadyTimeoutMs > 0 && _notReadyStartMs != 0) {
    const uint32_t deadline = _notReadyStartMs + _config.notReadyTimeoutMs;
    if (_timeElapsed(now, deadline)) {
      allowNoData = false;
    }
  }

  st = _readMeasurementRaw(_rawSample, true, allowNoData);
  if (st.code == Err::MEASUREMENT_NOT_READY) {
    if (_notReadyStartMs == 0) {
      _notReadyStartMs = now;
    }
    if (_notReadyCount < std::numeric_limits<uint32_t>::max()) {
      _notReadyCount++;
    }
    return st;
  }
  _notReadyStartMs = 0;
  _notReadyCount = 0;
  if (!st.ok()) {
    return st;
  }

  _compSample.tempC_x100 = convertTemperatureC_x100(_rawSample.rawTemperature);
  _compSample.humidityPct_x100 = convertHumidityPct_x100(_rawSample.rawHumidity);
  return Status::Ok();
}

Status SHT3x::_startSingleShot() {
  if (_periodicActive) {
    return Status::Error(Err::BUSY, "Periodic mode active");
  }

  const uint16_t cmd = _commandForSingleShot(_config.repeatability,
                                             _config.clockStretching);
  if (cmd == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid single-shot configuration");
  }

  return _writeCommand(cmd, true);
}

Status SHT3x::_enterPeriodic(PeriodicRate rate, Repeatability rep, bool art) {
  if (!isValidPeriodicRate(rate) || !isValidRepeatability(rep)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid periodic settings");
  }

  if (_periodicActive) {
    Status st = _stopPeriodicInternal();
    if (!st.ok()) {
      return st;
    }
  }

  uint16_t cmd = 0;
  if (art) {
    cmd = cmd::CMD_ART;
  } else {
    cmd = _commandForPeriodic(rep, rate);
  }
  if (cmd == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid periodic command");
  }

  Status st = _writeCommand(cmd, true);
  if (!st.ok()) {
    return st;
  }

  _measurementRequested = false;
  _measurementReady = false;
  _measurementReadyMs = 0;
  _periodicActive = true;
  _notReadyStartMs = 0;
  _notReadyCount = 0;
  _missedSamples = 0;
  _mode = art ? Mode::ART : Mode::PERIODIC;
  _config.mode = _mode;
  if (!art) {
    _config.periodicRate = rate;
    _config.repeatability = rep;
    _periodMs = _periodMsForRate(rate);
  } else {
    _periodMs = ART_PERIOD_MS;
  }
  _periodicStartMs = millis();
  _lastFetchMs = 0;

  return Status::Ok();
}

Status SHT3x::_stopPeriodicInternal() {
  if (!_periodicActive) {
    _mode = Mode::SINGLE_SHOT;
    _config.mode = Mode::SINGLE_SHOT;
    _periodicStartMs = 0;
    _lastFetchMs = 0;
    _periodMs = 0;
    _notReadyStartMs = 0;
    _notReadyCount = 0;
    _missedSamples = 0;
    return Status::Ok();
  }

  Status st = _writeCommand(cmd::CMD_BREAK, true);
  if (!st.ok()) {
    return st;
  }

  st = _waitMs(BREAK_DELAY_MS);
  if (!st.ok()) {
    return st;
  }

  _measurementRequested = false;
  _measurementReady = false;
  _measurementReadyMs = 0;
  _periodicActive = false;
  _mode = Mode::SINGLE_SHOT;
  _config.mode = Mode::SINGLE_SHOT;
  _periodicStartMs = 0;
  _lastFetchMs = 0;
  _periodMs = 0;
  _notReadyStartMs = 0;
  _notReadyCount = 0;
  _missedSamples = 0;
  return Status::Ok();
}

uint8_t SHT3x::_crc8(const uint8_t* data, size_t len) {
  uint8_t crc = cmd::CRC_INIT;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if (crc & 0x80) {
        crc = static_cast<uint8_t>((crc << 1) ^ cmd::CRC_POLY);
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

uint16_t SHT3x::_commandForSingleShot(Repeatability rep, ClockStretching stretch) {
  const bool useStretch = (stretch == ClockStretching::STRETCH_ENABLED);
  switch (rep) {
    case Repeatability::HIGH_REPEATABILITY:
      return useStretch ? cmd::CMD_SINGLE_SHOT_STRETCH_HIGH
                        : cmd::CMD_SINGLE_SHOT_NO_STRETCH_HIGH;
    case Repeatability::MEDIUM_REPEATABILITY:
      return useStretch ? cmd::CMD_SINGLE_SHOT_STRETCH_MED
                        : cmd::CMD_SINGLE_SHOT_NO_STRETCH_MED;
    case Repeatability::LOW_REPEATABILITY:
      return useStretch ? cmd::CMD_SINGLE_SHOT_STRETCH_LOW
                        : cmd::CMD_SINGLE_SHOT_NO_STRETCH_LOW;
    default:
      return 0;
  }
}

uint16_t SHT3x::_commandForPeriodic(Repeatability rep, PeriodicRate rate) {
  switch (rate) {
    case PeriodicRate::MPS_0_5:
      switch (rep) {
        case Repeatability::HIGH_REPEATABILITY: return cmd::CMD_PERIODIC_0_5_HIGH;
        case Repeatability::MEDIUM_REPEATABILITY: return cmd::CMD_PERIODIC_0_5_MED;
        case Repeatability::LOW_REPEATABILITY: return cmd::CMD_PERIODIC_0_5_LOW;
        default: return 0;
      }
    case PeriodicRate::MPS_1:
      switch (rep) {
        case Repeatability::HIGH_REPEATABILITY: return cmd::CMD_PERIODIC_1_HIGH;
        case Repeatability::MEDIUM_REPEATABILITY: return cmd::CMD_PERIODIC_1_MED;
        case Repeatability::LOW_REPEATABILITY: return cmd::CMD_PERIODIC_1_LOW;
        default: return 0;
      }
    case PeriodicRate::MPS_2:
      switch (rep) {
        case Repeatability::HIGH_REPEATABILITY: return cmd::CMD_PERIODIC_2_HIGH;
        case Repeatability::MEDIUM_REPEATABILITY: return cmd::CMD_PERIODIC_2_MED;
        case Repeatability::LOW_REPEATABILITY: return cmd::CMD_PERIODIC_2_LOW;
        default: return 0;
      }
    case PeriodicRate::MPS_4:
      switch (rep) {
        case Repeatability::HIGH_REPEATABILITY: return cmd::CMD_PERIODIC_4_HIGH;
        case Repeatability::MEDIUM_REPEATABILITY: return cmd::CMD_PERIODIC_4_MED;
        case Repeatability::LOW_REPEATABILITY: return cmd::CMD_PERIODIC_4_LOW;
        default: return 0;
      }
    case PeriodicRate::MPS_10:
      switch (rep) {
        case Repeatability::HIGH_REPEATABILITY: return cmd::CMD_PERIODIC_10_HIGH;
        case Repeatability::MEDIUM_REPEATABILITY: return cmd::CMD_PERIODIC_10_MED;
        case Repeatability::LOW_REPEATABILITY: return cmd::CMD_PERIODIC_10_LOW;
        default: return 0;
      }
    default:
      return 0;
  }
}

uint16_t SHT3x::_commandForAlertRead(AlertLimitKind kind) {
  switch (kind) {
    case AlertLimitKind::HIGH_SET: return cmd::CMD_ALERT_READ_HIGH_SET;
    case AlertLimitKind::HIGH_CLEAR: return cmd::CMD_ALERT_READ_HIGH_CLEAR;
    case AlertLimitKind::LOW_CLEAR: return cmd::CMD_ALERT_READ_LOW_CLEAR;
    case AlertLimitKind::LOW_SET: return cmd::CMD_ALERT_READ_LOW_SET;
    default: return 0;
  }
}

uint16_t SHT3x::_commandForAlertWrite(AlertLimitKind kind) {
  switch (kind) {
    case AlertLimitKind::HIGH_SET: return cmd::CMD_ALERT_WRITE_HIGH_SET;
    case AlertLimitKind::HIGH_CLEAR: return cmd::CMD_ALERT_WRITE_HIGH_CLEAR;
    case AlertLimitKind::LOW_CLEAR: return cmd::CMD_ALERT_WRITE_LOW_CLEAR;
    case AlertLimitKind::LOW_SET: return cmd::CMD_ALERT_WRITE_LOW_SET;
    default: return 0;
  }
}

uint32_t SHT3x::_periodMsForRate(PeriodicRate rate) {
  switch (rate) {
    case PeriodicRate::MPS_0_5: return 2000;
    case PeriodicRate::MPS_1: return 1000;
    case PeriodicRate::MPS_2: return 500;
    case PeriodicRate::MPS_4: return 250;
    case PeriodicRate::MPS_10: return 100;
    default: return 1000;
  }
}

bool SHT3x::_timeElapsed(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

}  // namespace SHT3x
