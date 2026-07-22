# SHT3x v1.7.0 HIL Validation Report - COM19 - 2026-07-22

## Result

Selected automated COM19 coverage passed on an ESP32-S3 with an SHT3x at
`0x44`. The final exact-build functional matrix completed 99 commands with
`PASS`; the application-provided interface-reset callback was the sole
`SKIP_UNSUPPORTED`. A separate uninterrupted firmware-side one-hour soak
completed with a strict `PASS` verdict.

The run validates the selected command/status/data paths and sustained I2C
operation. It does not prove calibrated accuracy, physical ALERT-pin behavior,
fault/hotplug recovery, address `0x45`, ESP32-S2 hardware, or safe bus-wide
reset behavior.

## Tested Identity And Fixture

| Field | Value |
| --- | --- |
| Library version | `1.7.0` |
| Core release | annotated tag `v1.7.0` at `5409793f9f6e69f4dcd3b106621653e2a31caf4e` |
| Exact diagnostic commit | `524001cad59510aca21003e3c6a738224d640507` |
| Firmware identity | `1.7.0 (524001cad595, Jul 22 2026 16:26:41, clean)` |
| Target | ESP32-S3, Arduino PlatformIO `esp32s3dev` |
| Port | `COM19`, 115200 baud |
| SHT3x address | `0x44` |
| SHT3x serial/EIC | `0x29075EB0` |
| Other ACK addresses | `0x3C`, `0x41`, `0x50`, `0x51` |

The exact diagnostic commit changes only example/HIL/test/documentation files
relative to the `v1.7.0` tag; `include/`, `src/`, and release metadata are
unchanged. Firmware version, 12-character commit, and clean status were checked
before optional commands ran.

## Final Functional Matrix

Artifact directory:
`hil_logs/COM19-v1.7.0-functional-final/i2c_20260722T152923Z`

- 99 selected commands passed; zero selected commands failed.
- `iface_reset` was recorded as `SKIP_UNSUPPORTED` because the example adapter
  has no SCL-toggle callback.
- Single-shot low/medium/high, no-stretch and clock-stretch measurements passed.
- Periodic fetch passed at 0.5, 1, 2, 4, and 10 mps; ART start/fetch/stop passed.
- CRC-protected status, measurement, and serial/EIC paths passed.
- Status restore, clear, soft reset, reset-and-restore, self-test, and manual
  recovery passed.
- Alert encode/decode and exact write/readback passed for `0xCD33`, `0xC92D`,
  `0x3869`, and `0x3466`; cleanup verified `HIGH_SET=0x0000` and
  `LOW_SET=0xFFFF`.
- Heater enable/status/disable passed and final heater status was off.
- `stress 500`, `stress_mix 250`, and the 1000-sample benchmark passed.
- Final driver state was `READY`, online, with 1,737 logical successes, zero
  failures, and deterministic single/high/no-stretch settings.

The recorded runner verdict is `INCOMPLETE`, not `PASS`, because the unsupported
interface-reset row remains visible. All executable selected rows passed.

## Final One-Hour Soak

Artifact directory:
`hil_logs/COM19-v1.7.0-1h-final/i2c_20260722T142757Z`

| Metric | Result |
| --- | ---: |
| Firmware elapsed time | `3,600,003 ms` |
| Host observed duration | `3,600.485 s` |
| Successful measurements | `514,286` |
| Failed measurements | `0` |
| Logical success/failure delta | `514,286 / 0` |
| Transport success/failure delta | `1,028,572 / 0` |
| Protocol failure delta | `0` |
| Expected-not-ready delta | `0` |
| Temperature extrema | `26.51..27.03 C` |
| Relative-humidity extrema | `33.35..34.35 %RH` |
| Owner/readout markers | `owner_api=pollJob`, `milli=1` |
| Final health | `READY`, online, consecutive failures `0` |
| Final settings | single-shot, high repeatability, clock stretching disabled |

The one-hour runner verdict is `PASS`. Every measurement used the nonzero-ID,
absolute-deadline, one-callback `pollJob()` owner path and consumed the sample
through `getMeasurementMilli()`.

An earlier complete hour on commit `9195c5c` also executed 514,286 successful
measurements and 1,028,572 successful transfers with final health `READY` and
zero logical failures. Its verdict correctly failed because the single summary
line exceeded the example's fixed output buffer and lost the trailing
diagnostics. Commit `524001c` split the evidence into bounded records, added a
regression, passed a physical three-second proof, and then passed the final
complete hour above. The failed artifact is retained under
`hil_logs/COM19-v1.7.0-1h/i2c_20260722T132250Z` as defect evidence, not as the
acceptance run.

## Software And CI Checks

- Native suite: 116/116 passed.
- Arduino PlatformIO builds: ESP32-S3 and ESP32-S2 passed.
- Repository guards, parser tests, generated-version check, strict Doxygen, and
  package validation passed.
- GitHub Actions run `29928607190` passed all six jobs: native tests,
  validate-library, Arduino ESP32-S2/S3, and native ESP-IDF ESP32-S2/S3.

## TunnelMonitor-node Use

The physical fixture is TunnelMonitor hardware, but the production
TunnelMonitor firmware does not yet consume this library. The read-only
integration review therefore establishes compatibility, not an end-to-end
consumer implementation result:

- `I2cTask` remains the sole bus, queue, deadline, retry, health, and recovery
  owner; private SHT3x modules call one externally bounded transfer per poll.
- Use `bind()` plus staged `requestEnsureIdle()` on initial use and after owner
  recovery, `HealthPolicy::OBSERVE_ONLY`, exact nonzero request IDs, low 32 bits
  of the owner deadline, `pollJob(..., 1, ...)`, and immediate typed terminal
  result consumption.
- Cancellation and invalidation stay bus-silent. A job with physical effect is
  polled to its terminal result rather than abandoned.
- Only discovery-probe NACK means absence. A later NACK is a transfer failure;
  do not claim `READ_HEADER_NACK` capability unless the backend proves it.
- Use `getMeasurementMilli()`. It rounds to nearest, while the current direct
  TunnelMonitor codec truncates and can differ by one milli-unit.
- Library `READY` is local driver state, not proof of presence or application
  health.

No TunnelMonitor file, branch, commit, or current worktree state was changed by
this validation.

## Artifact Hashes

SHA-256:

- Final one-hour `summary.json`:
  `7ba58e6898c3c01348043bb143bfef052822365cd6366b492f72c9b0339224ab`
- Final one-hour `serial_transcript.txt`:
  `5c03cf1172f2016df2e19979fdf9def626262fdf11f56ed2f447164454f252db`
- Final one-hour `progress.jsonl`:
  `07082a08243142e4584c7370902eca697d3a9208a9565ea2108d334d9a9f260a`
- Final functional `summary.json`:
  `d619b68cf92c52b9a55d798a4222e00d8adc42dc8c1c5dc759710dcfd3766702`
- Final functional `serial_transcript.txt`:
  `4ab2852499d8a270fb88b9304c4b07109436b72bbe0e21f3512f7a94e6de105d`

Generated `hil_logs/` remain intentionally ignored; this curated report and
the hashes above are the maintained repository evidence.

## Intentionally Not Run Or Not Proven

- General-call reset: not run because the shared bus has other devices.
- Interface-reset SCL sequence: callback unsupported by this example fixture.
- Physical ALERT pin: no GPIO or logic-analyzer capture.
- Forced CRC/NACK/timeout, stuck-bus, unplug/replug, or replacement fault
  injection: no authorized safe interposer was attached.
- SHT3x at `0x45`, ESP32-S2 hardware, and pure ESP-IDF execution on this
  physical fixture.
- Calibrated temperature/humidity accuracy and production environmental range.

