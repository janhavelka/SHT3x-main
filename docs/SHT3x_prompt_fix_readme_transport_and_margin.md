# Prompt for AI Coder Agent — Fix Wire Transport Contract Pitfalls + Periodic Fetch Margin (SHT3x)

You have full access to the repo. The library is already “Wire-first” and supports transport capability gating and comms-only recovery. Now address two remaining high-impact robustness hazards:

1) **The README’s Wire transport example can violate SHT3x timing (tIDLE) because it performs a combined write+read with repeated-start inside one callback.** fileciteturn19file0  
2) **Periodic fetch under Wire must never be early**, otherwise the ambiguous 0-byte read can be treated as a hard error (because READ_HEADER_NACK capability is disabled), causing false failures.

## Deliverables (REQUIRED)
1. Code changes implementing the fixes below.
2. Update **README.md** (and any related docs) to remove misleading/unsafe guidance.
3. Add/extend host tests to prevent regressions.
4. Write `docs/README_TRANSPORT_FIX_REPORT.md` (Markdown) describing:
   - what was wrong
   - what changed (file/function refs)
   - behavioral contract going forward
   - tests added

---

## A) Fix the transport contract: forbid “combined write+read” for SHT3x flows that require tIDLE

### Why this matters
The SHT3x protocol requires a minimum idle time between “send command” and “issue read header” for multiple operations (status, serial, alert limits, etc.). If the driver ever calls a transport callback that performs a combined write+read internally (repeated-start), the driver **cannot enforce** tIDLE.

### Problem in README example
`i2cWriteRead()` does:

- `Wire.endTransmission(false)` (no STOP)  
- immediately `Wire.requestFrom(...)`  
This is a write+read back-to-back inside one callback with no delay. fileciteturn19file0

### Required changes (choose one clean solution)
**Option 1 (preferred): remove “write+read” combined transport from the public contract**
- Replace `i2cWriteRead` with two callbacks:
  - `i2cWrite(...)`
  - `i2cRead(...)` (read-only)
- Update the driver to never need a combined callback.
- Update README examples accordingly.
- This forces the driver to control timing (STOP + delay gate + read).

**Option 2: keep `i2cWriteRead`, but impose a hard rule**
- Document and enforce: for SHT3x protocol reads that require tIDLE, the driver will only call `i2cWriteRead` with `txLen == 0`.
- Create a separate internal path for “write command” using `i2cWrite` only, then delay, then `i2cWriteRead(txLen==0)` / `i2cRead`.
- Update README example transport to:
  - always do STOP on command writes (`endTransmission(true)`)
  - and treat the read as a separate call
- Add a debug assert (or a `Status::INVALID_STATE`) if the driver accidentally calls `i2cWriteRead` with `txLen > 0` for any protocol read path.

### Tests to add
- A fake transport that records calls and fails the test if:
  - any status/serial/alert-limit read path invokes a combined write+read inside a single callback, OR
  - the driver requests a read less than `commandDelayMs` after a command write.

---

## B) Periodic fetch under Wire: add a fetch scheduling margin (prevent false errors)

### Why this matters
With Wire adapters, `TransportCapability::READ_HEADER_NACK` is disabled (correct), so the driver cannot safely treat a 0-byte read as “not ready”. Therefore **the driver must avoid fetching early**, otherwise it can produce false failures in periodic mode.

### Current README implications
README says “Wire 0-byte requestFrom commonly indicates read-header NACK; example maps this to I2C_NACK_READ.” fileciteturn19file0  
This is unsafe language: 0-byte is ambiguous on Wire and must be treated as “unknown failure unless proven”.

### Required driver changes
1. Implement an explicit **periodic fetch margin**:
   - `Config::periodicFetchMarginMs` (default e.g. 2 ms or max(2, periodMs/20) bounded)
2. In periodic/ART modes:
   - compute `nextSampleDueMs` from rate
   - only attempt fetch when `nowMs >= nextSampleDueMs + periodicFetchMarginMs`
3. If fetch is attempted and read returns 0 bytes under Wire-capability mode:
   - classify as `I2C_ERROR` (or whatever the transport can prove) and update health failure.
4. Ensure `tick()` doesn’t “busy-loop” fetch attempts:
   - backoff should align to the periodic rate, not just commandDelayMs.

### Tests to add
- Simulate periodic mode with a fake clock:
  - call `tick()` early (before due + margin) repeatedly
  - assert: **no I2C fetch is attempted**
- Simulate `tick()` late:
  - assert: fetch is attempted, and errors are surfaced if transport fails.

---

## C) README fixes (must be explicit and safe)

### Required README edits
1. In the Quick Start transport example:
   - remove any combined write+read implementation that performs repeated-start with no delay (or label it “DO NOT USE for SHT3x read flows”)
   - show separate command write, then read later
2. In ESP32 Notes:
   - change wording to: “On Wire, 0-byte requestFrom is **ambiguous**; do not assume it is read-header NACK.”
3. If you keep returning `I2C_NACK_READ` from the Wire adapter, ensure:
   - capabilities remain `NONE`, and
   - README clearly states the driver will not treat it as NOT_READY unless READ_HEADER_NACK is enabled.

---

## Acceptance criteria
- Driver timing constraints (tIDLE) are enforceable by design, not just “assumed”.
- Wire transports cannot accidentally mask bus errors as “not ready”.
- Periodic mode does not generate false I2C errors due to early fetch.
- Host tests (CI) prove the above and prevent regression.

---

## Output format requirements
- Create `docs/README_TRANSPORT_FIX_REPORT.md` as Markdown.
- Include: findings, fix strategy chosen (Option 1 or 2), code diffs summary, tests added, and updated README excerpts.
