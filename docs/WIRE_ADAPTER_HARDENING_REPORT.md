# Wire Adapter Hardening Report

## Summary
Hardened the Wire transport adapters to honor per-call timeouts, drain partial reads, and make STOP behavior explicit for SHT3x command/read timing. README Quick Start updated to match the contract, and native tests added to prevent regressions.

## Timeout Policy (Option A)
- The adapter calls `Wire.setTimeOut(timeoutMs)` in every `i2cWrite` / `i2cWriteRead`.
- This assumes the adapter is single-threaded or externally serialized (documented in README).

## Changes Applied
- `examples/common/I2cTransport.h`:
  - Set `Wire.setTimeOut(timeoutMs)` per call.
  - Use `Wire.endTransmission(true)` explicitly (STOP).
  - Drain partial reads before returning error.
  - Clarified mapping comment for ESP32 Arduino core error codes.
- `README.md`:
  - Quick Start now sets `Wire.setClock(...)` and `Wire.setTimeOut(...)`.
  - Quick Start adapters honor `timeoutMs`, use STOP, and drain partial reads.
  - Transport contract notes Wire’s ambiguity and single-threaded timeout changes.
- `test/stubs/Wire.h`:
  - Added timeout tracking, STOP flag capture, and read-call counting.
- `test/test_basic.cpp`:
  - Added tests for timeout propagation, STOP usage, and partial read draining.

## Tests Added
- `test_wire_adapter_timeout_and_stop`
- `test_wire_adapter_drains_partial_read`

## Evidence
- `pio run -e ex_bringup_s3` (success)
- `pio test -e native` requires a host compiler (`gcc`/`g++`).
