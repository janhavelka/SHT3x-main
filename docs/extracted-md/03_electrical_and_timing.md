# Electrical And Timing

## Electrical Specifications

| Parameter | Value | Source |
|---|---:|---|
| `VDD` operating range | 2.15 V to 5.5 V | Datasheet, p. 6 |
| Power-on reset level `VPOR` | 1.8 V min, 2.10 V typ., 2.15 V max | Datasheet, p. 6 |
| Supply slew rate | 20 V/ms max between `VDD,min` and `VDD,max` | Datasheet, p. 6 |
| Idle current, single-shot mode at 25 degC | typ. 0.2 uA, max 2.0 uA | Datasheet, p. 6 |
| Idle current, periodic mode | typ. 45 uA | Datasheet, p. 6 |
| Measuring current | typ. 600 uA, max 1500 uA | Datasheet, p. 6 |
| Average current, 1 Hz low-repeatability single-shot | typ. 1.7 uA | Datasheet, p. 6 |
| Heater power | 3.6 mW to 33 mW depending on supply voltage | Datasheet, p. 6 |

## Timing

| Parameter | 2.4-5.5 V value | 2.15-<2.4 V value | Source |
|---|---:|---:|---|
| Power-up time | typ. 0.5 ms, max 1 ms | typ. 0.5 ms, max 1.5 ms | Datasheet, p. 7 |
| Soft reset time | typ. 0.5 ms, max 1.5 ms | not repeated in low-voltage table | Datasheet, p. 7 |
| `nRESET` low pulse | min 1 us | min 1 us | Datasheet, p. 7 |
| Low repeatability measurement | typ. 2.5 ms, max 4 ms | typ. 2.5 ms, max 4.5 ms | Datasheet, p. 7 |
| Medium repeatability measurement | typ. 4.5 ms, max 6 ms | typ. 4.5 ms, max 6.5 ms | Datasheet, p. 7 |
| High repeatability measurement | typ. 12.5 ms, max 15 ms | typ. 12.5 ms, max 15.5 ms | Datasheet, p. 7 |
| Minimum time after command before next command | 1 ms | 1 ms | Datasheet, p. 9 |

## I2C Timing And Ratings

- I2C clock can be selected from 0 to 1000 kHz; Fast Mode up to 400 kHz follows the I2C Fast Mode standard, and up to 1 MHz follows the datasheet timing table. Source: datasheet, pp. 8, 21.
- I2C timing table lists `fSCL` max 1000 kHz, SDA setup min 100 ns, rise/fall max 300 ns, and SDA valid time max 0.9 us. Source: datasheet, p. 21.
- Absolute max `VDD` is -0.3 V to 6 V; pin voltages on SDA/ADDR/ALERT/SCL/nRESET are -0.3 V to `VDD + 0.3 V`; input current on any pin is +/-100 mA. Source: datasheet, p. 7.
