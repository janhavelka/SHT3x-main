/// @file test_basic.cpp
/// @brief Basic unit tests for SHT3x driver

#include <unity.h>
#include <type_traits>

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

static_assert(!std::is_copy_constructible<SHT3xDevice>::value,
              "SHT3x must not be copy constructible");
static_assert(!std::is_copy_assignable<SHT3xDevice>::value,
              "SHT3x must not be copy assignable");
static_assert(!std::is_move_constructible<SHT3xDevice>::value,
              "SHT3x must not be move constructible");
static_assert(!std::is_move_assignable<SHT3xDevice>::value,
              "SHT3x must not be move assignable");

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
  TEST_ASSERT_TRUE(st.inProgress());
}

void test_status_helpers() {
  Status ok = Status::Ok();
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE(ok.is(Err::OK));
  TEST_ASSERT_FALSE(ok.is(Err::TIMEOUT));

  Status err = Status::Error(Err::I2C_ERROR, "Test error", 42);
  TEST_ASSERT_FALSE(err);
  TEST_ASSERT_TRUE(err.is(Err::I2C_ERROR));
}

void test_config_defaults() {
  Config cfg;
  TEST_ASSERT_EQUAL(nullptr, cfg.i2cWrite);
  TEST_ASSERT_EQUAL(nullptr, cfg.i2cWriteRead);
  TEST_ASSERT_EQUAL(nullptr, cfg.busReset);
  TEST_ASSERT_EQUAL(nullptr, cfg.hardReset);
  TEST_ASSERT_EQUAL(nullptr, cfg.nowMs);
  TEST_ASSERT_EQUAL(nullptr, cfg.nowUs);
  TEST_ASSERT_EQUAL(nullptr, cfg.cooperativeYield);
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
  uint32_t nowMs = 1000;
  Status writeStatus = Status::Ok();
  Status writeReadStatus = Status::Ok();
};

static Status fakeWrite(uint8_t, const uint8_t*, size_t, uint32_t, void* user) {
  auto* ctx = static_cast<FakeTransport*>(user);
  return ctx->writeStatus;
}

static Status fakeWriteRead(uint8_t, const uint8_t* txData, size_t txLen,
                            uint8_t* rxData, size_t rxLen, uint32_t, void* user) {
  auto* ctx = static_cast<FakeTransport*>(user);
  (void)txData;
  (void)txLen;
  if (ctx->writeReadStatus.ok() && rxData != nullptr && rxLen >= 3) {
    rxData[0] = 0x00;
    rxData[1] = 0x00;
    rxData[2] = SHT3xDevice::_crc8(&rxData[0], 2);
    if (rxLen >= 6) {
      rxData[3] = 0x00;
      rxData[4] = 0x00;
      rxData[5] = SHT3xDevice::_crc8(&rxData[3], 2);
    }
  }
  return ctx->writeReadStatus;
}

static uint32_t fakeNowMs(void* user) {
  return static_cast<FakeTransport*>(user)->nowMs;
}

static uint32_t fakeNowUs(void* user) {
  return static_cast<FakeTransport*>(user)->nowMs * 1000U;
}

static void fakeYield(void* user) {
  static_cast<FakeTransport*>(user)->nowMs++;
}

static uint32_t stubNowMs(void*) {
  return millis();
}

static uint32_t stubNowUs(void*) {
  return micros();
}

static void stubYield(void*) {
  yield();
}

static void installTimingHooks(SHT3xDevice& device) {
  device._config.nowMs = stubNowMs;
  device._config.nowUs = stubNowUs;
  device._config.cooperativeYield = stubYield;
  device._config.timeUser = nullptr;
}

static Config makeConfig(FakeTransport& bus) {
  Config cfg;
  cfg.i2cWrite = fakeWrite;
  cfg.i2cWriteRead = fakeWriteRead;
  cfg.i2cUser = &bus;
  cfg.nowMs = fakeNowMs;
  cfg.nowUs = fakeNowUs;
  cfg.cooperativeYield = fakeYield;
  cfg.timeUser = &bus;
  cfg.i2cTimeoutMs = 10;
  cfg.offlineThreshold = 3;
  cfg.mode = Mode::SINGLE_SHOT;
  return cfg;
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

struct CommandCaptureTransport {
  uint32_t nowMs = 0;
  Status writeStatus = Status::Ok();
  Status writeReadStatus = Status::Ok();
  uint8_t lastWrite[8] = {};
  size_t lastWriteLen = 0;
  uint8_t lastReadTx[8] = {};
  size_t lastReadTxLen = 0;
  uint8_t lastReadRx[8] = {};
  size_t lastReadLen = 0;
};

static Status captureWrite(uint8_t addr, const uint8_t* data, size_t len,
                           uint32_t timeoutMs, void* user) {
  (void)addr;
  (void)timeoutMs;
  auto* ctx = static_cast<CommandCaptureTransport*>(user);
  ctx->lastWriteLen = (len > sizeof(ctx->lastWrite)) ? sizeof(ctx->lastWrite) : len;
  for (size_t i = 0; i < ctx->lastWriteLen; ++i) {
    ctx->lastWrite[i] = data[i];
  }
  return ctx->writeStatus;
}

static Status captureWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                               uint8_t* rxData, size_t rxLen, uint32_t timeoutMs,
                               void* user) {
  (void)addr;
  (void)timeoutMs;
  auto* ctx = static_cast<CommandCaptureTransport*>(user);
  ctx->lastReadTxLen = (txLen > sizeof(ctx->lastReadTx)) ? sizeof(ctx->lastReadTx) : txLen;
  for (size_t i = 0; i < ctx->lastReadTxLen; ++i) {
    ctx->lastReadTx[i] = txData[i];
  }
  ctx->lastReadLen = (rxLen > sizeof(ctx->lastReadRx)) ? sizeof(ctx->lastReadRx) : rxLen;
  Status st = ctx->writeReadStatus;
  if (st.ok() && rxData != nullptr) {
    for (size_t i = 0; i < ctx->lastReadLen; ++i) {
      rxData[i] = static_cast<uint8_t>(0xA0U + static_cast<uint8_t>(i));
      ctx->lastReadRx[i] = rxData[i];
    }
    if (ctx->lastReadLen == 3) {
      rxData[2] = SHT3xDevice::_crc8(&rxData[0], 2);
      ctx->lastReadRx[2] = rxData[2];
    } else if (ctx->lastReadLen == 6) {
      rxData[2] = SHT3xDevice::_crc8(&rxData[0], 2);
      rxData[5] = SHT3xDevice::_crc8(&rxData[3], 2);
      ctx->lastReadRx[2] = rxData[2];
      ctx->lastReadRx[5] = rxData[5];
    }
  }
  return st;
}

static uint32_t captureNowMs(void* user) {
  return static_cast<CommandCaptureTransport*>(user)->nowMs;
}

static uint32_t captureNowUs(void* user) {
  return static_cast<CommandCaptureTransport*>(user)->nowMs * 1000U;
}

static void captureYield(void* user) {
  static_cast<CommandCaptureTransport*>(user)->nowMs++;
}

struct FrameScriptTransport {
  uint32_t nowMs = 1000;
  Status writeStatus = Status::Ok();
  Status readStatus = Status::Ok();
  uint16_t failCommand = 0;
  Status failCommandStatus = Status::Ok();
  size_t failCommandOccurrence = 1;
  size_t failCommandSeen = 0;
  size_t failReadIndex = 0;
  Status failReadStatus = Status::Ok();
  uint16_t statusRaw = 0;
  bool corruptStatusCrc = false;
  uint16_t commands[40] = {};
  size_t commandCount = 0;
  uint32_t writes = 0;
  uint32_t reads = 0;
  uint16_t lastCommand = 0;
};

static Status frameWrite(uint8_t addr, const uint8_t* data, size_t len,
                         uint32_t timeoutMs, void* user) {
  (void)addr;
  (void)timeoutMs;
  auto* ctx = static_cast<FrameScriptTransport*>(user);
  ctx->writes++;
  uint16_t command = 0;
  if (data != nullptr && len >= 2) {
    command = static_cast<uint16_t>((data[0] << 8) | data[1]);
    ctx->lastCommand = command;
    if (ctx->commandCount < 40) {
      ctx->commands[ctx->commandCount++] = command;
    }
  }
  if (ctx->failCommand != 0 && command == ctx->failCommand) {
    ctx->failCommandSeen++;
    if (ctx->failCommandSeen == ctx->failCommandOccurrence) {
      return ctx->failCommandStatus;
    }
  }
  return ctx->writeStatus;
}

static Status frameWriteRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                             uint8_t* rxData, size_t rxLen, uint32_t timeoutMs,
                             void* user) {
  (void)addr;
  (void)txData;
  (void)txLen;
  (void)timeoutMs;
  auto* ctx = static_cast<FrameScriptTransport*>(user);
  ctx->reads++;
  if (ctx->failReadIndex != 0 && ctx->reads == ctx->failReadIndex) {
    return ctx->failReadStatus;
  }
  Status st = ctx->readStatus;
  if (!st.ok() || rxData == nullptr) {
    return st;
  }

  if (rxLen == cmd::STATUS_DATA_LEN) {
    rxData[0] = static_cast<uint8_t>(ctx->statusRaw >> 8);
    rxData[1] = static_cast<uint8_t>(ctx->statusRaw & 0xFF);
    rxData[2] = SHT3xDevice::_crc8(&rxData[0], 2);
    if (ctx->corruptStatusCrc) {
      rxData[2] ^= 0x01;
    }
  } else if (rxLen == cmd::MEASUREMENT_DATA_LEN) {
    rxData[0] = 0x00;
    rxData[1] = 0x00;
    rxData[2] = SHT3xDevice::_crc8(&rxData[0], 2);
    rxData[3] = 0x00;
    rxData[4] = 0x00;
    rxData[5] = SHT3xDevice::_crc8(&rxData[3], 2);
  }
  return st;
}

static uint32_t frameNowMs(void* user) {
  return static_cast<FrameScriptTransport*>(user)->nowMs;
}

static uint32_t frameNowUs(void* user) {
  return static_cast<FrameScriptTransport*>(user)->nowMs * 1000U;
}

static void frameYield(void* user) {
  static_cast<FrameScriptTransport*>(user)->nowMs++;
}

static Config makeFrameConfig(FrameScriptTransport& ctx) {
  Config cfg;
  cfg.i2cWrite = frameWrite;
  cfg.i2cWriteRead = frameWriteRead;
  cfg.i2cUser = &ctx;
  cfg.nowMs = frameNowMs;
  cfg.nowUs = frameNowUs;
  cfg.cooperativeYield = frameYield;
  cfg.timeUser = &ctx;
  cfg.i2cTimeoutMs = 10;
  cfg.commandDelayMs = 1;
  cfg.offlineThreshold = 3;
  return cfg;
}

static void clearFrameLog(FrameScriptTransport& ctx) {
  ctx.commandCount = 0;
  ctx.writes = 0;
  ctx.reads = 0;
  ctx.lastCommand = 0;
  ctx.failCommandSeen = 0;
  for (size_t i = 0; i < 40; ++i) {
    ctx.commands[i] = 0;
  }
}

static Status frameBusResetFail(void*) {
  return Status::Error(Err::I2C_BUS, "bus reset failed", -88);
}

void test_get_settings_snapshot_fields() {
  FakeTransport bus;
  SHT3xDevice device;
  Config cfg = makeConfig(bus);
  cfg.i2cTimeoutMs = 77;
  cfg.offlineThreshold = 4;
  gMillis = 0;
  gMicros = 0;
  gMillisStep = 1;
  gMicrosStep = 1000;
  Status beginSt = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(beginSt.ok(), beginSt.msg);

  SettingsSnapshot snap;
  Status st = device.getSettings(snap);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(snap.initialized);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DriverState::READY), static_cast<uint8_t>(snap.state));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(DriverState::READY),
                    static_cast<uint8_t>(device.driverState()));
  TEST_ASSERT_EQUAL_HEX8(0x44, snap.i2cAddress);
  TEST_ASSERT_EQUAL_UINT32(77u, snap.i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(4u, snap.offlineThreshold);
  TEST_ASSERT_TRUE(snap.hasNowMsHook);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Mode::SINGLE_SHOT), static_cast<uint8_t>(snap.mode));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Repeatability::HIGH_REPEATABILITY),
                    static_cast<uint8_t>(snap.repeatability));
  TEST_ASSERT_FALSE(snap.periodicActive);
  TEST_ASSERT_FALSE(snap.measurementPending);
  TEST_ASSERT_FALSE(snap.measurementReady);
  TEST_ASSERT_FALSE(snap.hasSample);
  TEST_ASSERT_FALSE(device.hasSample());
  TEST_ASSERT_EQUAL_UINT32(0u, snap.measurementReadyMs);
  TEST_ASSERT_EQUAL_UINT32(0u, snap.sampleTimestampMs);
  TEST_ASSERT_EQUAL_UINT32(0u, snap.missedSamples);
  TEST_ASSERT_FALSE(snap.statusValid);
  TEST_ASSERT_EQUAL(Err::UNSUPPORTED, snap.statusReadStatus.code);
}

void test_begin_resets_invalid_config_to_uninit_defaults_and_no_startup_health() {
  FakeTransport bus;
  SHT3xDevice device;
  Config cfg = makeConfig(bus);
  cfg.offlineThreshold = 4;

  TEST_ASSERT_TRUE(device.begin(cfg).ok());
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(0u, device.lastOkMs());
  TEST_ASSERT_EQUAL_UINT32(0u, device.lastBusActivityMs());

  Status pending = device._updateHealth(Status{Err::IN_PROGRESS, 0, "pending"});
  TEST_ASSERT_TRUE(pending.inProgress());
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(0u, device.lastBusActivityMs());

  (void)device._updateHealth(Status::Error(Err::I2C_TIMEOUT, "forced stale error"));
  device._allowOfflineI2c = true;

  Config invalid = cfg;
  invalid.i2cTimeoutMs = 0;
  const Status st = device.begin(invalid);
  TEST_ASSERT_EQUAL(Err::INVALID_CONFIG, st.code);
  TEST_ASSERT_FALSE(device.isInitialized());
  TEST_ASSERT_EQUAL(DriverState::UNINIT, device.state());
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(0u, device.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(0u, device.lastOkMs());
  TEST_ASSERT_EQUAL_UINT32(0u, device.lastErrorMs());
  TEST_ASSERT_EQUAL_UINT32(0u, device.lastBusActivityMs());
  TEST_ASSERT_EQUAL(Err::OK, device.lastError().code);
  TEST_ASSERT_FALSE(device._allowOfflineI2c);
  TEST_ASSERT_EQUAL(nullptr, device.getConfig().i2cWrite);
  TEST_ASSERT_EQUAL(nullptr, device.getConfig().i2cWriteRead);
  TEST_ASSERT_EQUAL_UINT32(50u, device.getConfig().i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(5u, device.getConfig().offlineThreshold);
}

void test_begin_i2c_failure_does_not_update_health() {
  FakeTransport bus;
  SHT3xDevice device;
  Config cfg = makeConfig(bus);
  bus.writeReadStatus = Status::Error(Err::I2C_TIMEOUT, "startup read timeout", -44);

  const Status st = device.begin(cfg);
  TEST_ASSERT_EQUAL(Err::DEVICE_NOT_FOUND, st.code);
  TEST_ASSERT_FALSE(device.isInitialized());
  TEST_ASSERT_EQUAL(DriverState::UNINIT, device.state());
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(0u, device.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(0u, device.lastOkMs());
  TEST_ASSERT_EQUAL_UINT32(0u, device.lastErrorMs());
  TEST_ASSERT_EQUAL_UINT32(0u, device.lastBusActivityMs());
}

void test_begin_rejects_missing_timing_hooks_before_i2c() {
  CountTransport ctx;
  Config cfg;
  cfg.i2cWrite = countWrite;
  cfg.i2cWriteRead = countWriteRead;
  cfg.i2cUser = &ctx;
  cfg.i2cTimeoutMs = 10;

  SHT3xDevice device;
  Status st = device.begin(cfg);
  TEST_ASSERT_EQUAL(Err::INVALID_CONFIG, st.code);
  TEST_ASSERT_EQUAL_STRING("Timing callbacks not set", st.msg);
  TEST_ASSERT_FALSE(device.isInitialized());
  TEST_ASSERT_EQUAL(DriverState::UNINIT, device.state());
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  cfg.nowMs = stubNowMs;
  st = device.begin(cfg);
  TEST_ASSERT_EQUAL(Err::INVALID_CONFIG, st.code);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  cfg.nowUs = stubNowUs;
  st = device.begin(cfg);
  TEST_ASSERT_EQUAL(Err::INVALID_CONFIG, st.code);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
}

void test_begin_rejects_oversized_timing_config() {
  FakeTransport ctx;
  Config cfg = makeConfig(ctx);

  SHT3xDevice device;
  cfg.i2cTimeoutMs = 60001;
  Status st = device.begin(cfg);
  TEST_ASSERT_EQUAL(Err::INVALID_CONFIG, st.code);

  cfg.i2cTimeoutMs = 50;
  cfg.commandDelayMs = 1001;
  st = device.begin(cfg);
  TEST_ASSERT_EQUAL(Err::INVALID_CONFIG, st.code);

  cfg.commandDelayMs = 1;
  cfg.notReadyTimeoutMs = 600001;
  st = device.begin(cfg);
  TEST_ASSERT_EQUAL(Err::INVALID_CONFIG, st.code);

  cfg.notReadyTimeoutMs = 0;
  cfg.periodicFetchMarginMs = 60001;
  st = device.begin(cfg);
  TEST_ASSERT_EQUAL(Err::INVALID_CONFIG, st.code);

  cfg.periodicFetchMarginMs = 0;
  cfg.recoverBackoffMs = 600001;
  st = device.begin(cfg);
  TEST_ASSERT_EQUAL(Err::INVALID_CONFIG, st.code);
}

void test_single_shot_pending_blocks_unrelated_commands() {
  CountTransport ctx;
  SHT3xDevice device;
  device._config.i2cWrite = countWrite;
  device._config.i2cWriteRead = countWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  installTimingHooks(device);
  device._initialized = true;
  device._driverState = DriverState::READY;
  device._mode = Mode::SINGLE_SHOT;
  device._measurementRequested = true;
  device._measurementReady = false;
  device._periodicActive = false;

  uint16_t statusRaw = 0;
  Status st = device.readStatus(statusRaw);
  TEST_ASSERT_EQUAL(Err::BUSY, st.code);

  uint32_t serial = 0;
  st = device.readSerialNumber(serial);
  TEST_ASSERT_EQUAL(Err::BUSY, st.code);

  st = device.startPeriodic(PeriodicRate::MPS_1, Repeatability::HIGH_REPEATABILITY);
  TEST_ASSERT_EQUAL(Err::BUSY, st.code);

  st = device.probe();
  TEST_ASSERT_EQUAL(Err::BUSY, st.code);

  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
}

void test_low_level_command_helpers_write_and_read() {
  CommandCaptureTransport ctx;
  SHT3xDevice device;
  Config cfg;
  cfg.i2cWrite = captureWrite;
  cfg.i2cWriteRead = captureWriteRead;
  cfg.i2cUser = &ctx;
  cfg.nowMs = captureNowMs;
  cfg.nowUs = captureNowUs;
  cfg.cooperativeYield = captureYield;
  cfg.timeUser = &ctx;
  cfg.i2cTimeoutMs = 10;
  cfg.commandDelayMs = 1;
  cfg.offlineThreshold = 3;

  gMillis = 0;
  gMicros = 0;
  gMillisStep = 1;
  gMicrosStep = 1000;

  Status beginSt = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(beginSt.ok(), beginSt.msg);

  ctx.lastWriteLen = 0;
  ctx.lastReadTxLen = 0;
  ctx.lastReadLen = 0;

  Status st = device.writeCommand(cmd::CMD_CLEAR_STATUS);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(2u, ctx.lastWriteLen);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(cmd::CMD_CLEAR_STATUS >> 8), ctx.lastWrite[0]);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(cmd::CMD_CLEAR_STATUS & 0xFF), ctx.lastWrite[1]);

  ctx.lastWriteLen = 0;
  st = device.writeCommandWithData(cmd::CMD_ALERT_WRITE_HIGH_SET, 0x1234);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(5u, ctx.lastWriteLen);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(cmd::CMD_ALERT_WRITE_HIGH_SET >> 8), ctx.lastWrite[0]);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(cmd::CMD_ALERT_WRITE_HIGH_SET & 0xFF), ctx.lastWrite[1]);
  TEST_ASSERT_EQUAL_HEX8(0x12, ctx.lastWrite[2]);
  TEST_ASSERT_EQUAL_HEX8(0x34, ctx.lastWrite[3]);
  TEST_ASSERT_EQUAL_HEX8(SHT3xDevice::_crc8(&ctx.lastWrite[2], 2), ctx.lastWrite[4]);

  ctx.lastWriteLen = 0;
  ctx.lastReadTxLen = 0;
  ctx.lastReadLen = 0;
  uint8_t buf[3] = {};
  st = device.readCommand(cmd::CMD_READ_STATUS, buf, sizeof(buf));
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(2u, ctx.lastWriteLen);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(cmd::CMD_READ_STATUS >> 8), ctx.lastWrite[0]);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(cmd::CMD_READ_STATUS & 0xFF), ctx.lastWrite[1]);
  TEST_ASSERT_EQUAL_UINT8(0u, ctx.lastReadTxLen);
  TEST_ASSERT_EQUAL_UINT8(3u, ctx.lastReadLen);
  TEST_ASSERT_EQUAL_HEX8(SHT3xDevice::_crc8(&buf[0], 2), buf[2]);
}

void test_low_level_command_helpers_map_expected_nack() {
  CommandCaptureTransport ctx;

  SHT3xDevice device;
  Config cfg;
  cfg.i2cWrite = captureWrite;
  cfg.i2cWriteRead = captureWriteRead;
  cfg.i2cUser = &ctx;
  cfg.nowMs = captureNowMs;
  cfg.nowUs = captureNowUs;
  cfg.cooperativeYield = captureYield;
  cfg.timeUser = &ctx;
  cfg.i2cTimeoutMs = 10;
  cfg.commandDelayMs = 1;
  cfg.offlineThreshold = 3;
  cfg.transportCapabilities = TransportCapability::READ_HEADER_NACK;

  gMillis = 0;
  gMicros = 0;
  gMillisStep = 1;
  gMicrosStep = 1000;

  Status beginSt = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(beginSt.ok(), beginSt.msg);

  ctx.writeReadStatus = Status::Error(Err::I2C_NACK_READ, "NACK read", 0);
  ctx.lastWriteLen = 0;
  ctx.lastReadTxLen = 0;
  ctx.lastReadLen = 0;

  uint8_t buf[6] = {};
  Status st = device.readCommand(cmd::CMD_FETCH_DATA, buf, sizeof(buf), true);
  TEST_ASSERT_EQUAL(Err::MEASUREMENT_NOT_READY, st.code);
  TEST_ASSERT_EQUAL_UINT8(2u, ctx.lastWriteLen);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(cmd::CMD_FETCH_DATA >> 8), ctx.lastWrite[0]);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(cmd::CMD_FETCH_DATA & 0xFF), ctx.lastWrite[1]);
  TEST_ASSERT_EQUAL_UINT8(0u, ctx.lastReadTxLen);
  TEST_ASSERT_EQUAL_UINT32(0u, device.consecutiveFailures());
  TEST_ASSERT_EQUAL(DriverState::READY, device.state());
}

void test_low_level_command_helpers_block_pending_measurement() {
  CountTransport ctx;
  SHT3xDevice device;
  device._config.i2cWrite = countWrite;
  device._config.i2cWriteRead = countWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  installTimingHooks(device);
  device._initialized = true;
  device._driverState = DriverState::READY;
  device._mode = Mode::SINGLE_SHOT;
  device._measurementRequested = true;
  device._measurementReady = false;

  Status st = device.writeCommand(cmd::CMD_CLEAR_STATUS);
  TEST_ASSERT_EQUAL(Err::BUSY, st.code);

  st = device.writeCommandWithData(cmd::CMD_ALERT_WRITE_HIGH_SET, 0x1234);
  TEST_ASSERT_EQUAL(Err::BUSY, st.code);

  uint8_t buf[3] = {};
  st = device.readCommand(cmd::CMD_READ_STATUS, buf, sizeof(buf));
  TEST_ASSERT_EQUAL(Err::BUSY, st.code);

  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
}

void test_low_level_read_rejects_invalid_buffers_before_i2c() {
  CountTransport ctx;
  SHT3xDevice device;
  device._config.i2cWrite = countWrite;
  device._config.i2cWriteRead = countWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  installTimingHooks(device);
  device._initialized = true;
  device._driverState = DriverState::READY;

  uint8_t oversized[cmd::MEASUREMENT_DATA_LEN + 1] = {};
  Status st = device.readCommand(cmd::CMD_READ_STATUS, nullptr, 3);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);

  st = device.readCommand(cmd::CMD_READ_STATUS, oversized, 0);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);

  st = device.readCommand(cmd::CMD_READ_STATUS, oversized, sizeof(oversized));
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);

  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalFailures());
}

void test_raw_transport_rejects_invalid_buffers() {
  FakeTransport ctx;
  SHT3xDevice device;
  device._config.i2cWrite = fakeWrite;
  device._config.i2cWriteRead = fakeWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  installTimingHooks(device);

  uint8_t byte = 0x00;
  uint8_t rx[3] = {};

  Status st = device._i2cWriteRaw(nullptr, 1);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);

  st = device._i2cWriteRaw(&byte, 0);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);

  st = device._i2cWriteReadRaw(nullptr, 1, rx, 0);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);

  st = device._i2cWriteReadRaw(nullptr, 0, nullptr, 1);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);

  st = device._i2cWriteReadRaw(nullptr, 0, nullptr, 0);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);
}

void test_expected_nack_mapping() {
  FakeTransport ctx;
  ctx.writeReadStatus = Status::Error(Err::I2C_NACK_READ, "NACK read", 0);

  SHT3xDevice device;
  device._config.i2cWrite = fakeWrite;
  device._config.i2cWriteRead = fakeWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  installTimingHooks(device);
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
  installTimingHooks(device);
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
  installTimingHooks(device);
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
  installTimingHooks(device);
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
  Status st = transport::wireWriteRead(0x44, nullptr, 0, buf, sizeof(buf), 10, &Wire);
  TEST_ASSERT_EQUAL(Err::I2C_ERROR, st.code);
  Wire._clearRequestFromOverride();

  st = transport::wireWriteRead(0x44, buf, 1, buf, sizeof(buf), 10, &Wire);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);
}

void test_wire_adapter_timeout_and_stop() {
  Wire.setTimeOut(123);
  Wire._clearClockSetCount();
  uint8_t buf[2] = {0x00, 0x00};
  Status st = transport::wireWrite(0x44, buf, sizeof(buf), 33, &Wire);
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
  Status st = transport::wireWriteRead(0x44, nullptr, 0, buf, sizeof(buf), 20, &Wire);
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
  installTimingHooks(device);
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
  installTimingHooks(device);
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
  installTimingHooks(device);
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
  installTimingHooks(device);
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
  installTimingHooks(device);
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

void test_periodic_setters_do_not_mutate_config_on_restart_failure() {
  LogTransport ctx;
  ctx.writeStatus = Status::Error(Err::I2C_ERROR, "forced failure");

  SHT3xDevice device;
  device._config.i2cWrite = logWrite;
  device._config.i2cWriteRead = logWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  installTimingHooks(device);
  device._config.repeatability = Repeatability::HIGH_REPEATABILITY;
  device._config.periodicRate = PeriodicRate::MPS_1;
  device._initialized = true;
  device._driverState = DriverState::READY;
  device._mode = Mode::PERIODIC;
  device._periodicActive = true;
  device._cachedSettings.repeatability = Repeatability::HIGH_REPEATABILITY;
  device._cachedSettings.periodicRate = PeriodicRate::MPS_1;
  device._hasCachedSettings = true;

  Status st = device.setRepeatability(Repeatability::LOW_REPEATABILITY);
  TEST_ASSERT_EQUAL(Err::I2C_ERROR, st.code);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Repeatability::HIGH_REPEATABILITY),
                    static_cast<uint8_t>(device._config.repeatability));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(Repeatability::HIGH_REPEATABILITY),
                    static_cast<uint8_t>(device._cachedSettings.repeatability));

  st = device.setPeriodicRate(PeriodicRate::MPS_10);
  TEST_ASSERT_EQUAL(Err::I2C_ERROR, st.code);
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(PeriodicRate::MPS_1),
                    static_cast<uint8_t>(device._config.periodicRate));
  TEST_ASSERT_EQUAL(static_cast<uint8_t>(PeriodicRate::MPS_1),
                    static_cast<uint8_t>(device._cachedSettings.periodicRate));
}

void test_read_paths_no_combined_and_respect_delay() {
  TimingTransport ctx;
  SHT3xDevice device;
  device._config.i2cWrite = timingWrite;
  device._config.i2cWriteRead = timingWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  installTimingHooks(device);
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

void test_read_status_periodic_requires_explicit_restore_helper() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.startPeriodic(PeriodicRate::MPS_1, Repeatability::HIGH_REPEATABILITY);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  clearFrameLog(ctx);
  uint16_t raw = 0;
  st = device.readStatus(raw);
  TEST_ASSERT_EQUAL(Err::BUSY, st.code);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  StatusRegister parsed;
  st = device.readStatus(parsed);
  TEST_ASSERT_EQUAL(Err::BUSY, st.code);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
}

void test_read_status_with_mode_restore_periodic_reads_alert_and_restores() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.startPeriodic(PeriodicRate::MPS_2, Repeatability::HIGH_REPEATABILITY);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  ctx.statusRaw = static_cast<uint16_t>(
      cmd::STATUS_ALERT_PENDING | cmd::STATUS_HEATER_ON |
      cmd::STATUS_RH_ALERT | cmd::STATUS_T_ALERT |
      cmd::STATUS_RESET_DETECTED | cmd::STATUS_COMMAND_ERROR |
      cmd::STATUS_WRITE_CRC_ERROR);
  clearFrameLog(ctx);

  StatusReadSnapshot snap;
  st = device.readStatusWithModeRestore(snap);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(snap.modeInterrupted);
  TEST_ASSERT_TRUE(snap.restored);
  TEST_ASSERT_TRUE(snap.statusValid);
  TEST_ASSERT_TRUE(snap.statusReadStatus.ok());
  TEST_ASSERT_TRUE(snap.restoreStatus.ok());
  TEST_ASSERT_EQUAL(Mode::PERIODIC, snap.initialMode);
  TEST_ASSERT_EQUAL(Mode::PERIODIC, snap.finalMode);
  TEST_ASSERT_TRUE(device.isPeriodicActive());
  TEST_ASSERT_EQUAL(Mode::PERIODIC, device._mode);
  TEST_ASSERT_EQUAL_UINT16(ctx.statusRaw, snap.status.raw);
  TEST_ASSERT_TRUE(snap.status.alertPending);
  TEST_ASSERT_TRUE(snap.status.heaterOn);
  TEST_ASSERT_TRUE(snap.status.rhAlert);
  TEST_ASSERT_TRUE(snap.status.tAlert);
  TEST_ASSERT_TRUE(snap.status.resetDetected);
  TEST_ASSERT_TRUE(snap.status.commandError);
  TEST_ASSERT_TRUE(snap.status.writeCrcError);

  TEST_ASSERT_EQUAL(3u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_BREAK, ctx.commands[0]);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_READ_STATUS, ctx.commands[1]);
  TEST_ASSERT_EQUAL_UINT16(SHT3xDevice::_commandForPeriodic(
                               Repeatability::HIGH_REPEATABILITY,
                               PeriodicRate::MPS_2),
                           ctx.commands[2]);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);
}

void test_read_status_with_mode_restore_art_reads_and_restores_art() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.startArt();
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  ctx.statusRaw = cmd::STATUS_T_ALERT;
  clearFrameLog(ctx);

  StatusReadSnapshot snap;
  st = device.readStatusWithModeRestore(snap);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(snap.modeInterrupted);
  TEST_ASSERT_TRUE(snap.restored);
  TEST_ASSERT_TRUE(snap.statusValid);
  TEST_ASSERT_TRUE(snap.status.tAlert);
  TEST_ASSERT_EQUAL(Mode::ART, snap.initialMode);
  TEST_ASSERT_EQUAL(Mode::ART, snap.finalMode);
  TEST_ASSERT_TRUE(device.isPeriodicActive());
  TEST_ASSERT_EQUAL(Mode::ART, device._mode);
  TEST_ASSERT_EQUAL(3u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_BREAK, ctx.commands[0]);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_READ_STATUS, ctx.commands[1]);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_ART, ctx.commands[2]);
}

void test_read_settings_status_success_sets_status_valid_and_reason() {
  FrameScriptTransport ctx;
  ctx.statusRaw = static_cast<uint16_t>(cmd::STATUS_HEATER_ON | cmd::STATUS_RESET_DETECTED);
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  clearFrameLog(ctx);
  SettingsSnapshot snap;
  st = device.readSettings(snap);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(snap.statusValid);
  TEST_ASSERT_TRUE(snap.statusReadStatus.ok());
  TEST_ASSERT_TRUE(snap.status.heaterOn);
  TEST_ASSERT_TRUE(snap.status.resetDetected);
  TEST_ASSERT_EQUAL(1u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_READ_STATUS, ctx.commands[0]);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);
}

void test_read_settings_periodic_exposes_status_busy_reason() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.startPeriodic(PeriodicRate::MPS_1, Repeatability::HIGH_REPEATABILITY);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  clearFrameLog(ctx);
  SettingsSnapshot snap;
  st = device.readSettings(snap);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(snap.periodicActive);
  TEST_ASSERT_FALSE(snap.statusValid);
  TEST_ASSERT_EQUAL(Err::BUSY, snap.statusReadStatus.code);
  TEST_ASSERT_EQUAL_STRING("Stop periodic mode before reading status", snap.statusReadStatus.msg);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
}

void test_read_settings_transport_failure_is_preserved() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  ctx.readStatus = Status::Error(Err::I2C_TIMEOUT, "status timeout", -77);
  clearFrameLog(ctx);
  SettingsSnapshot snap;
  st = device.readSettings(snap);
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);
  TEST_ASSERT_EQUAL_INT32(-77, st.detail);
  TEST_ASSERT_EQUAL_STRING("status timeout", st.msg);
  TEST_ASSERT_FALSE(snap.statusValid);
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, snap.statusReadStatus.code);
  TEST_ASSERT_EQUAL_INT32(-77, snap.statusReadStatus.detail);
  TEST_ASSERT_EQUAL(1u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_READ_STATUS, ctx.commands[0]);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);
}

void test_clear_status_command_only_failure_and_busy_modes() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  clearFrameLog(ctx);
  st = device.clearStatus();
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_EQUAL(1u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_CLEAR_STATUS, ctx.commands[0]);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  ctx.failCommand = cmd::CMD_CLEAR_STATUS;
  ctx.failCommandStatus = Status::Error(Err::I2C_TIMEOUT, "clear timeout", -12);
  clearFrameLog(ctx);
  st = device.clearStatus();
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);
  TEST_ASSERT_EQUAL_INT32(-12, st.detail);
  TEST_ASSERT_EQUAL(1u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_CLEAR_STATUS, ctx.commands[0]);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  ctx.failCommand = 0;
  st = device.startPeriodic(PeriodicRate::MPS_1, Repeatability::HIGH_REPEATABILITY);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  clearFrameLog(ctx);
  st = device.clearStatus();
  TEST_ASSERT_EQUAL(Err::BUSY, st.code);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
}

void test_status_with_mode_restore_break_failure_leaves_periodic_active() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.startPeriodic(PeriodicRate::MPS_1, Repeatability::HIGH_REPEATABILITY);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  ctx.failCommand = cmd::CMD_BREAK;
  ctx.failCommandStatus = Status::Error(Err::I2C_TIMEOUT, "break timeout", -31);
  clearFrameLog(ctx);

  StatusReadSnapshot snap;
  st = device.readStatusWithModeRestore(snap);
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);
  TEST_ASSERT_EQUAL_INT32(-31, st.detail);
  TEST_ASSERT_TRUE(snap.modeInterrupted);
  TEST_ASSERT_FALSE(snap.restored);
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, snap.stopStatus.code);
  TEST_ASSERT_EQUAL(Err::UNSUPPORTED, snap.statusReadStatus.code);
  TEST_ASSERT_TRUE(device.isPeriodicActive());
  TEST_ASSERT_EQUAL(Mode::PERIODIC, device._mode);
  TEST_ASSERT_EQUAL(1u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_BREAK, ctx.commands[0]);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
}

void test_status_with_mode_restore_status_write_failure_restores_periodic() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.startPeriodic(PeriodicRate::MPS_4, Repeatability::MEDIUM_REPEATABILITY);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  ctx.failCommand = cmd::CMD_READ_STATUS;
  ctx.failCommandStatus = Status::Error(Err::I2C_NACK_DATA, "status command nack", -41);
  clearFrameLog(ctx);

  StatusReadSnapshot snap;
  st = device.readStatusWithModeRestore(snap);
  TEST_ASSERT_EQUAL(Err::I2C_NACK_DATA, st.code);
  TEST_ASSERT_EQUAL_INT32(-41, st.detail);
  TEST_ASSERT_FALSE(snap.statusValid);
  TEST_ASSERT_EQUAL(Err::I2C_NACK_DATA, snap.statusReadStatus.code);
  TEST_ASSERT_TRUE(snap.restoreStatus.ok());
  TEST_ASSERT_TRUE(snap.restored);
  TEST_ASSERT_TRUE(device.isPeriodicActive());
  TEST_ASSERT_EQUAL(Mode::PERIODIC, device._mode);
  TEST_ASSERT_EQUAL(3u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_BREAK, ctx.commands[0]);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_READ_STATUS, ctx.commands[1]);
  TEST_ASSERT_EQUAL_UINT16(SHT3xDevice::_commandForPeriodic(
                               Repeatability::MEDIUM_REPEATABILITY,
                               PeriodicRate::MPS_4),
                           ctx.commands[2]);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
}

void test_status_with_mode_restore_status_read_failure_restores_periodic() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.startPeriodic(PeriodicRate::MPS_1, Repeatability::HIGH_REPEATABILITY);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  ctx.readStatus = Status::Error(Err::I2C_TIMEOUT, "status read timeout", -42);
  clearFrameLog(ctx);

  StatusReadSnapshot snap;
  st = device.readStatusWithModeRestore(snap);
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);
  TEST_ASSERT_EQUAL_INT32(-42, st.detail);
  TEST_ASSERT_FALSE(snap.statusValid);
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, snap.statusReadStatus.code);
  TEST_ASSERT_TRUE(snap.restoreStatus.ok());
  TEST_ASSERT_TRUE(snap.restored);
  TEST_ASSERT_TRUE(device.isPeriodicActive());
  TEST_ASSERT_EQUAL(3u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);
}

void test_status_with_mode_restore_crc_mismatch_restores_and_reports_crc() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.startPeriodic(PeriodicRate::MPS_1, Repeatability::HIGH_REPEATABILITY);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  ctx.statusRaw = cmd::STATUS_ALERT_PENDING;
  ctx.corruptStatusCrc = true;
  clearFrameLog(ctx);

  StatusReadSnapshot snap;
  st = device.readStatusWithModeRestore(snap);
  TEST_ASSERT_EQUAL(Err::CRC_MISMATCH, st.code);
  TEST_ASSERT_FALSE(snap.statusValid);
  TEST_ASSERT_EQUAL(Err::CRC_MISMATCH, snap.statusReadStatus.code);
  TEST_ASSERT_TRUE(snap.restoreStatus.ok());
  TEST_ASSERT_TRUE(snap.restored);
  TEST_ASSERT_TRUE(device.isPeriodicActive());
  TEST_ASSERT_EQUAL(Mode::PERIODIC, device._mode);
  TEST_ASSERT_EQUAL(3u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);
}

void test_status_with_mode_restore_restore_failure_reports_partial_state() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.startPeriodic(PeriodicRate::MPS_2, Repeatability::LOW_REPEATABILITY);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  const uint16_t restoreCommand = SHT3xDevice::_commandForPeriodic(
      Repeatability::LOW_REPEATABILITY, PeriodicRate::MPS_2);
  ctx.failCommand = restoreCommand;
  ctx.failCommandStatus = Status::Error(Err::I2C_TIMEOUT, "restore timeout", -51);
  ctx.statusRaw = cmd::STATUS_RH_ALERT;
  clearFrameLog(ctx);

  StatusReadSnapshot snap;
  st = device.readStatusWithModeRestore(snap);
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);
  TEST_ASSERT_EQUAL_INT32(-51, st.detail);
  TEST_ASSERT_TRUE(snap.statusValid);
  TEST_ASSERT_TRUE(snap.status.rhAlert);
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, snap.restoreStatus.code);
  TEST_ASSERT_FALSE(snap.restored);
  TEST_ASSERT_FALSE(device.isPeriodicActive());
  TEST_ASSERT_EQUAL(Mode::SINGLE_SHOT, device._mode);
  TEST_ASSERT_EQUAL(Mode::SINGLE_SHOT, snap.finalMode);
  TEST_ASSERT_EQUAL(3u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_BREAK, ctx.commands[0]);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_READ_STATUS, ctx.commands[1]);
  TEST_ASSERT_EQUAL_UINT16(restoreCommand, ctx.commands[2]);
}

void test_public_begin_rejects_invalid_address_without_i2c() {
  CountTransport ctx;
  Config cfg;
  cfg.i2cWrite = countWrite;
  cfg.i2cWriteRead = countWriteRead;
  cfg.i2cUser = &ctx;
  cfg.nowMs = stubNowMs;
  cfg.nowUs = stubNowUs;
  cfg.cooperativeYield = stubYield;
  cfg.i2cTimeoutMs = 10;
  cfg.i2cAddress = 0x46;

  SHT3xDevice device;
  Status st = device.begin(cfg);
  TEST_ASSERT_EQUAL(Err::INVALID_CONFIG, st.code);
  TEST_ASSERT_FALSE(device.isInitialized());
  TEST_ASSERT_EQUAL(DriverState::UNINIT, device.state());
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
}

void test_public_single_shot_measurement_timing_and_readout() {
  FakeTransport bus;
  SHT3xDevice device;
  Config cfg = makeConfig(bus);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  const uint32_t requestMs = bus.nowMs;
  const uint32_t readyMs = requestMs + device.estimateMeasurementTimeMs();
  st = device.requestMeasurement();
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_FALSE(device.measurementReady());

  device.tick(readyMs - 1);
  TEST_ASSERT_FALSE(device.measurementReady());

  device.tick(readyMs);
  TEST_ASSERT_TRUE(device.measurementReady());

  Measurement measurement;
  st = device.getMeasurement(measurement);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, -45.0f, measurement.temperatureC);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, measurement.humidityPct);
  TEST_ASSERT_FALSE(device.measurementReady());
  TEST_ASSERT_TRUE(device.hasSample());
}

void test_public_periodic_fetch_not_ready_does_not_count_as_failure() {
  FakeTransport bus;
  SHT3xDevice device;
  Config cfg = makeConfig(bus);
  cfg.transportCapabilities = TransportCapability::READ_HEADER_NACK;
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.startPeriodic(PeriodicRate::MPS_1, Repeatability::HIGH_REPEATABILITY);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  bus.writeReadStatus = Status::Error(Err::I2C_NACK_READ, "not ready");
  st = device.requestMeasurement();
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  device.tick(bus.nowMs + 2000U);

  TEST_ASSERT_FALSE(device.measurementReady());
  TEST_ASSERT_EQUAL_UINT32(1u, device.notReadyCount());
  TEST_ASSERT_EQUAL_UINT8(0u, device.consecutiveFailures());
  TEST_ASSERT_EQUAL(DriverState::READY, device.state());
}

void test_public_status_read_and_clear_are_explicit_operations() {
  FrameScriptTransport ctx;
  ctx.statusRaw = static_cast<uint16_t>(cmd::STATUS_HEATER_ON | cmd::STATUS_RESET_DETECTED);
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  clearFrameLog(ctx);
  StatusRegister reg;
  st = device.readStatus(reg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(reg.heaterOn);
  TEST_ASSERT_TRUE(reg.resetDetected);
  TEST_ASSERT_EQUAL(1u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_READ_STATUS, ctx.commands[0]);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);

  clearFrameLog(ctx);
  st = device.clearStatus();
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_EQUAL(1u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_CLEAR_STATUS, ctx.commands[0]);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
}

void test_periodic_start_command_failure_does_not_update_public_mode_or_cache() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  const uint16_t startCmd = SHT3xDevice::_commandForPeriodic(
      Repeatability::MEDIUM_REPEATABILITY, PeriodicRate::MPS_4);
  ctx.failCommand = startCmd;
  ctx.failCommandStatus = Status::Error(Err::I2C_NACK_DATA, "periodic start nack", -21);
  clearFrameLog(ctx);

  st = device.startPeriodic(PeriodicRate::MPS_4, Repeatability::MEDIUM_REPEATABILITY);
  TEST_ASSERT_EQUAL(Err::I2C_NACK_DATA, st.code);
  TEST_ASSERT_EQUAL_INT32(-21, st.detail);
  TEST_ASSERT_FALSE(device.isPeriodicActive());
  Mode mode = Mode::ART;
  TEST_ASSERT_TRUE(device.getMode(mode).ok());
  TEST_ASSERT_EQUAL(Mode::SINGLE_SHOT, mode);
  CachedSettings cached = device.getCachedSettings();
  TEST_ASSERT_EQUAL(Mode::SINGLE_SHOT, cached.mode);
  TEST_ASSERT_EQUAL(1u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(startCmd, ctx.commands[0]);
}

void test_periodic_restart_break_failure_keeps_previous_public_state() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.startPeriodic(PeriodicRate::MPS_1, Repeatability::HIGH_REPEATABILITY);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  ctx.failCommand = cmd::CMD_BREAK;
  ctx.failCommandStatus = Status::Error(Err::I2C_TIMEOUT, "break timeout", -22);
  clearFrameLog(ctx);

  st = device.startPeriodic(PeriodicRate::MPS_4, Repeatability::MEDIUM_REPEATABILITY);
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);
  TEST_ASSERT_EQUAL_INT32(-22, st.detail);
  TEST_ASSERT_TRUE(device.isPeriodicActive());
  Mode mode = Mode::SINGLE_SHOT;
  TEST_ASSERT_TRUE(device.getMode(mode).ok());
  TEST_ASSERT_EQUAL(Mode::PERIODIC, mode);
  PeriodicRate rate = PeriodicRate::MPS_10;
  TEST_ASSERT_TRUE(device.getPeriodicRate(rate).ok());
  TEST_ASSERT_EQUAL(PeriodicRate::MPS_1, rate);
  TEST_ASSERT_EQUAL(1u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_BREAK, ctx.commands[0]);
}

void test_periodic_reconfiguration_start_failure_exposes_stopped_state_and_preserves_cache() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.startPeriodic(PeriodicRate::MPS_1, Repeatability::HIGH_REPEATABILITY);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  const uint16_t restartCmd = SHT3xDevice::_commandForPeriodic(
      Repeatability::HIGH_REPEATABILITY, PeriodicRate::MPS_4);
  ctx.failCommand = restartCmd;
  ctx.failCommandStatus = Status::Error(Err::I2C_NACK_DATA, "restart nack", -26);
  clearFrameLog(ctx);

  st = device.setPeriodicRate(PeriodicRate::MPS_4);
  TEST_ASSERT_EQUAL(Err::I2C_NACK_DATA, st.code);
  TEST_ASSERT_EQUAL_INT32(-26, st.detail);
  TEST_ASSERT_FALSE(device.isPeriodicActive());
  Mode mode = Mode::PERIODIC;
  TEST_ASSERT_TRUE(device.getMode(mode).ok());
  TEST_ASSERT_EQUAL(Mode::SINGLE_SHOT, mode);
  PeriodicRate rate = PeriodicRate::MPS_10;
  TEST_ASSERT_TRUE(device.getPeriodicRate(rate).ok());
  TEST_ASSERT_EQUAL(PeriodicRate::MPS_1, rate);
  CachedSettings cached = device.getCachedSettings();
  TEST_ASSERT_EQUAL(Mode::PERIODIC, cached.mode);
  TEST_ASSERT_EQUAL(PeriodicRate::MPS_1, cached.periodicRate);
  TEST_ASSERT_EQUAL(2u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_BREAK, ctx.commands[0]);
  TEST_ASSERT_EQUAL_UINT16(restartCmd, ctx.commands[1]);
}

void test_alert_limit_write_command_failure_does_not_update_cache() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  ctx.failCommand = cmd::CMD_ALERT_WRITE_HIGH_SET;
  ctx.failCommandStatus = Status::Error(Err::I2C_NACK_DATA, "alert write nack", -23);
  clearFrameLog(ctx);

  st = device.writeAlertLimitRaw(AlertLimitKind::HIGH_SET, 0x2222);
  TEST_ASSERT_EQUAL(Err::I2C_NACK_DATA, st.code);
  TEST_ASSERT_EQUAL_INT32(-23, st.detail);
  CachedSettings cached = device.getCachedSettings();
  TEST_ASSERT_FALSE(cached.alertValid[static_cast<uint8_t>(AlertLimitKind::HIGH_SET)]);
  TEST_ASSERT_EQUAL(1u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_ALERT_WRITE_HIGH_SET, ctx.commands[0]);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
}

void test_alert_limit_write_status_verification_failure_does_not_update_cache() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  ctx.statusRaw = cmd::STATUS_WRITE_CRC_ERROR;
  clearFrameLog(ctx);

  st = device.writeAlertLimitRaw(AlertLimitKind::HIGH_SET, 0x2222);
  TEST_ASSERT_EQUAL(Err::WRITE_CRC_ERROR, st.code);
  CachedSettings cached = device.getCachedSettings();
  TEST_ASSERT_FALSE(cached.alertValid[static_cast<uint8_t>(AlertLimitKind::HIGH_SET)]);
  TEST_ASSERT_EQUAL(2u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_ALERT_WRITE_HIGH_SET, ctx.commands[0]);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_READ_STATUS, ctx.commands[1]);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);
}

void test_disable_alerts_second_write_failure_exposes_partial_cache() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  ctx.failCommand = cmd::CMD_ALERT_WRITE_LOW_SET;
  ctx.failCommandStatus = Status::Error(Err::I2C_TIMEOUT, "low set timeout", -27);
  clearFrameLog(ctx);

  st = device.disableAlerts();
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);
  TEST_ASSERT_EQUAL_INT32(-27, st.detail);
  CachedSettings cached = device.getCachedSettings();
  const uint8_t highSet = static_cast<uint8_t>(AlertLimitKind::HIGH_SET);
  const uint8_t lowSet = static_cast<uint8_t>(AlertLimitKind::LOW_SET);
  TEST_ASSERT_TRUE(cached.alertValid[highSet]);
  TEST_ASSERT_EQUAL_UINT16(0x0000, cached.alertRaw[highSet]);
  TEST_ASSERT_FALSE(cached.alertValid[lowSet]);
  TEST_ASSERT_EQUAL(3u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_ALERT_WRITE_HIGH_SET, ctx.commands[0]);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_READ_STATUS, ctx.commands[1]);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_ALERT_WRITE_LOW_SET, ctx.commands[2]);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);
}

void test_public_recover_bus_reset_failure_preserves_precise_status() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  cfg.busReset = frameBusResetFail;
  cfg.recoverUseBusReset = true;
  cfg.recoverUseSoftReset = false;
  cfg.recoverUseHardReset = false;
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  ctx.readStatus = Status::Error(Err::I2C_TIMEOUT, "probe timeout", -24);
  clearFrameLog(ctx);

  st = device.recover();
  TEST_ASSERT_EQUAL(Err::I2C_BUS, st.code);
  TEST_ASSERT_EQUAL_INT32(-88, st.detail);
  TEST_ASSERT_EQUAL(DriverState::DEGRADED, device.state());
  TEST_ASSERT_EQUAL_UINT32(1u, device.totalFailures());
  TEST_ASSERT_EQUAL(1u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_READ_STATUS, ctx.commands[0]);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);
}

void test_reset_and_restore_partial_alert_restore_failure_preserves_desired_cache() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.setHeater(true);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.writeAlertLimitRaw(AlertLimitKind::HIGH_SET, 0x2222);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  clearFrameLog(ctx);
  ctx.failReadIndex = 2;
  ctx.failReadStatus = Status::Error(Err::I2C_TIMEOUT, "alert verify timeout", -25);

  st = device.resetAndRestore();
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);
  TEST_ASSERT_EQUAL_INT32(-25, st.detail);
  TEST_ASSERT_TRUE(device.hasCachedSettings());
  CachedSettings cached = device.getCachedSettings();
  TEST_ASSERT_TRUE(cached.heaterEnabled);
  TEST_ASSERT_TRUE(cached.alertValid[static_cast<uint8_t>(AlertLimitKind::HIGH_SET)]);
  TEST_ASSERT_EQUAL_UINT16(0x2222, cached.alertRaw[static_cast<uint8_t>(AlertLimitKind::HIGH_SET)]);
  TEST_ASSERT_EQUAL(DriverState::DEGRADED, device.state());
  TEST_ASSERT_FALSE(device.isPeriodicActive());
  TEST_ASSERT_EQUAL(4u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_READ_STATUS, ctx.commands[0]);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_HEATER_ENABLE, ctx.commands[1]);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_ALERT_WRITE_HIGH_SET, ctx.commands[2]);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_READ_STATUS, ctx.commands[3]);
  TEST_ASSERT_EQUAL_UINT32(2u, ctx.reads);
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

void test_raw_and_compensated_samples_remain_after_measurement_read() {
  FakeTransport bus;
  SHT3xDevice device;
  Config cfg = makeConfig(bus);

  gMillis = 0;
  gMicros = 0;
  gMillisStep = 1;
  gMicrosStep = 1000;

  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  RawSample raw;
  st = device.getRawSample(raw);
  TEST_ASSERT_EQUAL(Err::MEASUREMENT_NOT_READY, st.code);

  st = device.requestMeasurement();
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  const uint32_t readyMs = device._measurementReadyMs;
  device.tick(readyMs);
  TEST_ASSERT_TRUE(device.measurementReady());

  Measurement measurement;
  st = device.getMeasurement(measurement);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_FALSE(device.measurementReady());

  st = device.getRawSample(raw);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT16(0u, raw.rawTemperature);
  TEST_ASSERT_EQUAL_UINT16(0u, raw.rawHumidity);

  CompensatedSample comp;
  st = device.getCompensatedSample(comp);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_INT32(-4500, comp.tempC_x100);
  TEST_ASSERT_EQUAL_UINT32(0u, comp.humidityPct_x100);

  SettingsSnapshot snap;
  st = device.getSettings(snap);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_TRUE(snap.hasSample);
  TEST_ASSERT_FALSE(snap.measurementReady);
}

void test_zero_timestamp_sample_age_uses_has_sample_flag() {
  SHT3xDevice device;
  device._hasSample = false;
  device._sampleTimestampMs = 0;
  TEST_ASSERT_EQUAL_UINT32(0u, device.sampleAgeMs(123u));

  device._hasSample = true;
  TEST_ASSERT_EQUAL_UINT32(123u, device.sampleAgeMs(123u));
}

void test_offline_request_measurement_does_not_touch_bus_or_schedule() {
  CountTransport ctx;
  SHT3xDevice device;
  device._config.i2cWrite = countWrite;
  device._config.i2cWriteRead = countWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  installTimingHooks(device);
  device._initialized = true;
  device._driverState = DriverState::OFFLINE;
  device._mode = Mode::SINGLE_SHOT;
  device._periodicActive = false;

  Status st = device.requestMeasurement();
  TEST_ASSERT_EQUAL(Err::BUSY, st.code);
  TEST_ASSERT_EQUAL_STRING("Driver is offline; call recover()", st.msg);
  TEST_ASSERT_FALSE(device._measurementRequested);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
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
  installTimingHooks(device);
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
  installTimingHooks(device);
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

void test_offline_latches_read_status_without_i2c() {
  CountTransport ctx;
  SHT3xDevice device;
  device._config.i2cWrite = countWrite;
  device._config.i2cWriteRead = countWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  installTimingHooks(device);
  device._config.offlineThreshold = 1;
  device._initialized = true;
  device._driverState = DriverState::READY;

  (void)device._updateHealth(Status::Error(Err::I2C_TIMEOUT, "forced offline"));
  TEST_ASSERT_EQUAL(DriverState::OFFLINE, device._driverState);

  uint16_t statusRaw = 0;
  Status st = device.readStatus(statusRaw);
  TEST_ASSERT_EQUAL(Err::BUSY, st.code);
  TEST_ASSERT_EQUAL_STRING("Driver is offline; call recover()", st.msg);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
}

void test_read_settings_returns_offline_busy_without_i2c() {
  CountTransport ctx;
  SHT3xDevice device;
  device._config.i2cWrite = countWrite;
  device._config.i2cWriteRead = countWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  installTimingHooks(device);
  device._config.offlineThreshold = 1;
  device._initialized = true;
  device._driverState = DriverState::OFFLINE;
  device._consecutiveFailures = 1;

  SettingsSnapshot snap;
  Status st = device.readSettings(snap);
  TEST_ASSERT_EQUAL(Err::BUSY, st.code);
  TEST_ASSERT_EQUAL_STRING("Driver is offline; call recover()", st.msg);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL(DriverState::OFFLINE, device._driverState);
}

void test_failed_recover_from_offline_keeps_latch_after_intermediate_success() {
  ScriptedTransport ctx;
  ctx.readScript[0] = Status::Error(Err::I2C_TIMEOUT, "initial probe timeout");
  ctx.readScript[1] = Status::Error(Err::I2C_TIMEOUT, "post-reset timeout");
  ctx.readCount = 2;

  SHT3xDevice device;
  device._config.i2cWrite = scriptedWrite;
  device._config.i2cWriteRead = scriptedWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  installTimingHooks(device);
  device._config.offlineThreshold = 3;
  device._config.recoverBackoffMs = 0;
  device._config.recoverUseBusReset = false;
  device._config.recoverUseSoftReset = true;
  device._config.recoverUseHardReset = false;
  device._config.allowGeneralCallReset = false;
  device._initialized = true;
  device._driverState = DriverState::OFFLINE;
  device._consecutiveFailures = device._config.offlineThreshold;

  gMillis = 0;
  gMicros = 0;
  gMillisStep = 1;
  gMicrosStep = 1000;
  Status st = device.recover();
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);
  TEST_ASSERT_EQUAL(DriverState::OFFLINE, device._driverState);
  TEST_ASSERT_TRUE(device._consecutiveFailures >= device._config.offlineThreshold);
}

void test_recover_backoff_enforced_at_zero_ms() {
  ScriptedTransport ctx;
  ctx.readScript[0] = Status::Error(Err::I2C_TIMEOUT, "timeout");
  ctx.readCount = 1;

  SHT3xDevice device;
  device._config.i2cWrite = scriptedWrite;
  device._config.i2cWriteRead = scriptedWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  installTimingHooks(device);
  device._config.recoverBackoffMs = 100;
  device._config.recoverUseBusReset = false;
  device._config.recoverUseSoftReset = false;
  device._config.recoverUseHardReset = false;
  device._config.allowGeneralCallReset = false;
  device._initialized = true;
  device._driverState = DriverState::READY;

  gMillis = 0;
  gMicros = 0;
  gMillisStep = 0;
  gMicrosStep = 1000;

  Status st = device.recover();
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);

  st = device.recover();
  TEST_ASSERT_EQUAL(Err::BUSY, st.code);
}

void test_periodic_fetch_margin_is_clamped() {
  SHT3xDevice device;
  device._periodMs = 100;
  device._config.periodicFetchMarginMs = 1000;
  TEST_ASSERT_EQUAL_UINT32(100u, device._periodicFetchMarginMs());
}

void test_end_clears_runtime_state() {
  SHT3xDevice device;
  device._initialized = true;
  device._driverState = DriverState::READY;
  device._measurementRequested = true;
  device._measurementReady = true;
  device._measurementReadyMs = 123;
  device._periodicActive = true;
  device._periodicStartMs = 456;
  device._lastFetchMs = 789;
  device._periodMs = 100;
  device._sampleTimestampMs = 111;
  device._missedSamples = 12;
  device._notReadyStartMs = 222;
  device._notReadyCount = 333;
  device._lastRecoverMs = 444;
  device._lastRecoverValid = true;
  device._lastCommandValid = true;

  device.end();

  TEST_ASSERT_FALSE(device._initialized);
  TEST_ASSERT_EQUAL(DriverState::UNINIT, device._driverState);
  TEST_ASSERT_FALSE(device._measurementRequested);
  TEST_ASSERT_FALSE(device._measurementReady);
  TEST_ASSERT_FALSE(device._hasSample);
  TEST_ASSERT_EQUAL_UINT32(0u, device._measurementReadyMs);
  TEST_ASSERT_FALSE(device._periodicActive);
  TEST_ASSERT_EQUAL_UINT32(0u, device._periodicStartMs);
  TEST_ASSERT_EQUAL_UINT32(0u, device._lastFetchMs);
  TEST_ASSERT_EQUAL_UINT32(0u, device._periodMs);
  TEST_ASSERT_EQUAL_UINT32(0u, device._sampleTimestampMs);
  TEST_ASSERT_EQUAL_UINT32(0u, device._missedSamples);
  TEST_ASSERT_EQUAL_UINT32(0u, device._notReadyStartMs);
  TEST_ASSERT_EQUAL_UINT32(0u, device._notReadyCount);
  TEST_ASSERT_EQUAL_UINT32(0u, device._lastRecoverMs);
  TEST_ASSERT_FALSE(device._lastRecoverValid);
  TEST_ASSERT_FALSE(device._lastCommandValid);
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
  RUN_TEST(test_status_helpers);
  RUN_TEST(test_config_defaults);
  RUN_TEST(test_get_settings_snapshot_fields);
  RUN_TEST(test_begin_resets_invalid_config_to_uninit_defaults_and_no_startup_health);
  RUN_TEST(test_begin_rejects_missing_timing_hooks_before_i2c);
  RUN_TEST(test_crc8_example);
  RUN_TEST(test_conversions_basic);
  RUN_TEST(test_alert_limit_roundtrip);
  RUN_TEST(test_time_elapsed_wrap);
  RUN_TEST(test_command_delay_guard);
  RUN_TEST(test_begin_rejects_oversized_timing_config);
  RUN_TEST(test_single_shot_pending_blocks_unrelated_commands);
  RUN_TEST(test_low_level_command_helpers_write_and_read);
  RUN_TEST(test_low_level_command_helpers_map_expected_nack);
  RUN_TEST(test_low_level_command_helpers_block_pending_measurement);
  RUN_TEST(test_low_level_read_rejects_invalid_buffers_before_i2c);
  RUN_TEST(test_raw_transport_rejects_invalid_buffers);
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
  RUN_TEST(test_periodic_setters_do_not_mutate_config_on_restart_failure);
  RUN_TEST(test_read_paths_no_combined_and_respect_delay);
  RUN_TEST(test_read_status_periodic_requires_explicit_restore_helper);
  RUN_TEST(test_read_status_with_mode_restore_periodic_reads_alert_and_restores);
  RUN_TEST(test_read_status_with_mode_restore_art_reads_and_restores_art);
  RUN_TEST(test_read_settings_status_success_sets_status_valid_and_reason);
  RUN_TEST(test_read_settings_periodic_exposes_status_busy_reason);
  RUN_TEST(test_read_settings_transport_failure_is_preserved);
  RUN_TEST(test_clear_status_command_only_failure_and_busy_modes);
  RUN_TEST(test_status_with_mode_restore_break_failure_leaves_periodic_active);
  RUN_TEST(test_status_with_mode_restore_status_write_failure_restores_periodic);
  RUN_TEST(test_status_with_mode_restore_status_read_failure_restores_periodic);
  RUN_TEST(test_status_with_mode_restore_crc_mismatch_restores_and_reports_crc);
  RUN_TEST(test_status_with_mode_restore_restore_failure_reports_partial_state);
  RUN_TEST(test_public_begin_rejects_invalid_address_without_i2c);
  RUN_TEST(test_public_single_shot_measurement_timing_and_readout);
  RUN_TEST(test_public_periodic_fetch_not_ready_does_not_count_as_failure);
  RUN_TEST(test_public_status_read_and_clear_are_explicit_operations);
  RUN_TEST(test_periodic_start_command_failure_does_not_update_public_mode_or_cache);
  RUN_TEST(test_periodic_restart_break_failure_keeps_previous_public_state);
  RUN_TEST(test_periodic_reconfiguration_start_failure_exposes_stopped_state_and_preserves_cache);
  RUN_TEST(test_alert_limit_write_command_failure_does_not_update_cache);
  RUN_TEST(test_alert_limit_write_status_verification_failure_does_not_update_cache);
  RUN_TEST(test_disable_alerts_second_write_failure_exposes_partial_cache);
  RUN_TEST(test_public_recover_bus_reset_failure_preserves_precise_status);
  RUN_TEST(test_reset_and_restore_partial_alert_restore_failure_preserves_desired_cache);
  RUN_TEST(test_periodic_fetch_margin_blocks_early_fetch);
  RUN_TEST(test_raw_and_compensated_samples_remain_after_measurement_read);
  RUN_TEST(test_zero_timestamp_sample_age_uses_has_sample_flag);
  RUN_TEST(test_offline_request_measurement_does_not_touch_bus_or_schedule);
  RUN_TEST(test_recover_transient_failure);
  RUN_TEST(test_recover_permanent_offline);
  RUN_TEST(test_offline_latches_read_status_without_i2c);
  RUN_TEST(test_read_settings_returns_offline_busy_without_i2c);
  RUN_TEST(test_failed_recover_from_offline_keeps_latch_after_intermediate_success);
  RUN_TEST(test_recover_backoff_enforced_at_zero_ms);
  RUN_TEST(test_periodic_fetch_margin_is_clamped);
  RUN_TEST(test_end_clears_runtime_state);
  return UNITY_END();
}
