#pragma once

#include <cstdint>
#include <cmath>
#include <array>
#include "Config.h"

namespace Hub75S3 {

// Compile-time gamma LUT generation
template<uint16_t GammaExp100, uint8_t BcmBitDepth>
struct GammaLut {
    static constexpr bool Enabled = (GammaExp100 != 0);
    static constexpr size_t InputSize = 256;
    static constexpr uint8_t MaxOutput = (1 << BcmBitDepth) - 1;

    std::array<uint8_t, InputSize> table;

    constexpr GammaLut() : table{} {
        if constexpr (Enabled) {
            constexpr double gamma = GammaExp100 / 100.0;
            for (size_t i = 0; i < InputSize; ++i) {
                double normalized = static_cast<double>(i) / 255.0;
                double corrected = std::pow(normalized, gamma);
                table[i] = static_cast<uint8_t>(corrected * MaxOutput + 0.5);
            }
        } else {
            // Identity: scale 8-bit input to BcmBitDepth range
            for (size_t i = 0; i < InputSize; ++i) {
                if constexpr (BcmBitDepth == 8) {
                    table[i] = static_cast<uint8_t>(i);
                } else {
                    table[i] = static_cast<uint8_t>(
                        (static_cast<uint16_t>(i) * MaxOutput) / 255
                    );
                }
            }
        }
    }

    constexpr uint8_t operator()(uint8_t input) const {
        return table[input];
    }
};

// BCM bitplane packer
// Converts framebuffer pixels into DMA-ready bitplane data.
//
// For each BCM bit (0..BcmBitDepth-1), one bitplane is generated per scan row.
// Each bitplane row contains the packed R1,G1,B1,R2,G2,B2 bits for every pixel
// in that row, ready to be clocked out to the HUB75 panel.
template<
    uint16_t PanelWidth,
    uint16_t PanelHeight,
    uint16_t ChainX,
    uint16_t ChainY,
    PixelFormat Format,
    uint8_t BcmBitDepth,
    uint16_t GammaExp100,
    uint8_t ScanRows
>
class BcmPacker {
public:
    using Pixel = typename PixelTraits<Format>::Type;
    using Traits = PixelTraits<Format>;

    static constexpr uint16_t TotalWidth  = PanelWidth * ChainX;
    static constexpr uint16_t TotalHeight = PanelHeight * ChainY;
    static constexpr uint16_t RowsPerScan = TotalHeight / ScanRows;

    // Each bitplane row: 6 bits per pixel (R1,G1,B1,R2,G2,B2), packed into bytes
    // For LCD_CAM i80, we use 8-bit or 16-bit bus width — each clock outputs one byte/word
    // with the 6 RGB bits mapped to specific data lines.
    static constexpr size_t PixelsPerRow = TotalWidth;
    static constexpr size_t BitplanesCount = BcmBitDepth;
    static constexpr size_t BytesPerBitplaneRow = PixelsPerRow; // 1 byte per pixel clock
    static constexpr size_t TotalBitplaneSize = BytesPerBitplaneRow * ScanRows * BitplanesCount;

    static constexpr GammaLut<GammaExp100, BcmBitDepth> gamma{};

    // Pack framebuffer into bitplane data for DMA
    // fb: source framebuffer (TotalWidth * TotalHeight pixels)
    // planes: destination bitplane buffer (TotalBitplaneSize bytes)
    static void pack(const Pixel* fb, uint8_t* planes) {
        for (uint8_t scan = 0; scan < ScanRows; ++scan) {
            const uint16_t rowUpper = scan;
            const uint16_t rowLower = scan + ScanRows;

            for (uint8_t bit = 0; bit < BcmBitDepth; ++bit) {
                uint8_t* dst = planes +
                    (scan * BcmBitDepth + bit) * BytesPerBitplaneRow;
                const uint8_t mask = 1 << bit;

                for (uint16_t x = 0; x < PixelsPerRow; ++x) {
                    Pixel pxUpper = fb[rowUpper * TotalWidth + x];
                    Pixel pxLower = fb[rowLower * TotalWidth + x];

                    uint8_t rU = gamma(Traits::r(pxUpper));
                    uint8_t gU = gamma(Traits::g(pxUpper));
                    uint8_t bU = gamma(Traits::b(pxUpper));
                    uint8_t rL = gamma(Traits::r(pxLower));
                    uint8_t gL = gamma(Traits::g(pxLower));
                    uint8_t bL = gamma(Traits::b(pxLower));

                    dst[x] = ((rU & mask) ? 0x01 : 0) |
                             ((gU & mask) ? 0x02 : 0) |
                             ((bU & mask) ? 0x04 : 0) |
                             ((rL & mask) ? 0x08 : 0) |
                             ((gL & mask) ? 0x10 : 0) |
                             ((bL & mask) ? 0x20 : 0);
                }
            }
        }
    }
};

} // namespace Hub75S3
