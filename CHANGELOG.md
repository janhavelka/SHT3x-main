# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.7.1] - 2026-07-22

### Changed
- Aligned README, hardware, ESP-IDF, and documentation-index status with the
  audited v1.7.0 software results while preserving the v1.6.1-only physical
  hardware claim boundary.
- Added the restored suitability audit to Doxygen input, enabled undocumented
  public-symbol warnings as errors, moved generated HTML to ignored
  `.doxygen/`, and clarified cooperative progress/timing-hook API comments.
- Updated the HIL documentation guard to require the maintained COM20 evidence
  boundary after removing the superseded COM17 summary.
- Updated GitHub Actions to their Node.js 24-based major versions.

### Fixed
- Excluded nested documentation index files from the Doxygen content input so
  Doxygen 1.9.8 no longer mistakes them for additional main pages.

### Removed
- Removed three completed prompt-capture files that remained under
  `docs/prompts/` despite the maintained-documentation boundary.
- Removed two superseded COM20 reports that only recorded blocked pre-validation
  attempts; the final maintained COM20 validation report remains.
- Removed the older v1.5.0 COM17 summary after the broader v1.6.1 COM20 report
  superseded its successful default-run evidence.
- Compacted the maintained suitability audit to current dispositions and open
  gates; the full pre-implementation assessment remains available at its
  immutable baseline commit.

## [1.7.0] - 2026-07-19

### Added
- Passive zero-I2C `bind()` lifecycle plus the bounded `requestEnsureIdle()`
  reconciliation job for external bus-owner tasks.
- Correlated `JobRequest` and phase-aware `PollJobResult` identity, outcome,
  deadline, exactly-once terminal, and partial/indeterminate-effect semantics.
- Zero-I2C `cancelJob()`/`cancelMeasurement()` and `HealthPolicy::OBSERVE_ONLY`.
- Signed milli-degree Celsius/milli-percent measurement output and conversion
  helpers using rounded 64-bit integer arithmetic.
- Separate saturating logical-operation, transport, CRC/protocol, and expected
  not-ready diagnostics, plus explicit hardware-cache verification state.
- Pure constexpr transport/protocol/not-ready/absence classifiers for
  phase-aware external-owner error mapping.

### Changed
- `pollJob()` now performs at most one transport callback per call, including
  periodic Fetch Data command/read flows, and bus-silent waits consume no work.
- Logical health is completed only after the final transfer; intermediate
  command success no longer erases a preceding failed-read streak.
- Conversion readiness and sample timestamps are captured after their transport
  callbacks complete, using wrap-safe microsecond command spacing.
- Raw command escape hatches explicitly invalidate verified hardware state, and
  cached transport errors use library-owned static messages.
- PlatformIO, the ESP32 platform package, and the ESP-IDF CI container are
  version-pinned. The version generator now synchronizes ESP-IDF and Doxygen
  metadata in addition to `Version.h`.

### Fixed
- Fixed fractional-millisecond and microsecond-wrap tIDLE enforcement.
- Fixed stale-result risk by preserving the previous sample until a complete,
  CRC-valid, in-deadline replacement frame succeeds.
- Preserved v1.6 positional aggregate initialization by keeping new `Config`
  and `SettingsSnapshot` fields append-only; made self-rebind and invalid rebind
  transactional and zero-I2C.
- Corrected cooperative terminal effects, post-callback deadline handling,
  ambiguous-write command spacing, and long-runtime periodic timestamp validity
  across the uint32 millisecond wrap.
- Prevented active ensure-idle work from leaking settle timestamps or accepting
  concurrent configuration mutation, and separated completed late/CRC-invalid
  frames from genuinely pending or indeterminate hardware effects.
- Corrected public transaction-count, blocking, health-policy, cancellation,
  and ownership documentation; superseded the TunnelMonitor suitability audit
  with traceable finding dispositions.
- Rejected cooperative reconciliation and synchronous initialization when
  status verification reports command/write-checksum errors, preserved unread
  samples across zero-I2C cancellation, and closed long-idle, deadline, and
  reset-callback timing edge cases.
- Kept ensure-idle settle deadlines separate from preserved sample metadata and
  required recovery from unverified state to prove idle acquisition with a
  reset sequence instead of treating a status probe or interface reset as proof.

## [1.6.1] - 2026-06-29

### Added
- Added COM20 ESP32-S3/SHT3x hardware validation report covering destructive
  serial HIL, post-reboot smoke validation, and incomplete long-soak evidence.
- Added a low-USB Arduino diagnostic `i2c_soak <seconds>` command for
  duration-based SHT3x I2C stability soaks.

### Changed
- Simplified `docs/` to maintained guides plus source reference material:
  `docs/hardware.md`, `docs/esp-idf.md`, `docs/reference/`, and curated HIL
  evidence under `docs/hil/`.
- Merged the original compact SHT3x `00` through `08` chip-documentation notes
  into `docs/reference/sht3x-chip-notes.md` so protocol, timing, status,
  ALERT, reset, and variant facts remain preserved.
- Hardened the host serial HIL runner with incremental `progress.jsonl`
  evidence, async periodic/ART measurement nudging, and structured serial
  exception failures.
- Changed duration-based HIL soaks to use the firmware-side low-output
  `i2c_soak` command instead of thousands of host-issued serial stress
  commands.
- Documented the v1.6.1 release boundary as core SHT3x functionality and I2C
  stability, without claiming uninterrupted 16-hour HIL or host CLI liveness.

### Removed
- Removed active audit leftovers, prompt captures, implementation reports, and
  generated vendor-PDF Markdown extracts from `docs/`.

## [1.6.0] - 2026-06-01

### Added
- Public `StatusReadSnapshot` and `readStatusWithModeRestore()` for explicit
  ALERT/status diagnosis that breaks and restores periodic/ART acquisition.
- `SettingsSnapshot::statusReadStatus` so failed or intentionally skipped
  status reads are visible instead of hidden behind `statusValid=false`.
- ESP-IDF component metadata for building the framework-neutral core with `idf_component_register`.
- ESP-IDF basic diagnostic example with an application-owned `i2c_master` bus/device and SHT3x transport callbacks.
- Arduino `examples/common/Sht3xCli.*` bringup command processor for diagnostics.
- `tools/check_idf_example_contract.py` to keep the ESP-IDF example on the same CLI contract.
- Pure ESP-IDF CI matrix for the native example on `esp32s3` and `esp32s2`.
- Hardware validation matrix and API latency/transaction documentation.
- Pre-HIL runbook and HIL log template to keep hardware validation evidence
  explicit and auditable.
- Host-side serial I2C HIL runner, contract guard, target template, and
  self-test report for auditor-ready hardware evidence capture.
- Curated ESP32-S3/COM17 serial HIL evidence summary for the 2026-06-01
  default command run under `docs/hil/`.
- Framework-neutral private timing/yield shim; real timing is supplied by application callbacks.
- `docs/IDF_PORT_IMPLEMENTATION.md` with the implemented port structure, validation notes, and remaining hardware checks.

### Changed
- `SHT3x` copy and move construction/assignment are now explicitly deleted to
  prevent duplicate mutable driver instances targeting the same physical sensor.
- Active periodic/ART status reads now use a documented explicit helper;
  direct `readStatus()` remains non-disruptive and returns `BUSY` while
  periodic/ART acquisition is active.
- Public headers now document thread/ISR safety, callback recursion limits,
  status-bit clearability, heater caveats, clock-stretch scope, and partial
  multi-step operation behavior.
- Core/IDF guard scripts now reject Arduino and ESP-IDF framework headers in core/public headers and `src/`.
- `begin()` now rejects missing timing/yield callbacks as `INVALID_CONFIG` before touching I2C.
- README and ESP-IDF port documentation now describe the implemented component/example flow, native IDF boundary, and shared CLI parity.
- Documentation is now split into active guides/status, curated HIL evidence
  under `docs/hil/`, rationale reports under `docs/rationale/`, historical
  snapshots under `docs/archive/`, and vendor/source material under
  `docs/reference/`.
- README and hardware-validation docs now point at the current ESP32-S3/COM17
  default serial HIL evidence while keeping hardware-only blockers explicit.
- Package export hygiene now keeps active docs and curated evidence while
  excluding archived reports, generated Doxygen output, and bulky vendor
  reference material.
- `library.json` now declares both Arduino and ESP-IDF framework support, while `idf_component.yml` pins the supported ESP-IDF floor to 5.4.
- The ESP-IDF example no longer depends on a checkout-directory-derived `SHT3x-main` component name.
- Arduino and ESP-IDF diagnostic CLIs now expose HIL-friendly aliases and
  `status_restore` output for stop/status/restore sub-status visibility.

### Fixed
- Arduino bringup CLI now injects `nowMs`, `nowUs`, and `cooperativeYield` into the driver config, preventing startup `Command delay timeout` from leaving the CLI in `UNINIT`.
- Arduino I2C scan now uses the same table-format `0x08..0x77` timeout-aware diagnostic scanner as the other maintained I2C examples.
- Public Doxygen comments now describe bounded synchronous behavior, reset/recover
  semantics, serial-number restrictions, alert-limit packing, and transport
  capability boundaries more accurately.
- PlatformIO firmware builds now embed generated version/git metadata so serial
  HIL evidence records the code revision and clean/dirty state.
- ESP-IDF example CMake now exposes the repository public include directory
  without hard-coding the checkout-derived component name.
- Repository URLs in README, changelog links, `library.json`, and
  `idf_component.yml` now match the current `SHT3x-main` remote.

### Removed
- Stale branch-planning and snapshot reports from the active documentation
  index; historical copies now live under `docs/archive/`.

## [1.5.0] - 2026-05-14

### Added
- Measurement status accessors (`measurementPending`, `measurementStatus`, `lastMeasurementStatus`) and snapshot status so adapters can distinguish pending, expected no-data, CRC/protocol faults, timeouts, and success.
- Public low-level SHT3x command helpers (`writeCommand`, `writeCommandWithData`, `readCommand`) so upper layers can exercise the protocol directly without bypassing the driver's tracked transport path.
- `SettingsSnapshot` now includes driver-level fields: `initialized`, `state`, `i2cAddress`, `i2cTimeoutMs`, `offlineThreshold`, `hasNowMsHook`, and `hasSample`.
- `hasSample()` and `driverState()` for cross-library diagnostics.
- `Status::is(Err)` method for type-safe error code comparison.
- `Status::operator bool()` explicit conversion for concise success checks.
- Native coverage proving latched `OFFLINE` blocks normal I2C operations without touching the bus while explicit recovery/reset paths remain available.

### Changed
- Doxyfile project metadata now matches `library.json` and references the
  maintained docs tree instead of removed template files.
- Reference documentation now uses human-readable vendor PDF names; after later
  cleanup, maintained extracts live under `docs/reference/extracted/`.
- Explicit recovery/reset bypass internals now use the shared `ScopedOfflineI2cAllowance` / `_reassertOfflineLatch()` procedure so failed recovery attempts that begin from `OFFLINE` keep the latch asserted.
- `readCommand()` now validates read buffers and rejects responses larger than the largest documented SHT3x frame before sending the command.
- `getRawSample()` and `getCompensatedSample()` now remain available after `getMeasurement()` consumes `measurementReady()`, with cache validity reported by `hasSample`.
- CLI help was refactored onto shared `CliStyle.h` `cli::printHelp*` helpers, and the CLI contract now checks that helper.
- Active periodic/ART repeatability and rate setters now update cached configuration only after the required restart succeeds.
- Health behavior is now standardized on latched `OFFLINE`: normal public I2C operations return `BUSY` with `Driver is offline; call recover()` and do not touch I2C until `recover()` succeeds.

### Changed
- Command-delay and reset/break timing guards now return visible `IN_PROGRESS`/`TIMEOUT` statuses instead of spinning/yielding internally.
- Package exports now exclude docs, tests, CI, tooling, and local build/editor directories by default.

### Fixed
- `begin()` and `probe()` now map only proven address NACK to `DEVICE_NOT_FOUND`, preserving timeout, bus, data-NACK, read-NACK, and generic I2C faults.
- `tick()` now records measurement readout failures and stops silently re-reading a failed single-shot CRC/protocol frame.

## [1.4.2] - 2026-04-05

### Added
- `CommandHandler.h` example helper for serial command parsing (`cmd::readLine`, `cmd::match`, `cmd::parseInt`).
- `HealthDiag.h` example helper with verbose health diagnostics, color-coded output, snapshots, diffs, and `HealthMonitor` class for continuous monitoring.
- README API coverage for lifecycle accessors (`isInitialized()`, `getConfig()`), maintenance helpers (`resetToDefaults()`, `resetAndRestore()`), settings snapshots, serial-number readout, alert-limit helpers, and timing helpers.
- Documentation for `Status::inProgress()` and the `Err::CONVERSION_NOT_READY` compatibility alias.

### Changed
- README now clarifies health semantics: expected not-ready fetches are non-failures, while pre-`begin()` validation/setup issues do not transition the driver state.
- Example documentation now reflects the current bringup CLI coverage instead of only the minimal measurement path.
- Unified build config and CI structure: simplified `platformio.ini` environments, aligned `.github/workflows/ci.yml`.
- `.gitignore` now uses the correct `Version.h` path instead of template placeholder.

### Fixed
- Doxyfile version, `SECURITY.md` email/version, and PlatformIO environment names now match the current release.

## [1.4.1] - 2026-04-03

### Changed
- README installation and documentation references now point to the current repository URL and shipped docs.
- Example transport documentation, bringup example, and native tests now share the same `i2cUser` / `TwoWire*` callback contract.

### Fixed
- `Version.h` generation now uses the same macro-based build metadata pattern as the other I2C libraries, avoiding self-referential dirty release headers.

## [1.4.0] - 2026-03-01

### Changed
- Updated `docs/IDF_PORT.md` and version metadata to match the current release baseline.

### Fixed
- Timing-guard compliance and timing-path stability improvements in `src/SHT3x.cpp`.

## [1.3.2] - 2026-02-28

### Added
- Shared bringup framework headers in `examples/common/*` (`BusDiag`, `CliShell`, `HealthView`, `TransportAdapter`).
- `docs/IDF_PORT.md` and driver-extraction notes, now archived at
  `docs/archive/notes/SHT3x_driver_extraction.md`, for standardized
  portability and command-map context.
- CLI/timing guard scripts for repository quality gates.

### Changed
- Unified `examples/01_basic_bringup_cli` command/help/reporting style with the cross-library scheme.
- Normalized repository text/line-ending policy and build metadata generation path (`.gitattributes`, version script updates).

## [1.3.1] - 2026-02-22

### Added
- `isPeriodicActive()` inline accessor for checking periodic mode state without I2C
- `notReadyCount()` accessor for diagnostic "not-ready" response counter during periodic fetch

### Fixed
- `generalCallReset()` now sets `_lastCommandValid` so subsequent commands respect tIDLE spacing
- `tick()` returns early when driver is OFFLINE, preventing I2C bus thrashing (recovery is manual via `recover()`)
- `_stopPeriodicInternal()` updates driver state immediately after BREAK is accepted, before the processing delay, preventing state inconsistency if `_waitMs()` fails
- `interfaceReset()` now records command timing after bus reset callback, ensuring next command respects tIDLE
- `begin()` sends best-effort BREAK + soft reset before probe to recover sensor stuck in periodic mode from a previous session (MCU reboot without sensor power cycle)
- `_crc8()` null-pointer and zero-length guard added for defensive safety

## [1.3.0] - 2026-02-02

### Added
- resetToDefaults() and resetAndRestore() APIs with RAM-only settings restore
- Cached settings tracking for mode/repeatability/periodic rate/heater/alert limits
- Manager-owned Wire policy and consolidated report
- Additional native tests covering restore behavior and adapter rules

### Changed
- Wire adapters no longer mutate global Wire timeout/clock in callbacks
- ART mode restarts when repeatability/periodic rate changes
- Periodic fetch scheduling uses an explicit margin to avoid early fetches

### Fixed
- Wire 0-byte reads treated as ambiguous errors with guardrails
- Combined write+read is rejected for SHT3x protocol reads (tIDLE enforced)
- NaN/Inf alert-limit inputs are rejected

## [1.2.0] - 2026-02-01

### Added
- Transport capability flags and Wire-safe NACK gating
- Unity-based native tests and GitHub Actions CI

### Changed
- recover() is comms-only (no mode/heater/alert restore)
- Periodic not-ready handling is gated by transport capabilities

### Fixed
- Read-header NACK never treated as not-ready unless transport supports it

## [1.1.0] - 2026-02-01

### Added
- I2C error taxonomy (NACK addr/data/read, timeout, bus error)
- Periodic not-ready timeout guard and missed sample estimate
- Sample timestamp and age helper
- Settings snapshot helper (getSettings/readSettings)
- Recovery ladder with backoff and optional hard reset callback
- Host-side unit tests for CRC8, conversions, alert packing, time wrap, NACK mapping, recovery

### Changed
- begin() now records pre-init bus activity and lastOk/lastError timestamps
- Expected NACK handling is explicit (read-header NACK only)
- Interface reset clears pending measurement state
- Transport callback contract documented with explicit error semantics

### Fixed
- Health tracking updated for general-call reset and bus activity
- Periodic fetch no longer masks non-NACK errors as not-ready

## [1.0.0] - 2026-02-01

### Added
- **First stable release**
- Complete SHT3x driver with CRC validation
- Injected I2C transport architecture (no Wire dependency in library)
- Health monitoring with automatic state tracking (READY/DEGRADED/OFFLINE)
- Single-shot, periodic, and ART measurement modes
- Periodic rates: 0.5/1/2/4/10 mps
- Heater enable/disable support
- Status register read/clear helpers
- Alert limit read/write with encode/decode helpers
- Electronic identification code (serial number) readout
- Soft reset and general call reset support
- Non-blocking tick-based measurement scheduling
- Measurement time estimation
- Basic CLI example (`01_basic_bringup_cli`)
- Comprehensive Doxygen documentation in public headers
- MIT License

[Unreleased]: https://github.com/janhavelka/SHT3x-main/compare/v1.7.1...HEAD
[1.7.1]: https://github.com/janhavelka/SHT3x-main/compare/v1.7.0...v1.7.1
[1.7.0]: https://github.com/janhavelka/SHT3x-main/compare/v1.6.1...v1.7.0
[1.6.1]: https://github.com/janhavelka/SHT3x-main/compare/v1.6.0...v1.6.1
[1.6.0]: https://github.com/janhavelka/SHT3x-main/compare/v1.5.0...v1.6.0
[1.5.0]: https://github.com/janhavelka/SHT3x-main/compare/v1.4.2...v1.5.0
[1.4.2]: https://github.com/janhavelka/SHT3x-main/compare/v1.4.1...v1.4.2
[1.4.1]: https://github.com/janhavelka/SHT3x-main/compare/v1.4.0...v1.4.1
[1.4.0]: https://github.com/janhavelka/SHT3x-main/compare/v1.3.2...v1.4.0
[1.3.2]: https://github.com/janhavelka/SHT3x-main/compare/v1.3.1...v1.3.2
[1.3.1]: https://github.com/janhavelka/SHT3x-main/compare/v1.3.0...v1.3.1
[1.3.0]: https://github.com/janhavelka/SHT3x-main/releases/tag/v1.3.0
[1.2.0]: https://github.com/janhavelka/SHT3x-main/releases/tag/v1.2.0
[1.1.0]: https://github.com/janhavelka/SHT3x-main/releases/tag/v1.1.0
[1.0.0]: https://github.com/janhavelka/SHT3x-main/releases/tag/v1.0.0
