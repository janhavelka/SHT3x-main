# Prompt for AI Coder Agent — Fix Remaining Wire “0-byte read” Semantics + Documentation Consistency (SHT3x)

You have full access to the repo. Recent changes enforced read-only `i2cWriteRead(txLen==0)` and added `periodicFetchMarginMs` + time-gated Fetch Data under Wire. Good.

Now fix the **last high-impact contradiction** in the README + example transport:

- README correctly states: **On ESP32 Wire, a 0-byte `requestFrom()` is ambiguous** and must not be assumed to be “read-header NACK / not-ready”.  
- But the Quick Start example still maps `received == 0` → `Err::I2C_NACK_READ` with the message “Read header NACK”. fileciteturn20file2  
  With `transportCapabilities = NONE`, the driver won’t treat it as NOT_READY, but this error code is still misleading for integrators and can cause wrong orchestrator policy (e.g., classifying it as sensor timing vs bus fault).

## Deliverables (REQUIRED)
1. Code + docs changes below.
2. Add tests preventing regression.
3. Write `docs/WIRE_0BYTE_SEMANTICS_REPORT.md` (Markdown) explaining:
   - what was inconsistent
   - final semantics and rationale
   - changes (files/functions)
   - tests added

---

## 1) Wire adapters must not emit “I2C_NACK_READ” unless they can prove it

### Required change
In all Wire-based adapters in the repo (README snippet and `examples/common/I2cTransport.h`):
- If `Wire.requestFrom()` returns 0 bytes:
  - return `Err::I2C_ERROR` (or `Err::I2C_TIMEOUT` only if you can **prove** timeout via Wire timeout API behavior),
  - and set `Status.detail` to the raw value you can observe.
- Do **not** return `Err::I2C_NACK_READ` from Wire adapters by default.

### Doc change
Update README’s Quick Start code and the transport contract text to reinforce:
- `Err::I2C_NACK_READ` is only valid when the transport declares `TransportCapability::READ_HEADER_NACK` and can truly distinguish it.
- For Wire adapters, keep `transportCapabilities = TransportCapability::NONE`.

### Acceptance criteria
- A naive user copy-pasting Quick Start cannot mistakenly believe “0 bytes == read header NACK”.

---

## 2) Driver-side guardrail (optional but recommended)
Even if docs are fixed, third-party adapters may still return `Err::I2C_NACK_READ` while claiming `TransportCapability::NONE`.

Add a driver guard:
- If `!(cfg.transportCapabilities & READ_HEADER_NACK)` and an operation returns `Err::I2C_NACK_READ`,
  - treat it as `Err::I2C_ERROR` (preserve original in `Status.detail` or message).
This prevents misconfigured adapters from enabling “not-ready” semantics accidentally.

---

## 3) Tests
Add/extend native Unity tests to cover:

1) **Wire ambiguity rule**:
   - With `transportCapabilities = NONE`, simulate a 0-byte read and ensure the driver never returns `MEASUREMENT_NOT_READY`.
   - Ensure the returned error is `I2C_ERROR` (or the remapped equivalent if guardrail added).

2) **Capability correctness**:
   - With `transportCapabilities` including `READ_HEADER_NACK`, simulate explicit `I2C_NACK_READ` and ensure periodic fetch returns `MEASUREMENT_NOT_READY` (and does not update failure health).

3) **README example parity**:
   - Add a small “example adapter” compile test (or static assert) that ensures the provided example adapters conform to the contract (no combined write+read, no fake NACK_READ without capability).

---

## 4) Report output
Create `docs/WIRE_0BYTE_SEMANTICS_REPORT.md` containing:
- Problem statement with the exact inconsistency (README says ambiguous, snippet returns “NACK_READ”).
- Final recommended semantics (Wire returns I2C_ERROR on 0 bytes).
- Changes applied (README, adapters, optional driver guard).
- Test evidence.

---

## Notes / Constraints
- Keep the library hardware-agnostic (no pin ownership).
- Do not change the “comms-only recovery” policy.
- Maintain backward compatibility where possible, but correctness beats compatibility in the transport contract.
