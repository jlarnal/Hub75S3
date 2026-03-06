#pragma once

#include <cstdint>
#include <esp_err.h>
#include "Config.h"
#include "PinMap.h"
#include "Framebuffer.h"
#include "BcmPacker.h"
#include "LcdCamDriver.h"
#include "Gfx.h"

namespace Hub75S3 {

template<
    uint16_t PanelWidth,
    uint16_t PanelHeight,
    uint16_t ChainX,
    uint16_t ChainY,
    typename PinMapT,
    PixelFormat Format,
    BufferMode Buffering,
    MemoryPolicy MemPolicy,
    uint8_t BcmBitDepth = 8,
    uint16_t GammaExp100 = 220,
    uint8_t ScanRows = PanelHeight / 2
>
class Display : public Gfx<
    Display<PanelWidth, PanelHeight, ChainX, ChainY, PinMapT,
            Format, Buffering, MemPolicy, BcmBitDepth, GammaExp100, ScanRows>,
    Format
> {
    // Compile-time validation
    static_assert(PanelWidth > 0 && PanelHeight > 0, "Panel dimensions must be positive");
    static_assert(ChainX > 0 && ChainY > 0, "Chain dimensions must be positive");
    static_assert(BcmBitDepth >= 1 && BcmBitDepth <= 8, "BCM depth must be 1-8");
    static_assert(ScanRows > 0, "ScanRows must be positive");
    static_assert(PanelHeight % 2 == 0, "PanelHeight must be even (upper/lower half)");

public:
    using Pixel = typename PixelTraits<Format>::Type;

    static constexpr uint16_t TotalWidth  = PanelWidth * ChainX;
    static constexpr uint16_t TotalHeight = PanelHeight * ChainY;

    using Fb = Framebuffer<PanelWidth, PanelHeight, ChainX, ChainY,
                           Format, Buffering, MemPolicy>;
    using Packer = BcmPacker<PanelWidth, PanelHeight, ChainX, ChainY,
                             Format, BcmBitDepth, GammaExp100, ScanRows>;
    using Driver = LcdCamDriver<PanelWidth, PanelHeight, ChainX, ChainY,
                                PinMapT, BcmBitDepth, ScanRows>;

    esp_err_t begin() {
        esp_err_t err = fb_.init();
        if (err != ESP_OK) return err;

        err = driver_.init(PinMapT::pins);
        if (err != ESP_OK) return err;

        err = driver_.startRefreshTask();
        if (err != ESP_OK) return err;

        return ESP_OK;
    }

    void swap() {
        // Pack current front buffer into bitplanes, then swap
        Packer::pack(fb_.frontBuffer(), driver_.bitplaneBuffer());
        fb_.swap();
    }

    void setBrightness(uint8_t brightness) {
        driver_.setBrightness(brightness);
    }

    Fb& framebuffer() { return fb_; }
    const Fb& framebuffer() const { return fb_; }

private:
    Fb fb_;
    Driver driver_;
};

} // namespace Hub75S3

// Convenience alias at global scope
template<
    uint16_t PanelWidth,
    uint16_t PanelHeight,
    uint16_t ChainX,
    uint16_t ChainY,
    typename PinMap,
    Hub75S3::PixelFormat Format,
    Hub75S3::BufferMode Buffering,
    Hub75S3::MemoryPolicy MemPolicy,
    uint8_t BcmBitDepth = 8,
    uint16_t GammaExp100 = 220,
    uint8_t ScanRows = PanelHeight / 2
>
using Hub75S3Display = Hub75S3::Display<
    PanelWidth, PanelHeight, ChainX, ChainY, PinMap,
    Format, Buffering, MemPolicy, BcmBitDepth, GammaExp100, ScanRows
>;
