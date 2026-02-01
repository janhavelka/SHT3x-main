/// @file test_basic.cpp
/// @brief Basic unit tests for SHT3x driver

#include <cstdio>
#include <cassert>

// Include stubs first
#include "Arduino.h"
#include "Wire.h"

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

// ============================================================================
// Test Helpers
// ============================================================================

static int testsPassed = 0;
static int testsFailed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
  printf("Running %s... ", #name); \
  test_##name(); \
  printf("PASSED\n"); \
  testsPassed++; \
} catch (...) { \
  printf("FAILED\n"); \
  testsFailed++; \
}

#define ASSERT_TRUE(x) assert(x)
#define ASSERT_FALSE(x) assert(!(x))
#define ASSERT_EQ(a, b) assert((a) == (b))
#define ASSERT_NE(a, b) assert((a) != (b))
#define ASSERT_NEAR(a, b, eps) assert(((a) > (b) ? (a) - (b) : (b) - (a)) <= (eps))

// ============================================================================
// Tests
// ============================================================================

TEST(status_ok) {
  Status st = Status::Ok();
  ASSERT_TRUE(st.ok());
  ASSERT_EQ(st.code, Err::OK);
}

TEST(status_error) {
  Status st = Status::Error(Err::I2C_ERROR, "Test error", 42);
  ASSERT_FALSE(st.ok());
  ASSERT_EQ(st.code, Err::I2C_ERROR);
  ASSERT_EQ(st.detail, 42);
}

TEST(status_in_progress) {
  Status st = Status{Err::IN_PROGRESS, 0, "In progress"};
  ASSERT_FALSE(st.ok());
  ASSERT_EQ(st.code, Err::IN_PROGRESS);
}

TEST(config_defaults) {
  Config cfg;
  ASSERT_EQ(cfg.i2cWrite, nullptr);
  ASSERT_EQ(cfg.i2cWriteRead, nullptr);
  ASSERT_EQ(cfg.busReset, nullptr);
  ASSERT_EQ(cfg.hardReset, nullptr);
  ASSERT_EQ(cfg.i2cAddress, 0x44);
  ASSERT_EQ(cfg.i2cTimeoutMs, 50u);
  ASSERT_EQ(cfg.offlineThreshold, 5);
  ASSERT_EQ(cfg.commandDelayMs, 1u);
  ASSERT_EQ(cfg.notReadyTimeoutMs, 0u);
  ASSERT_EQ(cfg.recoverBackoffMs, 100u);
  ASSERT_EQ(cfg.allowGeneralCallReset, false);
  ASSERT_EQ(cfg.recoverUseBusReset, true);
  ASSERT_EQ(cfg.recoverUseSoftReset, true);
  ASSERT_EQ(cfg.recoverUseHardReset, true);
  ASSERT_EQ(cfg.lowVdd, false);
  ASSERT_EQ(static_cast<uint8_t>(cfg.repeatability), static_cast<uint8_t>(Repeatability::HIGH_REPEATABILITY));
  ASSERT_EQ(static_cast<uint8_t>(cfg.clockStretching), static_cast<uint8_t>(ClockStretching::STRETCH_DISABLED));
  ASSERT_EQ(static_cast<uint8_t>(cfg.periodicRate), static_cast<uint8_t>(PeriodicRate::MPS_1));
  ASSERT_EQ(static_cast<uint8_t>(cfg.mode), static_cast<uint8_t>(Mode::SINGLE_SHOT));
}

TEST(crc8_example) {
  const uint8_t data[2] = {0xBE, 0xEF};
  const uint8_t crc = SHT3x::_crc8(data, 2);
  ASSERT_EQ(crc, 0x92);
}

TEST(conversions_basic) {
  ASSERT_NEAR(SHT3x::convertTemperatureC(0), -45.0f, 0.01f);
  ASSERT_NEAR(SHT3x::convertTemperatureC(65535), 130.0f, 0.02f);
  ASSERT_NEAR(SHT3x::convertHumidityPct(0), 0.0f, 0.01f);
  ASSERT_NEAR(SHT3x::convertHumidityPct(65535), 100.0f, 0.02f);

  ASSERT_EQ(SHT3x::convertTemperatureC_x100(0), -4500);
  ASSERT_EQ(SHT3x::convertTemperatureC_x100(65535), 13000);
  ASSERT_EQ(SHT3x::convertHumidityPct_x100(0), 0u);
  ASSERT_EQ(SHT3x::convertHumidityPct_x100(65535), 10000u);
}

TEST(alert_limit_roundtrip) {
  const float tIn = 25.3f;
  const float rhIn = 47.8f;
  const uint16_t packed = SHT3x::encodeAlertLimit(tIn, rhIn);
  float tOut = 0.0f;
  float rhOut = 0.0f;
  SHT3x::decodeAlertLimit(packed, tOut, rhOut);
  ASSERT_NEAR(tOut, tIn, 0.6f);
  ASSERT_NEAR(rhOut, rhIn, 1.5f);
}

TEST(time_elapsed_wrap) {
  ASSERT_FALSE(SHT3x::_timeElapsed(5, 10));
  ASSERT_TRUE(SHT3x::_timeElapsed(10, 10));
  ASSERT_TRUE(SHT3x::_timeElapsed(10, 5));

  const uint32_t nearMax = 0xFFFFFFF0U;
  ASSERT_TRUE(SHT3x::_timeElapsed(5, nearMax));
  ASSERT_FALSE(SHT3x::_timeElapsed(nearMax, 5));
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
    rxData[2] = SHT3x::_crc8(&rxData[0], 2);
    rxData[3] = 0x00;
    rxData[4] = 0x00;
    rxData[5] = SHT3x::_crc8(&rxData[3], 2);
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
    rxData[2] = SHT3x::_crc8(&rxData[0], 2);
  }
  return st;
}

TEST(expected_nack_mapping) {
  FakeTransport ctx;
  ctx.writeReadStatus = Status::Error(Err::I2C_NACK_READ, "NACK read", 0);

  SHT3x device;
  device._config.i2cWrite = fakeWrite;
  device._config.i2cWriteRead = fakeWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  device._config.commandDelayMs = 1;
  device._initialized = true;
  device._driverState = DriverState::READY;
  device._consecutiveFailures = 0;
  device._lastBusActivityMs = 0;

  gMillis = 123;
  gMillisStep = 0;
  uint8_t buf[6] = {};
  Status st = device._i2cWriteReadTrackedAllowNoData(nullptr, 0, buf, sizeof(buf), true);
  ASSERT_EQ(st.code, Err::MEASUREMENT_NOT_READY);
  ASSERT_EQ(device._consecutiveFailures, 0);
  ASSERT_EQ(device._lastBusActivityMs, 123u);
}

TEST(not_ready_timeout_escalation) {
  FakeTransport ctx;
  ctx.writeStatus = Status::Ok();
  ctx.writeReadStatus = Status::Error(Err::I2C_NACK_READ, "NACK read", 0);

  SHT3x device;
  device._config.i2cWrite = fakeWrite;
  device._config.i2cWriteRead = fakeWriteRead;
  device._config.i2cUser = &ctx;
  device._config.i2cTimeoutMs = 10;
  device._config.commandDelayMs = 1;
  device._config.notReadyTimeoutMs = 5;
  device._initialized = true;
  device._driverState = DriverState::READY;
  device._periodicActive = true;
  device._periodMs = 100;
  device._notReadyStartMs = 1;

  gMillis = 10;
  gMillisStep = 0;
  Status st = device._fetchPeriodic();
  ASSERT_NE(st.code, Err::MEASUREMENT_NOT_READY);
  ASSERT_TRUE(device._consecutiveFailures > 0);
}

TEST(recover_transient_failure) {
  ScriptedTransport ctx;
  ctx.readScript[0] = Status::Error(Err::I2C_TIMEOUT, "timeout");
  ctx.readScript[1] = Status::Ok();
  ctx.readCount = 2;

  auto busReset = [](void* user) -> Status {
    (void)user;
    return Status::Ok();
  };

  SHT3x device;
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
  gMillisStep = 0;
  Status st = device.recover();
  ASSERT_TRUE(st.ok());
}

TEST(recover_permanent_offline) {
  ScriptedTransport ctx;
  ctx.readScript[0] = Status::Error(Err::I2C_TIMEOUT, "timeout");
  ctx.readScript[1] = Status::Error(Err::I2C_TIMEOUT, "timeout");
  ctx.readScript[2] = Status::Error(Err::I2C_TIMEOUT, "timeout");
  ctx.readCount = 3;

  SHT3x device;
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
  gMillisStep = 0;
  Status st = device.recover();
  ASSERT_FALSE(st.ok());
  ASSERT_TRUE(device._consecutiveFailures > 0);
}

// ============================================================================
// Main
// ============================================================================

int main() {
  printf("\n=== SHT3x Unit Tests ===\n\n");

  RUN_TEST(status_ok);
  RUN_TEST(status_error);
  RUN_TEST(status_in_progress);
  RUN_TEST(config_defaults);

  printf("\n=== Results: %d passed, %d failed ===\n\n", testsPassed, testsFailed);

  return testsFailed > 0 ? 1 : 0;
}
