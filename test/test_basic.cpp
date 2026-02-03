/// @file test_basic.cpp
/// @brief Basic unit tests for SHT3x driver

#include <unity.h>

// Include stubs first
#include "Arduino.h"
#include "Wire.h"
#include "examples/common/I2cTransport.h"

// Stub implementations
SerialClass Serial;
TwoWire Wire;
uint32_t gMillis = 0;
uint32_t gMicros = 0;
uint32_t gMillisStep = 0;
uint32_t gMicrosStep = 0;

// Include driver (expose private for test hooks)
#define private public
#include "SHT3x/SHT3x.h"
#undef private

using namespace SHT3x;
using SHT3xDevice = SHT3x::SHT3x;

// ============================================================================
// Test Helpers
// ============================================================================

void setUp() {}
void tearDown() {}

// ============================================================================
// Tests
// ============================================================================

void test_status_ok() {
  Status st = Status::Ok();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL(Err::OK, st.code);
}

void test_status_error() {
  Status st = Status::Error(Err::I2C_ERROR, "Test error", 42);
  TEST_ASSERT_FALSE(st.ok());
  TEST_ASSERT_EQUAL(Err::I2C_ERROR, st.code);
  TEST_ASSERT_EQUAL(42, st.detail);
}

void test_status_in_progress() {
  Status st = Status{Err::IN_PROGRESS, 0, "In progress"};
  TEST_ASSERT_FALSE(st.ok());
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
}

void test_config_defaults() {
  Config cfg;
  TEST_ASSERT_EQUAL(nullptr, cfg.i2cWrite);
  TEST_ASSERT_EQUAL(nullptr, cfg.i2cWriteRead);
  TEST_ASSERT_EQUAL(nullptr, cfg.busReset);
  TEST_ASSERT_EQUAL(nullptr, cfg.hardReset);
  TEST_ASSERT_EQUAL(0x44, cfg.i2cAddress);
  TEST_ASSERT_EQUAL(50u, cfg.i2cTimeoutMs);
  TEST_ASSERT_EQUAL(0, static_cast<uint8_t>(cfg.transportCapabilities));
  TEST_ASSERT_EQUAL(5, cfg.offlineThreshold);
  TEST_ASSERT_EQUAL(1u, cfg.commandDelayMs);
  TEST_ASSERT_EQUAL(0u, cfg.notReadyTimeoutMs);
  TEST_ASSERT_EQUAL(0u, cfg.periodicFetchMarginMs);
  TEST_ASSERT_EQUAL(100u, cfg.recoverBackoffMs);
  TEST_ASSERT_FALSE(cfg.allowGeneralCallReset);
  TEST_ASSERT_TRUE(cfg.recoverUseBusReset);
  TEST_ASSERT_TRUE(cfg.recoverUseSoftReset);
  TEST_ASSERT_TRUE(cfg.recoverUseHardReset);
  TEST_ASSERT_FALSE(cfg.lowVdd);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Repeatability::HIGH_REPEATABILITY),
                    static_cast<uint8_t>(cfg.repeatability));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(ClockStretching::STRETCH_DISABLED),
                    static_cast<uint8_t>(cfg.clockStretching));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(PeriodicRate::MPS_1),
                    static_cast<uint8_t>(cfg.periodicRate));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Mode::SINGLE_SHOT),
                    static_cast<uint8_t>(cfg.mode));
}

void test_crc8_example() {
  const uint8_t data[2] = {0xBE, 0xEF};
  const uint8_t crc = SHT3xDevice::_crc8(data, 2);
  TEST_ASSERT_EQUAL(0x92, crc);
}

void test_conversions_basic() {
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -45.0f, SHT3xDevice::convertTemperatureC(0));
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 130.0f, SHT3xDevice::convertTemperatureC(65535));
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, SHT3xDevice::convertHumidityPct(0));
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 100.0f, SHT3xDevice::convertHumidityPct(65535));

  TEST_ASSERT_EQUAL_INT(-4500, SHT3xDevice::convertTemperatureC_x100(0));
  TEST_ASSERT_EQUAL_INT(13000, SHT3xDevice::convertTemperatureC_x100(65535));
  TEST_ASSERT_EQUAL_UINT32(0u, SHT3xDevice::convertHumidityPct_x100(0));
  TEST_ASSERT_EQUAL_UINT32(10000u, SHT3xDevice::convertHumidityPct_x100(65535));
}

void test_alert_limit_roundtrip() {
  const float tIn = 25.3f;
  const float rhIn = 47.8f;
  const uint16_t packed = SHT3xDevice::encodeAlertLimit(tIn, rhIn);
  float tOut = 0.0f;
  float rhOut = 0.0f;
  SHT3xDevice::decodeAlertLimit(packed, tOut, rhOut);
  TEST_ASSERT_FLOAT_WITHIN(0.6f, tIn, tOut);
  TEST_ASSERT_FLOAT_WITHIN(1.5f, rhIn, rhOut);
}

void test_time_elapsed_wrap() {
  TEST_ASSERT_FALSE(SHT3xDevice::_timeElapsed(5, 10));
  TEST_ASSERT_TRUE(SHT3xDevice::_timeElapsed(10, 10));
  TEST_ASSERT_TRUE(SHT3xDevice::_timeElapsed(10, 5));

  const uint32_t nearMax = 0xFFFFFFF0U;
  TEST_ASSERT_TRUE(SHT3xDevice::_timeElapsed(5, nearMax));
  TEST_ASSERT_FALSE(SHT3xDevice::_timeElapsed(nearMax, 5));
}

void test_command_delay_guard() {
  SHT3xDevice device;
  device._initialized = true;
  device._config.commandDelayMs = 1;
  device._config.i2cTimeoutMs = 1;
  gMicros = 0;
  gMicrosStep = 0;
  gMillis = 0;
  gMillisStep = 0;
  device._lastCommandUs = micros();
  device._lastCommandValid = true;
  Status st = device._ensureCommandDelay();
  TEST_ASSERT_EQUAL(Err::TIMEOUT, st.code);
}

struct FakeTransport {
  Status writeStatus = Status::Ok();
  Status writeReadStatus = Status::Ok();
};

static Status fakeWrite(uint8_t addr, const uint8_t* data, size_t len,
                        uint32_t timeoutMs, void* user) {
  (void)addr;
  (void)data;
  (void)len;
  (void)timeoutMs;
  auto* ctx = static_cast<FakeTransport*>(user);
  return ctx->writeStatus;
}

static Status fakeWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                            uint8_t* rxData, size_t rxLen, uint32_t timeoutMs,
                            void* user) {
  (void)addr;
  (void)txData;
  (void)txLen;
  (void)timeoutMs;
  auto* ctx = static_cast<FakeTransport*>(user);
  if (ctx->writeReadStatus.ok() && rxData != nullptr && rxLen == 6) {
    rxData[0] = 0x00;
    rxData[1] = 0x00;
    rxData[2] = SHT3xDevice::_crc8(&rxData[0], 2);
    rxData[3] = 0x00;
    rxData[4] = 0x00;
    rxData[5] = SHT3xDevice::_crc8(&rxData[3], 2);
  }
  return ctx->writeReadStatus;
}

struct ScriptedTransport {
  Status writeScript[8] = {};
  size_t writeCount = 0;
  size_t writeIdx = 0;
  Status readScript[8] = {};
  size_t readCount = 0;
  size_t readIdx = 0;
};

static Status scriptedWrite(uint8_t addr, const uint8_t* data, size_t len,
                            uint32_t timeoutMs, void* user) {
  (void)addr;
  (void)data;
  (void)len;
  (void)timeoutMs;
  auto* ctx = static_cast<ScriptedTransport*>(user);
  if (ctx->writeIdx < ctx->writeCount) {
    return ctx->writeScript[ctx->writeIdx++];
  }
  return Status::Ok();
}

static Status scriptedWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                                uint8_t* rxData, size_t rxLen, uint32_t timeoutMs,
                                void* user) {
  (void)addr;
  (void)txData;
  (void)txLen;
  (void)timeoutMs;
  auto* ctx = static_cast<ScriptedTransport*>(user);
  Status st = Status::Ok();
  if (ctx->readIdx < ctx->readCount) {
    st = ctx->readScript[ctx->readIdx++];
  }
  if (st.ok() && rxData != nullptr && rxLen == 3) {
    rxData[0] = 0x00;
    rxData[1] = 0x00;
    rxData[2] = SHT3xDevice::_crc8(&rxData[0], 2);
  }
  return st;
}

struct TimingTransport {
  SHT3xDevice* device = nullptr;
  uint32_t minDelayUs = 0;
  bool tooSoon = false;
  bool combinedUsed = false;
};

static Status timingWrite(uint8_t addr, const uint8_t* data, size_t len,
                          uint32_t timeoutMs, void* user) {
  (void)addr;
  (void)data;
  (void)len;
  (void)timeoutMs;
  (void)user;
  return Status::Ok();
}

static Status timingWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                              uint8_t* rxData, size_t rxLen, uint32_t timeoutMs,
                              void* user) {
  (void)addr;
  (void)txData;
  (void)timeoutMs;
  auto* ctx = static_cast<TimingTransport*>(user);
  if (txLen > 0 && rxLen > 0) {
    ctx->combinedUsed = true;
  }
  if (ctx->device != nullptr && ctx->device->_lastCommandUs != 0) {
    const uint32_t now = micros();
    const uint32_t delta = now - ctx->device->_lastCommandUs;
    if (delta < ctx->minDelayUs) {
      ctx->tooSoon = true;
    }
  }
  if (rxData != nullptr && rxLen >= 3) {
    rxData[0] = 0x00;
    rxData[1] = 0x00;
    rxData[2] = SHT3xDevice::_crc8(&rxData[0], 2);
    if (rxLen >= 6) {
      rxData[3] = 0x00;
      rxData[4] = 0x00;
      rxData[5] = SHT3xDevice::_crc8(&rxData[3], 2);
    }
  }
  return Status::Ok();
}

struct CountTransport {
  uint32_t writes = 0;
  uint32_t reads = 0;
};

static Status countWrite(uint8_t addr, const uint8_t* data, size_t len,
                         uint32_t timeoutMs, void* user) {
  (void)addr;
  (void)data;
  (void)len;
  (void)timeoutMs;
  auto* ctx = static_cast<CountTransport*>(user);
  ctx->writes++;
  return Status::Ok();
}

struct LogTransport {
  Status writeStatus = Status::Ok();
  Status writeReadStatus = Status::Ok();
  uint16_t commands[32] = {};
  size_t count = 0;
};

static Status logWrite(uint8_t addr, const uint8_t* data, size_t len,
                       uint32_t timeoutMs, void* user) {
  (void)addr;
  (void)timeoutMs;
  auto* ctx = static_cast<LogTransport*>(user);
  if (len >= 2 && ctx->count < 32) {
    const uint16_t cmd = static_cast<uint16_t>((data[0] << 8) | data[1]);
    ctx->commands[ctx->count++] = cmd;
  }
  return ctx->writeStatus;
}

static Status logWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                           uint8_t* rxData, size_t rxLen, uint32_t timeoutMs,
                           void* user) {
  (void)addr;
  (void)txData;
  (void)txLen;
  (void)timeoutMs;
  auto* ctx = static_cast<LogTransport*>(user);
  Status st = ctx->writeReadStatus;
  if (st.ok() && rxData != nullptr && rxLen == 3) {
    rxData[0] = 0x00;
    rxData[1] = 0x00;
    rxData[2] = SHT3xDevice::_crc8(&rxData[0], 2);
  }
  return st;
}

static Status countWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                             uint8_t* rxData, size_t rxLen, uint32_t timeoutMs,
                             void* user) {
  (void)addr;
  (void)txData;
  (void)txLen;
  (void)timeoutMs;
  auto* ctx = static_cast<CountTransport*>(user);
  ctx->reads++;
  if (rxData != nullptr && rxLen >= 3) {
    rxData[0] = 0x00;
    rxData[1] = 0x00;
    rxData[2] = SHT3xDevice::_crc8(&rxData[0], 2);
    if (rxLen >= 6) {
      rxData[3] = 0x00;
      rxData[4] = 0x00;
      rxData[5] = SHT3xDevice::_crc8(&rxData[3], 2);
    }
  }
  return Status::Ok();
}

void test_expected_nack_mapping() {
  FakeTransport ctx;
  ctx.writeReadStatus = Status::Error(Err::I2C_NACK_READ, "NACK read", 0);

  SHT3xDevice device;
  device._config.i2cWrite = fakeWrite;
  device._config.i2cWriteRead = fakeWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  device._config.commandDelayMs = 1;
  device._config.transportCapabilities = TransportCapability::READ_HEADER_NACK;
  device._initialized = true;
  device._driverState = DriverState::READY;
  device._consecutiveFailures = 0;
  device._lastBusActivityMs = 0;

  gMillis = 123;
  gMillisStep = 0;
  uint8_t buf[6] = {};
  Status st = device._i2cWriteReadTrackedAllowNoData(nullptr, 0, buf, sizeof(buf), true);
  TEST_ASSERT_EQUAL(Err::MEASUREMENT_NOT_READY, st.code);
  TEST_ASSERT_EQUAL_UINT8(0, device._consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT32(123u, device._lastBusActivityMs);
}

void test_not_ready_timeout_escalation() {
  FakeTransport ctx;
  ctx.writeStatus = Status::Ok();
  ctx.writeReadStatus = Status::Error(Err::I2C_NACK_READ, "NACK read", 0);

  SHT3xDevice device;
  device._config.i2cWrite = fakeWrite;
  device._config.i2cWriteRead = fakeWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  device._config.commandDelayMs = 1;
  device._config.notReadyTimeoutMs = 5;
  device._config.transportCapabilities = TransportCapability::READ_HEADER_NACK;
  device._initialized = true;
  device._driverState = DriverState::READY;
  device._periodicActive = true;
  device._periodMs = 100;
  device._notReadyStartMs = 1;

  gMillis = 10;
  gMicros = 0;
  gMillisStep = 1;
  gMicrosStep = 1000;
  Status st = device._fetchPeriodic();
  TEST_ASSERT_NOT_EQUAL(Err::MEASUREMENT_NOT_READY, st.code);
  TEST_ASSERT_TRUE(device._consecutiveFailures > 0);
}

void test_nack_mapping_without_capability() {
  FakeTransport ctx;
  ctx.writeReadStatus = Status::Error(Err::I2C_NACK_READ, "NACK read", 0);

  SHT3xDevice device;
  device._config.i2cWrite = fakeWrite;
  device._config.i2cWriteRead = fakeWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  device._config.commandDelayMs = 1;
  device._config.transportCapabilities = TransportCapability::NONE;
  device._initialized = true;
  device._driverState = DriverState::READY;

  gMillis = 50;
  gMillisStep = 0;
  uint8_t buf[6] = {};
  Status st = device._i2cWriteReadTrackedAllowNoData(nullptr, 0, buf, sizeof(buf), true);
  TEST_ASSERT_EQUAL(Err::I2C_ERROR, st.code);
}

void test_periodic_fetch_expected_nack_no_failure() {
  FakeTransport ctx;
  ctx.writeStatus = Status::Ok();
  ctx.writeReadStatus = Status::Error(Err::I2C_NACK_READ, "NACK read", 0);

  SHT3xDevice device;
  device._config.i2cWrite = fakeWrite;
  device._config.i2cWriteRead = fakeWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  device._config.commandDelayMs = 1;
  device._config.transportCapabilities = TransportCapability::READ_HEADER_NACK;
  device._initialized = true;
  device._driverState = DriverState::READY;
  device._periodicActive = true;
  device._periodMs = 100;
  device._consecutiveFailures = 0;

  gMillis = 0;
  gMicros = 0;
  gMillisStep = 1;
  gMicrosStep = 1000;
  Status st = device._fetchPeriodic();
  TEST_ASSERT_EQUAL(Err::MEASUREMENT_NOT_READY, st.code);
  TEST_ASSERT_EQUAL_UINT8(0, device._consecutiveFailures);
}

void test_example_adapter_ambiguous_zero_bytes() {
  Wire._setRequestFromResult(0);
  uint8_t buf[3] = {};
  Status st = transport::wireWriteRead(0x44, nullptr, 0, buf, sizeof(buf), 10, nullptr);
  TEST_ASSERT_EQUAL(Err::I2C_ERROR, st.code);
  Wire._clearRequestFromOverride();

  st = transport::wireWriteRead(0x44, buf, 1, buf, sizeof(buf), 10, nullptr);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);
}

void test_wire_adapter_timeout_and_stop() {
  Wire.setTimeOut(123);
  Wire._clearClockSetCount();
  uint8_t buf[2] = {0x00, 0x00};
  Status st = transport::wireWrite(0x44, buf, sizeof(buf), 33, nullptr);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(123u, Wire.getTimeOut());
  TEST_ASSERT_EQUAL_UINT32(0u, Wire._clockSetCount());
  TEST_ASSERT_TRUE(Wire._lastStopWasTrue());
}

void test_wire_adapter_drains_partial_read() {
  Wire.setTimeOut(123);
  Wire._setRequestFromResult(2);
  Wire._clearReadCallCount();
  uint8_t buf[6] = {};
  Status st = transport::wireWriteRead(0x44, nullptr, 0, buf, sizeof(buf), 20, nullptr);
  TEST_ASSERT_EQUAL(Err::I2C_ERROR, st.code);
  TEST_ASSERT_EQUAL_UINT32(123u, Wire.getTimeOut());
  TEST_ASSERT_EQUAL_UINT32(2u, Wire._readCallCount());
  Wire._clearRequestFromOverride();
}

void test_cache_updates_only_on_success() {
  LogTransport ctx;
  SHT3xDevice device;
  device._config.i2cWrite = logWrite;
  device._config.i2cWriteRead = logWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  device._initialized = true;
  device._driverState = DriverState::READY;
  device._cachedSettings = CachedSettings{};
  device._hasCachedSettings = true;

  Status st = device.setHeater(true);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(device._cachedSettings.heaterEnabled);

  ctx.writeStatus = Status::Error(Err::I2C_ERROR, "fail");
  st = device.setHeater(false);
  TEST_ASSERT_FALSE(st.ok());
  TEST_ASSERT_TRUE(device._cachedSettings.heaterEnabled);
}

void test_write_alert_limit_rejects_nan() {
  SHT3xDevice device;
  device._initialized = true;
  device._driverState = DriverState::READY;
  device._config.i2cWrite = fakeWrite;
  device._config.i2cWriteRead = fakeWriteRead;
  device._config.i2cTimeoutMs = 10;
  device._cachedSettings = CachedSettings{};
  device._hasCachedSettings = true;

  float nanValue = std::numeric_limits<float>::quiet_NaN();
  Status st = device.writeAlertLimit(AlertLimitKind::HIGH_SET, nanValue, 50.0f);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);
  TEST_ASSERT_FALSE(device._cachedSettings.alertValid[0]);
}

void test_reset_to_defaults_clears_cache() {
  LogTransport ctx;
  SHT3xDevice device;
  device._config.i2cWrite = logWrite;
  device._config.i2cWriteRead = logWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  device._config.recoverUseBusReset = false;
  device._config.recoverUseSoftReset = true;
  device._initialized = true;
  device._driverState = DriverState::READY;

  gMillis = 0;
  gMicros = 0;
  gMillisStep = 1;
  gMicrosStep = 1000;

  device._cachedSettings.mode = Mode::PERIODIC;
  device._cachedSettings.repeatability = Repeatability::LOW_REPEATABILITY;
  device._cachedSettings.periodicRate = PeriodicRate::MPS_4;
  device._cachedSettings.clockStretching = ClockStretching::STRETCH_ENABLED;
  device._cachedSettings.heaterEnabled = true;
  device._cachedSettings.alertValid[0] = true;
  device._cachedSettings.alertRaw[0] = 0x1234;
  device._hasCachedSettings = true;

  Status st = device.resetToDefaults();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL(Mode::SINGLE_SHOT, device._mode);
  TEST_ASSERT_FALSE(device._periodicActive);
  TEST_ASSERT_EQUAL(Mode::SINGLE_SHOT, device._cachedSettings.mode);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Repeatability::HIGH_REPEATABILITY),
                    static_cast<uint8_t>(device._cachedSettings.repeatability));
  TEST_ASSERT_FALSE(device._cachedSettings.heaterEnabled);
  TEST_ASSERT_FALSE(device._cachedSettings.alertValid[0]);
}

void test_reset_and_restore_applies_cached_settings() {
  LogTransport ctx;
  SHT3xDevice device;
  device._config.i2cWrite = logWrite;
  device._config.i2cWriteRead = logWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  device._config.recoverUseBusReset = false;
  device._config.recoverUseSoftReset = true;
  device._initialized = true;
  device._driverState = DriverState::READY;

  gMillis = 0;
  gMicros = 0;
  gMillisStep = 1;
  gMicrosStep = 1000;

  device._cachedSettings.mode = Mode::PERIODIC;
  device._cachedSettings.repeatability = Repeatability::MEDIUM_REPEATABILITY;
  device._cachedSettings.periodicRate = PeriodicRate::MPS_2;
  device._cachedSettings.clockStretching = ClockStretching::STRETCH_DISABLED;
  device._cachedSettings.heaterEnabled = true;
  device._cachedSettings.alertValid[static_cast<uint8_t>(AlertLimitKind::HIGH_SET)] = true;
  device._cachedSettings.alertRaw[static_cast<uint8_t>(AlertLimitKind::HIGH_SET)] = 0x2222;
  device._hasCachedSettings = true;

  Status st = device.resetAndRestore();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(device._periodicActive);
  TEST_ASSERT_EQUAL(Mode::PERIODIC, device._mode);

  const uint16_t periodicCmd = SHT3xDevice::_commandForPeriodic(
      device._cachedSettings.repeatability,
      device._cachedSettings.periodicRate);
  bool sawAlert = false;
  bool sawPeriodic = false;
  size_t alertIdx = 0;
  size_t periodicIdx = 0;
  for (size_t i = 0; i < ctx.count; ++i) {
    if (ctx.commands[i] == cmd::CMD_ALERT_WRITE_HIGH_SET) {
      sawAlert = true;
      alertIdx = i;
    }
    if (ctx.commands[i] == periodicCmd) {
      sawPeriodic = true;
      periodicIdx = i;
    }
  }
  TEST_ASSERT_TRUE(sawAlert);
  TEST_ASSERT_TRUE(sawPeriodic);
  TEST_ASSERT_TRUE(alertIdx < periodicIdx);
}

void test_setters_restart_art_mode() {
  LogTransport ctx;
  SHT3xDevice device;
  device._config.i2cWrite = logWrite;
  device._config.i2cWriteRead = logWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  device._initialized = true;
  device._driverState = DriverState::READY;
  device._mode = Mode::ART;
  device._periodicActive = true;

  gMillis = 0;
  gMicros = 0;
  gMillisStep = 1;
  gMicrosStep = 1000;

  ctx.count = 0;
  Status st = device.setRepeatability(Repeatability::LOW_REPEATABILITY);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(ctx.count > 0);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_ART, ctx.commands[ctx.count - 1]);

  ctx.count = 0;
  st = device.setPeriodicRate(PeriodicRate::MPS_10);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(ctx.count > 0);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_ART, ctx.commands[ctx.count - 1]);
}

void test_read_paths_no_combined_and_respect_delay() {
  TimingTransport ctx;
  SHT3xDevice device;
  device._config.i2cWrite = timingWrite;
  device._config.i2cWriteRead = timingWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  device._config.commandDelayMs = 2;
  device._initialized = true;
  device._driverState = DriverState::READY;
  ctx.device = &device;
  ctx.minDelayUs = static_cast<uint32_t>(device._config.commandDelayMs) * 1000U;

  gMillis = 0;
  gMicros = 0;
  gMillisStep = 0;
  gMicrosStep = 1000;

  uint16_t statusRaw = 0;
  ctx.tooSoon = false;
  ctx.combinedUsed = false;
  Status st = device.readStatus(statusRaw);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(ctx.combinedUsed);
  TEST_ASSERT_FALSE(ctx.tooSoon);

  uint32_t serial = 0;
  ctx.tooSoon = false;
  ctx.combinedUsed = false;
  st = device.readSerialNumber(serial, ClockStretching::STRETCH_DISABLED);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(ctx.combinedUsed);
  TEST_ASSERT_FALSE(ctx.tooSoon);

  uint16_t alertRaw = 0;
  ctx.tooSoon = false;
  ctx.combinedUsed = false;
  st = device.readAlertLimitRaw(AlertLimitKind::HIGH_SET, alertRaw);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(ctx.combinedUsed);
  TEST_ASSERT_FALSE(ctx.tooSoon);
}

void test_periodic_fetch_margin_blocks_early_fetch() {
  CountTransport ctx;
  SHT3xDevice device;
  device._config.i2cWrite = countWrite;
  device._config.i2cWriteRead = countWriteRead;
  device._config.i2cUser = &ctx;
  device._config.commandDelayMs = 1;
  device._config.periodicFetchMarginMs = 0;
  device._config.transportCapabilities = TransportCapability::NONE;
  device._initialized = true;
  device._driverState = DriverState::READY;
  device._mode = Mode::PERIODIC;
  device._periodicActive = true;
  device._periodMs = 100;
  device._periodicStartMs = 0;
  device._lastFetchMs = 0;

  gMillis = 0;
  gMicros = 0;
  gMillisStep = 0;
  gMicrosStep = 1000;

  Status st = device.requestMeasurement();
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);

  const uint32_t margin = device._periodicFetchMarginMs();
  const uint32_t expectedReady =
      device._periodicStartMs + device.estimateMeasurementTimeMs() + margin;
  TEST_ASSERT_EQUAL_UINT32(expectedReady, device._measurementReadyMs);

  device.tick(expectedReady - 1);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);

  device.tick(expectedReady);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes);
}

void test_recover_transient_failure() {
  ScriptedTransport ctx;
  ctx.readScript[0] = Status::Error(Err::I2C_TIMEOUT, "timeout");
  ctx.readScript[1] = Status::Ok();
  ctx.readCount = 2;

  auto busReset = [](void* user) -> Status {
    (void)user;
    return Status::Ok();
  };

  SHT3xDevice device;
  device._config.i2cWrite = scriptedWrite;
  device._config.i2cWriteRead = scriptedWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  device._config.recoverBackoffMs = 0;
  device._config.busReset = busReset;
  device._config.recoverUseBusReset = true;
  device._config.recoverUseSoftReset = true;
  device._initialized = true;
  device._driverState = DriverState::READY;

  gMillis = 0;
  gMicros = 0;
  gMillisStep = 1;
  gMicrosStep = 1000;
  Status st = device.recover();
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL(Mode::SINGLE_SHOT, device._mode);
  TEST_ASSERT_FALSE(device._periodicActive);
}

void test_recover_permanent_offline() {
  ScriptedTransport ctx;
  ctx.readScript[0] = Status::Error(Err::I2C_TIMEOUT, "timeout");
  ctx.readScript[1] = Status::Error(Err::I2C_TIMEOUT, "timeout");
  ctx.readScript[2] = Status::Error(Err::I2C_TIMEOUT, "timeout");
  ctx.readCount = 3;

  SHT3xDevice device;
  device._config.i2cWrite = scriptedWrite;
  device._config.i2cWriteRead = scriptedWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  device._config.recoverBackoffMs = 0;
  device._config.recoverUseBusReset = false;
  device._config.recoverUseSoftReset = true;
  device._initialized = true;
  device._driverState = DriverState::READY;

  gMillis = 0;
  gMicros = 0;
  gMillisStep = 1;
  gMicrosStep = 1000;
  Status st = device.recover();
  TEST_ASSERT_FALSE(st.ok());
  TEST_ASSERT_TRUE(device._consecutiveFailures > 0);
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_status_ok);
  RUN_TEST(test_status_error);
  RUN_TEST(test_status_in_progress);
  RUN_TEST(test_config_defaults);
  RUN_TEST(test_crc8_example);
  RUN_TEST(test_conversions_basic);
  RUN_TEST(test_alert_limit_roundtrip);
  RUN_TEST(test_time_elapsed_wrap);
  RUN_TEST(test_command_delay_guard);
  RUN_TEST(test_expected_nack_mapping);
  RUN_TEST(test_not_ready_timeout_escalation);
  RUN_TEST(test_nack_mapping_without_capability);
  RUN_TEST(test_periodic_fetch_expected_nack_no_failure);
  RUN_TEST(test_example_adapter_ambiguous_zero_bytes);
  RUN_TEST(test_wire_adapter_timeout_and_stop);
  RUN_TEST(test_wire_adapter_drains_partial_read);
  RUN_TEST(test_cache_updates_only_on_success);
  RUN_TEST(test_write_alert_limit_rejects_nan);
  RUN_TEST(test_reset_to_defaults_clears_cache);
  RUN_TEST(test_reset_and_restore_applies_cached_settings);
  RUN_TEST(test_setters_restart_art_mode);
  RUN_TEST(test_read_paths_no_combined_and_respect_delay);
  RUN_TEST(test_periodic_fetch_margin_blocks_early_fetch);
  RUN_TEST(test_recover_transient_failure);
  RUN_TEST(test_recover_permanent_offline);
  return UNITY_END();
}
