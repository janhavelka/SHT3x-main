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

[1.1.0] - 2026-02-01

### Added
- I2C error taxonomy (NACK addr/data/read, timeout, bus error)
- Periodic not-ready timeout guard and missed sample estimate
- Sample timestamp and age helper
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

[Unreleased]: https://github.com/janhavelka/SHT3x/compare/v1.1.0...HEAD
[1.1.0]: https://github.com/janhavelka/SHT3x/releases/tag/v1.1.0
[1.0.0]: https://github.com/janhavelka/SHT3x/releases/tag/v1.0.0
