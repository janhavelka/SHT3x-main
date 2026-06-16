# Protocol Commands And Transactions

SHT3x-DIS uses 16-bit I2C commands. Measurement data is returned as 16-bit temperature followed by CRC, then 16-bit humidity followed by CRC. Source: datasheet, pp. 9-10.

## Framing And CRC

| Item | Behavior | Source |
|---|---|---|
| Addresses | `0x44` with `ADDR` low, `0x45` with `ADDR` high. | Datasheet, p. 9 |
| Commands | 16-bit command words already include a 3-bit CRC in the command encoding. | Datasheet, p. 9 |
| Data CRC | Every 16-bit data word is followed by 8-bit CRC. Write CRC is mandatory; read CRC processing is up to the master. | Datasheet, pp. 9, 14 |
| CRC parameters | CRC-8 polynomial `0x31`, init `0xff`, no reflection, final XOR `0x00`; example CRC of `0xbeef` is `0x92`. | Datasheet, p. 14 |
| Read order | Temperature word first, then relative humidity word. | Datasheet, p. 10 |

## Output Conversion

The datasheet states raw words are unsigned 16-bit, already linearized and compensated. Source: datasheet, p. 14.

| Output | Conversion |
|---|---|
| Temperature | `T_degC = -45 + 175 * rawT / (2^16 - 1)` |
| Relative humidity | `RH_percent = 100 * rawRH / (2^16 - 1)` |

## Read/Write Byte Counts

| Operation | Bytes after address phase | Notes | Source |
|---|---:|---|---|
| Send command | 2 write bytes | Command MSB then LSB. | Datasheet, p. 9 |
| Single-shot read | 6 read bytes | Temperature MSB/LSB/CRC, then humidity MSB/LSB/CRC. | Datasheet, p. 10 |
| Periodic fetch `0xE000` | 6 read bytes | Same temperature/RH order; the data memory is reset after fetch. | Datasheet, p. 11 |
| Read status `0xF32D` | 3 read bytes | 16-bit status word plus CRC. | Datasheet, p. 13 |
| Write alert limit | 5 write bytes | 16-bit command, 16-bit reduced-limit value, CRC. | Alert note, p. 2 |
| Read alert limit | 3 read bytes | 16-bit reduced-limit value plus CRC. | Alert note, p. 2 |
| Get serial number | 6 read bytes | Two 16-bit words with CRCs; `SNB_3` is MSB and `SNB_0` is LSB. | Serial-number note, p. 1 |

## Main Commands

| Command | Hex | Notes | Source |
|---|---:|---|---|
| Single-shot high repeatability, clock stretching | `0x2C06` | One RH/T pair. | Datasheet, p. 10 |
| Single-shot medium repeatability, clock stretching | `0x2C0D` | One RH/T pair. | Datasheet, p. 10 |
| Single-shot low repeatability, clock stretching | `0x2C10` | One RH/T pair. | Datasheet, p. 10 |
| Single-shot high repeatability, no clock stretching | `0x2400` | Master reads later; NACK while data not ready. | Datasheet, p. 10 |
| Single-shot medium repeatability, no clock stretching | `0x240B` | One RH/T pair. | Datasheet, p. 10 |
| Single-shot low repeatability, no clock stretching | `0x2416` | One RH/T pair. | Datasheet, p. 10 |
| Periodic ART | `0x2B32` | Starts ART mode at 4 Hz. | Datasheet, p. 11 |
| Fetch data | `0xE000` | Reads periodic/ART buffered data; NACK if no data is present; buffer clears after fetch. | Datasheet, p. 11 |
| Break periodic | `0x3093` | Stops periodic acquisition; takes 1 ms. | Datasheet, pp. 11-12 |
| Soft reset | `0x30A2` | Reinitializes system controller and reloads calibration data. | Datasheet, p. 12 |
| General-call reset | address `0x00`, byte `0x06` | Resets all devices on bus that support I2C general call. | Datasheet, p. 12 |
| Heater enable / disable | `0x306D` / `0x3066` | Heater is for plausibility checking only; disabled after reset. | Datasheet, pp. 12-13 |
| Read status / clear status | `0xF32D` / `0x3041` | Clear affects flags 15, 11, 10, and 4. | Datasheet, p. 13 |
| Get serial number, stretching / no stretching | `0x3780` / `0x3682` | Returns two words plus CRCs; 32-bit serial number. | Serial-number note, p. 1 |

## Periodic Measurement Commands

| MPS | High | Medium | Low | Source |
|---:|---:|---:|---:|---|
| 0.5 | `0x2032` | `0x2024` | `0x202F` | Datasheet, p. 11 |
| 1 | `0x2130` | `0x2126` | `0x212D` | Datasheet, p. 11 |
| 2 | `0x2236` | `0x2220` | `0x222B` | Datasheet, p. 11 |
| 4 | `0x2334` | `0x2322` | `0x2329` | Datasheet, p. 11 |
| 10 | `0x2737` | `0x2721` | `0x272A` | Datasheet, p. 11 |
