# SHT3x (SHT30/SHT31/SHT35) — Software Driver Extraction Notes (I²C)

This document is an **extracted, driver-focused** consolidation of everything relevant to implementing a complete software driver for **Sensirion SHT3x-DIS** humidity + temperature sensors, plus the **Alert Mode** and **Serial Number (EIC)** application notes and related material.

**Primary sources used (prefer in case of conflicts):**
- **SHT3x-DIS Datasheet**, Feb 2019, Version 6 (`SHT3x_datasheet.pdf`)
- **Alert Mode of SHT3x- and STS3x-DIS**, Nov 2023, Version 3.1 (`SHT3x_HT_AN_AlertMode.pdf`)
- **Electronic Identification Code / Serial Number**, Jan 2023, Version 3 (`Sensirion_electronic_identification_code_SHT3x.pdf`)
- **Membrane Option Datasheet**, Mar 2025, Version 4.1 (`SHT3x_membrane_flyer.pdf`)
- **Humidity at a glance**, May 2025, Version 2.1 (`Sensirion_AppNotes_Humidity_Sensors_at_a_Glance.pdf`)
- **Testing at Ambient Conditions**, Oct 2022, Version 3 (`Sensirion_Humidity_Sensors_Testing_at_Ambient_Conditions.pdf`)
- **HT_AlertMode_BitConversion.xlsx** (Sensirion-provided conversion helper for alert limits)

---

## 1. Device variants and what a driver should support

### 1.1 Device family
- **SHT3x-DIS** is a **humidity + temperature** digital sensor with an **I²C** interface.
- SHT30 / SHT31 / SHT35 are accuracy variants (protocol/commands are the same for a driver).

### 1.2 Key functional blocks exposed to software
A complete driver should implement:
- I²C command transport (16-bit commands, MSB-first)
- CRC-8 validation (for **every 16-bit data word** returned and for **every 16-bit word written**)
- Measurement:
  - **Single shot** (with or without clock stretching)
  - **Periodic** (0.5/1/2/4/10 measurements per second)
  - **Fetch Data** readout path for periodic mode
  - **ART** mode (accelerated response time)
- Health and recovery:
  - Interface reset sequence
  - Soft reset
  - General-call reset (bus-wide)
  - Optional hardware reset via nRESET (board-level)
- Status register:
  - Read status and interpret bits
  - Clear status flags
- Heater:
  - Enable/disable
  - Detect heater state from status
- Alert mode configuration (limits read/write, limit encoding, behavior)
- Electronic Identification Code / serial number (32-bit unique ID)

---

## 2. Pins, wiring, addresses, and I²C electrical assumptions

### 2.1 Pin assignment (8-pin DFN)
| Pin | Name    | Direction | Notes |
|---:|---------|-----------|------|
| 1 | SDA    | I/O | I²C serial data (open drain) |
| 2 | ADDR   | In  | Address select; **must not float**; tie to logic low/high |
| 3 | ALERT  | Out | Alarm output; **must be left floating if unused** |
| 4 | SCL    | I/O | I²C serial clock (open drain) |
| 5 | VDD    | In  | Supply |
| 6 | nRESET | In  | Active-low reset input; if unused, recommended float or tie to VDD via series **R ≥ 2 kΩ**; internal pull-up ≈ 50 kΩ typ |
| 7 | R (no function) | — | Connect to VSS (package “no electrical function” pad) |
| 8 | VSS    | — | Ground |

**Pull-ups:** SDA and SCL need external pull-ups (example given: **~10 kΩ**); consider bus capacitance and frequency.

### 2.2 I²C device addresses
Only the **7 MSBs** of the I²C header are the address; the LSB is R/W.
| Condition | 7-bit addr (hex) |
|---|---|
| ADDR tied low | **0x44** (default) |
| ADDR tied high | **0x45** |

### 2.3 Supported I²C speed and timing limits
- Supported SCL frequency: **0–1000 kHz (1 MHz)**.
- Bus capacitive load: **≤ 400 pF**.
- Key timing (valid for -40…125 °C and VDD between VDDmin…VDDmax; nomenclature per UM10204):
  - tHD;STA = 0.24 µs (hold time for repeated START)
  - tLOW = 0.53 µs, tHIGH = 0.26 µs
  - tHD;DAT (transmit) = 0…250 ns, (receive) = 0…∞
  - tSU;DAT = 100 ns
  - tR, tF ≤ 300 ns
  - tVD;DAT ≤ 0.9 µs
  - tSU;STA = 0.26 µs, tSU;STO = 0.26 µs

(See datasheet timing diagram and Table 21.)

---

## 3. Power and timing behavior relevant to software

### 3.1 Supply and POR
- VDD: **2.15–5.5 V** (typ 3.3 V)
- VPOR (power-up/down level): min 1.8 V, typ 2.10 V, max 2.15 V
- VDD slew rate should be **≤ 20 V/ms**; faster may cause reset

### 3.2 Power-up and reset timing
- Power-up time (after hard reset / power-up): typ **0.5 ms**, max **1.0 ms** (at VDD ≥ 2.4 V)
  - At VDD < 2.4 V, max **1.5 ms**
- Soft reset time (ACK of soft reset command → idle): typ **0.5 ms**, max **1.5 ms**
- nRESET pulse: **≥ 1 µs** low to trigger reset

### 3.3 Measurement duration (tMEAS)
At VDD ≥ 2.4 V:
- Low repeatability: typ **2.5 ms**, max **4.0 ms**
- Medium repeatability: typ **4.5 ms**, max **6.0 ms**
- High repeatability: typ **12.5 ms**, max **15.0 ms**

At 2.15 V ≤ VDD < 2.4 V:
- Low: typ 2.5 ms, max 4.5 ms
- Medium: typ 4.5 ms, max 6.5 ms
- High: typ 12.5 ms, max 15.5 ms

### 3.4 Mandatory command spacing (tIDLE)
After sending **any command**, a **minimum waiting time of 1 ms** is required before the sensor can receive another command (datasheet).  
For serial-number readout, the EIC note explicitly recommends waiting **tIDLE = 1 ms** before issuing the read header.

---

## 4. Data framing, CRC rules, and conversions

### 4.1 Command framing: 16-bit commands
- All commands are **16-bit**, sent **MSB first, then LSB**.
- Commands and data are mapped to a **16-bit address space**.
- The **16-bit command constants already include** an integrated 3-bit checksum (you transmit the provided hex constant).

### 4.2 Data framing: 16-bit words + CRC byte
Sensor responses return 16-bit words followed by CRC:
- **Temperature word**: 2 bytes + 1 CRC
- **Humidity word**: 2 bytes + 1 CRC  
So a full measurement read is **6 bytes**.

**Writing data:** When writing *any* 16-bit data word (e.g., alert limits), transmitting the CRC byte is **mandatory**; the sensor only accepts data if the checksum is correct.

**Reading data:** CRC is always provided by the sensor; it is up to the master to validate.

### 4.3 CRC-8 parameters (I²C CRC)
CRC properties (datasheet Table 20):
- Algorithm: CRC-8
- Polynomial: **0x31** (x⁸ + x⁵ + x⁴ + 1)
- Init: **0xFF**
- Reflect in/out: False
- Final XOR: 0x00
- Example: CRC(0xBEEF) = 0x92

**CRC input is exactly the two data bytes** (MSB, LSB) of the 16-bit word.

### 4.4 Converting raw measurement values (16-bit)
Let `SRH` and `ST` be raw unsigned 16-bit values.

- Relative humidity (%RH):
  - `RH = 100 * SRH / 65535`
- Temperature (°C):
  - `T_C = -45 + 175 * ST / 65535`
- Temperature (°F):
  - `T_F = -49 + 315 * ST / 65535`

---

## 5. Full command set (register/command “map”)

### 5.1 Single-shot measurement commands (Table 9)
**Clock stretching enabled (sensor stretches SCL during measurement):**
- High repeatability: **0x2C06**
- Medium repeatability: **0x2C0D**
- Low repeatability: **0x2C10**

**Clock stretching disabled:**
- High repeatability: **0x2400**
- Medium repeatability: **0x240B**
- Low repeatability: **0x2416**

**Behavioral notes (datasheet):**
- If clock stretching is **disabled**, reading immediately after the command yields **NACK** until data is ready.
- If clock stretching is **enabled**, the sensor ACKs the read header, then pulls **SCL low** until measurement is complete, then returns data.

### 5.2 Periodic measurement commands (Table 10)
In periodic mode, one measurement command yields a continuous stream. Data is retrieved using **Fetch Data**.

**0.5 measurements per second (mps):**
- High: **0x2032**
- Medium: **0x2024**
- Low: **0x202F**

**1 mps:**
- High: **0x2130**
- Medium: **0x2126**
- Low: **0x212D**

**2 mps:**
- High: **0x2236**
- Medium: **0x2220**
- Low: **0x222B**

**4 mps:**
- High: **0x2334**
- Medium: **0x2322**
- Low: **0x2329**

**10 mps:**
- High: **0x2737**
- Medium: **0x2721**
- Low: **0x272A**

**Note:** At the highest mps setting, **self-heating might occur** (datasheet note under Table 10).

### 5.3 Fetch Data (periodic readout) (Table 11)
- Fetch Data: **0xE000**

**Behavior:**
- If no measurement data is present, the read header is responded with **NACK** and communication stops.
- After Fetch Data readout, the sensor’s data memory is **cleared** (so subsequent fetch without new data yields NACK).

### 5.4 ART mode (accelerated response time) (Table 12)
- Periodic Measurement with ART: **0x2B32**

After issuing ART, the sensor acquires data at **4 Hz** (applicable periodic-mode readout rules apply; stop with Break).

### 5.5 Break / stop periodic (Table 13)
- Break: **0x3093**

**Notes:**
- Recommended to stop periodic mode before sending another command (except Fetch Data).
- Upon Break, the sensor aborts the ongoing measurement and enters single-shot mode; this takes **~1 ms**.

### 5.6 Resets
**Soft reset (Table 14):**
- Soft Reset: **0x30A2**  
Works when the sensor is in **idle state**.

**General call reset (Table 15):**
- I²C address byte: **0x00**
- Second byte: **0x06**
- Equivalent 16-bit view: **0x0006**
**Warning:** This is **bus-wide** for all general-call-capable devices.

**nRESET pin reset:**
- Active-low reset input; **≥ 1 µs** low pulse
- Resets like a hard reset; interface-only reset sequence exists (below)

**Interface reset sequence (when comm is lost):**
- Leave SDA high, toggle SCL **9 or more times**, then issue a START before next command.
- This resets the **serial interface only**; status register content is preserved.

### 5.7 Heater (Table 16)
- Heater Enable: **0x306D**
- Heater Disable: **0x3066**
- Heater is disabled after reset (default).

The heater is “meant for plausibility checking only”; temperature increase is a few °C depending on conditions.

### 5.8 Status register (Tables 17–19)
**Read status:**
- Read Out of status register: **0xF32D**
- Read returns: 2 bytes status + CRC

**Clear status flags:**
- Clear status register: **0x3041**

**Status bit meanings (16-bit):**
| Bit | Name | Meaning |
|---:|------|---------|
| 15 | Alert pending | 1: at least one pending alert |
| 14 | Reserved | — |
| 13 | Heater status | 1: heater ON |
| 12 | Reserved | — |
| 11 | RH tracking alert | 1: RH alert |
| 10 | T tracking alert | 1: temperature alert |
| 9:5 | Reserved | — |
| 4 | System reset detected | 1: reset detected (hard reset / soft reset / supply fail) |
| 3:2 | Reserved | — |
| 1 | Command status | 1: last command not processed (invalid / command checksum failure) |
| 0 | Write data checksum status | 1: last write transfer checksum failed |

Bits (15, 11, 10, 4) can be cleared by the clear-status command.

### 5.9 Serial number / electronic identification code
Two commands are documented (EIC note):
- Get Serial Number (clock stretching enabled): **0x3780**
- Get Serial Number (clock stretching disabled): **0x3682**

Readout returns **2 words**, each followed by CRC:
- Word1 bytes: SNB_3, SNB_2 + CRC
- Word2 bytes: SNB_1, SNB_0 + CRC  
Together SNB_3..SNB_0 form a **32-bit unique serial number** (SNB_0 is LSB).

---

## 6. I²C transaction recipes (exact sequences)

### 6.1 Single-shot measurement — no clock stretching
1. START
2. Write header (addr + W)
3. Send 16-bit measurement command (MSB, LSB)
4. STOP
5. Wait tMEAS (worst-case for selected repeatability and VDD range)
6. START
7. Read header (addr + R)
8. Read 6 bytes: T_MSB, T_LSB, T_CRC, RH_MSB, RH_LSB, RH_CRC
9. NACK + STOP after last byte

**Alternative “data ready” probing:** If you attempt a read before data is ready, the sensor will NACK the read header.

### 6.2 Single-shot measurement — clock stretching
1. START
2. Write header
3. Send 16-bit command (stretching enabled variant)
4. (Option A) Immediately issue read header: the sensor ACKs and then stretches SCL low until done  
   or  
   (Option B) Wait a short time then read header
5. Read 6 bytes as above

### 6.3 Periodic measurement and fetch
1. Send one periodic measurement command (Table 10)
2. Whenever you want data:
   - Send Fetch Data command (0xE000)
   - Read 6 bytes (T word + CRC, RH word + CRC)
   - If read header is NACK, no new data present
3. To stop periodic mode: send Break (0x3093) and wait ~1 ms.

### 6.4 Status read / clear
- Read status: write 0xF32D, wait ≥1 ms, then read 3 bytes (2 status + CRC)
- Clear status: write 0x3041 (no readback)

### 6.5 Heater
- Enable: write 0x306D
- Disable: write 0x3066
- Confirm via status bit 13 (heater status)

### 6.6 Serial number readout
1. Write Get Serial Number command (0x3780 or 0x3682)
2. Wait tIDLE = 1 ms
3. Read 6 bytes:
   - SNB_3, SNB_2, CRC1, SNB_1, SNB_0, CRC2
4. Validate each CRC over its 16-bit word

---

## 7. Alert mode (watchdog) — limits, encoding, behavior

**Important:** Alert Mode is active **whenever the sensor operates in periodic data acquisition mode** (Alert Mode app note). There is no separate “enable” bit; starting periodic mode makes it active.

### 7.1 ALERT pin electrical behavior
- Alert Mode app note: **ALERT pin is configured push-pull.**
- Datasheet: recommends leaving ALERT **floating if unused**; warns about self-heating if loading the pin heavily; suggests driving a transistor.

### 7.2 Alert activation and deactivation logic
The alert output becomes active when **any** of the following occurs:
- T > T_high_set **or** RH > RH_high_set
- T < T_low_set **or** RH < RH_low_set

The alert output becomes inactive (deasserts) when **both** conditions are satisfied:
- T is within [T_low_clear … T_high_clear]
- RH is within [RH_low_clear … RH_high_clear]

This “set vs clear” hysteresis prevents fast oscillations near thresholds.

### 7.3 Deactivating alert mode (without leaving periodic mode)
You can deactivate alert evaluation (individually for humidity and temperature) by setting:
- **LowSet > HighSet**
(“Minimum set point higher than maximum set point”)

### 7.4 Default (power-up) alert limits
The sensor starts with default limits (“normal range”) after power-up or brown-out.

| Alert limit | RH | T | Limit value (hex) |
|---|---:|---:|---:|
| High alert set | 80 %RH | 60 °C | 0xCD33 |
| High alert clear | 79 %RH | 58 °C | 0xC92D |
| Low alert clear | 22 %RH | -9 °C | 0x3869 |
| Low alert set | 20 %RH | -10 °C | 0x3466 |

### 7.5 Reduced data format for alert limits (SHT3x)
Alert limits are stored in a reduced format:
- Standard measurement values: 16-bit T and 16-bit RH
- Alert limits use only MSBs:
  - **Humidity:** 7 bits (MSBs)
  - **Temperature:** 9 bits (MSBs)

**Resolution consequence:**
- Temperature limit resolution ≈ **0.5 °C**
- Humidity limit resolution ≈ **1 %RH**

**Bit layout (16-bit limit word):**
- Bits [15:9] = RH[15:9] (7 MSBs of RH)
- Bits [8:0]  = T[15:7] (9 MSBs of T)

### 7.6 Encoding/decoding alert limit values (from app note + Excel)
To compute a 16-bit limit value from physical units:

1. Convert physical setpoint to full 16-bit raw using inverse formulas:
   - `raw_RH = round( RH_percent * 65535 / 100 )`
   - `raw_T  = round( (T_C + 45) * 65535 / 175 )`
2. Reduce:
   - `RH7 = raw_RH >> 9`   (keep top 7 bits)
   - `T9  = raw_T  >> 7`   (keep top 9 bits)
3. Pack:
   - `limit = (RH7 << 9) | T9`
4. Compute CRC over `limit` bytes and append CRC when writing.

To approximately reconstruct the physical values from `limit`:
- `RH7 = (limit >> 9) & 0x7F`, `T9 = limit & 0x1FF`
- Effective raw values used for comparison:
  - `raw_RH_eff = RH7 << 9`
  - `raw_T_eff  = T9  << 7`
- Convert back with standard formulas.

**Note:** Because of truncation to MSBs, you do not get exact original values back; you get the quantized thresholds.

### 7.7 Alert limit read/write commands (SHT3x)
All alert limit commands are 16-bit. Each command reads or writes one packed limit word (containing both RH and T threshold for that limit).

**Read alert limits (command MSB 0xE1):**
| Limit | Command |
|---|---|
| Read High Set | **0xE11F** |
| Read High Clear | **0xE114** |
| Read Low Clear | **0xE109** |
| Read Low Set | **0xE102** |

**Write alert limits (command MSB 0x61):**
| Limit | Command |
|---|---|
| Write High Set | **0x611D** |
| Write High Clear | **0x6116** |
| Write Low Clear | **0x610B** |
| Write Low Set | **0x6100** |

**Write transaction requirements:**
- After the 16-bit command, send the 16-bit limit word (MSB, LSB) followed by its CRC byte.
- CRC is mandatory for writes.

### 7.8 Typical procedure to enable alert mode with custom limits
From the Alert Mode note:
1. Calculate limit values (pack RH7/T9 as above)
2. Start periodic mode at desired frequency (issuing periodic measurement command)
3. Alert mode is now active (it is tied to periodic mode)

### 7.9 Alert behavior on power-up and resets
Alert Mode app note:
- The ALERT pin becomes active (high) after power-up and after resets (including brown-out, general call, nRESET), regardless of periodic configuration.
- Brown-out/power-up restarts the sensor, restoring default limits (Table above) and discarding customer-defined limits.

---

## 8. Electrical + performance notes that influence firmware decisions

### 8.1 Current consumption (useful for duty-cycling decisions)
From datasheet electrical specs:
- Idle current (single-shot mode, not measuring, 25 °C): typ **0.2 µA**, max **2.0 µA**
- Idle current (single-shot, 125 °C): max **6.0 µA**
- Idle current (periodic data acquisition mode, between measurements): typ **45 µA**
- Measuring current: typ **600 µA**, max **1500 µA**
- Average current (1 measurement/sec, lowest repeatability, single shot): typ **1.7 µA**
- Heater power: **3.6–33 mW** (depends on heater supply voltage)

### 8.2 Recommended operating range / long-term behavior
Best performance range:
- Temperature: **5–60 °C**
- RH: **20–80 %RH**

Long-term exposure outside this range, especially high humidity, may temporarily offset RH reading (example given: **+3 %RH after 60 h at >80 %RH**), then it will recover slowly in normal range.

### 8.3 Sensor loading and self-heating
- Loading ALERT output can cause self-heating; recommended to use it to switch a transistor.
- High periodic sampling rates can cause self-heating (datasheet note).

---

## 9. Derived driver requirements / acceptance checklist

A robust driver should:
1. **Always** CRC-check each 16-bit data word (temp, humidity, status, serial number, alert limits).
2. For any write that includes data (e.g., alert limits), compute and send CRC; treat NACK or status bit 0 set as failure.
3. Enforce **≥1 ms** between commands, even if I²C bus is free.
4. In no-stretch mode, use either:
   - fixed wait (tMEAS max), or
   - “poll by read header” until ACK (bounded retries/timeouts)
5. Implement periodic mode as a state:
   - Start periodic (command)
   - Fetch data
   - Break to exit periodic before using other commands
6. Provide a **comm recovery** path:
   - Interface reset sequence (SCL toggles)
   - Soft reset
   - Optional general call reset (if safe on bus)
7. Provide status inspection helpers:
   - Command failure bit (bit 1)
   - Write checksum failure (bit 0)
   - Reset detected (bit 4)
   - Alert bits (15/11/10)
8. Alert mode API should include:
   - encode/decode limit word
   - read/write each limit
   - helper to disable alert via LowSet > HighSet
9. Serial number API should return 32-bit ID and expose both commands (0x3780/0x3682).

---

## 10. Optional environmental formula helpers (“Humidity at a glance”)

These are not required to talk to the sensor, but are commonly added in higher-level libraries.

### 10.1 Dew point (°C)
Given temperature `T` (°C) and RH (%):
```
RH = clamp(RH, 0, 100)
h = (log10(RH) - 2.0)/0.4343 + (17.62*T)/(243.12 + T)
Td = 243.12*h/(17.62 - h)
```

### 10.2 Absolute humidity (g/m³)
```
rho_v = 216.7 * (RH/100.0 * 6.112 * exp(17.62*T/(243.12+T)) / (273.15 + T))
```

### 10.3 Mixing ratio (g/kg)
Given barometric pressure `p` (hPa):
```
e = RH/100.0 * 6.112 * exp(17.62*T/(243.12+T))
r = 622.0 * e / (p - e)
```

### 10.4 Heat index (°C)
See the “Humidity at a glance” note for full piecewise model and coefficients (NOAA-based).

---

## 11. Manufacturing / testing notes (ambient testing note)

Not driver-critical, but relevant if your firmware test/QA compares against “known good” readings:
- After reflow soldering (>240 °C), sensors can show a **temporary RH offset** of about **-1 %RH to -2 %RH**, which recovers under ambient conditions.
- Temperature readings remain unaffected by high-temperature assembly processes.
- Production test fixtures should minimize turbulence, keep reference sensor close to DUT, ensure thermal equilibrium, and minimize cavity volume for faster settling.

---

## Appendix A — Membrane option (IP67) notes
If using SHT3x with the membrane option:
- PTFE foil membrane protects opening against water and dust to **IP67**.
- Membrane is designed to remain over sensor lifetime and withstand multiple reflow cycles.
- Due to minimal enclosed air volume, **RH response time is the same as uncovered sensor**.
- Ordering options listed in the membrane datasheet (SHT30/31/35 membrane variants).

---

## Appendix B — Implementation hint: CRC8 reference implementation (language-agnostic)
CRC for a 16-bit word sent/received as two bytes `[msb, lsb]`:

- init = 0xFF
- poly = 0x31
- for each byte:
  - crc ^= byte
  - repeat 8 times:
    - if (crc & 0x80) crc = (crc << 1) ^ poly else crc <<= 1
    - crc &= 0xFF

Compare with received CRC.

---

## Appendix C — Command quick reference (copy-paste table)

### Measurement
- Single shot (stretch): 0x2C06 / 0x2C0D / 0x2C10
- Single shot (no stretch): 0x2400 / 0x240B / 0x2416
- Periodic: 0x2032 0x2024 0x202F 0x2130 0x2126 0x212D 0x2236 0x2220 0x222B 0x2334 0x2322 0x2329 0x2737 0x2721 0x272A
- Fetch Data: 0xE000
- ART: 0x2B32
- Break: 0x3093

### Status / reset / heater
- Read status: 0xF32D
- Clear status: 0x3041
- Soft reset: 0x30A2
- General call reset: addr 0x00, byte 0x06
- Heater enable/disable: 0x306D / 0x3066

### Alert limits
- Read: 0xE11F 0xE114 0xE109 0xE102
- Write: 0x611D 0x6116 0x610B 0x6100

### Serial number
- Get SN (stretch): 0x3780
- Get SN (no-stretch): 0x3682
