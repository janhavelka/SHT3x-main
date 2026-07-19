# TunnelMonitor-node suitability audit

## SHT3x environmental sensor library

Date: 2026-07-18

## 2026-07-19 implementation re-audit and disposition

This section **supersedes the pre-implementation assessment below** for the
current hardening branch. The remainder of this document is intentionally
preserved as the historical v1.6.1 audit, including its original evidence,
recommended design, and validation gaps. A finding marked implemented here is
not a hardware-validation claim. The owner-safe implementation and follow-up
self-audit fixes are committed as `be2c18a`, `000fbd9`, `d79b485`, `5c7af7e`,
and `e25046b`; release metadata and primary documentation are in `ae61b9e` and
`7fe1fca`.
Independent core, validation, and traceability reviews drove the follow-up
fixes. The physical gates listed below remain open.

### Re-audit baselines and scope

| Repository | Re-audit baseline | Working-tree note |
| --- | --- | --- |
| SHT3x | Branch `hardening/tunnelmonitor-suitability-reaudit`, historical audit baseline `cf2ffad8eee28341edce74b05f06c12c8d71f7b6`; released v1.6.1 source/tag commit `113ecc67a082c844d062a402412b91eb7980202f`; latest follow-up code/test commit `e25046b` | The implementation, release metadata, and independent-review fixes are separated into focused commits and pushed to the named branch. |
| TunnelMonitor-node | Integration baseline `develop` at `0897f12c1a1369367747d1063936906005391580`; C-01 fix commit `4db59a6` on `fix/env-post-probe-nack-classification` | C-01 was implemented in a separate worktree and pushed. The user's selected shared worktree was left untouched and clean on `docs/mb85rc-suitability-contract-facts` at `b708f511964db6c51e949e99c67820476f00f9c7`. |

The current TunnelMonitor authority still keeps non-RV3032 I2C chip-library
dependencies deferred in
`docs/guidelines/dependency_policy.md`. Its existing ENV behavior remains
authoritative: `I2cTask` owns the bus, scheduling, deadlines, retries, health,
and recovery; `ReadEnv` uses a 20 ms transfer bound and 1000 ms operation
deadline; its fixed candidates remain SHT3x `0x44`, SHT3x `0x45`, and BME280
`0x76`; and only discovery-probe NACK is expected absence. This re-audit
therefore completes the general library boundary without adding a production
TunnelMonitor dependency or adapter.

### Requested workflow-to-commit mapping

The requested ten ordered implementation chunks were preserved as logical
review chunks, not padded into ten artificial commits:

| Requested chunks | Focused revision(s) | Result |
| --- | --- | --- |
| 1: audit baseline | `cf2ffad8` | Added the original evidence report before implementation. |
| 2-8: timing, lifecycle, health, accounting, timestamps, cancellation/identity, and milli-unit output | `be2c18a` | Landed the cohesive owner-safe API and implementation with native coverage. |
| 9: docs, version, reproducibility, and CI metadata | `ae61b9e`, `7fe1fca` | Published v1.7.0 metadata, owner/integration guidance, and the Doxygen-as-error CI gate. |
| 10: fault injection, final self-audit, and release cleanup | `000fbd9`, `d79b485`, `5c7af7e`, `e25046b` | Closed compatibility, wrap, deadline, provenance, status-validation, lifecycle, cache-integrity, recovery-proof, and diagnostic gaps. |
| TunnelMonitor C-01 cleanup | TunnelMonitor `4db59a6` | Separated discovery absence from post-probe transfer NACKs and added native coverage/guidance. |

### Hard-finding dispositions

| Finding | Affected contract / severity | Current evidence and resolution | Native coverage | Final software status |
| --- | --- | --- | --- | --- |
| H-01: mixed-clock tIDLE gate | Datasheet command spacing; **release blocker** | `pollJob()` samples `Config::nowUs` for tIDLE instead of multiplying caller milliseconds. Comparisons are wrapping-duration based. | Fractional-millisecond, exact-boundary, microsecond-wrap, long-idle, and periodic Fetch-transition tests pass in the 116-test suite. | **Resolved in software.** |
| H-02: active blocking lifecycle/recovery | External I2C-owner work bound; **release blocker** | Zero-I2C `bind()` plus staged `requestEnsureIdle()` advance Break, reset, status command, and validated status read with at most one callback per poll. Synchronous `begin()`/maintenance helpers remain explicitly bounded compatibility surfaces. Unverified synchronous recovery cannot claim idle from a status/interface probe alone. | Bind/rebind/end, repeated lifecycle, four reconciliation transfer stages, status flags/CRC, wait phases, deadlines, cancellation, and bind/interface-reset recovery-proof cases pass. | **Resolved for the owner-safe surface; synchronous restrictions documented.** |
| H-03: mandatory OFFLINE admission latch | Hotplug and owner admission; **release blocker** | `HealthPolicy::OBSERVE_ONLY` retains diagnostics without suppressing caller-authorized I2C; `LATCH_OFFLINE` preserves compatibility behavior. | Observe-only continues after threshold and a successful hot-return measurement restores READY without rebind; latch admission remains bus-silent. | **Resolved in software.** |
| H-04: health counted per transfer | Health semantics; **high library correctness defect** | Tracked wrappers separate physical callbacks from complete logical operations. Intermediate command success cannot erase a failed-read streak. Logical, transport, protocol, and expected-not-ready counters are separate and saturating. | Repeated command-success/read-timeout operations reach threshold exactly; separation and saturation tests pass. | **Resolved in software.** |
| H-05: pre-transfer conversion/sample timestamps | Completion-time and freshness; **high timing correctness issue** | Readiness and sample timestamps are captured from the configured clock after successful callbacks complete. | Delayed command/read completion, callback-crossed deadline, millisecond wrap, and both-address forwarding tests pass. | **Resolved in software.** |
| H-06: no owner-safe cancellation, deadline, or identity | Request ownership/deadline; **high integration blocker** | `JobRequest` and `PollJobResult` expose nonzero identity, phase, outcome, instruction use, and physical effect. Cancellation is zero-I2C between polls; an entered externally bounded callback is atomic to the driver. | Invalid/zero identity, invalid cancel reason, zero budget, all meaningful measurement/reconciliation stages including `PERIODIC_READ`, wrap, exactly-once terminal, and preserved-cache tests pass. | **Resolved in software.** |
| H-07: centi-only output | TunnelMonitor unit contract; **required API improvement for clean integration** | Signed milli-degree/milli-percent conversions use 64-bit intermediates and share the unrounded raw sample with float output. | Endpoint, midpoint, negative/near-zero, rounding, fixed-layout, and raw parity vectors pass. | **Resolved in software.** |
| H-08: inaccurate transaction/wait documentation | API ownership and timing documentation; **high contract/documentation issue** | Public Doxygen, README tables, ESP-IDF notes, changelog, and repository guidelines state zero-I2C scheduling/cancellation, one-callback polls, atomic callback limits, bus-silent waits, and bounded compatibility behavior. | Instruction-count tests and documentation/contract guards pass. Doxygen now runs with warnings as errors in CI. | **Resolved in software/documentation.** |

### Secondary-finding dispositions

| Finding | Affected contract / severity | Current disposition | Final software status |
| --- | --- | --- | --- |
| S-01: budget broader than behavior | Poll budget clarity; **medium** | `maxInstructions` is retained, while Doxygen/README state the implementation currently consumes at most one instruction; zero/wait polls perform no I2C. | **Resolved in contract and tests.** |
| S-02: transport/protocol diagnostics | Observability; **medium** | Fixed saturating counters separate physical transfer, logical operation, CRC/protocol, and expected-not-ready outcomes without a log/queue/framework. | **Resolved; separation/saturation tests pass.** |
| S-03: cached caller message lifetime | Memory ownership; **medium** | Cached transport errors retain enum/detail but replace callback-owned text with static library messages. | **Resolved; transient-buffer regression passes.** |
| S-04: unchecked raw command access | Cache/hardware ownership; **high restricted surface** | Raw helpers remain documented expert/diagnostic APIs, reject active cooperative jobs, and invalidate `hardwareStateValid()` after an attempted command. No TunnelMonitor adapter may use them; a breaking rename waits for a major version. | **Accepted restricted surface with explicit invalidation.** |
| S-05: reproducible release builds | Release reproducibility; **medium** | PlatformIO `6.1.18`, Espressif32 archive `54.03.20`, and ESP-IDF container `v5.4.2` are pinned; version generation synchronizes component and Doxygen metadata. | **Local pinned builds pass; live CI/pure-IDF execution remains external.** |
| S-06: monolithic native test file | Test maintainability; **low** | The focused tests remain in the established native fixture instead of adding a parallel framework. A later mechanical split may preserve behavior. | **Accepted maintenance debt; not an integration gate.** |

### Follow-up self-audit closeouts

| Self-audit item | Affected contract / severity | Closure evidence |
| --- | --- | --- |
| Synchronous `begin()` accepted sensor diagnostic flags | Reconciliation truthfulness; **high** | `begin()` now shares status-word diagnostic validation with ensure-idle/alert writes and rejects command/write-CRC flags without claiming initialization or startup health. |
| Synchronous `begin()` bypassed configured post-reset spacing | Datasheet command spacing; **high** | The reset command timestamp remains authoritative, so `commandDelayMs` values above the fixed reset wait are honored before status verification. A 7 ms regression passes. |
| Ensure-idle could discard an unread sample before doing I2C | Zero-I2C scheduling/cancellation; **high** | Request and pre-I2C cancellation preserve the last-good sample; destructive effects begin only after the staged callback runs. |
| Ensure-idle settle time overwrote preserved sample readiness metadata | Cache integrity; **high** | A dedicated job wake timestamp now owns Break/reset settle deadlines. Post-Break cancellation preserves both the unread sample and its original readiness timestamp. |
| Periodic expected-not-ready callback could cross the owner deadline | Deadline/terminal identity; **high** | The completing callback timestamp is checked immediately; the job terminates exactly once at `PERIODIC_READ` while preserving known no-frame hardware state. |
| Invalid cancellation enum was accepted | Public validation; **medium** | Invalid reasons return `INVALID_PARAM`, leave identity/progress active, and perform zero I2C. |
| Long idle exceeded signed half-range assumptions | Long-uptime timing; **high** | Periodic readiness, not-ready escalation, and recovery backoff use duration checks; short-lived single-shot deadlines retain wrap-safe target checks. Half-range and wrap regressions cover both forms. |
| Failed interface-reset callback hid possible physical effects | Shared-bus/reset provenance; **high** | Every attempt invalidates hardware state and starts tIDLE at callback completion while preserving the precise callback error and avoiding transport-health claims. |
| Recovery treated communication as acquisition-state proof | Recovery truthfulness; **high** | When state is unverified, status/interface probes prove communication only. Success requires Break plus soft reset, or hard/general-call reset, followed by CRC and diagnostic-bit validation. Passive-bind and interface-only regressions pass. |
| Raw probe CRC altered protocol diagnostics | Diagnostic neutrality; **medium** | Raw `probe()` is now neutral across logical, physical, protocol, and state diagnostics; tracked reads still count CRC faults. |
| Lifecycle/health guidelines described the old synchronous model | Ownership regression risk; **high** | `AGENTS.md`, public headers, and README now describe passive bind, one-callback polling, logical versus physical health, saturating counters, and local-state versus presence semantics. |
| Documentation gate omitted maintained report and failed parsing | Release documentation; **medium** | Doxyfile syntax was corrected, the maintained COM20 report remains in the input set, and CI installs/runs Doxygen with `WARN_AS_ERROR=YES`. |

### TunnelMonitor cleanup and integration disposition

| Finding | Affected contract / severity | Disposition |
| --- | --- | --- |
| C-01: post-probe NACK classified as absence | Phase-aware absence/transfer classification; **high integration correctness issue** | **Resolved** on TunnelMonitor commit `4db59a6`. Discovery-probe NACK/absent has its own expected-absence path; after ACK, command/read NACK uses transfer-failure accounting. Native tests cover command and frame-read NACK after successful discovery. |
| C-02: duplicate direct SHT3x protocol | Dependency and chip-protocol ownership; **external integration gate** | **Intentionally open.** Replacing the direct code requires an authoritative dependency decision, exact immutable SHT3x pin, owner-private adapter, native parity, and then deletion. Library types must not enter `include/TunnelMonitor/contracts/`. |

- The eventual adapter must use `bind()`, `HealthPolicy::OBSERVE_ONLY`,
  `JobRequest` with the owner request/deadline, `pollJob(..., 1, ...)`,
  `cancelJob()` on owner expiry, and milli-unit output. TunnelMonitor remains
  sole owner of discovery, candidate selection, fixed request/result capacity,
  64-bit completion time, retry, health projection, and bus recovery.

### Verification and physical gates

The pre-edit baseline passed 85/85 native tests, both Arduino targets, and all
repository guards. After implementation, the complete native suite passes
116/116; strict framework-neutral core compilation passes with C++17,
`-Wall -Wextra -Wpedantic -Werror`; pinned PlatformIO Arduino builds pass for
ESP32-S3 and ESP32-S2; and the core timing, Arduino/IDF CLI, IDF-example, HIL,
HIL-parser, version-metadata, Doxygen, and diff guards pass. TunnelMonitor C-01
passes its full 1052/1052 native suite and the pinned `tunnelmonitor_wifi`
production build. Independent reviews re-ran focused tests and documentation
validation. Final package inspection is performed from the committed branch
state at `5c7af7e`: the archive contains 42 files, is 122,513 bytes, has no
forbidden or missing required entries, and has SHA-256
`d2bde2392400350a1ae8edbbb97369ebf19649fa64f56f4da8810117a99229ae`.
The later test-only revision and this final audit file are excluded by
`library.json`, so committing them does not change those package contents.
`idf.py` and `gh` were unavailable in the inspected shell, so no new pure
ESP-IDF or live-CI result is claimed.

No new physical HIL was run. Native tests now cover address forwarding for
`0x45`, hot-return behavior, and forced phase-specific NACK/timeout/CRC faults;
that is not physical evidence. Physical `0x45`, hot-unplug/replug/replacement,
fault injection, ALERT pin behavior, humidity or temperature accuracy,
ESP32-S2 hardware, pure ESP-IDF hardware, shared-bus fault recovery, and an
uninterrupted production-duration soak remain open. Existing COM20 evidence
remains limited to its recorded `0x44` ESP32-S3 scenarios and partial long-run
boundary.

## Historical v1.6.1 audit (preserved)

Audit result: **strong protocol base, focused refactor required before integration**

SHT3x v1.6.1 already contains most of the chip work that TunnelMonitor should
not maintain itself. It has external transport injection, fixed memory,
per-word CRC checks, typed errors, no-clock-stretch single-shot support, and a
cooperative measurement job with an I2C instruction budget.

It is not suitable unchanged. The cooperative poll path has a real timing bug
that can violate the SHT3x 1 ms command spacing. Initialization and recovery
still perform several transactions and spin/yield waits in one public call.
The library also owns an OFFLINE latch and recovery policy that conflicts with
TunnelMonitor's I2C owner and runtime hotplug rules.

The recommended path is to refactor the existing driver, release and exact-pin
one reviewed revision, then replace the direct SHT3x code in `I2cTask` with one
narrow owner-private adapter. Do not keep a second SHT3x protocol
implementation in the firmware.

## Audit basis

The audit used these exact revisions:

| Repository | Revision | Notes |
| --- | --- | --- |
| TunnelMonitor-node | `fff99fe17e60b9287ec4d8d3eca5b3230ae44223` | Branch `prompt-44b-sequence`; current runtime ENV selection and direct SHT3x implementation |
| SHT3x | `113ecc67a082c844d062a402412b91eb7980202f` | `main`, `origin/main`, and annotated tag `v1.6.1` all resolved to this commit during the audit |

### Latest-branch revalidation

Revalidated on 2026-07-18 after `git fetch origin --prune --tags`. The remote
default branch is `origin/main`, and it is also the newest remote branch by
commit date. The local `main`, `origin/main`, and the dereferenced `v1.6.1` tag
still resolve to `113ecc67a082c844d062a402412b91eb7980202f`. The local branch is
zero commits ahead and zero commits behind. There is no commit delta from the
original audit revision. A fresh full-source re-audit still re-read the final
`Config`, public API, core implementation, native-test coverage, README, and
changelog rather than relying only on commit labels. It reconfirmed all eight
hard findings at their stated severity: the mixed-clock tIDLE defect, active
lifecycle I/O, mandatory OFFLINE admission, per-transfer health accounting,
pre-completion timestamps, missing bus-silent cancellation, missing milli-unit
output, and inaccurate transaction/wait documentation. No finding or release
gate changed.

Unless stated otherwise, SHT3x source references below mean v1.6.1 at the
commit above. TunnelMonitor references mean the revision above.

Primary chip facts were checked against the bundled Sensirion sources:

- [SHT3x-DIS datasheet, version 6](reference/vendor/SHT3x_datasheet.pdf),
  especially PDF pages 7 and 9 through 14;
- [SHT3x alert-mode application note, version 3.1](reference/vendor/SHT3x_HT_AN_AlertMode.pdf),
  especially PDF page 2; and
- the maintained [SHT3x chip notes](reference/sht3x-chip-notes.md).

The relevant PDF pages were rendered and visually checked. Poppler was not
installed, so PyMuPDF was used for rendering. The temporary page images were
removed after review.

This was a suitability audit. It changed no library source or TunnelMonitor
firmware, selected no production dependency, and ran no new physical hardware
tests.

## Decision summary

### Use after a focused refactor

The following are release gates for TunnelMonitor integration:

1. Fix the cooperative tIDLE gate so it compares timestamps from one
   microsecond timebase and cannot allow a same-millisecond follow-up transfer.
2. Split passive local binding from sensor I2C work. Binding a transport and
   address must perform zero I2C transactions.
3. Make initialization and any device recovery explicit cooperative jobs. One
   owner poll with budget one must cause at most one library transport call.
4. Add a health policy that does not latch OFFLINE or suppress I2C. TunnelMonitor
   must remain the sole health, retry, hotplug, and recovery authority.
5. If the standalone OFFLINE feature remains, update it from complete logical
   operations rather than individual command and read transfers.
6. Add zero-I2C cancellation for an active measurement job. Owner deadline
   expiry must not leave the next ENV poll permanently `BUSY`.
7. Base the conversion deadline and sample timestamp on time captured after the
   successful transport call, not on the caller's pre-transfer timestamp.
8. Add direct milli-degree Celsius and milli-percent output with 64-bit integer
   conversion.
9. Correct the public timing and transaction-count documentation, then add
   regression tests for the corrected contracts.
10. Qualify the exact refactored revision through a TunnelMonitor adapter on the
    real shared 400 kHz bus, including removal and return of the sensor.

Until these gates are complete, the current direct SHT3x path in TunnelMonitor
is the safer production baseline.

### Do not solve this with adapter band-aids

Avoid these long-term workarounds:

- Do not set `offlineThreshold` to a large value and hope it never latches.
- Do not call blocking `begin()` or `recover()` from the I2C owner and treat a
  timeout as acceptable.
- Do not call `end()` and destructive `begin()` on every optional-device miss.
- Do not duplicate CRC or raw conversion in the adapter because milli-unit
  helpers are missing.
- Do not keep the current direct firmware SHT3x path beside the library path.
- Do not add a second task, sensor registry, plugin system, or generic ENV
  framework.
- Do not let library reset callbacks reconfigure or recover the shared bus.
- Do not expose SHT3x library types in TunnelMonitor public contract headers.

The library should be corrected at its ownership and job boundaries. The
TunnelMonitor adapter should remain small.

## TunnelMonitor requirements

The integration target is narrower than the complete SHT3x feature set.

### Ownership and transport

- `I2cTask` is the only I2C owner. The library must not initialize the bus,
  configure pins, change clock or timeout settings, take a global bus handle,
  or run its own task.
- The production backend is ESP-IDF on ESP32-S3. The bus runs at 400 kHz on
  GPIO8/GPIO9.
- A normal `ReadEnv` owner poll permits one normal backend operation, with a
  20 ms transfer bound and a 1000 ms whole-operation deadline.
- The owner retains its existing retry accounting, metrics, deadline, and bus
  recovery policy. The SHT3x library must not add an independent retry or bus
  recovery loop.

Sources: TunnelMonitor
[I2C owner policy](../../TunnelMonitor-node/docs/guidelines/i2c_peripherals.md#owner-work-bounds),
[dependency policy](../../TunnelMonitor-node/docs/guidelines/dependency_policy.md),
and [I2C configuration](../../TunnelMonitor-node/include/TunnelMonitor/i2c/I2cConfig.h).

### Runtime sensor selection and hotplug

Every `ReadEnv` evaluates this fixed candidate set:

1. SHT3x at `0x44`;
2. SHT3x at `0x45`; and
3. BME280 at `0x76`.

The last successful candidate may be tried first, but the owner still evaluates
all three and selects the lowest valid address. It also reports multiple valid
candidates and sensor kind/address changes.

The owner probes each address before running chip protocol. Only a NACK from
that discovery probe is expected absence evidence. A device that ACKs the
probe and then fails its command, wait, frame read, or CRC is a real
transfer/protocol failure.

No valid ENV device is an optional disabled state. It must not degrade the
aggregate system health. A returned device must be usable without rebooting or
recreating the whole I2C owner.

Source: TunnelMonitor
[runtime ENV policy](../../TunnelMonitor-node/docs/guidelines/i2c_peripherals.md#L427).

### Measurement contract

TunnelMonitor uses high-repeatability, single-shot measurement with clock
stretching disabled. It needs:

- one command transfer;
- a nonblocking conversion wait;
- one six-byte read;
- CRC validation of both 16-bit words;
- signed milli-degrees Celsius;
- signed milli-percent relative humidity;
- no pressure validity for SHT3x; and
- a 64-bit owner completion timestamp.

The result must distinguish success, expected discovery absence, not ready,
transport failure, CRC/protocol failure, and owner deadline expiry.

The SHT3x datasheet gives a high-repeatability maximum of 15 ms at 2.4 V to
5.5 V and 15.5 ms below 2.4 V. The library currently rounds these to 15/16 ms
and adds a 1 ms safety margin, producing a safer 16/17 ms schedule. See the
[datasheet timing table](reference/vendor/SHT3x_datasheet.pdf#page=7) and
[SHT3x.cpp](../src/SHT3x.cpp#L130).

### What must remain outside the library

The following are application policy and must remain in TunnelMonitor:

- candidate order and lowest-address selection;
- generic discovery probing and expected-miss accounting;
- optional-device status, stale-data policy, and aggregate health;
- the one permitted automatic transfer retry;
- whole-operation deadline and scheduling priority;
- shared-bus recovery;
- sensor-changed and multiple-candidate events;
- conversion into `EnvReadResult` flags and pressure absence; and
- background poll cadence.

## What already fits

The library has a strong base worth refactoring.

### Correct ownership shape

The core takes transport callbacks and a caller context. It does not include
Arduino, `Wire`, ESP-IDF, FreeRTOS, or logging headers. It does not configure
pins or own a global bus. The callbacks receive an explicit requested timeout
and return a typed status.

Evidence: [Config.h](../include/SHT3x/Config.h#L32) and the repository core
guards.

### Bounded memory and clear instance ownership

The driver uses fixed-size buffers and has no steady-path heap allocation.
Copy and move are deleted, which prevents accidental duplication of mutable
driver state or its transport context.

Evidence: [SHT3x.h](../include/SHT3x/SHT3x.h#L128) and
[SHT3x.cpp](../src/SHT3x.cpp#L17).

### Useful typed protocol surface

The public API already has:

- `Repeatability`;
- `ClockStretching`;
- `Mode` and `PeriodicRate`;
- `RawSample` and `CompensatedSample`;
- `Status` and a useful transport error taxonomy;
- parsed status-register bits;
- address validation for `0x44` and `0x45`; and
- an explicit `PollJobResult` with instruction count and completion state.

This is much safer than leaking raw `esp_err_t`, `Wire` return codes, or raw
sensor frames into firmware services.

### Correct chip protocol coverage

The library sends 16-bit commands MSB first, rejects combined write/read for
the SHT3x command flow, applies CRC-8 polynomial `0x31` with initial value
`0xFF`, and checks each returned word separately. Measurement, status, serial,
and alert-limit paths have CRC handling. Alert writes append the required CRC.

Evidence: [CommandTable.h](../include/SHT3x/CommandTable.h),
[transport wrappers](../src/SHT3x.cpp#L1700), and
[frame decoding](../src/SHT3x.cpp#L2035).

### A useful cooperative measurement state machine

`requestMeasurement()` schedules a job. `pollJob(nowMs, 1, result)` separates
the single-shot command, conversion wait, and frame read. A normal poll step
performs at most one I2C instruction. `pollJob()` returns the exact status,
unlike the convenience `tick()` method.

Evidence: [SHT3x.cpp](../src/SHT3x.cpp#L276) and
[SHT3x.h](../include/SHT3x/SHT3x.h#L169).

This state machine should be corrected and reused. TunnelMonitor should not
create a second SHT3x state machine around blocking calls.

### Safe default measurement choice

Defaults are high repeatability, clock stretching disabled, and single-shot
mode. These match TunnelMonitor's current measurement shape. Measurement time
is derived from repeatability and supply range with an added 1 ms margin.

### Useful current validation

The current release has broad native coverage for CRC, fixed-point conversion,
status, reset, recovery, periodic modes, partial failures, and cooperative
measurement polling. The audited tree was clean and release metadata was
consistent.

## Hard findings

### H-01: the cooperative command-spacing gate can allow I2C too early

Severity: **release blocker**

The sensor requires at least 1 ms after a command before another command or
read phase. `pollJob()` checks this by multiplying its caller-supplied
millisecond value by 1000 and comparing it with `_lastCommandUs`, which was
captured from the independent microsecond callback.

Evidence:

- the poll gate is in [SHT3x.cpp:335-341](../src/SHT3x.cpp#L335);
- the command timestamp is captured in
  [SHT3x.cpp:1832-1841](../src/SHT3x.cpp#L1832); and
- the 1 ms requirement is stated on
  [datasheet PDF pages 9-10](reference/vendor/SHT3x_datasheet.pdf#page=9).

Example failure:

- a command finishes at `1000.900 ms`, so `_lastCommandUs` is `1000900`;
- the next owner poll is still in millisecond `1000`, so `nowMs * 1000` is
  `1000000`; and
- unsigned subtraction treats that 900 us backwards difference as a very
  large elapsed interval.

The gate can therefore open during the same millisecond. The periodic Fetch
Data path can attempt its receive phase before tIDLE. The result may be an
avoidable NACK or timing-dependent field behavior.

Required refactor:

- use `Config::nowUs` for the command-spacing comparison;
- keep the comparison wrap-safe;
- document that the microsecond clock is monotonic modulo 32 bits; and
- do not derive microseconds by multiplying a truncated millisecond timestamp.

Required regression:

- command at a non-zero microsecond remainder;
- another poll in the same millisecond before 1 ms elapsed;
- poll exactly at and after 1 ms;
- normal 32-bit microsecond wrap; and
- the periodic Fetch command/read transition.

### H-02: initialization and recovery are active blocking lifecycle calls

Severity: **release blocker for TunnelMonitor ownership**

`begin()` is not passive. In one call it attempts Break, waits 1 ms, attempts
soft reset, waits 2 ms, reads status through a command and read, and may start
periodic/ART mode. The early Break/reset errors are ignored and only the later
status result decides success.

Evidence: [SHT3x.cpp:150-268](../src/SHT3x.cpp#L150), especially
[SHT3x.cpp:232-260](../src/SHT3x.cpp#L232).

`recover()` can execute a status probe, interface reset callback, Break, soft
reset, hard-reset callback, general-call reset, and further probes in one
public call. Evidence: [SHT3x.cpp:1591-1697](../src/SHT3x.cpp#L1591).

The command-spacing and reset waits spin and call a cooperative-yield callback
until their deadline. They are bounded, but they still occupy the caller and
can execute many loop iterations. Evidence:
[SHT3x.cpp:1947-2012](../src/SHT3x.cpp#L1947).

This does not fit a runtime optional-device path where one owner poll must do
one bounded unit of I2C work and waits must be represented as future deadlines.

Required refactor:

1. Add passive local binding/configuration with zero I2C.
2. Move Break/reset/status verification into an explicit cooperative
   `EnsureIdle` or initialization job.
3. Advance that job with the same one-instruction poll primitive used by
   measurement.
4. Represent 1 ms and 2 ms waits as job deadlines. Do not spin.
5. Keep bus reset and general-call reset outside normal chip-driver recovery.

The existing synchronous helpers may remain as clearly named convenience APIs
for simple applications, but TunnelMonitor must not need them.

### H-03: mandatory OFFLINE latching conflicts with optional hotplug

Severity: **release blocker for TunnelMonitor ownership**

`offlineThreshold` cannot disable the latch. A value of zero is normalized to
one. When the threshold is reached, normal operations return `BUSY` and do not
touch I2C until `recover()` succeeds.

Evidence:

- [Config.h:180-187](../include/SHT3x/Config.h#L180);
- [SHT3x.cpp:224-230](../src/SHT3x.cpp#L224);
- [SHT3x.cpp:568-575](../src/SHT3x.cpp#L568); and
- [SHT3x.cpp:1804-1821](../src/SHT3x.cpp#L1804).

TunnelMonitor already probes optional candidates on every `ReadEnv`, owns
presence state, and must accept sensor return without a driver-owned recovery
decision. A second offline policy can turn expected removal into library
`BUSY`, hide a returned device, and create competing recovery counters.

Required refactor:

Add an explicit policy such as:

```cpp
enum class HealthPolicy : uint8_t {
  ObserveOnly,
  LatchOffline,
};
```

`ObserveOnly` may count transport outcomes for diagnostics, but it must never
suppress a caller-authorized I2C operation. TunnelMonitor should use this mode.
Standalone applications may use `LatchOffline` after H-04 is fixed.

Do not encode this policy as `offlineThreshold = 255`. That only delays the
ownership conflict.

### H-04: health is updated per transfer, not per logical operation

Severity: **high library correctness defect**

Every successful tracked transfer resets `_consecutiveFailures` to zero. A
logical status read is a successful command write followed by a read. If the
write always succeeds and every read fails, each next command write erases the
previous failure before the following read fails. The driver can remain
`DEGRADED` forever and never reach its advertised OFFLINE threshold.

Evidence:

- health reset and increment logic is in
  [SHT3x.cpp:1896-1932](../src/SHT3x.cpp#L1896); and
- the two-transfer status operation is in
  [SHT3x.cpp:2015-2032](../src/SHT3x.cpp#L2015).

The same issue affects other command-then-read operations.

Required refactor:

- count bus attempts separately if useful;
- update device health once when the complete logical operation succeeds or
  fails;
- keep expected not-ready separate from failure; and
- keep CRC/protocol failure separate from transport failure.

For TunnelMonitor `ObserveOnly` avoids admission gating, but the standalone
library contract must still be truthful.

### H-05: conversion and sample timestamps start before I2C completes

Severity: **high timing correctness issue**

After a single-shot command succeeds, `pollJob()` calculates readiness from the
`nowMs` value supplied before the write callback ran. A callback may consume
part of its 20 ms bound or wait for bus arbitration. That makes the conversion
deadline early by the callback duration.

Evidence: command execution and deadline assignment are adjacent in
[SHT3x.cpp:343-370](../src/SHT3x.cpp#L343).

The sample timestamp is also the pre-read `nowMs` captured by the poll lambda,
not a timestamp captured after the six-byte read completes. Evidence:
[SHT3x.cpp:320-332](../src/SHT3x.cpp#L320).

This is small on an idle bus but material on a shared, queued bus. It weakens
both the no-early-read guarantee and freshness diagnostics.

Required refactor:

- call the configured millisecond clock immediately after a successful command
  and use that value as conversion start;
- call it again immediately after a successful read and use that value as the
  sample timestamp; and
- keep TunnelMonitor's 64-bit completion timestamp authoritative outside the
  library.

### H-06: there is no zero-I2C measurement cancellation API

Severity: **high integration blocker**

TunnelMonitor owns a 1000 ms whole-operation deadline. The library must let the
owner abandon local job state when that deadline expires.

`pollJob()` deliberately keeps the measurement pending for `TIMEOUT` so a
later poll can retry the phase. Evidence:
[SHT3x.cpp:360-365](../src/SHT3x.cpp#L360) and
[SHT3x.cpp:432-437](../src/SHT3x.cpp#L432).

There is no public cancel method. `end()` clears state but also unbinds the
driver, while a new `begin()` performs destructive startup I2C. Using that pair
for every owner deadline or sensor removal would be a band-aid.

Required refactor:

Add a method with a narrow contract, for example:

```cpp
Status cancelMeasurement();
```

It must:

- perform zero I2C;
- clear the active measurement phases and ready deadline;
- preserve the last-good cached sample;
- report whether physical state may still contain an unread single-shot
  result; and
- leave the passive binding valid for the next owner-controlled attempt.

Periodic/ART cancellation may require a separate explicit Break job. Do not
hide Break inside local cancellation.

### H-07: fixed-point output does not match TunnelMonitor units

Severity: **required API improvement for clean integration**

`CompensatedSample` provides centi-degrees Celsius and centi-percent humidity.
TunnelMonitor requires milli-degrees Celsius and milli-percent humidity.

Evidence: [SHT3x.h:34-38](../include/SHT3x/SHT3x.h#L34) and TunnelMonitor
[EnvPowerDisplay.h](../../TunnelMonitor-node/include/TunnelMonitor/contracts/EnvPowerDisplay.h).

The firmware can currently get raw words and repeat the datasheet formula, but
that leaves conversion policy in two places. Multiplying the centi result by
10 loses the extra fixed-point resolution and changes current rounding.

Required refactor:

Add direct integer helpers using 64-bit intermediates:

```cpp
static int32_t convertTemperatureMilliCelsius(uint16_t raw);
static int32_t convertHumidityMilliPercent(uint16_t raw);

struct MeasurementMilli {
  int32_t temperatureMilliCelsius;
  int32_t humidityMilliPercent;
};
```

Document the rounding rule. Test raw `0`, `65535`, midpoints, and values around
zero degrees Celsius. Do not use float in the TunnelMonitor adapter.

The existing `getMeasurement()` also reconstructs float output from the
already rounded centi values instead of converting the raw sample directly.
Evidence: [SHT3x.cpp:631-644](../src/SHT3x.cpp#L631). Correcting that would make
the public float API match its apparent precision.

### H-08: public transaction and wait documentation does not match the code

Severity: **high contract/documentation issue**

The public header says `requestMeasurement()` sends the single-shot command
immediately. The README transaction table also counts one command write in
that call. The implementation only schedules `MeasurementPhase::SINGLE_SHOT_COMMAND`;
the first `pollJob()` sends the command.

Evidence:

- [SHT3x.h:277-282](../include/SHT3x/SHT3x.h#L277);
- [README.md:376-382](../README.md#L376); and
- [SHT3x.cpp:568-598](../src/SHT3x.cpp#L568).

The changelog says command-delay and reset/break guards return
`IN_PROGRESS`/`TIMEOUT` instead of spinning/yielding internally, but
`_ensureCommandDelay()` and `_waitMs()` still spin/yield. Evidence:
[CHANGELOG.md:126-132](../CHANGELOG.md#L126) and
[SHT3x.cpp:1947-2012](../src/SHT3x.cpp#L1947).

The README says `tick()` single-shot work is a receive-only read, but the first
tick after a request is the command write. It also describes periodic tick as
Fetch plus read while the current one-instruction convenience tick spreads
those phases across calls.

Transaction counts and blocking behavior are part of the integration contract,
not cosmetic documentation. Correct the docs with the refactor and enforce the
important counts in tests.

## Important secondary findings

### S-01: the poll budget is broader than current behavior

`pollJob()` accepts `maxInstructions`, but each current call performs at most
one instruction even when the caller supplies more. This is safe for
TunnelMonitor, which will always pass one, but the public contract is more
general than the implementation.

Prefer one of these simple choices:

- rename it to a one-step API and remove the unused generality; or
- document that the current maximum is one and larger budgets are reserved.

Do not add a loop merely to consume a larger budget.

### S-02: transport and protocol health need separate diagnostics

CRC failures are returned through the measurement status but do not update
transport health. That separation is reasonable, but the library has no small
bounded protocol-failure counter.

Useful counters are:

- logical operation failures;
- transport failures;
- CRC failures; and
- expected not-ready responses.

Use saturating fixed-width counters. Do not add logs, dynamic history, or an
event queue to the library.

### S-03: cached `Status::msg` relies on caller discipline

`Status` documents `msg` as a static string, but a transport callback can
return any pointer. The driver stores the returned `Status` in `_lastError`.
A callback that returns stack or temporary text leaves a dangling pointer.

TunnelMonitor can avoid the issue by returning only static literals and
mapping the library enum immediately. For a future major API, a message-free
status plus enum/detail is safer for embedded contracts.

This is not a blocker for the planned adapter if the static-string rule is
enforced in its tests.

### S-04: raw command access is intentionally unchecked

`readCommand()` returns unchecked bytes and makes the caller own response
length, CRC, and mode legality. That is acceptable as an expert diagnostic
escape hatch, but it must not be used by the TunnelMonitor production adapter.

Consider naming it `readCommandUnchecked()` in a future major version. Keep the
typed measurement, status, serial, and alert methods as the normal surface.

### S-05: release builds are not fully reproducible

The application can exact-pin SHT3x v1.6.1 by commit, and the current remote
tag is consistent. The library's own CI still installs moving tool versions:

- latest PlatformIO is installed at CI run time;
- `platform = espressif32` is not version-pinned; and
- the ESP-IDF container follows mutable `release-v5.4`.

Pin the build platform and major compiler/tool inputs before using CI output as
a reproducible release artifact. Pinning every GitHub Action by commit is not a
TunnelMonitor adoption gate.

### S-06: the large native test file is harder to maintain

The 85 native tests are in one 2872-line file and use `#define private public`
for internal checks. This is not an integration blocker. Over time, split
public contract/transport/fault tests from private helper tests. Do not delay
the focused correctness refactor for a broad test-framework rewrite.

## TunnelMonitor cleanup discovered by this audit

This section is not a defect in the SHT3x library, but it must be fixed during
integration.

### C-01: post-probe SHT3x NACKs are classified as expected absence

TunnelMonitor's `recordEnvCandidateFailure()` treats every NACK as expected ENV
absence. It is called after the discovery probe, the SHT3x command write, and
the six-byte measurement read.

Evidence:

- [I2cTask.cpp:2209-2220](../../TunnelMonitor-node/src/i2c/I2cTask.cpp#L2209);
- [command failure path](../../TunnelMonitor-node/src/i2c/I2cTask.cpp#L2395);
  and
- [read failure path](../../TunnelMonitor-node/src/i2c/I2cTask.cpp#L2418).

This can turn a device that ACKed discovery and then disappeared, timed out, or
NACKed the measurement read into optional disabled health instead of a real
failure. It can also hide an early no-clock-stretch read as absence.

The refactor must carry phase-aware typed results:

- only discovery-probe NACK is expected absence;
- command NACK after successful probe is a transfer failure;
- read-header NACK is `NotReady` only when the backend can prove that exact
  phase and the owner deadline still allows retry; and
- CRC mismatch is a protocol failure.

Do not preserve the current classification in the adapter.

### C-02: direct SHT3x protocol must be deleted after parity

Current firmware owns:

- command bytes `0x24 0x00`;
- the 15 ms wait literal;
- six-byte frame layout;
- CRC-8 implementation; and
- raw-to-milli conversion.

Evidence: [I2cTask.cpp:472-510](../../TunnelMonitor-node/src/i2c/I2cTask.cpp#L472)
and [I2cTask.cpp:2395-2451](../../TunnelMonitor-node/src/i2c/I2cTask.cpp#L2395).

Once the library adapter passes parity tests, delete these helpers and phases.
Keeping both paths would create two sources of chip truth.

## Recommended narrow library design

### 1. Passive binding

Refactor lifecycle into a local operation and explicit hardware jobs.

A passive method, named for example `bind()` or `configure()`, should:

- validate transport callbacks, time callbacks, address, timeout, and enums;
- copy the fixed configuration;
- reset local job state;
- perform zero I2C;
- perform no wait; and
- remain valid when the sensor is absent.

`end()` or `unbind()` should also be local and zero-I2C. If hardware shutdown,
Break, or reset is required, expose it as an explicit operation.

### 2. Reuse one cooperative job engine

Keep the existing measurement phases and add only the lifecycle phases that
have a concrete caller:

- ensure single-shot idle after owner startup or a proven protocol-state fault;
- single-shot command;
- conversion wait;
- single-shot read; and
- optional explicit device-reset recovery.

Each `pollJob(..., 1, ...)` call must perform zero or one transport callback.
All waits are deadlines. Do not create a second wrapper state machine in the
library or firmware.

TunnelMonitor does not need periodic, ART, heater, alert, or general-call reset
in its normal ENV path. Existing support can remain, but it must not complicate
the forced-measurement job.

### 3. Separate device action from bus recovery

Useful explicit device actions are:

- `EnsureIdle`: Break, wait, optional soft reset, status verification;
- `SoftReset`: device-addressed command only; and
- `CancelLocal`: zero-I2C local job cancellation.

Interface reset, SCL pulses, bus reinitialization, and general-call reset are
shared-bus policy. The chip library may expose command bytes or a callback
contract for a standalone application, but it must never invoke shared-bus
recovery automatically in TunnelMonitor mode.

### 4. Keep error reporting phase-aware

The existing `Err` enum is a good base. Preserve specific transport failures.
Add small pure helpers rather than a new error framework:

```cpp
constexpr bool isTransportError(Err error);
constexpr bool isProtocolError(Err error);
constexpr bool isExpectedNotReady(Err error);
constexpr bool isAbsent(Err error);
```

`PollJobResult` should expose the current semantic phase or action when useful
for mapping and tests. Do not expose driver internals or a dynamic trace.

### 5. Make time ownership explicit

Use:

- `nowUs` only for the short 1 ms tIDLE interval;
- `nowMs` for conversion, reset, backoff, and owner wake deadlines; and
- wrap-safe elapsed-duration comparisons.

Capture time after each completed transport. Document the required relation
between externally supplied `pollJob(nowMs, ...)` and `Config::nowMs`. Better,
let the driver sample its configured clock after I2C so the caller cannot
accidentally provide a stale completion time.

## Useful types, enums, and helpers

### Needed for the refactor

```cpp
enum class Address : uint8_t {
  Primary = 0x44,
  Alternate = 0x45,
};

enum class HealthPolicy : uint8_t {
  ObserveOnly,
  LatchOffline,
};

struct MeasurementMilli {
  int32_t temperatureMilliCelsius;
  int32_t humidityMilliPercent;
};
```

Also needed:

- zero-I2C `cancelMeasurement()`;
- `convertTemperatureMilliCelsius()`;
- `convertHumidityMilliPercent()`;
- a passive bind result;
- explicit logical operation outcome for health; and
- phase-aware `PollJobResult` diagnostics.

The current `Repeatability`, `ClockStretching`, `RawSample`, `StatusRegister`,
and transport error enums should be reused.

### Nice to have

- `nextPollDeadlineMs()` so an owner can sleep until useful work;
- `measurementDurationMs(repeatability, lowVdd)` as a pure helper independent
  of mutable driver state;
- `crc8Word(uint16_t)` or a small public frame-decoding helper for adapter and
  fixture tests;
- `toString(Err)` in examples only, returning static text;
- saturating protocol counters; and
- a `hasFreshSample` helper that combines cached-sample presence and age
  without changing ownership of stale policy.

Keep these helpers fixed-size and side-effect free. They do not justify a new
manager, registry, allocator, or logging layer.

### Not needed for TunnelMonitor platformization

- a generic environmental sensor base class;
- dynamic device discovery inside the SHT3x library;
- runtime calibration storage;
- pressure fields in SHT3x output;
- alert event queues;
- an internal RTOS task or mutex;
- automatic heater use;
- automatic general-call reset; or
- framework-specific core adapters.

## Recommended TunnelMonitor integration flow

After the library refactor and release:

1. Exact-pin the reviewed SHT3x revision in the TunnelMonitor production
   environment.
2. Add one owner-private adapter under `src/i2c`; do not add library includes to
   public contract headers.
3. Keep two fixed driver instances, one for `0x44` and one for `0x45`, owned by
   `I2cTask`.
4. Pass the existing owner backend through static transport thunks and a fixed
   context pointer.
5. Configure a 20 ms requested transfer timeout, high repeatability,
   no clock stretching, `HealthPolicy::ObserveOnly`, and general-call reset
   disabled.
6. Passively bind both instances during I2C-owner setup. Absence must not make
   binding fail.
7. For each candidate, keep the current generic owner probe first.
8. After a successful probe, schedule one single-shot library job and advance
   it with budget one from owner polls.
9. On library completion, map the milli sample into `EnvReadResult`. Set only
   temperature and humidity validity; pressure remains zero and invalid.
10. On owner deadline, cancel local job state without I2C and continue the
    existing owner failure path.
11. Map only the discovery-probe NACK to expected absence. Map later transport,
    not-ready, CRC, and protocol outcomes explicitly.
12. Evaluate all ENV candidates and retain TunnelMonitor's deterministic
    lowest-address selection.
13. After native and HIL parity, delete direct command, timing, CRC, and
    conversion code from `I2cTask`.

Use `pollJob()` rather than the void `tick()` so the owner receives the exact
status at the call site.

## Validation evidence and gaps

### Verification performed during this audit

| Check | Result |
| --- | --- |
| `pio test -e native` | PASS, 85/85 tests |
| `pio run -e esp32s3dev` | PASS, RAM 6.9%, flash 30.5% |
| `pio run -e esp32s2dev` | PASS, RAM 11.1%, flash 28.4% |
| `python tools/check_core_timing_guard.py` | PASS |
| `python tools/check_cli_contract.py` | PASS |
| `python tools/check_idf_example_contract.py` | PASS |
| `python tools/check_hil_contract.py` | PASS |
| `python tools/test_run_i2c_hil_parser.py` | PASS |
| `python scripts/generate_version.py check` | PASS |
| `pio pkg pack` plus archive inspection | PASS; generated archive removed |
| `git diff --check` before report creation | PASS |

`idf.py` was not installed locally, so the pure ESP-IDF example was not built
in this shell. The repository defines ESP-IDF CI jobs, but `gh` was also not
installed, so live GitHub Actions state was not independently queried.

### Existing hardware evidence

The strongest maintained report is
[COM20 validation](reports/hil-validation-COM20-20260629.md).

Useful evidence:

- ESP32-S3 at SHT3x address `0x44`;
- a short 47/47 command smoke pass;
- no-clock-stretch single-shot modes;
- clock-stretch, periodic, ART, status, reset, serial, and alert paths;
- 862,912 reported successful driver transfers and zero driver failures before
  the last long-run host CLI timeout; and
- current core source is unchanged between the HIL firmware commit and the
  audited tag.

Limits:

- no uninterrupted 16-hour pass;
- no hardware test at `0x45`;
- no hot-unplug/replug;
- no forced command/read NACK, CRC corruption, or stuck-bus fixture;
- no calibrated temperature/humidity accuracy validation;
- no ESP32-S2 hardware run; and
- no pure ESP-IDF hardware run.

The long-run failure was a diagnostic CLI/host transcript timeout, not an
observed SHT3x transport failure. It is still not valid to call the long soak
complete.

### Existing TunnelMonitor evidence

The current direct implementation has useful shared-bus evidence at 400 kHz.
One retained run reports 958 successful ENV reads over a 1 m lead with no I2C
timeouts, NACKs, queue overflows, or failed recoveries. Mixed
RTC/ENV/power/display/FRAM stress also stayed live and bounded.

Sources:

- [400 kHz experiment](../../TunnelMonitor-node/docs/reports/hil-testing/condensed/i2c_400khz_experiment_hil_20260705.md);
- [400 kHz intensive stress](../../TunnelMonitor-node/docs/reports/hil-testing/condensed/i2c_400khz_payload128_fram124_display64_intensive_stress_20260705.md).

The library adapter should match or improve this baseline.

## Required tests after refactor

### Library native tests

1. Passive bind and unbind perform zero transport calls.
2. `0x44` and `0x45` are both forwarded correctly on successful operations.
3. A non-zero microsecond remainder cannot open tIDLE early.
4. tIDLE remains correct across microsecond wrap.
5. Conversion readiness starts after the command callback completes.
6. Sample timestamp is captured after the read callback completes.
7. High-repeatability no-stretch uses `0x2400`.
8. All repeatability and low-VDD timing values match the datasheet plus the
   documented margin.
9. `pollJob(..., 1, ...)` causes at most one transport callback.
10. Cancel in command, wait, and read phases performs zero I2C and preserves
    the previous sample.
11. Repeated successful command writes followed by failed reads produce logical
    operation failures and, in latch mode, eventually OFFLINE.
12. Observe-only mode never suppresses a later owner-authorized attempt.
13. Removal and return allow a new successful measurement without destructive
    reinitialization.
14. Temperature CRC failure and humidity CRC failure are tested separately.
15. A failed new frame never presents the old sample as a newly completed
    measurement.
16. Milli conversion endpoints, midpoint, negative temperature, and rounding
    vectors pass with 64-bit intermediates.
17. Public single-shot scheduling crosses 32-bit millisecond and microsecond
    wrap safely.
18. Documentation transaction counts are represented by contract tests.

### TunnelMonitor adapter tests

1. The adapter is called only from `I2cTask`.
2. Library types do not enter `include/TunnelMonitor/contracts`.
3. The adapter forwards the 20 ms transfer timeout.
4. One owner poll advances at most one library transport step.
5. The six-byte result read is receive-only at the library transport boundary.
6. Owner retry remains the only retry policy.
7. Discovery NACK stays expected absence and does not change global errors.
8. Command NACK after successful probe is a transfer failure.
9. Read NACK after successful probe is failure or proven `NotReady`, not
   absence.
10. Temperature and humidity CRC failures map to protocol failure.
11. Owner deadline cancels library state and completes the command once.
12. Exact milli temperature and humidity values match current contract vectors.
13. Pressure remains zero and invalid.
14. `0x45` alone is selected correctly.
15. Both SHT3x addresses plus BME280 are evaluated; lowest valid address wins.
16. Multiple-candidate and sensor-changed flags remain correct.
17. Remove during command, remove during conversion/read, return, and replace
    with another sensor all remain bounded.
18. Direct SHT3x command, CRC, timing, and conversion helpers are absent after
    migration.

### Physical TunnelMonitor HIL

Run on ESP32-S3-N16R8 hardware revision 2.0.0 using the production ESP-IDF
backend:

- shared bus at 400 kHz on GPIO8/GPIO9;
- 20 ms owner transfer bound;
- normal mixed RTC, FRAM, ENV, power, and display traffic;
- repeated single-shot measurements at `0x44`;
- `0x45` strap or a separate fixture;
- removal before command, during the wait, and before the read where safe;
- return at the same address and replacement at the other address;
- power cycle and MCU-only reset, if the hardware permits separate behavior;
- bounded NACK/timeout fault injection without risking other bus devices; and
- an overnight mixed-bus soak with final queue, deadline, retry, and health
  counters retained.

Do not make ALERT, periodic, ART, heater, or calibrated accuracy qualification
a TunnelMonitor gate unless the product starts using those features.

## Physical suitability boundary

The SHT30, SHT31, and SHT35 use the same command protocol. Their accuracy
grades differ; the library and serial-number transaction do not prove which
grade is assembled.

At TunnelMonitor's expected 3.3 V supply, high-repeatability conversion has a
15 ms datasheet maximum. The library's 16 ms normal-voltage schedule includes
a reasonable margin. The device supports 400 kHz I2C.

The library cannot qualify:

- sensor placement in the enclosure;
- response time through filters or membranes;
- condensation or chemical exposure;
- PCB heat influence;
- pull-up sizing and bus capacitance;
- supply decoupling;
- actual temperature/humidity accuracy; or
- the exact SHT3x variant fitted.

The heater is for plausibility checks and condensation mitigation, not normal
measurement. Do not enable it automatically in the library or adapter.

The sensor datasheet recommends 100 nF local decoupling and external I2C
pull-ups sized for the bus. Those are hardware-review items, not driver
features.

## Recommended implementation order

1. Fix H-01 and add fractional-millisecond and wrap regressions.
2. Split passive binding from hardware initialization.
3. Extend the existing cooperative job engine to explicit ensure-idle/reset
   work; remove spin waits from the owner-facing path.
4. Add observe-only health and correct logical-operation health accounting.
5. Add cancellation and post-I2C time capture.
6. Add milli-unit types/helpers and correct float conversion from raw values.
7. Correct README, header, changelog, and API transaction tables.
8. Add missing native tests, especially `0x45`, humidity CRC, hotplug, and
   phase-specific failures.
9. Release and exact-pin one immutable revision.
10. Build the narrow TunnelMonitor adapter and phase-aware error mapping.
11. Run native parity tests, delete direct firmware SHT3x protocol, and run all
    TunnelMonitor native tests and production builds.
12. Run exact-board shared-bus HIL and retain condensed evidence.

## Final assessment

SHT3x v1.6.1 is a strong base and should be refactored rather than replaced by
another firmware-local register implementation. Its basic command table, CRC,
frame decoding, transport injection, fixed memory, and cooperative measurement
phases are good.

The current release is not a safe drop-in dependency. The tIDLE bug is a hard
correctness issue. Active lifecycle calls, mandatory OFFLINE latching, missing
cancellation, pre-I2C timestamps, and centi-only output do not fit
TunnelMonitor's owner and hotplug contracts.

Make the core passive and cooperatively driven, keep policy in `I2cTask`, pin
the corrected revision, and delete the duplicate firmware protocol after
parity. That is the simplest safe path for platformizing TunnelMonitor without
building a broad sensor framework.
