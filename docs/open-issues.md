# SHT3x Open Issues

A live backlog of confirmed defects and simplifications that are too large, too
behavioural, or too coupled to hardware to fix without review. Each entry states
what the code does, why that is wrong, and the smallest change that fixes it.

This file is maintained, not archived: **delete an entry when it is fixed**, and
record the fix in [../CHANGELOG.md](../CHANGELOG.md). It replaces the previous
`CODE_AUDIT.md` / `CODE_AUDIT_REMEDIATION.md` pair, which had become a frozen
report about work that was already finished.

Everything below was reproduced against the working tree before being written
down. Nothing here is a hardware claim; items marked **needs HIL** change
behaviour on a real sensor and must be validated per
[hardware.md](hardware.md) before release.

---

## Summary

| # | Issue | Severity | Needs HIL |
| --- | --- | --- | --- |
| S | **Structural:** one fact stored twice, six times over | — | no |
| 1 | Periodic fetch margin is re-applied every cycle; ~5 % of samples are lost | High | yes |
| 2 | A failed *read* invalidates the acquisition baseline, permanently | High | no |
| 3 | A callback returning `INVALID_PARAM`/`INVALID_CONFIG` is invisible to health | High | no |
| 4 | `bind()` discards the heater/alert restore plan but still reports it valid | High | no |
| 5 | A general-call ACK clears *this* device's OFFLINE latch | Medium | no |
| 6 | `readStatus()`/`clearStatus()` refused in the only mode where ALERT exists | Medium | yes |
| 7 | `ClockStretching` is an inert configuration axis | Medium | yes |
| 8 | `encodeAlertLimit()` is non-monotonic at exactly one point | Medium | no |
| 9 | The single-shot conversion margin delays the first *periodic* fetch | Low | no |
| 10 | `readSettings()` throws away a valid snapshot when OFFLINE | Low | no |
| 11 | Example CLI: five behavioural defects | Medium | no |
| 12 | Test-suite gaps, three proven by mutation | Medium | no |
| 13 | Tooling and housekeeping | Low | no |

---

## S. One fact stored twice

Six of the entries below, and most of the driver's accidental complexity, come
from the same habit: a fact that already has an owner is mirrored into a second
member, and every write site then has to remember both. Nothing here is a bug
today. All of it is why the next bug will be easy to write and hard to see.

`src/SHT3x.cpp` is 3 000 lines, and roughly 90 of them are these mirrors.

### S.1 `_measurementRequested` mirrors `_jobType`

`_measurementRequested = true` is written at exactly two places
([src/SHT3x.cpp:907](../src/SHT3x.cpp), :930) and each is followed three lines
later by `_jobType = JobType::MEASUREMENT`. The only clear-to-`NONE` of
`_jobType` is inside `_clearJobState()`, which clears `_measurementRequested` in
the same function. So `_measurementRequested` implies `_jobActive()`, and the
second clause of

```cpp
bool SHT3x::_singleShotMeasurementPending() const {
  return _jobActive() ||
         (_mode == Mode::SINGLE_SHOT && _measurementRequested && !_measurementReady);
}
```

is unreachable. That predicate — the precondition guard for fifteen public APIs —
is an alias of `_jobActive()` under a narrower-sounding name, and the suite has
to fabricate an impossible state (`_measurementRequested = true` with
`_jobType == NONE`, through `#define private public`) at
`test/test_basic.cpp:1502` and `:1661` to reach the dead clause.

**Proposal.** Delete the member and its sixteen assignments. Redefine the two
public readers in terms of `_jobType`: `measurementPending()` becomes
`_jobType == JobType::MEASUREMENT && !_measurementReady` — *not*
`_jobActive() && !_measurementReady`, because `test_basic.cpp:4003` correctly
requires it to stay false during an `ENSURE_IDLE` job. `getSettings()`'s
`measurementReadyMs` line needs the same substitution. Then
`_singleShotMeasurementPending()` collapses into `_jobActive()` and can be
deleted outright.

### S.2 `_config.mode` mirrors `_mode`

`grep -n "_config\.mode" src/SHT3x.cpp` returns eleven hits and **every one is an
assignment**. No driver logic reads it; `getMode()` and `getSettings()` both read
`_mode`. The field exists only so the stored `Config` carries a copy — and
`bind()` deliberately overwrites the caller's requested mode with
`SINGLE_SHOT`, so `getConfig().mode` is neither the caller's request nor a
reliable state source.

**Proposal.** Delete the ten mirror writes outside `bind()`, keep the single
normalising `_config.mode = Mode::SINGLE_SHOT;`, and say in the `getConfig()`
comment that `mode` is the bind-time normalized value only. Delete the now-dead
`device._config.mode = Mode::PERIODIC;` setup at `test/test_basic.cpp:5092`.
Keep the two assertions at `:3315` and `:4067` — they test that `bind()` refuses
to claim a periodic hardware state, which is a real contract.

### S.3 `_jobEffect` mirrors `_effectForPhase()` — and has already drifted

`_jobEffect` is written at eight sites, and at every one the stored value is
fully determined by the phase, so it holds nothing `_effectForPhase()` cannot
compute. Because the periodic path forgot its assignment, the two copies now
disagree:

```text
poll -> phase=PERIODIC_FETCH_COMMAND active=1 effect=NONE  "Periodic read pending"
        (Fetch Data is already on the wire)
cancelJob(REQUESTED) -> phase=PERIODIC_READ effect=RESULT_MAY_BE_PENDING
```

Same driver state, two different published effects. `JobEffect` is the owner's
only signal that the sensor is holding an unread word, so an owner that trusts
the polling stream rather than cancelling gets the wrong answer.

**Proposal.** Delete `_jobEffect` and call `_effectForPhase(phase, false)`
wherever an active result is published — `src/SHT3x.cpp:457` and the
invalid-`CancelReason` echo at `:986`. Line `:409` is a dead store (every return
path reassigns `result.effect`) and can simply go. Keep `_effectForPhase()`'s
`IDLE`/default arm returning `JobEffect::NONE`.

### S.4 `_hasCachedSettings` mirrors `_initialized`

`bind()` sets both `true` on its single success path and nothing ever clears the
flag, so `_initialized` implies `_hasCachedSettings`. Consequences:
`hasCachedSettings()` is a constant-true predicate; the `if (!_hasCachedSettings)`
branch in `resetAndRestore()` is unreachable because the function already
returned `NOT_INITIALIZED` for the only state where the flag could be false; and
sixteen `_hasCachedSettings = true;` statements are no-ops inside functions that
already guard on `_initialized`. Where it is not merely redundant it is wrong:
`end()` leaves it set, so a torn-down `UNINIT` instance reports a valid restore
plan.

**Proposal.** Delete the member and all sixteen assignments; redefine
`hasCachedSettings()` as `return _initialized;` and delete the unreachable
branch. (Note this interacts with issue 4 — fix that first, then this.)

### S.5 `_periodicActive` mirrors `_mode != SINGLE_SHOT`

The two are written together at all eight sites, making the guards at
`src/SHT3x.cpp:627`, `:655` and `:920-923` unreachable, and forcing every test
that pokes internals to set two fields for one state.

**Do not delete `_periodicActive` yet.** The remaining prerequisite is
`begin()`, which assigns `_mode = requestedMode;` before `_enterPeriodic()` runs
— a window that exists only because the mirror exists. Move that out of `begin()`
so `_enterPeriodic()` is the sole writer, and only then is the flag safely
derivable from `_mode`.

(The other prerequisite is already done: `end()` used to clear `_periodicActive`
without resetting `_mode`, which was the one reachable skew between the two
fields. `end()` now clears both.)

### S.6 `_setSafeBaseline()` is hand-rolled in three more places

The same ~20-field reset appears in eleven separate blocks.
`generalCallReset()` ([:1701-1719](../src/SHT3x.cpp)) repeats all nineteen
assignments of `_setSafeBaseline()` verbatim; `softReset()` repeats them minus
`_measurementReadyMs`; `_stopPeriodicInternal()` repeats them minus
`_hasSample`/`_sampleTimestampMs`, **once per branch of the same function**.

The copies have already diverged, and nothing tells a reader which subset is
deliberate: `interfaceReset()` omits `_mode`/`_periodicActive`/`_periodMs`
(correctly — an interface reset does not stop acquisition on the chip), while
`pollJob`'s `ENSURE_BREAK_COMMAND` does a partial stop that leaves `_periodMs`,
`_lastFetchValid`, `_periodicStartMs` and `_missedRemainderMs` still describing
the periodic run it just broke.

**Proposal.** Replace `generalCallReset()`'s block with
`_setSafeBaseline(); _hardwareStateValid = true;` — it is field-for-field
identical, so this is a pure refactor that inherits the helper's existing
coverage. Do the same for `softReset()` (the missing `_measurementReadyMs` is
inert: it is never read before it is rewritten). Give
`_stopPeriodicInternal()` a single shared block instead of two. Leave `bind()`,
`_enterPeriodic()`, `interfaceReset()` and the `pollJob` `ENSURE_*` steps alone —
they are deliberately different.

### S.7 Dead parameters in the transport wrappers

`_i2cWriteReadTracked()` and `_i2cWriteReadTrackedAllowNoData()` have one call
site, `_readOnly()`, which passes the literals `nullptr, 0` and `true`. So
`txLen` is always `0`, the `(txLen > 0 && txBuf == nullptr)` guard is
unreachable, `readOnly` at `:2436` is always `true`, and `allowNoData` at
`:2437`/`:2441` is always `true` — the actual rule ("proven read-header NACK, or
an ambiguous `I2C_ERROR` during a `PERIODIC_READ` job") is hidden behind two
compile-time-true conjuncts. `Config.h` already states that combined write+read
is not allowed for SHT3x flows, so the write path here is speculative capability
that contradicts the documented contract.

**Proposal.** Collapse both into one function rather than trimming parameters on
two: `_i2cWriteReadTracked()` is a strict prefix of the AllowNoData variant.
Replace them with
`Status _i2cReadTracked(uint8_t* rxBuf, size_t rxLen, bool allowNoData, bool logicalComplete = true)`,
drop the `txBuf`/`txLen` guard half and the `readOnly` local, and make
`_readOnly()`'s tracked branch a single call. `allowNoData` stays a real
parameter — it is genuinely both `true` and `false` across `_readOnly`'s callers.
Update the one test call at `test/test_basic.cpp:1838`.

Related: `_writeCommandWithData()` and `_readMeasurementRawNoDelay()` take a
`tracked` parameter that is `true` at every call site, leaving untracked
branches that cannot execute.

### S.8 The transport wrapper reads the job state machine

`_i2cWriteReadTrackedAllowNoData()` inspects `_jobType` and `_measurementPhase`
to decide whether a returned `I2C_ERROR` means "no new data":

```cpp
  const bool periodicReadJob = _jobType == JobType::MEASUREMENT &&
                               _measurementPhase == JobPhase::PERIODIC_READ;
```

`pollJob()` reaches that read only from the `PERIODIC_READ` branch, so the
predicate is always `true` there. It exists solely to exclude the public
`readCommand(..., allowNoData = true)` caller — which the caller could have said
directly. A maintainer who reorders the phase transition silently turns every
ordinary not-ready Fetch back into a transport failure, with no local change to
the transport code and no compile error.

**Proposal.** Replace the `allowNoData` bool threaded through
`_readOnly`/`_readAfterCommand`/`_readMeasurementRawNoDelay` with the caller's
actual intent — `enum class NoDataPolicy { REJECT, PROVEN_ONLY, INFER }` —
have `pollJob()` pass `INFER` and `readCommand()` pass `PROVEN_ONLY`, and delete
`periodicReadJob`. Two job-state reads leave the transport layer.

### S.9 `maxInstructions` is a budget that is not a budget

`pollJob()` caps itself at one instruction, as the header admits. Every switch
case that consumes an instruction returns from inside the case, so at
`src/SHT3x.cpp:685` `result.instructionsUsed` is always `0` and
`maxInstructions` is always `>= 1`: the comparison
`result.instructionsUsed >= maxInstructions` can never be true. An integrator who
sizes an I2C task slot and calls `pollJob(now, 3, r)` gets identical behaviour to
`pollJob(now, 1, r)`, and `instructionsUsed` comes back as `1` — matching a budget
of `1` — so the ignored request is indistinguishable from a satisfied one.

**Proposal.** Delete the dead branch now. At the next MAJOR, collapse the
parameter to what it is — `pollJob(uint32_t nowMs, bool allowTransport, PollJobResult&)` —
and drop `PollJobResult::instructionsUsed`.

---

## 1. The periodic fetch margin is re-applied every cycle, and costs 5 % of all samples

**Severity: High. Needs HIL.** Affects the default periodic configuration.

**What the code does.** `_periodicReadyMs()` picks the earliest allowed Fetch
Data instant. In steady state it anchors on the previous successful read and then
adds the fetch margin unconditionally:

```cpp
  } else {
    startMs = _lastFetchMs;
    waitMs = _periodMs;
  }
  waitMs += _periodicFetchMarginMs();          // src/SHT3x.cpp:2094
```

The auto margin is `max(2, period/20)` — 5 %, i.e. 50 ms at 1 mps.

**Why it is wrong.** `_lastFetchMs` is the *completion* time of the previous
read, which is already at or after the instant the sensor produced that word. So
`_lastFetchMs + _periodMs` can never be earlier than the next sensor output on a
nominal sensor, and the extra margin buys nothing — it is pure cumulative lag.
Because the anchor advances with every fetch, the driver's cadence becomes
`period + margin` while the sensor free-runs at `period`, so the driver falls a
full period behind every 20 fetches. The SHT3x holds exactly one result word and
overwrites it ("After the read out command fetch data has been issued, the data
memory is cleared", datasheet §4.6), so those outputs are permanently lost.

The margin's real job — tolerating a sensor whose true period exceeds nominal —
is already done, and done better, by the not-ready retry path: a proven or
inferred no-data observation backs off exactly one margin and is explicitly
neither a transport nor a logical health failure.

**Reproduction** (host harness compiled against the real `src/SHT3x.cpp`; 1 mps,
sensor free-running at exactly 1000 ms, owner polls every 1 ms and re-requests
immediately):

```text
60 s, unmodified:      sensor outputs=61  fetched=58  lost=3
                       mean fetch interval = 1051.0 ms   missedSamplesEstimate()=2
60 s, margin removed:  sensor outputs=61  fetched=61  lost=0
```

Raising `periodicFetchMarginMs`, as the README currently advises, makes it worse:
a 200 ms margin on a 1000 ms period loses 15 %; `margin == period` loses 50 %.

**Proposal.** Apply the margin only to the *first* fetch, where the anchor is
`_periodicStartMs` and the conversion really may still be running:

```cpp
uint32_t SHT3x::_periodicReadyMs(uint32_t nowMs) const {
  if (_periodMs == 0) {
    return nowMs;
  }
  uint32_t startMs = 0;
  uint32_t waitMs = 0;
  if (!_lastFetchValid) {
    // First fetch after starting acquisition: the first conversion may still be
    // running, so keep the margin.
    startMs = _periodicStartMs;
    waitMs = estimateMeasurementTimeMs() + _periodicFetchMarginMs();
  } else {
    // Steady state: _lastFetchMs is a completion timestamp, already at or after
    // the sensor produced that word, so one period from it is never early on a
    // nominal sensor. A slower-than-nominal sensor is handled by the bounded
    // no-data retry, which is health-neutral by design.
    startMs = _lastFetchMs;
    waitMs = _periodMs;
  }
  if (_durationElapsed(nowMs, startMs, waitMs)) {
    return nowMs;
  }
  return startMs + waitMs;
}
```

Update `test_periodic_fetch_margin_blocks_early_fetch` to assert the margin on
the first fetch and its absence on repeats, and correct the README's
"Use `Config::periodicFetchMarginMs` to avoid early Fetch Data reads" paragraph,
which currently recommends the setting that causes the loss.

---

## 2. A failed read invalidates the acquisition baseline, and nothing ever restores it

**Severity: High.**

**What the code does.** `_updateHealth()` is the funnel for every tracked
transport callback. On any non-OK callback it clears the verified-hardware-state
flag, whether the callback was a command write or a receive-only read:

```cpp
  if (_transportFailures < maxU32) {
    _transportFailures++;
  }
  _hardwareStateValid = false;                 // src/SHT3x.cpp:2569
```

`_hardwareStateValid = true` appears only in `recordEnsureSuccess()`, `recover()`,
`resetToDefaults()`, `resetAndRestore()`, `softReset()`, `generalCallReset()` and
`begin()`. A successful CRC-valid Fetch Data frame — positive proof that periodic
acquisition is running — does not re-establish it.

**Why it is wrong.** A receive-only read cannot change the sensor's acquisition
state. The SHT3x has no read command that alters the mode, and the driver only
ever calls `i2cWriteRead()` with `txLen == 0`. The job layer already models this
correctly and is bypassed: `_effectForPhase()` returns `RESULT_MAY_BE_PENDING`
for `SINGLE_SHOT_READ`/`PERIODIC_READ` and `DEVICE_STATE_INDETERMINATE` only for
ambiguous *command* phases, and `recordFailure()` clears `_hardwareStateValid`
only for the indeterminate case. `_updateHealth()` clears it first, so that
careful distinction never gets a chance to run.

The consequence is not cosmetic, because there is no non-destructive way back.
`requestEnsureIdle()` is Break + soft reset. `recover()` computes
`acquisitionStopped = _hardwareStateValid && !_periodicActive`, so with the flag
false its probe short-circuit is refused and it falls through to Break + soft
reset too. A conforming owner following [integration.md](integration.md) tears
down and restarts a perfectly healthy periodic stream after one transient bus
glitch.

**Proposal.** Move the single line out of `_updateHealth()` and into the two
*write* wrappers, `_i2cWriteTracked()` and `_i2cWriteRawAddrTracked()`, where a
failure genuinely means "a command may or may not have reached the device". No
new parameter and no new flag: the read wrappers stop making a claim they cannot
support, and the existing `_effectForPhase()`/`recordFailure()` logic — plus the
explicit invalidations already present in `writeAlertLimitRaw()`,
`readCommand()`, `writeCommand()`, `interfaceReset()` and
`_stopPeriodicInternal()` — remains the only source of invalidation.

---

## 3. A callback returning `INVALID_PARAM`/`INVALID_CONFIG` is invisible to health

**Severity: High.**

**What the code does.** All four tracked wrappers inspect what the transport
callback returned and, if it is `INVALID_CONFIG` or `INVALID_PARAM`, return it
*without* calling `_updateHealth()`:

```cpp
  Status st = _i2cWriteRaw(buf, len);
  if (st.code == Err::INVALID_CONFIG || st.code == Err::INVALID_PARAM) {
    return st;                       // never reaches _updateHealth()
  }
  return _updateHealth(st, logicalComplete);
```

**Why it is wrong.** For driver-generated codes the branch is provably dead:
`bind()` rejects null callbacks, each wrapper null/len-checks its own buffers
before calling raw, and every tracked read passes `txLen == 0` with `rxLen > 0`.
So the branch only ever fires for a status the adapter returned *after the
callback was actually invoked* — a real, admitted bus attempt that failed — and it
makes that failure invisible to the entire health API.

This was raised before and rejected on the premise that "adapters must return the
documented transport status family after an admitted callback". That premise is
false in this repository: the shipped Arduino adapter returns `INVALID_PARAM`
for a real `endTransmission()` outcome and `INVALID_CONFIG`/`INVALID_PARAM` from
its null-handle and zero-timeout guards, and `IdfI2cTransport`'s `validate()`
does the same. Nothing in `Config.h` or [integration.md](integration.md) told
those authors the codes would be discarded — that has now been added to
`Config.h`, which closes the documentation half but not the behaviour.

**Reproduction.** Bring-up with the repo's own Arduino glue where the
application fills `Config` but forgets `cfg.i2cUser = &Wire` (it defaults to
`nullptr` and `bind()` never validates it — it is an opaque `void*`). `bind()`
returns OK and sets `READY`. After 10 000 failed operations:

```text
state=READY online=1 consec=0 totalFailures=0 transportFailures=0 lastError=OK
```

`LATCH_OFFLINE` can never engage and a supervisor polling `isOnline()` never
fires.

**Proposal.** Delete the four post-callback filters. Every driver-side rejection
already happens *before* the callback, so the filter protects nothing, and
without it an admitted callback's failure is accounted however the adapter
reported it. Then fix the Arduino example so `endTransmission() == 1`
("data too long") maps to `Err::I2C_ERROR` rather than `INVALID_PARAM` — it is a
bus outcome, not a parameter error. Keep the new `Config.h` note as the contract
for third-party adapters.

---

## 4. `bind()` discards the heater and alert restore plan but still reports `hasCachedSettings() == true`

**Severity: High.**

**What the code does.** `bind()` wipes the whole restore plan and then
immediately re-marks it valid from config alone:

```cpp
  _cachedSettings = defaultCachedSettings();
  _hasCachedSettings = false;
  ...
  _syncCacheFromConfig();
  _hasCachedSettings = true;
```

`_syncCacheFromConfig()` copies only `mode`, `repeatability`, `periodicRate` and
`clockStretching`. `CachedSettings::heaterEnabled` and all four
`alertValid[]`/`alertRaw[]` entries are left at their defaults — heater off, no
alert words.

**Why it is wrong.** `CachedSettings` is the documented write-through restore
plan and the only RAM copy of settings the SHT3x cannot hold across a reset.
`hasCachedSettings()` is documented as "true when the restore-plan snapshot is
valid", and `resetAndRestore()` rejects with `INVALID_PARAM` when it is false —
so that flag is precisely the signal an owner uses to decide a restore is
meaningful. Rebinding is an explicitly supported flow, and nothing says a
successful rebind destroys the alert limits and heater state the application
already wrote to hardware. The failure is silent in both directions: no error at
rebind, and no error at restore time.

**Scenario.** The adapter writes two alert limits and enables the heater for a
condensation cycle. Bus contention appears, so the owner widens the callback
bound the supported way — `Config c = dev.getConfig(); c.i2cTimeoutMs = 100; dev.bind(c);`
— which returns OK. A later fault triggers `dev.resetAndRestore()`.
`hasCachedSettings()` is true so the precondition passes;
`_applyCachedSettingsAfterReset()` calls `setHeater(false)`, physically switching
off a heater nobody asked to switch off, and skips all four alert writes because
`alertValid[i]` is false. The device comes back with vendor-default alert limits
and the call returns `Status::Ok()`.

**Proposal.** Two options, in preference order.

1. **Preserve the plan across a rebind.** Delete the
   `_cachedSettings = defaultCachedSettings(); _hasCachedSettings = false;` pair
   from `bind()` and keep only the runtime/session reset. `bind()` is
   configuration binding, not a factory reset; a caller who wants a clean slate
   already has `resetToDefaults()`. This also stops a diagnostic rebind from
   destroying the health counters that motivated it.
2. **Or make the flag honest.** Leave `bind()` as it is but do not set
   `_hasCachedSettings = true`, so `resetAndRestore()` reports `INVALID_PARAM`
   instead of quietly restoring defaults.

Whichever is chosen, `bind()`'s Doxygen must say what it resets. Note that
option 1 removes the interaction with S.4;
option 2 makes S.4 no longer applicable.

---

## 5. A general-call ACK clears this device's OFFLINE latch

**Severity: Medium.**

**What the code does.** `generalCallReset()` writes the reset byte to the
bus-wide address `0x00` through `_i2cWriteRawAddrTracked()`, whose only behaviour
beyond the raw write is `_updateHealth(st)` with `logicalComplete == true`. On
success that runs `_completeLogicalOperation(Status::Ok())`, which zeroes
`_consecutiveFailures` and forces `_driverState = READY`. `_reassertOfflineLatch()`
fires only on failure.

**Why it is wrong.** The general call is not addressed to the sensor. An ACK on
`0x00` proves only that *some* general-call-capable device on the bus answered;
it is zero evidence that the SHT3x at 0x44/0x45 responded. Health counters are
documented as this device's logical-operation health, and OFFLINE is documented
as latching until recovery succeeds. It also contradicts the repository's own
precedent that non-device-specific actions stay health-neutral: `interfaceReset()`
is fully health-neutral and `probe()` is raw by contract.

**Scenario.** Shared bus with the SHT3x and one other general-call-capable part.
The SHT3x dies (open SDA at its pin), five failures latch OFFLINE and normal I2C
is correctly suppressed. The application issues `generalCallReset()`. The *other*
device ACKs `0x00`, so the write returns OK, `consecutiveFailures` resets to 0,
state becomes READY and `isOnline()` reports true for a dead sensor.

**Proposal.** Use the untracked `_i2cWriteRawAddr()` and make
`generalCallReset()` health-neutral, exactly like `interfaceReset()`. The
recovery ladder does not need the tracking: it always follows
`generalCallReset()` with `probeTracked()`, which is addressed to the device and
does update health.

---

## 6. `readStatus()` and `clearStatus()` are refused in the only mode where ALERT exists

**Severity: Medium. Needs HIL.**

**What the code does.** Both helpers hard-reject while acquisition is running
(`src/SHT3x.cpp:1424`, `:1515`), and `readHeaterStatus()` inherits the rejection.
The only sanctioned alternative, `readStatusWithModeRestore()`, sends Break,
reads status, and restarts acquisition — four callbacks and a full cadence
restart. There is no `clearStatus` counterpart at all.

**Why it is wrong.** The README justifies the restriction as a datasheet
constraint. The vendor documents say something weaker and, for ALERT, something
contrary. The datasheet says only that it is *recommended* to stop periodic
acquisition before sending another command, in a section about measurement
commands. The alert application note — the authority for ALERT — puts the status
register squarely inside periodic operation: "Whenever the sensor operates in
periodic data acquisition mode the alert mode is active", "a status register bit
indicates the cause of the alert", with §3.1 *Readout the status register* and
§3.2 *Clear Status register* given as the operating procedure. Breaking to
single-shot deactivates alert mode outright, so the driver's policy makes the
vendor's own workflow impossible without destroying the thing being diagnosed.

The restriction is also inconsistent with the driver's own escape hatch:
`writeCommand()`/`readCommand()` will happily push `0x3041` or `0xF32D` at a
device in periodic mode.

**Proposal.** Permit `readStatus()` and `clearStatus()` in periodic/ART — neither
is a measurement command and neither disturbs the Fetch Data stream — then delete
`readStatusWithModeRestore()`, the `StatusReadSnapshot` struct and their
README/Doxygen sections. That removes ~120 lines, a public struct with eight
fields and four partial-failure statuses, and the missing-`clearStatusWithModeRestore()`
asymmetry, in exchange for deleting two `if (_periodicActive)` guards. Validate
on hardware first and record the result in [hardware.md](hardware.md); this is
exactly the row that currently reads "Status read and status restore — covered,
without an induced ALERT".

---

## 7. `ClockStretching` is an inert configuration axis

**Severity: Medium. Needs HIL if made real.**

**What the code does.** The cooperative single-shot path selects the stretch or
no-stretch command word from config, then unconditionally arms a conversion wait
equal to the full datasheet maximum before it will issue the read.
`_commandForSingleShot()` is the only consumer of `_config.clockStretching`.

**Why it is wrong.** Clock stretching exists so the master does *not* have to
time the conversion: the sensor ACKs the read header and holds SCL until the
measurement completes. Because the driver always waits past the conversion
maximum first, the read header always arrives with data already latched and the
sensor never stretches. Selecting `STRETCH_ENABLED` changes the command word and
nothing else observable — while the README (now corrected) used to tell the
integrator to enlarge `i2cTimeoutMs` to pay for a stretch that never happens.

**Proposal.** The documentation half is already fixed. For the code, pick one:

- **(a) Make it real.** When `STRETCH_ENABLED`, skip the
  `SINGLE_SHOT_CONVERSION` phase and go straight from `SINGLE_SHOT_COMMAND` to
  `SINGLE_SHOT_READ`. This is what the command family is for and it deletes a
  state — but it moves a 15 ms block *inside* one transport callback, which
  breaks the "at most one bounded callback per poll" property that makes the
  driver safe on a shared bus. Only do this if the owner-safe contract is
  explicitly relaxed for this mode.
- **(b) Retire it from the measurement path.** Keep `ClockStretching` as the
  `readSerialNumber()` command selector, where it is already an explicit
  argument, and drop it from single-shot at the next MAJOR.

**(b) is recommended** — the current timed, non-blocking flow is the right
behaviour for this library's stated purpose; only the configuration surface is
misleading.

---

## 8. `encodeAlertLimit()` is non-monotonic at exactly one point

**Severity: Medium.**

**What the code does.** `encodeAlertLimit()` clamps its inputs, then consults
`ALERT_APP_NOTE_DEFAULTS` — a single `{58.0 °C, 79 %RH} -> 0xC92D` entry matched
with a ±0.001 float epsilon — and returns that word verbatim, bypassing the
generic nearest-reduced-code arithmetic below it.

**Why it is wrong.** The generic rule maps (58, 79) to RH code 101 (`0xCB2D`);
the override forces code 100. Because it is a point exception inside an otherwise
monotone function, the encoder is non-monotonic in both inputs:

```text
encode(58, 78.99) = 0xCB2D   (rh7=101)
encode(58, 79.00) = 0xC92D   (rh7=100)   <-- a larger request, a lower threshold
encode(58, 79.01) = 0xCB2D   (rh7=101)
```

Sweeping a threshold upward through 79 %RH — a UI slider, a commissioning script —
makes the effective alert point jump *down* by 0.78 %RH and then back up. The
override also applies to all four `AlertLimitKind` values, not just the
power-up `HIGH_CLEAR` default it was added to reproduce, and it has **no runtime
consumer at all**: there is no restore-defaults API. About 30 lines of struct,
table, epsilon and two lookup helpers are sustained only by the doc, test and HIL
vectors written for them.

Underneath is a vendor *labelling* artifact, not a vendor disagreement: the alert
note's Table 1 labels are the decoded words rounded to integers for three of four
rows, but the high-clear row prints "79 %RH" for `0xC92D`, which decodes to
78.13 %RH. The 2-reduced-code RH hysteresis symmetry at both ends (102→100,
28→26) confirms `0xC92D` is the genuine device word and the label is the
approximation.

**Proposal.** Delete `ALERT_DEFAULT_MATCH_EPSILON`, `struct AlertDefaultVector`,
`ALERT_APP_NOTE_DEFAULTS`, `isCloseAlertDefault()`, `alertAppNoteDefaultWord()`
and the lookup in `encodeAlertLimit()`. Keep `ALERT_T_CODE_BITS`,
`ALERT_RH_CODE_MAX` and `ALERT_T_CODE_MAX` — the generic path uses them. If the
four power-up words are worth having as data, expose them as four
`static constexpr uint16_t` defaults in `CommandTable.h`, which is what they
actually are, instead of warping a general-purpose converter. Update the
`encodeAlertLimit()` Doxygen note, the fourth row of `kAlertAppNoteVectors` in
`test/test_basic.cpp`, and vendor-inconsistency row 3 in
[reference/sht3x-chip-notes.md](reference/sht3x-chip-notes.md) to say the driver
follows the arithmetic and the note's label is rounded.

---

## 9. The single-shot conversion margin delays the first periodic fetch

**Severity: Low.**

`_periodicReadyMs()` uses `estimateMeasurementTimeMs()` for the first fetch after
acquisition starts, and that includes `Config::singleShotMeasurementMarginMs`,
which is documented as a *single-shot* setting and may be set as high as 1000 ms.
An owner who widens the single-shot margin silently delays the first periodic
sample by up to a second.

**Proposal.** Use the datasheet conversion bound plus the clock-quantization
allowance for the periodic first fetch, and leave the single-shot margin out of
it — or document the coupling on `singleShotMeasurementMarginMs`. Prefer the
former.

---

## 10. `readSettings()` throws away a valid snapshot when OFFLINE

**Severity: Low.**

`readSettings()` first calls `getSettings(out)`, fully populating the caller's
snapshot from cache with zero I2C. It then has two "cannot read status" branches
that behave differently: the OFFLINE branch returns `BUSY`, while the
job-active/periodic-active branch — which reaches the identical no-I2C outcome —
returns `Status::Ok()` with `statusValid = false`.

The snapshot is the owner's window into `state`, `hardwareStateValid`,
`measurementPending`, `sampleTimestampMs`, `missedSamples` and
`lastMeasurementStatus`. In the OFFLINE case every one of those is filled in
correctly and costs nothing — but the near-universal
`if (!dev.readSettings(snap).ok()) return;` idiom discards it. The one state in
which an owner most needs to see *why* the driver latched offline is the one
state in which the API says the snapshot is unusable.

**Proposal.** Collapse the two branches: set `out.statusReadStatus` to the reason
(offline, job active, or periodic active), leave `out.statusValid = false`, and
return `Status::Ok()` in all three cases. `readSettings()`'s only documented
failure then stays `NOT_INITIALIZED`, and the return value means one thing: "the
cached snapshot is valid; check `statusValid`/`statusReadStatus` for the
register." Update the README paragraph and
`test_read_settings_returns_offline_busy_without_i2c`.

---

## 11. Example CLI defects

`examples/common/` is not part of the library, but it is the bench tool and the
worked example integrators copy.

**11.1 `settings` is byte-identical to `cfg`.** `printConfig(true)` calls
`readSettings()`, which really does issue a status read — and then prints none of
`snap.status`, `snap.statusValid` or `snap.statusReadStatus`. The register is
fetched and discarded. Worse, when the status read fails, or under the default
`LATCH_OFFLINE` when the driver has latched OFFLINE, `printConfig` prints one
status line and returns — discarding a snapshot it already holds in full. That is
the exact state in which the HIL runner uses `settings` as its recovery and
verification command. *Fix:* print the status block when `statusValid`, print
`statusReadStatus` when not, and always print the config fields.

**11.2 `selftest` skips its restore exactly when it matters.** `runSelfTest()`
gates the settings restore on `haveBaseline = readSettings(baseline).ok()`, which
reports whether the *status register* read succeeded. `readSettings()` returns
non-OK whenever the status read fails — the degraded-bus case in which a selftest
is most likely to be run — so a single transient failure leaves clock stretching
ENABLED and repeatability HIGH. The four restore calls also discard their
`Status`. *Fix:* capture the baseline with the zero-I2C `getSettings()`, which
cannot fail once initialized, drop `haveBaseline`, and check the four restore
statuses.

**11.3 `periodic fetch` and `art fetch` do not check the mode.** Both call the
same `scheduleMeasurement()` as `read`, with only the printed label differing,
and `requestMeasurement()` dispatches purely on the driver's own `_mode`. Issued
while the driver is in SINGLE_SHOT — the mode `bind()` normalizes to — `periodic
fetch` runs a single-shot conversion and reports success under the periodic
label. An operator can record that as evidence that periodic acquisition works.
*Fix:* pass the expected mode into `requestMeasurementCommand()` and reject a
mismatch with the zero-I2C `getMode()` accessor.

**11.4 `request` is the only scheduling command with no owner-job guard.** Its
siblings call `rejectActiveOwnerJob()`; `scheduleMeasurement()` does not.

**11.5 `mode`, `repeat` and `rate` query handlers issue I2C.** They call
`readSettings()`, which performs a status read, where `getSettings()` would
answer the same question with zero bus traffic.

Also: `saturatingAdd()` and the transfer-counter body are duplicated between
`examples/common/I2cTransport.h` and `examples/idf/basic/main/IdfI2cTransport.cpp`
and belong in `TransferStats.h`; the Arduino example warns about internal
pull-ups only in a source comment while the IDF example warns at runtime;
`HealthSnapshot`/`printHealthView` are one-type templates; `checkAddress()` has
no caller outside the test that tests it; and `scheduleEnsureIdle()`'s `manual`
parameter is `false` at every call site.

---

## 12. Test-suite gaps, proven by mutation

Three gaps were confirmed by mutating the source and observing that the suite
stayed green.

**12.1 `transport::wireWriteRead()` is never driven to success.** All six calls
in the suite are error cases, so the byte-copy loop and the success return are
entirely uncovered, and `TwoWire::_setReadData` — the stub helper written for
exactly this — is dead code. *Mutation:* replacing the copy body with
`(void)wire->read(); rxData[i] = 0xA5;` still gives 130/130. Every measurement
word on Arduino passes through that loop. *Fix:* append a success case to
`test_wire_adapter_drains_partial_read` using `_setReadData` and
`TEST_ASSERT_EQUAL_HEX8_ARRAY`.

**12.2 `disableAlerts()`'s only correctness property is unverified.**
`AGENTS.md` makes it a driver requirement — "disable alerts by setting the low
set point above the high set point" — and no test observes the LOW_SET word.
*Mutation:* changing `writeAlertLimitRaw(LOW_SET, 0xFFFF)` to `0x0000`
(LowSet == HighSet, alerts *not* deactivated) still gives 130/130. *Fix:* prepend
a success phase to the existing test asserting
`alertRaw[LOW_SET] > alertRaw[HIGH_SET]` — one comparison that states the vendor
rule directly and kills both the value mutation and a constant swap.

**12.3 `generalCallReset()`'s success path and the hard-reset rung never
execute.** `allowGeneralCallReset` is `false` at all seven sites,
`Config::hardReset` is only ever asserted null, and `recoverUseHardReset` is only
ever set false — yet its config default is `true`, so the default recovery ladder
contains a rung the suite has never run. *Fix:* prefer the deletion in
S.6, which makes
`generalCallReset()` inherit `_setSafeBaseline()`'s existing coverage; then one
small test covering the guard and the two wire bytes (`0x06` to address `0x00`)
is enough.

Smaller: `convertTemperatureC_x100`/`convertHumidityPct_x100` rounding is
asserted only at the two endpoints, where rounding cannot matter; four tests
fabricate `_periodicActive == true` with `_periodMs == 0`, a state no public API
can produce, which keeps five unreachable `_periodMs == 0` guards alive; the
tIDLE test uses `0` as a "no command yet" sentinel, which is also a real
timestamp value; and `test/stubs/Wire.h`'s `write(uint8_t)` plus
`Arduino.h`'s `String` class are unreferenced.

---

## 13. Tooling and housekeeping

- **The core framework-neutrality scan exists twice**, in
  `check_core_timing_guard.py` and `check_idf_example_contract.py`, run by two
  different CI jobs, with divergent forbidden-token lists. The copy in
  `check_idf_example_contract.py` is dead. Delete it and keep one owner.
- **`check_docs_contract.py`'s link check delegates to host filesystem
  semantics**, so wrong-case links pass on Windows and macOS and fail only in
  Linux CI. Compare the resolved name against the on-disk entry explicitly.
- **`SHT3x::VERSION_INT` is generated dead code** — emitted by
  `generate_version.py`, referenced nowhere.
- **`idf_component.yml` ships `docs/hardware.md` but excludes
  `tools/run_sht3x_hil.py`**, the runner that runbook requires. Ship both or
  neither.
- **`run_sht3x_hil.py`**: `run_serial()` re-joins and re-scans the whole
  accumulated transcript every 20 ms, so cost is quadratic in output length; the
  `else:` fallback inside the stress-progress branch is unreachable.
- **`sht3x_cli_contract.py::_spec()`** is an 18-line identity wrapper that
  duplicates the frozen dataclass signature it wraps.
- **`_reassertOfflineLatch()`** carries a zero-guard ternary for
  `offlineThreshold`, which `bind()` has already normalized to at least 1.
- **The hard-reset rung** is the only rung in `_performRecoveryLadder()` that
  aborts the ladder on a local failure instead of recording `last` and falling
  through to the next rung.
- **`notReadyCount()`** is zeroed by `_clearJobState()`, so it always reads 0 by
  the time the owner sees the terminal result — including the timeout that the
  streak caused. Expose the streak length in the terminal result, or stop
  clearing it there.
- **The periodic not-ready timeout** is classified as an ambiguous transport
  failure, so it reports `DEVICE_STATE_INDETERMINATE` and invalidates the
  acquisition baseline, even though no command was in flight.

---

## Verification status

Everything in this file was reproduced against the working tree. The following
were run locally on the tree that contains this file:

- Native PlatformIO tests: 130/130 passed.
- Strict host compile of the core and the shared CLI with
  `-Wall -Wextra -Wpedantic -Werror`: passed.
- All seven repository gates and the generated-version check: passed.
- Strict Doxygen generation with warnings as errors: passed.
- `pio pkg pack`: 33 files, no test, build, generated or vendor payload.

Not run here, and therefore not claimed: the Arduino ESP32-S2/S3 builds (no
xtensa toolchain on this machine), the native ESP-IDF builds (`idf.py` not
installed), and any hardware. Items marked **needs HIL** change behaviour on a
real sensor.
