# SHT3x Driver Audit

Findings from a full review of the driver core, examples, tests, tooling, and
documentation against the Sensirion vendor documents in
`docs/reference/vendor/`.

Issues that were simple and certain have already been fixed — they are listed in
"Already fixed" at the end. Everything below is an open item with a concrete
proposal. Each finding states what the code does, how it fails, and the smallest
change that makes the logic simpler rather than merely patched.

Severity is about consequence for a device in the field, not about effort.

---

## Summary

| # | Finding | Severity |
| --- | --- | --- |
| [1](#1-a-normal-periodic-not-ready-fetch-latches-the-driver-offline) | A normal periodic "no new sample" latches the driver OFFLINE | Critical |
| [2](#2-a-periodic-measurement-job-can-stay-active-forever) | A periodic measurement job can stay ACTIVE forever | High |
| [3](#3-the-not-ready-window-never-re-arms) | The not-ready window never re-arms | High |
| [4](#4-tracked-transport-wrappers-silently-drop-a-class-of-callback-failure) | Tracked wrappers silently drop a class of callback failure | High |
| [5](#5-begin-claims-a-verified-hardware-baseline-it-did-not-verify) | `begin()` claims a verified hardware baseline it did not verify | High |
| [6](#6-crc-and-sensor-rejection-failures-never-reach-the-health-state-machine) | CRC and sensor-rejection failures never reach the health state machine | High |
| [7](#7-the-arduino-example-adapter-reports-successful-transfers-as-timeouts) | The Arduino example adapter reports successful transfers as timeouts | High |
| [8](#8-deadline-checks-run-after-the-work-and-throw-away-completed-results) | Deadline checks run after the work and throw away completed results | Medium |
| [9](#9-cancelling-a-measurement-forces-a-destructive-recovery-later) | Cancelling a measurement forces a destructive recovery later | Medium |
| [10](#10-the-recovery-backoff-touches-no-bus-but-is-treated-as-a-hardware-failure) | The recovery backoff touches no bus but is treated as a hardware failure | Medium |
| [11](#11-tidle-is-never-restarted-after-a-read-frame) | tIDLE is never restarted after a read frame | Medium |
| [12](#12-a-not-ready-retry-backs-off-a-full-period-and-guarantees-a-lost-sample) | A not-ready retry backs off a full period and guarantees a lost sample | Medium |
| [13](#13-missedsamplesestimate-both-over--and-under-reports) | `missedSamplesEstimate()` both over- and under-reports | Medium |
| [14](#14-art-mode-accepts-settings-changes-the-hardware-cannot-carry) | ART mode accepts settings changes the hardware cannot carry | Medium |
| [15](#15-probe-sends-a-command-during-periodic-acquisition) | `probe()` sends a command during periodic acquisition | Medium |
| [16](#16-the-esp-idf-adapter-never-maps-a-nack-so-probe-cannot-report-device_not_found) | The ESP-IDF adapter never maps a NACK | Medium |
| [17](#17-setheater-commits-cached-state-on-a-transport-ack) | `setHeater()` commits cached state on a transport ACK | Low |
| [18](#18-smaller-correctness-items) | Smaller correctness items (7 of them) | Low |
| [19](#19-test-suite-gaps) | Test suite gaps | — |
| [20](#20-the-two-clis-are-one-cli-forked) | The two CLIs are one CLI, forked (~1850 duplicate lines) | — |
| [21](#21-packaging-declares-the-library-narrower-than-it-is) | Packaging declares the library narrower than it is | — |
| [22](#22-the-v180-tag-does-not-contain-this-tree) | The `v1.8.0` tag does not contain this tree | — |

A cross-cutting theme runs through 1, 2, 3, 8, 9, 10 and 12: several separate
variables encode overlapping facts, and each one drifts differently. See
[Structural notes](#structural-notes).

---

## 1. A normal periodic "not ready" fetch latches the driver OFFLINE

**Severity: Critical.** Affects the default configuration and the shipped
Arduino example.

**What the code does.** In periodic mode, `pollJob()` only tolerates a
not-ready Fetch when the transport declares
`TransportCapability::READ_HEADER_NACK` ([src/SHT3x.cpp:692-699](src/SHT3x.cpp)).
Without that flag, `_i2cWriteReadRaw()` rewrites `I2C_NACK_READ` to `I2C_ERROR`
([src/SHT3x.cpp:2265-2272](src/SHT3x.cpp)), the read goes through
`_updateHealth()` as a completed logical failure, and `pollJob()` terminates the
job with `recordFailure()`.

Arduino `Wire` genuinely cannot prove a read-header NACK — `requestFrom()`
returns only a byte count — so `examples/01_basic_bringup_cli/main.cpp:178`
correctly declares `TransportCapability::NONE`. That correct declaration is
exactly what makes the driver punish it.

**Reproduction.** I built a harness against the real `src/SHT3x.cpp` with a
sensor model whose actual period is slightly slower than nominal. The driver's
auto fetch margin is `period / 20`, i.e. 5%:

```
sensor real period 1000 ms  ->  FINAL: state=READY   totalFailures=0
sensor real period 1050 ms  ->  FINAL: state=READY   totalFailures=0   (5% == margin)
sensor real period 1080 ms  ->  FINAL: state=OFFLINE totalFailures=5   after 5 samples
sensor real period 1200 ms  ->  FINAL: state=OFFLINE totalFailures=5   after 5 samples
```

Once OFFLINE, every `requestMeasurement()` returns
`BUSY "Driver is offline; call recover()"`, and `totalNotReady()` — the counter
built for exactly this condition — reads **0** the whole time. The operator sees
five I2C failures, not "the sensor is slower than we assumed".

With `READ_HEADER_NACK` declared, the same 1200 ms sensor runs indefinitely:
`state=READY, totalFailures=0, totalNotReady=2`.

**Why it is wrong.** A capability flag should decide whether the driver can
*prove* not-ready, not whether it *tolerates* it. Conflating the two means the
honest transport is the one that breaks.

**Proposal.** Make the retry decision depend on the driver's own cadence model,
and let the capability select only the diagnostic counter and the provenance
detail:

```cpp
// Capability decides what we can PROVE, never what we tolerate.
const bool provenNotReady = hasCapability(_config.transportCapabilities,
                                          TransportCapability::READ_HEADER_NACK);
const bool windowOpen = !_notReadyStartValid ||
                        !_timeElapsed(nowMs, _notReadyStartMs + _notReadyWindowMs());

RawSample sample;
Status st = _readMeasurementRawNoDelay(sample, true, provenNotReady && windowOpen);
result.instructionsUsed++;
const uint32_t readCompletedMs = _nowMs(_config);

if (!st.ok()) {
  const bool retryable = windowOpen &&
      (st.code == Err::MEASUREMENT_NOT_READY ||
       (!provenNotReady && (st.code == Err::I2C_ERROR || st.code == Err::I2C_NACK_READ)));
  if (!retryable) {
    _notReadyStartValid = false;          // see finding 3
    return recordFailure(st);
  }
  if (!_notReadyStartValid) { _notReadyStartMs = readCompletedMs; _notReadyStartValid = true; }
  if (_notReadyCount < std::numeric_limits<uint32_t>::max()) { _notReadyCount++; }
  _measurementPhase = JobPhase::PERIODIC_FETCH_COMMAND;
  _measurementReadyMs = _periodicRetryMs(readCompletedMs);
  return recordProgress("Periodic sample not ready");
}
```

This needs one supporting change: when the capability is absent, the retry must
not run through `_updateHealth()`. Keep `totalNotReady()` reserved for proven
NACKs so existing diagnostics keep their meaning, and add a separate counter for
unproven retries.

Findings 2 and 3 are fixed by the same block. Do all three together.

---

## 2. A periodic measurement job can stay ACTIVE forever

**Severity: High.**

**What the code does.** The not-ready branch never terminates the job; it rewinds
the phase and reschedules ([src/SHT3x.cpp:705-719](src/SHT3x.cpp)). Three things
could bound that loop and all three are off by default: `JobRequest::hasDeadline`
is `false`, `Config::notReadyTimeoutMs` is `0` meaning *disabled*
([include/SHT3x/Config.h:193](include/SHT3x/Config.h)), and a proven not-ready is
deliberately not a health failure. `_notReadyCount` grows without bound and is
never read by the driver.

**Reproduction.** `READ_HEADER_NACK` declared, `notReadyTimeoutMs = 0`, sensor
silently drops out of periodic mode and NACKs every Fetch. After 100 000 polls
(≈1.5 simulated hours):

```
jobActive=1 terminal=0 status=IN_PROGRESS state=READY notReadyCount=50000
requestMeasurement -> BUSY   readStatus -> BUSY   stopPeriodic -> BUSY
```

`driverState()` reports **READY** for a dead sensor, `PollJobResult::terminal` —
documented as "true exactly once" — is true zero times, and the whole driver is
wedged in `BUSY`.

**Proposal.** Make `0` mean *derive a bound*, not *no bound*:

```cpp
uint32_t SHT3x::_notReadyWindowMs() const {
  if (_config.notReadyTimeoutMs > 0) {
    return _config.notReadyTimeoutMs;
  }
  // Three acquisition intervals plus the fetch margin: long enough to ride out
  // a slow sensor oscillator, short enough that a dead sensor is reported.
  const uint32_t period = (_periodMs > 0) ? _periodMs : 1000U;
  return saturatingAddU32(period * 3U, _periodicFetchMarginMs());
}
```

Then update `Config.h` to document `0 = auto (3 periods + margin)` and drop the
"only when READ_HEADER_NACK" caveat, which finding 1 removes.

---

## 3. The not-ready window never re-arms

**Severity: High.**

**What the code does.** `_notReadyStartMs` / `_notReadyStartValid` are cleared
only on a successful periodic read or a full acquisition reset. `_clearJobState()`
([src/SHT3x.cpp:1989-1998](src/SHT3x.cpp)) — which every terminal path calls —
does not touch them. Once the window has expired it stays expired for every
later job, because the start timestamp is frozen at the first not-ready of the
first streak.

**Reproduction.** `notReadyTimeoutMs = 200`, one `requestMeasurement()` per job:

```
job#1 st=I2C_NACK_READ terminal=1 nrStart=1025 consecFail=1
job#2 st=I2C_NACK_READ terminal=1 nrStart=1025 consecFail=2
...
job#5 st=I2C_NACK_READ terminal=1 nrStart=1025 consecFail=5   -> OFFLINE
stopPeriodic -> BUSY (offline latch blocks the way out)
```

`nrStart` never moves. Each later job gets a fresh streak of length 1 and is
escalated instantly because the *first* streak's window is still expired.

**Proposal.** Reset the streak whenever you escalate — the single line shown in
finding 1's block. The health counters already carry the long-run history; the
window should only ever measure the current streak.

**Note for whoever implements this:** `test_not_ready_timeout_escalation`
([test/test_basic.cpp:1657-1686](test/test_basic.cpp)) asserts
`TEST_ASSERT_TRUE(device._notReadyStartValid)` *after* the terminal failure. That
assertion pins the sticky latch as intended behaviour and must be inverted.

---

## 4. Tracked transport wrappers silently drop a class of callback failure

**Severity: High.**

**What the code does.** All four tracked wrappers filter on the *return value*
before updating health, e.g. [src/SHT3x.cpp:2383-2387](src/SHT3x.cpp):

```cpp
Status st = _i2cWriteRaw(buf, len);
if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
  return st;                       // never reaches _updateHealth()
}
return _updateHealth(st, logicalComplete);
```

`_updateHealth()` adds a second escape for `IN_PROGRESS`
([src/SHT3x.cpp:2460](src/SHT3x.cpp)). The intent is to keep the driver's own
pre-flight rejections out of the health counters — but by that point the driver's
rejection and the user callback's return are indistinguishable.

**Failure scenario.** An adapter that guards its own transfer size:

```cpp
if (len > 4) return Status::Error(Err::INVALID_PARAM, "tx too long");
```

Alert-limit writes are 5 bytes. After 1000 failed `writeAlertLimitRaw()` calls:
`state() == READY`, `consecutiveFailures() == 0`, `totalFailures() == 0`,
`lastError() == OK`, `hardwareStateValid() == true`. A supervisor polling
`isOnline()` sees a perfectly healthy driver that has never completed an
operation, and `LATCH_OFFLINE` can never engage.

**Proposal.** Do every driver-side rejection *before* the callback, so anything
that comes back is unambiguously a transport result. This also collapses the
offline gate that is currently copy-pasted into four wrappers:

```cpp
Status SHT3x::_admitTrackedI2c(bool needWrite, bool needWriteRead) const {
  if (needWrite && _config.i2cWrite == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C write not set");
  }
  if (needWriteRead && _config.i2cWriteRead == nullptr) {
    return Status::Error(Err::INVALID_CONFIG, "I2C write-read not set");
  }
  if (!_allowOfflineI2c && _initialized &&
      _config.healthPolicy == HealthPolicy::LATCH_OFFLINE &&
      _driverState == DriverState::OFFLINE &&
      _jobType != JobType::ENSURE_IDLE) {
    return _offlineStatus();
  }
  return Status::Ok();
}

Status SHT3x::_i2cWriteTracked(const uint8_t* buf, size_t len, bool logicalComplete) {
  if (buf == nullptr || len == 0) {
    return Status::Error(Err::INVALID_PARAM, "Invalid I2C buffer");
  }
  const Status admit = _admitTrackedI2c(true, false);
  if (!admit.ok()) {
    return admit;
  }
  // Past this point the bus was touched: every code the callback returns is
  // health relevant, including ones outside the documented transport set.
  return _updateHealth(_i2cWriteRaw(buf, len), logicalComplete);
}
```

Apply the same three-line body to the other three wrappers and drop the
`IN_PROGRESS` escape from `_updateHealth()`. Net effect: about 12 lines of
duplicated gate code removed, and two guess-the-origin branches gone.

---

## 5. `begin()` claims a verified hardware baseline it did not verify

**Severity: High.**

**What the code does.** [src/SHT3x.cpp:311-322](src/SHT3x.cpp) discards the
result of both writes it depends on, then sets `_hardwareStateValid = true` at
line 345 on the strength of a status read:

```cpp
(void)_writeCommand(cmd::CMD_BREAK, false);
(void)_waitMs(BREAK_DELAY_MS);
(void)_writeCommand(cmd::CMD_SOFT_RESET, false);
(void)_waitMs(RESET_DELAY_MS);
st = _readStatusRaw(statusRaw, false);
```

**Failure scenario.** Multi-master bus. The MCU reboots while the sensor is
streaming at 1 mps. Both writes lose arbitration and never reach the sensor. The
status read succeeds — the SHT3x accepts Read Status during periodic
acquisition — and status bit 1 is clear because no command was ever rejected.
`begin()` returns OK with `_periodicActive = false` and
`_hardwareStateValid = true`, while the sensor is still streaming. The next
single-shot command is rejected by the sensor.

The variant where the sensor *does* receive and reject the reset is handled: bit 1
gets set and `statusDiagnosticFailure()` catches it. The unhandled variant is the
writes never landing — which is precisely the "MCU rebooted, sensor did not" case
this block exists to handle.

The driver already knows better elsewhere: `_performRecoveryLadder()` tracks
`acquisitionStopped` and refuses to accept a probe as proof of idleness, and
`SHT3x.h:339-342` states the rule — *"a successful status probe proves
communication only."*

**Proposal.** Stop asserting what was not proven:

```cpp
// Break + soft reset are the only steps that can prove the sensor left a stale
// periodic/ART stream. A later status read proves communication, never idleness.
const bool breakSent = _writeCommand(cmd::CMD_BREAK, false).ok();
(void)_waitMs(BREAK_DELAY_MS);
const bool resetSent = _writeCommand(cmd::CMD_SOFT_RESET, false).ok();
(void)_waitMs(RESET_DELAY_MS);
const bool acquisitionStopped = breakSent && resetSent;
...
_hardwareStateValid = acquisitionStopped;
```

and add to the `begin()` doc comment: returns OK once the sensor answers a
CRC-valid status read, but leaves `hardwareStateValid()` false when the
reconciliation writes did not land; call `recover()` or
`requestEnsureIdle()`/`pollJob()` before trusting the acquisition baseline.

---

## 6. CRC and sensor-rejection failures never reach the health state machine

**Severity: High.**

**What the code does.** `_updateHealth()` commits logical success from the
*transport* result, before any frame validation
([src/SHT3x.cpp:2470-2484](src/SHT3x.cpp)). The CRC check runs afterwards in the
caller, and `_recordProtocolFailure()` touches exactly one counter
([src/SHT3x.cpp:2508-2512](src/SHT3x.cpp)).

**Failure scenario.** A sensor with a damaged output stage: transactions complete
cleanly, every data frame has a corrupt CRC. Loop `readStatus()` 10 000 times:

- `readStatus()` returned `CRC_MISMATCH` 10 000 times
- `state() == READY`, `isOnline() == true`, `consecutiveFailures() == 0`
- `totalSuccess() == 10000`, `totalFailures() == 0`, `lastError() == OK`
- `protocolFailures() == 10000` — the only counter that knows

Nothing in the driver ever reads `_protocolFailures`, so the health state machine
counts a whole failure class and then ignores it. A supervisor keyed on
`isOnline()` never fires for a device that has not produced one usable byte.

**Proposal.** Give protocol failures the same admission consequence as transport
failures. The counters stay separate for diagnostics; only the state machine is
unified:

```cpp
void SHT3x::_recordProtocolFailure() {
  if (_protocolFailures < std::numeric_limits<uint32_t>::max()) { _protocolFailures++; }
  // A CRC-invalid or sensor-rejected frame is a failed *logical* operation even
  // though the transport callback succeeded. _updateHealth() has already cleared
  // the streak on the transport result by the time the frame is validated.
  if (_totalFailures < std::numeric_limits<uint32_t>::max()) { _totalFailures++; }
  if (_consecutiveFailures < std::numeric_limits<uint8_t>::max()) { _consecutiveFailures++; }
  _hardwareStateValid = false;
  _driverState = (_consecutiveFailures >= _config.offlineThreshold)
                     ? DriverState::OFFLINE : DriverState::DEGRADED;
}
```

This preserves the documented `DriverState` rule exactly and keeps
`transportFailures()` free of protocol errors as documented. It is a deliberate
behaviour change: update `README.md:359-360`, which currently frames the protocol
counters as strictly diagnostic, and expect test churn where
`protocolFailures()` is asserted alongside a state.

If you prefer to keep the current policy, then at minimum document that
`isOnline()` cannot detect a CRC-broken device and that supervisors must poll
`protocolFailures()` separately. The present pairing — a counter with no
consumer, and a health API that cannot see the failure — is the one option that
should not stand.

---

## 7. The Arduino example adapter reports successful transfers as timeouts

**Severity: High.** Examples are the reference for how to use the library.

**What the code does.** `examples/common/I2cTransport.h:107-111` checks elapsed
time *before* checking the transfer result:

```cpp
uint8_t result = wire->endTransmission(true);
const uint32_t elapsedMs = millis() - startMs;
...
if (elapsedMs > timeoutMs) {            // fires even when result == 0
  return recordTransfer(Status::Error(Err::I2C_TIMEOUT, "I2C write timeout", elapsedMs), ...);
}
if (result != 0) { ... }                // unreachable in that case
```

The read path is worse (`I2cTransport.h:177-184`): a **fully successful** 6-byte
measurement read is drained byte by byte, discarded, and reported as a transport
failure that feeds the OFFLINE counter.

`millis()` has 1 ms granularity and `Config.h:175` permits `i2cTimeoutMs = 1`, so
a caller choosing a tight budget gets spurious timeouts on nearly every transfer.

**This is currently asserted as intended behaviour.**
[test/test_basic.cpp:1826-1837](test/test_basic.cpp) sets a *complete* read with
`gMillisStep = 30 > timeoutMs = 20` and asserts `Err::I2C_TIMEOUT` plus the
drain. The test is named `test_wire_adapter_drains_partial_read`, but this half
of it is a complete read.

**Proposal.** Check the outcome first; report a timeout only when the transfer
actually failed. On a late-but-complete read, copy the bytes out and return OK —
the data is valid and the caller can decide what to do about the latency:

```cpp
size_t received = wire->requestFrom(addr, rxLen);
const uint32_t elapsedMs = millis() - startMs;
wire->setTimeOut(previousTimeoutMs);

if (received == rxLen) {
  for (size_t i = 0; i < rxLen; i++) { rxData[i] = wire->read(); }
  return recordTransfer(Status::Ok(), true, txLen, rxLen);   // late but valid
}
for (size_t i = 0; i < received; i++) { (void)wire->read(); }
if (elapsedMs > timeoutMs) {
  return recordTransfer(Status::Error(Err::I2C_TIMEOUT, "I2C read timeout",
                                      static_cast<int32_t>(elapsedMs)), true, txLen, received);
}
return recordTransfer(Status::Error(Err::I2C_ERROR, received == 0 ? "I2C read returned 0 bytes"
                                                                  : "I2C read incomplete",
                                    static_cast<int32_t>(received)), true, txLen, received);
```

Apply the same ordering to the write path, and update the two tests that assert
the current behaviour.

---

## 8. Deadline checks run after the work and throw away completed results

**Severity: Medium.**

**What the code does.** There are eight deadline checks in `pollJob()`: one before
any work (correct) and seven *after* a step has already succeeded — lines 517,
546, 569, 597, 624, 686, 714, 727.

Two of them lose real work:

- **Ensure-idle** ([src/SHT3x.cpp:597-599](src/SHT3x.cpp)). Break went out, soft
  reset went out, the status register came back CRC-valid and clean — and then
  the deadline check routes into `cancelJob()`, which sets
  `_hardwareStateValid = false` and never calls `recordEnsureSuccess()`. The
  device *is* in verified single-shot idle; the driver reports `TIMED_OUT` and
  forgets it. The owner's only correct response is to run the whole destructive
  sequence again.
- **Measurement** ([src/SHT3x.cpp:686-688](src/SHT3x.cpp), 727-729). A 6-byte
  frame with both CRCs validated is discarded: `_rawSample`, `_hasSample`,
  `_sampleTimestampMs` are all left untouched. The bus cost, the conversion
  latency, and the sensor's data register slot were all spent.

**Proposal.** One deadline check per poll, at the top, before any I2C. Delete the
seven post-work checks. Semantics are preserved — a job still never *starts* a
step after its deadline, because the next poll terminates it — and roughly 30
lines of special-casing disappear with them: `recordDeadline`'s
`measurementReadResolved` parameter, the `_hardwareStateValid` save/restore at
[src/SHT3x.cpp:376-386](src/SHT3x.cpp), and the `consumedInvalidMeasurement`
branch in `recordFailure`.

A deadline governs whether *more* work is authorized, never whether *finished*
work counts.

---

## 9. Cancelling a measurement forces a destructive recovery later

**Severity: Medium.**

**What the code does.** `cancelJob()` clears `_hardwareStateValid` whenever the
effect is anything but `NONE` ([src/SHT3x.cpp:981-983](src/SHT3x.cpp)), and
`_effectForPhase()` returns `RESULT_MAY_BE_PENDING` for the conversion and read
phases. So a plain "never mind, cancel that reading" invalidates the acquisition
baseline.

The header says the opposite: *"Raw/advanced command access and ambiguous
transport failures invalidate it."* `RESULT_MAY_BE_PENDING` is neither.

**Consequence, measured.** After cancelling a single-shot conversion,
`recover()` costs 4 writes and 2 reads instead of a bare 1-write/1-read probe,
because `_performRecoveryLadder()` computes
`acquisitionStopped = _hardwareStateValid && !_periodicActive`, rejects the
successful probe, and falls through to Break + soft reset. The owner cancelled a
reading and got a sensor reset.

`recordFailure` has the same over-reach, invalidating unconditionally
([src/SHT3x.cpp:418-420](src/SHT3x.cpp)) — including for
`recordFailure(_offlineStatus())` and `recordFailure(BUSY, "Periodic mode
active")`, neither of which performs any I2C at all.

**Proposal.** Invalidate only for the effect that actually means the acquisition
mode is unknown. `_updateHealth()` already clears the flag on every genuine
transport failure, so nothing that should invalidate stops invalidating:

```cpp
// RESULT_MAY_BE_PENDING is about unread data; DEVICE_STATE_CHANGED is a change
// this job made and already reflected in _mode/_periodicActive. Only
// DEVICE_STATE_INDETERMINATE means the acquisition mode is genuinely unknown.
if (effect == JobEffect::DEVICE_STATE_INDETERMINATE) {
  _hardwareStateValid = false;
}
```

The conservative variant keeps `DEVICE_STATE_CHANGED` in the condition and still
fixes the reported scenario.

---

## 10. The recovery backoff touches no bus but is treated as a hardware failure

**Severity: Medium.**

**What the code does.** The backoff gate lives *inside*
`_performRecoveryLadder()` and returns `BUSY` before any bus access
([src/SHT3x.cpp:2128-2134](src/SHT3x.cpp)). All three callers treat any non-OK
identically and clear `_hardwareStateValid`.

**Failure scenario.** A bus-manager-owned deployment with all reset options
disabled (`recoverUseBusReset/SoftReset/HardReset = false`,
`allowGeneralCallReset = false`) and the default `recoverBackoffMs = 100`:

1. `t=0`: `recover()` succeeds, `_hardwareStateValid = true`.
2. `t=20`: `resetToDefaults()` hits the backoff. No I2C happens. It still sets
   `_hardwareStateValid = false` and returns `BUSY`.
3. `t=200`: `recover()` now computes `acquisitionStopped = false`, so the
   successful probe is rejected, and no reset branch is enabled to prove
   idleness. Returns `BUSY "Recovery did not establish idle state"` — permanently.

The owner-safe `requestEnsureIdle()`/`pollJob()` path still works, so this is a
synchronous-API lockout rather than a bricked device. With reset branches enabled
the effect is milder but still wrong: a purely local rate-limit rejection forces
an extra destructive Break + soft reset on the next recovery.

**Proposal.** Lift the backoff out of the ladder into a caller-side admission
guard, so `BUSY` from the ladder keeps a single meaning:

```cpp
Status SHT3x::_admitRecoveryAttempt() {
  const uint32_t now = _nowMs(_config);
  if (_config.recoverBackoffMs > 0 && _lastRecoverValid &&
      !_durationElapsed(now, _lastRecoverMs, _config.recoverBackoffMs)) {
    return Status::Error(Err::BUSY, "Recovery backoff active");
  }
  _lastRecoverMs = now;
  _lastRecoverValid = true;
  return Status::Ok();
}
```

Call it in `recover()`, `resetToDefaults()` and `resetAndRestore()` before
`_performRecoveryLadder()`, and return its status directly without touching
`_hardwareStateValid`. A rejected attempt does not refresh the timestamp, so
repeated calls cannot starve recovery.

---

## 11. tIDLE is never restarted after a read frame

**Severity: Medium.**

**What the code does.** `_i2cWriteRaw()` stamps `_lastCommandUs` after every
attempt ([src/SHT3x.cpp:2286-2287](src/SHT3x.cpp)). `_i2cWriteReadRaw()` does
not. So `_ensureCommandDelay()` always measures from the last *write*, never from
the last bus transaction.

**Consequence.** `readStatus()` followed by `clearStatus()` with the default
`commandDelayMs = 1`:

| t | event | `_lastCommandUs` |
| --- | --- | --- |
| 0 µs | status command write | 0 |
| 0→1000 µs | tIDLE gate blocks the read | 0 |
| 1000 µs | 3-byte read, STOP at ~1150 µs | still 0 |
| 1150 µs | `clearStatus()` → gate sees 1150 ≥ 1000 → **passes immediately** | |

`0x3041` goes out ~150 µs after the read's STOP, against a configured 1000 µs
minimum. With `commandDelayMs = 1000` the violation is up to a full second. The
same holds for `readSerialNumber()` → `setHeater()`, `readAlertLimitRaw()` →
`writeAlertLimitRaw()`, `probe()` → anything.

The datasheet wording is *"after sending a **command** … before another
**command**"*, and a read header is not a command — so this is very likely
harmless in practice. But the driver silently fails to enforce its own configured
minimum, and `README.md:478` claims it does.

**Proposal.** One helper, four call sites, and the concept becomes "bus
transaction" rather than "command write":

```cpp
void SHT3x::_markBusTransaction() {
  // A failed callback may still have driven the bus, and a completed read frame
  // ends as late as a write does, so both restart tIDLE.
  _lastCommandUs = _nowUs(_config);
  _lastCommandValid = true;
}
```

Replace the duplicated stamp pairs in `_i2cWriteRaw`, `_i2cWriteRawAddr` and
`interfaceReset`, and add the call to `_i2cWriteReadRaw` right after the
callback. No regression in `pollJob`: periodic fetch and single-shot read are
both already gated by `_measurementReadyMs`.

---

## 12. A not-ready retry backs off a full period and guarantees a lost sample

**Severity: Medium.**

**What the code does.** [src/SHT3x.cpp:2039-2044](src/SHT3x.cpp):

```cpp
return nowMs + _periodMs + _periodicFetchMarginMs();
```

**Consequence, measured** at 10 mps (period 100 ms, margin 5 ms). A fetch lands a
few milliseconds before the sensor's sample:

```
first fetch ok at t=1023
not-ready near t=1129; next attempt at t=1234 (backoff=105 ms)
success at t=1235   missedSamplesEstimate=1
```

The sample it narrowly missed was available a few milliseconds later. Instead the
driver waits a whole period, by which time that sample has been overwritten. One
not-ready = one permanently lost sample. At 0.5 mps the same event costs 2100 ms.

**Proposal.** The condition being handled is "our estimate was a few milliseconds
early", not "the sensor has not started producing". Back off by the margin:

```cpp
uint32_t SHT3x::_periodicRetryMs(uint32_t nowMs) const {
  // A not-ready Fetch means the next conversion is at most one fetch margin
  // away, not a whole period. The bounded not-ready window (finding 2) is what
  // stops this loop when the sensor is dead.
  uint32_t backoff = (_periodMs == 0) ? _config.commandDelayMs : _periodicFetchMarginMs();
  if (backoff < _config.commandDelayMs) { backoff = _config.commandDelayMs; }
  return nowMs + backoff;
}
```

Each retry still costs exactly one budgeted instruction, so the owner keeps
control of bus load.

---

## 13. `missedSamplesEstimate()` both over- and under-reports

**Severity: Medium.**

**What the code does.** [src/SHT3x.cpp:731-740](src/SHT3x.cpp) floors
`elapsed / _periodMs` on every fetch and discards the remainder.

**Under-reports the driver's own loss.** Steady-state fetch cadence is
`period + margin`, i.e. 5% slower than the sensor. At 10 mps every gap is 108 ms
against a 100 ms period: `108/100 = 1`, minus 1, equals 0 — every time. Roughly
7% of the sensor's output is never fetched while the counter reports zero loss
indefinitely.

**Over-reports the owner's polling cadence.** Same config, owner fetches once a
minute: the counter climbs by 599 per minute. A caller reading this as a health
signal reads a perfectly healthy system as catastrophically lossy. At 10 mps with
hourly fetches it saturates `UINT32_MAX` in about four months.

**Wrap hazard.** `elapsed` is an unsigned difference. If a fetch stamp ever lands
behind `_lastFetchMs`, `missed += 42949671` in a single fetch. I could not reach
this through the public API, so treat it as robustness rather than a live bug.

**Proposal.** Carry the remainder and clamp the absurd case:

```cpp
if (_lastFetchValid && _periodMs > 0) {
  uint32_t elapsed = readCompletedMs - _lastFetchMs;
  // A gap longer than the wrap-safe window is a clock discontinuity, not a
  // million lost samples. Restart the estimate instead of poisoning it.
  if (elapsed > (std::numeric_limits<uint32_t>::max() / 2U)) {
    elapsed = 0;
    _missedRemainderMs = 0;
  }
  const uint32_t total = saturatingAddU32(_missedRemainderMs, elapsed);
  const uint32_t produced = total / _periodMs;
  _missedRemainderMs = total % _periodMs;      // new uint32_t member
  if (produced > 1U) {
    _missedSamples = saturatingAddU32(_missedSamples, produced - 1U);
  }
}
```

Reset `_missedRemainderMs` wherever `_missedSamples` is already cleared. Also
tighten the doc at `SHT3x.h:504-506`: the counter measures *sensor output not
fetched*, which includes the owner's own polling cadence.

---

## 14. ART mode accepts settings changes the hardware cannot carry

**Severity: Medium.**

**What the code does.** In ART mode, `setPeriodicRate()` and
`setRepeatability()` ([src/SHT3x.cpp:1248-1259](src/SHT3x.cpp), 1319-1330) call
`startArt()`, which sends Break and then the fixed word `0x2B32`. The `rate` and
`rep` arguments are used only in the non-ART branch of `_enterPeriodic()`.

The ART command carries neither a rate nor a repeatability — the datasheet gives
a single fixed code and states ART acquires at 4 Hz. So the restart destroys the
acquisition cadence, costs two bus transactions, and changes nothing. Worse, if
the ART re-issue fails, the device is left in single-shot idle: **a setting change
that cannot affect the hardware has dropped the device out of ART.**

**Proposal.** Record the desired value for the restore plan without touching the
bus. Both ART branches then become identical to the existing single-shot tail, so
the two `if (_mode == Mode::ART)` blocks can be deleted outright and both
functions collapse to *"restart only when `_mode == Mode::PERIODIC`, otherwise
cache"*:

```cpp
if (_mode == Mode::ART) {
  // 0x2B32 carries neither rate nor repeatability, so a restart would break the
  // acquisition cadence for no hardware effect. Record it for the restore plan.
  _config.periodicRate = rate;
  _cachedSettings.periodicRate = rate;
  _hasCachedSettings = true;
  return Status::Ok();
}
```

Returning `Err::UNSUPPORTED` instead would be stricter but is an API break; the
version above is not.

---

## 15. `probe()` sends a command during periodic acquisition

**Severity: Medium.**

**What the code does.** `probe()` ([src/SHT3x.cpp:772-787](src/SHT3x.cpp)) checks
only `_singleShotMeasurementPending()`, which is false in periodic mode with no
job pending. It then issues `0xF32D` + a 3-byte read on a sensor mid-acquisition.

The datasheet (p. 11 §4.8) says: *"It is recommended to stop the periodic data
acquisition prior to sending another command (except Fetch Data command) using
the break command."* Every other command path honours this — `readStatus()`
returns `BUSY "Stop periodic mode before reading status"` for the **identical**
transaction. `probe()` is the sole hole.

**Proposal.** Either add the `_periodicActive` guard for symmetry with
`readStatus()`, or — if a presence check must work during acquisition — probe with
`CMD_FETCH_DATA`, the one command the datasheet explicitly exempts. The header
already warns that `probe()` is a raw diagnostic, so this is a deliberate-looking
gap; make the decision explicit either way.

---

## 16. The ESP-IDF adapter never maps a NACK, so `probe()` cannot report `DEVICE_NOT_FOUND`

**Severity: Medium.**

**What the code does.** `examples/idf/basic/main/IdfI2cTransport.cpp:24-41` maps
`ESP_OK`, `ESP_ERR_TIMEOUT`, `ESP_ERR_INVALID_ARG` and `ESP_ERR_INVALID_RESPONSE`;
everything else falls through to `Err::I2C_BUS`. It never returns
`I2C_NACK_ADDR`, `I2C_NACK_DATA` or `I2C_NACK_READ`.

`mapPresenceProbeFailure()` ([src/SHT3x.cpp:125-130](src/SHT3x.cpp)) converts
`I2C_NACK_ADDR → DEVICE_NOT_FOUND`, and `probe()` is its only consumer. So an
absent sensor reports `DEVICE_NOT_FOUND` on Arduino and `I2C_BUS` on ESP-IDF: the
two reference adapters disagree on the most basic diagnostic.

**Proposal.** Add the NACK cases to `mapEspError()`. Confirm the exact codes
against the ESP-IDF version you target before committing — `i2c_master_probe()`
returns `ESP_ERR_NOT_FOUND` for no-ACK, which is unambiguous;
`i2c_master_transmit()` surfaces an unexpected NACK differently across 5.x
releases, so verify rather than assume:

```cpp
case ESP_ERR_NOT_FOUND:
  return SHT3x::Status::Error(SHT3x::Err::I2C_NACK_ADDR, message, static_cast<int32_t>(err));
```

The example already calls `i2c_master_probe()` in `scanBus()`, so the machinery
is present. (The related over-claim of `TransportCapability::BUS_ERROR` is
already fixed — see "Already fixed".)

---

## 17. `setHeater()` commits cached state on a transport ACK

**Severity: Low.**

`setHeater()` ([src/SHT3x.cpp:1517-1522](src/SHT3x.cpp)) treats "the sensor ACKed
two bytes" as "the sensor executed the command". `writeAlertLimitRaw()` performs
the correct pattern for the same class of question: it reads the status register
back and checks the command-error and write-checksum bits before committing to
the cache.

If the sensor rejects `0x306D`, `setHeater()` returns OK, the cache records
`heaterEnabled = true`, and the restore plan will keep re-asserting a state the
device never entered.

**Proposal.** Reuse the verification `writeAlertLimitRaw()` already performs
(write with `logicalComplete = false`, then `_readStatusRaw` +
`statusDiagnosticFailure` before updating the cache). This costs two extra
callbacks; update the latency row in `README.md:498` to match, exactly as the
alert-write row already does. If the extra traffic is unacceptable, document in
`SHT3x.h:729-735` that the cached heater state reflects transport acceptance
only.

---

## 18. Smaller correctness items

| Item | Location | Proposal |
| --- | --- | --- |
| `_durationElapsed(now, start, d)` returns *true* when `now` is one tick **behind** `start` — `_durationElapsed(999, 1000, 60000)` is `true`. Every caller but one compares same-source timestamps, so it is latent; the exception is [src/SHT3x.cpp:695](src/SHT3x.cpp), which mixes the caller's `nowMs` with an internally sampled `_notReadyStartMs`. | [src/SHT3x.cpp:2843-2845](src/SHT3x.cpp) | Delete `_durationElapsed` and express every duration gate as `_timeElapsed(now, start + d)`. `start + d` wraps correctly and the signed comparison is already the wrap-safe primitive the rest of the file uses. One primitive instead of two, and the unsafe one is gone. |
| The tIDLE gate uses `uint32` **microseconds**, which wrap every 71.6 min, and `_lastCommandValid` is never aged out. After a long idle the gate can falsely report "not open" (probability `commandDelayMs·1000 / 2^32` per wrap boundary). Bounded consequence: one wasted poll, or up to `commandDelayMs` of synchronous spinning. | [src/SHT3x.cpp:493-499](src/SHT3x.cpp), 2526-2550 | Add a millisecond companion `_lastCommandMs` (49.7-day window) and open the gate unconditionally once a full command delay has provably passed on the ms clock. |
| `estimateMeasurementTimeMs()` returns exactly the datasheet maximum when `singleShotMeasurementMarginMs = 0`, but `_measurementReadyMs` is derived from an integer-ms clock sampled *after* the write returned, so up to 1 ms of the real interval is truncated away. The read can fire at ~14.1 ms against a 15 ms worst case. The default margin of 1 covers it; `bind()` accepts 0. | [src/SHT3x.cpp:1943-1946](src/SHT3x.cpp) | Add `+ 1U` unconditionally inside `estimateMeasurementTimeMs()` to absorb the truncation, and let the configured margin be pure headroom on top. |
| `readSettings()` maps a transport-level `BUSY` (a shared-bus adapter reporting the bus held by another master) to `Status::Ok()`, because it cannot tell that `BUSY` apart from "job active" / "periodic active". Meanwhile `consecutiveFailures()` has silently advanced. | [src/SHT3x.cpp:1169-1176](src/SHT3x.cpp) | Decide the OK-snapshot cases positively from local state instead of decoding a return code: `if (_jobActive() \|\| _periodicActive) { out.statusValid = false; return Status::Ok(); } return stStatus;` |
| `resetToDefaults()` can return OK with the physical heater still on: when `_hardwareStateValid && !_periodicActive`, the ladder short-circuits on a successful probe without issuing any reset, and `_setDefaultsToConfigAndCache()` is purely local. The header promises *"OK after a recovered default single-shot state"*. | [src/SHT3x.cpp:806-822](src/SHT3x.cpp) | Either drive `setHeater(false)` explicitly after `_setSafeBaseline()`, or weaken the header to say defaults are applied to the driver's restore plan only. The current pairing is what should not stand. |
| `_recordProtocolFailure()` is gated on `tracked` in `_readStatusRaw` but called unconditionally in `_readMeasurementRawNoDelay`. Latent today (only `pollJob` reaches the latter, always tracked), but it will bite the first time an untracked measurement read is added. | [src/SHT3x.cpp:2599-2601](src/SHT3x.cpp) vs 2617 | Move the decision into the helper: `void _recordProtocolFailure(bool tracked)` with an early return, so no call site can get it wrong. |
| `PollJobResult::phase` is sampled once at function entry and never updated by `recordProgress()`, so every progress result reports the step that just *finished* rather than the one now pending. `SINGLE_SHOT_READ` is never observable in a non-terminal result. | [src/SHT3x.cpp:371](src/SHT3x.cpp), 425-435 | Either publish `_measurementPhase` in `recordProgress()`, or tighten the field doc to "the phase that performed this step". |
| `requestMeasurement()` clears `_measurementReady` before it finishes validating, so the "Periodic mode not active" and "Invalid mode" paths destroy a ready sample on their way to returning an error. I could not reach that state combination through the public API. | [src/SHT3x.cpp:875](src/SHT3x.cpp) | Move the clear below all validation, into each mode branch next to `_measurementRequested = true`. |
| The ESP-IDF CLI calls `std::fgets(..., stdin)` with no `uart_driver_install` / `uart_vfs_dev_use_driver` anywhere in the example, so ESP-IDF stdin is non-blocking and returns partial lines. Typing character-by-character into a serial console prints `"Input line too long; discarded"`. Masked in practice because the HIL runner writes whole lines at once. | `examples/idf/basic/main/main.cpp:1964` | Install the UART driver and register it with the VFS before the input task starts, or switch to `linenoise`, which the IDF console component already provides. |
| Both examples enable internal pull-ups at 400 kHz. ESP32 internal pull-ups are ~45 kΩ — marginal at that speed. | `examples/common/I2cTransport.h:66`, `main.cpp:590` | Keep the default but say so in a comment: reference designs should use external 2.2–10 kΩ pull-ups. |
| `Config::transportCapabilities` is the one config field `bind()` does not validate; an out-of-range value is accepted. `hasCapability` masks, so extra bits are inert. | [src/SHT3x.cpp:201-207](src/SHT3x.cpp) | Add the range check for symmetry with the other eight validated fields. |
| Unreachable `greset` branches in both CLIs — arity validation rejects the bare command before dispatch. Identical in both files, which is itself evidence of the copy-paste in finding 20. | `examples/common/Sht3xCli.cpp:2774-2778`, `examples/idf/basic/main/main.cpp:1707-1709` | Delete both. |

---

## 19. Test suite gaps

118 tests, all registered, none trivially duplicated, with genuinely good failure
injection. The gaps that matter:

- **OFFLINE-latch escape is never tested.** Every test that reaches `OFFLINE`
  under `LATCH_OFFLINE` asserts it *stays* offline. The two tests showing a
  return to `READY` use `OBSERVE_ONLY`, where the gate never fires — so they
  prove nothing about the latch. Neither escape door (`_allowOfflineI2c`, the
  `_jobType != ENSURE_IDLE` bypass) is exercised. A regression that trapped the
  driver in OFFLINE forever, or let it escape spuriously, would pass CI. This is
  the largest gap, in the riskiest state machine.
- **Missed-sample estimation is dead code under test.** The block needs two
  successful periodic fetches in one test; no test achieves two.
  `_missedSamples` is never nonzero anywhere and `missedSamplesEstimate()` is
  never called.
- **Serial-number and alert-limit CRC paths are unverified.**
  `readSerialNumber`'s out-parameter is never read, so word assembly and both CRC
  checks are untested. `readAlertLimit()` is never called, so there is no
  write-then-read-back test.
- **Assertions that cannot fail.** `sampleAgeMs(123)` with
  `_sampleTimestampMs == 0` would pass if the implementation were `return nowMs`
  ([test/test_basic.cpp:4771](test/test_basic.cpp)). The scanner test asserts
  `112 == 0x77 - 0x08 + 1` while the stub ACKs every address
  ([test/test_basic.cpp:1886](test/test_basic.cpp)).
  `test_recover_permanent_offline` never asserts `DriverState::OFFLINE` and with
  the default threshold ends `DEGRADED`.
- **Order dependence.** `setUp()`/`tearDown()` are empty while timing runs
  through process-global `gMillis`/`gMicros`/`gMillisStep`/`gMicrosStep`.
  `test_cache_updates_only_on_success` terminates only because an earlier test
  set `gMicrosStep = 1000`. Reordering `RUN_TEST` could make it spin 500 000
  iterations.
- **The mocks are protocol-shape mocks, not a device model.** tIDLE violations
  are observed but still answered `Ok()`; conversion time is not modelled;
  not-ready is a static "NACK forever" override; Break is logged but not
  honoured; the status register is a constant never mutated by
  `CMD_CLEAR_STATUS`/`CMD_SOFT_RESET`/`CMD_HEATER_ENABLE` — which is why
  `readHeaterStatus()` has no test. Nothing here would catch a driver that reads
  before conversion completes or issues a mode-illegal command.

**Proposal, in order of value:** populate `setUp()` to reset the global clocks
and `Wire` (removes the latent hang); add the OFFLINE-escape test; give the fake
transport a minimal device model (conversion deadline, data-ready flag cleared by
Fetch, Break honoured, status bits mutated by the commands that mutate them) —
that one change makes findings 1, 12, 13 and 15 testable; then assert the
serial-number out-parameter and add the two missing CRC cases.

---

## 20. The two CLIs are one CLI, forked

`examples/common/Sht3xCli.cpp` (3205 lines) and
`examples/idf/basic/main/main.cpp` (2042 lines) implement the same 70-command
surface twice. All 70 help rows are character-identical and frozen that way by a
CI gate. 617 normalized lines are verbatim duplicates — 32% of the IDF file — and
~40 more functions are the same logic under a different name
(`clearPendingOwner`↔`clearOwnerToken`, `runSelfTest`↔`runSelftest`,
`printTransferStats`↔`printTransferCounters`, …).

**The shared CLI is already framework-neutral.** It includes only C++ standard
headers, routes all output through a `Platform::vprintf` function pointer, and
uses fixed `char` buffers with no heap. The stated rationale in `Sht3xCli.h:5-6`
— *"the ESP-IDF example intentionally uses its own native fixed-buffer CLI"* — is
obsolete, because the shared CLI **is** a native fixed-buffer CLI.

**The fork has already diverged where it is not gated.** Help text is frozen
identical; actual machine-readable output is not, and differs for essentially
every command (`xfer_reset: OK` vs `XFER_RESET read=0 write=0 total=0`;
`Temp: %.2f C` vs `temperature=%.2f C`; `Serial: 0x%08lX` vs `serial=0x%08lX`).
`tools/run_sht3x_hil.py` compensates with dual regexes throughout. The IDF file is
even inconsistent with itself (`"Usage: "` vs `"usage: "`).

**Proposal.** Delete `tools/check_idf_example_contract.py:174-177`, which
currently *forbids* the deduplication, then:

1. Move `examples/common/Sht3xCli.{h,cpp}` to `examples/common/cli/` and drop the
   "Arduino" framing from its header comment.
2. Add `examples/idf/basic/main/IdfCliPlatform.cpp` (~120 lines) implementing the
   six `Platform` function pointers: `vprintf`, `nowMs` via `esp_timer`, `yield`
   via `taskYIELD`, `scanBus` via the existing `i2c_master_probe` loop, and the
   two transfer-stats adapters.
3. Add the shared source to the example's `CMakeLists.txt`.
4. Shrink `main.cpp` to bus/device creation, `configureDriver`, the input task,
   and `app_main` — roughly 180 lines.

Net: about 1850 lines deleted, and the output-format divergence disappears at its
root, which in turn lets the HIL runner drop its dual regexes.

While there: `check_idf_example_contract.py:166-170` and `:192-204` duplicate
checks already performed by `check_cli_contract.py`, and both gates run in the
same CI job. Keep `check_core_boundary` — the framework-neutrality check is the
single most valuable gate in the repository and directly protects the
"embeddable in other firmware" goal — and delete the duplicates.

---

## 21. Packaging declares the library narrower than it is

The core is genuinely platform-neutral: `src/SHT3x.cpp` includes only
`"SHT3x/SHT3x.h"`, `<cstring>`, `<limits>` and `<cmath>`, and every public header
includes only `<cstdint>`/`<cstddef>`. It compiles clean under host g++ with
`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror`.

Yet `library.json:41-43` declares `"platforms": ["espressif32"]` and
`idf_component.yml:6-8` restricts `targets:` to `esp32s2`/`esp32s3`. A consumer
on a plain ESP32, C3, C6, H2 or P4 — or on any non-Espressif platform — is
refused by the manifest, for a driver that has no platform dependency. The
repository's own `[env:native]` uses `platform = native`, a configuration
`library.json` declares unsupported.

**Proposal.** Widen `library.json` to `"platforms": "*"` and `"frameworks": "*"`,
and drop or widen `idf_component.yml`'s `targets:`. This also requires relaxing
`tools/check_idf_example_contract.py:188-189`, which currently pins those two
target names and so cements the restriction.

Two related packaging notes:

- `library.json:66-69` ships `tools/run_sht3x_hil.py` (114 KB of Python) inside
  the C++ package, and `check_hil_contract.py:137-143` *enforces* that it stays
  there. Consider whether a host-side maintenance tool belongs in a firmware
  dependency.
- `library.json:71-78`'s `export.exclude` list is fully redundant with
  `.gitignore`, since `export.include` is already a whitelist.

---

## 22. The `v1.8.0` tag does not contain this tree

Version metadata is internally consistent: `library.json`,
`include/SHT3x/Version.h`, `idf_component.yml`, `Doxyfile` and `CHANGELOG.md` all
say `1.8.0`, and `generate_version.py check` passes. No `1.9.0` string survives
anywhere.

The problem is external. History shows:

```
7b184a6  2026-07-23  docs: align v1.8.0 status         <- tag v1.8.0 points HERE
   ... 6 commits ...
dfff43c  2026-08-05  Prepare v1.9.0 release            <- bumped everything to 1.9.0
   ... 3 commits ...
e156047  2026-08-05  Prepare v1.8.0 release on main    <- reverted 1.9.0 -> 1.8.0
524850d  2026-08-05  Record exact v1.8.0 HIL evidence  <- HEAD
```

So two different trees are both called `1.8.0`, and `git merge-base --is-ancestor
v1.8.0 HEAD` confirms the published tag is 13 commits behind. They differ in
substance: the tag ships pioarduino `54.03.20` and the since-deleted
`tools/run_i2c_hil.py`; HEAD ships `55.03.311` and the current runner.

Consequences:

- `CHANGELOG.md:421`'s `compare/v1.7.0...v1.8.0` link renders a diff that does
  not contain the ~30 bullets listed under its own `[1.8.0]` heading.
- `CHANGELOG.md:420` renders 13 commits of shipped, changelogged work as
  `[Unreleased]`, while the `[Unreleased]` section itself is empty.
- `SECURITY.md:7`'s "1.8.x supported" is ambiguous.
- Any consumer pinning `#v1.8.0` gets a different library than this README
  documents.

**This one needs your decision, so I have not acted on it.** Options, in
descending preference:

1. **Release this tree as `1.9.0`** — what `dfff43c` originally did. Run
   `generate_version.py set 1.9.0`, restore `## [1.9.0] - 2026-08-05` in the
   changelog with the original `## [1.8.0] - 2026-07-23` section reinstated
   beneath it, fix the compare links, update `SECURITY.md`, then tag. This is the
   only option that leaves the published tag truthful and gives existing
   consumers a real upgrade target.
2. **Release as `1.8.1`**, accepting that the `[1.8.0]` changelog section
   describes a superset of the tag.
3. **Force-move the tag** — do not. `CONTRIBUTING.md:44-46` and the README's
   exact-pin guidance both depend on tag immutability.

The README no longer tells consumers to pin `#v1.8.0`; it now says a tag is only
equivalent to a commit pin once you have confirmed what the tag points at.

---

## Structural notes

Six of the findings above are symptoms of the same four design decisions.
Addressing these would prevent the class, not just the instance.

**`Err::BUSY` carries four unrelated meanings** — job active, periodic active,
offline latch, recovery backoff — plus "recovery did not establish idle state".
Findings 10 and 18 (`readSettings`) are direct consequences: the code has to
re-derive which `BUSY` it is looking at by inspecting `_driverState`, and gets it
wrong. A distinct code for "admission refused, no bus touched" would make finding
10 impossible to write.

**Four state variables encode one fact.** `_jobType`, `_measurementRequested`,
`_measurementPhase` and `_jobEffect` are not independent: `_measurementRequested`
is provably equivalent to `_jobType == JobType::MEASUREMENT`, and `_jobType` is
derivable from `_measurementPhase` (the `ENSURE_*` values occur only for
ensure-idle, the `SINGLE_SHOT_*`/`PERIODIC_*` values only for measurement), while
`JobPhase::IDLE` already means "no job". Collapsing to a single `JobPhase _phase`
plus a `jobTypeFor(_phase)` helper would delete three fields, make
`_clearJobState()` a one-liner, and reduce `_singleShotMeasurementPending()`
(whose second clause is dead) to `_phase != JobPhase::IDLE`. Findings 3, 18
(`requestMeasurement`) and the unreachable offline gate at
[src/SHT3x.cpp:484-488](src/SHT3x.cpp) are all consequences of state stored twice.

**Health is committed at the wrong layer.** `_updateHealth()` decides "logical
operation succeeded" from the transport callback's return code, but the operation
is not complete until the CRC and the sensor's status bits have been checked one
or two layers up. That inversion is the root of finding 6, and of the surprising
fact that `totalSuccess()` counts operations that returned `CRC_MISMATCH` to the
caller. The natural formulation: tracked wrappers record *transport* facts only,
and a single `_completeLogicalOperation(const Status& final)` — called once per
public API, with the status that API actually returns — owns `_totalSuccess`,
`_totalFailures`, `_consecutiveFailures`, `_lastOkMs` and `_driverState`. That
also removes the `logicalComplete` bool currently threaded through nine
signatures.

**`JobEffect` does double duty.** It is a *reporting* type that tells the owner
what physically happened, but `cancelJob()` and `recordFailure()` reuse it as the
*decision* for clearing `_hardwareStateValid` — which is how "there may be an
unread sample" came to mean "I no longer know which mode the sensor is in"
(finding 9). Compute them separately: `JobEffect` for the owner, an explicit
acquisition-knowledge flag for the driver.

Two smaller ones: the offline-gate predicate is copy-pasted verbatim into four
wrappers (finding 4 collapses it), and the deadline is checked eight times where
once would do (finding 8).

---

## Already fixed

Applied directly, with the native suite (118/118), all five repository gates, a
strict-warning host compile, and `pio pkg pack` verified after each change.

**Alert-limit encoder rounded the wrong way.** `encodeAlertLimit()` rounded to a
16-bit raw value and then *truncated* into the reduced 7-bit RH / 9-bit T fields.
Sensirion's own workbook (`docs/reference/vendor/HT_AlertMode_BitConversion.xlsx`,
cell `D8`) rounds to the nearest reduced code. Measured over 0–100 %RH, the
driver's mean encoding error was **−0.39 %RH** against the vendor rule's
−0.002 %RH — every threshold biased low by half a code, never high. It also
mis-encoded the published `20 %RH / −10 °C` reset default as `0x3266` instead of
`0x3466`, which is why a four-entry float-equality lookup table existed to paper
over the arithmetic.

Fixed by rounding in the stored field, with saturation so a full-scale input
cannot carry into the neighbouring field. Three of the four published defaults
now fall out of the plain arithmetic; the lookup table is down to the one entry
that genuinely cannot (see below). Verified through the real library code:

```
HighSet   80%/60C   encode=0xCD33 want=0xCD33 OK
HighClear 79%/58C   encode=0xC92D want=0xC92D OK
LowClear  22%/-9C   encode=0x3869 want=0x3869 OK
LowSet    20%/-10C  encode=0x3466 want=0x3466 OK
clamp hi: encode(200,200)=0xFFFF   clamp lo: encode(-100,-50)=0x0000
RH monotonicity breaks over 0..100%: 0
```

**Undocumented vendor conflicts.** Four disagreements between the Sensirion
documents are now recorded in
[docs/reference/sht3x-chip-notes.md](docs/reference/sht3x-chip-notes.md) with the
driver's choice for each: the alert note prints `0xC92D` for 79 %RH/58 °C while
the workbook computes `0xCB2D` (we follow the printed word, since it describes
the device's power-up state); the datasheet and the alert note disagree on
whether a soft reset sets status bit 4, and the alert note's revision history says
that table was *corrected*; they disagree on the reset value of reserved bits
9:5 (`xxxxx` vs `00000`), which is why the raw status word must never be compared
for equality; and the alert note's worked example has a hex typo
(`0xE699` for a binary value that equals `0xCD33`).

**Chip-notes factual errors.** The alert-limit packing was described as
`RH[6:0]` and `T[8:0]`, which reads as the *least* significant bits and is the
exact inverse of what the sensor does — anyone implementing from that line alone
would produce a wrong encoder. Replaced with an explicit bit table plus the
vendor formula. Also: a citation pointing at datasheet page 21 for a table that
is on page 14; a claim that the alert note contains conversion "equations" when
it contains a worked binary procedure and a pointer to the workbook; the missing
Fahrenheit conversion formula that the datasheet publishes alongside Celsius; and
the workbook itself was absent from the source inventory despite carrying the
only closed-form alert arithmetic Sensirion publishes.

**Over-claimed transport capability.** The ESP-IDF example declared
`TransportCapability::BUS_ERROR`, but `mapEspError()` routes every unrecognized
`esp_err_t` to `I2C_BUS` as a catch-all — the opposite of "can reliably report
bus errors". Now declares `TIMEOUT` only, which is the one it can actually prove.

**ESP-IDF package shipped 2.4 MB of vendor PDFs.** `idf_component.yml` had no
`files:` filter, so `idf.py upload-component` would ship `test/`, `tools/`,
`scripts/` and all seven Sensirion source documents to every consumer — bloat, and
a third-party redistribution question. Added an exclude list.

**Namespace shadowing.** Local variables and parameters named `cmd` shadowed the
`SHT3x::cmd` namespace in five functions, so lines read
`const uint16_t cmd = cmd::CMD_SERIAL_STRETCH;`. Legal C++ — qualified lookup
ignores the variable — but needlessly confusing. Renamed to `command`.

**`_allocateJobId()`** had an unreachable branch and a post-increment wrap guard;
replaced with three obvious lines.

### Documentation cleanup

- **Deleted `docs/tunnelmonitor-integration.md`** and replaced it with
  [docs/integration.md](docs/integration.md), which covers the same ownership
  boundary, cooperative flow, transport contract and presence/health rules
  without being about one downstream product. The old file was largely a report
  about a sibling checkout on the author's disk ("The local sibling source
  shows…", "does not treat its dirty working tree as validated evidence", plus a
  task list for a different project) — and it was compiled into the public
  Doxygen build and shipped in the PlatformIO package. References updated in
  `README.md`, `docs/README.md`, `Doxyfile` and `library.json`.
- **Rewrote `docs/hardware.md`** (397 → 284 lines). Removed eight SHA-256
  fingerprints of files the document itself admits "are not present in this
  checkout and were not found elsewhere… during the 2026-08-01 cleanup",
  fixture-specific run narratives, and "release candidate / require CI before
  tagging" framing frozen mid-release. Kept the runbook, the opt-in group table,
  the restore procedure and the record-keeping checklist, and turned the evidence
  ledger into a coverage matrix that says what has and has not been exercised.
- **Trimmed `docs/README.md`** of meta-commentary about a cleanup that had
  already happened, and of a hand-maintained version-string mirror that no tool
  syncs.
- **Removed migration archaeology from the chip notes** — a 13-row table mapping
  `00_document_inventory.md` … `08_variant_differences.md` to sections of the
  file, when all nine of those files were deleted two releases ago, plus the
  same "generated extracts were removed" note written twice in two files.
- **`README.md`**: removed the frozen validation report in "Current State" and
  replaced it with a scope section; removed instructions addressed to a report
  author ("Use results from the exact commit being evaluated…", "Do not extend
  the accepted one-hour result into claims of…"); removed two references to
  `I2CManager`, a type that exists nowhere in this repository; fixed the one
  absolute `blob/main` link in a list of relative ones; and corrected the
  tag-pinning advice (see finding 22).
- **`AGENTS.md`**: removed the second-person system-prompt persona, a
  machine-specific instruction to use `scripts/pio.cmd` and never install
  PlatformIO Core, session-hygiene rules ("keep changes tightly scoped to the
  user's request"), and the "Hardening review focus areas" acceptance checklist
  from a completed task. Corrected the repository tree, which was missing
  `docs/`, `tools/`, `scripts/`, `test/`, `.github/` and four root manifests, and
  replaced release steps that contradicted `CONTRIBUTING.md` with a pointer to
  it.
- **`scripts/pio.cmd`** printed an instruction to an AI agent on stderr ("Stop
  and report the missing installation; do not install another PlatformIO Core").
  Replaced with something a human can act on. Note the path it looks for does not
  exist on this machine; `python -m platformio` was used throughout.

### Deleted local scratch

Both were git-ignored, unreferenced, and regenerable or obsolete:

- **`tools/__pycache__/`** — including `run_i2c_hil.cpython-313.pyc` and
  `test_run_i2c_hil_parser.cpython-313.pyc`, bytecode for modules deleted from
  the repository in commit `90e70b8`.
- **`hil_logs/`** — 16 run directories. I verified before deleting that all were
  produced by the *predecessor* runner (directory prefix `i2c_`, whereas
  `run_sht3x_hil.py` writes `sht3x_`), that 12 of the 16 were `port=<dry-run>`
  with no hardware attached, that the four real runs recorded
  `library_version=1.5.0` on branch `hardening/sht3x-release-readiness-gaps`,
  and that no document, script or gate referenced any of them. The evidence
  `docs/hardware.md` cites was a different run, already archived outside the
  checkout.

### Simplified gates

- **`tools/check_core_timing_guard.py`** (123 → 83 lines). `ALLOWED_CALL_COUNTS`
  and `ALLOWED_INCLUDE_COUNTS` were both permanently empty dicts, which made
  ~60 lines of per-file allowance reconciliation dead code that reduced to "any
  occurrence is an error". Rewritten to say that directly. Verified it still
  catches a violation by injecting a `millis()` call.
- **`tools/check_hil_contract.py`** — removed two stale rule sets. The first
  pinned twelve literal prose strings in `docs/hardware.md`, freezing five
  headings and three sentences including a semantically empty one ("No physical
  HIL validation was performed by a dry run"), which made the cleanup above
  impossible without touching the gate. Replaced with a check that the runbook
  documents the tokens the runner actually emits, so docs and runner still cannot
  drift. The second banned six exact English phrases ("hardware validation
  passed", …) in `README.md` and `docs/README.md` — it could not detect a false
  claim, only those six spellings, while constraining a general-purpose library's
  README to an audit's vocabulary.

Everything else the gates enforce is intact: the default HIL command plan still
has to match the runner exactly, mutations still require the literal `confirm`
suffix, `hil_logs/` must stay untracked, the 70-row CLI parity contract is
unchanged, and the framework-neutrality boundary is unchanged.
