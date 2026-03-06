# Hub75S3 - Functional Specification Document

## Overview

Template-driven, header-only HUB75 driver for ESP32-S3 via the LCD_CAM peripheral.
Zero runtime branching for compile-time-known parameters. All configuration is resolved
at compile time through C++ templates, `if constexpr`, and constexpr functions.

## Hardware Context

- **Target MCU**: ESP32-S3 (specifically ESP32-S3-WROOM-2 N32R16V on Adafruit Matrix Portal S3)
- **Peripheral**: LCD_CAM in Intel 8080 (i80) parallel mode with GDMA
- **Why not I2S**: ESP32-S3 removed parallel output from I2S. LCD_CAM is the only option.
- **LCD_CAM limitation**: Transaction-based DMA (not free-running circular like I2S was).
  Requires re-triggering from EOF ISR callback.

## Template Parameters

| Parameter      | Type         | Default           | Description                                   |
|----------------|--------------|-------------------|-----------------------------------------------|
| `PanelWidth`   | `uint16_t`   | (required)        | Single panel pixel width                      |
| `PanelHeight`  | `uint16_t`   | (required)        | Single panel pixel height                     |
| `ChainX`       | `uint16_t`   | (required)        | Number of panels horizontally                 |
| `ChainY`       | `uint16_t`   | (required)        | Number of panels vertically                   |
| `PinMap`       | `typename`   | (required)        | Constexpr struct with HUB75 pin assignments   |
| `Format`       | `PixelFormat` | (required)       | RGB888 or RGB565                              |
| `Buffering`    | `BufferMode` | (required)        | Single or Double                              |
| `MemPolicy`    | `MemoryPolicy`| (required)       | PsramFramebuf or InternalOnly                 |
| `BcmBitDepth`  | `uint8_t`    | `8`               | Bits per color channel for BCM                |
| `GammaExp100`  | `uint16_t`   | `220`             | Gamma exponent * 100. 0 = disabled.           |
| `ScanRows`     | `uint8_t`    | `PanelHeight / 2` | Scan row count. Overridable for odd panels.   |

### Design principles

- Single panel: `<W, H, 1, 1>` — all multi-panel logic eliminated at compile time.
- Horizontal chain: `<W, H, N, 1>` — serpentine detection only when `ChainY > 1`.
- All coordinate mapping (`(x, y) -> buffer offset`) is constexpr/`if constexpr`.
- `GammaExp100 == 0` eliminates LUT generation and lookup entirely.
- `BufferMode::Single` eliminates back-buffer and swap logic entirely.

## Architecture

### File Structure

```
Hub75S3/
  library.json                 # PlatformIO library manifest
  include/
    Hub75S3/
      Hub75S3.h                # Umbrella include
      Config.h                 # Enums: PixelFormat, BufferMode, MemoryPolicy
      PinMap.h                 # PinMap concept + MatrixPortalS3 preset
      Display.h                # Hub75S3Display class template (main entry point)
      Framebuffer.h            # Pixel storage, coordinate mapping
      BcmPacker.h              # Framebuffer -> DMA bitplane conversion, gamma LUT
      LcdCamDriver.h           # LCD_CAM + GDMA init, DMA descriptors, refresh task
      Gfx.h                    # GFX-like drawing primitives, font support
  src/
    (minimal .cpp for PIO library discovery)
```

### Header-only

Templates require definitions in headers. The library is almost entirely header-only.
A minimal `.cpp` file exists solely for PlatformIO to detect the library.

### Dependency Flow

```
Hub75S3.h -> Display.h -> Framebuffer.h   (pixel storage, coordinate mapping)
                       -> BcmPacker.h      (framebuffer -> DMA bitplanes)
                       -> LcdCamDriver.h   (hardware init, DMA, refresh task)
                       -> Gfx.h            (drawing API, mixed into Display)
             Config.h  <-- (used by all)
             PinMap.h  <-- (used by LcdCamDriver)
```

## Component Details

### Pin Mapping (`PinMap.h`)

- Constexpr struct defining all HUB75 signals: R1, G1, B1, R2, G2, B2, A, B, C, D, E, CLK, LAT, OE
- `MatrixPortalS3Pins` provided as a preset for Adafruit Matrix Portal S3
- Any board can be supported by defining a custom constexpr struct — zero runtime cost

### Framebuffer (`Framebuffer.h`)

- Pixel format determined by `PixelFormat` template parameter (RGB888 or RGB565)
- Double buffering: two framebuffers allocated, `swap()` atomically switches front/back
- Single buffering: one framebuffer, no swap logic compiled
- Memory placement controlled by `MemoryPolicy`:
  - `PsramFramebuf`: framebuffer in PSRAM, DMA buffers in internal SRAM
  - `InternalOnly`: everything in internal SRAM (for S3 boards without PSRAM)
- Coordinate mapping from `(x, y)` to buffer offset handles chain topology at compile time

### BCM Bitplane Packer (`BcmPacker.h`)

- Converts RGB framebuffer into DMA-ready bitplanes for Binary Code Modulation
- Templated on `BcmBitDepth` and `Format` — bit extraction loops are unrolled at compile time
- Gamma correction via constexpr LUT generated from `GammaExp100`
  - When `GammaExp100 == 0`, gamma path is eliminated entirely (`if constexpr`)
- This is the performance-critical hot path

### LCD_CAM Driver (`LcdCamDriver.h`)

- Configures LCD_CAM peripheral in i80 parallel mode
- Sets up GDMA channel with linked DMA descriptor chains
- EOF ISR callback re-triggers the next DMA transaction (chaining bitplanes)
- Library-managed FreeRTOS refresh task:
  - Continuously feeds DMA transactions
  - Core affinity and task priority configurable
- `begin()` returns `esp_err_t` for hardware init failures (DMA channel allocation, memory, LCD_CAM)

### GFX-like API (`Gfx.h`)

- Non-virtual, inlined drawing methods — no Adafruit_GFX inheritance
- Methods: `drawPixel()`, `fillRect()`, `drawFastHLine()`, `drawFastVLine()`,
  `fillScreen()`, `drawChar()`, text rendering
- Supports Adafruit GFX-compatible font struct format (drop-in font files)
- All methods are templates or inline — zero virtual dispatch overhead

### Brightness Control

- `setBrightness(uint8_t)` — global brightness via OE pulse width modulation
- Adjusts OE timing during BCM scan

### Error Handling

- **Compile-time**: `static_assert` for invalid configurations (impossible scan rates,
  unsupported combinations)
- **Runtime**: `esp_err_t` return from `begin()` for hardware resource failures
- No exceptions

## Usage Example

### platformio.ini (consumer project)

```ini
lib_deps = symlink:///G/sources/Esp32/Hub75S3
```

### Application code

```cpp
#include <Hub75S3/Hub75S3.h>

using Display = Hub75S3Display<
    128, 64,                             // panel size
    1, 1,                                // single panel
    Hub75S3::MatrixPortalS3Pins,         // pin map preset
    Hub75S3::PixelFormat::RGB888,
    Hub75S3::BufferMode::Double,
    Hub75S3::MemoryPolicy::PsramFramebuf,
    8,                                   // 8-bit BCM depth
    220                                  // gamma 2.2 (0 to disable)
>;

Display display;

void setup() {
    esp_err_t err = display.begin();
    if (err != ESP_OK) {
        // handle init failure
        return;
    }
    display.setBrightness(128);
    display.fillScreen(0x000000);
    display.drawPixel(10, 5, 0xFF0000);
    display.swap();  // flip buffers (double-buffered mode)
}

void loop() {
    // draw, swap, repeat
}
```

## Test Configuration

- Panel: 128x64, HUB75, 1/32 scan, 3840Hz advertised refresh, RGB
- Board: Adafruit Matrix Portal ESP32-S3 with N32R16V module (32MB flash, 16MB PSRAM)
- Single panel: `<128, 64, 1, 1>`
