# Codex Prompt: Production-Grade SHT3x I2C Driver Library (ESP32 Arduino / PlatformIO)

You are an expert embedded engineer. Implement a **production-grade, reusable SHT3x I2C driver library** for **ESP32-S2 / ESP32-S3** using **Arduino framework** under **PlatformIO**.

---

## Baseline Contracts (DO NOT RESTATE)

- **`AGENTS.md`** defines: repository layout, non-blocking architecture, injected I2C transport rules, deterministic tick behavior, managed synchronous driver pattern, transport wrapper architecture, health tracking, and DriverState model.
- **`docs/SHT3x_driver_extraction.md`** is the authoritative command map and behavior reference.
- **Do not duplicate those requirements.** Implement them and only document what is *not already covered*.

---

## Deliverables

Generate a complete library repository:
- All headers in `include/SHT3x/` (Status.h, Config.h, SHT3x.h, CommandTable.h, Version.h)
- Implementation in `src/SHT3x.cpp`
- Examples in `examples/01_basic_bringup_cli/`
- Example helpers in `examples/common/` (Log.h, BoardConfig.h, I2cTransport.h)
- README.md, CHANGELOG.md, library.json, platformio.ini
- Version generation script in `scripts/generate_version.py`

**This is a library, not an application.**

---

## Namespace

All types in `namespace SHT3x { }`.

---

## Key SHT3x-Specific Requirements (Summary)

- I2C address: 0x44 (default) or 0x45.
- All commands are 16-bit, MSB-first.
- CRC-8 validation for every 16-bit data word returned.
- CRC-8 appended for any 16-bit word written (alert limits).
- Minimum command spacing tIDLE >= 1 ms.
- Single-shot (stretch/no-stretch), periodic (0.5/1/2/4/10 mps), ART, fetch data, break.
- Status read/clear, heater enable/disable, serial number readout, alert limits read/write.
- Resets: soft reset, interface reset (SCL toggles via callback), optional general call reset.

---

## Transport Adapter Note

`I2cWriteReadFn` must support `txLen == 0` for read-only transactions.

---

## Example CLI

The bringup example should expose:
- Measurements (single-shot + periodic fetch)
- Mode/Repeatability/Rate/Clock-stretching controls
- Status/Heater/Serial/Alert limit operations
- Driver health reporting and recovery

---

**Now implement the full library.**
