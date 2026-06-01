# Variants And Open Questions

## Device Variants

| Variant / option | Driver-visible facts | Source |
|---|---|---|
| SHT30, SHT31, SHT35 | Same command protocol; accuracy differs by variant. | Datasheet, pp. 1-2 |
| Membrane option | PTFE membrane protects against water/dust according to IP67 and keeps RH response time within uncovered-sensor specification. Protocol is in the main datasheet. | Membrane datasheet, p. 1 |
| ALERT mode | Applies to SHT3x-DIS in periodic mode; STS3x has temperature-only limits, but SHT3x supports humidity and temperature limits. | Alert note, pp. 1-2 |

## Revision Notes

- Main datasheet source is Version 6, February 2019. Source: datasheet, p. 1.
- Serial-number application note source is Version 3, January 2023; it changed `tIDLE` to 1 ms in an earlier revision and currently documents `0x3780`/`0x3682`. Source: serial-number note, pp. 1-2.
- Membrane option source is Version 4.1, March 2025. Source: membrane datasheet, p. 1.

## Open Questions For Implementation

- The compact notes do not encode all package/tape dimensions; inspect the PDF drawings for footprint or production tooling. Source: datasheet, pp. 16-18; membrane datasheet, p. 2.
- The main datasheet references several handling/design guides that are not in this compact set; use them before adding manufacturing or cleaning guidance to the library. Source: datasheet, pp. 6, 8.
- The supplemental humidity-formula note provides dew point and absolute-humidity application equations; it adds no SHT3x-DIS I2C command, status-bit, or alert-limit definitions.
