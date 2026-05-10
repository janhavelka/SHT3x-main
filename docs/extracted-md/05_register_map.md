# Register Map

SHT3x-DIS exposes command-addressed state rather than byte registers: measurement output, a 16-bit status word, heater state, alert limits, and the 32-bit serial number are reached through 16-bit I2C commands. Source: datasheet, pp. 9-14; alert note, pp. 1-2; serial-number note, p. 1.

## Pseudo-Registers And Commands

| State / function | Access command(s) | Driver notes | Source |
|---|---|---|---|
| Measurement output | Single-shot command or `fetch data` (`0xE000`) | Returns temperature word + CRC, then humidity word + CRC. | Datasheet, pp. 10-11 |
| Status register | Read `0xF32D`; clear `0x3041` | 16-bit status word. Clear resets flags bit 15, 11, 10, and 4. | Datasheet, p. 13 |
| Heater state | Enable `0x306D`, disable `0x3066`; status bit 13 reports state | Heater disabled after reset. | Datasheet, pp. 12-13 |
| Alert limits | Read/write high set, high clear, low clear, low set | Alert note defines command encodings and reduced limit data format. | Alert note, pp. 1-2 |
| Serial number | `0x3780` with stretching or `0x3682` without stretching | Returns two words plus CRCs; assemble 32-bit serial number as MSW then LSW. | Serial-number note, p. 1 |

## Status Word Bits

| Bit(s) | Meaning | Default | Source |
|---|---|---:|---|
| 15 | Alert pending (`1` means at least one pending alert) | `1` | Datasheet, p. 13 |
| 13 | Heater status (`1` on) | `0` | Datasheet, p. 13 |
| 11 | RH tracking alert | `0` | Datasheet, p. 13 |
| 10 | Temperature tracking alert | `0` | Datasheet, p. 13 |
| 4 | System reset detected | `1` | Datasheet, p. 13 |
| 1 | Command status (`1` means last command not processed) | `0` | Datasheet, p. 13 |
| 0 | Write data checksum status (`1` means last write checksum failed) | `0` | Datasheet, p. 13 |

## Alert Limit Commands

| Limit | Read | Write | Initial physical value | Initial hex | Source |
|---|---:|---:|---|---:|---|
| High set | `0xE11F` | `0x611D` | 80 %RH / 60 degC | `0xCD33` | Alert note, p. 2 |
| High clear | `0xE114` | `0x6116` | 79 %RH / 58 degC | `0xC92D` | Alert note, p. 2 |
| Low clear | `0xE109` | `0x610B` | 22 %RH / -9 degC | `0x3869` | Alert note, p. 2 |
| Low set | `0xE102` | `0x6100` | 20 %RH / -10 degC | `0x3466` | Alert note, p. 2 |

SHT3x alert-limit storage uses reduced precision: 7 bits for humidity and 9 bits for temperature. Approximate limit resolution is 1 %RH and 0.5 degC. Source: alert note, p. 1.

## Alert Limit Reduced Format

The 16-bit alert limit word packs humidity and temperature thresholds as `RH[6:0]` and `T[8:0]`; the application note shows the initial high-set limit `80 %RH / 60 degC` as `0xCD33`. Convert physical thresholds with the alert-note equations before writing the reduced value and its CRC. Source: alert note, pp. 1-3.
