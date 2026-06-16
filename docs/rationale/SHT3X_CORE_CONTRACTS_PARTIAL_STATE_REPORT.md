# SHT3x Core Contracts and Partial-State Report

## Copy/Move Contract

`SHT3x` now explicitly deletes copy and move operations:

- `SHT3x(const SHT3x&) = delete`
- `SHT3x& operator=(const SHT3x&) = delete`
- `SHT3x(SHT3x&&) = delete`
- `SHT3x& operator=(SHT3x&&) = delete`

Native tests include compile-time assertions for all four traits. This prevents
duplicating mutable driver state, timing state, cached settings, health counters,
and injected transport pointers across multiple C++ objects targeting the same
physical sensor.

## Public Contract Updates

- Public headers now state the driver is synchronous, not ISR-safe, and not
  internally thread-safe.
- Transport, bus-reset, and hard-reset callbacks now explicitly forbid
  recursively calling public APIs on the same driver instance.
- `tick(nowMs)` documents that callers must use the same wrapping millisecond
  timebase as `Config::nowMs`.
- Health timestamps/counters are documented as tracked post-`begin()` activity.
- `CachedSettings` is documented as a RAM restore plan, not a live hardware
  readback. Alert entries become valid after successful writes, not reads.
- General-call reset is documented as a bus-manager/application decision because
  it affects every supporting device on the bus.
- Multi-step APIs touched by this prompt now document latency/partial-state
  behavior where relevant.

## Multi-Step Operation Matrix

| Operation | Transaction sequence | Classification | Contract |
|---|---|---|---|
| `begin()` | best-effort Break, wait, soft reset, wait, status probe, optional periodic/ART start | Multi-transaction; may leave hardware changed on failure | Startup can stop/reset the sensor even if `begin()` fails. Health counters intentionally start after initialization. |
| `requestMeasurement()` single-shot | one measurement command | One transaction | Schedules readiness only after command success. |
| `tick()` single-shot read | one read plus CRC checks | One transaction | Failure preserves no false ready sample. Transport failures update health; CRC/protocol failures after a successful bus transaction do not. |
| periodic/ART fetch | Fetch Data command, read, CRC checks | Multi-transaction; no config mutation | Not-ready NACK is non-failure when transport can prove it. |
| inactive `startPeriodic()` / `startArt()` | one start command | One transaction | Cache updates only after success. |
| active periodic/ART restart | Break, wait, new start command | Multi-transaction; may leave hardware stopped | If Break succeeds and restart fails, driver reports single-shot idle and cached desired settings are not advanced. |
| `stopPeriodic()` | Break, wait | One command plus wait | If Break write fails, old mode remains active. If Break succeeds but wait fails, driver already reflects stopped state. |
| `setRepeatability()` / `setPeriodicRate()` while active | restart path | Multi-transaction; may leave hardware stopped | Cache updates only after full restart success. |
| raw command helpers | caller-selected command/read frame | Expert escape hatch | Tracked transport and tIDLE apply, but periodic/ART command legality, response CRC interpretation, and cache coherence are caller-owned. |
| `readStatus()` / `readSettings()` | status command, read, CRC | Read-only multi-transaction | `readStatus()` returns `BUSY` during pending single-shot or periodic/ART. `readSettings()` returns OK with `statusValid=false` for pending/active status unavailability, but returns `BUSY` while OFFLINE. |
| `readStatusWithModeRestore()` active | Break, status read, restore start | Multi-transaction; may leave hardware stopped | Snapshot exposes stop/read/restore status and partial-state flags. If status read and restore both fail, top-level return reports restore failure. |
| `clearStatus()` | clear command | One destructive command | Clears status flags 15, 11, 10, and 4 only when explicitly called. |
| `setHeater()` | heater command | One transaction | Cache updates only after command success. |
| `writeAlertLimitRaw()` | data+CRC write, status verification read | Multi-transaction; may leave hardware changed | Cache updates only after write and verification fully succeed. |
| `disableAlerts()` | HIGH_SET write+verify, then LOW_SET write+verify | Multi-transaction; may partially apply | First limit can be applied/cached if second limit fails. |
| `recover()` | probe, optional bus reset, soft reset, hard reset, general-call reset, probes | Multi-transaction; may leave hardware reset/stopped | Success forces safe single-shot baseline. Failure returns precise step status where possible. |
| `resetToDefaults()` | recovery ladder, local default cache reset | Multi-transaction | Defaults are applied locally only after recovery success. |
| `resetAndRestore()` | recovery ladder, heater/alert/mode restore | Multi-transaction; may partially restore | Cached desired plan remains available when a restore step fails. |

## Dirty/Unknown State Decision

No generic dirty/unknown flag was added.

Reasoning: SHT3x is command-oriented rather than a register-cache ADC. The
highest-risk partial-state surfaces are already either exposed by existing public
state (`isPeriodicActive()`, `getMode()`, `getCachedSettings()`), guarded by
cache-update-after-success rules, or surfaced through Prompt 01's
`StatusReadSnapshot` stop/read/restore fields. A generic dirty flag would be
easy to set broadly but hard to clear rigorously without hardware readback for
every volatile setting.

Partial conditions are instead documented and tested directly:
- active restart can leave the device stopped after Break;
- alert-limit verification failure can mean hardware changed but cache did not;
- `disableAlerts()` can partially apply HIGH_SET before LOW_SET fails;
- reset/restore failures preserve the desired RAM cache for explicit retry.

## Public Tests Added

- Invalid I2C address rejected before I2C.
- Public single-shot request/tick/readout timing.
- Public periodic not-ready NACK does not count as a health failure.
- Public status read and clear are explicit separate operations.
- Public recover preserves precise callback failure status.

## Partial-Failure Tests Added

- Idle periodic start command failure leaves mode/cache unchanged.
- Active periodic restart Break failure keeps old periodic mode active.
- Active periodic restart start-command failure exposes stopped/single-shot state
  while preserving the last fully applied cache.
- Alert-limit data write failure does not update cache.
- Alert-limit status verification failure does not update cache.
- `disableAlerts()` second write failure exposes partial HIGH_SET cache.
- `resetAndRestore()` alert restore verification failure preserves desired cache.

Existing Prompt 01 tests continue to cover `readStatusWithModeRestore()` success
and failure at Break, status-command write, status read, CRC mismatch, and
restore command failure.

## Validation Results

- `python tools/check_core_timing_guard.py`: passed.
- `python tools/check_cli_contract.py`: passed.
- `python tools/check_idf_example_contract.py`: passed.
- `python scripts/generate_version.py check`: `include/SHT3x/Version.h` up to date.
- `python -m platformio test -e native`: passed, 70/70 tests.
- `python -m platformio run -e esp32s3dev`: success.
- `python -m platformio run -e esp32s2dev`: success.
- `idf.py --version`: unavailable in this shell (`idf.py` not recognized), so
  native ESP-IDF builds were not run and are not claimed as validation.

## Remaining Gaps

- Hardware validation is still required for partial-state recovery expectations.
- Pure ESP-IDF CI jobs are now configured, but live CI or local ESP-IDF logs
  are still required before claiming ESP-IDF validation for a specific commit.
- A fully position-indexed transport harness could broaden reset/recover matrix
  coverage further, but the current focused tests cover the highest-risk Prompt
  02 surfaces without adding noisy infrastructure.
