# SHT3x ESP-IDF Portability Status

Last audited: 2026-03-01

## Current Reality
- Primary runtime remains PlatformIO + Arduino.
- Transport is callback-based (`Config.i2cWrite`, `Config.i2cWriteRead`).
- Optional reset hooks are supported (`busReset`, `hardReset`).
- Optional timing hooks are available:
  - `Config.nowMs`
  - `Config.nowUs`
  - `Config.cooperativeYield`
  - `Config.timeUser`
- Core logic uses internal wrappers (`_nowMs`, `_nowUs`, `_cooperativeYield`).
- Arduino calls are only fallback paths inside wrappers:
  - `millis()` fallback for `_nowMs`
  - `micros()` fallback for `_nowUs`
  - `yield()` fallback for `_cooperativeYield`

## ESP-IDF Adapter Requirements
To run under pure ESP-IDF, provide:
1. I2C write callback.
2. I2C write-read callback.
3. Optional bus reset and hard reset callbacks.
4. Optional timing callbacks (`nowMs`, `nowUs`, `cooperativeYield`).

## Minimal Adapter Pattern
```cpp
static uint32_t idfNowMs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

static uint32_t idfNowUs(void*) {
  return static_cast<uint32_t>(esp_timer_get_time());
}

static void idfYield(void*) {
  taskYIELD();
}

SHT3x::Config cfg{};
cfg.i2cWrite = myI2cWrite;
cfg.i2cWriteRead = myI2cWriteRead;
cfg.nowMs = idfNowMs;
cfg.nowUs = idfNowUs;
cfg.cooperativeYield = idfYield;
```

## Porting Notes
- Keep `tick(nowMs)` scheduled regularly.
- Preserve transport error mapping for NACK/timeout/bus errors (important for recovery logic).
- Keep command spacing and periodic-fetch timing behavior unchanged.

## Verification Checklist
- `python tools/check_core_timing_guard.py` passes.
- Native tests pass (`pio test -e native`).
- Example build passes (`pio run -e ex_bringup_s3`).
- No new direct Arduino timing calls are added outside wrapper fallback.
