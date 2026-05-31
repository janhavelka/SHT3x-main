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
matrix is a planning index; the runbook is the HIL execution contract. Each row
below names its setup/procedure, expected pass/fail boundary, evidence artifact
path, and current status.
The host-side serial evidence runner is `tools/run_i2c_hil.py`; its
auditor-facing default command contract is documented in
`docs/SHT3X_I2C_HIL_RUNBOOK.md`.

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

| Scenario | Setup and command/procedure | Expected pass/fail criteria | Log/evidence path | Status |
| --- | --- | --- | --- | --- |
| I2C scan default address `0x44` | Default ADDR strap; `scan`, `begin`, `probe`, `drv` | Pass: device at `0x44`, driver `READY`, no unexpected failures. Fail: no ACK, wrong state, hidden recovery. | `docs/hil/..._address_0x44.log` | Not run |
| Optional address `0x45` | Strap ADDR high; rebuild/configure address `0x45`; `scan`, `begin`, `probe` | Pass: device at `0x45`; `0x44` absent unless another device is present. Fail: ambiguous scan. | `docs/hil/..._address_0x45.log` | Not run |
| Single-shot high repeatability | Stable supply/ambient; `clear_status`, `single high`, `raw`, `comp`, `drv` | Pass: CRC-valid plausible T/RH and valid raw/comp cache. Fail: timeout, CRC error, stale success. | `docs/hil/..._single_high.log` | Not run |
| Single-shot medium repeatability | Stable supply/ambient; `clear_status`, `single medium`, `raw`, `comp`, `drv` | Pass: CRC-valid plausible T/RH. Fail: timeout, CRC error, state degradation. | `docs/hil/..._single_medium.log` | Not run |
| Single-shot low repeatability | Stable supply/ambient; `clear_status`, `single low`, `raw`, `comp`, `drv` | Pass: CRC-valid plausible T/RH. Fail: timeout, CRC error, state degradation. | `docs/hil/..._single_low.log` | Not run |
| Single-shot clock stretching | Transport timeout >= worst-case tMEAS; `mode single`, `stretch 1`, `meastime`, `read`, `serial stretch`, `stretch 0` | Pass: completes or records explicit unsupported/timeout behavior. Fail: ambiguous timeout or stretch left enabled. | `docs/hil/..._clock_stretch.log` | Not run |
| Periodic 0.5 mps | Stable setup; `periodic start 0.5 high`, wait, repeated `periodic fetch`, `stats`, `status_restore`, `periodic stop` | Pass: fresh CRC-valid samples and clean Break/restore. Fail: stale sample confusion, restore failure, non-READY final state. | `docs/hil/..._periodic_0_5.log` | Not run |
| Periodic 1 mps | Stable setup; `periodic start 1 high`, repeated `periodic fetch`, `stats`, `status_restore`, `periodic stop` | Pass: expected cadence/counters and clean restore. Fail: unexplained missed samples or non-READY final state. | `docs/hil/..._periodic_1.log` | Not run |
| Periodic 2 mps | Stable setup; `periodic start 2 high`, repeated `periodic fetch`, `stats`, `periodic stop` | Pass: stable timing and CRC-valid samples. Fail: unexplained failures or self-heating not recorded. | `docs/hil/..._periodic_2.log` | Not run |
| Periodic 4 mps | Stable setup; `periodic start 4 high`, repeated `periodic fetch`, `stats`, `periodic stop` | Pass: timing/not-ready behavior understood. Fail: unexpected failure or hidden recovery. | `docs/hil/..._periodic_4.log` | Not run |
| Periodic 10 mps | Stable setup; `periodic start 10 high`, repeated `periodic fetch`, `stats`, `periodic stop` | Pass: high-rate behavior stable with bus load/self-heating noted. Fail: unexplained failures. | `docs/hil/..._periodic_10.log` | Not run |
| ART mode | Stable setup; `art start`, repeated `art fetch`, `stats`, `status_restore`, `art stop` | Pass: ART samples arrive, restore evidence is complete, final state `READY`. Fail: no sample or restore failure. | `docs/hil/..._art.log` | Not run |
| Status decode while idle | Idle sensor; `status`, `status_raw` | Pass: parsed bits match raw register. Fail: mismatch or missing parsed output. | `docs/hil/..._status_idle.log` | Not run |
| Clear status | Idle sensor; `status`, `clear_status`, `status` | Pass: clearable bits 15, 11, 10, and 4 clear; non-clearable behavior recorded. Fail: hidden/ambiguous status changes. | `docs/hil/..._clear_status.log` | Not run |
| ALERT low/high RH threshold | ALERT wired to GPIO/logic analyzer; `alert set ...`, `periodic start 1 high`, fixture RH change, `status_restore` | Pass: ALERT transition plus `rh_alert` cause and restore evidence. Fail: no GPIO/logic evidence or wrong cause. | `docs/hil/..._alert_rh.log`, `_logic.csv`, `_fixture.md` | Not run |
| ALERT temperature threshold | ALERT wired; controlled temperature stimulus; `alert set ...`, `periodic start 1 high`, `status_restore` | Pass: ALERT transition plus `t_alert` cause. Fail: unsafe/ambiguous stimulus or wrong cause. | `docs/hil/..._alert_temperature.log`, `_logic.csv`, `_fixture.md` | Not run |
| ALERT cause while periodic active | Periodic active and alert induced; `status_restore`, then `periodic fetch` | Pass: logs `result`, `initialMode`, `finalMode`, `modeInterrupted`, `stopStatus`, `statusReadStatus`, `restoreStatus`, `statusValid`, `restored`, parsed status, and resumed fetch. Fail: missing fields or failed restore. | `docs/hil/..._alert_status_restore.log` | Not run |
| Alert-limit read/write round trip | Idle only; `alert set hs 30 55`, `alert read hs`, repeat `hc`, `lc`, `ls`; `alert raw read <kind>` | Pass: quantized raw/physical values match tolerance. Fail: partial write hidden or mismatch. | `docs/hil/..._alert_limits.log` | Not run |
| Disable alerts | Idle; `alert disable`, `alert show`, start periodic at ambient, observe ALERT/status | Pass: normal ambient does not assert ALERT or datasheet latch behavior is recorded. Fail: thresholds left unsafe. | `docs/hil/..._alert_disable.log` | Not run |
| Heater enable/disable | Controlled ambient; `heater off`, `heater status`, `single high`, `heater on`, wait, repeated `single high`, `heater off` | Pass: heater bit follows commands and cooldown recorded. Fail: heater left on or status mismatch. | `docs/hil/..._heater.log`, `_fixture.md` | Not run |
| Soft reset | Idle sensor; `status`, `reset`, `status`, `single high`, `drv` | Pass: reset command succeeds and usability is restored. Fail: hidden partial state or non-READY final state. | `docs/hil/..._soft_reset.log` | Not run |
| Defaults/reset restore | Set repeat/rate/heater/alerts; `restore`, `settings`, `drv`, `alert read hs`, `heater status` | Pass: intended cached settings restore or partial failure is explicit. Fail: hidden partial restore. | `docs/hil/..._restore.log` | Not run |
| nRESET pin | Application-owned hardware pulse; `probe`, `status`, `single high` | Pass: reset behavior and policy documented. Fail: undocumented wiring or no recovery evidence. | `docs/hil/..._nreset.log`, `_logic.csv` | Not run |
| Interface reset | Callback configured; `iface_reset`, `probe`, `single high`, `drv` | Pass: bus recovers or unsupported/error is explicit. Fail: hidden bus manipulation. | `docs/hil/..._iface_reset.log`, `_logic.csv` | Not run |
| General-call reset | Isolated single-device bus only; application opt-in; `greset`, `probe`, `status` | Pass: every bus-wide reset effect is intentional and documented. Fail: shared-bus use or unsupported path hidden. | `docs/hil/..._general_call_reset.log` | Not run |
| Unplug/replug | Safe manual sequence; disconnect sensor, `single high` until `DEGRADED`/`OFFLINE`, reconnect, `recover`, `drv` | Pass: no hangs and manual recovery returns `READY`. Fail: stale success, hidden retry loop, or no recovery. | `docs/hil/..._unplug_replug.log` | Not run |
| Address NACK | Wrong address firmware or removed sensor; `begin`, `probe`, `single high` | Pass: precise status logged, no stale success. Fail: ambiguous error. | `docs/hil/..._address_nack.log` | Not run |
| Command error fault | Idle; `clear_status`, `command write 0x0000`, `status` | Pass: command-error bit behavior recorded if rejected. Fail: status hidden or destructive sequence not restored. | `docs/hil/..._command_error.log` | Not run |
| Write CRC fault injection | Injector/interposer/test transport sends bad alert-limit payload CRC | Pass: sensor reports write checksum error or expected failure. Fail: invalid claim without injector evidence. | `docs/hil/..._write_crc_fault.log`, `_fixture.md` | Not run |
| Data/read CRC fault injection | Emulator/interposer/harness corrupts response CRC | Pass: driver returns `CRC_MISMATCH`, no stale sample success. Fail: corruption not proven. | `docs/hil/..._read_crc_fault.log`, `_fixture.md` | Not run |
| Timeout/NACK fault | Safe jig holds/removes bus/device; `single high`, `state`, `recover` | Pass: specific status codes and health transitions match contract. Fail: hang or hidden recovery. | `docs/hil/..._timeout_nack_fault.log`, `_fixture.md` | Not run |
| Long soak | Stable fixture; `stress 1000`, `stress_mix 500`, `drv`; optional periodic overnight logging | Pass: no unexplained failures, leaks, state drift, or missed samples. Fail: unexplained failure or missing final restore. | `docs/hil/..._soak.log`, `_fixture.md` | Not run |

## ALERT Procedure Notes

Use thresholds close to current ambient values to avoid unsafe environmental
extremes. Configure limits while idle, then start periodic mode:

```text
clear_status
single high
alert set hs 25 45
alert set hc 24 40
alert set ls 0 0
alert set lc 1 5
alert read hs
alert read hc
periodic start 1 high
```

Then change RH or temperature with a controlled fixture, observe the ALERT pin,
and capture status:

```text
periodic stop
status
status_raw
clear_status
status
alert disable
periodic stop
```

The diagnostic CLIs expose `status_restore`, which calls
`readStatusWithModeRestore()` and prints `stopStatus`, `statusReadStatus`,
`restoreStatus`, `result`, `initialMode`, `finalMode`, `modeInterrupted`,
`statusValid`, and `restored`. Use that command for ALERT diagnosis while
periodic/ART acquisition is active, then run `periodic fetch` or `art fetch` to
prove acquisition resumed.

## Fault-Injection Notes

The stock CLIs cannot inject bad read CRC bytes because they sit above the
transport adapter, and `command write_data` computes the write CRC internally.
CRC and low-level NACK/timeout validation requires a sensor emulator,
interposer, fault-injection jig, or temporary hardware-test transport.
