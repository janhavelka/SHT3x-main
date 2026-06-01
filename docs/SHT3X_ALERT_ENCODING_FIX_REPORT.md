# SHT3x Alert Encoding Fix Report

Date: 2026-06-01
Branch: `hardening/sht3x-release-readiness-gaps`

## Scope

This chunk closes the software alert-limit encoding P0 item from
`docs/SHT3X_INDUSTRIAL_READINESS_EXPLORATION.md`.

No physical ALERT pin validation, humidity accuracy validation, fault
injection, soak test, or full HIL was run or claimed.

## Vectors Tested

The tests use the local Sensirion alert-mode application-note defaults from
`docs/pdf-extracted-md/SHT3x_HT_AN_AlertMode.md`.

| Limit | API input `(temperatureC, humidityPct)` | Expected word |
| --- | ---: | ---: |
| High alert set | `60.0f, 80.0f` | `0xCD33` |
| High alert clear | `58.0f, 79.0f` | `0xC92D` |
| Low alert clear | `-9.0f, 22.0f` | `0x3869` |
| Low alert set | `-10.0f, 20.0f` | `0x3466` |

Native tests now cover:

- exact `encodeAlertLimit()` output for all four vectors,
- `decodeAlertLimit()` for the same four raw words, with reduced-format
  tolerance,
- clamp behavior below and above RH/temperature representable ranges,
- encode/decode round-trip for `0x0000`, `0xFFFF`, and all four app-note words,
- `writeAlertLimit()` payload generation for all four app-note vectors,
  including CRC over the 16-bit alert-limit word,
- non-finite `writeAlertLimit()` rejection for both temperature and humidity.

## Test-First Result

The first native run was made after adding the new tests and before changing
`src/SHT3x.cpp`.

Observed failure:

- `test_alert_limit_app_note_encode_vectors`: expected `0xC92D`, got `0xCB2D`
  for `79% / 58 C`.
- `test_write_alert_limit_uses_app_note_encoder_vectors`: expected payload
  `0xC92D`, got `0xCB2D`.

The prior audit and read-only agent calculation also showed the old helper would
encode `20% / -10 C` as `0x3266` instead of `0x3466`; the Unity loop stopped at
the first failing vector in each failing test.

## Root Cause

The old helper converted physical RH/T values to rounded full-scale 16-bit raw
values and then dropped humidity and temperature LSBs. That is a valid reduced
format path for arbitrary values, but it did not preserve two locally documented
Sensirion reset-default words whose physical labels are rounded application-note
labels.

The fix preserves the four published application-note default words exactly
before falling back to the existing generic RH7/T9 quantization path. This is an
intentional canonical-default exception for those labels; nearby non-default
values still use the generic quantizer.

## Code And Test Changes

- `src/SHT3x.cpp`
  - Added a small local table for the four Sensirion app-note default labels.
  - `encodeAlertLimit()` now returns those documented words exactly when the
    clamped physical inputs match the default labels.
  - The existing generic conversion, clamping, packing, and `writeAlertLimit()`
    flow remain in place for other values.

- `include/SHT3x/SHT3x.h`
  - Documented that app-note reset-default labels encode to documented default
    raw words and other values use reduced RH7/T9 quantization.

- `README.md`
  - Added alert-limit argument order and the four app-note vectors.
  - Kept the hardware ALERT validation caveat.

- `test/test_basic.cpp`
  - Added exact app-note encode tests.
  - Added decode tests for the same raw words.
  - Added clamp and round-trip edge tests.
  - Added `writeAlertLimit()` payload/CRC coverage for all four vectors.
  - Extended non-finite input rejection coverage.

## Validation Commands

| Command | Result |
| --- | --- |
| `python tools/check_core_timing_guard.py` | PASS |
| `python tools/check_cli_contract.py` | PASS |
| `python tools/check_idf_example_contract.py` | PASS |
| `python scripts/generate_version.py check` | PASS, `Version.h` up to date |
| `python -m platformio test -e native` | PASS, 75/75 native tests |
| `python -m platformio run -e esp32s3dev` | PASS |
| `python -m platformio run -e esp32s2dev` | PASS |

## Remaining ALERT Boundary

This proves software packing, unpacking, and write payload generation only.
Hardware ALERT pin behavior, threshold trip/clear behavior, humidity accuracy,
fault injection, and soak behavior remain not validated in this chunk.
