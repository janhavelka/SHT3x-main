# Wire 0-Byte Read Semantics Report

## Problem
The README correctly states that a 0-byte `Wire.requestFrom()` result is **ambiguous** on ESP32 and must not be assumed to be a read-header NACK. However, the Quick Start transport example still returned `Err::I2C_NACK_READ` when `received == 0`, which is misleading and can cause wrong orchestration policy.

## Final Semantics (Rationale)
- Wire adapters **must not** emit `Err::I2C_NACK_READ` unless they can **prove** a read-header NACK and declare `TransportCapability::READ_HEADER_NACK`.
- For Wire, `requestFrom()` returning 0 bytes is treated as an **ambiguous I2C error** and mapped to `Err::I2C_ERROR`.
- The driver adds a guardrail: if a transport returns `Err::I2C_NACK_READ` without declaring `READ_HEADER_NACK`, the driver remaps it to `Err::I2C_ERROR`.

This prevents misconfigured adapters from accidentally enabling “not-ready” semantics and keeps orchestration logic conservative under Wire’s limited error reporting.

## Changes Applied
- `README.md`: Quick Start and transport contract updated so 0-byte reads map to `Err::I2C_ERROR` and `Err::I2C_NACK_READ` is only valid with `READ_HEADER_NACK` capability.
- `examples/common/I2cTransport.h`: Wire adapter returns `Err::I2C_ERROR` on 0-byte reads (no fake NACK_READ).
- `src/SHT3x.cpp`: `_i2cWriteReadRaw()` remaps `Err::I2C_NACK_READ` to `Err::I2C_ERROR` when `READ_HEADER_NACK` is not declared.
- `test/stubs/Wire.h`: Added requestFrom override to simulate 0-byte reads in native tests.
- `test/test_basic.cpp`: Added tests for the new semantics and example adapter parity.

## Tests Added / Updated
- `test/test_basic.cpp`:
  - `test_nack_mapping_without_capability` now expects `Err::I2C_ERROR`.
  - `test_periodic_fetch_expected_nack_no_failure` validates NOT_READY behavior only when capability is set.
  - `test_example_adapter_ambiguous_zero_bytes` ensures the example Wire adapter returns `Err::I2C_ERROR` on 0-byte reads and rejects combined write+read.

## Evidence
- Build: `pio run -e ex_bringup_s3` (success)
- Native tests require a host toolchain (`gcc`/`g++`) to run: `pio test -e native`
