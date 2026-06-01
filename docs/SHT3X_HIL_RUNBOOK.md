# SHT3x HIL Runbook

Date: 2026-05-31
Branch: `hardening/sht3x-industry-readiness`
Scope: pre-HIL operator procedure only. No result in this document is a HIL pass.

## Rule For Recording Results

Every item starts as `Not run`. Mark `Pass` or `Fail` only after recording the
exact board, target, sensor, commit, command log, and fixture evidence. Mark
`Blocked` when required hardware or a fault jig is missing. A casual desk
reading is a smoke test, not humidity accuracy validation.

Store evidence under:

```text
docs/hil/YYYYMMDD_<framework>_<target>_<board>_<commit>_<scenario>.log
docs/hil/YYYYMMDD_<framework>_<target>_<board>_<commit>_<scenario>_logic.csv
docs/hil/YYYYMMDD_<framework>_<target>_<board>_<commit>_<scenario>_fixture.md
```

Use the template in `docs/SHT3X_HIL_LOG_TEMPLATE.md` for each board/target run.

## Automatic Serial Runner

Use the automatic runner for repeatable serial evidence after the diagnostic
firmware is flashed:

```text
python tools/run_sht3x_hil.py --dry-run --expect-address 0x44 --board esp32s3 --target-name desk
python tools/run_sht3x_hil.py --port COMx --baud 115200 --expect-address 0x44 --board esp32s3 --target-name desk
```

The runner writes `serial_transcript.txt`, `summary.md`, `summary.json`,
`operator_checklist.md`, and `environment.txt` under
`hil_logs/i2c_<UTC_TIMESTAMP>/`. It parses version, I2C addresses, T/RH, raw
samples, status bits, serial/EIC, health counters, mode, repeatability,
periodic rate, and clock-stretch state where the CLI prints them.

The default automatic sequence is safe and non-destructive. It covers smoke,
single-shot low/medium/high, selected periodic rates, ART, status,
status_restore, serial/EIC, heater status, and alert read/encode/decode checks.
It does not enable heater, clear status, reset, write alert limits, run soak, or
require GPIO/fault fixtures.

Opt-in flags:

```text
--include-destructive
--include-bus-wide-reset
--include-soak --soak-count 100
--include-clock-stretch
--include-alert-write
--include-all-periodic-rates
--include-output-tests
--include-fault-tests
```

Do not mark ALERT pin validation as pass unless GPIO or logic-analyzer evidence
is attached. Do not mark humidity accuracy as pass without a reference fixture.
Do not mark fault-injection as pass without a safe jig/interposer/emulator or
documented manual fault evidence. Do not mark long-soak stability as pass unless
the configured soak duration was actually run.

## Required Hardware

Minimum smoke setup:

- ESP32-S2 board.
- ESP32-S3 board.
- SHT30, SHT31, or SHT35 sensor board.
- Stable 3.3 V supply and common ground.
- I2C pull-ups on SDA/SCL. Record values; 2.2 kOhm to 10 kOhm is typical.
- Short wires first; record cable length and bus speed.

Optional full-HIL setup:

- Logic analyzer on SDA/SCL and optional ALERT.
- Temperature/RH reference sensor.
- Controlled humidity/temperature fixture or repeatable local stimulus.
- ALERT pin pull-up if the sensor board does not already provide one.
- Safe fault jig for unplug, NACK, timeout, and CRC-injection tests.

Default example pins are SDA GPIO8 and SCL GPIO9 at 400 kHz with address `0x44`.
Record any board-specific changes in the log.

## Build And Flash

Arduino PlatformIO, ESP32-S3:

```text
python -m platformio run -e esp32s3dev
python -m platformio run -e esp32s3dev -t upload --upload-port COMx
python -m platformio device monitor -e esp32s3dev --port COMx
```

Arduino PlatformIO, ESP32-S2:

```text
python -m platformio run -e esp32s2dev
python -m platformio run -e esp32s2dev -t upload --upload-port COMx
python -m platformio device monitor -e esp32s2dev --port COMx
```

Arduino serial settings come from `platformio.ini`: 115200 baud, RTS disabled,
DTR disabled. If opening another terminal, use 115200 8N1 and send newline.

Native ESP-IDF, ESP32-S3:

```text
idf.py -C examples/idf/basic set-target esp32s3
idf.py -C examples/idf/basic build
idf.py -C examples/idf/basic -p COMx flash monitor
```

Native ESP-IDF, ESP32-S2:

```text
idf.py -C examples/idf/basic set-target esp32s2
idf.py -C examples/idf/basic build
idf.py -C examples/idf/basic -p COMx flash monitor
```

If `idf.py` is unavailable, record the exact shell error and leave pure-IDF local
HIL build/run as `Blocked` or `Not run`; do not infer it from PlatformIO builds.

## Expected Output Patterns

Arduino diagnostic CLI success evidence may include:

```text
Status: OK (code=0, detail=0)
Health: state=READY online=true
State: READY
Temp: <number> C, Humidity: <number> %
Raw: T=0x.... RH=0x....
status: raw=0x....
status_restore:
result: OK code=0
initialMode=<mode> finalMode=<mode> modeInterrupted=<0|1> statusValid=1 restored=1
stopStatus: OK code=0
statusReadStatus: OK code=0
restoreStatus: OK code=0
```

ESP-IDF diagnostic CLI success evidence may include:

```text
begin: OK code=0 detail=0
state=READY initialized=1 online=1
temperature=<number> C humidity=<number> %RH
raw=0x....
status: raw=0x....
status_restore:
result: OK code=0
initialMode=<mode> finalMode=<mode> modeInterrupted=<0|1> statusValid=1 restored=1
stopStatus: OK code=0
statusReadStatus: OK code=0
restoreStatus: OK code=0
```

Any unexpected `ERR`, nonzero `code=`, `CRC_MISMATCH`, `TIMEOUT`, `I2C_*`,
missing sample, missing ALERT transition, or undocumented manual intervention is
a fail unless the test explicitly defines it as expected.

## Common Restore Step

Run this after every disruptive scenario:

```text
periodic stop
heater off
alert disable
clear_status
mode single
stretch 0
repeat high
drv
```

Pass the restore step only if the final state is `READY`, `online` is true, and
there are no new unexplained consecutive failures. If the fault test leaves the
sensor disconnected, reconnect first, run `recover`, then run the restore step.

## Minimal Smoke Test

Run on every framework/target combination before the full matrix:

```text
help
version
scan
begin
probe
settings
drv
single high
raw
comp
status
status_restore
heater status
alert show
selftest
```

Pass criteria:

- `version` prints library version, full build string, build timestamp, and git commit/status.
- `scan` sees the configured SHT3x address.
- `begin`, `probe`, `settings`, and `drv` show initialized `READY`/online state.
- `single high` produces one sample after the driver measurement time.
- `raw` and `comp` show cached data from that sample.
- `status` prints raw and parsed status bits.
- `status_restore` prints `result`, `initialMode`, `finalMode`,
  `modeInterrupted`, `stopStatus`, `statusReadStatus`, `restoreStatus`,
  `statusValid`, and `restored`.
- `selftest` reports zero failures.

Fail criteria: any unexpected error, no SHT3x address, no sample, missing parsed
status bits, missing `status_restore` fields, or final driver state not `READY`.

## Single-Shot Tests

Commands:

```text
clear_status
single high
raw
comp
single medium
raw
comp
single low
raw
comp
drv
```

Pass criteria:

- Each `single` command requests a no-stretch single-shot measurement.
- Each sample is CRC-valid and yields plausible desk values: RH from 0 to 100
  percent and temperature within the sensor operating range.
- `raw` and `comp` remain available after the measurement is consumed.
- `drv` ends in `READY`.

Fail criteria: timeout, CRC failure, stale cache after a failed request, implausible
unexplained value, or health degradation not explained by a deliberate fault.

## Clock-Stretching And No-Stretch Tests

Preferred ESP32 path is no-stretch single-shot, because the driver schedules the
sensor conversion and reads after the calculated measurement time.

No-stretch commands:

```text
mode single
stretch 0
repeat high
meastime
read
raw
comp
serial nostretch
```

Clock-stretch commands:

```text
mode single
stretch 1
repeat high
meastime
read
serial stretch
stretch 0
```

Commands that intentionally use clock stretching are single-shot measurements
with `stretch 1` and `serial stretch`. No-stretch commands are `single high`,
`single medium`, `single low`, `read` with `stretch 0`, periodic fetch, ART
fetch, and `serial nostretch`.

Pass criteria:

- No-stretch path completes within the logged measurement time plus driver margin.
- Stretch path either completes successfully or records a clear transport timeout
  on hardware known not to support clock stretching.
- HIL distinguishes sensor conversion delay from I2C failure by logging
  `meastime`, timeout value, command path, and final `drv` health.

Fail criteria: ambiguous timeout, missing `meastime`, hidden recovery, or failure
to restore `stretch 0` before the next scenario.

## Periodic Tests

For each rate, use high repeatability unless a separate row says otherwise.

| Rate | First wait | Wait between fetches |
| --- | --- | --- |
| 0.5 | 7 s | 3 s |
| 1 | 4 s | 2 s |
| 2 | 3 s | 1 s |
| 4 | 2 s | 1 s |
| 10 | 1 s | 0.5 s |

Commands for each rate:

```text
clear_status
periodic start <rate> high
periodic fetch
raw
comp
periodic fetch
stats
status_restore
periodic fetch
periodic stop
drv
```

Pass criteria:

- `periodic start` succeeds.
- Fetch samples are CRC-valid and plausible.
- `status_restore` while periodic is active reports `statusValid=1` and
  `restored=1`; all three sub-status fields are logged.
- The fetch after `status_restore` succeeds, proving acquisition resumed.
- `periodic stop` returns the driver to `READY` single-shot state.

Fail criteria: missing fetch data, unexplained missed samples, `restored=0`,
failed `restoreStatus`, or final non-READY state.

## ART Mode Tests

Commands:

```text
clear_status
art start
art fetch
raw
comp
status_restore
art fetch
art stop
drv
```

Pass criteria:

- ART start succeeds and produces fetch data.
- `status_restore` logs stop/read/restore sub-statuses and restores ART.
- Fetch after restore succeeds.
- `art stop` returns to `READY`.

Fail criteria: no sample, missing restore evidence, `restored=0`, or final
health degradation.

## ALERT Threshold Tests

Safe setup:

1. Wire ALERT to an MCU GPIO input or logic analyzer channel.
2. Confirm ALERT has a pull-up to the sensor I/O voltage.
3. Keep the sensor and reference in the same airflow and away from operator
   breath, body heat, lighting heat, and HVAC drafts.
4. Record the idle ALERT level before programming limits.

Commands and procedure:

```text
periodic stop
heater off
clear_status
single high
status
alert show
```

Record current temperature and RH. For a deliberate high-RH trigger, choose
thresholds from the current reading:

```text
alert set hs <T_now_plus_20C> <RH_now_minus_2pct>
alert set hc <T_now_plus_19C> <RH_now_minus_5pct>
alert set lc <T_now_minus_19C> 5
alert set ls <T_now_minus_20C> 1
alert show
periodic start 1 high
```

If the calculated RH values are outside 0 to 100 percent, use a controlled
fixture to move RH first or mark the test `Blocked`.

Then:

```text
status_restore
periodic fetch
clear_status
status
periodic stop
alert disable
clear_status
alert show
drv
```

Pass criteria:

- ALERT pin asserts when thresholds are crossed.
- `status_restore` prints `result`, initial/final mode, `modeInterrupted`,
  `stopStatus`, `statusReadStatus`, `restoreStatus`, `statusValid=1`, and
  `restored=1`.
- Parsed status identifies the expected alert cause (`rh_alert` or `t_alert`).
- `periodic fetch` after `status_restore` succeeds.
- `clear_status` clears clearable status bits.
- `alert disable` or restored safe thresholds deassert ALERT, or the exact
  datasheet latch behavior is recorded.

Fail criteria: no ALERT transition, missing GPIO/logic evidence, wrong parsed
cause, restore failure not recorded as failure, or thresholds not restored.

## Status And Clear Status Tests

Commands:

```text
clear_status
status
status_raw
status_restore
clear_status
status
drv
```

Pass criteria: raw and parsed status agree; clearable bits are cleared by
`clear_status`; non-clearable heater/command/checksum behavior follows the
datasheet and is recorded.

Fail criteria: parsed bits do not match raw status, clearable bits remain without
explanation, or command/checksum flags are hidden.

## Heater Tests

Commands:

```text
heater off
heater status
single high
heater on
heater status
status
```

Wait 30 to 120 seconds, then:

```text
single high
single high
heater off
heater status
status
```

Wait for cooldown and record notes.

Pass criteria: heater status follows commands, status bit reflects heater state,
temperature/RH movement is recorded, and heater is off at the end.

Fail criteria: heater cannot be disabled, status bit disagrees with command, or
the sensor is left heated for following tests.

## Reset And Recover Tests

Soft reset:

```text
reset
status
single high
drv
```

Cached restore:

```text
repeat low
rate 2
heater off
alert disable
restore
settings
drv
```

Interface reset, only when the callback is configured:

```text
iface_reset
probe
single high
drv
```

General-call reset, isolated single-device bus only:

```text
greset
probe
status
drv
```

Pass criteria: reset command results are explicit, driver returns to `READY`, and
any partial restore error is visible in command status and health output.

Fail criteria: hidden partial restore, bus-wide reset on a shared bus, or no
manual recovery path after a fault.

## Fault And Unplug Tests

Use only a safe jig or deliberate manual unplug sequence.

Commands:

```text
single high
drv
```

Disconnect sensor or force the selected fault, then:

```text
single high
single high
drv
```

Reconnect or clear the fault, then:

```text
recover
drv
single high
```

Pass criteria: no unbounded wait, specific `Status` code is logged, health
transitions to `DEGRADED` or `OFFLINE` according to failure count, and manual
`recover` returns to `READY` after the hardware fault is removed.

Fail criteria: hang, stale success after failed I2C, hidden retry loop, or
automatic recovery that the application did not request.

CRC fault injection requires an emulator, bus interposer, or temporary fault
transport. The stock CLIs compute valid write CRCs and cannot corrupt read CRC
bytes by themselves.

## Soak Test

Commands:

```text
clear_status
mode single
stretch 0
repeat high
stress 1000
stress_mix 500
drv
```

Optional periodic soak:

```text
periodic start 1 high
```

Log periodic fetches externally for the chosen duration, then:

```text
periodic stop
drv
```

Pass criteria: zero unexplained failures, no state drift, no hidden OFFLINE
latch, and all missed samples are explained by the chosen cadence.

Fail criteria: any unexplained failure, memory/state drift, recovery without
operator command, or missing final restore.

## Fixture Realism Notes

Desk RH/T readings only prove that the bus and protocol path are alive. Real
humidity validation requires a reference sensor or controlled setup. Relative
humidity is strongly temperature-dependent, so thermal mismatch between DUT and
reference appears as humidity error. Let the sensor settle after handling,
avoid local airflow and heat sources, avoid operator breath near the sensor, and
record self-heating when using periodic high-rate modes or the heater. Housings
and fixtures slow response; document coupling method and settling time before
using readings as production evidence.
