# Initialization, Reset, And Operational Notes

## Startup And Reset

- After `VDD` reaches `VPOR`, the sensor enters idle after the specified power-up time. Source: datasheet, p. 7.
- A minimum 1 ms wait is needed after sending a command before another command can be received. Source: datasheet, p. 9.
- Soft reset is `0x30A2`; send it while SHT3x-DIS is idle. It resets the system controller and reloads calibration data. Source: datasheet, p. 12.
- Interface reset can be attempted by leaving `SDA` high and toggling `SCL` nine or more times, followed by a transmission start. This resets the serial interface only; the status register is preserved. Source: datasheet, p. 12.
- Full hard reset means cycling `VDD`; remove voltage from `SDA`, `SCL`, and `ADDR` too to avoid powering through ESD diodes. Source: datasheet, p. 12.

## Typical Measurement Flows

| Flow | Steps | Source |
|---|---|---|
| Single-shot no stretching | Send one of `0x24xx`, wait measurement time, read six bytes: T MSB/LSB/CRC, RH MSB/LSB/CRC. | Datasheet, p. 10 |
| Single-shot with stretching | Send one of `0x2Cxx`, then read; sensor ACKs and holds SCL low until measurement is complete. | Datasheet, p. 10 |
| Periodic | Send periodic command, poll/fetch with `0xE000`, stop with break `0x3093` before most other commands. | Datasheet, pp. 10-12 |
| Serial number | Send `0x3780` or `0x3682`, wait 1 ms, read two 16-bit words with CRC. | Serial-number note, p. 1 |

## Operating Range And Validation Notes

- Best performance is in 5 to 60 degC and 20 to 80 %RH. Long exposure outside normal range, especially high humidity, can temporarily offset RH; returning to normal range allows slow recovery. Source: datasheet, p. 6.
- Sensirion says sensors are factory-tested/calibrated and customer calibration verification is not required, though after-assembly tests may be useful to verify storage, handling, and assembly. Source: ambient testing note, p. 1.
- For ambient production tests, Sensirion's fixture guidance minimizes temperature/RH gradients and places an accurate reference sensor close to the DUT. Source: ambient testing note, pp. 1-2.
- Heater use is a plausibility check: it raises sensor temperature by a few degrees depending on conditions, has 3.6 mW to 33 mW power depending on supply, and is disabled after reset. Source: datasheet, pp. 6, 12-13.
