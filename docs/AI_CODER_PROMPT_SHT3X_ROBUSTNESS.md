# SHT3x Driver — Robustness Upgrade Prompt (for AI Coder Agent)

## Context
You have full access to the repo. The current `docs/AUDIT.md` describes the intended architecture and current behaviors (managed synchronous public API, non-blocking measurement lifecycle via `requestMeasurement()` + `tick()` + `getMeasurement()`, health tracking via tracked I2C wrappers).

Goal: **make this driver “big-project-grade”** on ESP32 (Arduino/PlatformIO), with predictable behavior under bus faults, timeouts, repeated NACKs, and integration with an external I2C manager.

**Deliverable requirements**
- Implement fixes + improvements below.
- Update `README.md` and `docs/AUDIT.md` accordingly.
- Add or expand automated tests where requested.
- Keep driver hardware-agnostic unless explicitly asked (no pin ownership unless configured).
- Generate/Update an implementation report as **Markdown (.md)** committed to `docs/`.

---

## 0) First: tighten the I2C transport contract (blocking + error semantics)

### Problem
The driver currently treats a **0-byte read** as “expected NACK / measurement not ready” in at least one path. On ESP32/Wire, 0 bytes can also mean **bus error, timeout, arbitration loss, or slave disappeared**. If we can’t distinguish these, we will mask real failures as “not ready” and never recover.

### Required changes
1. **Define a strict, documented transport API contract** in `include/SHT3x/*.h` (or `README.md`) for the user-provided callbacks.
   - Callbacks MUST be able to distinguish at least:
     - ACK + bytes read/written
     - NACK on address / NACK on data / NACK on read header
     - Timeout
     - Bus error (SDA stuck low, etc.)
2. Introduce a driver-level `Err` mapping that preserves these distinctions.
3. Update all call sites to use these richer statuses.
4. Update expected-NACK logic so it triggers **only** on the specific NACK case that datasheet describes (read header NACK for “not ready”), not on “0 bytes for any reason”.

### Acceptance criteria
- No path should “swallow” a timeout/bus error as `MEASUREMENT_NOT_READY`.
- Health tracking increments failures on true comms errors; “expected NACK” does not increment failures but **should** record a “bus interaction occurred” timestamp if that’s part of the health model.

---

## 1) Health tracking: make it operationally meaningful

### Problems to solve
- `begin()` probe is currently untracked by design, which means:
  - first comms failures may not appear in counters
  - first comms success may not set `lastOk` timestamps
- Expected NACK reads don’t update health at all (neither success nor failure), which may cause “stale health” during long periodic operation.

### Required changes
1. Decide and implement **explicit policy**:
   - Either (A) begin/probe update health in a special “pre-init” mode without changing `DriverState`, or
   - (B) keep probe untracked but explicitly set `lastOkMs` on successful begin and track begin failures separately.
2. For expected-NACK reads, add a “soft success” path:
   - does **not** increment success counters (optional)
   - does **not** increment failure counters
   - **does** update `lastBusActivityMs` (add field) or `lastOkMs` (if you decide that “ACK-only” defines OK)

### Acceptance criteria
- Health metrics remain informative during long-running periodic mode even when measurements are “not ready” often due to scheduling jitter.

---

## 2) Measurement lifecycle: define and enforce a correctness contract

### Problems to solve
- In periodic mode, “Fetch Data” behavior implies a single-sample memory that is cleared after fetch.
- The current audit text implies the user must “call tick regularly”, but the driver should define what happens when:
  - tick is late / skipped
  - tick is too frequent
  - mode changes mid-flight
  - repeated `MEASUREMENT_NOT_READY` happens for too long

### Required changes
1. Document and implement **staleness** handling:
   - track `sampleTimestampMs` and `sampleAgeMs()` helper
   - optionally expose `missedSamplesEstimate` (best-effort)
2. Add a configurable guard:
   - `notReadyBudget` or `maxNotReadyDurationMs` in periodic mode
   - when exceeded → treat as fault and allow recovery escalation
3. Ensure `setMode/setPeriodicRate/setRepeatability` explicitly defines behavior when called while a measurement is pending.

### Acceptance criteria
- Caller can tell whether a sample is fresh/stale.
- Driver behavior is deterministic under scheduling jitter.

---

## 3) Implement a real recovery ladder (stateful, not “just read status”)

### Datasheet-relevant facts you must respect
- Interface reset: toggle SCL ≥ 9 while SDA high, then START before next command.
- Soft reset: 0x30A2 reloads calibration data, requires idle and a wait afterward.
- General call reset: 0x0006 resets all devices that support it (dangerous on shared bus).

### Required changes
Implement a `recover()` ladder with clear configuration + documentation. Example ladder (adapt as needed):
1. **Interface reset** (bus reset callback), then re-probe.
2. **Soft reset**, then re-probe, then re-apply mode/config.
3. Optional: **nRESET pin pulse** (if the project wants it; add as optional callback or optional pin config).
4. Optional: **General call reset** ONLY if `Config::allowGeneralCallReset == true`.
5. If recovery fails repeatedly → transition to `OFFLINE` per `offlineThreshold` rules.

Also define:
- What `recover()` returns (whether it restored mode, whether it only restored comms)
- Whether `tick()` ever calls `recover()` automatically (default: NO; external orchestrator triggers).

### Acceptance criteria
- After a successful recover, the driver re-applies the active mode (single-shot/periodic/ART) and user-configured settings.
- Recovery never “thrashes” the bus (add backoff).

---

## 4) Timing correctness: prove/lock it down

### Required changes
1. Ensure tIDLE (1 ms) is applied in **every command→read header** sequence (status read, serial number, alert limit read, etc.).
2. Verify command spacing rules around break + subsequent commands.
3. Audit whether any code path attempts to use a repeated-start sequence that cannot satisfy tIDLE.
4. Add a small unit test (or a fake clock test) that validates wraparound-safe time comparisons and that `_ensureCommandDelay()` never blocks unbounded.

### Acceptance criteria
- All timing guards are bounded and do not trigger WDT on ESP32 under reasonable config values.

---

## 5) Tests: add fault-injection and behavior tests (minimal but meaningful)

### Required tests
- Alert packing encode/decode round-trip (public helpers)
- Conversions (raw→C, raw→%RH, *_x100 variants)
- CRC8: if CRC implementation is private, add a test hook OR move CRC into a small internal module with test access.
- Fault mapping tests for transport statuses:
  - NACK-not-ready vs timeout vs bus error
- Recovery ladder unit test using fake transport that simulates:
  - transient failures that recover
  - permanent offline device

### Acceptance criteria
- CI can run tests on host (no hardware required) and provides confidence in edge-case handling.

---

## 6) Documentation updates (required)
Update `README.md` with explicit sections:
- **Transport contract** (what callbacks must do; error semantics)
- **Blocking behavior** (max blocking per API)
- **Thread-safety / ISR-safety** (explicit: not thread-safe; must be externally serialized)
- **Expected NACK semantics** (exact conditions)
- **Recovery** (ladder, state transitions, configuration flags)
- **ESP32 notes** (Wire timeout, clock stretching expectations, common failure modes)

---

## Output format
1. Implement code changes.
2. Update docs.
3. Add tests.
4. Write `docs/ROBUSTNESS_UPGRADE_REPORT.md`:
   - Summary
   - Changes applied (with file + function references)
   - New/updated config flags and behavioral contracts
   - Test coverage added
   - Remaining known limitations (if any)

