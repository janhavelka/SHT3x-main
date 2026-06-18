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

/// Result from one bounded measurement polling step.
struct PollJobResult {
  Status status = Status::Error(Err::MEASUREMENT_NOT_READY, "No poll job active"); ///< Current job status
  uint8_t instructionsUsed = 0; ///< Number of I2C instructions executed
  bool active = false;          ///< True while a measurement job remains pending
  bool completed = false;       ///< True when this step completed a sample
};

/// Parsed status register
struct StatusRegister {
  uint16_t raw = 0;            ///< Raw 16-bit status register value
  bool alertPending = false;   ///< Bit 15, alert pending; cleared by clearStatus()
  bool heaterOn = false;       ///< Bit 13, heater status; not cleared by clearStatus()
  bool rhAlert = false;        ///< Bit 11, humidity alert event; cleared by clearStatus()
  bool tAlert = false;         ///< Bit 10, temperature alert event; cleared by clearStatus()
  bool resetDetected = false;  ///< Bit 4, reset detected; cleared by clearStatus()
  bool commandError = false;   ///< Bit 1, last command rejected; not cleared by clearStatus()
  bool writeCrcError = false;  ///< Bit 0, last write payload CRC failed; not cleared by clearStatus()
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
  Status lastMeasurementStatus = Status::Error(Err::MEASUREMENT_NOT_READY,
                                               "Measurement not ready"); ///< Last measurement-path status
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
  Status stopStatus = Status::Ok();                           ///< Break/stop result; OK when no stop was needed or stop succeeded
  Status statusReadStatus = Status::Error(Err::UNSUPPORTED, "Status not read"); ///< Result of status read step
  Status restoreStatus = Status::Ok();                        ///< Restore result; OK when no restore was needed or restore succeeded
  Mode initialMode = Mode::SINGLE_SHOT;                       ///< Mode observed before the operation
  Mode finalMode = Mode::SINGLE_SHOT;                         ///< Mode after all attempted steps
  bool modeInterrupted = false;                               ///< True when periodic/ART was stopped for this read
  bool restored = true;                                       ///< True when no restore was needed or restore succeeded
};

/// Cached sensor settings for restore-after-reset (RAM only).
/// @note This is a write-through restore plan, not a live hardware snapshot.
///       Alert entries become valid only after successful driver writes; reads
///       from hardware do not populate this cache.
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
  SHT3x() = default;
  SHT3x(const SHT3x&) = delete;
  SHT3x& operator=(const SHT3x&) = delete;
  SHT3x(SHT3x&&) = delete;
  SHT3x& operator=(SHT3x&&) = delete;

  // =========================================================================
  // Lifecycle
  // =========================================================================

  /// Initialize the driver with configuration.
  /// @note Performs multiple bounded transactions: best-effort Break, soft
  ///       reset, status probe, and optional periodic/ART start from config.
  /// @param config Configuration including transport callbacks
  /// @return Status::Ok() on success, error otherwise
  Status begin(const Config& config);

  /// Process pending measurement work.
  /// @note This function is bounded, but it may perform one measurement step
  ///       when a previously requested sample is due. Single-shot completion
  ///       performs one receive-only read; periodic/ART completion performs a
  ///       Fetch Data command write plus receive-only read. In OFFLINE it clears
  ///       pending measurement state and does not touch the bus.
  /// @note The return type is void. I2C failures are observable through
  ///       lastError(), consecutiveFailures(), totalFailures(), driverState(),
  ///       and retry scheduling. Protocol failures after a successful bus
  ///       transaction, such as CRC mismatch, do not update transport health;
  ///       they leave no ready sample and are retried through normal scheduling.
  ///       Use explicit read APIs when the caller must handle a synchronous
  ///       Status at the call site.
  /// @param nowMs Current timestamp from the same wrapping millisecond timebase
  ///              used by Config::nowMs
  void tick(uint32_t nowMs);

  /// Advance a pending measurement job with a bounded I2C instruction budget.
  /// One command write or read-only measurement frame counts as one instruction.
  Status pollJob(uint32_t nowMs, uint8_t maxInstructions, PollJobResult& result);

  /// End the local driver session.
  /// @note This clears runtime/session state and returns the instance to
  ///       UNINIT. It does not send Break, reset the sensor, or guarantee that
  ///       physical periodic/ART acquisition has stopped. Call stopPeriodic()
  ///       before end() when hardware acquisition state matters.
  void end();

  /// Check if begin() completed successfully and end() has not been called
  bool isInitialized() const { return _initialized; }

  /// Get the active normalized configuration reference.
  /// @note The returned reference is owned by the driver and remains valid
  ///       until the next begin() or end().
  const Config& getConfig() const { return _config; }

  // =========================================================================
  // Diagnostics
  // =========================================================================

  /// Raw diagnostic presence check with no health tracking.
  /// @note Uses the status-register command path through raw transport wrappers.
  ///       This can issue I2C outside normal health accounting and is not an
  ///       online-state verdict. Avoid calling during active acquisition unless
  ///       that diagnostic side effect is intentional.
  /// @return Status::Ok() if device responds, error otherwise
  Status probe();

  /// Run the manual communication recovery ladder after begin().
  /// @note May run multiple bounded transactions through the configured
  ///       recovery ladder and is destructive to pending measurement/acquisition
  ///       state. On success the driver is left in safe SINGLE_SHOT idle mode.
  ///       General-call reset is used only when explicitly enabled because it
  ///       affects all supporting devices on the bus.
  /// @return Status::Ok() if device now responsive, error otherwise
  Status recover();

  /// Recover communication and reset the driver's desired settings to defaults.
  /// @note This calls the recovery ladder, sets a safe single-shot baseline,
  ///       and resets the local restore cache to defaults. The recovery ladder
  ///       may stop after a successful probe without issuing a sensor reset.
  Status resetToDefaults();

  /// Recover communication and reapply cached settings.
  /// @note This calls the recovery ladder, sets a safe single-shot baseline,
  ///       then reapplies cached settings. The recovery ladder may stop after a
  ///       successful probe without issuing a sensor reset. A failure can leave
  ///       hardware partially restored while the returned Status identifies the step.
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

  /// Timestamp of last successful tracked transport operation after begin()
  uint32_t lastOkMs() const { return _lastOkMs; }

  /// Timestamp of last failed tracked transport operation after begin()
  uint32_t lastErrorMs() const { return _lastErrorMs; }

  /// Timestamp of last tracked I2C attempt after begin().
  /// @note Includes successes, failures, and expected read-header NACKs.
  uint32_t lastBusActivityMs() const { return _lastBusActivityMs; }

  /// Most recent tracked transport error status.
  /// @note Validation, precondition, CRC, and sensor status-bit errors are
  ///       returned by the API that observes them but do not replace this value
  ///       unless they occur through a tracked transport wrapper.
  Status lastError() const { return _lastError; }

  /// Consecutive tracked transport failures since last tracked success
  uint8_t consecutiveFailures() const { return _consecutiveFailures; }

  /// Total tracked transport failure count for the current driver session
  uint32_t totalFailures() const { return _totalFailures; }

  /// Total tracked transport success count for the current driver session
  uint32_t totalSuccess() const { return _totalSuccess; }

  /// Count of consecutive "not-ready" responses during periodic fetch
  uint32_t notReadyCount() const { return _notReadyCount; }

  // =========================================================================
  // Measurement API
  // =========================================================================

  /// Request or schedule a measurement.
  /// @note In SINGLE_SHOT mode this sends one bounded I2C command immediately
  ///       and schedules the later read for tick(). In PERIODIC/ART mode this
  ///       schedules the next Fetch Data operation for tick().
  /// @return IN_PROGRESS if measurement started/scheduled, BUSY if already pending, or an error.
  Status requestMeasurement();

  /// Check if measurement is ready to read
  bool measurementReady() const { return _measurementReady; }

  /// Check if a measurement request is pending completion.
  bool measurementPending() const { return _measurementRequested && !_measurementReady; }

  /// True after at least one sample has been cached.
  bool hasSample() const { return _hasSample; }

  /// Most recent measurement-path result from request, tick, pollJob, or readout.
  Status lastMeasurementStatus() const { return _lastMeasurementStatus; }

  /// Current measurement status derived from cached state.
  Status measurementStatus() const;

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

  /// Set operating mode (SINGLE_SHOT, PERIODIC, ART).
  /// @note Switching into or out of periodic/ART can send Break and/or start
  ///       commands; cached mode is updated only after success.
  Status setMode(Mode mode);

  /// Get current operating mode
  Status getMode(Mode& out) const;

  /// Get a snapshot of current settings/state (no I2C)
  Status getSettings(SettingsSnapshot& out) const;

  /// Get cached settings for restore-after-reset.
  /// @note This is the driver's desired restore plan, not a live sensor readback.
  CachedSettings getCachedSettings() const { return _cachedSettings; }

  /// Check if cached settings are available
  bool hasCachedSettings() const { return _hasCachedSettings; }

  /// Get a snapshot of settings/state and attempt a non-disruptive status read.
  /// statusValid is true only if the status read succeeds; statusReadStatus
  /// records the exact status-read result when it does not.
  /// @note In active periodic/ART mode the status read is not issued; the
  ///       snapshot returns OK with statusValid=false and statusReadStatus=BUSY.
  ///       The same OK snapshot behavior applies while a single-shot
  ///       measurement is pending.
  ///       In OFFLINE state, readSettings() returns BUSY like other normal
  ///       public I2C APIs.
  Status readSettings(SettingsSnapshot& out);

  // =========================================================================
  // Low-Level Command Access
  // =========================================================================

  /// Issue one raw 16-bit command using the tracked transport path.
  /// @param command 16-bit SHT3x command constant from CommandTable.h
  /// @note Expert escape hatch. This bypasses high-level mode safety for
  ///       periodic/ART, so the caller owns datasheet command legality,
  ///       hardware desynchronization risk, local cache coherence, and any
  ///       status side effects. Stop periodic/ART before raw commands unless
  ///       the datasheet explicitly permits the command in the active mode.
  /// @return Status::Ok() on success, error otherwise
  Status writeCommand(uint16_t command);

  /// Issue one raw 16-bit command followed by a packed 16-bit data word.
  /// The CRC byte is computed internally.
  /// @param command 16-bit SHT3x command constant from CommandTable.h
  /// @param data Packed 16-bit payload word
  /// @note Expert escape hatch. This bypasses high-level mode safety for
  ///       periodic/ART, so the caller owns datasheet command legality,
  ///       hardware desynchronization risk, local cache coherence,
  ///       write-payload meaning, and status side effects. Stop periodic/ART before raw commands unless the
  ///       datasheet explicitly permits the command in the active mode.
  /// @return Status::Ok() on success, error otherwise
  Status writeCommandWithData(uint16_t command, uint16_t data);

  /// Issue a 16-bit command and read a raw response frame.
  /// The command spacing gate and health tracking still apply.
  /// @param command 16-bit SHT3x command constant from CommandTable.h
  /// @param out Read buffer
  /// @param len Number of bytes to read; SHT3x responses are limited to 6 bytes
  /// @param allowNoData Map a read-header NACK to MEASUREMENT_NOT_READY when supported
  /// @note Invalid buffers and oversized reads are rejected before any I2C command is sent.
  /// @note Expert escape hatch. This bypasses high-level mode safety for
  ///       periodic/ART, so the caller owns datasheet command legality,
  ///       response length, response CRC interpretation, and desynchronization
  ///       risk. Raw reads do not update local mode/heater/alert caches. Stop
  ///       periodic/ART before raw commands unless the datasheet explicitly
  ///       permits the command in the active mode.
  /// @return Status::Ok() on success, error otherwise
  Status readCommand(uint16_t command, uint8_t* out, size_t len,
                     bool allowNoData = false);

  /// Set measurement repeatability; active periodic/ART modes are restarted and
  /// cached settings are updated only after the restart succeeds.
  /// @note If stopping an active periodic/ART mode succeeds but the restart
  ///       command fails, the sensor and driver are left in single-shot idle
  ///       while cached settings still describe the last fully applied plan.
  Status setRepeatability(Repeatability rep);

  /// Get current repeatability
  Status getRepeatability(Repeatability& out) const;

  /// Set clock stretching mode for single-shot measurement and serial-number reads.
  /// @note Periodic/ART modes use Fetch Data and do not use this setting.
  Status setClockStretching(ClockStretching stretch);

  /// Get current clock stretching mode
  Status getClockStretching(ClockStretching& out) const;

  /// Set periodic rate; active periodic/ART modes are restarted and cached
  /// settings are updated only after the restart succeeds.
  /// @note If stopping an active periodic/ART mode succeeds but the restart
  ///       command fails, the sensor and driver are left in single-shot idle
  ///       while cached settings still describe the last fully applied plan.
  Status setPeriodicRate(PeriodicRate rate);

  /// Get current periodic rate
  Status getPeriodicRate(PeriodicRate& out) const;

  /// Start periodic measurements.
  /// @note Sends one start command when idle. If already in periodic/ART, it
  ///       first sends Break and only updates cached settings after success.
  Status startPeriodic(PeriodicRate rate, Repeatability rep);

  /// Start ART (accelerated response time) mode.
  /// @note Sends one ART command when idle. If already in periodic/ART, it
  ///       first sends Break and only updates cached settings after success.
  Status startArt();

  /// Stop periodic/ART mode (Break).
  /// @note Sends one Break command when active. After Break is accepted, local
  ///       state is updated before the bounded 1 ms processing wait.
  Status stopPeriodic();

  // =========================================================================
  // Status / Heater / Resets
  // =========================================================================

  /// Read raw status register without clearing flags.
  /// @note Returns BUSY while a single-shot measurement is pending or
  ///       periodic/ART acquisition is active. Use readStatusWithModeRestore()
  ///       when ALERT diagnosis is needed in periodic/ART modes.
  Status readStatus(uint16_t& raw);

  /// Read and parse status register without clearing flags.
  /// @note Returns BUSY while a single-shot measurement is pending or
  ///       periodic/ART acquisition is active. Use readStatusWithModeRestore()
  ///       when ALERT diagnosis is needed in periodic/ART modes.
  Status readStatus(StatusRegister& out);

  /// Read status, breaking and restoring periodic/ART mode when needed.
  /// @note This is disruptive in periodic/ART mode: it sends Break, aborts the
  ///       current acquisition cadence, reads status, and attempts to restore
  ///       the prior periodic or ART mode. Inspect the snapshot for partial
  ///       stop/read/restore results when this returns an error. statusValid is
  ///       true only when the status read step succeeded; restored is true only
  ///       when no restore was needed or the previous periodic/ART mode was
  ///       restarted successfully. If both status read and restore fail, the
  ///       top-level return reports restoreStatus; inspect statusReadStatus for
  ///       the earlier status-read failure.
  Status readStatusWithModeRestore(StatusReadSnapshot& out);

  /// Clear status flags. This is destructive for status bits 15, 11, 10, and 4.
  /// @note Returns BUSY while periodic/ART acquisition is active.
  Status clearStatus();

  /// Enable/disable heater.
  /// @note The heater is intended for plausibility checks and condensation
  ///       mitigation workflows, not normal measurement. Self-heating can affect
  ///       temperature and humidity readings. Stop periodic/ART before changing
  ///       it. Cached heater state is updated only after success.
  Status setHeater(bool enable);

  /// Read heater state from status register.
  /// @note Follows readStatus() restrictions in active periodic/ART mode.
  Status readHeaterStatus(bool& enabled);

  /// Soft reset the device.
  /// @note Sends one reset command and waits the bounded reset delay. Returns
  ///       BUSY while periodic/ART is active. A pending single-shot conversion
  ///       is not preserved; on reset success pending measurement/sample state
  ///       is cleared, mode is set to SINGLE_SHOT, and the restore cache is not
  ///       automatically rewritten.
  Status softReset();

  /// Interface reset sequence (SCL pulse recovery).
  /// @note Uses the application-provided bus reset callback. The callback must
  ///       be bounded, must not recursively call into the same SHT3x instance,
  ///       and must own any GPIO/SCL sequencing externally. Direct calls clear
  ///       pending/sample state and update command spacing, but callback failure
  ///       is not a tracked transport failure and success does not prove a
  ///       sensor reset occurred.
  Status interfaceReset();

  /// General call reset (bus-wide).
  /// @note Resets every supporting device on the bus and is therefore an
  ///       application/bus-manager decision guarded by Config::allowGeneralCallReset.
  ///       It is disabled by default and is used by recover() only when that
  ///       explicit opt-in is set. On success, local measurement state is
  ///       cleared and mode is set to SINGLE_SHOT.
  Status generalCallReset();

  // =========================================================================
  // Serial Number
  // =========================================================================

  /// Read electronic identification code (serial number).
  /// @param[out] serial 32-bit serial/EIC value assembled from two CRC-checked words
  /// @param stretch Command family to use for this read
  /// @return Status::Ok() on success, BUSY when measurement or periodic/ART
  ///         activity blocks the read, CRC_MISMATCH on invalid CRC, or an I2C error.
  /// @note This proves a CRC-valid SHT3x serial/EIC transaction, not the exact
  ///       product grade or humidity accuracy.
  Status readSerialNumber(uint32_t& serial,
                          ClockStretching stretch = ClockStretching::STRETCH_DISABLED);

  // =========================================================================
  // Alert Limits
  // =========================================================================

  /// Read raw alert limit word.
  /// @note Alert mode is active during periodic acquisition, but alert-limit
  ///       commands are not documented as valid while periodic/ART is running.
  ///       This API returns BUSY in active periodic/ART mode.
  /// @return Status::Ok() on success, CRC_MISMATCH on invalid response CRC, BUSY
  ///         when acquisition blocks access, or an I2C/precondition error.
  Status readAlertLimitRaw(AlertLimitKind kind, uint16_t& value);

  /// Read and decode alert limit.
  /// @note Returns BUSY in active periodic/ART mode.
  /// @return Status::Ok() on success or the status from readAlertLimitRaw().
  Status readAlertLimit(AlertLimitKind kind, AlertLimit& out);

  /// Write raw alert limit word (CRC is computed internally).
  /// @note Returns BUSY in active periodic/ART mode.
  /// @return Status::Ok() on success, COMMAND_FAILED or WRITE_CRC_ERROR when
  ///         status verification reports a sensor-side rejection, BUSY when
  ///         acquisition blocks access, or an I2C/precondition error.
  Status writeAlertLimitRaw(AlertLimitKind kind, uint16_t value);

  /// Encode and write alert limit from physical values.
  /// @note Returns BUSY in active periodic/ART mode.
  /// @return Status::Ok() on success, INVALID_PARAM for non-finite inputs, or
  ///         the status from writeAlertLimitRaw().
  Status writeAlertLimit(AlertLimitKind kind, float temperatureC, float humidityPct);

  /// Disable alerts by setting LowSet > HighSet.
  /// @note Multi-step operation: HIGH_SET is written before LOW_SET. If LOW_SET
  ///       fails, HIGH_SET may already be applied and cached.
  /// @return Status::Ok() only after both writes succeed; otherwise the first
  ///         failing write status is returned and partial hardware/cache state is possible.
  Status disableAlerts();

  // =========================================================================
  // Helpers
  // =========================================================================

  /// Encode alert limit word from physical values.
  /// @param temperatureC Temperature threshold in Celsius
  /// @param humidityPct Relative humidity threshold in percent
  /// @return Packed RH7/T9 alert-limit word
  /// @note Inputs are clamped to the SHT3x representable range. Non-finite
  ///       values are treated as invalid by writeAlertLimit().
  /// @note The Sensirion alert application-note reset-default labels
  ///       (80/60, 79/58, 22/-9, 20/-10 in RH%/degC order) encode to the
  ///       documented default raw words through this API as
  ///       encodeAlertLimit(60, 80) -> 0xCD33,
  ///       encodeAlertLimit(58, 79) -> 0xC92D,
  ///       encodeAlertLimit(-9, 22) -> 0x3869, and
  ///       encodeAlertLimit(-10, 20) -> 0x3466. Other values use reduced
  ///       RH7/T9 quantization.
  static uint16_t encodeAlertLimit(float temperatureC, float humidityPct);

  /// Decode alert limit word into physical values.
  /// @param limit Packed RH7/T9 alert-limit word
  /// @param[out] temperatureC Decoded approximate temperature in Celsius
  /// @param[out] humidityPct Decoded approximate relative humidity in percent
  /// @note Alert-limit packing is quantized, so decode is approximate.
  static void decodeAlertLimit(uint16_t limit, float& temperatureC, float& humidityPct);

  /// Convert raw temperature to Celsius (float)
  /// @param raw Raw 16-bit temperature word
  /// @return Temperature in Celsius
  static float convertTemperatureC(uint16_t raw);

  /// Convert raw humidity to percent (float)
  /// @param raw Raw 16-bit humidity word
  /// @return Relative humidity in percent
  static float convertHumidityPct(uint16_t raw);

  /// Convert raw temperature to Celsius * 100
  /// @param raw Raw 16-bit temperature word
  /// @return Temperature in centi-degrees Celsius
  static int32_t convertTemperatureC_x100(uint16_t raw);

  /// Convert raw humidity to percent * 100
  /// @param raw Raw 16-bit humidity word
  /// @return Relative humidity in centi-percent
  static uint32_t convertHumidityPct_x100(uint16_t raw);

  // =========================================================================
  // Timing
  // =========================================================================

  /// Estimate max measurement time based on current repeatability, Config::lowVdd,
  /// and the driver's safety margin.
  /// @return Measurement time in milliseconds
  uint32_t estimateMeasurementTimeMs() const;

private:
  enum class MeasurementPhase : uint8_t {
    IDLE,
    SINGLE_SHOT_COMMAND,
    SINGLE_SHOT_CONVERSION,
    SINGLE_SHOT_READ,
    PERIODIC_FETCH_COMMAND,
    PERIODIC_READ
  };

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
  Status _writeCommandNoDelay(uint16_t cmd, bool tracked);
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
  Status _readMeasurementRawNoDelay(RawSample& out, bool tracked, bool allowNoData);
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
  MeasurementPhase _measurementPhase = MeasurementPhase::IDLE;
  Status _lastMeasurementStatus = Status::Error(Err::MEASUREMENT_NOT_READY,
                                                "Measurement not ready");
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
