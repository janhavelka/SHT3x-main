# TunnelMonitor-node suitability audit

## SHT3x environmental sensor library

- Original audit: 2026-07-18
- Last updated: 2026-07-22

## Maintained implementation disposition

This document records the maintained implementation disposition. The
full pre-implementation v1.6.1 assessment remains available at its immutable
baseline commit, linked under **Historical source** below. A finding marked
implemented here is not a hardware-validation claim. The owner-safe
implementation and follow-up self-audit fixes are committed as `be2c18a`,
`000fbd9`, `d79b485`, `5c7af7e`, and `e25046b`; release metadata and primary
documentation are in `ae61b9e` and `7fe1fca`. The published annotated v1.7.0
tag and `main` are at `5409793f9f6e69f4dcd3b106621653e2a31caf4e`; changes
between the earlier `5e478af` audit state and that tag were documentation-only.
Independent core, validation, and traceability reviews drove the follow-up
fixes. The physical gates listed below remain open.

### Re-audit baselines and scope

| Repository | Re-audit baseline | Working-tree note |
| --- | --- | --- |
| SHT3x | Historical audit baseline `cf2ffad8eee28341edce74b05f06c12c8d71f7b6`; v1.6.1 source/tag commit `113ecc67a082c844d062a402412b91eb7980202f`; published annotated v1.7.0 tag commit `5409793f9f6e69f4dcd3b106621653e2a31caf4e` | The release is on `main`; no separate hardening branch is required for consumption. |
| TunnelMonitor-node | Read-only planning baseline observed on 2026-07-22: `prompt-45-platformization` at `b9cf56d164e86ed94ca2924f835173236f42638d` | The worktree contained an unrelated active edit in `src/measurement/ProfileSampleBuilder.cpp` and was not modified, built, committed, branched, or reset by this audit. The earlier read-only `develop` baseline and discarded C-01 experiment remain historical evidence only. |

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
| TunnelMonitor C-01 experiment (not retained) | Historical TunnelMonitor commit `4db59a6` | Demonstrated phase-aware NACK classification and native coverage, then was removed without merging. It is evidence for the recommendation, not a current TunnelMonitor implementation. |

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
| C-01: post-probe NACK classified as absence | Phase-aware absence/transfer classification; **high integration correctness issue** | **Open outside this library.** The discarded experiment at `4db59a6` showed the intended correction and passed native tests, but no TunnelMonitor change is retained. During any later authorized integration, only discovery-probe NACK/absent should use the expected-absence path; command/read NACK after ACK should use transfer-failure accounting. |
| C-02: duplicate direct SHT3x protocol | Dependency and chip-protocol ownership; **external integration gate** | **Intentionally open.** Replacing the direct code requires an authoritative dependency decision, exact immutable SHT3x pin, owner-private adapter, native parity, and then deletion. Library types must not enter `include/TunnelMonitor/contracts/`. |

- The eventual adapter must use `bind()`, `HealthPolicy::OBSERVE_ONLY`,
  `JobRequest` with the owner request/deadline, `pollJob(..., 1, ...)`,
  `cancelJob()` on owner expiry, and milli-unit output. TunnelMonitor remains
  sole owner of discovery, candidate selection, fixed request/result capacity,
  64-bit completion time, retry, health projection, and bus recovery.
- Initial use and first use after owner recovery must run staged
  `requestEnsureIdle()` reconciliation; zero-I2C `bind()` alone does not prove
  physical acquisition state. The binding must supply the required
  `cooperativeYield` callback even though the owner-safe path does not invoke it.
- Preserve the owner's 64-bit deadline/completion values while passing the low
  32 bits into the library's wrap-safe deadline. Validate exact callback byte
  counts, never retry or recover inside callbacks, and do not advertise
  `READ_HEADER_NACK` unless the backend can actually distinguish that phase.
- Only the discovery probe may classify NACK as absence. A NACK after probe
  success is a transfer failure. Library `READY` is local health/admission
  state, not presence or application-health proof.
- The v1.7.0 milli conversion rounds to nearest; the current TunnelMonitor
  direct codec truncates and can differ by one milli-unit. Integration should
  adopt `getMeasurementMilli()` and record that intentional precision fix
  instead of duplicating the legacy conversion.

### Verification and physical gates

The pre-edit baseline passed 85/85 native tests, both Arduino targets, and all
repository guards. After implementation, the complete native suite passes
116/116; strict framework-neutral core compilation passes with C++17,
`-Wall -Wextra -Wpedantic -Werror`; pinned PlatformIO Arduino builds pass for
ESP32-S3 and ESP32-S2; and the core timing, Arduino/IDF CLI, IDF-example, HIL,
HIL-parser, version-metadata, Doxygen, and diff guards pass. The discarded
TunnelMonitor C-01 experiment passed its full 1052/1052 native suite and the
pinned `tunnelmonitor_wifi` production build before removal; that result is
historical design evidence, not a claim about retained TunnelMonitor code.
The TunnelMonitor worktree remains outside this library change. Independent
reviews re-ran focused tests and documentation validation. Final package
inspection is performed from the committed branch
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

## Historical source

The complete pre-implementation v1.6.1 assessment remains available in the
[immutable baseline report](https://github.com/janhavelka/SHT3x-main/blob/cf2ffad8eee28341edce74b05f06c12c8d71f7b6/docs/TUNNELMONITOR_NODE_SUITABILITY_AUDIT.md).
It contains the original line-by-line findings, evidence, recommended design,
and validation plan. It is intentionally not duplicated here because the
current dispositions and open gates above are authoritative.
