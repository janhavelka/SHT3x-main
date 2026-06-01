/// @file CommandTable.h
/// @brief Command definitions and bit masks for SHT3x
#pragma once

#include <cstdint>
#include <cstddef>

namespace SHT3x {
namespace cmd {

/// @name I2C addresses and general-call bytes
/// @{

static constexpr uint8_t I2C_ADDR_LOW = 0x44;   ///< Default 7-bit address when ADDR is strapped low
static constexpr uint8_t I2C_ADDR_HIGH = 0x45;  ///< Alternate 7-bit address when ADDR is strapped high

static constexpr uint8_t GENERAL_CALL_ADDR = 0x00;       ///< I2C general-call address; bus-wide policy only
static constexpr uint8_t GENERAL_CALL_RESET_BYTE = 0x06; ///< General-call reset byte; affects every supporting device

/// @}
/// @name CRC-8 parameters
/// @{

static constexpr uint8_t CRC_INIT = 0xFF; ///< SHT3x CRC-8 initial value
static constexpr uint8_t CRC_POLY = 0x31; ///< SHT3x CRC-8 polynomial

/// @}
/// @name Measurement commands
/// @{

/// Single-shot measurement commands with clock stretching enabled.
static constexpr uint16_t CMD_SINGLE_SHOT_STRETCH_HIGH = 0x2C06;
static constexpr uint16_t CMD_SINGLE_SHOT_STRETCH_MED = 0x2C0D;
static constexpr uint16_t CMD_SINGLE_SHOT_STRETCH_LOW = 0x2C10;

/// Single-shot measurement commands with clock stretching disabled.
static constexpr uint16_t CMD_SINGLE_SHOT_NO_STRETCH_HIGH = 0x2400;
static constexpr uint16_t CMD_SINGLE_SHOT_NO_STRETCH_MED = 0x240B;
static constexpr uint16_t CMD_SINGLE_SHOT_NO_STRETCH_LOW = 0x2416;

/// Periodic measurement commands for each rate/repeatability pair.
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

static constexpr uint16_t CMD_FETCH_DATA = 0xE000; ///< Fetch Data command for periodic/ART readout

static constexpr uint16_t CMD_ART = 0x2B32; ///< Accelerated response time mode command

static constexpr uint16_t CMD_BREAK = 0x3093; ///< Break command to stop periodic/ART acquisition

/// @}
/// @name Status, reset, and heater commands
/// @{

static constexpr uint16_t CMD_READ_STATUS = 0xF32D;   ///< Read status register
static constexpr uint16_t CMD_CLEAR_STATUS = 0x3041;  ///< Clear status flags 15, 11, 10, and 4
static constexpr uint16_t CMD_SOFT_RESET = 0x30A2;    ///< Sensor soft reset command

static constexpr uint16_t CMD_HEATER_ENABLE = 0x306D;  ///< Enable integrated heater
static constexpr uint16_t CMD_HEATER_DISABLE = 0x3066; ///< Disable integrated heater

/// @}
/// @name Serial number commands
/// @{

static constexpr uint16_t CMD_SERIAL_STRETCH = 0x3780;    ///< Read serial/EIC using stretch command family
static constexpr uint16_t CMD_SERIAL_NO_STRETCH = 0x3682; ///< Read serial/EIC using no-stretch command family

/// @}
/// @name Alert-limit commands
/// @{

static constexpr uint16_t CMD_ALERT_READ_HIGH_SET = 0xE11F;   ///< Read high-set alert limit
static constexpr uint16_t CMD_ALERT_READ_HIGH_CLEAR = 0xE114; ///< Read high-clear alert limit
static constexpr uint16_t CMD_ALERT_READ_LOW_CLEAR = 0xE109;  ///< Read low-clear alert limit
static constexpr uint16_t CMD_ALERT_READ_LOW_SET = 0xE102;    ///< Read low-set alert limit

static constexpr uint16_t CMD_ALERT_WRITE_HIGH_SET = 0x611D;   ///< Write high-set alert limit plus CRC byte
static constexpr uint16_t CMD_ALERT_WRITE_HIGH_CLEAR = 0x6116; ///< Write high-clear alert limit plus CRC byte
static constexpr uint16_t CMD_ALERT_WRITE_LOW_CLEAR = 0x610B;  ///< Write low-clear alert limit plus CRC byte
static constexpr uint16_t CMD_ALERT_WRITE_LOW_SET = 0x6100;    ///< Write low-set alert limit plus CRC byte

/// @}
/// @name Status-register bit masks
/// @{

static constexpr uint16_t STATUS_ALERT_PENDING = 0x8000;    ///< Alert pending bit
static constexpr uint16_t STATUS_HEATER_ON = 0x2000;        ///< Heater status bit
static constexpr uint16_t STATUS_RH_ALERT = 0x0800;         ///< Humidity alert bit
static constexpr uint16_t STATUS_T_ALERT = 0x0400;          ///< Temperature alert bit
static constexpr uint16_t STATUS_RESET_DETECTED = 0x0010;   ///< Reset detected bit
static constexpr uint16_t STATUS_COMMAND_ERROR = 0x0002;    ///< Last command rejected bit
static constexpr uint16_t STATUS_WRITE_CRC_ERROR = 0x0001;  ///< Write checksum error bit

/// @}
/// @name Response frame lengths
/// @{

static constexpr size_t DATA_WORD_BYTES = 2;     ///< Bytes in one data word
static constexpr size_t DATA_CRC_BYTES = 1;      ///< CRC bytes following one data word
static constexpr size_t DATA_WORD_WITH_CRC = 3;  ///< One data word plus CRC

static constexpr size_t MEASUREMENT_DATA_LEN = 6; ///< Temperature word+CRC plus humidity word+CRC
static constexpr size_t STATUS_DATA_LEN = 3;      ///< Status word plus CRC
static constexpr size_t SERIAL_DATA_LEN = 6;      ///< Two serial/EIC words, each with CRC
static constexpr size_t ALERT_DATA_LEN = 3;       ///< Alert-limit word plus CRC

/// @}

} // namespace cmd
} // namespace SHT3x
