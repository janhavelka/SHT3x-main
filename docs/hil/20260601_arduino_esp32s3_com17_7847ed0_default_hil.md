# SHT3x Default Serial HIL Evidence - ESP32-S3 COM17

Date/time UTC: 2026-06-01T18:30:17Z

Source run: `hil_logs/i2c_20260601T183017Z/summary.md`

Repository branch: `hardening/sht3x-release-readiness-gaps`

Repository commit recorded by runner: `7847ed0eb83fbeeb9f08c4f5ea14c8a8b24756c9`

Worktree state recorded by runner: `clean`

Firmware metadata reported by CLI:

```text
1.5.0 (7847ed0eb83f, Jun  1 2026 20:13:07, clean)
```

Target:

- Framework/example: Arduino PlatformIO `esp32s3dev`, `examples/01_basic_bringup_cli`
- Serial port: `COM17`
- Baud: `115200`
- Board field: `esp32s3`
- Target name: `esp32s3-com17-repeat`
- Expected SHT3x address: `0x44`
- I2C addresses seen: `0x3C`, `0x44`, `0x50`, `0x51`

Runner command:

```bash
python tools/run_sht3x_hil.py --port COM17 --baud 115200 --expect-address 0x44 --board esp32s3 --target-name esp32s3-com17-repeat --operator codex
```

Result:

- Final verdict: `PASS`
- Final driver state: `READY`
- Final `online`: `true`
- Final `consecutive_failures`: `0`
- Final `total_success`: `42`
- Final `total_failures`: `0`
- Serial/EIC read: `0x29075EB0`

Default command groups covered:

- version/help/scan/probe/settings/driver-health
- status and raw status read
- no-stretch single-shot low, medium, and high repeatability
- raw and fixed-point compensated cache reads
- no-stretch serial/EIC read
- heater status read with heater remaining off
- alert-limit read/show plus app-note encode/decode vectors
- periodic 0.5 mps high start/fetch/stop
- periodic 1 mps high status-restore/fetch/stop
- periodic 2 mps medium start/fetch/stop
- ART start/fetch/stop
- final driver health

Boundary:

This was the default automated serial HIL sequence only. It did not run
destructive/reset groups, clock-stretching, alert-limit writes, ALERT GPIO or
logic-analyzer checks, address `0x45`, ESP32-S2 hardware, fault injection,
humidity accuracy validation, production fixture validation, or long-soak
stability.

The generated `hil_logs/` directory is local scratch output and is ignored by
default. This file preserves the key evidence needed by the maintained docs
without committing the raw generated transcript.
