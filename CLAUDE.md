# Hub75S3 - Project Context

## Overview

Template-driven, header-only HUB75 LED matrix driver for ESP32-S3 via LCD_CAM peripheral.
Design doc: `FSD.md`

## Architecture

- **Header-only** in `include/Hub75S3/` — templates require definitions in headers
- `src/Hub75S3.cpp` exists solely for PlatformIO library discovery
- **Zero runtime branching** for compile-time parameters — `if constexpr`, template specialization, constexpr functions
- **GFX-like API via CRTP** — non-virtual, inlined. Same method signatures as Adafruit_GFX but no inheritance.
- **PinMap pattern**: template parameter is a type with `static constexpr PinMapDef pins` member

## File Layout

| Header | Role |
|---|---|
| `Hub75S3.h` | Umbrella include |
| `Config.h` | Enums (`PixelFormat`, `BufferMode`, `MemoryPolicy`) + `PixelTraits` |
| `PinMap.h` | `PinMapDef` struct + `MatrixPortalS3Pins` preset |
| `Framebuffer.h` | Pixel storage, double-buffer swap, memory policy |
| `BcmPacker.h` | Framebuffer -> DMA bitplanes, constexpr gamma LUT |
| `LcdCamDriver.h` | LCD_CAM i80 + GDMA setup, DMA descriptors, refresh task |
| `Gfx.h` | Drawing primitives (CRTP mixin) |
| `Display.h` | Main template class, ties everything together + global alias |

## ESP-IDF 5.5.2 Specifics

- LCD peripheral signals: `#include <soc/lcd_periph.h>` -> `lcd_periph_i80_signals.buses[0]`
- GDMA: `gdma_new_ahb_channel()` + `gdma_config_transfer()` (old API deprecated)
- LCD_CAM clock: `lcd_clk_sel = 3` for PLL_F160M_CLK
- DMA descriptors: `dma_descriptor_t` from `hal/dma_types.h` (12-bit size/length fields, max 4095 bytes)

## Consumer Integration

Symlinked into consumer projects at `lib/Hub75S3/`. PlatformIO picks it up automatically.
No `lib_deps` entry needed when symlinked.

## Build

- Requires `-std=gnu++20` (set in `library.json` build flags)
- Framework: `arduino, espidf` (dual-framework)
- Platform: pioarduino (`platform-espressif32`, community fork)
- PIO executable: `C:/Users/arnal/.platformio/penv/Scripts/pio.exe` (not on PATH in bash)

## Primary Consumer

- **HubView**: `https://github.com/jlarnal/HubView` — local at `G:\sources\Esp32\HubView\`
- Test panel: 128x64, HUB75, 1/32 scan, 3840 Hz advertised, RGB
