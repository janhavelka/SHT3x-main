/// @file CommandTable.h
/// @brief Command definitions and bit masks for SHT3x
#pragma once

#include <cstdint>
#include <cstddef>

namespace SHT3x {
namespace cmd {

// ============================================================================
// I2C Addresses (7-bit)
// ============================================================================

static constexpr uint8_t I2C_ADDR_LOW = 0x44;
static constexpr uint8_t I2C_ADDR_HIGH = 0x45;

// General call reset (bus-wide)
static constexpr uint8_t GENERAL_CALL_ADDR = 0x00;
static constexpr uint8_t GENERAL_CALL_RESET_BYTE = 0x06;

// ============================================================================
// CRC-8 parameters
// ============================================================================

static constexpr uint8_t CRC_INIT = 0xFF;
static constexpr uint8_t CRC_POLY = 0x31;

// ============================================================================
// Measurement Commands (16-bit)
// ============================================================================

// Single-shot measurement (clock stretching enabled)
static constexpr uint16_t CMD_SINGLE_SHOT_STRETCH_HIGH = 0x2C06;
static constexpr uint16_t CMD_SINGLE_SHOT_STRETCH_MED = 0x2C0D;
static constexpr uint16_t CMD_SINGLE_SHOT_STRETCH_LOW = 0x2C10;

// Single-shot measurement (clock stretching disabled)
static constexpr uint16_t CMD_SINGLE_SHOT_NO_STRETCH_HIGH = 0x2400;
static constexpr uint16_t CMD_SINGLE_SHOT_NO_STRETCH_MED = 0x240B;
static constexpr uint16_t CMD_SINGLE_SHOT_NO_STRETCH_LOW = 0x2416;

// Periodic measurement commands (repeatability + mps)
static constexpr uint16_t CMD_PERIODIC_0_5_HIGH = 0x2032;
static constexpr uint16_t CMD_PERIODIC_0_5_MED = 0x2024;
static constexpr uint16_t CMD_PERIODIC_0_5_LOW = 0x202F;

static constexpr uint16_t CMD_PERIODIC_1_HIGH = 0x2130;
static constexpr uint16_t CMD_PERIODIC_1_MED = 0x2126;
static constexpr uint16_t CMD_PERIODIC_1_LOW = 0x212D;

static constexpr uint16_t CMD_PERIODIC_2_HIGH = 0x2236;
static constexpr uint16_t CMD_PERIODIC_2_MED = 0x2220;
static constexpr uint16_t CMD_PERIODIC_2_LOW = 0x222B;

static constexpr uint16_t CMD_PERIODIC_4_HIGH = 0x2334;
static constexpr uint16_t CMD_PERIODIC_4_MED = 0x2322;
static constexpr uint16_t CMD_PERIODIC_4_LOW = 0x2329;

static constexpr uint16_t CMD_PERIODIC_10_HIGH = 0x2737;
static constexpr uint16_t CMD_PERIODIC_10_MED = 0x2721;
static constexpr uint16_t CMD_PERIODIC_10_LOW = 0x272A;

// Fetch data (periodic readout)
static constexpr uint16_t CMD_FETCH_DATA = 0xE000;

// Accelerated response time (ART) mode
static constexpr uint16_t CMD_ART = 0x2B32;

// Stop periodic mode
static constexpr uint16_t CMD_BREAK = 0x3093;

// ============================================================================
// Status, reset, heater
// ============================================================================

static constexpr uint16_t CMD_READ_STATUS = 0xF32D;
static constexpr uint16_t CMD_CLEAR_STATUS = 0x3041;
static constexpr uint16_t CMD_SOFT_RESET = 0x30A2;

static constexpr uint16_t CMD_HEATER_ENABLE = 0x306D;
static constexpr uint16_t CMD_HEATER_DISABLE = 0x3066;

// ============================================================================
// Serial number
// ============================================================================

static constexpr uint16_t CMD_SERIAL_STRETCH = 0x3780;
static constexpr uint16_t CMD_SERIAL_NO_STRETCH = 0x3682;

// ============================================================================
// Alert limits (read/write)
// ============================================================================

static constexpr uint16_t CMD_ALERT_READ_HIGH_SET = 0xE11F;
static constexpr uint16_t CMD_ALERT_READ_HIGH_CLEAR = 0xE114;
static constexpr uint16_t CMD_ALERT_READ_LOW_CLEAR = 0xE109;
static constexpr uint16_t CMD_ALERT_READ_LOW_SET = 0xE102;

static constexpr uint16_t CMD_ALERT_WRITE_HIGH_SET = 0x611D;
static constexpr uint16_t CMD_ALERT_WRITE_HIGH_CLEAR = 0x6116;
static constexpr uint16_t CMD_ALERT_WRITE_LOW_CLEAR = 0x610B;
static constexpr uint16_t CMD_ALERT_WRITE_LOW_SET = 0x6100;

// ============================================================================
// Status register bit masks (16-bit)
// ============================================================================

static constexpr uint16_t STATUS_ALERT_PENDING = 0x8000;
static constexpr uint16_t STATUS_HEATER_ON = 0x2000;
static constexpr uint16_t STATUS_RH_ALERT = 0x0800;
static constexpr uint16_t STATUS_T_ALERT = 0x0400;
static constexpr uint16_t STATUS_RESET_DETECTED = 0x0010;
static constexpr uint16_t STATUS_COMMAND_ERROR = 0x0002;
static constexpr uint16_t STATUS_WRITE_CRC_ERROR = 0x0001;

// ============================================================================
// Data lengths
// ============================================================================

static constexpr size_t DATA_WORD_BYTES = 2;
static constexpr size_t DATA_CRC_BYTES = 1;
static constexpr size_t DATA_WORD_WITH_CRC = 3;

static constexpr size_t MEASUREMENT_DATA_LEN = 6; // T (2+1) + RH (2+1)
static constexpr size_t STATUS_DATA_LEN = 3;      // Status (2+1)
static constexpr size_t SERIAL_DATA_LEN = 6;      // SN (2+1 + 2+1)
static constexpr size_t ALERT_DATA_LEN = 3;       // Limit (2+1)

} // namespace cmd
} // namespace SHT3x
