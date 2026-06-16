# Modes, Interrupts, Status, And Faults

## Measurement Modes

| Mode | Behavior | Readout | Source |
|---|---|---|---|
| Single-shot | One command triggers one RH/T pair. Commands select repeatability and clock stretching. | Read sensor after measurement; with no stretching, read header NACKs until data is ready. | Datasheet, p. 10 |
| Periodic | One command starts a stream of RH/T pairs at selected mps and repeatability. | Use `fetch data` (`0xE000`); data memory clears after fetch. | Datasheet, pp. 10-11 |
| ART | Accelerated response-time mode starts acquisition at 4 Hz. | Same fetch and break flow as periodic mode. | Datasheet, p. 11 |

## ALERT Behavior

- Alert mode is active whenever the sensor operates in periodic data acquisition mode. Source: alert note, p. 1.
- The ALERT pin changes state when humidity or temperature crosses programmable limits; status bits identify RH and temperature alert causes. Source: datasheet, p. 13; alert note, p. 1.
- Alert can be deactivated per channel by setting the minimum set point higher than the maximum set point. Source: alert note, p. 1.
- Separate set and clear thresholds provide hysteresis to avoid fast ALERT oscillation. Source: alert note, p. 1.

## Fault And Status Indicators

| Indicator | Meaning | Source |
|---|---|---|
| Status bit 1 | Last command was not processed, invalid, or failed integrated command checksum. | Datasheet, p. 13 |
| Status bit 0 | Last write data checksum failed. | Datasheet, p. 13 |
| Status bit 4 | Reset detected since last clear-status command. | Datasheet, p. 13 |
| Read NACK in periodic fetch | No measurement data present. | Datasheet, p. 11 |
| Read NACK after no-stretch single-shot | Measurement result not ready yet. | Datasheet, p. 10 |

## Heater

The internal heater is intended for plausibility checking only. It raises temperature by a few degrees depending on conditions and is disabled after reset. Source: datasheet, pp. 12-13.
