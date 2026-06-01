# Pinout And Signals

## 8-Pin DFN Pinout

| Pin | Name | Driver/wiring meaning | Source |
|---:|---|---|---|
| 1 | `SDA` | I2C serial data, input/output. | Datasheet, p. 8 |
| 2 | `ADDR` | I2C address select; connect high or low, do not float. | Datasheet, pp. 8-9 |
| 3 | `ALERT` | Alarm output; leave floating if unused. | Datasheet, pp. 8-9 |
| 4 | `SCL` | I2C serial clock, input/output. | Datasheet, p. 8 |
| 5 | `VDD` | Supply voltage. | Datasheet, p. 8 |
| 6 | `nRESET` | Active-low reset input. If unused, leave floating or connect to `VDD` through >=2 kOhm. | Datasheet, pp. 8-9 |
| 7 | `R` | No electrical function; connect to `VSS`. | Datasheet, p. 8 |
| 8 | `VSS` | Ground. | Datasheet, p. 8 |

## I2C Address Selection

| `ADDR` level | 7-bit address | Source |
|---|---:|---|
| Logic low | `0x44` (default) | Datasheet, p. 9 |
| Logic high | `0x45` | Datasheet, p. 9 |

The `ADDR` level may be changed dynamically, but it must remain constant from I2C START until the communication finishes. Source: datasheet, p. 8.

## Signal Notes

- `SCL` and `SDA` are open-drain I/Os with diodes to `VDD` and `VSS`; use external pull-ups such as 10 kOhm, sized for bus capacitance and speed. Source: datasheet, p. 8.
- `ALERT` switches high when alert conditions are met. The alert application note says the ALERT pin is push-pull. Source: datasheet, p. 9; alert note, p. 1.
- `nRESET` needs a low pulse of at least 1 us to reset the sensor; it has an internal typ. 50 kOhm pull-up to `VDD`. Source: datasheet, p. 9.
- Decouple `VDD` with 100 nF placed close to the sensor. Source: datasheet, p. 8.
