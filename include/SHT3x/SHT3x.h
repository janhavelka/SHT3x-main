/// @file SHT3x.h
/// @brief Main driver class for SHT3x
#pragma once

#include <cstddef>
#include <cstdint>
#include "SHT3x/Status.h"
#include "SHT3x/Config.h"
#include "SHT3x/CommandTable.h"
#include "SHT3x/Version.h"

namespace SHT3x {

/// Driver state for health monitoring
enum class DriverState : uint8_t {
  UNINIT,    ///< begin() not called or end() called
  READY,     ///< Operational, consecutiveFailures == 0
  DEGRADED,  ///< 1 <= consecutiveFailures < offlineThreshold
  OFFLINE    ///< consecutiveFailures >= offlineThreshold
};

/// Measurement result (float)
struct Measurement {
  float temperatureC = 0.0f; ///< Temperature in Celsius
  float humidityPct = 0.0f;  ///< Relative humidity in percent
};

/// Raw measurement values
struct RawSample {
  uint16_t rawTemperature = 0; ///< Raw temperature (16-bit)
  uint16_t rawHumidity = 0;    ///< Raw humidity (16-bit)
};

/// Fixed-point converted values (no float)
struct CompensatedSample {
  int32_t tempC_x100 = 0;       ///< Temperature * 100 (e.g., 2534 = 25.34 degC)
  uint32_t humidityPct_x100 = 0; ///< Humidity * 100 (e.g., 4234 = 42.34 %RH)
};

/// Parsed status register
struct StatusRegister {
  uint16_t raw = 0;            ///< Raw 16-bit status register value
  bool alertPending = false;   ///< ALERT_PENDING flag
  bool heaterOn = false;       ///< HEATER_ON flag
  bool rhAlert = false;        ///< Humidity alert event flag
  bool tAlert = false;         ///< Temperature alert event flag
  bool resetDetected = false;  ///< Reset-detected flag
  bool commandError = false;   ///< Last command was rejected by the sensor
  bool writeCrcError = false;  ///< Last command payload failed CRC validation
};

/// Snapshot of driver configuration and state
struct SettingsSnapshot {
  bool initialized = false;                                   ///< True after begin() succeeds
  DriverState state = DriverState::UNINIT;                   ///< Current driver state
  uint8_t i2cAddress = 0x44;                                 ///< Active 7-bit I2C address
  uint32_t i2cTimeoutMs = 50;                                ///< Active I2C timeout
  uint8_t offlineThreshold = 5;                              ///< Failure threshold for OFFLINE
  bool hasNowMsHook = false;                                 ///< True when Config::nowMs is provided
  Mode mode = Mode::SINGLE_SHOT;                              ///< Active acquisition mode
  Repeatability repeatability = Repeatability::HIGH_REPEATABILITY; ///< Cached repeatability setting
  PeriodicRate periodicRate = PeriodicRate::MPS_1;            ///< Cached periodic rate
  ClockStretching clockStretching = ClockStretching::STRETCH_DISABLED; ///< Cached clock-stretching policy
  bool periodicActive = false;                                ///< True if periodic or ART mode is currently running
  bool measurementPending = false;                            ///< A measurement request has been issued but not read yet
  bool measurementReady = false;                              ///< Cached sample is ready to read
  bool hasSample = false;                                     ///< True after at least one sample has been captured
  uint32_t measurementReadyMs = 0;                            ///< Deadline/timestamp associated with the pending sample
  uint32_t sampleTimestampMs = 0;                             ///< Timestamp of the last successful sample
  uint32_t missedSamples = 0;                                 ///< Best-effort missed periodic sample count
  StatusRegister status = {};                                 ///< Parsed status-register snapshot when available
  bool statusValid = false;                                   ///< True if status was read successfully for this snapshot
  Status statusReadStatus = Status::Error(Err::UNSUPPORTED, "Status not read"); ///< Result of the status-read attempt
};

/// Result for status reads that may temporarily stop periodic/ART acquisition
struct StatusReadSnapshot {
  StatusRegister status = {};                                 ///< Parsed status register when statusValid is true
  bool statusValid = false;                                   ///< True if the status read succeeded
  Status stopStatus = Status::Ok();                           ///< Break/stop result; OK when no stop was needed
  Status statusReadStatus = Status::Error(Err::UNSUPPORTED, "Status not read"); ///< Result of status read step
  Status restoreStatus = Status::Ok();                        ///< Restore result; OK when no restore was needed
  Mode initialMode = Mode::SINGLE_SHOT;                       ///< Mode observed before the operation
  Mode finalMode = Mode::SINGLE_SHOT;                         ///< Mode after all attempted steps
  bool modeInterrupted = false;                               ///< True when periodic/ART was stopped for this read
  bool restored = true;                                       ///< True when no restore was needed or restore succeeded
};

/// Cached sensor settings for restore-after-reset (RAM only)
struct CachedSettings {
  Mode mode = Mode::SINGLE_SHOT;                              ///< Mode to restore after reset
  Repeatability repeatability = Repeatability::HIGH_REPEATABILITY; ///< Repeatability to restore
  PeriodicRate periodicRate = PeriodicRate::MPS_1;            ///< Periodic rate to restore
  ClockStretching clockStretching = ClockStretching::STRETCH_DISABLED; ///< Clock-stretching policy to restore
  bool heaterEnabled = false;                                 ///< Cached heater state
  bool alertValid[4] = {false, false, false, false};          ///< Per-limit validity flags for cached alert words
  uint16_t alertRaw[4] = {0, 0, 0, 0};                        ///< Cached raw alert-limit words by AlertLimitKind index
};

/// Alert limit selector
enum class AlertLimitKind : uint8_t {
  HIGH_SET = 0,   ///< High alert set threshold
  HIGH_CLEAR = 1, ///< High alert clear threshold
  LOW_CLEAR = 2,  ///< Low alert clear threshold
  LOW_SET = 3     ///< Low alert set threshold
};

/// Decoded alert limit
struct AlertLimit {
  uint16_t raw = 0;         ///< Packed 16-bit limit word
  float temperatureC = 0.0f; ///< Approximate temperature threshold
  float humidityPct = 0.0f;  ///< Approximate humidity threshold
};

/// SHT3x driver class.
///
/// Public APIs are synchronous, not ISR-safe, and not internally thread-safe.
/// Serialize access externally and use interrupt handlers only to signal work
/// into normal task/loop context.
class SHT3x {
public:
  // =========================================================================
  // Lifecycle
  // =========================================================================

  /// Initialize the driver with configuration
  /// @param config Configuration including transport callbacks
  /// @return Status::Ok() on success, error otherwise
  Status begin(const Config& config);

  /// Process pending operations (call regularly from loop)
  /// @param nowMs Current timestamp from the same wrapping millisecond timebase
  ///              used by Config::nowMs
  void tick(uint32_t nowMs);

  /// Shutdown the driver and release resources
  void end();

  /// Check if begin() completed successfully and end() has not been called
  bool isInitialized() const { return _initialized; }

  /// Get the active configuration snapshot
  const Config& getConfig() const { return _config; }

  // =========================================================================
  // Diagnostics
  // =========================================================================

  /// Check if device is present on the bus (no health tracking)
  /// @return Status::Ok() if device responds, error otherwise
  Status probe();

  /// Attempt to recover from DEGRADED/OFFLINE state
  /// @return Status::Ok() if device now responsive, error otherwise
  Status recover();

  /// Reset sensor to defaults (no restore)
  Status resetToDefaults();

  /// Reset sensor and restore cached settings
  Status resetAndRestore();

  // =========================================================================
  // Driver State
  // =========================================================================

  /// Get current driver state
  DriverState state() const { return _driverState; }

  /// Alias for state() used by shared diagnostics.
  DriverState driverState() const { return state(); }

  /// Check if driver is ready for operations
  bool isOnline() const {
    return _driverState == DriverState::READY ||
           _driverState == DriverState::DEGRADED;
  }

  /// Check if periodic acquisition is currently active
  bool isPeriodicActive() const { return _periodicActive; }

  // =========================================================================
  // Health Tracking
  // =========================================================================

  /// Timestamp of last successful I2C operation
  uint32_t lastOkMs() const { return _lastOkMs; }

  /// Timestamp of last failed I2C operation
  uint32_t lastErrorMs() const { return _lastErrorMs; }

  /// Timestamp of last I2C bus activity (success or expected NACK)
  uint32_t lastBusActivityMs() const { return _lastBusActivityMs; }

  /// Most recent error status
  Status lastError() const { return _lastError; }

  /// Consecutive failures since last success
  uint8_t consecutiveFailures() const { return _consecutiveFailures; }

  /// Total failure count (lifetime)
  uint32_t totalFailures() const { return _totalFailures; }

  /// Total success count (lifetime)
  uint32_t totalSuccess() const { return _totalSuccess; }

  /// Count of consecutive "not-ready" responses during periodic fetch
  uint32_t notReadyCount() const { return _notReadyCount; }

  // =========================================================================
  // Measurement API
  // =========================================================================

  /// Request a measurement (non-blocking)
  /// In SINGLE_SHOT mode: triggers measurement
  /// In PERIODIC/ART mode: schedules next fetch
  /// Returns IN_PROGRESS if measurement started, BUSY if already pending
  Status requestMeasurement();

  /// Check if measurement is ready to read
  bool measurementReady() const { return _measurementReady; }

  /// True after at least one sample has been cached.
  bool hasSample() const { return _hasSample; }

  /// Timestamp of last completed sample (0 if none)
  uint32_t sampleTimestampMs() const { return _sampleTimestampMs; }

  /// Age of the last captured sample in milliseconds.
  /// @param nowMs Current monotonic timestamp in milliseconds
  /// @return `nowMs - sampleTimestampMs()` when a sample exists, otherwise 0
  uint32_t sampleAgeMs(uint32_t nowMs) const {
    return _hasSample ? (nowMs - _sampleTimestampMs) : 0;
  }

  /// Best-effort estimate of missed samples (periodic/ART mode)
  uint32_t missedSamplesEstimate() const { return _missedSamples; }

  /// Get measurement result (float)
  /// Returns MEASUREMENT_NOT_READY if not available
  /// Clears ready flag after successful read
  Status getMeasurement(Measurement& out);

  /// Get last captured raw measurement values.
  /// @param[out] out Last cached raw temperature/humidity words
  /// @return Status::Ok() on success, MEASUREMENT_NOT_READY until a sample has been captured
  Status getRawSample(RawSample& out) const;

  /// Get last captured fixed-point converted values.
  /// @param[out] out Last cached fixed-point sample
  /// @return Status::Ok() on success, MEASUREMENT_NOT_READY until a sample has been captured
  Status getCompensatedSample(CompensatedSample& out) const;

  // =========================================================================
  // Configuration
  // =========================================================================

  /// Set operating mode (SINGLE_SHOT, PERIODIC, ART)
  Status setMode(Mode mode);

  /// Get current operating mode
  Status getMode(Mode& out) const;

  /// Get a snapshot of current settings/state (no I2C)
  Status getSettings(SettingsSnapshot& out) const;

  /// Get cached settings for restore-after-reset
  CachedSettings getCachedSettings() const { return _cachedSettings; }

  /// Check if cached settings are available
  bool hasCachedSettings() const { return _hasCachedSettings; }

  /// Get a snapshot of settings/state and attempt a non-disruptive status read.
  /// statusValid is true only if the status read succeeds; statusReadStatus
  /// records the exact status-read result when it does not.
  Status readSettings(SettingsSnapshot& out);

  // =========================================================================
  // Low-Level Command Access
  // =========================================================================

  /// Issue a raw 16-bit command using the tracked transport path.
  /// @param command 16-bit SHT3x command constant from CommandTable.h
  /// @return Status::Ok() on success, error otherwise
  Status writeCommand(uint16_t command);

  /// Issue a raw 16-bit command followed by a packed 16-bit data word.
  /// The CRC byte is computed internally.
  /// @param command 16-bit SHT3x command constant from CommandTable.h
  /// @param data Packed 16-bit payload word
  /// @return Status::Ok() on success, error otherwise
  Status writeCommandWithData(uint16_t command, uint16_t data);

  /// Issue a 16-bit command and read a raw response frame.
  /// The command spacing gate and health tracking still apply.
  /// @param command 16-bit SHT3x command constant from CommandTable.h
  /// @param out Read buffer
  /// @param len Number of bytes to read; SHT3x responses are limited to 6 bytes
  /// @param allowNoData Map a read-header NACK to MEASUREMENT_NOT_READY when supported
  /// @note Invalid buffers and oversized reads are rejected before any I2C command is sent.
  /// @return Status::Ok() on success, error otherwise
  Status readCommand(uint16_t command, uint8_t* out, size_t len,
                     bool allowNoData = false);

  /// Set measurement repeatability; active periodic/ART modes are restarted and
  /// cached settings are updated only after the restart succeeds.
  Status setRepeatability(Repeatability rep);

  /// Get current repeatability
  Status getRepeatability(Repeatability& out) const;

  /// Set clock stretching mode (single-shot/serial reads)
  Status setClockStretching(ClockStretching stretch);

  /// Get current clock stretching mode
  Status getClockStretching(ClockStretching& out) const;

  /// Set periodic rate; active periodic/ART modes are restarted and cached
  /// settings are updated only after the restart succeeds.
  Status setPeriodicRate(PeriodicRate rate);

  /// Get current periodic rate
  Status getPeriodicRate(PeriodicRate& out) const;

  /// Start periodic measurements
  Status startPeriodic(PeriodicRate rate, Repeatability rep);

  /// Start ART (accelerated response time) mode
  Status startArt();

  /// Stop periodic/ART mode (Break)
  Status stopPeriodic();

  // =========================================================================
  // Status / Heater / Resets
  // =========================================================================

  /// Read raw status register without clearing flags.
  /// @note Returns BUSY while periodic/ART acquisition is active; use
  ///       readStatusWithModeRestore() when ALERT diagnosis is needed in those modes.
  Status readStatus(uint16_t& raw);

  /// Read and parse status register without clearing flags.
  /// @note Returns BUSY while periodic/ART acquisition is active; use
  ///       readStatusWithModeRestore() when ALERT diagnosis is needed in those modes.
  Status readStatus(StatusRegister& out);

  /// Read status, breaking and restoring periodic/ART mode when needed.
  /// @note This is disruptive in periodic/ART mode: it sends Break, aborts the
  ///       current acquisition cadence, reads status, and attempts to restore
  ///       the prior periodic or ART mode. Inspect the snapshot for partial
  ///       stop/read/restore results when this returns an error.
  Status readStatusWithModeRestore(StatusReadSnapshot& out);

  /// Clear status flags. This is destructive for status bits 15, 11, 10, and 4.
  /// @note Returns BUSY while periodic/ART acquisition is active.
  Status clearStatus();

  /// Enable/disable heater
  Status setHeater(bool enable);

  /// Read heater state from status register
  Status readHeaterStatus(bool& enabled);

  /// Soft reset the device
  Status softReset();

  /// Interface reset sequence (SCL pulse recovery)
  Status interfaceReset();

  /// General call reset (bus-wide)
  Status generalCallReset();

  // =========================================================================
  // Serial Number
  // =========================================================================

  /// Read electronic identification code (serial number)
  Status readSerialNumber(uint32_t& serial,
                          ClockStretching stretch = ClockStretching::STRETCH_DISABLED);

  // =========================================================================
  // Alert Limits
  // =========================================================================

  /// Read raw alert limit word.
  /// @note Alert mode is active during periodic acquisition, but alert-limit
  ///       commands are not documented as valid while periodic/ART is running.
  ///       This API returns BUSY in active periodic/ART mode.
  Status readAlertLimitRaw(AlertLimitKind kind, uint16_t& value);

  /// Read and decode alert limit.
  /// @note Returns BUSY in active periodic/ART mode.
  Status readAlertLimit(AlertLimitKind kind, AlertLimit& out);

  /// Write raw alert limit word (CRC is computed internally).
  /// @note Returns BUSY in active periodic/ART mode.
  Status writeAlertLimitRaw(AlertLimitKind kind, uint16_t value);

  /// Encode and write alert limit from physical values.
  /// @note Returns BUSY in active periodic/ART mode.
  Status writeAlertLimit(AlertLimitKind kind, float temperatureC, float humidityPct);

  /// Disable alerts by setting LowSet > HighSet
  Status disableAlerts();

  // =========================================================================
  // Helpers
  // =========================================================================

  /// Encode alert limit word from physical values
  static uint16_t encodeAlertLimit(float temperatureC, float humidityPct);

  /// Decode alert limit word into physical values
  static void decodeAlertLimit(uint16_t limit, float& temperatureC, float& humidityPct);

  /// Convert raw temperature to Celsius (float)
  static float convertTemperatureC(uint16_t raw);

  /// Convert raw humidity to percent (float)
  static float convertHumidityPct(uint16_t raw);

  /// Convert raw temperature to Celsius * 100
  static int32_t convertTemperatureC_x100(uint16_t raw);

  /// Convert raw humidity to percent * 100
  static uint32_t convertHumidityPct_x100(uint16_t raw);

  // =========================================================================
  // Timing
  // =========================================================================

  /// Estimate max measurement time based on current repeatability
  /// Returns time in milliseconds
  uint32_t estimateMeasurementTimeMs() const;

private:
  // =========================================================================
  // Transport Wrappers
  // =========================================================================

  /// Raw I2C write-read (no health tracking)
  Status _i2cWriteReadRaw(const uint8_t* txBuf, size_t txLen,
                          uint8_t* rxBuf, size_t rxLen);

  /// Raw I2C write (no health tracking)
  Status _i2cWriteRaw(const uint8_t* buf, size_t len);

  /// Raw I2C write to alternate address (no health tracking)
  Status _i2cWriteRawAddr(uint8_t addr, const uint8_t* buf, size_t len);

  /// Tracked I2C write to alternate address (updates health)
  Status _i2cWriteRawAddrTracked(uint8_t addr, const uint8_t* buf, size_t len);

  /// Tracked I2C write-read (updates health)
  Status _i2cWriteReadTracked(const uint8_t* txBuf, size_t txLen,
                              uint8_t* rxBuf, size_t rxLen);

  /// Tracked I2C write-read with optional no-data handling (updates health)
  Status _i2cWriteReadTrackedAllowNoData(const uint8_t* txBuf, size_t txLen,
                                         uint8_t* rxBuf, size_t rxLen,
                                         bool allowNoData);

  /// Tracked I2C write (updates health)
  Status _i2cWriteTracked(const uint8_t* buf, size_t len);

  /// Return BUSY when normal operations try I2C while OFFLINE.
  Status _offlineStatus() const;

  // =========================================================================
  // Command Access
  // =========================================================================

  Status _writeCommand(uint16_t cmd, bool tracked);
  Status _writeCommandWithData(uint16_t cmd, uint16_t data, bool tracked);
  Status _readAfterCommand(uint8_t* buf, size_t len, bool tracked,
                           bool allowNoData = false);
  Status _readOnly(uint8_t* buf, size_t len, bool tracked,
                   bool allowNoData = false);

  // =========================================================================
  // Health Management
  // =========================================================================

  /// Update health counters and state based on operation result
  /// Called ONLY from tracked transport wrappers
  Status _updateHealth(const Status& st);
  void _reassertOfflineLatch();

  /// Record any bus activity (including expected NACK)
  void _recordBusActivity(uint32_t nowMs);

  // =========================================================================
  // Internal Helpers
  // =========================================================================

  uint32_t _periodicFetchMarginMs() const;
  uint32_t _periodicReadyMs(uint32_t nowMs) const;
  uint32_t _periodicRetryMs(uint32_t nowMs) const;
  bool _singleShotMeasurementPending() const;
  Status _ensureCommandDelay();
  Status _waitMs(uint32_t delayMs);
  Status _readStatusRaw(uint16_t& raw, bool tracked);
  Status _readMeasurementRaw(RawSample& out, bool tracked, bool allowNoData);
  Status _fetchPeriodic();
  Status _startSingleShot();
  Status _enterPeriodic(PeriodicRate rate, Repeatability rep, bool art);
  Status _stopPeriodicInternal();
  Status _applyCachedSettingsAfterReset();
  Status _performRecoveryLadder();
  void _setSafeBaseline();
  void _setDefaultsToConfigAndCache();
  void _syncCacheFromConfig();

  static uint8_t _crc8(const uint8_t* data, size_t len);
  static uint16_t _commandForSingleShot(Repeatability rep, ClockStretching stretch);
  static uint16_t _commandForPeriodic(Repeatability rep, PeriodicRate rate);
  static uint16_t _commandForAlertRead(AlertLimitKind kind);
  static uint16_t _commandForAlertWrite(AlertLimitKind kind);
  static uint32_t _periodMsForRate(PeriodicRate rate);
  static bool _timeElapsed(uint32_t now, uint32_t target);
  static void _parseStatusRegister(uint16_t raw, StatusRegister& out);

  // =========================================================================
  // State
  // =========================================================================

  Config _config;
  bool _initialized = false;
  DriverState _driverState = DriverState::UNINIT;

  // Health counters
  uint32_t _lastOkMs = 0;
  uint32_t _lastErrorMs = 0;
  uint32_t _lastBusActivityMs = 0;
  Status _lastError = Status::Ok();
  uint8_t _consecutiveFailures = 0;
  uint32_t _totalFailures = 0;
  uint32_t _totalSuccess = 0;
  bool _allowOfflineI2c = false;

  // Command timing
  uint32_t _lastCommandUs = 0;
  bool _lastCommandValid = false;

  // Measurement state
  bool _measurementRequested = false;
  bool _measurementReady = false;
  bool _hasSample = false;
  uint32_t _measurementReadyMs = 0;
  uint32_t _periodicStartMs = 0;
  uint32_t _lastFetchMs = 0;
  uint32_t _periodMs = 0;
  uint32_t _sampleTimestampMs = 0;
  uint32_t _missedSamples = 0;
  uint32_t _notReadyStartMs = 0;
  uint32_t _notReadyCount = 0;
  uint32_t _lastRecoverMs = 0;
  bool _lastRecoverValid = false;

  CachedSettings _cachedSettings = {};
  bool _hasCachedSettings = false;

  RawSample _rawSample;
  CompensatedSample _compSample;
  Mode _mode = Mode::SINGLE_SHOT;
  bool _periodicActive = false;
};

} // namespace SHT3x
