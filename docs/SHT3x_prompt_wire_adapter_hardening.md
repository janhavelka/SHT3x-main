# Prompt for AI Coder Agent — Wire Adapter Hardening + Timeout Propagation (SHT3x)

You have full access to the repo. The last changes correctly:
- Treat `Wire.requestFrom()==0` as **ambiguous** and map to `Err::I2C_ERROR`
- Forbid combined write+read (`txLen>0`) in Wire adapters
- Add driver guardrails and tests to lock behavior

Now harden the **Wire transport adapter** so it behaves predictably under a real ESP32 bus manager and does not silently ignore driver timeouts.

## Deliverables
1. Implement changes below.
2. Update README Quick Start accordingly.
3. Add tests.
4. Write `docs/WIRE_ADAPTER_HARDENING_REPORT.md` (Markdown) describing what changed and why.

---

## 1) Propagate `timeoutMs` meaningfully in Wire adapters

### Problem
Driver APIs pass `timeoutMs` into `i2cWrite` / `i2cWriteRead`, but the README Quick Start adapter ignores it. On ESP32 Arduino, Wire timeouts are configurable and materially affect robustness.

### Required changes
Choose one approach and document it clearly:

**Option A (recommended): adapter sets Wire timeout per call**
- In `i2cWrite` and `i2cWriteRead`, call `Wire.setTimeOut(timeoutMs)` (or equivalent) before the transaction.
- Restore the previous timeout if you can (optional).
- Document that the adapter is single-threaded / externally serialized (so changing timeout is safe).

**Option B: adapter treats timeoutMs as “contractual” and asserts**
- Document: “timeoutMs must equal the configured Wire timeout.”
- Add a debug check (if the platform exposes `Wire.getTimeOut()`), or at least a comment warning users.

Acceptance: the README no longer suggests that `timeoutMs` is honored when it isn’t.

---

## 2) Drain partial reads to avoid stale bytes (defensive)

### Problem
If `requestFrom()` returns a non-zero count less than `rxLen`, the current snippet returns error without draining the bytes. Depending on Wire implementation, leaving bytes unread can cause confusing behavior later.

### Required changes
- If `received > 0 && received < rxLen`, read and discard `received` bytes before returning error.
- Keep `Status.detail = received` for debugging.

---

## 3) Make STOP behavior explicit

### Problem
Correct tIDLE enforcement relies on command writes not using repeated-start sequences.

### Required changes
- In Wire adapter `i2cWrite`, use `Wire.endTransmission(true)` explicitly (STOP).
- Document: “SHT3x flows require STOP between command and read; do not use endTransmission(false) for this device.”

---

## 4) Improve error mapping realism on ESP32 Arduino

### Problem
Wire error return codes differ across cores/versions; mapping “4=bus, 5=timeout” may not always be valid.

### Required changes
- In README and example adapter, clearly label which platform/core the mapping is for.
- Add guidance:
  - If platform cannot provide bus/timeout reasons reliably, map unknown codes to `I2C_ERROR` and set `detail` to raw code.
- (Optional) Provide a second mapping for ESP32 Arduino core if you can confirm the returned codes in your repo/tooling.

---

## 5) Documentation touch-ups (README)

Update Quick Start:
- Show `Wire.setClock(...)` and `Wire.setTimeOut(...)` in `setup()`
- State plainly: “Wire cannot prove read-header NACK, so expected-NACK semantics are disabled.”

---

## 6) Tests

Add/extend native tests to cover:
- timeout propagation policy (Option A or B):
  - if you add a Wire stub with a “current timeout” variable, assert the adapter sets it to `timeoutMs`.
- draining behavior:
  - simulate partial read (e.g., 2 bytes received when rxLen=6), ensure adapter drains 2 bytes.
- STOP behavior:
  - if stubs can observe `endTransmission(stop)` argument, assert `stop==true`.

---

## Output
- Implement code changes and doc changes.
- Add tests.
- Create `docs/WIRE_ADAPTER_HARDENING_REPORT.md` (Markdown) with:
  - Chosen timeout policy
  - File/function references
  - Updated README excerpt
  - Test evidence
