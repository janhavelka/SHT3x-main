/// @file test_basic.cpp
/// @brief Basic unit tests for SHT3x driver

#include <unity.h>
#include <type_traits>

// Include stubs first
#include "Arduino.h"
#include "Wire.h"
#include "examples/common/I2cTransport.h"
#include "examples/common/I2cScanner.h"

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

static uint32_t stubNowMs(void*);
static uint32_t stubNowUs(void*);
static void stubYield(void*);
static void installTimingHooks(SHT3xDevice& device);

struct AlertAppNoteVector {
  AlertLimitKind kind;
  float temperatureC;
  float humidityPct;
  uint16_t word;
};

static constexpr AlertAppNoteVector kAlertAppNoteVectors[] = {
    {AlertLimitKind::HIGH_SET, 60.0f, 80.0f, 0xCD33},
    {AlertLimitKind::HIGH_CLEAR, 58.0f, 79.0f, 0xC92D},
    {AlertLimitKind::LOW_CLEAR, -9.0f, 22.0f, 0x3869},
    {AlertLimitKind::LOW_SET, -10.0f, 20.0f, 0x3466},
};

static_assert(!std::is_copy_constructible<SHT3xDevice>::value,
              "SHT3x must not be copy constructible");
static_assert(!std::is_copy_assignable<SHT3xDevice>::value,
              "SHT3x must not be copy assignable");
static_assert(!std::is_move_constructible<SHT3xDevice>::value,
              "SHT3x must not be move constructible");
static_assert(!std::is_move_assignable<SHT3xDevice>::value,
              "SHT3x must not be move assignable");
static_assert(std::is_trivially_copyable<JobRequest>::value,
              "JobRequest must remain fixed-memory and trivially copyable");
static_assert(std::is_trivially_copyable<PollJobResult>::value,
              "PollJobResult must remain fixed-memory and trivially copyable");
static_assert(sizeof(JobRequest) <= 16,
              "JobRequest grew beyond the owner message budget");
static_assert(sizeof(PollJobResult) <= 64,
              "PollJobResult grew beyond the owner message budget");
static_assert(sizeof(MeasurementMilli) == 8,
              "MeasurementMilli must remain two signed 32-bit values");

static constexpr Config kLegacyAggregateConfig = {
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    0x45,
    77,
    TransportCapability::READ_HEADER_NACK,
    Repeatability::LOW_REPEATABILITY,
    ClockStretching::STRETCH_ENABLED,
    PeriodicRate::MPS_4,
    Mode::ART,
    true,
    7,
    123,
    17,
    456,
    9,
    true,
    false,
    false,
    false,
};
static_assert(kLegacyAggregateConfig.offlineThreshold == 9,
              "v1.6 Config aggregate field order must remain compatible");
static_assert(kLegacyAggregateConfig.allowGeneralCallReset,
              "v1.6 Config reset fields must retain their aggregate positions");
static_assert(!kLegacyAggregateConfig.recoverUseBusReset &&
                  !kLegacyAggregateConfig.recoverUseSoftReset &&
                  !kLegacyAggregateConfig.recoverUseHardReset,
              "v1.6 Config recovery fields must retain their aggregate positions");
static_assert(kLegacyAggregateConfig.healthPolicy == HealthPolicy::LATCH_OFFLINE,
              "appended Config fields must retain their documented defaults");

static constexpr SettingsSnapshot kLegacyAggregateSettingsSnapshot = {
    true,
    DriverState::READY,
    0x45,
    77,
    9,
    true,
    Mode::ART,
    Repeatability::LOW_REPEATABILITY,
    PeriodicRate::MPS_4,
    ClockStretching::STRETCH_ENABLED,
    true,
    true,
    false,
    true,
    Status::Error(Err::TIMEOUT, "legacy snapshot", -3),
    101,
    102,
    103,
    StatusRegister{0x1234, true, false, true, false, true, false, true},
    true,
    Status::Ok(),
};
static_assert(kLegacyAggregateSettingsSnapshot.hasNowMsHook &&
                  kLegacyAggregateSettingsSnapshot.mode == Mode::ART,
              "v1.6 SettingsSnapshot aggregate field order must remain compatible");
static_assert(kLegacyAggregateSettingsSnapshot.status.raw == 0x1234 &&
                  kLegacyAggregateSettingsSnapshot.statusValid,
              "v1.6 SettingsSnapshot status fields must retain their positions");
static_assert(kLegacyAggregateSettingsSnapshot.healthPolicy ==
                  HealthPolicy::LATCH_OFFLINE &&
                  !kLegacyAggregateSettingsSnapshot.hardwareStateValid,
              "appended SettingsSnapshot fields must retain documented defaults");
static_assert(isTransportError(Err::I2C_TIMEOUT) &&
                  !isTransportError(Err::TIMEOUT),
              "transport classifier must preserve callback provenance");
static_assert(isProtocolError(Err::CRC_MISMATCH) &&
                  !isProtocolError(Err::I2C_ERROR),
              "protocol classifier must stay distinct from transport errors");
static_assert(isExpectedNotReady(Err::MEASUREMENT_NOT_READY) &&
                  isAbsent(Err::DEVICE_NOT_FOUND),
              "owner classifiers must remain constexpr");

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
  TEST_ASSERT_TRUE(isTransportError(Err::I2C_NACK_READ));
  TEST_ASSERT_FALSE(isTransportError(Err::DEVICE_NOT_FOUND));
  TEST_ASSERT_TRUE(isProtocolError(Err::COMMAND_FAILED));
  TEST_ASSERT_FALSE(isProtocolError(Err::INVALID_PARAM));
  TEST_ASSERT_TRUE(isExpectedNotReady(Err::CONVERSION_NOT_READY));
  TEST_ASSERT_FALSE(isExpectedNotReady(Err::BUSY));
  TEST_ASSERT_TRUE(isAbsent(Err::DEVICE_NOT_FOUND));
  TEST_ASSERT_FALSE(isAbsent(Err::I2C_NACK_ADDR));
  TEST_ASSERT_FALSE(isTransportError(static_cast<Err>(0xFFu)));
  TEST_ASSERT_FALSE(isProtocolError(static_cast<Err>(0xFFu)));
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
  TEST_ASSERT_EQUAL(HealthPolicy::LATCH_OFFLINE, cfg.healthPolicy);
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

  SHT3xDevice timing;
  timing._config.lowVdd = false;
  timing._config.repeatability = Repeatability::LOW_REPEATABILITY;
  TEST_ASSERT_EQUAL_UINT32(5u, timing.estimateMeasurementTimeMs());
  timing._config.repeatability = Repeatability::MEDIUM_REPEATABILITY;
  TEST_ASSERT_EQUAL_UINT32(7u, timing.estimateMeasurementTimeMs());
  timing._config.repeatability = Repeatability::HIGH_REPEATABILITY;
  TEST_ASSERT_EQUAL_UINT32(16u, timing.estimateMeasurementTimeMs());
  timing._config.lowVdd = true;
  timing._config.repeatability = Repeatability::LOW_REPEATABILITY;
  TEST_ASSERT_EQUAL_UINT32(6u, timing.estimateMeasurementTimeMs());
  timing._config.repeatability = Repeatability::MEDIUM_REPEATABILITY;
  TEST_ASSERT_EQUAL_UINT32(8u, timing.estimateMeasurementTimeMs());
  timing._config.repeatability = Repeatability::HIGH_REPEATABILITY;
  TEST_ASSERT_EQUAL_UINT32(17u, timing.estimateMeasurementTimeMs());
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

void test_alert_limit_app_note_encode_vectors() {
  for (const auto& vector : kAlertAppNoteVectors) {
    TEST_ASSERT_EQUAL_HEX16(vector.word,
                            SHT3xDevice::encodeAlertLimit(vector.temperatureC,
                                                          vector.humidityPct));
  }
}

void test_alert_limit_app_note_decode_vectors() {
  for (const auto& vector : kAlertAppNoteVectors) {
    float temperatureC = 0.0f;
    float humidityPct = 0.0f;
    SHT3xDevice::decodeAlertLimit(vector.word, temperatureC, humidityPct);
    TEST_ASSERT_FLOAT_WITHIN(0.7f, vector.temperatureC, temperatureC);
    TEST_ASSERT_FLOAT_WITHIN(1.0f, vector.humidityPct, humidityPct);
  }
}

void test_alert_limit_clamps_out_of_range_inputs() {
  TEST_ASSERT_EQUAL_HEX16(0x0000, SHT3xDevice::encodeAlertLimit(-100.0f, -1.0f));
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, SHT3xDevice::encodeAlertLimit(200.0f, 120.0f));
  TEST_ASSERT_EQUAL_HEX16(0x0000, SHT3xDevice::encodeAlertLimit(-45.0f, 0.0f));
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, SHT3xDevice::encodeAlertLimit(130.0f, 100.0f));
}

void test_alert_limit_quantized_roundtrip_edges() {
  const uint16_t words[] = {0x0000, 0xFFFF, 0xCD33, 0xC92D, 0x3869, 0x3466};
  for (const uint16_t word : words) {
    float temperatureC = 0.0f;
    float humidityPct = 0.0f;
    SHT3xDevice::decodeAlertLimit(word, temperatureC, humidityPct);
    TEST_ASSERT_EQUAL_HEX16(word, SHT3xDevice::encodeAlertLimit(temperatureC,
                                                                humidityPct));
  }
}

void test_time_elapsed_wrap() {
  TEST_ASSERT_FALSE(SHT3xDevice::_timeElapsed(5, 10));
  TEST_ASSERT_TRUE(SHT3xDevice::_timeElapsed(10, 10));
  TEST_ASSERT_TRUE(SHT3xDevice::_timeElapsed(10, 5));

  const uint32_t nearMax = 0xFFFFFFF0U;
  TEST_ASSERT_TRUE(SHT3xDevice::_timeElapsed(5, nearMax));
  TEST_ASSERT_FALSE(SHT3xDevice::_timeElapsed(nearMax, 5));

  TEST_ASSERT_FALSE(SHT3xDevice::_durationElapsed(10, 5, 6));
  TEST_ASSERT_TRUE(SHT3xDevice::_durationElapsed(11, 5, 6));
  TEST_ASSERT_TRUE(SHT3xDevice::_durationElapsed(5, nearMax, 20));
  TEST_ASSERT_FALSE(SHT3xDevice::_durationElapsed(5, nearMax, 22));
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

void test_command_delay_allows_long_idle_after_microsecond_half_range() {
  SHT3xDevice device;
  device._initialized = true;
  device._config.commandDelayMs = 1;
  device._config.i2cTimeoutMs = 1;
  installTimingHooks(device);
  gMicros = 0x80001000U;
  gMicrosStep = 0;
  gMillis = 0;
  gMillisStep = 0;
  device._lastCommandUs = 0;
  device._lastCommandValid = true;

  Status st = device._ensureCommandDelay();
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
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
  uint16_t statusRaw = 0;
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
      const bool statusRead =
          ctx->lastWriteLen >= 2 &&
          static_cast<uint16_t>((static_cast<uint16_t>(ctx->lastWrite[0]) << 8) |
                                ctx->lastWrite[1]) == cmd::CMD_READ_STATUS;
      if (statusRead) {
        rxData[0] = static_cast<uint8_t>(ctx->statusRaw >> 8);
        rxData[1] = static_cast<uint8_t>(ctx->statusRaw & 0xFF);
        ctx->lastReadRx[0] = rxData[0];
        ctx->lastReadRx[1] = rxData[1];
      }
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
  bool corruptMeasurementCrc = false;
  uint16_t commands[40] = {};
  size_t commandCount = 0;
  uint32_t writes = 0;
  uint32_t reads = 0;
  uint16_t lastCommand = 0;
  bool sawAlertWritePayload = false;
  uint16_t lastAlertWriteCommand = 0;
  uint16_t lastAlertWriteValue = 0;
  uint8_t lastAlertWriteCrc = 0;
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
    if (len >= 5 && (command == cmd::CMD_ALERT_WRITE_HIGH_SET ||
                     command == cmd::CMD_ALERT_WRITE_HIGH_CLEAR ||
                     command == cmd::CMD_ALERT_WRITE_LOW_CLEAR ||
                     command == cmd::CMD_ALERT_WRITE_LOW_SET)) {
      ctx->sawAlertWritePayload = true;
      ctx->lastAlertWriteCommand = command;
      ctx->lastAlertWriteValue = static_cast<uint16_t>((data[2] << 8) | data[3]);
      ctx->lastAlertWriteCrc = data[4];
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
    if (ctx->corruptMeasurementCrc) {
      rxData[2] ^= 0x01;
    }
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
  ctx.sawAlertWritePayload = false;
  ctx.lastAlertWriteCommand = 0;
  ctx.lastAlertWriteValue = 0;
  ctx.lastAlertWriteCrc = 0;
  for (size_t i = 0; i < 40; ++i) {
    ctx.commands[i] = 0;
  }
}

struct PreciseTimingTransport {
  uint32_t nowMs = 0;
  uint32_t nowUs = 0;
  uint32_t writeAdvanceMs = 0;
  uint32_t writeAdvanceUs = 0;
  uint32_t readAdvanceMs = 0;
  uint32_t readAdvanceUs = 0;
  uint32_t writes = 0;
  uint32_t reads = 0;
  uint8_t lastAddress = 0;
  uint32_t lastTimeoutMs = 0;
  uint16_t lastCommand = 0;
  uint16_t failCommand = 0;
  Status writeStatus = Status::Ok();
  Status readStatus = Status::Ok();
  Status failCommandStatus = Status::Ok();
  uint16_t rawTemperature = 0;
  uint16_t rawHumidity = 0;
  uint16_t statusRaw = 0;
  bool corruptTemperatureCrc = false;
  bool corruptHumidityCrc = false;
  bool corruptStatusCrc = false;
};

static Status preciseTimingWrite(uint8_t addr, const uint8_t* data, size_t len,
                                 uint32_t timeoutMs, void* user) {
  (void)addr;
  auto* ctx = static_cast<PreciseTimingTransport*>(user);
  ++ctx->writes;
  ctx->lastAddress = addr;
  ctx->lastTimeoutMs = timeoutMs;
  if (data != nullptr && len >= 2) {
    ctx->lastCommand = static_cast<uint16_t>((data[0] << 8) | data[1]);
  }
  ctx->nowMs += ctx->writeAdvanceMs;
  ctx->nowUs += ctx->writeAdvanceUs;
  if (ctx->failCommand != 0 && ctx->lastCommand == ctx->failCommand) {
    return ctx->failCommandStatus;
  }
  return ctx->writeStatus;
}

static Status preciseTimingRead(uint8_t addr, const uint8_t* txData, size_t txLen,
                                uint8_t* rxData, size_t rxLen,
                                uint32_t timeoutMs, void* user) {
  (void)txData;
  auto* ctx = static_cast<PreciseTimingTransport*>(user);
  ++ctx->reads;
  ctx->lastAddress = addr;
  ctx->lastTimeoutMs = timeoutMs;
  ctx->nowMs += ctx->readAdvanceMs;
  ctx->nowUs += ctx->readAdvanceUs;

  if (txLen != 0 || rxData == nullptr) {
    return Status::Error(Err::INVALID_PARAM, "Unexpected precise timing read");
  }
  if (!ctx->readStatus.ok()) {
    return ctx->readStatus;
  }
  if (rxLen == cmd::MEASUREMENT_DATA_LEN) {
    rxData[0] = static_cast<uint8_t>(ctx->rawTemperature >> 8);
    rxData[1] = static_cast<uint8_t>(ctx->rawTemperature & 0xFF);
    rxData[2] = SHT3xDevice::_crc8(&rxData[0], 2);
    rxData[3] = static_cast<uint8_t>(ctx->rawHumidity >> 8);
    rxData[4] = static_cast<uint8_t>(ctx->rawHumidity & 0xFF);
    rxData[5] = SHT3xDevice::_crc8(&rxData[3], 2);
    if (ctx->corruptTemperatureCrc) {
      rxData[2] ^= 0x01;
    }
    if (ctx->corruptHumidityCrc) {
      rxData[5] ^= 0x01;
    }
    return Status::Ok();
  }
  if (rxLen == cmd::STATUS_DATA_LEN) {
    rxData[0] = static_cast<uint8_t>(ctx->statusRaw >> 8);
    rxData[1] = static_cast<uint8_t>(ctx->statusRaw & 0xFF);
    rxData[2] = SHT3xDevice::_crc8(&rxData[0], 2);
    if (ctx->corruptStatusCrc) {
      rxData[2] ^= 0x01;
    }
    return Status::Ok();
  }
  return Status::Error(Err::INVALID_PARAM, "Unexpected precise timing length");
}

static uint32_t preciseTimingNowMs(void* user) {
  return static_cast<PreciseTimingTransport*>(user)->nowMs;
}

static uint32_t preciseTimingNowUs(void* user) {
  return static_cast<PreciseTimingTransport*>(user)->nowUs;
}

static void preciseTimingYield(void* user) {
  auto* ctx = static_cast<PreciseTimingTransport*>(user);
  ++ctx->nowMs;
  ctx->nowUs += 1000u;
}

static Config makePreciseTimingConfig(PreciseTimingTransport& ctx,
                                      uint8_t address = cmd::I2C_ADDR_LOW) {
  Config cfg;
  cfg.i2cWrite = preciseTimingWrite;
  cfg.i2cWriteRead = preciseTimingRead;
  cfg.i2cUser = &ctx;
  cfg.nowMs = preciseTimingNowMs;
  cfg.nowUs = preciseTimingNowUs;
  cfg.cooperativeYield = preciseTimingYield;
  cfg.timeUser = &ctx;
  cfg.i2cAddress = address;
  cfg.i2cTimeoutMs = 10;
  cfg.commandDelayMs = 1;
  cfg.offlineThreshold = 3;
  return cfg;
}

static void preparePreciseTimingDevice(SHT3xDevice& device,
                                       PreciseTimingTransport& ctx,
                                       Mode mode) {
  Config cfg = makePreciseTimingConfig(ctx);
  cfg.mode = mode;

  device._config = cfg;
  device._initialized = true;
  device._driverState = DriverState::READY;
  device._mode = mode;
  device._periodicActive = mode != Mode::SINGLE_SHOT;
  device._periodMs = 0;
  device._periodicStartMs = ctx.nowMs;
}

static void advancePreciseTimeMs(PreciseTimingTransport& ctx,
                                 uint32_t deltaMs) {
  ctx.nowMs += deltaMs;
  ctx.nowUs += deltaMs * 1000u;
}

static Status runEnsureIdleToStatusResult(SHT3xDevice& device,
                                          PreciseTimingTransport& ctx,
                                          PollJobResult& result) {
  Status st = device.pollJob(ctx.nowMs, 1, result);  // Break command.
  if (!st.inProgress()) {
    return st;
  }
  ctx.nowMs = device._measurementReadyMs;
  ctx.nowUs += 1000u;
  st = device.pollJob(ctx.nowMs, 1, result);  // Break wait.
  if (!st.inProgress()) {
    return st;
  }
  st = device.pollJob(ctx.nowMs, 1, result);  // Soft-reset command.
  if (!st.inProgress()) {
    return st;
  }
  ctx.nowMs = device._measurementReadyMs;
  ctx.nowUs += 2000u;
  st = device.pollJob(ctx.nowMs, 1, result);  // Reset wait.
  if (!st.inProgress()) {
    return st;
  }
  st = device.pollJob(ctx.nowMs, 1, result);  // Status command.
  if (!st.inProgress()) {
    return st;
  }
  advancePreciseTimeMs(ctx, 1);
  return device.pollJob(ctx.nowMs, 1, result);  // Status read/verification.
}

void test_write_alert_limit_uses_app_note_encoder_vectors() {
  for (const auto& vector : kAlertAppNoteVectors) {
    FrameScriptTransport ctx;
    SHT3xDevice device;
    const Status beginSt = device.begin(makeFrameConfig(ctx));
    TEST_ASSERT_TRUE_MESSAGE(beginSt.ok(), beginSt.msg);

    clearFrameLog(ctx);
    const Status st = device.writeAlertLimit(vector.kind, vector.temperatureC,
                                             vector.humidityPct);
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
    TEST_ASSERT_TRUE(ctx.sawAlertWritePayload);
    TEST_ASSERT_EQUAL_HEX16(SHT3xDevice::_commandForAlertWrite(vector.kind),
                            ctx.lastAlertWriteCommand);
    TEST_ASSERT_EQUAL_HEX16(vector.word, ctx.lastAlertWriteValue);

    const uint8_t data[2] = {
        static_cast<uint8_t>(ctx.lastAlertWriteValue >> 8),
        static_cast<uint8_t>(ctx.lastAlertWriteValue & 0xFF),
    };
    TEST_ASSERT_EQUAL_HEX8(SHT3xDevice::_crc8(data, 2), ctx.lastAlertWriteCrc);
  }
}

static Status frameBusResetFail(void*) {
  return Status::Error(Err::I2C_BUS, "bus reset failed", -88);
}

void test_long_idle_duration_gates_do_not_fail_after_half_range() {
  {
    SHT3xDevice device;
    device._periodMs = 1000u;
    device._periodicStartMs = 100u;
    device._lastFetchValid = false;
    const uint32_t nowMs = 100u + 0x80010000u;
    TEST_ASSERT_EQUAL_UINT32(nowMs, device._periodicReadyMs(nowMs));
  }

  {
    PreciseTimingTransport ctx;
    ctx.nowMs = 100u + 0x80010000u;
    ctx.nowUs = 10000u;
    ctx.readStatus = Status::Error(Err::I2C_NACK_READ, "not ready");
    Config cfg = makePreciseTimingConfig(ctx);
    cfg.transportCapabilities = TransportCapability::READ_HEADER_NACK;
    cfg.notReadyTimeoutMs = 5u;
    SHT3xDevice device;
    Status st = device.bind(cfg);
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
    device._mode = Mode::PERIODIC;
    device._periodicActive = true;
    device._periodMs = 0u;
    device._periodicStartMs = ctx.nowMs;
    device._notReadyStartMs = 100u;
    device._notReadyStartValid = true;
    JobRequest request;
    request.requestId = 415u;
    st = device.requestMeasurement(request);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    PollJobResult result;
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    advancePreciseTimeMs(ctx, 1u);
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::I2C_NACK_READ, st.code);
    TEST_ASSERT_TRUE(result.terminal);
    TEST_ASSERT_EQUAL(JobPhase::PERIODIC_READ, result.phase);
    TEST_ASSERT_EQUAL_UINT32(0u, device.totalNotReady());
  }

  {
    PreciseTimingTransport ctx;
    ctx.nowMs = 100u + 0x80010000u;
    ctx.nowUs = 10000u;
    Config cfg = makePreciseTimingConfig(ctx);
    cfg.recoverBackoffMs = 100u;
    SHT3xDevice device;
    Status st = device.bind(cfg);
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
    device._lastRecoverMs = 100u;
    device._lastRecoverValid = true;
    st = device.recover();
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
    TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes);
    TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);
    TEST_ASSERT_TRUE(device.hardwareStateValid());
  }
}

void test_begin_respects_configured_delay_after_soft_reset() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 100u;
  ctx.nowUs = 100000u;
  Config cfg = makePreciseTimingConfig(ctx);
  cfg.commandDelayMs = 7u;
  SHT3xDevice device;
  const Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_EQUAL_UINT32(3u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);
  TEST_ASSERT_EQUAL_UINT32(121u, ctx.nowMs);
  TEST_ASSERT_EQUAL_UINT32(121000u, ctx.nowUs);
  TEST_ASSERT_EQUAL_HEX16(cmd::CMD_READ_STATUS, ctx.lastCommand);
}

void test_begin_rejects_status_diagnostic_flags_without_startup_health() {
  struct DiagnosticCase {
    uint16_t statusRaw;
    Err expected;
  };
  static constexpr DiagnosticCase cases[] = {
      {cmd::STATUS_WRITE_CRC_ERROR, Err::WRITE_CRC_ERROR},
      {cmd::STATUS_COMMAND_ERROR, Err::COMMAND_FAILED},
  };

  for (const auto& testCase : cases) {
    PreciseTimingTransport ctx;
    ctx.nowMs = 100u;
    ctx.nowUs = 100000u;
    ctx.statusRaw = testCase.statusRaw;
    SHT3xDevice device;
    const Status st = device.begin(makePreciseTimingConfig(ctx));
    TEST_ASSERT_EQUAL(testCase.expected, st.code);
    TEST_ASSERT_FALSE(device.isInitialized());
    TEST_ASSERT_EQUAL(DriverState::UNINIT, device.state());
    TEST_ASSERT_FALSE(device.hardwareStateValid());
    TEST_ASSERT_EQUAL_UINT32(0u, device.totalSuccess());
    TEST_ASSERT_EQUAL_UINT32(0u, device.totalFailures());
    TEST_ASSERT_EQUAL_UINT32(0u, device.transportSuccess());
    TEST_ASSERT_EQUAL_UINT32(0u, device.transportFailures());
    TEST_ASSERT_EQUAL_UINT32(0u, device.protocolFailures());
    TEST_ASSERT_EQUAL_UINT32(3u, ctx.writes);
    TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);
  }
}

void test_probe_status_crc_is_counter_neutral() {
  FrameScriptTransport ctx;
  ctx.corruptStatusCrc = true;
  SHT3xDevice device;
  Status st = device.bind(makeFrameConfig(ctx));
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.probe();
  TEST_ASSERT_EQUAL(Err::CRC_MISMATCH, st.code);
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(0u, device.transportFailures());
  TEST_ASSERT_EQUAL_UINT32(0u, device.transportSuccess());
  TEST_ASSERT_EQUAL_UINT32(0u, device.protocolFailures());
  TEST_ASSERT_EQUAL(DriverState::READY, device.state());
}

void test_interface_reset_failure_invalidates_state_and_starts_tidle() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 1000u;
  ctx.nowUs = 1000000u;
  Config cfg = makePreciseTimingConfig(ctx);
  cfg.busReset = frameBusResetFail;
  SHT3xDevice device;
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(device.hardwareStateValid());
  const uint32_t logicalFailures = device.totalFailures();
  const uint32_t transportFailures = device.transportFailures();

  const uint32_t resetCompletedUs = ctx.nowUs;
  st = device.interfaceReset();
  TEST_ASSERT_EQUAL(Err::I2C_BUS, st.code);
  TEST_ASSERT_EQUAL_INT32(-88, st.detail);
  TEST_ASSERT_FALSE(device.hardwareStateValid());
  TEST_ASSERT_EQUAL_UINT32(resetCompletedUs, device._lastCommandUs);
  TEST_ASSERT_TRUE(device._lastCommandValid);
  TEST_ASSERT_EQUAL_UINT32(logicalFailures, device.totalFailures());
  TEST_ASSERT_EQUAL_UINT32(transportFailures, device.transportFailures());

  const uint32_t callbacks = ctx.writes + ctx.reads;
  JobRequest request;
  request.requestId = 416u;
  st = device.requestEnsureIdle(request);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  PollJobResult result;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(callbacks, ctx.writes + ctx.reads);
  advancePreciseTimeMs(ctx, 1u);
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(callbacks + 1u, ctx.writes + ctx.reads);
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

void test_begin_invalid_config_preserves_existing_binding_and_no_startup_health() {
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

  const DriverState stateBefore = device.state();
  const uint32_t totalFailuresBefore = device.totalFailures();
  const uint8_t consecutiveFailuresBefore = device.consecutiveFailures();
  const uint32_t lastOkBefore = device.lastOkMs();
  const uint32_t lastErrorBefore = device.lastErrorMs();
  const uint32_t lastBusActivityBefore = device.lastBusActivityMs();
  const Status cachedErrorBefore = device.lastError();
  const Config boundConfigBefore = device.getConfig();

  Config invalid = cfg;
  invalid.i2cTimeoutMs = 0;
  const Status st = device.begin(invalid);
  TEST_ASSERT_EQUAL(Err::INVALID_CONFIG, st.code);
  TEST_ASSERT_TRUE(device.isInitialized());
  TEST_ASSERT_EQUAL(stateBefore, device.state());
  TEST_ASSERT_EQUAL_UINT32(0u, device.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(totalFailuresBefore, device.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(consecutiveFailuresBefore, device.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(lastOkBefore, device.lastOkMs());
  TEST_ASSERT_EQUAL_UINT32(lastErrorBefore, device.lastErrorMs());
  TEST_ASSERT_EQUAL_UINT32(lastBusActivityBefore, device.lastBusActivityMs());
  TEST_ASSERT_EQUAL(cachedErrorBefore.code, device.lastError().code);
  TEST_ASSERT_TRUE(device._allowOfflineI2c);
  TEST_ASSERT_EQUAL(boundConfigBefore.i2cWrite, device.getConfig().i2cWrite);
  TEST_ASSERT_EQUAL(boundConfigBefore.i2cWriteRead, device.getConfig().i2cWriteRead);
  TEST_ASSERT_EQUAL_UINT32(boundConfigBefore.i2cTimeoutMs,
                           device.getConfig().i2cTimeoutMs);
  TEST_ASSERT_EQUAL_UINT8(boundConfigBefore.offlineThreshold,
                          device.getConfig().offlineThreshold);
}

void test_begin_i2c_failure_does_not_update_health() {
  FakeTransport bus;
  SHT3xDevice device;
  Config cfg = makeConfig(bus);
  bus.writeReadStatus = Status::Error(Err::I2C_TIMEOUT, "startup read timeout", -44);

  const Status st = device.begin(cfg);
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);
  TEST_ASSERT_EQUAL_INT32(-44, st.detail);
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

  cfg.recoverBackoffMs = 100;
  cfg.healthPolicy = static_cast<HealthPolicy>(0xFF);
  st = device.bind(cfg);
  TEST_ASSERT_EQUAL(Err::INVALID_CONFIG, st.code);
  TEST_ASSERT_FALSE(device.isInitialized());
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
  TEST_ASSERT_FALSE(device.hardwareStateValid());
  SettingsSnapshot snapshot;
  TEST_ASSERT_TRUE(device.getSettings(snapshot).ok());
  TEST_ASSERT_FALSE(snapshot.hardwareStateValid);
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

void test_low_level_command_helpers_bypass_periodic_guard_as_expert_api() {
  CommandCaptureTransport ctx;
  SHT3xDevice device;
  device._config.i2cWrite = captureWrite;
  device._config.i2cWriteRead = captureWriteRead;
  device._config.i2cUser = &ctx;
  device._config.nowMs = captureNowMs;
  device._config.nowUs = captureNowUs;
  device._config.cooperativeYield = captureYield;
  device._config.timeUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  device._config.commandDelayMs = 1;
  device._initialized = true;
  device._driverState = DriverState::READY;
  device._mode = Mode::PERIODIC;
  device._periodicActive = true;
  device._periodMs = 1000;

  Status st = device.writeCommand(cmd::CMD_CLEAR_STATUS);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(2u, ctx.lastWriteLen);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(cmd::CMD_CLEAR_STATUS >> 8), ctx.lastWrite[0]);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(cmd::CMD_CLEAR_STATUS & 0xFF), ctx.lastWrite[1]);
  TEST_ASSERT_TRUE(device._periodicActive);
  TEST_ASSERT_EQUAL(Mode::PERIODIC, device._mode);

  ctx.lastWriteLen = 0;
  st = device.writeCommandWithData(cmd::CMD_ALERT_WRITE_HIGH_SET, 0x1234);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT8(5u, ctx.lastWriteLen);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(cmd::CMD_ALERT_WRITE_HIGH_SET >> 8), ctx.lastWrite[0]);
  TEST_ASSERT_EQUAL_HEX8(static_cast<uint8_t>(cmd::CMD_ALERT_WRITE_HIGH_SET & 0xFF), ctx.lastWrite[1]);
  TEST_ASSERT_EQUAL_HEX8(0x12, ctx.lastWrite[2]);
  TEST_ASSERT_EQUAL_HEX8(0x34, ctx.lastWrite[3]);
  TEST_ASSERT_EQUAL_HEX8(SHT3xDevice::_crc8(&ctx.lastWrite[2], 2), ctx.lastWrite[4]);
  TEST_ASSERT_TRUE(device._periodicActive);
  TEST_ASSERT_EQUAL(Mode::PERIODIC, device._mode);

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
  TEST_ASSERT_TRUE(device._periodicActive);
  TEST_ASSERT_EQUAL(Mode::PERIODIC, device._mode);
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
  PreciseTimingTransport ctx;
  ctx.readStatus = Status::Error(Err::I2C_NACK_READ, "NACK read", 0);
  SHT3xDevice device;
  Config cfg = makePreciseTimingConfig(ctx);
  cfg.notReadyTimeoutMs = 5;
  cfg.transportCapabilities = TransportCapability::READ_HEADER_NACK;
  Status st = device.bind(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.startPeriodic(PeriodicRate::MPS_10,
                            Repeatability::HIGH_REPEATABILITY);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  device._notReadyStartMs = 0;
  device._notReadyStartValid = true;

  JobRequest request;
  request.requestId = 201;
  st = device.requestMeasurement(request);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  ctx.nowMs = device._measurementReadyMs;
  ctx.nowUs = ctx.nowMs * 1000u;
  PollJobResult result;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  advancePreciseTimeMs(ctx, 1);
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_NOT_EQUAL(Err::MEASUREMENT_NOT_READY, st.code);
  TEST_ASSERT_TRUE(result.terminal);
  TEST_ASSERT_TRUE(device.consecutiveFailures() > 0);
  TEST_ASSERT_TRUE(device._notReadyStartValid);
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
  PreciseTimingTransport ctx;
  ctx.nowMs = 100;
  ctx.nowUs = 100000;
  ctx.readStatus = Status::Error(Err::I2C_NACK_READ, "NACK read", 0);
  SHT3xDevice device;
  Config cfg = makePreciseTimingConfig(ctx);
  cfg.transportCapabilities = TransportCapability::READ_HEADER_NACK;
  Status st = device.bind(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.startPeriodic(PeriodicRate::MPS_10,
                            Repeatability::HIGH_REPEATABILITY);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  const uint32_t firstReady = device._periodicStartMs +
                              device.estimateMeasurementTimeMs() +
                              device._periodicFetchMarginMs();
  JobRequest request;
  request.requestId = 202;
  request.hasDeadline = true;
  request.deadlineMs = firstReady + device._periodMs +
                       device._periodicFetchMarginMs() + 6u;
  st = device.requestMeasurement(request);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  ctx.nowMs = device._measurementReadyMs;
  ctx.nowUs = ctx.nowMs * 1000u;
  PollJobResult result;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  advancePreciseTimeMs(ctx, 1);
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_TRUE(device._notReadyStartValid);
  TEST_ASSERT_EQUAL_UINT32(1u, device._notReadyCount);
  TEST_ASSERT_EQUAL_UINT8(0u, device.consecutiveFailures());

  ctx.readStatus = Status::Ok();
  ctx.readAdvanceMs = 5;
  ctx.readAdvanceUs = 5000;
  ctx.nowMs = device._measurementReadyMs;
  ctx.nowUs = ctx.nowMs * 1000u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  advancePreciseTimeMs(ctx, 1);
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::TIMEOUT, st.code);
  TEST_ASSERT_TRUE(result.terminal);
  TEST_ASSERT_EQUAL(JobEffect::NONE, result.effect);
  TEST_ASSERT_FALSE(device._notReadyStartValid);
  TEST_ASSERT_EQUAL_UINT32(0u, device._notReadyCount);

  ctx.readStatus = Status::Error(Err::I2C_NACK_READ, "NACK read", 0);
  ctx.readAdvanceMs = 0;
  ctx.readAdvanceUs = 0;
  JobRequest next;
  next.requestId = 203;
  st = device.requestMeasurement(next);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  advancePreciseTimeMs(ctx, 1);
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_FALSE(result.terminal);
  TEST_ASSERT_TRUE(device._notReadyStartValid);
  TEST_ASSERT_EQUAL_UINT32(1u, device._notReadyCount);
  TEST_ASSERT_EQUAL_UINT8(0u, device.consecutiveFailures());
}

void test_example_adapter_ambiguous_zero_bytes() {
  gMillis = 0;
  gMillisStep = 0;
  Wire._setRequestFromResult(0);
  uint8_t buf[3] = {};
  Status st = transport::wireWriteRead(0x44, nullptr, 0, buf, sizeof(buf), 10, &Wire);
  TEST_ASSERT_EQUAL(Err::I2C_ERROR, st.code);
  Wire._clearRequestFromOverride();

  st = transport::wireWriteRead(0x44, buf, 1, buf, sizeof(buf), 10, &Wire);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);
}

void test_wire_adapter_timeout_and_stop() {
  gMillis = 0;
  gMillisStep = 0;
  Wire.setTimeOut(123);
  Wire._clearClockSetCount();
  uint8_t buf[2] = {0x00, 0x00};
  Status st = transport::wireWrite(0x44, buf, sizeof(buf), 33, &Wire);
  TEST_ASSERT_TRUE(st.ok());
  TEST_ASSERT_EQUAL_UINT32(123u, Wire.getTimeOut());
  TEST_ASSERT_EQUAL_UINT32(0u, Wire._clockSetCount());
  TEST_ASSERT_TRUE(Wire._lastStopWasTrue());

  gMillis = 0;
  gMillisStep = 20;
  st = transport::wireWrite(0x44, buf, sizeof(buf), 10, &Wire);
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);
  TEST_ASSERT_EQUAL_INT32(20, st.detail);
  TEST_ASSERT_EQUAL_UINT32(123u, Wire.getTimeOut());
  gMillisStep = 0;
}

void test_wire_adapter_drains_partial_read() {
  gMillis = 0;
  gMillisStep = 0;
  Wire.setTimeOut(123);
  Wire._setRequestFromResult(2);
  Wire._clearReadCallCount();
  uint8_t buf[6] = {};
  Status st = transport::wireWriteRead(0x44, nullptr, 0, buf, sizeof(buf), 20, &Wire);
  TEST_ASSERT_EQUAL(Err::I2C_ERROR, st.code);
  TEST_ASSERT_EQUAL_UINT32(123u, Wire.getTimeOut());
  TEST_ASSERT_EQUAL_UINT32(2u, Wire._readCallCount());
  Wire._clearRequestFromOverride();

  gMillis = 0;
  gMillisStep = 30;
  Wire._setRequestFromResult(sizeof(buf));
  Wire._clearReadCallCount();
  st = transport::wireWriteRead(0x44, nullptr, 0, buf, sizeof(buf), 20, &Wire);
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);
  TEST_ASSERT_EQUAL_INT32(30, st.detail);
  TEST_ASSERT_EQUAL_UINT32(123u, Wire.getTimeOut());
  TEST_ASSERT_EQUAL_UINT32(sizeof(buf), Wire._readCallCount());
  Wire._clearRequestFromOverride();
  gMillisStep = 0;
}

void test_wire_adapter_rejects_invalid_buffers_and_timeout() {
  gMillis = 0;
  gMillisStep = 0;
  uint8_t buf[2] = {0x00, 0x00};

  Status st = transport::wireWrite(0x44, nullptr, sizeof(buf), 10, &Wire);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);

  st = transport::wireWrite(0x44, buf, 0, 10, &Wire);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);

  st = transport::wireWrite(0x44, buf, sizeof(buf), 0, &Wire);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);

  st = transport::wireWriteRead(0x44, nullptr, 0, nullptr, sizeof(buf), 10, &Wire);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);

  st = transport::wireWriteRead(0x44, nullptr, 0, buf, sizeof(buf), 0, &Wire);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);
}

void test_i2c_scanner_restores_timeout() {
  Wire.setTimeOut(77);

  TEST_ASSERT_TRUE(i2c_scanner::checkAddress(Wire, 0x44, 12));
  TEST_ASSERT_EQUAL_UINT32(77u, Wire.getTimeOut());

  TEST_ASSERT_FALSE(i2c_scanner::checkAddress(Wire, 0x07, 12));
  TEST_ASSERT_EQUAL_UINT32(77u, Wire.getTimeOut());

  const int count = i2c_scanner::scan(Wire, 14);
  TEST_ASSERT_EQUAL(112, count);
  TEST_ASSERT_EQUAL_UINT32(77u, Wire.getTimeOut());
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

  st = device.writeAlertLimit(AlertLimitKind::HIGH_SET, 25.0f, nanValue);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);
  TEST_ASSERT_FALSE(device._cachedSettings.alertValid[0]);

  const float infValue = std::numeric_limits<float>::infinity();
  st = device.writeAlertLimit(AlertLimitKind::HIGH_SET, infValue, 50.0f);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);
  TEST_ASSERT_FALSE(device._cachedSettings.alertValid[0]);

  st = device.writeAlertLimit(AlertLimitKind::HIGH_SET, 25.0f, infValue);
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
  st = device.requestMeasurement();
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_FALSE(device.measurementReady());

  bus.nowMs = requestMs;
  device.tick(bus.nowMs);
  TEST_ASSERT_FALSE(device.measurementReady());

  const uint32_t readyMs = device._measurementReadyMs;
  bus.nowMs = readyMs - 1U;
  device.tick(bus.nowMs);
  TEST_ASSERT_FALSE(device.measurementReady());

  bus.nowMs = readyMs;
  device.tick(bus.nowMs);
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

  bus.nowMs = device._measurementReadyMs;
  device.tick(bus.nowMs);
  TEST_ASSERT_EQUAL_UINT32(0u, device.notReadyCount());

  device.tick(bus.nowMs);
  TEST_ASSERT_EQUAL_UINT32(0u, device.notReadyCount());

  bus.nowMs += cfg.commandDelayMs;
  device.tick(bus.nowMs);

  TEST_ASSERT_FALSE(device.measurementReady());
  TEST_ASSERT_EQUAL_UINT32(1u, device.notReadyCount());
  TEST_ASSERT_EQUAL_UINT8(0u, device.consecutiveFailures());
  TEST_ASSERT_EQUAL(DriverState::READY, device.state());
}

void test_single_shot_poll_job_instruction_budget() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  clearFrameLog(ctx);
  st = device.requestMeasurement();
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  PollJobResult result;
  st = device.pollJob(ctx.nowMs, 0, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_TRUE(result.active);
  TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_TRUE(result.active);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_SINGLE_SHOT_NO_STRETCH_HIGH, ctx.commands[0]);

  ctx.nowMs = device._measurementReadyMs - 1U;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_TRUE(result.active);
  TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  ctx.nowMs = device._measurementReadyMs;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(result.completed);
  TEST_ASSERT_FALSE(result.active);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);
  TEST_ASSERT_TRUE(device.measurementReady());
}

void test_single_shot_poll_job_crc_mismatch_visible_status() {
  FrameScriptTransport ctx;
  ctx.corruptMeasurementCrc = true;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  clearFrameLog(ctx);
  st = device.requestMeasurement();
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);

  PollJobResult result;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_TRUE(result.active);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);

  ctx.nowMs = device._measurementReadyMs;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::CRC_MISMATCH, st.code);
  TEST_ASSERT_FALSE(result.active);
  TEST_ASSERT_FALSE(result.completed);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  TEST_ASSERT_FALSE(device.measurementReady());
  TEST_ASSERT_FALSE(device.measurementPending());
  TEST_ASSERT_EQUAL(Err::CRC_MISMATCH, device.measurementStatus().code);
  TEST_ASSERT_EQUAL(Err::CRC_MISMATCH, device.lastMeasurementStatus().code);

  const uint32_t readsAfterFailure = ctx.reads;
  ctx.nowMs += cfg.commandDelayMs;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::CRC_MISMATCH, st.code);
  TEST_ASSERT_FALSE(result.active);
  TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(readsAfterFailure, ctx.reads);
}

void test_periodic_fetch_poll_job_instruction_budget() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.startPeriodic(PeriodicRate::MPS_1, Repeatability::HIGH_REPEATABILITY);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  clearFrameLog(ctx);
  st = device.requestMeasurement();
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  PollJobResult result;
  st = device.pollJob(ctx.nowMs, 0, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_TRUE(result.active);
  TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  ctx.nowMs = device._measurementReadyMs - 1U;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_TRUE(result.active);
  TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  ctx.nowMs = device._measurementReadyMs;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_TRUE(result.active);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_FETCH_DATA, ctx.commands[0]);

  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_TRUE(result.active);
  TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  ctx.nowMs += cfg.commandDelayMs;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(result.completed);
  TEST_ASSERT_FALSE(result.active);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);
  TEST_ASSERT_TRUE(device.measurementReady());
}

void test_periodic_poll_job_tidle_uses_fractional_microsecond_clock() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 1000;
  ctx.nowUs = 1000000;
  ctx.writeAdvanceUs = 900;

  SHT3xDevice device;
  preparePreciseTimingDevice(device, ctx, Mode::PERIODIC);

  Status st = device.requestMeasurement();
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);

  PollJobResult result;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_FETCH_DATA, ctx.lastCommand);
  TEST_ASSERT_EQUAL_UINT32(1000900u, device._lastCommandUs);

  // A poll in the same millisecond must not turn the backwards mixed-clock
  // subtraction into a falsely elapsed tIDLE interval.
  ctx.writeAdvanceUs = 0;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  ctx.nowUs += 999u;
  ctx.nowMs = ctx.nowUs / 1000u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  ctx.nowUs += 1u;
  ctx.nowMs = ctx.nowUs / 1000u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(result.completed);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);
}

void test_periodic_poll_job_tidle_is_wrap_safe_in_microseconds() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 4000;
  ctx.nowUs = 0xFFFFFF00u;

  SHT3xDevice device;
  preparePreciseTimingDevice(device, ctx, Mode::PERIODIC);

  Status st = device.requestMeasurement();
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);

  PollJobResult result;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFF00u, device._lastCommandUs);

  ctx.nowUs += 999u;
  ++ctx.nowMs;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  ctx.nowUs += 1u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(result.completed);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);
}

void test_single_shot_poll_timestamps_follow_transport_completion() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 100;
  ctx.nowUs = 100000;
  ctx.writeAdvanceMs = 7;
  ctx.writeAdvanceUs = 7000;

  SHT3xDevice device;
  preparePreciseTimingDevice(device, ctx, Mode::SINGLE_SHOT);

  Status st = device.requestMeasurement();
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);

  PollJobResult result;
  st = device.pollJob(100u, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(107u + device.estimateMeasurementTimeMs(),
                           device._measurementReadyMs);

  // The old pre-transport deadline would have expired here. No read is legal
  // until the full conversion duration after command completion.
  ctx.nowMs = 100u + device.estimateMeasurementTimeMs();
  ctx.nowUs = ctx.nowMs * 1000u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  ctx.nowMs = device._measurementReadyMs;
  ctx.nowUs = ctx.nowMs * 1000u;
  ctx.readAdvanceMs = 4;
  ctx.readAdvanceUs = 4000;
  const uint32_t expectedCompletedMs = ctx.nowMs + ctx.readAdvanceMs;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(result.completed);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);
  TEST_ASSERT_EQUAL_UINT32(expectedCompletedMs, device.sampleTimestampMs());
}

void test_single_shot_poll_ready_time_wraps_milliseconds() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 0xFFFFFFF8u;
  ctx.nowUs = 1000u;
  ctx.rawTemperature = 0x1357;
  ctx.rawHumidity = 0x2468;
  SHT3xDevice device;
  Status st = device.bind(makePreciseTimingConfig(ctx));
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  JobRequest request;
  request.requestId = 401;
  st = device.requestMeasurement(request);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  PollJobResult result;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL_UINT32(8u, device._measurementReadyMs);
  TEST_ASSERT_EQUAL_HEX8(cmd::I2C_ADDR_LOW, ctx.lastAddress);

  ctx.nowMs = 7u;
  ctx.nowUs = 2000u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  ctx.nowMs = 8u;
  ctx.nowUs = 2001u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(result.terminal);
  TEST_ASSERT_TRUE(result.completed);
  TEST_ASSERT_EQUAL_UINT32(401u, result.requestId);
  TEST_ASSERT_EQUAL(JobPhase::SINGLE_SHOT_READ, result.phase);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);
  TEST_ASSERT_EQUAL_UINT32(8u, device.sampleTimestampMs());
}

void test_milli_conversions_cover_endpoints_midpoint_and_rounding() {
  struct MilliVector {
    uint16_t raw;
    int32_t temperatureMilliCelsius;
    int32_t humidityMilliPercent;
  };
  static constexpr MilliVector vectors[] = {
      {0u, -45000, 0},
      {1u, -44997, 2},
      {16851u, -2, 25713},
      {16852u, 0, 25715},
      {32767u, 42499, 49999},
      {32768u, 42501, 50001},
      {65534u, 129997, 99998},
      {65535u, 130000, 100000},
  };

  for (const auto& vector : vectors) {
    TEST_ASSERT_EQUAL_INT32(
        vector.temperatureMilliCelsius,
        SHT3xDevice::convertTemperatureMilliCelsius(vector.raw));
    TEST_ASSERT_EQUAL_INT32(
        vector.humidityMilliPercent,
        SHT3xDevice::convertHumidityMilliPercent(vector.raw));
  }

  // Values immediately below and above the half-denominator threshold verify
  // nearest-integer rounding instead of truncation.
  TEST_ASSERT_EQUAL_INT32(-27379,
      SHT3xDevice::convertTemperatureMilliCelsius(6599u));
  TEST_ASSERT_EQUAL_INT32(-27621,
      SHT3xDevice::convertTemperatureMilliCelsius(6508u));
  TEST_ASSERT_EQUAL_INT32(2621,
      SHT3xDevice::convertHumidityMilliPercent(1718u));
  TEST_ASSERT_EQUAL_INT32(17379,
      SHT3xDevice::convertHumidityMilliPercent(11389u));
}

void test_measurement_milli_and_float_use_unrounded_raw_sample() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 200;
  ctx.nowUs = 200000;
  ctx.rawTemperature = 1u;
  ctx.rawHumidity = 1u;

  SHT3xDevice device;
  preparePreciseTimingDevice(device, ctx, Mode::SINGLE_SHOT);

  MeasurementMilli milli;
  Status st = device.getMeasurementMilli(milli);
  TEST_ASSERT_EQUAL(Err::MEASUREMENT_NOT_READY, st.code);

  st = device.requestMeasurement();
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  PollJobResult result;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);

  ctx.nowMs = device._measurementReadyMs;
  ctx.nowUs = ctx.nowMs * 1000u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(result.completed);

  CompensatedSample centi;
  st = device.getCompensatedSample(centi);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_EQUAL_INT32(-4500, centi.tempC_x100);
  TEST_ASSERT_EQUAL_UINT32(0u, centi.humidityPct_x100);

  Measurement measurement;
  st = device.getMeasurement(measurement);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  const float expectedTemperature = -45.0f + (175.0f / 65535.0f);
  const float expectedHumidity = 100.0f / 65535.0f;
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, expectedTemperature,
                           measurement.temperatureC);
  TEST_ASSERT_FLOAT_WITHIN(0.0001f, expectedHumidity,
                           measurement.humidityPct);

  st = device.getMeasurementMilli(milli);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_EQUAL_INT32(-44997, milli.temperatureMilliCelsius);
  TEST_ASSERT_EQUAL_INT32(2, milli.humidityMilliPercent);
}

void test_bind_rebind_and_end_are_zero_i2c_local_operations() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 10;
  ctx.nowUs = 10000;
  SHT3xDevice device;

  Config cfg = makePreciseTimingConfig(ctx);
  cfg.mode = Mode::PERIODIC;
  Status st = device.bind(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(device.isInitialized());
  TEST_ASSERT_FALSE(device.hardwareStateValid());
  TEST_ASSERT_EQUAL(Mode::SINGLE_SHOT, device.getConfig().mode);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  st = device.bind(device.getConfig());
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(device.isInitialized());
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes + ctx.reads);

  Config invalidRebind = device.getConfig();
  invalidRebind.nowUs = nullptr;
  st = device.bind(invalidRebind);
  TEST_ASSERT_EQUAL(Err::INVALID_CONFIG, st.code);
  TEST_ASSERT_TRUE(device.isInitialized());
  TEST_ASSERT_NOT_EQUAL(nullptr, device.getConfig().nowUs);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes + ctx.reads);

  JobRequest abandoned;
  abandoned.requestId = 11;
  st = device.requestMeasurement(abandoned);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);

  Config rebound = makePreciseTimingConfig(ctx, cmd::I2C_ADDR_HIGH);
  rebound.i2cTimeoutMs = 17;
  st = device.bind(rebound);
  TEST_ASSERT_EQUAL(Err::BUSY, st.code);
  PollJobResult cancelled;
  st = device.cancelJob(CancelReason::REQUESTED, cancelled);
  TEST_ASSERT_EQUAL(Err::CANCELLED, st.code);
  TEST_ASSERT_TRUE(cancelled.terminal);
  TEST_ASSERT_EQUAL_UINT32(11u, cancelled.requestId);
  st = device.bind(rebound);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  PollJobResult result;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::MEASUREMENT_NOT_READY, st.code);
  TEST_ASSERT_FALSE(result.active);
  TEST_ASSERT_FALSE(result.terminal);
  TEST_ASSERT_EQUAL_UINT32(0u, result.requestId);

  JobRequest request;
  request.requestId = 12;
  st = device.requestMeasurement(request);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
  TEST_ASSERT_EQUAL_HEX8(cmd::I2C_ADDR_HIGH, ctx.lastAddress);
  TEST_ASSERT_EQUAL_UINT32(17u, ctx.lastTimeoutMs);

  device.end();
  TEST_ASSERT_FALSE(device.isInitialized());
  TEST_ASSERT_EQUAL(DriverState::UNINIT, device.state());
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);

  st = device.bind(rebound);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
}

void test_repeated_begin_and_end_reinitialize_deterministically() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 10u;
  ctx.nowUs = 10000u;
  const Config cfg = makePreciseTimingConfig(ctx);
  SHT3xDevice device;

  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  const uint32_t firstCallbacks = ctx.writes + ctx.reads;
  TEST_ASSERT_GREATER_THAN_UINT32(0u, firstCallbacks);
  TEST_ASSERT_TRUE(device.hardwareStateValid());

  st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  const uint32_t secondCallbacks = ctx.writes + ctx.reads;
  TEST_ASSERT_GREATER_THAN_UINT32(firstCallbacks, secondCallbacks);
  TEST_ASSERT_TRUE(device.hardwareStateValid());
  TEST_ASSERT_EQUAL(DriverState::READY, device.state());

  device.end();
  TEST_ASSERT_EQUAL_UINT32(secondCallbacks, ctx.writes + ctx.reads);
  TEST_ASSERT_EQUAL(DriverState::UNINIT, device.state());
  st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_GREATER_THAN_UINT32(secondCallbacks, ctx.writes + ctx.reads);
  TEST_ASSERT_TRUE(device.hardwareStateValid());
}

void test_owner_job_identity_zero_budget_and_exactly_once_terminal() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 20;
  ctx.nowUs = 20000;
  ctx.rawTemperature = 0x1234;
  ctx.rawHumidity = 0x5678;
  SHT3xDevice device;
  Status st = device.bind(makePreciseTimingConfig(ctx));
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  JobRequest invalid;
  st = device.requestMeasurement(invalid);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);
  st = device.requestEnsureIdle(invalid);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes + ctx.reads);

  JobRequest request;
  request.requestId = 42;
  st = device.requestMeasurement(request);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);

  PollJobResult result;
  st = device.pollJob(ctx.nowMs, 0, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_TRUE(result.active);
  TEST_ASSERT_FALSE(result.terminal);
  TEST_ASSERT_EQUAL_UINT32(42u, result.requestId);
  TEST_ASSERT_EQUAL(JobType::MEASUREMENT, result.type);
  TEST_ASSERT_EQUAL(JobPhase::SINGLE_SHOT_COMMAND, result.phase);
  TEST_ASSERT_EQUAL(JobOutcome::ACTIVE, result.outcome);
  TEST_ASSERT_EQUAL(JobEffect::NONE, result.effect);
  TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes + ctx.reads);

  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_TRUE(result.active);
  TEST_ASSERT_FALSE(result.terminal);
  TEST_ASSERT_EQUAL_UINT32(42u, result.requestId);
  TEST_ASSERT_EQUAL(JobPhase::SINGLE_SHOT_COMMAND, result.phase);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes + ctx.reads);

  ctx.nowMs = device._measurementReadyMs;
  ctx.nowUs = ctx.nowMs * 1000u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_FALSE(result.active);
  TEST_ASSERT_TRUE(result.completed);
  TEST_ASSERT_TRUE(result.terminal);
  TEST_ASSERT_EQUAL_UINT32(42u, result.requestId);
  TEST_ASSERT_EQUAL(JobType::MEASUREMENT, result.type);
  TEST_ASSERT_EQUAL(JobPhase::SINGLE_SHOT_READ, result.phase);
  TEST_ASSERT_EQUAL(JobOutcome::SUCCEEDED, result.outcome);
  TEST_ASSERT_EQUAL(JobEffect::NONE, result.effect);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(2u, ctx.writes + ctx.reads);

  const uint32_t callbacksAfterTerminal = ctx.writes + ctx.reads;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_FALSE(result.active);
  TEST_ASSERT_FALSE(result.terminal);
  TEST_ASSERT_FALSE(result.completed);
  TEST_ASSERT_EQUAL_UINT32(0u, result.requestId);
  TEST_ASSERT_EQUAL(JobType::NONE, result.type);
  TEST_ASSERT_EQUAL(JobOutcome::NONE, result.outcome);
  TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(callbacksAfterTerminal, ctx.writes + ctx.reads);
}

void test_invalid_cancel_reason_is_zero_i2c_and_leaves_job_active() {
  PreciseTimingTransport ctx;
  SHT3xDevice device;
  Status st = device.bind(makePreciseTimingConfig(ctx));
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  JobRequest request;
  request.requestId = 406;
  st = device.requestMeasurement(request);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);

  PollJobResult result;
  st = device.cancelJob(static_cast<CancelReason>(0xFFu), result);
  TEST_ASSERT_EQUAL(Err::INVALID_PARAM, st.code);
  TEST_ASSERT_TRUE(result.active);
  TEST_ASSERT_FALSE(result.terminal);
  TEST_ASSERT_EQUAL_UINT32(406u, result.requestId);
  TEST_ASSERT_EQUAL(JobPhase::SINGLE_SHOT_COMMAND, result.phase);
  TEST_ASSERT_EQUAL(JobOutcome::ACTIVE, result.outcome);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes + ctx.reads);

  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL_UINT32(406u, result.requestId);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes + ctx.reads);
}

void test_single_shot_transfer_stage_failures_report_phase_and_effect() {
  {
    PreciseTimingTransport ctx;
    ctx.nowMs = 25u;
    ctx.nowUs = 25000u;
    ctx.failCommand = cmd::CMD_SINGLE_SHOT_NO_STRETCH_HIGH;
    ctx.failCommandStatus =
        Status::Error(Err::I2C_TIMEOUT, "measurement command timeout", -41);
    SHT3xDevice device;
    Status st = device.bind(makePreciseTimingConfig(ctx));
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
    JobRequest request;
    request.requestId = 402;
    st = device.requestMeasurement(request);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    PollJobResult result;
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);
    TEST_ASSERT_EQUAL_INT32(-41, st.detail);
    TEST_ASSERT_TRUE(result.terminal);
    TEST_ASSERT_EQUAL_UINT32(402u, result.requestId);
    TEST_ASSERT_EQUAL(JobPhase::SINGLE_SHOT_COMMAND, result.phase);
    TEST_ASSERT_EQUAL(JobOutcome::TIMED_OUT, result.outcome);
    TEST_ASSERT_EQUAL(JobEffect::DEVICE_STATE_INDETERMINATE, result.effect);
    TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
    TEST_ASSERT_FALSE(device.hardwareStateValid());
  }

  {
    PreciseTimingTransport ctx;
    ctx.nowMs = 50u;
    ctx.nowUs = 50000u;
    ctx.readStatus = Status::Error(Err::I2C_BUS, "measurement read bus error", -42);
    SHT3xDevice device;
    Status st = device.bind(makePreciseTimingConfig(ctx));
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
    JobRequest request;
    request.requestId = 403;
    st = device.requestMeasurement(request);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    PollJobResult result;
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    ctx.nowMs = device._measurementReadyMs;
    ctx.nowUs += device.estimateMeasurementTimeMs() * 1000u;
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::I2C_BUS, st.code);
    TEST_ASSERT_EQUAL_INT32(-42, st.detail);
    TEST_ASSERT_TRUE(result.terminal);
    TEST_ASSERT_EQUAL_UINT32(403u, result.requestId);
    TEST_ASSERT_EQUAL(JobPhase::SINGLE_SHOT_READ, result.phase);
    TEST_ASSERT_EQUAL(JobOutcome::FAILED, result.outcome);
    TEST_ASSERT_EQUAL(JobEffect::RESULT_MAY_BE_PENDING, result.effect);
    TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
    TEST_ASSERT_FALSE(device.hasSample());
    TEST_ASSERT_FALSE(device.hardwareStateValid());
  }
}

void test_cancel_before_and_after_command_is_zero_i2c_and_preserves_sample() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 30;
  ctx.nowUs = 30000;
  ctx.rawTemperature = 0x1111;
  ctx.rawHumidity = 0x2222;
  SHT3xDevice device;
  Status st = device.begin(makePreciseTimingConfig(ctx));
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(device.hardwareStateValid());

  JobRequest first;
  first.requestId = 1;
  st = device.requestMeasurement(first);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  PollJobResult result;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  ctx.nowMs = device._measurementReadyMs;
  ctx.nowUs = ctx.nowMs * 1000u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  RawSample expected;
  st = device.getRawSample(expected);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_EQUAL_HEX16(0x1111, expected.rawTemperature);
  TEST_ASSERT_EQUAL_HEX16(0x2222, expected.rawHumidity);

  JobRequest beforeCommand;
  beforeCommand.requestId = 2;
  st = device.requestMeasurement(beforeCommand);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  uint32_t callbacks = ctx.writes + ctx.reads;
  st = device.cancelJob(CancelReason::REQUESTED, result);
  TEST_ASSERT_EQUAL(Err::CANCELLED, st.code);
  TEST_ASSERT_TRUE(result.terminal);
  TEST_ASSERT_FALSE(result.active);
  TEST_ASSERT_EQUAL_UINT32(2u, result.requestId);
  TEST_ASSERT_EQUAL(JobPhase::SINGLE_SHOT_COMMAND, result.phase);
  TEST_ASSERT_EQUAL(JobOutcome::CANCELLED, result.outcome);
  TEST_ASSERT_EQUAL(JobEffect::NONE, result.effect);
  TEST_ASSERT_EQUAL_UINT32(callbacks, ctx.writes + ctx.reads);

  RawSample actual;
  st = device.getRawSample(actual);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_EQUAL_HEX16(expected.rawTemperature, actual.rawTemperature);
  TEST_ASSERT_EQUAL_HEX16(expected.rawHumidity, actual.rawHumidity);

  JobRequest afterCommand;
  afterCommand.requestId = 3;
  st = device.requestMeasurement(afterCommand);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL(JobPhase::SINGLE_SHOT_COMMAND, result.phase);
  callbacks = ctx.writes + ctx.reads;

  st = device.cancelJob(CancelReason::REQUESTED, result);
  TEST_ASSERT_EQUAL(Err::CANCELLED, st.code);
  TEST_ASSERT_TRUE(result.terminal);
  TEST_ASSERT_FALSE(result.active);
  TEST_ASSERT_EQUAL_UINT32(3u, result.requestId);
  TEST_ASSERT_EQUAL(JobPhase::SINGLE_SHOT_CONVERSION, result.phase);
  TEST_ASSERT_EQUAL(JobOutcome::CANCELLED, result.outcome);
  TEST_ASSERT_EQUAL(JobEffect::RESULT_MAY_BE_PENDING, result.effect);
  TEST_ASSERT_EQUAL_UINT32(callbacks, ctx.writes + ctx.reads);

  st = device.getRawSample(actual);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_EQUAL_HEX16(expected.rawTemperature, actual.rawTemperature);
  TEST_ASSERT_EQUAL_HEX16(expected.rawHumidity, actual.rawHumidity);

  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::CANCELLED, st.code);
  TEST_ASSERT_FALSE(result.terminal);
  TEST_ASSERT_EQUAL_UINT32(0u, result.requestId);
  TEST_ASSERT_EQUAL_UINT32(callbacks, ctx.writes + ctx.reads);
}

void test_cancel_periodic_read_is_zero_i2c_and_preserves_cached_sample() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 100u;
  ctx.nowUs = 100000u;
  ctx.rawTemperature = 0x1111;
  ctx.rawHumidity = 0x2222;
  SHT3xDevice device;
  Status st = device.begin(makePreciseTimingConfig(ctx));
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  JobRequest first;
  first.requestId = 404;
  st = device.requestMeasurement(first);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  PollJobResult result;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  ctx.nowMs = device._measurementReadyMs;
  ctx.nowUs += device.estimateMeasurementTimeMs() * 1000u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  RawSample expected;
  st = device.getRawSample(expected);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  st = device.startPeriodic(PeriodicRate::MPS_10,
                            Repeatability::HIGH_REPEATABILITY);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  JobRequest periodic;
  periodic.requestId = 405;
  st = device.requestMeasurement(periodic);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  ctx.nowMs = device._measurementReadyMs;
  ctx.nowUs += device.estimateMeasurementTimeMs() * 1000u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL(JobPhase::PERIODIC_READ, device._measurementPhase);
  const uint32_t callbacksBeforeCancel = ctx.writes + ctx.reads;

  st = device.cancelJob(CancelReason::REQUESTED, result);
  TEST_ASSERT_EQUAL(Err::CANCELLED, st.code);
  TEST_ASSERT_TRUE(result.terminal);
  TEST_ASSERT_EQUAL_UINT32(405u, result.requestId);
  TEST_ASSERT_EQUAL(JobType::MEASUREMENT, result.type);
  TEST_ASSERT_EQUAL(JobPhase::PERIODIC_READ, result.phase);
  TEST_ASSERT_EQUAL(JobOutcome::CANCELLED, result.outcome);
  TEST_ASSERT_EQUAL(JobEffect::RESULT_MAY_BE_PENDING, result.effect);
  TEST_ASSERT_EQUAL_UINT32(callbacksBeforeCancel, ctx.writes + ctx.reads);
  TEST_ASSERT_FALSE(device.measurementReady());
  TEST_ASSERT_TRUE(device.hasSample());

  RawSample actual;
  st = device.getRawSample(actual);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_EQUAL_HEX16(expected.rawTemperature, actual.rawTemperature);
  TEST_ASSERT_EQUAL_HEX16(expected.rawHumidity, actual.rawHumidity);
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::CANCELLED, st.code);
  TEST_ASSERT_FALSE(result.terminal);
  TEST_ASSERT_EQUAL_UINT32(0u, result.requestId);
  TEST_ASSERT_EQUAL_UINT32(callbacksBeforeCancel, ctx.writes + ctx.reads);
}

void test_job_deadlines_before_work_inside_callback_and_across_wrap() {
  {
    PreciseTimingTransport ctx;
    ctx.nowMs = 100;
    ctx.nowUs = 100000;
    SHT3xDevice device;
    Status st = device.bind(makePreciseTimingConfig(ctx));
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

    JobRequest request;
    request.requestId = 51;
    request.hasDeadline = true;
    request.deadlineMs = ctx.nowMs;
    st = device.requestMeasurement(request);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    PollJobResult result;
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::TIMEOUT, st.code);
    TEST_ASSERT_TRUE(result.terminal);
    TEST_ASSERT_EQUAL_UINT32(51u, result.requestId);
    TEST_ASSERT_EQUAL(JobPhase::SINGLE_SHOT_COMMAND, result.phase);
    TEST_ASSERT_EQUAL(JobOutcome::TIMED_OUT, result.outcome);
    TEST_ASSERT_EQUAL(JobEffect::NONE, result.effect);
    TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
    TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes + ctx.reads);
  }

  {
    PreciseTimingTransport ctx;
    ctx.nowMs = 0xFFFFFFF0u;
    ctx.nowUs = 1000;
    SHT3xDevice device;
    Status st = device.bind(makePreciseTimingConfig(ctx));
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

    JobRequest request;
    request.requestId = 52;
    request.hasDeadline = true;
    request.deadlineMs = 5u;
    st = device.requestMeasurement(request);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    PollJobResult result;
    st = device.pollJob(ctx.nowMs, 0, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    TEST_ASSERT_TRUE(result.active);
    ctx.nowMs = 4u;
    st = device.pollJob(ctx.nowMs, 0, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    TEST_ASSERT_TRUE(result.active);
    TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes + ctx.reads);
    ctx.nowMs = 5u;
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::TIMEOUT, st.code);
    TEST_ASSERT_TRUE(result.terminal);
    TEST_ASSERT_EQUAL_UINT32(52u, result.requestId);
    TEST_ASSERT_EQUAL(JobOutcome::TIMED_OUT, result.outcome);
    TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
    TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes + ctx.reads);
  }

  {
    PreciseTimingTransport ctx;
    ctx.nowMs = 200;
    ctx.nowUs = 200000;
    ctx.writeAdvanceMs = 6;
    ctx.writeAdvanceUs = 6000;
    SHT3xDevice device;
    Status st = device.bind(makePreciseTimingConfig(ctx));
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

    JobRequest request;
    request.requestId = 53;
    request.hasDeadline = true;
    request.deadlineMs = 205;
    st = device.requestMeasurement(request);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    PollJobResult result;
    st = device.pollJob(200u, 1, result);
    TEST_ASSERT_EQUAL(Err::TIMEOUT, st.code);
    TEST_ASSERT_TRUE(result.terminal);
    TEST_ASSERT_EQUAL_UINT32(53u, result.requestId);
    TEST_ASSERT_EQUAL(JobPhase::SINGLE_SHOT_CONVERSION, result.phase);
    TEST_ASSERT_EQUAL(JobOutcome::TIMED_OUT, result.outcome);
    TEST_ASSERT_EQUAL(JobEffect::RESULT_MAY_BE_PENDING, result.effect);
    TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
    TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes);
    TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
  }

  {
    PreciseTimingTransport ctx;
    ctx.nowMs = 300;
    ctx.nowUs = 300000;
    ctx.rawTemperature = 0x1111;
    ctx.rawHumidity = 0x2222;
    SHT3xDevice device;
    Status st = device.begin(makePreciseTimingConfig(ctx));
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
    TEST_ASSERT_TRUE(device.hardwareStateValid());

    JobRequest initial;
    initial.requestId = 54;
    st = device.requestMeasurement(initial);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    PollJobResult result;
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    ctx.nowMs = device._measurementReadyMs;
    ctx.nowUs = ctx.nowMs * 1000u;
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
    RawSample previous;
    st = device.getRawSample(previous);
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

    ctx.rawTemperature = 0xAAAA;
    ctx.rawHumidity = 0xBBBB;
    ctx.readAdvanceMs = 6;
    ctx.readAdvanceUs = 6000;
    JobRequest late;
    late.requestId = 55;
    late.hasDeadline = true;
    late.deadlineMs = ctx.nowMs + device.estimateMeasurementTimeMs() + 5u;
    st = device.requestMeasurement(late);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    ctx.nowMs = device._measurementReadyMs;
    ctx.nowUs = ctx.nowMs * 1000u;
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::TIMEOUT, st.code);
    TEST_ASSERT_TRUE(result.terminal);
    TEST_ASSERT_EQUAL_UINT32(55u, result.requestId);
    TEST_ASSERT_EQUAL(JobPhase::SINGLE_SHOT_READ, result.phase);
    TEST_ASSERT_EQUAL(JobOutcome::TIMED_OUT, result.outcome);
    TEST_ASSERT_EQUAL(JobEffect::NONE, result.effect);
    TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
    TEST_ASSERT_TRUE(device.hardwareStateValid());

    RawSample afterLateRead;
    st = device.getRawSample(afterLateRead);
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
    TEST_ASSERT_EQUAL_HEX16(previous.rawTemperature, afterLateRead.rawTemperature);
    TEST_ASSERT_EQUAL_HEX16(previous.rawHumidity, afterLateRead.rawHumidity);
  }
}

void test_periodic_not_ready_callback_crossing_deadline_terminates_once() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 100u;
  ctx.nowUs = 100000u;
  ctx.readAdvanceMs = 2u;
  ctx.readAdvanceUs = 2000u;
  ctx.readStatus = Status::Error(Err::I2C_NACK_READ, "frame not ready");
  Config cfg = makePreciseTimingConfig(ctx);
  cfg.transportCapabilities = TransportCapability::READ_HEADER_NACK;
  SHT3xDevice device;
  Status st = device.bind(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  device._mode = Mode::PERIODIC;
  device._periodicActive = true;
  device._periodMs = 0u;
  device._periodicStartMs = ctx.nowMs;
  device._hardwareStateValid = true;

  JobRequest request;
  request.requestId = 407;
  request.hasDeadline = true;
  request.deadlineMs = 102u;
  st = device.requestMeasurement(request);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  PollJobResult result;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL(JobPhase::PERIODIC_READ, device._measurementPhase);
  advancePreciseTimeMs(ctx, 1u);
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::TIMEOUT, st.code);
  TEST_ASSERT_TRUE(result.terminal);
  TEST_ASSERT_EQUAL_UINT32(407u, result.requestId);
  TEST_ASSERT_EQUAL(JobPhase::PERIODIC_READ, result.phase);
  TEST_ASSERT_EQUAL(JobOutcome::TIMED_OUT, result.outcome);
  TEST_ASSERT_EQUAL(JobEffect::NONE, result.effect);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(1u, device.totalNotReady());
  TEST_ASSERT_TRUE(device.hardwareStateValid());

  const uint32_t callbacks = ctx.writes + ctx.reads;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::TIMEOUT, st.code);
  TEST_ASSERT_FALSE(result.terminal);
  TEST_ASSERT_EQUAL_UINT32(0u, result.requestId);
  TEST_ASSERT_EQUAL_UINT32(callbacks, ctx.writes + ctx.reads);
}

void test_request_ensure_idle_is_staged_and_one_callback_bounded() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 400;
  ctx.nowUs = 400000;
  SHT3xDevice device;
  Status st = device.bind(makePreciseTimingConfig(ctx));
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  JobRequest request;
  request.requestId = 61;
  st = device.requestEnsureIdle(request);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes + ctx.reads);

  PeriodicRate originalRate = PeriodicRate::MPS_10;
  st = device.getPeriodicRate(originalRate);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.setPeriodicRate(PeriodicRate::MPS_4);
  TEST_ASSERT_EQUAL(Err::BUSY, st.code);
  PeriodicRate retainedRate = PeriodicRate::MPS_10;
  st = device.getPeriodicRate(retainedRate);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_EQUAL(originalRate, retainedRate);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.writes + ctx.reads);

  PollJobResult result;
  st = device.pollJob(ctx.nowMs, 8, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL(JobPhase::ENSURE_BREAK_COMMAND, result.phase);
  TEST_ASSERT_EQUAL(JobEffect::DEVICE_STATE_CHANGED, result.effect);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(0u, ctx.reads);
  TEST_ASSERT_EQUAL_HEX16(cmd::CMD_BREAK, ctx.lastCommand);

  SettingsSnapshot duringEnsure;
  st = device.getSettings(duringEnsure);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_FALSE(duringEnsure.measurementPending);
  TEST_ASSERT_FALSE(duringEnsure.measurementReady);
  TEST_ASSERT_EQUAL_UINT32(0u, duringEnsure.measurementReadyMs);

  st = device.pollJob(ctx.nowMs, 8, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL(JobPhase::ENSURE_BREAK_WAIT, result.phase);
  TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes + ctx.reads);

  advancePreciseTimeMs(ctx, 1);
  st = device.pollJob(ctx.nowMs, 8, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL(JobPhase::ENSURE_BREAK_WAIT, result.phase);
  TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes + ctx.reads);

  st = device.pollJob(ctx.nowMs, 8, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL(JobPhase::ENSURE_RESET_COMMAND, result.phase);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(2u, ctx.writes);
  TEST_ASSERT_EQUAL_HEX16(cmd::CMD_SOFT_RESET, ctx.lastCommand);

  st = device.pollJob(ctx.nowMs, 8, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL(JobPhase::ENSURE_RESET_WAIT, result.phase);
  TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(2u, ctx.writes + ctx.reads);

  advancePreciseTimeMs(ctx, 2);
  st = device.pollJob(ctx.nowMs, 8, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL(JobPhase::ENSURE_RESET_WAIT, result.phase);
  TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(2u, ctx.writes + ctx.reads);

  st = device.pollJob(ctx.nowMs, 8, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL(JobPhase::ENSURE_STATUS_COMMAND, result.phase);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(3u, ctx.writes);
  TEST_ASSERT_EQUAL_HEX16(cmd::CMD_READ_STATUS, ctx.lastCommand);

  st = device.pollJob(ctx.nowMs, 8, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL(JobPhase::ENSURE_STATUS_READ, result.phase);
  TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(3u, ctx.writes + ctx.reads);

  advancePreciseTimeMs(ctx, 1);
  st = device.pollJob(ctx.nowMs, 8, result);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(result.terminal);
  TEST_ASSERT_FALSE(result.active);
  TEST_ASSERT_EQUAL_UINT32(61u, result.requestId);
  TEST_ASSERT_EQUAL(JobType::ENSURE_IDLE, result.type);
  TEST_ASSERT_EQUAL(JobPhase::ENSURE_STATUS_READ, result.phase);
  TEST_ASSERT_EQUAL(JobOutcome::SUCCEEDED, result.outcome);
  TEST_ASSERT_EQUAL(JobEffect::DEVICE_STATE_CHANGED, result.effect);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(3u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);
  TEST_ASSERT_TRUE(device.hardwareStateValid());
  TEST_ASSERT_EQUAL(Mode::SINGLE_SHOT, device.getConfig().mode);
}

void test_ensure_idle_stage_failures_report_phase_and_effect() {
  {
    PreciseTimingTransport ctx;
    ctx.nowMs = 25;
    ctx.nowUs = 25000;
    SHT3xDevice device;
    Status st = device.bind(makePreciseTimingConfig(ctx));
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
    JobRequest request;
    request.requestId = 75;
    st = device.requestEnsureIdle(request);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    PollJobResult result;
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    const uint32_t callbacksAfterBreak = ctx.writes + ctx.reads;
    st = device.cancelJob(CancelReason::REQUESTED, result);
    TEST_ASSERT_EQUAL(Err::CANCELLED, st.code);
    TEST_ASSERT_TRUE(result.terminal);
    TEST_ASSERT_EQUAL_UINT32(75u, result.requestId);
    TEST_ASSERT_EQUAL(JobType::ENSURE_IDLE, result.type);
    TEST_ASSERT_EQUAL(JobPhase::ENSURE_BREAK_WAIT, result.phase);
    TEST_ASSERT_EQUAL(JobOutcome::CANCELLED, result.outcome);
    TEST_ASSERT_EQUAL(JobEffect::DEVICE_STATE_CHANGED, result.effect);
    TEST_ASSERT_EQUAL_UINT32(callbacksAfterBreak, ctx.writes + ctx.reads);
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::MEASUREMENT_NOT_READY, st.code);
    TEST_ASSERT_FALSE(result.terminal);
    TEST_ASSERT_EQUAL_UINT32(0u, result.requestId);
    TEST_ASSERT_EQUAL_UINT32(callbacksAfterBreak, ctx.writes + ctx.reads);
  }

  {
    PreciseTimingTransport ctx;
    ctx.nowMs = 50;
    ctx.nowUs = 50000;
    SHT3xDevice device;
    Status st = device.bind(makePreciseTimingConfig(ctx));
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
    JobRequest request;
    request.requestId = 76;
    request.hasDeadline = true;
    request.deadlineMs = 51;
    st = device.requestEnsureIdle(request);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    PollJobResult result;
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes + ctx.reads);
    advancePreciseTimeMs(ctx, 1);
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::TIMEOUT, st.code);
    TEST_ASSERT_TRUE(result.terminal);
    TEST_ASSERT_EQUAL_UINT32(76u, result.requestId);
    TEST_ASSERT_EQUAL(JobType::ENSURE_IDLE, result.type);
    TEST_ASSERT_EQUAL(JobPhase::ENSURE_BREAK_WAIT, result.phase);
    TEST_ASSERT_EQUAL(JobOutcome::TIMED_OUT, result.outcome);
    TEST_ASSERT_EQUAL(JobEffect::DEVICE_STATE_CHANGED, result.effect);
    TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
    TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes + ctx.reads);
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::MEASUREMENT_NOT_READY, st.code);
    TEST_ASSERT_FALSE(result.terminal);
    TEST_ASSERT_EQUAL_UINT32(0u, result.requestId);
    TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes + ctx.reads);
  }

  {
    PreciseTimingTransport ctx;
    ctx.failCommand = cmd::CMD_BREAK;
    ctx.failCommandStatus = Status::Error(Err::I2C_TIMEOUT, "break timeout", 1);
    SHT3xDevice device;
    Status st = device.bind(makePreciseTimingConfig(ctx));
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
    JobRequest request;
    request.requestId = 71;
    st = device.requestEnsureIdle(request);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    PollJobResult result;
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);
    TEST_ASSERT_TRUE(result.terminal);
    TEST_ASSERT_EQUAL_UINT32(71u, result.requestId);
    TEST_ASSERT_EQUAL(JobPhase::ENSURE_BREAK_COMMAND, result.phase);
    TEST_ASSERT_EQUAL(JobOutcome::TIMED_OUT, result.outcome);
    TEST_ASSERT_EQUAL(JobEffect::DEVICE_STATE_INDETERMINATE, result.effect);
    TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
    TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes + ctx.reads);

    ctx.failCommand = 0;
    request.requestId = 77;
    st = device.requestEnsureIdle(request);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    TEST_ASSERT_EQUAL(JobPhase::ENSURE_BREAK_COMMAND, result.phase);
    TEST_ASSERT_EQUAL_UINT8(0u, result.instructionsUsed);
    TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes + ctx.reads);
    advancePreciseTimeMs(ctx, 1);
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
    TEST_ASSERT_EQUAL_UINT32(2u, ctx.writes + ctx.reads);
  }

  {
    PreciseTimingTransport ctx;
    ctx.nowMs = 100;
    ctx.nowUs = 100000;
    SHT3xDevice device;
    Status st = device.bind(makePreciseTimingConfig(ctx));
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
    JobRequest request;
    request.requestId = 72;
    st = device.requestEnsureIdle(request);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    PollJobResult result;
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    advancePreciseTimeMs(ctx, 1);
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    ctx.failCommand = cmd::CMD_SOFT_RESET;
    ctx.failCommandStatus = Status::Error(Err::I2C_NACK_DATA, "reset rejected", 2);
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::I2C_NACK_DATA, st.code);
    TEST_ASSERT_TRUE(result.terminal);
    TEST_ASSERT_EQUAL_UINT32(72u, result.requestId);
    TEST_ASSERT_EQUAL(JobPhase::ENSURE_RESET_COMMAND, result.phase);
    TEST_ASSERT_EQUAL(JobOutcome::FAILED, result.outcome);
    TEST_ASSERT_EQUAL(JobEffect::DEVICE_STATE_CHANGED, result.effect);
    TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
    TEST_ASSERT_EQUAL_UINT32(2u, ctx.writes + ctx.reads);
  }

  {
    PreciseTimingTransport ctx;
    ctx.nowMs = 200;
    ctx.nowUs = 200000;
    SHT3xDevice device;
    Status st = device.bind(makePreciseTimingConfig(ctx));
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
    JobRequest request;
    request.requestId = 73;
    st = device.requestEnsureIdle(request);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    PollJobResult result;
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    advancePreciseTimeMs(ctx, 1);
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    advancePreciseTimeMs(ctx, 2);
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    ctx.failCommand = cmd::CMD_READ_STATUS;
    ctx.failCommandStatus = Status::Error(Err::I2C_BUS, "status command bus error", 3);
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::I2C_BUS, st.code);
    TEST_ASSERT_TRUE(result.terminal);
    TEST_ASSERT_EQUAL_UINT32(73u, result.requestId);
    TEST_ASSERT_EQUAL(JobPhase::ENSURE_STATUS_COMMAND, result.phase);
    TEST_ASSERT_EQUAL(JobOutcome::FAILED, result.outcome);
    TEST_ASSERT_EQUAL(JobEffect::DEVICE_STATE_INDETERMINATE, result.effect);
    TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
    TEST_ASSERT_EQUAL_UINT32(3u, ctx.writes + ctx.reads);
  }

  {
    PreciseTimingTransport ctx;
    ctx.nowMs = 300;
    ctx.nowUs = 300000;
    SHT3xDevice device;
    Status st = device.bind(makePreciseTimingConfig(ctx));
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
    JobRequest request;
    request.requestId = 74;
    st = device.requestEnsureIdle(request);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    PollJobResult result;
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    advancePreciseTimeMs(ctx, 1);
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    advancePreciseTimeMs(ctx, 2);
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    advancePreciseTimeMs(ctx, 1);
    ctx.readStatus = Status::Error(Err::I2C_TIMEOUT, "status read timeout", 4);
    st = device.pollJob(ctx.nowMs, 1, result);
    TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);
    TEST_ASSERT_TRUE(result.terminal);
    TEST_ASSERT_EQUAL_UINT32(74u, result.requestId);
    TEST_ASSERT_EQUAL(JobPhase::ENSURE_STATUS_READ, result.phase);
    TEST_ASSERT_EQUAL(JobOutcome::TIMED_OUT, result.outcome);
    TEST_ASSERT_EQUAL(JobEffect::DEVICE_STATE_CHANGED, result.effect);
    TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
    TEST_ASSERT_EQUAL_UINT32(4u, ctx.writes + ctx.reads);
  }
}

void test_ensure_idle_request_cancel_preserves_unread_sample() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 400u;
  ctx.nowUs = 400000u;
  ctx.rawTemperature = 0x1234;
  ctx.rawHumidity = 0x5678;
  SHT3xDevice device;
  Status st = device.begin(makePreciseTimingConfig(ctx));
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  JobRequest measurement;
  measurement.requestId = 408;
  st = device.requestMeasurement(measurement);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  PollJobResult result;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  ctx.nowMs = device._measurementReadyMs;
  ctx.nowUs += device.estimateMeasurementTimeMs() * 1000u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(device.measurementReady());

  JobRequest ensure;
  ensure.requestId = 409;
  st = device.requestEnsureIdle(ensure);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_TRUE(device.measurementReady());
  const uint32_t callbacks = ctx.writes + ctx.reads;
  st = device.cancelJob(CancelReason::REQUESTED, result);
  TEST_ASSERT_EQUAL(Err::CANCELLED, st.code);
  TEST_ASSERT_EQUAL(JobEffect::NONE, result.effect);
  TEST_ASSERT_EQUAL_UINT32(callbacks, ctx.writes + ctx.reads);
  TEST_ASSERT_TRUE(device.measurementReady());
  TEST_ASSERT_TRUE(device.hardwareStateValid());

  Measurement value{-999.0f, -999.0f};
  st = device.getMeasurement(value);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_NOT_EQUAL(-999.0f, value.temperatureC);
  TEST_ASSERT_NOT_EQUAL(-999.0f, value.humidityPct);
}

void test_ensure_idle_status_verification_rejects_crc_and_error_flags() {
  struct VerificationCase {
    uint16_t statusRaw;
    bool corruptCrc;
    Err expected;
  };
  static constexpr VerificationCase cases[] = {
      {0u, true, Err::CRC_MISMATCH},
      {cmd::STATUS_WRITE_CRC_ERROR, false, Err::WRITE_CRC_ERROR},
      {cmd::STATUS_COMMAND_ERROR, false, Err::COMMAND_FAILED},
  };

  uint32_t requestId = 410u;
  for (const auto& testCase : cases) {
    PreciseTimingTransport ctx;
    ctx.nowMs = 500u;
    ctx.nowUs = 500000u;
    ctx.statusRaw = testCase.statusRaw;
    ctx.corruptStatusCrc = testCase.corruptCrc;
    SHT3xDevice device;
    Status st = device.bind(makePreciseTimingConfig(ctx));
    TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
    JobRequest request;
    request.requestId = requestId++;
    st = device.requestEnsureIdle(request);
    TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
    PollJobResult result;
    st = runEnsureIdleToStatusResult(device, ctx, result);
    TEST_ASSERT_EQUAL(testCase.expected, st.code);
    TEST_ASSERT_TRUE(result.terminal);
    TEST_ASSERT_EQUAL(JobPhase::ENSURE_STATUS_READ, result.phase);
    TEST_ASSERT_EQUAL(JobOutcome::FAILED, result.outcome);
    TEST_ASSERT_EQUAL(JobEffect::DEVICE_STATE_CHANGED, result.effect);
    TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
    TEST_ASSERT_EQUAL_UINT32(1u, device.protocolFailures());
    TEST_ASSERT_FALSE(device.hardwareStateValid());
  }
}

void test_latch_health_counts_status_command_read_as_one_logical_failure() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 500;
  ctx.nowUs = 500000;
  ctx.readStatus = Status::Error(Err::I2C_TIMEOUT, "status timeout", 9);
  Config cfg = makePreciseTimingConfig(ctx);
  cfg.offlineThreshold = 3;
  cfg.healthPolicy = HealthPolicy::LATCH_OFFLINE;
  SHT3xDevice device;
  Status st = device.bind(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  StatusRegister reg;
  for (uint32_t attempt = 1; attempt <= 3; ++attempt) {
    st = device.readStatus(reg);
    TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);
    TEST_ASSERT_EQUAL_UINT32(attempt, device.totalFailures());
    TEST_ASSERT_EQUAL_UINT8(attempt, device.consecutiveFailures());
    TEST_ASSERT_EQUAL_UINT32(attempt, device.transportSuccess());
    TEST_ASSERT_EQUAL_UINT32(attempt, device.transportFailures());
    TEST_ASSERT_EQUAL_UINT32(0u, device.totalSuccess());
    TEST_ASSERT_EQUAL(attempt == 3 ? DriverState::OFFLINE : DriverState::DEGRADED,
                      device.state());
  }

  const uint32_t callbacksAtLatch = ctx.writes + ctx.reads;
  st = device.readStatus(reg);
  TEST_ASSERT_EQUAL(Err::BUSY, st.code);
  TEST_ASSERT_EQUAL_UINT32(callbacksAtLatch, ctx.writes + ctx.reads);
  TEST_ASSERT_EQUAL_UINT32(3u, device.totalFailures());
  TEST_ASSERT_EQUAL_UINT8(3u, device.consecutiveFailures());
  TEST_ASSERT_EQUAL(DriverState::OFFLINE, device.state());
}

void test_observe_only_health_continues_i2c_and_counters_saturate() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 600;
  ctx.nowUs = 600000;
  ctx.readStatus = Status::Error(Err::I2C_TIMEOUT, "status timeout", 10);
  Config cfg = makePreciseTimingConfig(ctx);
  cfg.offlineThreshold = 1;
  cfg.healthPolicy = HealthPolicy::OBSERVE_ONLY;
  SHT3xDevice device;
  Status st = device.bind(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  StatusRegister reg;
  st = device.readStatus(reg);
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);
  TEST_ASSERT_EQUAL(DriverState::OFFLINE, device.state());
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);

  device._transportFailures = UINT32_MAX;
  st = device.readStatus(reg);
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);
  TEST_ASSERT_EQUAL(DriverState::OFFLINE, device.state());
  TEST_ASSERT_EQUAL_UINT32(2u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(2u, ctx.reads);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, device.transportFailures());
  TEST_ASSERT_EQUAL_UINT32(2u, device.totalFailures());

  ctx.readStatus = Status::Ok();
  st = device.readStatus(reg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_EQUAL_UINT32(3u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(3u, ctx.reads);
  TEST_ASSERT_EQUAL(DriverState::READY, device.state());
  TEST_ASSERT_EQUAL_UINT8(0u, device.consecutiveFailures());
  TEST_ASSERT_EQUAL_UINT32(1u, device.totalSuccess());
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, device.transportFailures());
}

void test_observe_only_measurement_hot_return_succeeds_without_rebind() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 700u;
  ctx.nowUs = 700000u;
  ctx.readStatus = Status::Error(Err::I2C_NACK_ADDR, "sensor removed", -50);
  Config cfg = makePreciseTimingConfig(ctx);
  cfg.healthPolicy = HealthPolicy::OBSERVE_ONLY;
  cfg.offlineThreshold = 1u;
  SHT3xDevice device;
  Status st = device.bind(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  JobRequest removed;
  removed.requestId = 413;
  st = device.requestMeasurement(removed);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  PollJobResult result;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  ctx.nowMs = device._measurementReadyMs;
  ctx.nowUs += device.estimateMeasurementTimeMs() * 1000u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::I2C_NACK_ADDR, st.code);
  TEST_ASSERT_TRUE(result.terminal);
  TEST_ASSERT_EQUAL_UINT32(413u, result.requestId);
  TEST_ASSERT_EQUAL(DriverState::OFFLINE, device.state());
  TEST_ASSERT_FALSE(device.hasSample());

  ctx.readStatus = Status::Ok();
  ctx.rawTemperature = 0xAAAA;
  ctx.rawHumidity = 0xBBBB;
  JobRequest returned;
  returned.requestId = 414;
  st = device.requestMeasurement(returned);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  ctx.nowMs = device._measurementReadyMs;
  ctx.nowUs += device.estimateMeasurementTimeMs() * 1000u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(result.terminal);
  TEST_ASSERT_EQUAL_UINT32(414u, result.requestId);
  TEST_ASSERT_EQUAL(JobOutcome::SUCCEEDED, result.outcome);
  TEST_ASSERT_EQUAL(DriverState::READY, device.state());
  TEST_ASSERT_TRUE(device.hasSample());
  TEST_ASSERT_EQUAL_UINT32(2u, ctx.writes);
  TEST_ASSERT_EQUAL_UINT32(2u, ctx.reads);
  TEST_ASSERT_EQUAL_HEX8(cmd::I2C_ADDR_LOW, ctx.lastAddress);
  RawSample sample;
  st = device.getRawSample(sample);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_EQUAL_HEX16(0xAAAA, sample.rawTemperature);
  TEST_ASSERT_EQUAL_HEX16(0xBBBB, sample.rawHumidity);
}

void test_last_error_message_is_stable_after_transient_callback_buffer_changes() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 700;
  ctx.nowUs = 700000;
  char transientMessage[] = "temporary callback text";
  ctx.readStatus = Status::Error(Err::I2C_TIMEOUT, transientMessage, -17);
  SHT3xDevice device;
  Status st = device.bind(makePreciseTimingConfig(ctx));
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  StatusRegister reg;
  st = device.readStatus(reg);
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, st.code);
  transientMessage[0] = 'X';

  const Status cached = device.lastError();
  TEST_ASSERT_EQUAL(Err::I2C_TIMEOUT, cached.code);
  TEST_ASSERT_EQUAL_INT32(-17, cached.detail);
  TEST_ASSERT_EQUAL_STRING("I2C timeout", cached.msg);
}

void test_humidity_crc_failure_does_not_publish_new_frame() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 800;
  ctx.nowUs = 800000;
  ctx.rawTemperature = 0x1111;
  ctx.rawHumidity = 0x2222;
  SHT3xDevice device;
  Status st = device.begin(makePreciseTimingConfig(ctx));
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_TRUE(device.hardwareStateValid());

  JobRequest good;
  good.requestId = 81;
  st = device.requestMeasurement(good);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  PollJobResult result;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  ctx.nowMs = device._measurementReadyMs;
  ctx.nowUs = ctx.nowMs * 1000u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  RawSample oldSample;
  st = device.getRawSample(oldSample);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_EQUAL_HEX16(0x1111, oldSample.rawTemperature);
  TEST_ASSERT_EQUAL_HEX16(0x2222, oldSample.rawHumidity);

  ctx.rawTemperature = 0xAAAA;
  ctx.rawHumidity = 0xBBBB;
  ctx.corruptHumidityCrc = true;
  JobRequest bad;
  bad.requestId = 82;
  st = device.requestMeasurement(bad);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  ctx.nowMs = device._measurementReadyMs;
  ctx.nowUs = ctx.nowMs * 1000u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::CRC_MISMATCH, st.code);
  TEST_ASSERT_EQUAL_STRING("CRC mismatch (humidity)", st.msg);
  TEST_ASSERT_TRUE(result.terminal);
  TEST_ASSERT_FALSE(result.completed);
  TEST_ASSERT_EQUAL_UINT32(82u, result.requestId);
  TEST_ASSERT_EQUAL(JobPhase::SINGLE_SHOT_READ, result.phase);
  TEST_ASSERT_EQUAL(JobOutcome::FAILED, result.outcome);
  TEST_ASSERT_EQUAL(JobEffect::NONE, result.effect);
  TEST_ASSERT_EQUAL_UINT32(1u, device.protocolFailures());
  TEST_ASSERT_TRUE(device.hasSample());
  TEST_ASSERT_FALSE(device.measurementReady());
  TEST_ASSERT_TRUE(device.hardwareStateValid());

  RawSample afterFailure;
  st = device.getRawSample(afterFailure);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_EQUAL_HEX16(oldSample.rawTemperature, afterFailure.rawTemperature);
  TEST_ASSERT_EQUAL_HEX16(oldSample.rawHumidity, afterFailure.rawHumidity);

  device._protocolFailures = UINT32_MAX;
  bad.requestId = 83;
  st = device.requestMeasurement(bad);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  ctx.nowMs = device._measurementReadyMs;
  ctx.nowUs = ctx.nowMs * 1000u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::CRC_MISMATCH, st.code);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, device.protocolFailures());
}

void test_periodic_not_ready_counter_is_separate_and_saturating() {
  PreciseTimingTransport ctx;
  ctx.nowMs = 900;
  ctx.nowUs = 900000;
  Config cfg = makePreciseTimingConfig(ctx);
  cfg.transportCapabilities = TransportCapability::READ_HEADER_NACK;
  SHT3xDevice device;
  Status st = device.bind(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.startPeriodic(PeriodicRate::MPS_1,
                            Repeatability::HIGH_REPEATABILITY);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  ctx.readStatus = Status::Error(Err::I2C_NACK_READ, "no frame", 23);
  JobRequest request;
  request.requestId = 91;
  st = device.requestMeasurement(request);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  PollJobResult result;
  ctx.nowMs = device._measurementReadyMs;
  ctx.nowUs = ctx.nowMs * 1000u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL(JobPhase::PERIODIC_FETCH_COMMAND, result.phase);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  advancePreciseTimeMs(ctx, 1);
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_TRUE(result.active);
  TEST_ASSERT_FALSE(result.terminal);
  TEST_ASSERT_EQUAL(JobPhase::PERIODIC_READ, result.phase);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  TEST_ASSERT_EQUAL_UINT32(1u, device.totalNotReady());
  TEST_ASSERT_EQUAL_UINT32(1u, device.notReadyCount());
  TEST_ASSERT_EQUAL_UINT32(0u, device.transportFailures());
  TEST_ASSERT_EQUAL_UINT32(0u, device.protocolFailures());

  device._totalNotReady = UINT32_MAX;
  ctx.nowMs = device._measurementReadyMs;
  ctx.nowUs = ctx.nowMs * 1000u;
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL_UINT8(1u, result.instructionsUsed);
  advancePreciseTimeMs(ctx, 1);
  st = device.pollJob(ctx.nowMs, 1, result);
  TEST_ASSERT_EQUAL(Err::IN_PROGRESS, st.code);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, device.totalNotReady());
  TEST_ASSERT_EQUAL_UINT32(2u, device.notReadyCount());
  TEST_ASSERT_EQUAL_UINT32(0u, device.transportFailures());
  TEST_ASSERT_EQUAL_UINT32(0u, device.protocolFailures());
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
  TEST_ASSERT_EQUAL_UINT32(1u, device.protocolFailures());
  CachedSettings cached = device.getCachedSettings();
  TEST_ASSERT_FALSE(cached.alertValid[static_cast<uint8_t>(AlertLimitKind::HIGH_SET)]);
  TEST_ASSERT_EQUAL(2u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_ALERT_WRITE_HIGH_SET, ctx.commands[0]);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_READ_STATUS, ctx.commands[1]);
  TEST_ASSERT_EQUAL_UINT32(1u, ctx.reads);

  ctx.statusRaw = cmd::STATUS_COMMAND_ERROR;
  clearFrameLog(ctx);
  st = device.writeAlertLimitRaw(AlertLimitKind::HIGH_SET, 0x2222);
  TEST_ASSERT_EQUAL(Err::COMMAND_FAILED, st.code);
  TEST_ASSERT_EQUAL_UINT32(2u, device.protocolFailures());

  device._protocolFailures = UINT32_MAX;
  ctx.statusRaw = cmd::STATUS_WRITE_CRC_ERROR;
  clearFrameLog(ctx);
  st = device.writeAlertLimitRaw(AlertLimitKind::HIGH_SET, 0x2222);
  TEST_ASSERT_EQUAL(Err::WRITE_CRC_ERROR, st.code);
  TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, device.protocolFailures());
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

void test_recover_periodic_requires_break_before_success() {
  FrameScriptTransport ctx;
  SHT3xDevice device;
  Config cfg = makeFrameConfig(ctx);
  cfg.recoverUseBusReset = false;
  cfg.recoverUseSoftReset = true;
  cfg.recoverUseHardReset = false;
  cfg.allowGeneralCallReset = false;
  Status st = device.begin(cfg);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  st = device.startPeriodic(PeriodicRate::MPS_1, Repeatability::HIGH_REPEATABILITY);
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);

  clearFrameLog(ctx);

  st = device.recover();
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_FALSE(device.isPeriodicActive());
  TEST_ASSERT_EQUAL(Mode::SINGLE_SHOT, device._mode);
  TEST_ASSERT_EQUAL(4u, ctx.commandCount);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_READ_STATUS, ctx.commands[0]);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_BREAK, ctx.commands[1]);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_SOFT_RESET, ctx.commands[2]);
  TEST_ASSERT_EQUAL_UINT16(cmd::CMD_READ_STATUS, ctx.commands[3]);
  TEST_ASSERT_EQUAL_UINT32(2u, ctx.reads);
}

void test_stop_periodic_waits_after_break() {
  FakeTransport bus;
  SHT3xDevice device;
  Config cfg = makeConfig(bus);
  device._config = cfg;
  device._initialized = true;
  device._driverState = DriverState::READY;
  device._periodicActive = true;
  device._mode = Mode::PERIODIC;
  device._config.mode = Mode::PERIODIC;
  device._periodMs = 100;
  device._lastCommandValid = false;
  bus.nowMs = 10;

  Status st = device.stopPeriodic();
  TEST_ASSERT_TRUE_MESSAGE(st.ok(), st.msg);
  TEST_ASSERT_FALSE(device.isPeriodicActive());
  TEST_ASSERT_TRUE(bus.nowMs >= 11u);
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
  device._periodicStartMs = 0xFFFFFFF0u;
  device._lastFetchMs = 0;
  device._lastFetchValid = true;

  TEST_ASSERT_EQUAL_UINT32(105u, device._periodicReadyMs(1u));

  device._periodicStartMs = 0;
  device._lastFetchValid = false;

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
  bus.nowMs = device._measurementReadyMs;
  device.tick(bus.nowMs);
  bus.nowMs = device._measurementReadyMs;
  device.tick(bus.nowMs);
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
  device._lastFetchValid = true;
  device._periodMs = 100;
  device._sampleTimestampMs = 111;
  device._missedSamples = 12;
  device._notReadyStartMs = 222;
  device._notReadyStartValid = true;
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
  TEST_ASSERT_FALSE(device._lastFetchValid);
  TEST_ASSERT_EQUAL_UINT32(0u, device._periodMs);
  TEST_ASSERT_EQUAL_UINT32(0u, device._sampleTimestampMs);
  TEST_ASSERT_EQUAL_UINT32(0u, device._missedSamples);
  TEST_ASSERT_EQUAL_UINT32(0u, device._notReadyStartMs);
  TEST_ASSERT_FALSE(device._notReadyStartValid);
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
  RUN_TEST(test_long_idle_duration_gates_do_not_fail_after_half_range);
  RUN_TEST(test_begin_respects_configured_delay_after_soft_reset);
  RUN_TEST(test_begin_rejects_status_diagnostic_flags_without_startup_health);
  RUN_TEST(test_probe_status_crc_is_counter_neutral);
  RUN_TEST(test_interface_reset_failure_invalidates_state_and_starts_tidle);
  RUN_TEST(test_get_settings_snapshot_fields);
  RUN_TEST(test_begin_invalid_config_preserves_existing_binding_and_no_startup_health);
  RUN_TEST(test_begin_i2c_failure_does_not_update_health);
  RUN_TEST(test_begin_rejects_missing_timing_hooks_before_i2c);
  RUN_TEST(test_crc8_example);
  RUN_TEST(test_conversions_basic);
  RUN_TEST(test_alert_limit_roundtrip);
  RUN_TEST(test_alert_limit_app_note_encode_vectors);
  RUN_TEST(test_alert_limit_app_note_decode_vectors);
  RUN_TEST(test_alert_limit_clamps_out_of_range_inputs);
  RUN_TEST(test_alert_limit_quantized_roundtrip_edges);
  RUN_TEST(test_time_elapsed_wrap);
  RUN_TEST(test_command_delay_guard);
  RUN_TEST(test_command_delay_allows_long_idle_after_microsecond_half_range);
  RUN_TEST(test_begin_rejects_oversized_timing_config);
  RUN_TEST(test_single_shot_pending_blocks_unrelated_commands);
  RUN_TEST(test_low_level_command_helpers_write_and_read);
  RUN_TEST(test_low_level_command_helpers_map_expected_nack);
  RUN_TEST(test_low_level_command_helpers_block_pending_measurement);
  RUN_TEST(test_low_level_command_helpers_bypass_periodic_guard_as_expert_api);
  RUN_TEST(test_low_level_read_rejects_invalid_buffers_before_i2c);
  RUN_TEST(test_raw_transport_rejects_invalid_buffers);
  RUN_TEST(test_expected_nack_mapping);
  RUN_TEST(test_not_ready_timeout_escalation);
  RUN_TEST(test_nack_mapping_without_capability);
  RUN_TEST(test_periodic_fetch_expected_nack_no_failure);
  RUN_TEST(test_example_adapter_ambiguous_zero_bytes);
  RUN_TEST(test_wire_adapter_timeout_and_stop);
  RUN_TEST(test_wire_adapter_drains_partial_read);
  RUN_TEST(test_wire_adapter_rejects_invalid_buffers_and_timeout);
  RUN_TEST(test_i2c_scanner_restores_timeout);
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
  RUN_TEST(test_single_shot_poll_job_instruction_budget);
  RUN_TEST(test_single_shot_poll_job_crc_mismatch_visible_status);
  RUN_TEST(test_periodic_fetch_poll_job_instruction_budget);
  RUN_TEST(test_periodic_poll_job_tidle_uses_fractional_microsecond_clock);
  RUN_TEST(test_periodic_poll_job_tidle_is_wrap_safe_in_microseconds);
  RUN_TEST(test_single_shot_poll_timestamps_follow_transport_completion);
  RUN_TEST(test_single_shot_poll_ready_time_wraps_milliseconds);
  RUN_TEST(test_milli_conversions_cover_endpoints_midpoint_and_rounding);
  RUN_TEST(test_measurement_milli_and_float_use_unrounded_raw_sample);
  RUN_TEST(test_bind_rebind_and_end_are_zero_i2c_local_operations);
  RUN_TEST(test_repeated_begin_and_end_reinitialize_deterministically);
  RUN_TEST(test_owner_job_identity_zero_budget_and_exactly_once_terminal);
  RUN_TEST(test_invalid_cancel_reason_is_zero_i2c_and_leaves_job_active);
  RUN_TEST(test_single_shot_transfer_stage_failures_report_phase_and_effect);
  RUN_TEST(test_cancel_before_and_after_command_is_zero_i2c_and_preserves_sample);
  RUN_TEST(test_cancel_periodic_read_is_zero_i2c_and_preserves_cached_sample);
  RUN_TEST(test_job_deadlines_before_work_inside_callback_and_across_wrap);
  RUN_TEST(test_periodic_not_ready_callback_crossing_deadline_terminates_once);
  RUN_TEST(test_request_ensure_idle_is_staged_and_one_callback_bounded);
  RUN_TEST(test_ensure_idle_stage_failures_report_phase_and_effect);
  RUN_TEST(test_ensure_idle_request_cancel_preserves_unread_sample);
  RUN_TEST(test_ensure_idle_status_verification_rejects_crc_and_error_flags);
  RUN_TEST(test_latch_health_counts_status_command_read_as_one_logical_failure);
  RUN_TEST(test_observe_only_health_continues_i2c_and_counters_saturate);
  RUN_TEST(test_observe_only_measurement_hot_return_succeeds_without_rebind);
  RUN_TEST(test_last_error_message_is_stable_after_transient_callback_buffer_changes);
  RUN_TEST(test_humidity_crc_failure_does_not_publish_new_frame);
  RUN_TEST(test_periodic_not_ready_counter_is_separate_and_saturating);
  RUN_TEST(test_public_status_read_and_clear_are_explicit_operations);
  RUN_TEST(test_periodic_start_command_failure_does_not_update_public_mode_or_cache);
  RUN_TEST(test_periodic_restart_break_failure_keeps_previous_public_state);
  RUN_TEST(test_periodic_reconfiguration_start_failure_exposes_stopped_state_and_preserves_cache);
  RUN_TEST(test_alert_limit_write_command_failure_does_not_update_cache);
  RUN_TEST(test_alert_limit_write_status_verification_failure_does_not_update_cache);
  RUN_TEST(test_write_alert_limit_uses_app_note_encoder_vectors);
  RUN_TEST(test_disable_alerts_second_write_failure_exposes_partial_cache);
  RUN_TEST(test_public_recover_bus_reset_failure_preserves_precise_status);
  RUN_TEST(test_recover_periodic_requires_break_before_success);
  RUN_TEST(test_stop_periodic_waits_after_break);
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
