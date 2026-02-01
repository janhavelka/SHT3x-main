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

// Include driver
#include "SHT3x/Status.h"
#include "SHT3x/Config.h"

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
  ASSERT_EQ(cfg.i2cAddress, 0x44);
  ASSERT_EQ(cfg.i2cTimeoutMs, 50u);
  ASSERT_EQ(cfg.offlineThreshold, 5);
  ASSERT_EQ(cfg.commandDelayMs, 1u);
  ASSERT_EQ(cfg.allowGeneralCallReset, false);
  ASSERT_EQ(cfg.lowVdd, false);
  ASSERT_EQ(static_cast<uint8_t>(cfg.repeatability), static_cast<uint8_t>(Repeatability::HIGH_REPEATABILITY));
  ASSERT_EQ(static_cast<uint8_t>(cfg.clockStretching), static_cast<uint8_t>(ClockStretching::STRETCH_DISABLED));
  ASSERT_EQ(static_cast<uint8_t>(cfg.periodicRate), static_cast<uint8_t>(PeriodicRate::MPS_1));
  ASSERT_EQ(static_cast<uint8_t>(cfg.mode), static_cast<uint8_t>(Mode::SINGLE_SHOT));
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
