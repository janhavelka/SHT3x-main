# SHT3x Hardware Validation Matrix

Date: 2026-05-31
Branch: `hardening/sht3x-industry-readiness`

No hardware validation was run while preparing this document. Every hardware
scenario below remains `Not run` until logs, board details, and evidence are
captured from a real ESP32-S2/S3 plus SHT3x setup or an explicit fault jig.

## Evidence Required For Each Run

Record the board model, ESP32 target, sensor variant, address strap, pull-up
values, bus speed, cable length, supply voltage, firmware commit, CLI type
Arduino/IDF, date/time, ambient/reference temperature and RH, full serial log,
and ALERT GPIO or logic-analyzer capture where applicable.

The exact operator sequence, output patterns, pass/fail criteria, restore steps,
and log naming convention are defined in `docs/SHT3X_HIL_RUNBOOK.md`. This
matrix is a planning index; the runbook is the HIL execution contract.

Default example assumptions: SDA GPIO8, SCL GPIO9, 400 kHz I2C, SHT3x address
`0x44`. The stock examples are diagnostic bring-up CLIs, not production
application architectures.

## Bring-Up Entry Points

Arduino diagnostic CLI:

```text
pio run -e esp32s3dev -t upload
pio device monitor -b 115200
help
version
scan
begin
drv
selftest
```

ESP-IDF diagnostic CLI:

```text
idf.py -C examples/idf/basic set-target esp32s3
idf.py -C examples/idf/basic -p COMx flash monitor
help
version
scan
begin
drv
selftest
```

Repeat on ESP32-S2 with `pio run -e esp32s2dev -t upload` or
`idf.py -C examples/idf/basic set-target esp32s2`.

## Scenario Matrix

| Scenario | Command/procedure | Expected result | Status |
| --- | --- | --- | --- |
| I2C scan default address `0x44` | `scan`, `begin`, `probe`, `drv` | Device present at `0x44`; driver state `READY`; no unexpected failures | Not run |
| Optional address `0x45` | Strap ADDR high; rebuild or configure test firmware for `0x45`; run `scan`, `begin`, `probe` | Device present at `0x45`; `0x44` absent unless another device is present | Not run |
| Single-shot high repeatability | `mode single`, `stretch 0`, `repeat high`, `read`, `raw`, `comp`, `stats` | CRC OK, plausible T/RH, raw and compensated cache valid | Not run |
| Single-shot medium repeatability | `mode single`, `stretch 0`, `repeat med`, `read`, `raw`, `comp` | CRC OK, plausible T/RH | Not run |
| Single-shot low repeatability | `mode single`, `stretch 0`, `repeat low`, `read`, `raw`, `comp` | CRC OK, plausible T/RH | Not run |
| Single-shot clock stretching | `mode single`, `stretch 1`, `meastime`, `read`, `serial stretch` | No timeout with configured margin; serial CRC OK | Not run |
| Periodic 0.5 mps | `start_periodic 0.5 high`, wait, `read` several times, `stats`, `stop_periodic` | Fetch Data samples are fresh; no stale sample confusion; Break returns to idle | Not run |
| Periodic 1 mps | `start_periodic 1 high`, wait, `read` several times, `stats`, `stop_periodic` | CRC-valid samples; expected cadence and counters | Not run |
| Periodic 2 mps | `start_periodic 2 high`, repeated `read`, `stats`, `stop_periodic` | Timing remains stable; no self-heating concern beyond documented limits | Not run |
| Periodic 4 mps | `start_periodic 4 high`, repeated `read`, `stats`, `stop_periodic` | Timing and not-ready behavior understood; no unexpected failures | Not run |
| Periodic 10 mps | `start_periodic 10 high`, repeated `read`, `stats`, `stop_periodic` | High-rate behavior stable; self-heating and bus load noted | Not run |
| ART mode | `start_art`, wait, `read` several times, `stats`, `stop_periodic` | Samples arrive; mode stops cleanly; driver returns to idle/single-shot | Not run |
| Status decode while idle | `status`, `status_raw` | Parsed bits match raw register | Not run |
| Clear status | `status`, `clearstatus`, `status` | Clearable bits 15, 11, 10, and 4 clear; heater/command/checksum bits follow datasheet behavior | Not run |
| ALERT low/high RH threshold | Configure limits near ambient with `alert write`; start periodic; change RH with safe fixture; observe ALERT pin; `stop_periodic`, `status` | ALERT pin asserts/deasserts with hysteresis; `alert` and `rh_alert` bits identify cause | Not run |
| ALERT temperature threshold | Configure temperature limits near a safe controlled setpoint; start periodic; change temperature slowly; observe ALERT; `stop_periodic`, `status` | ALERT pin and `t_alert` bit identify temperature cause | Not run |
| ALERT cause while periodic active | Start periodic and induce alert; run `status_restore` while active; then `periodic fetch` | `readStatusWithModeRestore()` logs stop/read/restore sub-statuses, status bits, `statusValid`, and `restored`; fetch after restore succeeds | Not run |
| Alert-limit read/write round trip | Idle only: `alert write hs 30 55`, `alert read hs`, repeat `hc`, `lc`, `ls`; also `alert raw read <kind>` | Quantized raw/physical values match expected tolerance | Not run |
| Disable alerts | Idle: `alert disable`, read `hs` and `ls`, start periodic at ambient, observe ALERT/status | Normal ambient does not assert ALERT | Not run |
| Heater enable/disable | Idle: `heater off`, `heater status`, `read`; `heater on`, wait 30-120 s, `heater status`, repeated `read`; `heater off`, cool down | Heater status bit follows commands; temperature rises/RH shifts; cooldown recorded | Not run |
| Soft reset | Idle: `status`, `reset`, `status`, `read`, `drv` | Reset command succeeds; reset behavior and usability recorded | Not run |
| Defaults/reset restore | Set repeat/rate/heater/alerts; run `restore`, `drv`, `alert read hs`, `heater status` | Cached settings restore where intended; any partial restore failure is explicit | Not run |
| nRESET pin | Application-owned hardware pulse, then `probe`, `status`, `read` | Sensor resets and driver/application policy is documented | Not run |
| Interface reset | `iface_reset`, `probe`, `read` | If callback is configured, bus recovers; otherwise expected unsupported/error is logged | Not run |
| General-call reset | Isolated single-device bus only; enable policy in application; run `greset`, then `probe`, `status` | All supporting bus devices intentionally reset; not used as a production default | Not run |
| Unplug/replug | Disconnect sensor, call `read` until `DEGRADED`/`OFFLINE`, reconnect, `recover`, `drv` | No hangs; manual recovery returns `READY` when bus/device are restored | Not run |
| Address NACK | Configure wrong address in test firmware or remove sensor; `begin`, `probe`, `read` | Precise error/status is logged; no stale success | Not run |
| Command error fault | Idle: `clearstatus`, `command write 0x0000`, `status` | Sensor command-error bit behavior recorded if command is rejected | Not run |
| Write CRC fault injection | Use low-level I2C injector/interposer or temporary fault transport; send bad alert-limit payload CRC | Sensor reports write checksum error or driver maps expected failure | Not run |
| Data/read CRC fault injection | Use emulator, bus fault injector, or hardware harness with corrupt response CRC | Driver returns `CRC_MISMATCH`; no stale sample success | Not run |
| Timeout/NACK fault | Safe jig: hold/remove bus/device or force timeout; `read`, `state`, `recover` | Specific status codes logged; health transitions and recovery behavior match contract | Not run |
| Long soak | Stable fixture: `stress 1000`, `stress_mix 500`, `drv`; optionally periodic overnight with external logging | No failures, leaks, state drift, or unexplained missed samples under stable conditions | Not run |

## ALERT Procedure Notes

Use thresholds close to current ambient values to avoid unsafe environmental
extremes. Configure limits while idle, then start periodic mode:

```text
clearstatus
read
alert write hs 25 45
alert write hc 24 40
alert write ls 0 0
alert write lc 1 5
alert read hs
alert read hc
start_periodic 1 high
```

Then change RH or temperature with a controlled fixture, observe the ALERT pin,
and capture status:

```text
stop_periodic
status
status_raw
clearstatus
status
alert disable
stop_periodic
```

The diagnostic CLIs expose `status_restore`, which calls
`readStatusWithModeRestore()` and prints `stopStatus`, `statusReadStatus`,
`restoreStatus`, `statusValid`, and `restored`. Use that command for ALERT
diagnosis while periodic/ART acquisition is active, then run `periodic fetch` or
`art fetch` to prove acquisition resumed.

## Fault-Injection Notes

The stock CLIs cannot inject bad read CRC bytes because they sit above the
transport adapter, and `command write_data` computes the write CRC internally.
CRC and low-level NACK/timeout validation requires a sensor emulator,
interposer, fault-injection jig, or temporary hardware-test transport.
