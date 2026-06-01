# SHT3x Release-Readiness Gap Closure Plan

Date: 2026-06-01
Branch: `hardening/sht3x-release-readiness-gaps`
Base: `main` at `87213c6`

Scope: focused code, docs, and CI release-readiness gap closure after
`docs/SHT3X_INDUSTRIAL_READINESS_EXPLORATION.md`.

This plan does not claim physical ALERT pin validation, humidity accuracy
validation, fault-injection validation, soak validation, or full hardware
validation. Full HIL is out of scope for this sequence.

## Subagent Findings

- `release-metadata-agent`: version metadata still says `1.5.0` in
  `library.json`, `idf_component.yml`, generated `Version.h`, and `Doxyfile`,
  while tag `v1.5.0` already exists. Current unreleased changes look like a
  likely `2.0.0` SemVer candidate because copy/move deletion, required timing
  callbacks, and `readStatus()` behavior changes are compatibility-sensitive.
  A clear Breaking Changes / Migration section is missing.
- `idf-ci-agent`: pure ESP-IDF S2/S3 support is configured but not proven. The
  failing CI jobs need real Actions-log inspection or ESP-IDF 5.4 container
  reproduction. Local contract and version checks are not enough to claim IDF
  validation.
- `alert-encoding-agent`: app-note vectors are `0xCD33`, `0xC92D`, `0x3869`,
  and `0x3466`. Current kind-blind `encodeAlertLimit()` matches only two of
  four. Add vendor-vector tests first, then fix or document exact API limits.
- `api-contract-agent`: raw command helpers need caller-responsibility wording
  for periodic/ART legality and raw CRC handling. `end()` is local-only,
  reset/general-call side effects and preconditions need sharper docs,
  `tick()` surfaces failures only through health/last-error side channels, and
  recovery docs need to match implementation.
- `hil-docs-agent`: current docs now acknowledge limited smoke-HIL evidence,
  but the physical PASS log is tied to commit `8661a38`, not current HEAD.
  Curated `docs/hil/` evidence and completed target template are still missing.
  The prior PASS skipped selftest, recover, ALERT threshold, fault, stress, and
  soak checks.
- `package-export-agent`: `library.json` has no explicit export rules,
  `.gitattributes` has no `export-ignore`, and tracked `hil_logs/` contradict
  `.gitignore`. Packaging policy for generated logs, extracted references,
  tools, tests, and generated `Version.h` must be made explicit.
- `hil-runner-agent`: dry-run is implemented and documented as non-evidence.
  The runner emits transcript, summaries, checklist, and environment files, but
  there is no separate `device-tester` runner and no `--base-address` option in
  the inspected path. Hardware runs require `--port`; dry-runs force an
  `INCOMPLETE` verdict and must not be treated as physical evidence.
- `final-review-agent`: the plan covers the P0 and P1 gaps from the audit:
  version metadata, migration notes, ESP-IDF CI, alert encoding, HIL claim
  boundaries, package/export hygiene, raw command contracts, reset/end/tick
  side effects, and missing manual hardware evidence.

## Chunk Order

1. Alert-limit encoding tests/fix.
   - Add native tests for the Sensirion app-note vectors.
   - Decide whether the public static encoder remains approximate/kind-blind or
     whether write paths need kind-aware quantization.
   - Fix code or document exact limitations before any release claim around
     alert-limit writes.

2. ESP-IDF CI fix.
   - Inspect the failed GitHub Actions IDF job logs.
   - Reproduce `idf.py -C examples/idf/basic set-target esp32s3 build` and
     `esp32s2 build` in an ESP-IDF 5.4 environment.
   - Fix only the CI/build issue and require a green CI run before claiming
     pure ESP-IDF validation.

3. Version/changelog/migration.
   - Choose the next SemVer version, likely `2.0.0` if breaking items remain.
   - Update `library.json`, `idf_component.yml`, generated `Version.h`,
     `Doxyfile`, README release wording, and changelog compare refs.
   - Promote `[Unreleased]` into a dated release section only after release
     gates pass.
   - Add explicit Breaking Changes / Migration guidance for timing callbacks,
     deleted copy/move, and `readStatus()` behavior.

4. API contract and side-effect docs.
   - Clarify raw command helper responsibilities and mode restrictions.
   - Document `end()` as local-only unless behavior is changed.
   - Document reset/general-call reset preconditions and local state effects.
   - Clarify `tick()` failure visibility, recovery behavior, callback
     boundedness, and health counters as begin/end session counters.

5. HIL docs/evidence and package export hygiene.
   - Keep smoke-HIL wording limited to the exact prior run and its commit.
   - Mark unrun hardware rows as not run; do not promote dry-runs to hardware
     evidence.
   - Decide whether to create curated `docs/hil/` evidence or untrack generated
     `hil_logs/` scratch logs.
   - Add PlatformIO package export rules and optional `.gitattributes`
     `export-ignore` rules for source archives.

6. HIL runner dry-run/device-tester validation.
   - Run dry-run only, not hardware HIL.
   - Tighten parser acceptance for required health fields and final failure
     counters.
   - Add tests for dry-run artifact generation, flag planning, summary schema,
     custom command review behavior, and missing-health negative cases.
   - Keep output/fault/ALERT physical procedures operator/fixture-gated.

7. Final validation/report.
   - Run lightweight guards, native tests, Arduino S2/S3 builds, package checks,
     dry-run HIL, and CI checks as available.
   - Record exactly what passed, what was not run, and which release blockers
     remain.
   - Do not tag or claim production/industry-grade readiness until CI and
     evidence gates pass.

## Current Blockers

- Final review subagent did not return before timeout.
- `idf.py` is not available in this local shell; pure ESP-IDF build diagnosis
  needs GitHub logs or an ESP-IDF 5.4 environment.
- No new hardware HIL was run in this chunk by design.
