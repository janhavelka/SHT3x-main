# SHT3x Robustness Follow-up Report (Wire-first, Comms-only Recovery)
Date: 2026-02-01
Version: 1.2.0

## Findings (from auditor follow-up)
- Wire cannot reliably distinguish read-header NACK vs timeout/bus error when requestFrom() returns 0 bytes.
- Expected-NACK handling must be capability-gated to avoid masking failures.
- Recovery must restore communications only, not prior runtime configuration.
- CI/test execution must be runnable via pio test -e native.

## Changes Applied

### 1) Transport capabilities + Wire-safe NACK gating
- Added TransportCapability bitmask and Config::transportCapabilities.
  - File: include/SHT3x/Config.h
- I2C_NACK_READ is treated as MEASUREMENT_NOT_READY only when READ_HEADER_NACK capability is set.
  - File: src/SHT3x.cpp (_i2cWriteReadTrackedAllowNoData)
- Wire example transport maps Wire error codes but sets capabilities to NONE (best-effort).
  - Files: examples/common/I2cTransport.h, examples/01_basic_bringup_cli/main.cpp
- README updated with transport contract and capability guidance.
  - File: README.md

### 2) Periodic mode: time-gated fetch, no unreliable NACK polling
- Fetch is scheduled by driver timing; no early polling required.
- If transport cannot report read-header NACK, 0-byte reads remain tracked errors.
  - File: src/SHT3x.cpp (tick, _fetchPeriodic)

### 3) Recovery ladder: comms-only
- recover() restores comms only and leaves device in SINGLE_SHOT idle.
- No re-application of heater/mode/alert limits.
- Ladder order: bus reset -> soft reset -> hard reset (optional) -> general call reset (opt-in).
  - File: src/SHT3x.cpp (recover)
- README and AUDIT updated to document comms-only semantics.
  - Files: README.md, docs/AUDIT.md

### 4) Tests + CI
- Native tests converted to Unity and runnable via pio test -e native.
  - File: test/test_basic.cpp
- GitHub Actions workflow added for ESP32 build + native tests.
  - File: .github/workflows/ci.yml
- README updated with test commands.
  - File: README.md

### 5) Library settings snapshot helpers + CLI reads
- Added SettingsSnapshot plus getSettings() / readSettings() for cached settings and optional status read.
  - Files: include/SHT3x/SHT3x.h, src/SHT3x.cpp
- CLI uses readSettings() for bare mode / repeat / rate / stretch commands to show actual driver state.
  - File: examples/01_basic_bringup_cli/main.cpp

### 6) Audit report refresh
- Updated AUDIT_REPORT.md to reflect capability gating, comms-only recovery, settings snapshot helper, and Unity/CI coverage.
  - File: AUDIT_REPORT.md

## Wire Limitations + Capability Gating
- With Wire, requestFrom() returning 0 is ambiguous; driver will not classify this as not-ready.
- Expected-NACK (MEASUREMENT_NOT_READY) occurs only when TransportCapability::READ_HEADER_NACK is set.
- For Wire adapters, keep transportCapabilities = TransportCapability::NONE.

## Recovery Semantics (Comms-only)
- On success, recover():
  - clears measurement state
  - disables periodic/ART internally
  - sets mode to SINGLE_SHOT
- Orchestrator must re-apply settings if needed.

## Test/CI Evidence
- ESP32 build: pio run -e ex_bringup_s3 (local run OK)
- Native tests: pio test -e native (CI-ready; local requires g++)

## Remaining Notes
- Wire timeout/bus error reporting remains best-effort; richer transports should set capability flags.
- If the orchestrator wants full restore after recovery, it should explicitly reconfigure after recover().
