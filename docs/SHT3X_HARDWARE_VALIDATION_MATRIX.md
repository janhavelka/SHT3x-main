# SHT3x Hardware Validation Scenario Matrix

Date: 2026-05-31
Branch: `hardening/sht3x-industry-readiness`

This file is a scenario index, not a status tracker. Current evidence status is
tracked in `docs/HARDWARE_VALIDATION.md`.

Manual operator procedures live in `docs/SHT3X_HIL_RUNBOOK.md`. The host-side
serial runner is `tools/run_sht3x_hil.py` / `tools/run_i2c_hil.py`, and its
default command contract is documented in `docs/SHT3X_I2C_HIL_RUNBOOK.md`.

Every real run must record board model, ESP32 target, sensor variant, I2C
address, pull-ups, bus speed, cable length, supply voltage, firmware commit,
CLI type, date/time, serial transcript, and fixture evidence. ALERT, reset, and
fault scenarios also need GPIO, logic-analyzer, scope, or jig evidence as
appropriate.

Default example assumptions: SDA GPIO8, SCL GPIO9, 400 kHz I2C, SHT3x address
`0x44`. The stock examples are diagnostic bring-up CLIs, not production
application architectures.

## Automatic Runner Mapping

The default runner is safe and non-destructive. It can generate evidence for:

- smoke/version/help/scan/probe/settings/health,
- status and status-restore parsing,
- no-stretch single-shot low/medium/high,
- raw and compensated cache reads,
- no-stretch serial/EIC,
- heater status read with heater remaining off,
- alert-limit read plus alert encode/decode vectors,
- selected periodic 0.5/1/2 mps paths,
- ART start/fetch/stop when supported by the target firmware,
- final driver health.

Additional runner flags map to scenario groups:

| Flag | Scenario coverage | Evidence boundary |
| --- | --- | --- |
| `--include-destructive` | selftest, recover, clear status, soft reset, restore, interface reset | Alters device/status state; not part of default smoke. |
| `--include-bus-wide-reset` | general-call reset | Requires isolated bus; can reset other supporting devices. |
| `--include-soak --soak-count N` | bounded stress and mixed-operation stress | Only proves the configured duration/count, not long-soak stability by default. |
| `--include-clock-stretch` | stretch-enabled `read` and `serial stretch` | Unsupported/timeout behavior is recorded, not hidden. |
| `--include-alert-write` | software-visible alert write/readback and cleanup | Does not prove physical ALERT pin transitions. |
| `--include-all-periodic-rates` | additional 4 and 10 mps periodic fetches | Still requires final health review and self-heating notes for production claims. |
| `--include-output-tests` | ALERT output operator/GPIO procedure | PASS requires GPIO or logic-analyzer evidence. |
| `--include-fault-tests` | fault/unplug/CRC-injection procedure | PASS requires safe jig/interposer/emulator or documented manual fault evidence. |

## Scenarios

| Scenario | Setup and command/procedure | Expected pass/fail criteria | Evidence path |
| --- | --- | --- | --- |
| I2C scan default address `0x44` | Default ADDR strap; `scan`, `begin`, `probe`, `drv` | Pass: device at `0x44`, driver `READY`, no unexpected failures. Fail: no ACK, wrong state, hidden recovery. | `docs/hil/..._address_0x44.log` |
| Optional address `0x45` | Strap ADDR high; rebuild/configure address `0x45`; `scan`, `begin`, `probe` | Pass: device at `0x45`; `0x44` absent unless another device is present. Fail: ambiguous scan. | `docs/hil/..._address_0x45.log` |
| Single-shot high repeatability | Stable supply/ambient; `clear_status`, `single high`, `raw`, `comp`, `drv` | Pass: CRC-valid plausible T/RH and valid raw/comp cache. Fail: timeout, CRC error, stale success. | `docs/hil/..._single_high.log` |
| Single-shot medium repeatability | Stable supply/ambient; `clear_status`, `single medium`, `raw`, `comp`, `drv` | Pass: CRC-valid plausible T/RH. Fail: timeout, CRC error, state degradation. | `docs/hil/..._single_medium.log` |
| Single-shot low repeatability | Stable supply/ambient; `clear_status`, `single low`, `raw`, `comp`, `drv` | Pass: CRC-valid plausible T/RH. Fail: timeout, CRC error, state degradation. | `docs/hil/..._single_low.log` |
| Single-shot clock stretching | Transport timeout >= worst-case tMEAS; `mode single`, `stretch 1`, `meastime`, `read`, `serial stretch`, `stretch 0` | Pass: completes or records explicit unsupported/timeout behavior. Fail: ambiguous timeout or stretch left enabled. | `docs/hil/..._clock_stretch.log` |
| Periodic 0.5 mps | Stable setup; `periodic start 0.5 high`, wait, repeated `periodic fetch`, `drv`, `status_restore`, `periodic stop` | Pass: fresh CRC-valid samples and clean Break/restore. Fail: stale sample confusion, restore failure, non-READY final state. | `docs/hil/..._periodic_0_5.log` |
| Periodic 1 mps | Stable setup; `periodic start 1 high`, repeated `periodic fetch`, `drv`, `status_restore`, `periodic stop` | Pass: expected cadence/counters and clean restore. Fail: unexplained missed samples or non-READY final state. | `docs/hil/..._periodic_1.log` |
| Periodic 2 mps | Stable setup; `periodic start 2 high`, repeated `periodic fetch`, `drv`, `periodic stop` | Pass: stable timing and CRC-valid samples. Fail: unexplained failures or self-heating not recorded. | `docs/hil/..._periodic_2.log` |
| Periodic 4 mps | Stable setup; `periodic start 4 high`, repeated `periodic fetch`, `drv`, `periodic stop` | Pass: timing/not-ready behavior understood. Fail: unexpected failure or hidden recovery. | `docs/hil/..._periodic_4.log` |
| Periodic 10 mps | Stable setup; `periodic start 10 high`, repeated `periodic fetch`, `drv`, `periodic stop` | Pass: high-rate behavior stable with bus load/self-heating noted. Fail: unexplained failures. | `docs/hil/..._periodic_10.log` |
| ART mode | Stable setup; `art start`, repeated `art fetch`, `drv`, `status_restore`, `art stop` | Pass: ART samples arrive, restore evidence is complete, final state `READY`. Fail: no sample or restore failure. | `docs/hil/..._art.log` |
| Status decode while idle | Idle sensor; `status`, `status_raw` | Pass: parsed bits match raw register. Fail: mismatch or missing parsed output. | `docs/hil/..._status_idle.log` |
| Clear status | Idle sensor; `status`, `clear_status`, `status` | Pass: clearable bits 15, 11, 10, and 4 clear; non-clearable behavior recorded. Fail: hidden/ambiguous status changes. | `docs/hil/..._clear_status.log` |
| ALERT low/high RH threshold | ALERT wired to GPIO/logic analyzer; `alert set ...`, `periodic start 1 high`, fixture RH change, `status_restore` | Pass: ALERT transition plus `rh_alert` cause and restore evidence. Fail: no GPIO/logic evidence or wrong cause. | `docs/hil/..._alert_rh.log`, `_logic.csv`, `_fixture.md` |
| ALERT temperature threshold | ALERT wired; controlled temperature stimulus; `alert set ...`, `periodic start 1 high`, `status_restore` | Pass: ALERT transition plus `t_alert` cause. Fail: unsafe/ambiguous stimulus or wrong cause. | `docs/hil/..._alert_temperature.log`, `_logic.csv`, `_fixture.md` |
| ALERT cause while periodic active | Periodic active and alert induced; `status_restore`, then `periodic fetch` | Pass: logs `result`, `initialMode`, `finalMode`, `modeInterrupted`, `stopStatus`, `statusReadStatus`, `restoreStatus`, `statusValid`, `restored`, parsed status, and resumed fetch. Fail: missing fields or failed restore. | `docs/hil/..._alert_status_restore.log` |
| Alert-limit read/write round trip | Idle only; `alert set hs 30 55`, `alert read hs`, repeat `hc`, `lc`, `ls`; `alert raw read <kind>` | Pass: quantized raw/physical values match tolerance. Fail: partial write hidden or mismatch. | `docs/hil/..._alert_limits.log` |
| Disable alerts | Idle; `alert disable`, `alert show`, start periodic at ambient, observe ALERT/status | Pass: normal ambient does not assert ALERT or datasheet latch behavior is recorded. Fail: thresholds left unsafe. | `docs/hil/..._alert_disable.log` |
| Heater enable/disable | Controlled ambient; `heater off`, `heater status`, `single high`, `heater on`, wait, repeated `single high`, `heater off` | Pass: heater bit follows commands and cooldown recorded. Fail: heater left on or status mismatch. | `docs/hil/..._heater.log`, `_fixture.md` |
| Soft reset | Idle sensor; `status`, `reset`, `status`, `single high`, `drv` | Pass: reset command succeeds and usability is restored. Fail: hidden partial state or non-READY final state. | `docs/hil/..._soft_reset.log` |
| Defaults/reset restore | Set repeat/rate/heater/alerts; `restore`, `settings`, `drv`, `alert read hs`, `heater status` | Pass: intended cached settings restore or partial failure is explicit. Fail: hidden partial restore. | `docs/hil/..._restore.log` |
| nRESET pin | Application-owned hardware pulse; `probe`, `status`, `single high` | Pass: reset behavior and policy documented. Fail: undocumented wiring or no recovery evidence. | `docs/hil/..._nreset.log`, `_logic.csv` |
| Interface reset | Callback configured; `iface_reset`, `probe`, `single high`, `drv` | Pass: bus recovers or unsupported/error is explicit. Fail: hidden bus manipulation. | `docs/hil/..._iface_reset.log`, `_logic.csv` |
| General-call reset | Isolated single-device bus only; application opt-in; `greset`, `probe`, `status` | Pass: every bus-wide reset effect is intentional and documented. Fail: shared-bus use or unsupported path hidden. | `docs/hil/..._general_call_reset.log` |
| Unplug/replug | Safe manual sequence; disconnect sensor, `single high` until `DEGRADED`/`OFFLINE`, reconnect, `recover`, `drv` | Pass: no hangs and manual recovery returns `READY`. Fail: stale success, hidden retry loop, or no recovery. | `docs/hil/..._unplug_replug.log` |
| Address NACK | Wrong address firmware or removed sensor; `begin`, `probe`, `single high` | Pass: precise status logged, no stale success. Fail: ambiguous error. | `docs/hil/..._address_nack.log` |
| Command error fault | Idle; `clear_status`, `command write 0x0000`, `status` | Pass: command-error bit behavior recorded if rejected. Fail: status hidden or destructive sequence not restored. | `docs/hil/..._command_error.log` |
| Write CRC fault injection | Injector/interposer/test transport sends bad alert-limit payload CRC | Pass: sensor reports write checksum error or expected failure. Fail: invalid claim without injector evidence. | `docs/hil/..._write_crc_fault.log`, `_fixture.md` |
| Data/read CRC fault injection | Emulator/interposer/harness corrupts response CRC | Pass: driver returns `CRC_MISMATCH`, no stale sample success. Fail: corruption not proven. | `docs/hil/..._read_crc_fault.log`, `_fixture.md` |
| Timeout/NACK fault | Safe jig holds/removes bus/device; `single high`, `state`, `recover` | Pass: specific status codes and health transitions match contract. Fail: hang or hidden recovery. | `docs/hil/..._timeout_nack_fault.log`, `_fixture.md` |
| Long soak | Stable fixture; `stress 1000`, `stress_mix 500`, `drv`; optional periodic overnight logging | Pass: no unexplained failures, leaks, state drift, or missed samples. Fail: unexplained failure or missing final restore. | `docs/hil/..._soak.log`, `_fixture.md` |
