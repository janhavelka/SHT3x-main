# Prompt for AI Coder Agent — Reset/Recover With Optional Settings Restore + Manager-Owned Wire Policy (SHT3x)

You have full access to the repo. The driver is currently robust with:
- capability-gated expected-NACK semantics (Wire treats 0-byte reads as ambiguous errors)
- bounded waits (no unbounded stalls)
- recovery ladder present
- host tests + CI

Now implement two **distinct** reset/recover behaviors and tighten the “I2CManager owns Wire configuration” contract.

---

## Requirements from the user (treat as MUST)
### A) Two reset/recovery modes
1) **Reset to defaults (no restore)**  
   - Reset the sensor and leave it at default power-up state.
   - Driver clears internal pending measurement state.
   - Driver does **not** reapply previous settings.

2) **Reset + restore previous settings (RAM)**  
   - Reset the sensor.
   - Reapply the last-known settings stored in RAM (within the driver), so the sensor returns to the same operational configuration as before reset.

These must be exposed as two public APIs (names are up to you but must be unambiguous).

### B) Multi-device bus: Wire clock/timeout must be manager-owned
- The driver must **never** call `Wire.setClock()` / `Wire.setTimeOut()` anywhere.
- The Wire adapter must also avoid per-call global mutations unless it is explicitly an “example-only simple mode”.

### C) Full feature support desired
Even if ALERT is not used now, keep full implementation and make restoration capable of restoring alert limits when they were configured.

---

## Deliverables (REQUIRED)
1) Code changes implementing the items below.
2) Update docs (`README.md`, `docs/AUDIT.md`) to reflect the new API contract and manager-owned bus policy.
3) Add/extend native host tests to prevent regression.
4) Add a new report: `docs/RESET_RESTORE_SETTINGS_REPORT.md` (Markdown) including:
   - new APIs and exact semantics
   - what settings are cached/restored
   - command ordering constraints
   - example usage patterns
   - tests added

---

# 1) Add “settings cache” stored in RAM (driver-owned)

## 1.1 Define a settings struct
Add an internal (and optionally public) struct representing **sensor configuration that must be re-applied after reset**:

Example fields (adapt to your existing types):
- `mode` (SINGLE_SHOT / PERIODIC / ART) — if you support ART separately, store it explicitly
- `repeatability`
- `periodicRate` (only used when mode is PERIODIC)
- `heaterEnabled`
- `alertsConfigured` + stored alert limits (raw limits or converted form)
  - include enough data to restore: high set/clear, low set/clear (and/or all four alert registers you support)
- any other *sensor* settings that are volatile and user-configurable

Do **not** store transport configuration (address, capabilities, timeouts). Those belong to `Config` / manager.

## 1.2 Update the cache only on successful apply
Whenever a public API successfully changes a sensor setting (e.g., `setRepeatability`, `setHeater`, `startPeriodic`, `writeAlertLimit*`):
- only then update the cached settings, so the cache reflects a known-good applied state
- if the I2C op fails, do not mutate the cache

Add getters:
- `Settings getCachedSettings() const;`
- optionally `bool hasCachedSettings() const;` (if needed)

---

# 2) Implement two reset/recovery APIs

You may implement these as separate functions or as one function with an enum parameter. The user wants *two* clear entry points.

## 2.1 API #1: Reset to defaults (no restore)
Example:
- `Status resetToDefaults(ResetKind kind = ResetKind::SOFT);`
or
- `Status recoverDefaults();`

Semantics:
- perform reset (soft reset by default; optional bus reset / hard reset if configured)
- clear pending measurement state and cached “measurementReady”
- set internal mode state to a safe baseline (SINGLE_SHOT idle)
- clear/overwrite the cached settings to library defaults (so future restore doesn’t resurrect old state)
- return status (tracked health)

## 2.2 API #2: Reset + restore cached settings
Example:
- `Status resetAndRestore(ResetKind kind = ResetKind::SOFT);`
or
- `Status recoverAndRestore();`

Semantics:
- perform reset using the same ladder you already have (bus reset → soft reset → optional hard reset → optional general call if enabled)
- if reset succeeds and comms are restored:
  - reapply cached settings in the correct order (see 2.3)
  - restore operational mode (restart periodic/ART if that was cached)
- clear any transient “pending measurement” state at the start; after restore you may mark the driver as READY but do not fabricate a measurement

Return value:
- If comms reset succeeded but restore failed, return the restore error (but keep comms online).

## 2.3 Apply ordering rules (important)
Implement a single internal helper like:
- `Status _applyCachedSettingsAfterReset();`

Order (recommended):
1) ensure device is idle (if needed)
2) clear status flags if your design wants that
3) apply repeatability (if it impacts command selection)
4) apply heater state
5) apply alert limits (and any alert enable/disable commands)
6) restore mode:
   - if cached mode == PERIODIC: start periodic with cached rate
   - if cached mode == ART: start ART
   - else: remain in SINGLE_SHOT

Ensure tIDLE gating is respected by design (do not use combined write+read callbacks for command+read flows).

---

# 3) Multi-device “manager owns Wire” policy

## 3.1 Remove Wire global configuration from adapters (unless explicitly example-only)
Search the repo for:
- `Wire.setClock`
- `Wire.setTimeOut`

Rules:
- **Driver library**: must not contain either call.


## 3.2 Make timeoutMs parameter policy explicit
Because Wire is global:
- Document in README:
  - The driver passes `timeoutMs` to callbacks as a *requested bound*.
  - In a managed bus, the I2CManager decides whether to honor it per call.
  - Example init sets a bus-wide timeout; callbacks do not mutate it.

Optional:
- In the example Wire adapter, you may ignore `timeoutMs` but must document that clearly.

---

# 4) Documentation updates (REQUIRED)
Update `README.md` and `docs/AUDIT.md` with:
- the two reset APIs and exact semantics
- what settings are cached and restored
- note: sensor has no user NVM; settings are volatile; restore uses RAM cache only
- “I2CManager owns Wire clock/timeout; drivers must not mutate global Wire settings”
- example usage snippet:
  - apply settings once
  - on fault: call `resetToDefaults()` or `resetAndRestore()`
  - if resetToDefaults was used: app must reconfigure manually

---

# 5) Tests (native host + CI)
Add/extend Unity tests to validate:

1) **Cache correctness**
- calling `setRepeatability` etc updates cache only on success
- on simulated I2C failure, cache unchanged

2) **ResetToDefaults behavior**
- after calling resetToDefaults, cache == defaults, mode baseline, no periodic restart invoked

3) **ResetAndRestore behavior**
- configure: repeatability + heater + alerts + periodic
- simulate reset success, then verify `_applyCachedSettingsAfterReset()` issues expected command sequence
- verify periodic/ART restarts only when cached

4) **Manager-owned Wire rule (example parity)**
- ensure example transport does not call `Wire.setTimeOut/setClock` inside callbacks (compile-time grep test or stub observation)

---

# Output report
Create `docs/RESET_RESTORE_SETTINGS_REPORT.md` with:
- new API signatures
- what is cached/restored
- exact behavior differences between resetToDefaults vs resetAndRestore
- ordering rationale
- tests added + CI evidence

---

## Notes / Constraints
- Keep backward compatibility where reasonable, but correctness and clarity are priority.
- Maintain capability gating for expected-NACK semantics.
- Do not regress the “0-byte Wire reads are ambiguous” rule.
