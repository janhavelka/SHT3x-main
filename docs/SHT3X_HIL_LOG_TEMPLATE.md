# SHT3x HIL Log

Date:
Operator:
Branch:
Commit:
Worktree state:
Firmware/library version:
Framework:
Board:
Target:
Serial port:
Baud rate: `115200`
Sensor variant:
I2C address:
I2C bus speed:
Pull-up values:
Supply voltage:
Cable length:
ALERT wiring:
Reset wiring:
Reference sensor / fixture:
Ambient conditions:
Artifact directory:
Serial transcript path:
Logic analyzer path:
Fixture notes path:
Deviations:

Default verdict for every section is `Not run` until real command output and
operator evidence are pasted below.

## Build / Flash

Commands:

```text

```

Raw log:

```text

```

Verdict: Not run

## Smoke Test

Commands:

```text

```

Raw log:

```text

```

Verdict: Not run

## Status Restore Snapshot

Command: `status_restore`

Required fields:

- `result`
- `initialMode`
- `finalMode`
- `modeInterrupted`
- `stopStatus`
- `statusReadStatus`
- `restoreStatus`
- `statusValid`
- `restored`
- parsed `status:` line when `statusValid=1`

Raw log:

```text

```

Verdict: Not run

## Single-Shot Tests

Commands:

```text

```

Raw log:

```text

```

Verdict: Not run

## Periodic Tests

Commands:

```text

```

Raw log:

```text

```

Verdict: Not run

## ART Mode Tests

Commands:

```text

```

Raw log:

```text

```

Verdict: Not run

## ALERT Threshold Tests

Setup:

Commands:

```text

```

Raw log:

```text

```

Logic analyzer / GPIO evidence:
Restore commands:

```text

```

Restore raw log:

```text

```

Post-restore verdict: Not run
Verdict: Not run

## Status / Clear Status Tests

Commands:

```text

```

Raw log:

```text

```

Verdict: Not run

## Heater Tests

Commands:

```text

```

Raw log:

```text

```

Cooldown notes:
Restore commands:

```text

```

Restore raw log:

```text

```

Post-restore verdict: Not run
Verdict: Not run

## Reset / Recover Tests

Commands:

```text

```

Raw log:

```text

```

Restore commands:

```text

```

Restore raw log:

```text

```

Post-restore verdict: Not run
Verdict: Not run

## Fault / Unplug Tests

Commands:

```text

```

Raw log:

```text

```

Restore commands:

```text

```

Restore raw log:

```text

```

Post-restore verdict: Not run
Verdict: Not run

## Soak Test

Duration:
Commands:

```text

```

Raw log summary:

```text

```

Failures:
Restore commands:

```text

```

Restore raw log:

```text

```

Post-restore verdict: Not run
Verdict: Not run

## Final HIL Verdict

Verdict: Not run
Remaining caveats:
