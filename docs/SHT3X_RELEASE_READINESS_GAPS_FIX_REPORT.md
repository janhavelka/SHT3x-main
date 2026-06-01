# SHT3x Release Readiness Gaps Fix Report

Date: 2026-05-31
Branch: `main`

This report records documentation/tooling changes made for release-readiness
gap closure. It is not a release certificate.

## Automatic HIL Runner Extension

- Command groups added: expanded safe default smoke, single-shot
  low/medium/high, selected periodic 0.5/1/2 mps, ART, status/status_restore,
  serial/EIC, heater status, alert read, and alert encode/decode vectors.
- Opt-in groups added: destructive maintenance, bus-wide reset, bounded soak,
  clock stretching, alert write/readback, additional periodic 4/10 mps,
  ALERT output procedure, and fault/unplug/CRC-injection procedure.
- Flags added: `--include-destructive`, `--include-bus-wide-reset`,
  `--include-soak`, `--soak-count`, `--include-clock-stretch`,
  `--include-alert-write`, `--include-all-periodic-rates`,
  `--include-output-tests`, `--include-fault-tests`, `--expect-address`,
  `--board`, `--target-name`, and `--operator`.
- Evidence files generated: `serial_transcript.txt`, `summary.md`,
  `summary.json`, `operator_checklist.md`, and `environment.txt`. Operator
  groups can also create `operator_notes.md`, `alert_gpio_capture.csv`,
  `logic_analyzer_reference.txt`, and `photos_or_evidence_manifest.md`.
- Parsing/acceptance added: version, scan address, measurement plausibility,
  raw/comp cache, status word/bits, serial/EIC, heater OFF, alert raw values,
  alert encode/decode vectors, status_restore fields, driver READY/online, and
  zero final failures.
- Dry-run result: `python tools/run_sht3x_hil.py --dry-run --expect-address
  0x44 --board esp32s3 --target-name dry-run` completed and generated
  `hil_logs/i2c_20260531T201540Z/` with final verdict `INCOMPLETE`, as expected
  for dry-run.
- Hardware run result: not run in this change. The user did not explicitly
  request a new hardware run for this prompt.
- Remaining operator/fixture-only evidence: physical ALERT pin transitions,
  humidity accuracy, fault injection, long-soak stability, ESP32-S2 hardware
  evidence, and any production fixture evidence.
