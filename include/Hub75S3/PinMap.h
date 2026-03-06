#pragma once

#include <cstdint>

namespace Hub75S3 {

// Pin map: constexpr struct with all HUB75 signal assignments.
// Users define their own or use a provided preset.
struct PinMapDef {
    int8_t r1, g1, b1;    // Upper RGB data
    int8_t r2, g2, b2;    // Lower RGB data
    int8_t a, b, c, d, e; // Row address (E = -1 if unused, e.g., 1/16 scan)
    int8_t clk;            // Pixel clock
    int8_t lat;            // Latch / strobe
    int8_t oe;             // Output enable (active low)
};

// Adafruit Matrix Portal ESP32-S3 pin preset
// Reference: Adafruit Matrix Portal S3 schematic
struct MatrixPortalS3Pins {
    static constexpr PinMapDef pins = {
        .r1  = 42, .g1  = 41, .b1  = 40,
        .r2  = 38, .g2  = 39, .b2  = 37,
        .a   = 45, .b   = 36, .c   = 48, .d = 35, .e = 21,
        .clk = 2,
        .lat = 47,
        .oe  = 14
    };
};

} // namespace Hub75S3
