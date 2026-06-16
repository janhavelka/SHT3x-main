# SHT3x I2C HIL Target Template

Copy this template for each board/sensor fixture used for HIL evidence.

## Target Identity

- Operator:
- Date/time:
- Branch:
- Commit hash:
- Worktree state:
- MCU board:
- Build environment:
- Framework:
- Serial port:
- Baud rate: `115200`
- Firmware version:
- Device/module:
- Chip marking:
- I2C address:
- Supply voltage:
- I2C bus speed:
- Pull-ups:
- SDA pin:
- SCL pin:
- Reset wiring:
- ALERT/interrupt wiring:
- Fixture details:

## Exact Commands

Build:

```bash

```

Upload:

```bash

```

Monitor, optional manual inspection:

```bash

```

HIL runner:

```bash
python tools/run_sht3x_hil.py --port COMx --baud 115200 --expect-address 0x44 --board esp32s3 --target-name <name> --out hil_logs
```

## Evidence Checklist

- [ ] `serial_transcript.txt` attached.
- [ ] `summary.md` attached.
- [ ] `summary.json` attached.
- [ ] `environment.txt` attached.
- [ ] Operator checklist reviewed.
- [ ] Board photo attached.
- [ ] Sensor/module photo attached.
- [ ] Wiring photo attached.
- [ ] Power rail reading recorded.
- [ ] I2C address strap recorded.
- [ ] Any deviations from the default command sequence recorded.

## Logic Analyzer Checklist

- [ ] Capture file attached, if ALERT/reset/fault behavior is claimed.
- [ ] SDA/SCL voltage levels match target supply.
- [ ] Address observed as `0x44` or `0x45`.
- [ ] ALERT pin channel named and threshold procedure described, if used.
- [ ] Reset or fault-injection channel named, if used.

## Operator Review

- Final runner verdict:
- Manual checks marked `OPERATOR_REVIEW_REQUIRED`:
- Unsafe/fault tests run:
- Unsafe/fault tests not run:
- Remaining blockers:
- Auditor-facing verdict:
