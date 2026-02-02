# SHT3x Consolidated Report

Date: 2026-02-02
Scope: Library behavior, transport contract, recovery/reset modes, and test coverage.

## 1) Architecture Summary
- Managed synchronous driver with health tracking (READY/DEGRADED/OFFLINE).
- All I2C is synchronous and bounded; no unbounded loops or delays.
- Transport is injected; library never touches Wire directly.
- Expected-NACK semantics are capability-gated (READ_HEADER_NACK only).

## 2) Transport Contract
- Callbacks must return explicit `Err` codes (I2C_NACK_ADDR/DATA/READ, I2C_TIMEOUT, I2C_BUS, I2C_ERROR).
- `Err::I2C_NACK_READ` is only valid when the transport can prove read-header NACK and declares `READ_HEADER_NACK`.
- Wire `requestFrom()==0` is ambiguous and treated as `I2C_ERROR`.
- `timeoutMs` is a requested bound; in managed buses the I2CManager owns actual Wire timeout/clock.
- For SHT3x protocol reads, callbacks must not perform combined write+read (no repeated-start).

## 3) Reset/Recover Modes
- `recover()` remains comms-only using the ladder (bus reset ? soft reset ? hard reset ? general call), leaving SINGLE_SHOT idle.
- `resetToDefaults()` resets and clears cached settings to library defaults (no restore).
- `resetAndRestore()` resets and reapplies cached settings from RAM.

## 4) Cached Settings (RAM only)
Cached fields:
- mode (SINGLE_SHOT / PERIODIC / ART)
- repeatability
- periodicRate
- clockStretching
- heaterEnabled
- alert limits (raw values + valid flags)

Cache updates only on successful apply operations. SHT3x has no user NVM; restore is RAM-only.

## 5) Restore Ordering
After reset, restore is applied in this order:
1) repeatability
2) clockStretching
3) periodicRate
4) heater
5) alert limits (per register)
6) mode (PERIODIC/ART restart if cached)

## 6) Wire Adapter Policy (Manager-Owned)
- Example adapter callbacks do **not** change Wire clock/timeout per call.
- STOP is explicit for command writes to enforce tIDLE.
- Partial reads are drained before returning error.

## 7) Tests Added/Updated (Native)
- Cache correctness: updates only on success.
- resetToDefaults(): clears cache and returns SINGLE_SHOT baseline.
- resetAndRestore(): reapplies cached settings and restart order (alerts before periodic).
- Adapter behavior: no per-call timeout/clock changes, STOP usage, partial-read draining.

## 8) Known Medium/Low Risks
- Partial-success ambiguity on operations that could include verification reads. Current policy treats verification failure as overall failure; cache does not update.
- Future adapters could ignore the transport contract; tests guard example behavior but external integrations must follow README.

## 9) Example Usage Pattern
```cpp
sensor.setRepeatability(SHT3x::Repeatability::MEDIUM_REPEATABILITY);
sensor.setHeater(true);
sensor.writeAlertLimitRaw(SHT3x::AlertLimitKind::HIGH_SET, 0x2222);
sensor.startPeriodic(SHT3x::PeriodicRate::MPS_2,
                     SHT3x::Repeatability::MEDIUM_REPEATABILITY);

// On fault:
sensor.resetToDefaults();   // app must reconfigure
// or
sensor.resetAndRestore();   // restores cached settings
```
