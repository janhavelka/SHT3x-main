# Chip Overview

SHT3x-DIS is a digital humidity and temperature sensor family with factory-calibrated, linearized, and temperature-compensated outputs. It uses I2C, supports two selectable addresses, and is packaged in a 2.5 mm x 2.5 mm x 0.9 mm 8-pin DFN. Source: datasheet, p. 1.

## Family And Outputs

| Variant | Humidity accuracy anchor | Temperature accuracy anchor | Source |
|---|---|---|---|
| SHT30 | Typ. 2 %RH | Typ. 0.2 degC over 0-65 degC | Datasheet, p. 2 |
| SHT31 | Typ. 2 %RH | Typ. 0.2 degC over 0-90 degC | Datasheet, p. 2 |
| SHT35 | Typ. +/-1.5 %RH | Typ. +/-0.1 degC over 20-60 degC | Datasheet, p. 2 |

## Measurement Facts

| Parameter | Value | Source |
|---|---:|---|
| Humidity specified range | 0 to 100 %RH | Datasheet, p. 2 |
| Temperature specified range | -40 to 125 degC | Datasheet, p. 2 |
| Humidity repeatability | Low 0.21 %RH, medium 0.15 %RH, high 0.08 %RH typ. | Datasheet, p. 2 |
| Temperature repeatability | Low 0.15 degC, medium 0.08 degC, high 0.04 degC typ. | Datasheet, p. 2 |
| Humidity resolution | 0.01 %RH typ. | Datasheet, p. 2 |
| Temperature resolution | 0.01 degC typ. | Datasheet, p. 2 |
| RH response time | 8 s to 63% humidity step at 25 degC and 1 m/s airflow | Datasheet, p. 2 |

The sensor supports single-shot measurements, periodic measurements at 0.5/1/2/4/10 measurements per second, an ART mode at 4 Hz, status readout, soft reset, heater control, and alert limits. Source: datasheet, pp. 10-13.
