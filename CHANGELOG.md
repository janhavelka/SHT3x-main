# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

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
- `library.json` now declares both Arduino and ESP-IDF framework support, while `idf_component.yml` pins the supported ESP-IDF floor to 5.4.
- The ESP-IDF example no longer depends on a checkout-directory-derived `SHT3x-main` component name.

### Fixed
- Arduino bringup CLI now injects `nowMs`, `nowUs`, and `cooperativeYield` into the driver config, preventing startup `Command delay timeout` from leaving the CLI in `UNINIT`.
- Arduino I2C scan now uses the same table-format `0x08..0x77` timeout-aware diagnostic scanner as the other maintained I2C examples.

## [1.5.0] - 2026-05-14

### Added
- Public low-level SHT3x command helpers (`writeCommand`, `writeCommandWithData`, `readCommand`) so upper layers can exercise the protocol directly without bypassing the driver's tracked transport path.
- `SettingsSnapshot` now includes driver-level fields: `initialized`, `state`, `i2cAddress`, `i2cTimeoutMs`, `offlineThreshold`, `hasNowMsHook`, and `hasSample`.
- `hasSample()` and `driverState()` for cross-library diagnostics.
- `Status::is(Err)` method for type-safe error code comparison.
- `Status::operator bool()` explicit conversion for concise success checks.
- Native coverage proving latched `OFFLINE` blocks normal I2C operations without touching the bus while explicit recovery/reset paths remain available.

### Changed
- Doxyfile project metadata now matches `library.json` and references the
  maintained docs tree instead of removed template files.
- Reference documentation now uses human-readable vendor PDF names and separates compact SHT3x notes from full PDF/application-note extractions under `docs/extracted-md/` and `docs/pdf-extracted-md/`.
- Explicit recovery/reset bypass internals now use the shared `ScopedOfflineI2cAllowance` / `_reassertOfflineLatch()` procedure so failed recovery attempts that begin from `OFFLINE` keep the latch asserted.
- `readCommand()` now validates read buffers and rejects responses larger than the largest documented SHT3x frame before sending the command.
- `getRawSample()` and `getCompensatedSample()` now remain available after `getMeasurement()` consumes `measurementReady()`, with cache validity reported by `hasSample`.
- CLI help was refactored onto shared `CliStyle.h` `cli::printHelp*` helpers, and the CLI contract now checks that helper.
- Active periodic/ART repeatability and rate setters now update cached configuration only after the required restart succeeds.
- Health behavior is now standardized on latched `OFFLINE`: normal public I2C operations return `BUSY` with `Driver is offline; call recover()` and do not touch I2C until `recover()` succeeds.

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
- `docs/IDF_PORT.md` and `docs/SHT3x_driver_extraction.md` for standardized portability and driver-extraction notes.
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

[Unreleased]: https://github.com/janhavelka/SHT3x/compare/v1.5.0...HEAD
[1.5.0]: https://github.com/janhavelka/SHT3x/compare/v1.4.2...v1.5.0
[1.4.2]: https://github.com/janhavelka/SHT3x/compare/v1.4.1...v1.4.2
[1.4.1]: https://github.com/janhavelka/SHT3x/compare/v1.4.0...v1.4.1
[1.4.0]: https://github.com/janhavelka/SHT3x/compare/v1.3.2...v1.4.0
[1.3.2]: https://github.com/janhavelka/SHT3x/compare/v1.3.1...v1.3.2
[1.3.1]: https://github.com/janhavelka/SHT3x/compare/v1.3.0...v1.3.1
[1.3.0]: https://github.com/janhavelka/SHT3x/releases/tag/v1.3.0
[1.2.0]: https://github.com/janhavelka/SHT3x/releases/tag/v1.2.0
[1.1.0]: https://github.com/janhavelka/SHT3x/releases/tag/v1.1.0
[1.0.0]: https://github.com/janhavelka/SHT3x/releases/tag/v1.0.0
