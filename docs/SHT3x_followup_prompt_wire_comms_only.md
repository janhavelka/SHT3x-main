# Follow-up Prompt for AI Coder Agent — Wire-first + “Recover Comms Only” Policy

## User decisions (treat as requirements)
1) **Transport today:** Arduino `Wire` (likely behind an I2C manager later).  
2) **Recovery policy:** “Recover comms only” is sufficient — do **not** restore previous runtime configuration (heater/mode/alert limits).

Your job: harden the SHT3x driver so it is robust **even with Wire’s limited error visibility**, and so it integrates cleanly with a future I2C manager that may provide richer errors.

---

## 1) Transport contract must acknowledge Wire’s limitations (avoid fake certainty)

### Problem (high-impact)
On ESP32 Arduino Wire, `requestFrom()` returning 0 bytes is **ambiguous**:
- could be **read-header NACK** (“not ready”) — valid for SHT3x periodic fetch
- could be **timeout / bus error / device disappeared**
Wire typically cannot prove which.

If we map “0 bytes” → `I2C_NACK_READ` and treat that as `MEASUREMENT_NOT_READY`, we can **mask real failures** and delay recovery.

### Required change
Add a transport capability flag (or equivalent) and gate “expected NACK” behavior behind it:

- Add `Config::transportCapabilities` bitmask (or booleans), at minimum:
  - `supportsReadHeaderNack` (default **false** for Wire adapters)
  - `supportsTimeoutReporting` (default false for Wire unless your adapter can prove it)
  - `supportsBusErrorReporting` (default false for Wire unless proven)

- In driver logic:
  - Only treat `I2C_NACK_READ` as `MEASUREMENT_NOT_READY` **if** `supportsReadHeaderNack == true`.
  - If `supportsReadHeaderNack == false`, then:
    - **do not** use the “expected NACK not-ready” read path at all, OR
    - treat 0-byte reads as `I2C_ERROR` / `I2C_TIMEOUT` (whichever can be proven).

### Wire adapter guidance (docs + code)
- Mark the Wire adapter as **best-effort** and set `supportsReadHeaderNack = false`.
- Document in README:
  - With Wire transport, periodic fetch has **no reliable NOT_READY detection** via NACK.
  - Therefore the driver will behave conservatively (time-gated fetch; ambiguous 0-byte reads treated as errors).

### Tests
- Add a unit test that asserts:
  - if `supportsReadHeaderNack == false`, a 0-byte read **never** returns `MEASUREMENT_NOT_READY`.

---

## 2) Periodic-mode behavior under Wire: time-gated fetch, not NACK-gated fetch

### Problem
Without reliable NACK classification, “poll until ready” is unsafe.

### Required change (recommended approach)
- In PERIODIC / ART modes, do not call Fetch Data early.
- Derive an expected “next sample ready time” from the configured periodic rate.
- Only attempt `Fetch Data` when `now >= nextSampleDueMs`.
- If `Fetch Data` returns 0 bytes under Wire:
  - treat it as `I2C_ERROR` (or timeout if proven), update health failure
  - allow the orchestrator to decide whether to call `recover()`.

This eliminates the need for `MEASUREMENT_NOT_READY` in Wire-based periodic mode in normal operation.

### Optional improvement
If you keep `MEASUREMENT_NOT_READY` at all, make it strictly limited:
- Only possible when `supportsReadHeaderNack == true` (non-Wire transports).

---

## 3) Timing: prove tIDLE compliance independent of transport implementation details

### Requirement
SHT3x requires tIDLE (~1 ms) between command and subsequent command/read header in multiple flows.

### Required change
- Ensure the driver *never relies on* “combined write+read” in a single transport function for SHT3x reads that need a delay.
- Document a transport assumption:
  - driver issues STOP after command writes (or at minimum has control over the timing before the next read header)

### Tests
- Add a fake transport that fails if the driver performs command+read without allowing the delay gate to run.

---

## 4) Recovery ladder: **comms-only** and deterministic

### User requirement
After recovery, it is OK if the device returns to a safe default state; do not restore previous settings.

### Required change
Implement/adjust `recover()` to:
1) Attempt interface reset (bus reset callback) + probe
2) Soft reset + wait + probe
3) Optional: general call reset only if explicitly enabled in config
4) If recovery succeeds:
   - set internal mode to a safe baseline (e.g., SINGLE_SHOT idle)
   - clear pending measurement state and cached “measurementReady” flag
   - do **not** re-enable heater
   - do **not** restart periodic/ART
   - document: “reconfigure after recover() if you want periodic/ART/heater”

Add backoff so repeated recover calls don’t thrash the bus.

### Docs
- Document `recover()` as **comms restoration**, not “restore config”.
- Add a snippet showing orchestrator behavior:
  - on repeated failures: call `recover()`, then re-apply desired mode/settings at system level.

---

## 5) CI-ready tests (make them actually runnable)
The last report says host builds failed due to missing g++ locally. This must not be a permanent situation.

### Required change
- Convert host tests to `pio test -e native` (Unity or Catch2/Doctest are fine).
- Add GitHub Actions workflow running:
  - `pio run` for at least one ESP32 env
  - `pio test -e native`
- Update README: “How to run tests”.

---

## Output requirements
- Implement changes.
- Update docs.
- Add tests + CI.
- Write `docs/ROBUSTNESS_FOLLOWUP_REPORT.md` (Markdown) with:
  - Findings
  - Changes applied (file/function references)
  - Wire limitations + capability gating
  - Recovery semantics (“comms only”)
  - Test/CI evidence
