# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Nothing yet

### Changed
- Nothing yet

### Deprecated
- Nothing yet

### Removed
- Nothing yet

### Fixed
- Nothing yet

### Security
- Nothing yet

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

[Unreleased]: https://github.com/janhavelka/SHT3x/compare/v1.3.0...HEAD
[1.3.0]: https://github.com/janhavelka/SHT3x/releases/tag/v1.3.0
[1.2.0]: https://github.com/janhavelka/SHT3x/releases/tag/v1.2.0
[1.1.0]: https://github.com/janhavelka/SHT3x/releases/tag/v1.1.0
[1.0.0]: https://github.com/janhavelka/SHT3x/releases/tag/v1.0.0
