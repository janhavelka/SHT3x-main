# Reset/Recover With Optional Settings Restore Report

## New APIs
- `Status resetToDefaults();`
  - Resets the sensor using the recovery ladder and clears cached settings to defaults.
  - Leaves the driver in SINGLE_SHOT idle with no pending measurement.
- `Status resetAndRestore();`
  - Resets the sensor using the same ladder, then reapplies cached settings from RAM.
  - Restores heater, alert limits, and operational mode (PERIODIC/ART as cached).

## Cached Settings (RAM only)
Cached fields:
- `mode` (SINGLE_SHOT / PERIODIC / ART)
- `repeatability`
- `periodicRate`
- `clockStretching`
- `heaterEnabled`
- `alertRaw[4]` + `alertValid[4]`

Cache updates only on successful apply operations (e.g., `setHeater`, `writeAlertLimitRaw`, `startPeriodic`).

## Reset/Restore Ordering
Apply ordering after reset:
1) `setRepeatability`
2) `setClockStretching`
3) `setPeriodicRate`
4) `setHeater`
5) `writeAlertLimitRaw` for each valid cached limit
6) restore mode (`startPeriodic` / `startArt` / SINGLE_SHOT)

This respects tIDLE and keeps the device idle until configuration is complete.

## Manager-Owned Wire Policy
- Driver callbacks receive `timeoutMs` as a requested bound.
- In a managed bus, the I2CManager owns Wire clock/timeout and callbacks must not mutate them.
- Example adapter now ignores per-call timeouts and uses STOP for command writes.

## Example Usage
```cpp
// Apply settings once
sensor.setRepeatability(SHT3x::Repeatability::MEDIUM_REPEATABILITY);
sensor.setHeater(true);
sensor.writeAlertLimitRaw(SHT3x::AlertLimitKind::HIGH_SET, 0x2222);
sensor.startPeriodic(SHT3x::PeriodicRate::MPS_2, SHT3x::Repeatability::MEDIUM_REPEATABILITY);

// On fault: either reset to defaults or restore
sensor.resetToDefaults();   // app must reconfigure
// or
sensor.resetAndRestore();   // restores cached settings
```

## Changes Applied (files)
- `include/SHT3x/SHT3x.h`: added `CachedSettings`, `resetToDefaults`, `resetAndRestore`, cache accessors.
- `src/SHT3x.cpp`: cache maintenance, reset/restore logic, recovery ladder refactor.
- `README.md`, `docs/AUDIT.md`: updated recovery/reset semantics and manager-owned Wire policy.
- `examples/common/I2cTransport.h`: callbacks no longer mutate Wire clock/timeout.
- `test/stubs/Wire.h`, `test/test_basic.cpp`: new tests for cache and reset/restore, manager-owned Wire behavior.

## Tests Added
- `test_cache_updates_only_on_success`
- `test_reset_to_defaults_clears_cache`
- `test_reset_and_restore_applies_cached_settings`
- Updated adapter tests for manager-owned Wire policy

## Evidence
- `pio run -e ex_bringup_s3` (run locally)
- `pio test -e native` requires a host compiler (`gcc`/`g++`).
