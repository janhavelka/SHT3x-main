# SHT3x ALERT/Status Fix Report

## Datasheet Conclusion

Local Sensirion documentation does not explicitly allow status-register reads,
status clear, or alert-limit commands while periodic or ART acquisition remains
active. The datasheet documents Fetch Data (`0xE000`) as the only explicit
periodic-mode exception and recommends stopping periodic acquisition before
sending another command.

Checked references:
- `docs/pdf-extracted-md/SHT3x_datasheet.md`, section 4.6, Table 11: Fetch Data `0xE000`.
- `docs/pdf-extracted-md/SHT3x_datasheet.md`, section 4.7, Table 12: ART `0x2B32` follows periodic start/readout/stop flow.
- `docs/pdf-extracted-md/SHT3x_datasheet.md`, section 4.8, Table 13: Break `0x3093` stops periodic mode.
- `docs/pdf-extracted-md/SHT3x_datasheet.md`, section 4.11, Table 17: status read `0xF32D`, 16-bit status word plus CRC.
- `docs/pdf-extracted-md/SHT3x_datasheet.md`, section 4.11, Table 18: status bits 15, 13, 11, 10, 4, 1, and 0.
- `docs/pdf-extracted-md/SHT3x_datasheet.md`, section 4.11, Table 19: clear status `0x3041`, clearing bits 15, 11, 10, and 4.
- `docs/pdf-extracted-md/SHT3x_HT_AN_AlertMode.md`: ALERT mode is active during periodic acquisition and status bits indicate alert cause.

## Contract Decision

Direct `readStatus()` remains non-disruptive and returns `BUSY` during active
periodic/ART mode. It does not send undocumented status commands while periodic
acquisition is active.

ALERT diagnosis during periodic/ART mode is now provided by the explicit helper:

```cpp
Status readStatusWithModeRestore(StatusReadSnapshot& out);
```

When periodic/ART is active, this helper sends Break, reads status, then attempts
to restore the prior acquisition mode. This is intentionally documented as
disruptive: it aborts the current conversion cadence and restarts periodic/ART
timing from the restore point.

## Code Changes

- Added `StatusReadSnapshot` with stop/read/restore statuses, parsed status,
  mode metadata, and partial-state flags.
- Added `SettingsSnapshot::statusReadStatus` so `readSettings()` no longer hides
  the reason a status read was unavailable.
- Added `SHT3x::readStatusWithModeRestore()`.
- Factored status-bit parsing through `_parseStatusRegister()`.
- Kept `clearStatus()` command-only and destructive; it still returns `BUSY` in
  active periodic/ART mode.
- Kept alert-limit read/write APIs returning `BUSY` in active periodic/ART mode.
- Updated public Doxygen and README with ALERT/status semantics, helper side
  effects, destructive clear semantics, and thread/ISR safety.

## Tests Added

Native tests cover:
- direct `readStatus()` returns `BUSY` in active periodic mode without I2C;
- `readStatusWithModeRestore()` success in periodic mode with ALERT/RH/T bits;
- `readStatusWithModeRestore()` success in ART mode;
- `readSettings()` status success and `statusReadStatus` reason reporting;
- status transport failure preservation;
- clear-status command-only behavior, failure propagation, and busy/no-I2C mode;
- stop/read/restore helper failures at Break, status-command write, status read,
  CRC mismatch, and restore.

## Validation Results

- `python tools/check_core_timing_guard.py`: passed.
- `python tools/check_cli_contract.py`: passed.
- `python tools/check_idf_example_contract.py`: passed.
- `python scripts/generate_version.py check`: `include/SHT3x/Version.h` up to date.
- `python -m platformio test -e native`: passed, 58/58 tests.
- `python -m platformio run -e esp32s3dev`: success.
- `python -m platformio run -e esp32s2dev`: success.
- `idf.py --version`: unavailable in this shell (`idf.py` not recognized), so
  native ESP-IDF builds were not run and are not claimed as validation.

## Remaining Issues

- Hardware ALERT validation is still required.
- Pure ESP-IDF CI/build proof is still missing because `idf.py` is unavailable
  locally and no ESP-IDF CI job exists yet.
- Alert-limit updates during active periodic/ART are still intentionally blocked;
  callers must configure limits before starting acquisition or explicitly stop
  and restart around limit changes.
