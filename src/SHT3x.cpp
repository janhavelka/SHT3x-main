/**
 * @file SHT3x.cpp
 * @brief SHT3x driver implementation.
 */

#include "SHT3x/SHT3x.h"

#include <limits>
#include <cmath>

namespace SHT3x {
namespace {

static constexpr size_t MAX_WRITE_LEN = 5;
static constexpr size_t MAX_READ_LEN = cmd::MEASUREMENT_DATA_LEN;
// A millisecond timestamp sampled when a wait starts can truncate almost a full
// millisecond, so a wait of N ms only guarantees more than (N - 1) ms of real
// time. Every settle wait therefore carries one extra millisecond on top of the
// datasheet figure, the same allowance estimateMeasurementTimeMs() applies.
static constexpr uint32_t CLOCK_QUANTIZATION_MS = 1;
static constexpr uint32_t RESET_SETTLE_MS = 2;  // tSR max 1.5 ms, rounded up
static constexpr uint32_t BREAK_SETTLE_MS = 1;  // Break processing takes 1 ms
static constexpr uint32_t RESET_DELAY_MS = RESET_SETTLE_MS + CLOCK_QUANTIZATION_MS;
static constexpr uint32_t BREAK_DELAY_MS = BREAK_SETTLE_MS + CLOCK_QUANTIZATION_MS;
static constexpr uint16_t MIN_COMMAND_DELAY_MS = 1;
static constexpr uint32_t ART_PERIOD_MS = 250;
static constexpr uint32_t MAX_I2C_TIMEOUT_MS = 60000;
static constexpr uint16_t MAX_COMMAND_DELAY_MS = 1000;
static constexpr uint32_t MAX_NOT_READY_TIMEOUT_MS = 600000;
static constexpr uint32_t MAX_PERIODIC_FETCH_MARGIN_MS = 60000;
static constexpr uint32_t MAX_RECOVER_BACKOFF_MS = 600000;
static constexpr uint16_t MAX_SINGLE_SHOT_MARGIN_MS = 1000;
static constexpr uint8_t VALID_TRANSPORT_CAPABILITIES =
    static_cast<uint8_t>(TransportCapability::READ_HEADER_NACK) |
    static_cast<uint8_t>(TransportCapability::TIMEOUT) |
    static_cast<uint8_t>(TransportCapability::BUS_ERROR);
static constexpr float ALERT_DEFAULT_MATCH_EPSILON = 0.001f;

// Reduced alert-limit packing: humidity in bits 15:9, temperature in bits 8:0.
static constexpr uint16_t ALERT_T_CODE_BITS = 9;
static constexpr uint16_t ALERT_RH_CODE_MAX = 127;   // 7 bits
static constexpr uint16_t ALERT_T_CODE_MAX = 511;    // 9 bits

struct AlertDefaultVector {
  float temperatureC;
  float humidityPct;
  uint16_t word;
};

// Sensirion's own alert-limit workbook (HT_AlertMode_BitConversion.xlsx) rounds
// to the nearest reduced RH7/T9 code. encodeAlertLimit() implements that rule, so
// three of the four published reset defaults fall out of the generic arithmetic.
//
// The fourth does not: the alert application note prints 79 %RH / 58 degC as
// 0xC92D, but 0xC92D decodes to 78.13 %RH and Sensirion's own workbook computes
// 0xCB2D for that pair. The two vendor artifacts disagree by one humidity code.
// The printed word describes the device's power-up state, so it wins here.
static constexpr AlertDefaultVector ALERT_APP_NOTE_DEFAULTS[] = {
    {58.0f, 79.0f, 0xC92D},
};

class ScopedOfflineI2cAllowance {
public:
  explicit ScopedOfflineI2cAllowance(bool& flag, bool allow) : _flag(flag), _old(flag) {
    _flag = allow;
  }

  ~ScopedOfflineI2cAllowance() {
    _flag = _old;
  }

  ScopedOfflineI2cAllowance(const ScopedOfflineI2cAllowance&) = delete;
  ScopedOfflineI2cAllowance& operator=(const ScopedOfflineI2cAllowance&) = delete;

private:
  bool& _flag;
  bool _old;
};

static uint32_t _nowMs(const Config& cfg) {
  return cfg.nowMs(cfg.timeUser);
}

static uint32_t _nowUs(const Config& cfg) {
  return cfg.nowUs(cfg.timeUser);
}

static uint32_t saturatingAddU32(uint32_t a, uint32_t b) {
  const uint32_t maxU32 = std::numeric_limits<uint32_t>::max();
  if (a > (maxU32 - b)) {
    return maxU32;
  }
  return static_cast<uint32_t>(a + b);
}

static CachedSettings defaultCachedSettings() {
  CachedSettings settings;
  return settings;
}

static bool isCloseAlertDefault(float value, float expected) {
  return std::fabs(value - expected) <= ALERT_DEFAULT_MATCH_EPSILON;
}

static bool alertAppNoteDefaultWord(float temperatureC, float humidityPct, uint16_t& word) {
  for (const auto& vector : ALERT_APP_NOTE_DEFAULTS) {
    if (isCloseAlertDefault(temperatureC, vector.temperatureC) &&
        isCloseAlertDefault(humidityPct, vector.humidityPct)) {
      word = vector.word;
      return true;
    }
  }
  return false;
}

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

static bool isValidHealthPolicy(HealthPolicy policy) {
  return policy == HealthPolicy::OBSERVE_ONLY || policy == HealthPolicy::LATCH_OFFLINE;
}

static Status initialMeasurementStatus() {
  return Status::Error(Err::MEASUREMENT_NOT_READY, "Measurement not ready");
}

static Status mapPresenceProbeFailure(const Status& st) {
  if (st.code == Err::I2C_NACK_ADDR) {
    return Status::Error(Err::DEVICE_NOT_FOUND, "Device address not acknowledged", st.detail);
  }
  return st;
}

static Status statusDiagnosticFailure(uint16_t raw) {
  if ((raw & cmd::STATUS_WRITE_CRC_ERROR) != 0U) {
    return Status::Error(Err::WRITE_CRC_ERROR, "Write checksum error");
  }
  if ((raw & cmd::STATUS_COMMAND_ERROR) != 0U) {
    return Status::Error(Err::COMMAND_FAILED, "Command rejected");
  }
  return Status::Ok();
}

static Status stableStatus(const Status& st) {
  if (st.ok()) {
    return Status::Ok();
  }

  const char* message = "Transport error";
  switch (st.code) {
    case Err::I2C_ERROR: message = "I2C error"; break;
    case Err::I2C_NACK_ADDR: message = "I2C address NACK"; break;
    case Err::I2C_NACK_DATA: message = "I2C data NACK"; break;
    case Err::I2C_NACK_READ: message = "I2C read-header NACK"; break;
    case Err::I2C_TIMEOUT: message = "I2C timeout"; break;
    case Err::I2C_BUS: message = "I2C bus error"; break;
    case Err::TIMEOUT: message = "Operation timeout"; break;
    default: break;
  }
  return Status::Error(st.code, message, st.detail);
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

Status SHT3x::bind(const Config& config) {
  if (_jobActive()) {
    return Status::Error(Err::BUSY, "Cancel active job before rebinding");
  }

  // Copy before validation so bind(device.getConfig()) is alias-safe.
  const Config candidate = config;
  if (candidate.i2cWrite == nullptr || candidate.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C callbacks not set");
  }
  if (candidate.i2cTimeoutMs == 0) {
    return Status::Error(Err::INVALID_CONFIG, "I2C timeout must be > 0");
  }
  if (candidate.i2cTimeoutMs > MAX_I2C_TIMEOUT_MS) {
    return Status::Error(Err::INVALID_CONFIG, "I2C timeout too large");
  }
  if (candidate.i2cAddress != cmd::I2C_ADDR_LOW &&
      candidate.i2cAddress != cmd::I2C_ADDR_HIGH) {
    return Status::Error(Err::INVALID_CONFIG, "Invalid I2C address");
  }
  if (!isValidRepeatability(candidate.repeatability) ||
      !isValidClockStretching(candidate.clockStretching) ||
      !isValidPeriodicRate(candidate.periodicRate) ||
      !isValidMode(candidate.mode) ||
      !isValidHealthPolicy(candidate.healthPolicy)) {
    return Status::Error(Err::INVALID_CONFIG, "Invalid configuration value");
  }
  if ((static_cast<uint8_t>(candidate.transportCapabilities) &
       static_cast<uint8_t>(~VALID_TRANSPORT_CAPABILITIES)) != 0U) {
    return Status::Error(Err::INVALID_CONFIG, "Invalid transport capabilities");
  }
  if (candidate.commandDelayMs > MAX_COMMAND_DELAY_MS) {
    return Status::Error(Err::INVALID_CONFIG, "Command delay too large");
  }
  if (candidate.notReadyTimeoutMs > MAX_NOT_READY_TIMEOUT_MS) {
    return Status::Error(Err::INVALID_CONFIG, "Not-ready timeout too large");
  }
  if (candidate.periodicFetchMarginMs > MAX_PERIODIC_FETCH_MARGIN_MS) {
    return Status::Error(Err::INVALID_CONFIG, "Periodic fetch margin too large");
  }
  if (candidate.recoverBackoffMs > MAX_RECOVER_BACKOFF_MS) {
    return Status::Error(Err::INVALID_CONFIG, "Recover backoff too large");
  }
  if (candidate.singleShotMeasurementMarginMs > MAX_SINGLE_SHOT_MARGIN_MS) {
    return Status::Error(Err::INVALID_CONFIG, "Single-shot margin too large");
  }
  if (candidate.nowMs == nullptr || candidate.nowUs == nullptr ||
      candidate.cooperativeYield == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "Timing callbacks not set");
  }

  _config = Config{};
  _initialized = false;
  _driverState = DriverState::UNINIT;

  _lastOkMs = 0;
  _lastErrorMs = 0;
  _lastBusActivityMs = 0;
  _lastError = Status::Ok();
  _consecutiveFailures = 0;
  _totalFailures = 0;
  _totalSuccess = 0;
  _transportFailures = 0;
  _transportSuccess = 0;
  _protocolFailures = 0;
  _totalNotReady = 0;
  _totalInferredNotReady = 0;
  _allowOfflineI2c = false;

  _measurementRequested = false;
  _measurementReady = false;
  _hasSample = false;
  _measurementPhase = JobPhase::IDLE;
  _jobType = JobType::NONE;
  _jobRequestId = 0;
  _nextJobId = 1;
  _jobDeadlineMs = 0;
  _jobHasDeadline = false;
  _jobEffect = JobEffect::NONE;
  _jobWakeMs = 0;
  _lastMeasurementStatus = initialMeasurementStatus();
  _measurementReadyMs = 0;
  _periodicStartMs = 0;
  _lastFetchMs = 0;
  _lastFetchValid = false;
  _periodMs = 0;
  _sampleTimestampMs = 0;
  _missedSamples = 0;
  _missedRemainderMs = 0;
  _notReadyStartMs = 0;
  _notReadyStartValid = false;
  _notReadyCount = 0;
  _lastRecoverMs = 0;
  _lastRecoverValid = false;
  _rawSample = RawSample{};
  _compSample = CompensatedSample{};
  _mode = Mode::SINGLE_SHOT;
  _periodicActive = false;
  _hardwareStateValid = false;
  _lastCommandUs = 0;
  _lastCommandMs = 0;
  _lastCommandValid = false;
  _cachedSettings = defaultCachedSettings();
  _hasCachedSettings = false;

  _config = candidate;
  if (_config.offlineThreshold == 0) {
    _config.offlineThreshold = 1;
  }
  if (_config.commandDelayMs < MIN_COMMAND_DELAY_MS) {
    _config.commandDelayMs = MIN_COMMAND_DELAY_MS;
  }

  // Passive binding cannot claim or create a periodic/ART hardware state.
  _config.mode = Mode::SINGLE_SHOT;
  _mode = Mode::SINGLE_SHOT;
  _initialized = true;
  _driverState = DriverState::READY;
  _syncCacheFromConfig();
  _hasCachedSettings = true;
  return Status::Ok();
}

Status SHT3x::begin(const Config& config) {
  const Mode requestedMode = config.mode;
  Status st = bind(config);
  if (!st.ok()) {
    return st;
  }

  // Best-effort: stop any stale periodic mode and reset sensor.
  // If the MCU rebooted but the sensor did not, the sensor may still be in
  // periodic mode, which rejects most commands.  BREAK exits periodic mode;
  // soft reset then brings the sensor to a known idle state.
  // Either accepted-and-settled command establishes an idle acquisition
  // baseline. Both are attempted so a transient failure in one path does not
  // hide a successful reconciliation by the other.
  const Status breakStatus = _writeCommand(cmd::CMD_BREAK, false);
  const Status breakWait = _waitMs(BREAK_DELAY_MS);
  const bool breakEstablished = breakStatus.ok() && breakWait.ok();

  const Status resetStatus = _writeCommand(cmd::CMD_SOFT_RESET, false);
  const Status resetWait = _waitMs(RESET_DELAY_MS);
  const bool resetEstablished = resetStatus.ok() && resetWait.ok();
  const bool acquisitionStopped = breakEstablished || resetEstablished;

  Status reconciliationFailure = resetStatus;
  if (resetStatus.ok() && !resetWait.ok()) {
    reconciliationFailure = resetWait;
  } else if (resetStatus.ok() && resetWait.ok()) {
    reconciliationFailure = breakStatus.ok() ? breakWait : breakStatus;
  }

  uint16_t statusRaw = 0;
  st = _readStatusRaw(statusRaw, false);
  if (!st.ok()) {
    const Status failure = mapPresenceProbeFailure(st);
    end();
    return failure;
  }
  st = statusDiagnosticFailure(statusRaw);
  if (!st.ok()) {
    end();
    return st;
  }
  if (!acquisitionStopped) {
    end();
    return reconciliationFailure;
  }

  _mode = requestedMode;
  _config.mode = requestedMode;
  if (_mode == Mode::PERIODIC) {
    st = _enterPeriodic(_config.periodicRate, _config.repeatability, false);
    if (!st.ok()) {
      end();
      return st;
    }
  } else if (_mode == Mode::ART) {
    st = _enterPeriodic(_config.periodicRate, _config.repeatability, true);
    if (!st.ok()) {
      end();
      return st;
    }
  }

  _hardwareStateValid = acquisitionStopped;
  _syncCacheFromConfig();
  _hasCachedSettings = true;

  return Status::Ok();
}

void SHT3x::tick(uint32_t nowMs) {
  PollJobResult result;
  (void)pollJob(nowMs, 1, result);
}

Status SHT3x::pollJob(uint32_t nowMs, uint8_t maxInstructions, PollJobResult& result) {
  result = PollJobResult{};

  if (!_initialized) {
    result.status = Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
    return result.status;
  }
  if (!_jobActive()) {
    result.status = _measurementReady ? Status::Ok() : measurementStatus();
    return result.status;
  }

  result.active = true;
  result.requestId = _jobRequestId;
  result.type = _jobType;
  result.phase = _measurementPhase;
  result.outcome = JobOutcome::ACTIVE;
  result.effect = _jobEffect;

  if (_jobHasDeadline && _timeElapsed(nowMs, _jobDeadlineMs)) {
    return cancelJob(CancelReason::DEADLINE_EXPIRED, result);
  }

  auto recordFailure = [this, &result](Status st) -> Status {
    const JobType type = _jobType;
    const JobPhase phase = _measurementPhase;
    const bool timedOut = st.code == Err::TIMEOUT || st.code == Err::I2C_TIMEOUT;
    const bool ambiguous = timedOut || st.code == Err::I2C_ERROR || st.code == Err::I2C_BUS;
    const bool consumedInvalidMeasurement =
        type == JobType::MEASUREMENT && st.code == Err::CRC_MISMATCH &&
        (phase == JobPhase::SINGLE_SHOT_READ || phase == JobPhase::PERIODIC_READ);
    const JobEffect effect = consumedInvalidMeasurement
                                 ? JobEffect::NONE
                                 : _effectForPhase(phase, ambiguous);

    if (type == JobType::MEASUREMENT) {
      _lastMeasurementStatus = isTransportError(st.code) ? stableStatus(st) : st;
      _measurementReady = false;
      _measurementRequested = false;
    }

    result.status = st;
    result.active = false;
    result.terminal = true;
    result.requestId = _jobRequestId;
    result.type = type;
    result.phase = phase;
    result.outcome = timedOut ? JobOutcome::TIMED_OUT : JobOutcome::FAILED;
    result.effect = effect;
    if (effect == JobEffect::DEVICE_STATE_INDETERMINATE ||
        (type == JobType::ENSURE_IDLE && effect != JobEffect::NONE)) {
      _hardwareStateValid = false;
    }
    _clearJobState();
    return st;
  };

  auto recordProgress = [this, &result](const char* message) -> Status {
    const Status st = Status::Error(Err::IN_PROGRESS, message);
    if (_jobType == JobType::MEASUREMENT) {
      _lastMeasurementStatus = st;
    }
    result.status = st;
    result.active = true;
    result.outcome = JobOutcome::ACTIVE;
    result.effect = _jobEffect;
    return st;
  };

  auto recordSample = [this, &result](const RawSample& sample,
                                      uint32_t completedMs) -> Status {
    const uint32_t requestId = _jobRequestId;
    const JobPhase phase = _measurementPhase;
    _rawSample = sample;
    _compSample.tempC_x100 = convertTemperatureC_x100(_rawSample.rawTemperature);
    _compSample.humidityPct_x100 = convertHumidityPct_x100(_rawSample.rawHumidity);
    _sampleTimestampMs = completedMs;
    _measurementReady = true;
    _hasSample = true;
    _measurementRequested = false;
    _lastMeasurementStatus = Status::Ok();

    result.completed = true;
    result.active = false;
    result.terminal = true;
    result.requestId = requestId;
    result.type = JobType::MEASUREMENT;
    result.phase = phase;
    result.outcome = JobOutcome::SUCCEEDED;
    result.effect = JobEffect::NONE;
    result.status = Status::Ok();
    _clearJobState();
    return result.status;
  };

  auto recordEnsureSuccess = [this, &result]() -> Status {
    const uint32_t requestId = _jobRequestId;
    const JobPhase phase = _measurementPhase;
    _setSafeBaseline();
    _hardwareStateValid = true;
    result.status = Status::Ok();
    result.active = false;
    result.terminal = true;
    result.requestId = requestId;
    result.type = JobType::ENSURE_IDLE;
    result.phase = phase;
    result.outcome = JobOutcome::SUCCEEDED;
    result.effect = JobEffect::DEVICE_STATE_CHANGED;
    _clearJobState();
    return result.status;
  };

  if (_jobType == JobType::MEASUREMENT &&
      _config.healthPolicy == HealthPolicy::LATCH_OFFLINE &&
      _driverState == DriverState::OFFLINE) {
    return recordFailure(_offlineStatus());
  }
  if (_jobType == JobType::MEASUREMENT &&
      (_measurementPhase == JobPhase::PERIODIC_FETCH_COMMAND ||
       _measurementPhase == JobPhase::PERIODIC_READ) &&
      _notReadyStartValid &&
      _durationElapsed(_nowMs(_config), _notReadyStartMs,
                       _notReadyWindowMs())) {
    const Status timeout = Status::Error(
        Err::TIMEOUT, "Periodic data not ready before timeout");
    _hardwareStateValid = false;
    (void)_completeLogicalOperation(timeout);
    return recordFailure(timeout);
  }
  if (maxInstructions == 0) {
    return recordProgress("Poll budget exhausted");
  }

  auto commandDelayOpen = [this]() -> bool { return _commandDelayElapsed(); };

  switch (_measurementPhase) {
    case JobPhase::ENSURE_BREAK_COMMAND: {
      if (!commandDelayOpen()) {
        return recordProgress("Command delay pending");
      }
      Status st = _writeCommandNoDelay(cmd::CMD_BREAK, true, false);
      result.instructionsUsed = 1;
      if (!st.ok()) {
        return recordFailure(st);
      }
      _periodicActive = false;
      _mode = Mode::SINGLE_SHOT;
      _config.mode = Mode::SINGLE_SHOT;
      _jobEffect = JobEffect::DEVICE_STATE_CHANGED;
      _jobWakeMs = _nowMs(_config) + BREAK_DELAY_MS;
      _measurementPhase = JobPhase::ENSURE_BREAK_WAIT;
      return recordProgress("Break settle pending");
    }

    case JobPhase::ENSURE_BREAK_WAIT:
      if (!_timeElapsed(nowMs, _jobWakeMs)) {
        return recordProgress("Break settle pending");
      }
      _measurementPhase = JobPhase::ENSURE_RESET_COMMAND;
      return recordProgress("Soft reset pending");

    case JobPhase::ENSURE_RESET_COMMAND: {
      if (!commandDelayOpen()) {
        return recordProgress("Command delay pending");
      }
      Status st = _writeCommandNoDelay(cmd::CMD_SOFT_RESET, true, false);
      result.instructionsUsed = 1;
      if (!st.ok()) {
        return recordFailure(st);
      }
      _measurementRequested = false;
      _measurementReady = false;
      _hasSample = false;
      _lastMeasurementStatus = initialMeasurementStatus();
      _jobEffect = JobEffect::DEVICE_STATE_CHANGED;
      _jobWakeMs = _nowMs(_config) + RESET_DELAY_MS;
      _measurementPhase = JobPhase::ENSURE_RESET_WAIT;
      return recordProgress("Reset settle pending");
    }

    case JobPhase::ENSURE_RESET_WAIT:
      if (!_timeElapsed(nowMs, _jobWakeMs)) {
        return recordProgress("Reset settle pending");
      }
      _measurementPhase = JobPhase::ENSURE_STATUS_COMMAND;
      return recordProgress("Status verification pending");

    case JobPhase::ENSURE_STATUS_COMMAND: {
      if (!commandDelayOpen()) {
        return recordProgress("Command delay pending");
      }
      Status st = _writeCommandNoDelay(cmd::CMD_READ_STATUS, true, false);
      result.instructionsUsed = 1;
      if (!st.ok()) {
        return recordFailure(st);
      }
      _measurementPhase = JobPhase::ENSURE_STATUS_READ;
      return recordProgress("Status read pending");
    }

    case JobPhase::ENSURE_STATUS_READ: {
      if (!commandDelayOpen()) {
        return recordProgress("Command delay pending");
      }
      uint8_t buf[cmd::STATUS_DATA_LEN] = {};
      Status st = _readOnly(buf, sizeof(buf), true, false, false);
      result.instructionsUsed = 1;
      if (!st.ok()) {
        return recordFailure(st);
      }
      if (_crc8(buf, 2) != buf[2]) {
        const Status failure = Status::Error(Err::CRC_MISMATCH,
                                             "CRC mismatch (status)");
        _recordProtocolFailure(failure, true);
        return recordFailure(failure);
      }
      const uint16_t statusRaw =
          static_cast<uint16_t>((static_cast<uint16_t>(buf[0]) << 8) | buf[1]);
      st = statusDiagnosticFailure(statusRaw);
      if (!st.ok()) {
        _recordProtocolFailure(st, true);
        return recordFailure(st);
      }
      (void)_completeLogicalOperation(Status::Ok());
      return recordEnsureSuccess();
    }

    case JobPhase::SINGLE_SHOT_COMMAND: {
      if (!commandDelayOpen()) {
        return recordProgress("Command delay pending");
      }
      if (_periodicActive) {
        return recordFailure(Status::Error(Err::BUSY, "Periodic mode active"));
      }
      const uint16_t command = _commandForSingleShot(_config.repeatability,
                                                     _config.clockStretching);
      if (command == 0) {
        return recordFailure(Status::Error(Err::INVALID_PARAM,
                                           "Invalid single-shot configuration"));
      }
      Status st = _writeCommandNoDelay(command, true, false);
      result.instructionsUsed = 1;
      if (!st.ok()) {
        return recordFailure(st);
      }
      _jobEffect = JobEffect::RESULT_MAY_BE_PENDING;
      _measurementPhase = JobPhase::SINGLE_SHOT_CONVERSION;
      _measurementReadyMs = _nowMs(_config) + estimateMeasurementTimeMs();
      return recordProgress("Conversion pending");
    }

    case JobPhase::SINGLE_SHOT_CONVERSION:
      if (!_timeElapsed(nowMs, _measurementReadyMs)) {
        return recordProgress("Conversion pending");
      }
      _measurementPhase = JobPhase::SINGLE_SHOT_READ;
      break;

    case JobPhase::PERIODIC_FETCH_COMMAND:
      if (!_periodicActive) {
        return recordFailure(Status::Error(Err::INVALID_PARAM,
                                           "Periodic mode not active"));
      }
      if (!_timeElapsed(nowMs, _measurementReadyMs)) {
        return recordProgress("Periodic fetch pending");
      }
      if (!commandDelayOpen()) {
        return recordProgress("Command delay pending");
      }
      {
        Status st = _writeCommandNoDelay(cmd::CMD_FETCH_DATA, true, false);
        result.instructionsUsed = 1;
        if (!st.ok()) {
          return recordFailure(st);
        }
      }
      _measurementPhase = JobPhase::PERIODIC_READ;
      return recordProgress("Periodic read pending");

    case JobPhase::SINGLE_SHOT_READ:
    case JobPhase::PERIODIC_READ:
      break;

    case JobPhase::IDLE:
    default:
      return recordFailure(Status::Error(Err::INVALID_PARAM,
                                         "Invalid poll job phase"));
  }

  if (result.instructionsUsed >= maxInstructions) {
    return recordProgress("Poll budget exhausted");
  }
  if (!commandDelayOpen()) {
    return recordProgress("Command delay pending");
  }

  if (_measurementPhase == JobPhase::SINGLE_SHOT_READ) {
    RawSample sample;
    Status st = _readMeasurementRawNoDelay(sample, true, false);
    result.instructionsUsed++;
    if (!st.ok()) {
      return recordFailure(st);
    }
    const uint32_t completedMs = _nowMs(_config);
    return recordSample(sample, completedMs);
  }

  RawSample sample;
  Status st = _readMeasurementRawNoDelay(sample, true, true);
  result.instructionsUsed++;
  const uint32_t readCompletedMs = _nowMs(_config);
  if (!st.ok()) {
    if (st.code == Err::MEASUREMENT_NOT_READY) {
      if (!_notReadyStartValid) {
        _notReadyStartMs = readCompletedMs;
        _notReadyStartValid = true;
      }
      if (_notReadyCount < std::numeric_limits<uint32_t>::max()) {
        _notReadyCount++;
      }
      _measurementPhase = JobPhase::PERIODIC_FETCH_COMMAND;
      _measurementReadyMs = _periodicRetryMs(readCompletedMs);
      return recordProgress("Periodic sample not ready");
    }
    return recordFailure(st);
  }

  _notReadyStartMs = 0;
  _notReadyStartValid = false;
  _notReadyCount = 0;

  if (_lastFetchValid && _periodMs > 0) {
    uint32_t elapsed = readCompletedMs - _lastFetchMs;
    if (elapsed > (std::numeric_limits<uint32_t>::max() / 2U)) {
      elapsed = 0;
      _missedRemainderMs = 0;
    }
    const uint32_t total = saturatingAddU32(_missedRemainderMs, elapsed);
    const uint32_t produced = total / _periodMs;
    _missedRemainderMs = total % _periodMs;
    if (produced > 1U) {
      _missedSamples = saturatingAddU32(_missedSamples, produced - 1U);
    }
  }
  _lastFetchMs = readCompletedMs;
  _lastFetchValid = true;
  return recordSample(sample, readCompletedMs);
}

void SHT3x::end() {
  _clearJobState();
  _measurementRequested = false;
  _measurementReady = false;
  _hasSample = false;
  _measurementPhase = JobPhase::IDLE;
  _lastMeasurementStatus = initialMeasurementStatus();
  _measurementReadyMs = 0;
  _periodicActive = false;
  // _periodicActive and _mode describe the same fact and are written together
  // everywhere else; clearing only the flag here was the one reachable skew.
  _mode = Mode::SINGLE_SHOT;
  _config.mode = Mode::SINGLE_SHOT;
  _periodicStartMs = 0;
  _lastFetchMs = 0;
  _lastFetchValid = false;
  _periodMs = 0;
  _sampleTimestampMs = 0;
  _missedSamples = 0;
  _missedRemainderMs = 0;
  _notReadyStartMs = 0;
  _notReadyStartValid = false;
  _notReadyCount = 0;
  _lastRecoverMs = 0;
  _lastRecoverValid = false;
  _initialized = false;
  _driverState = DriverState::UNINIT;
  _lastCommandValid = false;
  _lastCommandMs = 0;
  _lastCommandUs = 0;
  _hardwareStateValid = false;
}

Status SHT3x::probe() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_singleShotMeasurementPending()) {
    return Status::Error(Err::BUSY, "Cooperative job in progress");
  }
  if (_periodicActive) {
    return Status::Error(Err::BUSY, "Stop periodic mode before probing");
  }

  uint16_t statusRaw = 0;
  Status st = _readStatusRaw(statusRaw, false);
  if (!st.ok()) {
    return mapPresenceProbeFailure(st);
  }

  return Status::Ok();
}

Status SHT3x::recover() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_jobActive()) {
    return Status::Error(Err::BUSY, "Cooperative job in progress");
  }
  Status st = _admitRecoveryAttempt();
  if (!st.ok()) {
    return st;
  }
  st = _performRecoveryLadder();
  if (!st.ok()) {
    _hardwareStateValid = false;
    return st;
  }
  _setSafeBaseline();
  _hardwareStateValid = true;
  return Status::Ok();
}

Status SHT3x::resetToDefaults() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_jobActive()) {
    return Status::Error(Err::BUSY, "Cooperative job in progress");
  }
  Status st = _admitRecoveryAttempt();
  if (!st.ok()) {
    return st;
  }
  st = _performRecoveryLadder();
  if (!st.ok()) {
    _hardwareStateValid = false;
    return st;
  }
  _setSafeBaseline();
  st = softReset();
  if (!st.ok()) {
    _hardwareStateValid = false;
    return st;
  }
  _setDefaultsToConfigAndCache();
  _hardwareStateValid = true;
  return Status::Ok();
}

Status SHT3x::resetAndRestore() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_jobActive()) {
    return Status::Error(Err::BUSY, "Cooperative job in progress");
  }
  if (!_hasCachedSettings) {
    return Status::Error(Err::INVALID_PARAM, "No cached settings");
  }
  Status st = _admitRecoveryAttempt();
  if (!st.ok()) {
    return st;
  }
  st = _performRecoveryLadder();
  if (!st.ok()) {
    _hardwareStateValid = false;
    return st;
  }
  _setSafeBaseline();
  st = _applyCachedSettingsAfterReset();
  if (!st.ok()) {
    _hardwareStateValid = false;
    return st;
  }
  _hardwareStateValid = true;
  return Status::Ok();
}

Status SHT3x::requestMeasurement() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  JobRequest request;
  request.requestId = _allocateJobId();
  return requestMeasurement(request);
}

Status SHT3x::requestMeasurement(const JobRequest& request) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (request.requestId == 0) {
    return Status::Error(Err::INVALID_PARAM, "Job request ID must be nonzero");
  }
  if (_config.healthPolicy == HealthPolicy::LATCH_OFFLINE &&
      _driverState == DriverState::OFFLINE) {
    _lastMeasurementStatus = _offlineStatus();
    return _lastMeasurementStatus;
  }
  if (_jobActive()) {
    _lastMeasurementStatus = Status::Error(Err::BUSY, "Cooperative job in progress");
    return _lastMeasurementStatus;
  }

  if (_mode == Mode::SINGLE_SHOT) {
    const uint16_t command = _commandForSingleShot(_config.repeatability,
                                                   _config.clockStretching);
    if (command == 0) {
      _lastMeasurementStatus =
          Status::Error(Err::INVALID_PARAM, "Invalid single-shot configuration");
      return _lastMeasurementStatus;
    }

    _measurementReady = false;
    _measurementRequested = true;
    _measurementPhase = JobPhase::SINGLE_SHOT_COMMAND;
    _measurementReadyMs = _nowMs(_config);
    _jobType = JobType::MEASUREMENT;
    _jobRequestId = request.requestId;
    _jobDeadlineMs = request.deadlineMs;
    _jobHasDeadline = request.hasDeadline;
    _jobEffect = JobEffect::NONE;
    _lastMeasurementStatus = Status::Error(Err::IN_PROGRESS, "Measurement scheduled");

    return _lastMeasurementStatus;
  }

  if (_mode == Mode::PERIODIC || _mode == Mode::ART) {
    if (!_periodicActive) {
      _lastMeasurementStatus = Status::Error(Err::INVALID_PARAM, "Periodic mode not active");
      return _lastMeasurementStatus;
    }

    const uint32_t now = _nowMs(_config);
    uint32_t readyMs = _periodicReadyMs(now);

    _measurementReady = false;
    _measurementRequested = true;
    _measurementPhase = JobPhase::PERIODIC_FETCH_COMMAND;
    _measurementReadyMs = readyMs;
    _jobType = JobType::MEASUREMENT;
    _jobRequestId = request.requestId;
    _jobDeadlineMs = request.deadlineMs;
    _jobHasDeadline = request.hasDeadline;
    _jobEffect = JobEffect::NONE;
    _lastMeasurementStatus = Status::Error(Err::IN_PROGRESS, "Measurement scheduled");

    return _lastMeasurementStatus;
  }

  _lastMeasurementStatus = Status::Error(Err::INVALID_PARAM, "Invalid mode");
  return _lastMeasurementStatus;
}

Status SHT3x::requestEnsureIdle(const JobRequest& request) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (request.requestId == 0) {
    return Status::Error(Err::INVALID_PARAM, "Job request ID must be nonzero");
  }
  if (_jobActive()) {
    return Status::Error(Err::BUSY, "Cooperative job in progress");
  }

  _measurementPhase = JobPhase::ENSURE_BREAK_COMMAND;
  _jobType = JobType::ENSURE_IDLE;
  _jobRequestId = request.requestId;
  _jobDeadlineMs = request.deadlineMs;
  _jobHasDeadline = request.hasDeadline;
  _jobEffect = JobEffect::NONE;
  _jobWakeMs = 0;
  return Status::Error(Err::IN_PROGRESS, "Ensure-idle scheduled");
}

Status SHT3x::cancelJob(CancelReason reason, PollJobResult& result) {
  result = PollJobResult{};
  if (!_initialized) {
    result.status = Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
    return result.status;
  }
  if (!_jobActive()) {
    result.status = Status::Error(Err::MEASUREMENT_NOT_READY, "No poll job active");
    return result.status;
  }
  if (reason != CancelReason::REQUESTED &&
      reason != CancelReason::DEADLINE_EXPIRED) {
    result.status = Status::Error(Err::INVALID_PARAM, "Invalid cancel reason");
    result.active = true;
    result.requestId = _jobRequestId;
    result.type = _jobType;
    result.phase = _measurementPhase;
    result.outcome = JobOutcome::ACTIVE;
    result.effect = _jobEffect;
    return result.status;
  }

  const JobType type = _jobType;
  const JobPhase phase = _measurementPhase;
  const JobEffect effect = _effectForPhase(phase, false);
  const bool timedOut = reason == CancelReason::DEADLINE_EXPIRED;
  const Status st = timedOut
      ? Status::Error(Err::TIMEOUT, "Job deadline expired")
      : Status::Error(Err::CANCELLED, "Job cancelled");

  if (type == JobType::MEASUREMENT) {
    _measurementRequested = false;
    _measurementReady = false;
    _lastMeasurementStatus = st;
  }
  if (effect == JobEffect::DEVICE_STATE_INDETERMINATE ||
      (type == JobType::ENSURE_IDLE && effect != JobEffect::NONE)) {
    _hardwareStateValid = false;
  }

  result.status = st;
  result.active = false;
  result.terminal = true;
  result.requestId = _jobRequestId;
  result.type = type;
  result.phase = phase;
  result.outcome = timedOut ? JobOutcome::TIMED_OUT : JobOutcome::CANCELLED;
  result.effect = effect;
  _clearJobState();
  return st;
}

Status SHT3x::cancelMeasurement() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_jobType != JobType::MEASUREMENT) {
    return Status::Error(Err::MEASUREMENT_NOT_READY, "No measurement job active");
  }
  PollJobResult result;
  return cancelJob(CancelReason::REQUESTED, result);
}

Status SHT3x::measurementStatus() const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_measurementReady) {
    return Status::Ok();
  }
  return _lastMeasurementStatus;
}

Status SHT3x::getMeasurement(Measurement& out) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (!_measurementReady) {
    return measurementStatus();
  }

  out.temperatureC = convertTemperatureC(_rawSample.rawTemperature);
  out.humidityPct = convertHumidityPct(_rawSample.rawHumidity);

  _measurementReady = false;
  _lastMeasurementStatus = initialMeasurementStatus();
  return Status::Ok();
}

Status SHT3x::getRawSample(RawSample& out) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (!_hasSample) {
    return measurementStatus();
  }

  out = _rawSample;
  return Status::Ok();
}

Status SHT3x::getCompensatedSample(CompensatedSample& out) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (!_hasSample) {
    return measurementStatus();
  }

  out = _compSample;
  return Status::Ok();
}

Status SHT3x::getMeasurementMilli(MeasurementMilli& out) const {
  return getMeasurementMilli(out, MilliRounding::NEAREST);
}

Status SHT3x::getMeasurementMilli(MeasurementMilli& out,
                                  MilliRounding rounding) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (!_hasSample) {
    return measurementStatus();
  }

  if (rounding != MilliRounding::NEAREST &&
      rounding != MilliRounding::TRUNCATE_SCALED) {
    return Status::Error(Err::INVALID_PARAM, "Invalid milli rounding");
  }
  out.temperatureMilliCelsius =
      convertTemperatureMilliCelsius(_rawSample.rawTemperature, rounding);
  out.humidityMilliPercent =
      convertHumidityMilliPercent(_rawSample.rawHumidity, rounding);
  return Status::Ok();
}

Status SHT3x::setMode(Mode mode) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_singleShotMeasurementPending()) {
    return Status::Error(Err::BUSY, "Measurement in progress");
  }
  if (!isValidMode(mode)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid mode");
  }

  if (mode == _mode) {
    // No sensor command is needed, but the explicit request still commits the
    // restore plan: a recovery can leave _mode at SINGLE_SHOT while the cache
    // still names the old acquisition mode.
    _cachedSettings.mode = mode;
    _hasCachedSettings = true;
    return Status::Ok();
  }

  if (mode == Mode::SINGLE_SHOT) {
    Status st = stopPeriodic();
    if (!st.ok()) {
      return st;
    }
    _mode = Mode::SINGLE_SHOT;
    _config.mode = Mode::SINGLE_SHOT;
    _cachedSettings.mode = Mode::SINGLE_SHOT;
    _hasCachedSettings = true;
    return Status::Ok();
  }

  if (mode == Mode::PERIODIC) {
    return startPeriodic(_config.periodicRate, _config.repeatability);
  }

  return startArt();
}

Status SHT3x::getMode(Mode& out) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  out = _mode;
  return Status::Ok();
}

Status SHT3x::getSettings(SettingsSnapshot& out) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }

  out.initialized = _initialized;
  out.state = _driverState;
  out.i2cAddress = _config.i2cAddress;
  out.i2cTimeoutMs = _config.i2cTimeoutMs;
  out.offlineThreshold = _config.offlineThreshold;
  out.healthPolicy = _config.healthPolicy;
  out.hasNowMsHook = (_config.nowMs != nullptr);
  out.mode = _mode;
  out.repeatability = _config.repeatability;
  out.periodicRate = _config.periodicRate;
  out.clockStretching = _config.clockStretching;
  out.periodicActive = _periodicActive;
  out.measurementPending = _measurementRequested && !_measurementReady;
  out.measurementReady = _measurementReady;
  out.hasSample = _hasSample;
  out.lastMeasurementStatus = measurementStatus();
  out.measurementReadyMs = (_measurementRequested || _measurementReady)
                               ? _measurementReadyMs
                               : 0;
  out.sampleTimestampMs = _sampleTimestampMs;
  out.missedSamples = _missedSamples;
  out.hardwareStateValid = _hardwareStateValid;
  out.status = StatusRegister{};
  out.statusValid = false;
  out.statusReadStatus = Status::Error(Err::UNSUPPORTED, "Status not read");
  return Status::Ok();
}

Status SHT3x::readSettings(SettingsSnapshot& out) {
  Status st = getSettings(out);
  if (!st.ok()) {
    return st;
  }

  if (_config.healthPolicy == HealthPolicy::LATCH_OFFLINE &&
      _driverState == DriverState::OFFLINE) {
    out.statusReadStatus = _offlineStatus();
    return out.statusReadStatus;
  }
  if (_jobActive() || _periodicActive) {
    out.statusReadStatus = Status::Error(
        Err::BUSY, _jobActive() ? "Cooperative job in progress"
                               : "Stop periodic mode before reading status");
    out.statusValid = false;
    return Status::Ok();
  }

  const Status stStatus = readStatus(out.status);
  out.statusReadStatus = stStatus;
  out.statusValid = stStatus.ok();
  return stStatus;
}

Status SHT3x::writeCommand(uint16_t command) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_singleShotMeasurementPending()) {
    return Status::Error(Err::BUSY, "Measurement in progress");
  }

  Status st = _writeCommand(command, true);
  _hardwareStateValid = false;
  return st;
}

Status SHT3x::writeCommandWithData(uint16_t command, uint16_t data) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_singleShotMeasurementPending()) {
    return Status::Error(Err::BUSY, "Measurement in progress");
  }

  Status st = _writeCommandWithData(command, data, true);
  _hardwareStateValid = false;
  return st;
}

Status SHT3x::readCommand(uint16_t command, uint8_t* out, size_t len,
                          bool allowNoData) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (out == nullptr || len == 0 || len > MAX_READ_LEN) {
    return Status::Error(Err::INVALID_PARAM, "Invalid read buffer");
  }
  if (_singleShotMeasurementPending()) {
    return Status::Error(Err::BUSY, "Measurement in progress");
  }

  Status st = _writeCommand(command, true, false);
  if (!st.ok()) {
    _hardwareStateValid = false;
    return st;
  }

  st = _readAfterCommand(out, len, true, allowNoData);
  _hardwareStateValid = false;
  return st;
}

Status SHT3x::setRepeatability(Repeatability rep) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_singleShotMeasurementPending()) {
    return Status::Error(Err::BUSY, "Measurement in progress");
  }
  if (!isValidRepeatability(rep)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid repeatability");
  }

  if (_mode == Mode::PERIODIC) {
    Status st = startPeriodic(_config.periodicRate, rep);
    if (st.ok()) {
      _cachedSettings.repeatability = rep;
      _hasCachedSettings = true;
    }
    return st;
  }
  // ART and SINGLE_SHOT both apply the setting locally with zero I2C: ART's
  // fixed vendor command encodes no repeatability, and single-shot picks its
  // command word at request time.
  _config.repeatability = rep;
  _cachedSettings.repeatability = rep;
  _hasCachedSettings = true;
  return Status::Ok();
}

Status SHT3x::getRepeatability(Repeatability& out) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  out = _config.repeatability;
  return Status::Ok();
}

Status SHT3x::setClockStretching(ClockStretching stretch) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_singleShotMeasurementPending()) {
    return Status::Error(Err::BUSY, "Measurement in progress");
  }
  if (!isValidClockStretching(stretch)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid clock stretching");
  }

  _config.clockStretching = stretch;
  _cachedSettings.clockStretching = stretch;
  _hasCachedSettings = true;
  return Status::Ok();
}

Status SHT3x::getClockStretching(ClockStretching& out) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  out = _config.clockStretching;
  return Status::Ok();
}

Status SHT3x::setPeriodicRate(PeriodicRate rate) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_singleShotMeasurementPending()) {
    return Status::Error(Err::BUSY, "Measurement in progress");
  }
  if (!isValidPeriodicRate(rate)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid periodic rate");
  }

  if (_mode == Mode::PERIODIC) {
    Status st = startPeriodic(rate, _config.repeatability);
    if (st.ok()) {
      _cachedSettings.periodicRate = rate;
      _hasCachedSettings = true;
    }
    return st;
  }
  // ART and SINGLE_SHOT both apply the setting locally with zero I2C: ART runs
  // at its fixed vendor cadence, and single-shot has no periodic rate.
  _config.periodicRate = rate;
  _cachedSettings.periodicRate = rate;
  _hasCachedSettings = true;
  return Status::Ok();
}

Status SHT3x::getPeriodicRate(PeriodicRate& out) const {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  out = _config.periodicRate;
  return Status::Ok();
}

Status SHT3x::startPeriodic(PeriodicRate rate, Repeatability rep) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_singleShotMeasurementPending()) {
    return Status::Error(Err::BUSY, "Measurement in progress");
  }
  if (!isValidPeriodicRate(rate) || !isValidRepeatability(rep)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid periodic settings");
  }

  Status st = _enterPeriodic(rate, rep, false);
  if (st.ok()) {
    _cachedSettings.mode = Mode::PERIODIC;
    _cachedSettings.repeatability = rep;
    _cachedSettings.periodicRate = rate;
    _hasCachedSettings = true;
  }
  return st;
}

Status SHT3x::startArt() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_singleShotMeasurementPending()) {
    return Status::Error(Err::BUSY, "Measurement in progress");
  }

  Status st = _enterPeriodic(_config.periodicRate, _config.repeatability, true);
  if (st.ok()) {
    _cachedSettings.mode = Mode::ART;
    _cachedSettings.repeatability = _config.repeatability;
    _cachedSettings.periodicRate = _config.periodicRate;
    _hasCachedSettings = true;
  }
  return st;
}

Status SHT3x::stopPeriodic() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_jobActive()) {
    return Status::Error(Err::BUSY, "Cooperative job in progress");
  }

  Status st = _stopPeriodicInternal();
  if (st.ok()) {
    _cachedSettings.mode = Mode::SINGLE_SHOT;
    _hasCachedSettings = true;
  }
  return st;
}

Status SHT3x::readStatus(uint16_t& raw) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_singleShotMeasurementPending()) {
    return Status::Error(Err::BUSY, "Measurement in progress");
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
  _parseStatusRegister(raw, out);
  return Status::Ok();
}

Status SHT3x::readStatusWithModeRestore(StatusReadSnapshot& out) {
  out = StatusReadSnapshot{};

  if (!_initialized) {
    Status st = Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
    out.statusReadStatus = st;
    return st;
  }
  if (_jobActive()) {
    Status st = Status::Error(Err::BUSY, "Cooperative job in progress");
    out.statusReadStatus = st;
    return st;
  }

  const Mode initialMode = _mode;
  const Repeatability initialRepeatability = _config.repeatability;
  const PeriodicRate initialRate = _config.periodicRate;
  const bool restorePeriodic =
      _periodicActive && (initialMode == Mode::PERIODIC || initialMode == Mode::ART);

  out.initialMode = initialMode;
  out.finalMode = _mode;
  out.modeInterrupted = restorePeriodic;
  out.restored = !restorePeriodic;

  if (!restorePeriodic) {
    Status st = readStatus(out.status);
    out.statusReadStatus = st;
    out.statusValid = st.ok();
    out.finalMode = _mode;
    out.restored = true;
    return st;
  }

  Status stStop = _stopPeriodicInternal();
  out.stopStatus = stStop;
  out.finalMode = _mode;
  if (!stStop.ok()) {
    return stStop;
  }

  uint16_t raw = 0;
  Status stStatus = _readStatusRaw(raw, true);
  out.statusReadStatus = stStatus;
  if (stStatus.ok()) {
    _parseStatusRegister(raw, out.status);
    out.statusValid = true;
  }

  {
    ScopedOfflineI2cAllowance allowOfflineI2c(_allowOfflineI2c, true);
    const Status stRestore = (initialMode == Mode::ART)
        ? _enterPeriodic(initialRate, initialRepeatability, true)
        : _enterPeriodic(initialRate, initialRepeatability, false);
    out.restoreStatus = stRestore;
    out.finalMode = _mode;
    out.restored = stRestore.ok() && _periodicActive && _mode == initialMode;
    if (!stRestore.ok()) {
      return stRestore;
    }
  }
  if (!stStatus.ok()) {
    return stStatus;
  }
  return Status::Ok();
}

Status SHT3x::clearStatus() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_singleShotMeasurementPending()) {
    return Status::Error(Err::BUSY, "Measurement in progress");
  }
  if (_periodicActive) {
    return Status::Error(Err::BUSY, "Stop periodic mode before clearing status");
  }

  return _writeCommand(cmd::CMD_CLEAR_STATUS, true);
}

Status SHT3x::setHeater(bool enable) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_singleShotMeasurementPending()) {
    return Status::Error(Err::BUSY, "Measurement in progress");
  }
  if (_periodicActive) {
    return Status::Error(Err::BUSY, "Stop periodic mode before changing heater");
  }

  Status st = _writeCommand(enable ? cmd::CMD_HEATER_ENABLE
                                   : cmd::CMD_HEATER_DISABLE,
                            true, false);
  if (!st.ok()) {
    return st;
  }

  uint16_t statusRaw = 0;
  st = _readStatusRaw(statusRaw, true, false);
  if (!st.ok()) {
    return st;
  }
  st = statusDiagnosticFailure(statusRaw);
  if (!st.ok()) {
    _recordProtocolFailure(st, true);
    return st;
  }
  const bool heaterOn = (statusRaw & cmd::STATUS_HEATER_ON) != 0U;
  if (heaterOn != enable) {
    st = Status::Error(Err::COMMAND_FAILED,
                       "Heater state did not match command");
    _recordProtocolFailure(st, true);
    return st;
  }

  (void)_completeLogicalOperation(Status::Ok());
  _cachedSettings.heaterEnabled = enable;
  _hasCachedSettings = true;
  return Status::Ok();
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
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_jobActive()) {
    return Status::Error(Err::BUSY, "Cooperative job in progress");
  }
  if (_periodicActive) {
    return Status::Error(Err::BUSY, "Stop periodic mode before reset");
  }

  const bool startedOffline = (_driverState == DriverState::OFFLINE);
  ScopedOfflineI2cAllowance allowOfflineI2c(_allowOfflineI2c, true);
  Status result = [&]() -> Status {
    Status st = _writeCommand(cmd::CMD_SOFT_RESET, true);
    if (!st.ok()) {
      return st;
    }

    st = _waitMs(RESET_DELAY_MS);
    if (!st.ok()) {
      _hardwareStateValid = false;
      return st;
    }

    _measurementRequested = false;
    _measurementReady = false;
    _hasSample = false;
    _measurementPhase = JobPhase::IDLE;
    _lastMeasurementStatus = initialMeasurementStatus();
    _mode = Mode::SINGLE_SHOT;
    _config.mode = Mode::SINGLE_SHOT;
    _periodicActive = false;
    _periodicStartMs = 0;
    _lastFetchMs = 0;
    _lastFetchValid = false;
    _periodMs = 0;
    _sampleTimestampMs = 0;
    _missedSamples = 0;
    _missedRemainderMs = 0;
    _notReadyStartMs = 0;
    _notReadyStartValid = false;
    _notReadyCount = 0;
    _hardwareStateValid = true;

    return Status::Ok();
  }();
  if (startedOffline && !result.ok() && !result.inProgress()) {
    _reassertOfflineLatch();
  }
  return result;
}

Status SHT3x::interfaceReset() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_jobActive()) {
    return Status::Error(Err::BUSY, "Cooperative job in progress");
  }
  if (_config.busReset == nullptr) {
    return Status::Error(Err::UNSUPPORTED, "Bus reset callback not set");
  }

  Status st = _config.busReset(_config.i2cUser);

  // A callback failure can still mean that some SCL edges reached the bus.
  // Conservatively invalidate the chip-state model and start tIDLE from the
  // callback's completion before exposing its precise status to the caller.
  _markCommandAttempt();
  _hardwareStateValid = false;
  if (!st.ok()) {
    return st;
  }

  _measurementRequested = false;
  _measurementReady = false;
  _hasSample = false;
  _measurementPhase = JobPhase::IDLE;
  _lastMeasurementStatus = initialMeasurementStatus();
  _measurementReadyMs = 0;
  _lastFetchMs = 0;
  _lastFetchValid = false;
  _sampleTimestampMs = 0;
  _missedSamples = 0;
  _missedRemainderMs = 0;
  _notReadyStartMs = 0;
  _notReadyStartValid = false;
  _notReadyCount = 0;
  if (_periodicActive) {
    _periodicStartMs = _nowMs(_config);
  }

  return Status::Ok();
}

Status SHT3x::generalCallReset() {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_jobActive()) {
    return Status::Error(Err::BUSY, "Cooperative job in progress");
  }
  if (!_config.allowGeneralCallReset) {
    return Status::Error(Err::INVALID_CONFIG, "General call reset disabled");
  }

  const bool startedOffline = (_driverState == DriverState::OFFLINE);
  ScopedOfflineI2cAllowance allowOfflineI2c(_allowOfflineI2c, true);
  Status result = [&]() -> Status {
    Status st = _ensureCommandDelay();
    if (!st.ok()) {
      return st;
    }

    uint8_t byte = cmd::GENERAL_CALL_RESET_BYTE;
    st = _i2cWriteRawAddrTracked(cmd::GENERAL_CALL_ADDR, &byte, 1);
    if (!st.ok()) {
      return st;
    }

    st = _waitMs(RESET_DELAY_MS);
    if (!st.ok()) {
      _hardwareStateValid = false;
      return st;
    }

    _measurementRequested = false;
    _measurementReady = false;
    _hasSample = false;
    _measurementPhase = JobPhase::IDLE;
    _lastMeasurementStatus = initialMeasurementStatus();
    _measurementReadyMs = 0;
    _mode = Mode::SINGLE_SHOT;
    _config.mode = Mode::SINGLE_SHOT;
    _periodicActive = false;
    _periodicStartMs = 0;
    _lastFetchMs = 0;
    _lastFetchValid = false;
    _periodMs = 0;
    _sampleTimestampMs = 0;
    _missedSamples = 0;
    _missedRemainderMs = 0;
    _notReadyStartMs = 0;
    _notReadyStartValid = false;
    _notReadyCount = 0;
    _hardwareStateValid = true;

    return Status::Ok();
  }();
  if (startedOffline && !result.ok() && !result.inProgress()) {
    _reassertOfflineLatch();
  }
  return result;
}

Status SHT3x::readSerialNumber(uint32_t& serial, ClockStretching stretch) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_singleShotMeasurementPending()) {
    return Status::Error(Err::BUSY, "Measurement in progress");
  }
  if (_periodicActive) {
    return Status::Error(Err::BUSY, "Stop periodic mode before reading serial");
  }
  if (!isValidClockStretching(stretch)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid clock stretching");
  }

  const uint16_t command = (stretch == ClockStretching::STRETCH_ENABLED)
      ? cmd::CMD_SERIAL_STRETCH
      : cmd::CMD_SERIAL_NO_STRETCH;

  Status st = _writeCommand(command, true, false);
  if (!st.ok()) {
    return st;
  }

  uint8_t buf[cmd::SERIAL_DATA_LEN] = {};
  st = _readAfterCommand(buf, sizeof(buf), true, false, false);
  if (!st.ok()) {
    return st;
  }

  if (_crc8(&buf[0], 2) != buf[2]) {
    const Status failure = Status::Error(Err::CRC_MISMATCH,
                                         "CRC mismatch (serial word1)");
    _recordProtocolFailure(failure, true);
    return failure;
  }
  if (_crc8(&buf[3], 2) != buf[5]) {
    const Status failure = Status::Error(Err::CRC_MISMATCH,
                                         "CRC mismatch (serial word2)");
    _recordProtocolFailure(failure, true);
    return failure;
  }

  const uint16_t word1 = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
  const uint16_t word2 = static_cast<uint16_t>((buf[3] << 8) | buf[4]);
  serial = (static_cast<uint32_t>(word1) << 16) | word2;

  (void)_completeLogicalOperation(Status::Ok());
  return Status::Ok();
}

Status SHT3x::readAlertLimitRaw(AlertLimitKind kind, uint16_t& value) {
  if (!_initialized) {
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_singleShotMeasurementPending()) {
    return Status::Error(Err::BUSY, "Measurement in progress");
  }
  if (_periodicActive) {
    return Status::Error(Err::BUSY, "Stop periodic mode before reading alert limits");
  }

  const uint16_t command = _commandForAlertRead(kind);
  if (command == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid alert limit kind");
  }

  Status st = _writeCommand(command, true, false);
  if (!st.ok()) {
    return st;
  }

  uint8_t buf[cmd::ALERT_DATA_LEN] = {};
  st = _readAfterCommand(buf, sizeof(buf), true, false, false);
  if (!st.ok()) {
    return st;
  }

  if (_crc8(&buf[0], 2) != buf[2]) {
    const Status failure = Status::Error(Err::CRC_MISMATCH,
                                         "CRC mismatch (alert limit)");
    _recordProtocolFailure(failure, true);
    return failure;
  }

  value = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
  (void)_completeLogicalOperation(Status::Ok());
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
    return Status::Error(Err::NOT_INITIALIZED, "Driver not bound");
  }
  if (_singleShotMeasurementPending()) {
    return Status::Error(Err::BUSY, "Measurement in progress");
  }
  if (_periodicActive) {
    return Status::Error(Err::BUSY, "Stop periodic mode before writing alert limits");
  }

  const uint16_t command = _commandForAlertWrite(kind);
  if (command == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid alert limit kind");
  }

  Status st = _writeCommandWithData(command, value, true, false);
  if (!st.ok()) {
    return st;
  }

  uint16_t statusRaw = 0;
  st = _readStatusRaw(statusRaw, true, false);
  if (!st.ok()) {
    _hardwareStateValid = false;
    return st;
  }

  st = statusDiagnosticFailure(statusRaw);
  if (!st.ok()) {
    _recordProtocolFailure(st, true);
    _hardwareStateValid = false;
    return st;
  }

  (void)_completeLogicalOperation(Status::Ok());

  const uint8_t idx = static_cast<uint8_t>(kind);
  if (idx < 4) {
    _cachedSettings.alertRaw[idx] = value;
    _cachedSettings.alertValid[idx] = true;
    _hasCachedSettings = true;
  }
  return Status::Ok();
}

Status SHT3x::writeAlertLimit(AlertLimitKind kind, float temperatureC, float humidityPct) {
  if (!std::isfinite(temperatureC) || !std::isfinite(humidityPct)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid alert limit value");
  }
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
  if (!std::isfinite(temperatureC)) {
    temperatureC = -45.0f;
  }
  if (!std::isfinite(humidityPct)) {
    humidityPct = 0.0f;
  }
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

  uint16_t appNoteWord = 0;
  if (alertAppNoteDefaultWord(temperatureC, humidityPct, appNoteWord)) {
    return appNoteWord;
  }

  // Alert limits keep only the 7 most significant humidity bits and the 9 most
  // significant temperature bits. Round to the nearest reduced code rather than
  // truncating: truncation biases every threshold low by half a code on average
  // (-0.39 %RH / -0.17 degC) and mis-encodes the published 20 %RH / -10 degC
  // reset default as 0x3266 instead of 0x3466.
  const float rh7F = humidityPct * 65535.0f / 100.0f / 512.0f;
  const float t9F = (temperatureC + 45.0f) * 65535.0f / 175.0f / 128.0f;

  uint32_t rh7 = static_cast<uint32_t>(rh7F + 0.5f);
  uint32_t t9 = static_cast<uint32_t>(t9F + 0.5f);

  if (rh7 > ALERT_RH_CODE_MAX) {
    rh7 = ALERT_RH_CODE_MAX;
  }
  if (t9 > ALERT_T_CODE_MAX) {
    t9 = ALERT_T_CODE_MAX;
  }

  return static_cast<uint16_t>((rh7 << ALERT_T_CODE_BITS) | t9);
}

void SHT3x::decodeAlertLimit(uint16_t limit, float& temperatureC, float& humidityPct) {
  const uint16_t rh7 =
      static_cast<uint16_t>((limit >> ALERT_T_CODE_BITS) & ALERT_RH_CODE_MAX);
  const uint16_t t9 = static_cast<uint16_t>(limit & ALERT_T_CODE_MAX);

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

int32_t SHT3x::convertTemperatureMilliCelsius(uint16_t raw) {
  return convertTemperatureMilliCelsius(raw, MilliRounding::NEAREST);
}

int32_t SHT3x::convertTemperatureMilliCelsius(uint16_t raw,
                                               MilliRounding rounding) {
  const int64_t numerator = 175000LL * static_cast<int64_t>(raw);
  const int64_t bias =
      rounding == MilliRounding::TRUNCATE_SCALED ? 0LL : 32767LL;
  const int32_t scaled =
      static_cast<int32_t>((numerator + bias) / 65535LL);
  return scaled - 45000;
}

int32_t SHT3x::convertHumidityMilliPercent(uint16_t raw) {
  return convertHumidityMilliPercent(raw, MilliRounding::NEAREST);
}

int32_t SHT3x::convertHumidityMilliPercent(uint16_t raw,
                                           MilliRounding rounding) {
  const int64_t numerator = 100000LL * static_cast<int64_t>(raw);
  const int64_t bias =
      rounding == MilliRounding::TRUNCATE_SCALED ? 0LL : 32767LL;
  return static_cast<int32_t>((numerator + bias) / 65535LL);
}

uint32_t SHT3x::estimateMeasurementTimeMs() const {
  const uint32_t baseMs = baseMeasurementMs(_config.repeatability, _config.lowVdd);
  // The millisecond timestamp sampled after the command callback can truncate
  // almost one millisecond. Keep configured margin as headroom beyond this
  // unavoidable clock-quantization allowance.
  return baseMs + 1U + _config.singleShotMeasurementMarginMs;
}

bool SHT3x::_singleShotMeasurementPending() const {
  return _jobActive() ||
         (_mode == Mode::SINGLE_SHOT && _measurementRequested && !_measurementReady);
}

uint32_t SHT3x::_allocateJobId() {
  // Identities are nonzero; wrap back to 1 instead of handing out zero.
  const uint32_t id = _nextJobId;
  _nextJobId = (id == std::numeric_limits<uint32_t>::max()) ? 1U : (id + 1U);
  return id;
}

JobEffect SHT3x::_effectForPhase(JobPhase phase, bool ambiguous) const {
  switch (phase) {
    case JobPhase::SINGLE_SHOT_COMMAND:
    case JobPhase::PERIODIC_FETCH_COMMAND:
    case JobPhase::ENSURE_BREAK_COMMAND:
    case JobPhase::ENSURE_RESET_COMMAND:
    case JobPhase::ENSURE_STATUS_COMMAND:
      return ambiguous ? JobEffect::DEVICE_STATE_INDETERMINATE : _jobEffect;

    case JobPhase::SINGLE_SHOT_CONVERSION:
    case JobPhase::SINGLE_SHOT_READ:
    case JobPhase::PERIODIC_READ:
      return JobEffect::RESULT_MAY_BE_PENDING;

    case JobPhase::ENSURE_BREAK_WAIT:
    case JobPhase::ENSURE_RESET_WAIT:
    case JobPhase::ENSURE_STATUS_READ:
      return JobEffect::DEVICE_STATE_CHANGED;

    case JobPhase::IDLE:
    default:
      return _jobEffect;
  }
}

void SHT3x::_clearJobState() {
  _measurementRequested = false;
  _measurementPhase = JobPhase::IDLE;
  _jobType = JobType::NONE;
  _jobRequestId = 0;
  _jobDeadlineMs = 0;
  _jobHasDeadline = false;
  _jobEffect = JobEffect::NONE;
  _jobWakeMs = 0;
  _notReadyStartMs = 0;
  _notReadyStartValid = false;
  _notReadyCount = 0;
}

uint32_t SHT3x::_periodicFetchMarginMs() const {
  uint32_t margin = _config.periodicFetchMarginMs;
  if (margin == 0) {
    if (_periodMs == 0) {
      return 2;
    }
    margin = _periodMs / 20;
    if (margin < 2) {
      margin = 2;
    }
  }
  if (_periodMs > 0 && margin > _periodMs) {
    margin = _periodMs;
  }
  return margin;
}

uint32_t SHT3x::_notReadyWindowMs() const {
  if (_config.notReadyTimeoutMs > 0U) {
    return _config.notReadyTimeoutMs;
  }
  const uint32_t period = (_periodMs > 0U) ? _periodMs : 1000U;
  return saturatingAddU32(period * 3U, _periodicFetchMarginMs());
}

uint32_t SHT3x::_periodicReadyMs(uint32_t nowMs) const {
  if (_periodMs == 0) {
    return nowMs;
  }

  uint32_t startMs = 0;
  uint32_t waitMs = 0;
  if (!_lastFetchValid) {
    startMs = _periodicStartMs;
    waitMs = estimateMeasurementTimeMs();
  } else {
    startMs = _lastFetchMs;
    waitMs = _periodMs;
  }
  waitMs += _periodicFetchMarginMs();

  if (_durationElapsed(nowMs, startMs, waitMs)) {
    return nowMs;
  }
  return startMs + waitMs;
}

uint32_t SHT3x::_periodicRetryMs(uint32_t nowMs) const {
  uint32_t backoff = (_periodMs == 0U) ? _config.commandDelayMs
                                      : _periodicFetchMarginMs();
  if (backoff < _config.commandDelayMs) {
    backoff = _config.commandDelayMs;
  }
  return nowMs + backoff;
}

void SHT3x::_setSafeBaseline() {
  _measurementRequested = false;
  _measurementReady = false;
  _hasSample = false;
  _measurementPhase = JobPhase::IDLE;
  _lastMeasurementStatus = initialMeasurementStatus();
  _measurementReadyMs = 0;
  _periodicActive = false;
  _periodicStartMs = 0;
  _lastFetchMs = 0;
  _lastFetchValid = false;
  _periodMs = 0;
  _sampleTimestampMs = 0;
  _missedSamples = 0;
  _missedRemainderMs = 0;
  _notReadyStartMs = 0;
  _notReadyStartValid = false;
  _notReadyCount = 0;
  _mode = Mode::SINGLE_SHOT;
  _config.mode = Mode::SINGLE_SHOT;
}

void SHT3x::_setDefaultsToConfigAndCache() {
  Config defaults;
  _config.repeatability = defaults.repeatability;
  _config.periodicRate = defaults.periodicRate;
  _config.clockStretching = defaults.clockStretching;
  _config.mode = defaults.mode;
  _mode = defaults.mode;

  _cachedSettings = defaultCachedSettings();
  _hasCachedSettings = true;
}

void SHT3x::_syncCacheFromConfig() {
  _cachedSettings.mode = _mode;
  _cachedSettings.repeatability = _config.repeatability;
  _cachedSettings.periodicRate = _config.periodicRate;
  _cachedSettings.clockStretching = _config.clockStretching;
}

Status SHT3x::_applyCachedSettingsAfterReset() {
  Status st = setRepeatability(_cachedSettings.repeatability);
  if (!st.ok()) {
    return st;
  }
  st = setClockStretching(_cachedSettings.clockStretching);
  if (!st.ok()) {
    return st;
  }
  st = setPeriodicRate(_cachedSettings.periodicRate);
  if (!st.ok()) {
    return st;
  }
  st = setHeater(_cachedSettings.heaterEnabled);
  if (!st.ok()) {
    return st;
  }

  for (size_t i = 0; i < 4; ++i) {
    if (!_cachedSettings.alertValid[i]) {
      continue;
    }
    const auto kind = static_cast<AlertLimitKind>(i);
    st = writeAlertLimitRaw(kind, _cachedSettings.alertRaw[i]);
    if (!st.ok()) {
      return st;
    }
  }

  if (_cachedSettings.mode == Mode::PERIODIC) {
    return startPeriodic(_cachedSettings.periodicRate, _cachedSettings.repeatability);
  }
  if (_cachedSettings.mode == Mode::ART) {
    return startArt();
  }
  return Status::Ok();
}

Status SHT3x::_admitRecoveryAttempt() {
  const uint32_t now = _nowMs(_config);
  if (_config.recoverBackoffMs > 0U && _lastRecoverValid &&
      !_durationElapsed(now, _lastRecoverMs, _config.recoverBackoffMs)) {
    return Status::Error(Err::BUSY, "Recovery backoff active");
  }
  _lastRecoverMs = now;
  _lastRecoverValid = true;
  return Status::Ok();
}

Status SHT3x::_performRecoveryLadder() {
  const bool startedOffline = (_driverState == DriverState::OFFLINE);
  ScopedOfflineI2cAllowance allowOfflineI2c(_allowOfflineI2c, true);
  Status result = [&]() -> Status {
    auto probeTracked = [this]() -> Status {
      uint16_t statusRaw = 0;
      Status st = _readStatusRaw(statusRaw, true, false);
      if (!st.ok()) {
        return st;
      }
      st = statusDiagnosticFailure(statusRaw);
      if (!st.ok()) {
        _recordProtocolFailure(st, true);
      } else {
        (void)_completeLogicalOperation(st);
      }
      return st;
    };
    bool acquisitionStopped = _hardwareStateValid && !_periodicActive;
    auto reconciliationRequired = []() -> Status {
      return Status::Error(Err::BUSY, "Recovery did not establish idle state");
    };
    auto acceptProbe = [&acquisitionStopped,
                        &reconciliationRequired](Status st,
                                                 Status& last) -> bool {
      if (!st.ok()) {
        last = st;
        return false;
      }
      if (acquisitionStopped) {
        return true;
      }
      last = reconciliationRequired();
      return false;
    };

    // The opening probe is a Read Status transaction. While periodic/ART
    // acquisition is running, Fetch Data is the only documented readout, and
    // acceptProbe() could not accept the answer anyway because Break has to run
    // first. Skip it there; keep it otherwise, where it is the cheap way to
    // learn whether the bus works at all.
    Status last = reconciliationRequired();
    if (!_periodicActive) {
      last = probeTracked();
      if (acceptProbe(last, last)) {
        return Status::Ok();
      }
    }

    if (_config.recoverUseBusReset && _config.busReset != nullptr) {
      Status st = interfaceReset();
      acquisitionStopped = false;
      if (st.ok()) {
        st = probeTracked();
        if (acceptProbe(st, last)) {
          return Status::Ok();
        }
      } else {
        last = st;
      }
    }

    if (_config.recoverUseSoftReset) {
      Status stStop = Status::Ok();
      if (!acquisitionStopped) {
        stStop = _writeCommand(cmd::CMD_BREAK, true);
        if (stStop.ok()) {
          stStop = _waitMs(BREAK_DELAY_MS);
        }
        if (stStop.ok()) {
          _setSafeBaseline();
          acquisitionStopped = true;
        } else {
          last = stStop;
        }
      }

      if (stStop.ok()) {
        Status st = softReset();
        if (st.ok()) {
          acquisitionStopped = true;
          st = probeTracked();
          if (acceptProbe(st, last)) {
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
        _setSafeBaseline();
        acquisitionStopped = true;
        st = probeTracked();
        if (acceptProbe(st, last)) {
          return Status::Ok();
        }
      }
      last = st;
    }

    if (_config.allowGeneralCallReset) {
      Status st = generalCallReset();
      if (st.ok()) {
        acquisitionStopped = true;
        st = probeTracked();
        if (acceptProbe(st, last)) {
          return Status::Ok();
        }
      }
      last = st;
    }

    return last;
  }();
  if (startedOffline && !result.ok() && !result.inProgress()) {
    _reassertOfflineLatch();
  }
  return result;
}

Status SHT3x::_i2cWriteReadRaw(const uint8_t* txBuf, size_t txLen,
                               uint8_t* rxBuf, size_t rxLen) {
  if (_config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C write-read not set");
  }
  if ((txLen > 0 && txBuf == nullptr) || (rxLen > 0 && rxBuf == nullptr)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }
  if (txLen == 0 && rxLen == 0) {
    return Status::Error(Err::INVALID_PARAM, "Empty I2C transaction");
  }
  if (txLen > 0 && rxLen > 0) {
    return Status::Error(Err::INVALID_PARAM, "Combined write+read not supported");
  }
  Status st = _config.i2cWriteRead(_config.i2cAddress, txBuf, txLen, rxBuf, rxLen,
                                   _config.i2cTimeoutMs, _config.i2cUser);
  if (st.code == Err::I2C_NACK_READ &&
      !hasCapability(_config.transportCapabilities,
                     TransportCapability::READ_HEADER_NACK)) {
    const int32_t detail = (st.detail != 0)
        ? st.detail
        : static_cast<int32_t>(Err::I2C_NACK_READ);
    return Status::Error(Err::I2C_ERROR, "Read-header NACK unsupported", detail);
  }
  return st;
}

Status SHT3x::_i2cWriteRawAddr(uint8_t addr, const uint8_t* buf, size_t len) {
  if (_config.i2cWrite == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C write not set");
  }
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }
  const Status st =
      _config.i2cWrite(addr, buf, len, _config.i2cTimeoutMs, _config.i2cUser);
  // A failed callback may still have placed the command on the bus/device.
  _markCommandAttempt();
  return st;
}

Status SHT3x::_i2cWriteRaw(const uint8_t* buf, size_t len) {
  return _i2cWriteRawAddr(_config.i2cAddress, buf, len);
}

Status SHT3x::_admitTrackedI2c() const {
  if (!_allowOfflineI2c && _initialized &&
      _config.healthPolicy == HealthPolicy::LATCH_OFFLINE &&
      _driverState == DriverState::OFFLINE && _jobType != JobType::ENSURE_IDLE) {
    return _offlineStatus();
  }
  return Status::Ok();
}

Status SHT3x::_i2cWriteRawAddrTracked(uint8_t addr, const uint8_t* buf, size_t len) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }
  const Status admission = _admitTrackedI2c();
  if (!admission.ok()) {
    return admission;
  }

  Status st = _i2cWriteRawAddr(addr, buf, len);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  return _updateHealth(st);
}

Status SHT3x::_i2cWriteReadTracked(const uint8_t* txBuf, size_t txLen,
                                   uint8_t* rxBuf, size_t rxLen,
                                   bool logicalComplete) {
  if ((txLen > 0 && txBuf == nullptr) || (rxLen > 0 && rxBuf == nullptr)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }
  const Status admission = _admitTrackedI2c();
  if (!admission.ok()) {
    return admission;
  }

  Status st = _i2cWriteReadRaw(txBuf, txLen, rxBuf, rxLen);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  return _updateHealth(st, logicalComplete);
}

Status SHT3x::_i2cWriteReadTrackedAllowNoData(const uint8_t* txBuf, size_t txLen,
                                              uint8_t* rxBuf, size_t rxLen,
                                              bool allowNoData,
                                              bool logicalComplete) {
  if ((txLen > 0 && txBuf == nullptr) || (rxLen > 0 && rxBuf == nullptr)) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }

  const bool canReportNack = hasCapability(
      _config.transportCapabilities, TransportCapability::READ_HEADER_NACK);
  const Status admission = _admitTrackedI2c();
  if (!admission.ok()) {
    return admission;
  }

  Status st = _i2cWriteReadRaw(txBuf, txLen, rxBuf, rxLen);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  const bool readOnly = txLen == 0U && rxLen > 0U;
  const bool provenNoData = allowNoData && readOnly && canReportNack &&
                            st.code == Err::I2C_NACK_READ;
  const bool periodicReadJob = _jobType == JobType::MEASUREMENT &&
                               _measurementPhase == JobPhase::PERIODIC_READ;
  const bool inferredNoData = allowNoData && readOnly && !canReportNack &&
                              periodicReadJob && st.code == Err::I2C_ERROR;
  if (provenNoData || inferredNoData) {
    _recordBusActivity(_nowMs(_config));
    if (provenNoData) {
      if (_totalNotReady < std::numeric_limits<uint32_t>::max()) {
        _totalNotReady++;
      }
    } else if (_totalInferredNotReady < std::numeric_limits<uint32_t>::max()) {
      _totalInferredNotReady++;
    }
    return Status::Error(Err::MEASUREMENT_NOT_READY,
                         provenNoData ? "No new data (proven read NACK)"
                                      : "No new data (inferred ambiguous read)",
                         st.detail);
  }
  return _updateHealth(st, logicalComplete);
}

Status SHT3x::_i2cWriteTracked(const uint8_t* buf, size_t len,
                               bool logicalComplete) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }
  const Status admission = _admitTrackedI2c();
  if (!admission.ok()) {
    return admission;
  }

  Status st = _i2cWriteRaw(buf, len);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;
  }
  return _updateHealth(st, logicalComplete);
}

Status SHT3x::_offlineStatus() const {
  return Status::Error(Err::BUSY, "Driver is offline; call recover()");
}

Status SHT3x::_writeCommand(uint16_t command, bool tracked, bool logicalComplete) {
  Status st = _ensureCommandDelay();
  if (!st.ok()) {
    return st;
  }

  return _writeCommandNoDelay(command, tracked, logicalComplete);
}

Status SHT3x::_writeCommandNoDelay(uint16_t command, bool tracked,
                                   bool logicalComplete) {
  uint8_t buf[2] = {static_cast<uint8_t>(command >> 8), static_cast<uint8_t>(command & 0xFF)};
  Status st = tracked ? _i2cWriteTracked(buf, sizeof(buf), logicalComplete)
                      : _i2cWriteRaw(buf, sizeof(buf));
  return st;
}

Status SHT3x::_writeCommandWithData(uint16_t command, uint16_t data, bool tracked,
                                    bool logicalComplete) {
  Status st = _ensureCommandDelay();
  if (!st.ok()) {
    return st;
  }

  uint8_t payload[MAX_WRITE_LEN] = {};
  payload[0] = static_cast<uint8_t>(command >> 8);
  payload[1] = static_cast<uint8_t>(command & 0xFF);
  payload[2] = static_cast<uint8_t>(data >> 8);
  payload[3] = static_cast<uint8_t>(data & 0xFF);
  payload[4] = _crc8(&payload[2], 2);

  st = tracked ? _i2cWriteTracked(payload, sizeof(payload), logicalComplete)
               : _i2cWriteRaw(payload, sizeof(payload));
  return st;
}

Status SHT3x::_readAfterCommand(uint8_t* buf, size_t len, bool tracked,
                                bool allowNoData, bool logicalComplete) {
  if (buf == nullptr || len == 0 || len > MAX_READ_LEN) {
    return Status::Error(Err::INVALID_PARAM, "Invalid read buffer");
  }

  Status st = _ensureCommandDelay();
  if (!st.ok()) {
    return st;
  }

  return _readOnly(buf, len, tracked, allowNoData, logicalComplete);
}

Status SHT3x::_readOnly(uint8_t* buf, size_t len, bool tracked,
                        bool allowNoData, bool logicalComplete) {
  if (buf == nullptr || len == 0 || len > MAX_READ_LEN) {
    return Status::Error(Err::INVALID_PARAM, "Invalid read buffer");
  }

  if (tracked) {
    return allowNoData
        ? _i2cWriteReadTrackedAllowNoData(nullptr, 0, buf, len, true,
                                          logicalComplete)
        : _i2cWriteReadTracked(nullptr, 0, buf, len, logicalComplete);
  }
  return _i2cWriteReadRaw(nullptr, 0, buf, len);
}

Status SHT3x::_updateHealth(const Status& st, bool logicalComplete) {
  if (!_initialized || st.inProgress()) {
    return st;
  }

  const uint32_t now = _nowMs(_config);
  const uint32_t maxU32 = std::numeric_limits<uint32_t>::max();

  _recordBusActivity(now);

  if (st.ok()) {
    if (_transportSuccess < maxU32) {
      _transportSuccess++;
    }
    if (!logicalComplete) {
      return st;
    }
    return _completeLogicalOperation(st);
  }

  if (_transportFailures < maxU32) {
    _transportFailures++;
  }
  _hardwareStateValid = false;
  (void)_completeLogicalOperation(stableStatus(st));
  return st;
}

Status SHT3x::_completeLogicalOperation(const Status& st) {
  if (!_initialized || st.inProgress()) {
    return st;
  }

  const uint32_t now = _nowMs(_config);
  const uint32_t maxU32 = std::numeric_limits<uint32_t>::max();
  const uint8_t maxU8 = std::numeric_limits<uint8_t>::max();

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

void SHT3x::_recordProtocolFailure(const Status& st, bool tracked) {
  if (!tracked) {
    return;
  }
  if (_protocolFailures < std::numeric_limits<uint32_t>::max()) {
    _protocolFailures++;
  }
  (void)_completeLogicalOperation(st);
}

void SHT3x::_reassertOfflineLatch() {
  _driverState = DriverState::OFFLINE;
  const uint8_t threshold = _config.offlineThreshold == 0 ? 1 : _config.offlineThreshold;
  if (_consecutiveFailures < threshold) {
    _consecutiveFailures = threshold;
  }
}

void SHT3x::_recordBusActivity(uint32_t nowMs) {
  _lastBusActivityMs = nowMs;
}

void SHT3x::_markCommandAttempt() {
  _lastCommandUs = _nowUs(_config);
  _lastCommandMs = _nowMs(_config);
  _lastCommandValid = true;
}

bool SHT3x::_commandDelayElapsed() const {
  if (!_lastCommandValid) {
    return true;
  }

  // A millisecond difference greater than the requested delay proves the
  // interval elapsed despite timestamp quantization and extends the proof
  // window beyond the 71.6-minute microsecond wrap. Within that boundary the
  // microsecond clock preserves sub-millisecond precision.
  const uint32_t delayMs = _config.commandDelayMs;
  if (_durationElapsed(_nowMs(_config), _lastCommandMs, delayMs + 1U)) {
    return true;
  }
  const uint32_t delayUs = delayMs * 1000U;
  return _durationElapsed(_nowUs(_config), _lastCommandUs, delayUs);
}

Status SHT3x::_ensureCommandDelay() {
  if (_commandDelayElapsed()) {
    return Status::Ok();
  }

  const uint32_t startMs = _nowMs(_config);
  const uint32_t timeoutMs = saturatingAddU32(static_cast<uint32_t>(_config.commandDelayMs),
                                              _config.i2cTimeoutMs);
  uint32_t lastMs = startMs;
  uint32_t stableLoops = 0;

  while (!_commandDelayElapsed()) {
    const uint32_t nowMs = _nowMs(_config);
    if (static_cast<uint32_t>(nowMs - startMs) > timeoutMs) {
      return Status::Error(Err::TIMEOUT, "Command delay timeout");
    }
    if (nowMs != lastMs) {
      lastMs = nowMs;
      stableLoops = 0;
    } else if (++stableLoops >= 500000U) {
      return Status::Error(Err::TIMEOUT, "Command delay timeout");
    }
    _config.cooperativeYield(_config.timeUser);
  }

  return Status::Ok();
}

Status SHT3x::_waitMs(uint32_t delayMs) {
  if (delayMs == 0) {
    return Status::Ok();
  }

  const uint32_t startMs = _nowMs(_config);
  const uint32_t deadline = startMs + delayMs;
  const uint32_t timeoutMs = saturatingAddU32(delayMs, _config.i2cTimeoutMs);
  uint32_t lastMs = startMs;
  uint32_t stableLoops = 0;

  while (true) {
    const uint32_t nowMs = _nowMs(_config);
    if (_timeElapsed(nowMs, deadline)) {
      break;
    }
    if (static_cast<uint32_t>(nowMs - startMs) > timeoutMs) {
      return Status::Error(Err::TIMEOUT, "Wait timeout");
    }
    if (nowMs != lastMs) {
      lastMs = nowMs;
      stableLoops = 0;
    } else if (++stableLoops >= 500000U) {
      return Status::Error(Err::TIMEOUT, "Wait timeout");
    }
    _config.cooperativeYield(_config.timeUser);
  }

  return Status::Ok();
}

Status SHT3x::_readStatusRaw(uint16_t& raw, bool tracked,
                             bool logicalComplete) {
  Status st = _writeCommand(cmd::CMD_READ_STATUS, tracked, false);
  if (!st.ok()) {
    return st;
  }

  uint8_t buf[cmd::STATUS_DATA_LEN] = {};
  st = _readAfterCommand(buf, sizeof(buf), tracked, false, false);
  if (!st.ok()) {
    return st;
  }

  if (_crc8(&buf[0], 2) != buf[2]) {
    const Status failure = Status::Error(Err::CRC_MISMATCH,
                                         "CRC mismatch (status)");
    _recordProtocolFailure(failure, tracked);
    return failure;
  }

  raw = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
  if (tracked && logicalComplete) {
    (void)_completeLogicalOperation(Status::Ok());
  }
  return Status::Ok();
}

Status SHT3x::_readMeasurementRawNoDelay(RawSample& out, bool tracked, bool allowNoData) {
  uint8_t buf[cmd::MEASUREMENT_DATA_LEN] = {};
  Status st = _readOnly(buf, sizeof(buf), tracked, allowNoData, false);
  if (!st.ok()) {
    return st;
  }

  if (_crc8(&buf[0], 2) != buf[2]) {
    const Status failure = Status::Error(Err::CRC_MISMATCH,
                                         "CRC mismatch (temperature)");
    _recordProtocolFailure(failure, tracked);
    return failure;
  }
  if (_crc8(&buf[3], 2) != buf[5]) {
    const Status failure = Status::Error(Err::CRC_MISMATCH,
                                         "CRC mismatch (humidity)");
    _recordProtocolFailure(failure, tracked);
    return failure;
  }

  out.rawTemperature = static_cast<uint16_t>((buf[0] << 8) | buf[1]);
  out.rawHumidity = static_cast<uint16_t>((buf[3] << 8) | buf[4]);
  if (tracked) {
    (void)_completeLogicalOperation(Status::Ok());
  }
  return Status::Ok();
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

  uint16_t command = 0;
  if (art) {
    command = cmd::CMD_ART;
  } else {
    command = _commandForPeriodic(rep, rate);
  }
  if (command == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid periodic command");
  }

  Status st = _writeCommand(command, true);
  if (!st.ok()) {
    return st;
  }

  _measurementRequested = false;
  _measurementReady = false;
  _measurementPhase = JobPhase::IDLE;
  _lastMeasurementStatus = initialMeasurementStatus();
  _measurementReadyMs = 0;
  _periodicActive = true;
  _notReadyStartMs = 0;
  _notReadyStartValid = false;
  _notReadyCount = 0;
  _missedSamples = 0;
  _missedRemainderMs = 0;
  _mode = art ? Mode::ART : Mode::PERIODIC;
  _config.mode = _mode;
  if (!art) {
    _config.periodicRate = rate;
    _config.repeatability = rep;
    _periodMs = _periodMsForRate(rate);
  } else {
    _periodMs = ART_PERIOD_MS;
  }
  _periodicStartMs = _nowMs(_config);
  _lastFetchMs = 0;
  _lastFetchValid = false;

  return Status::Ok();
}

Status SHT3x::_stopPeriodicInternal() {
  if (!_periodicActive) {
    _measurementRequested = false;
    _measurementReady = false;
    _measurementPhase = JobPhase::IDLE;
    _lastMeasurementStatus = initialMeasurementStatus();
    _measurementReadyMs = 0;
    _mode = Mode::SINGLE_SHOT;
    _config.mode = Mode::SINGLE_SHOT;
    _periodicStartMs = 0;
    _lastFetchMs = 0;
    _lastFetchValid = false;
    _periodMs = 0;
    _notReadyStartMs = 0;
    _notReadyStartValid = false;
    _notReadyCount = 0;
    _missedSamples = 0;
    _missedRemainderMs = 0;
    return Status::Ok();
  }

  Status st = _writeCommand(cmd::CMD_BREAK, true);
  if (!st.ok()) {
    return st;
  }

  // BREAK was accepted by the sensor — periodic mode is now stopped.
  // Update driver state immediately, before the processing delay,
  // so that state remains consistent even if _waitMs() fails.
  _measurementRequested = false;
  _measurementReady = false;
  _measurementPhase = JobPhase::IDLE;
  _lastMeasurementStatus = initialMeasurementStatus();
  _measurementReadyMs = 0;
  _periodicActive = false;
  _mode = Mode::SINGLE_SHOT;
  _config.mode = Mode::SINGLE_SHOT;
  _periodicStartMs = 0;
  _lastFetchMs = 0;
  _lastFetchValid = false;
  _periodMs = 0;
  _notReadyStartMs = 0;
  _notReadyStartValid = false;
  _notReadyCount = 0;
  _missedSamples = 0;
  _missedRemainderMs = 0;

  st = _waitMs(BREAK_DELAY_MS);
  if (!st.ok()) {
    _hardwareStateValid = false;
  }
  return st;
}

uint8_t SHT3x::_crc8(const uint8_t* data, size_t len) {
  if (data == nullptr || len == 0) {
    return 0;
  }
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

bool SHT3x::_durationElapsed(uint32_t now, uint32_t start, uint32_t duration) {
  return static_cast<uint32_t>(now - start) >= duration;
}

bool SHT3x::_timeElapsed(uint32_t now, uint32_t target) {
  return static_cast<int32_t>(now - target) >= 0;
}

void SHT3x::_parseStatusRegister(uint16_t raw, StatusRegister& out) {
  out.raw = raw;
  out.alertPending = (raw & cmd::STATUS_ALERT_PENDING) != 0;
  out.heaterOn = (raw & cmd::STATUS_HEATER_ON) != 0;
  out.rhAlert = (raw & cmd::STATUS_RH_ALERT) != 0;
  out.tAlert = (raw & cmd::STATUS_T_ALERT) != 0;
  out.resetDetected = (raw & cmd::STATUS_RESET_DETECTED) != 0;
  out.commandError = (raw & cmd::STATUS_COMMAND_ERROR) != 0;
  out.writeCrcError = (raw & cmd::STATUS_WRITE_CRC_ERROR) != 0;
}

}  // namespace SHT3x
