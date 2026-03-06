#pragma once

#include <cstdint>
#include <cstring>
#include <esp_heap_caps.h>
#include "Config.h"

namespace Hub75S3 {

template<
    uint16_t PanelWidth,
    uint16_t PanelHeight,
    uint16_t ChainX,
    uint16_t ChainY,
    PixelFormat Format,
    BufferMode Buffering,
    MemoryPolicy MemPolicy
>
class Framebuffer {
public:
    using Pixel = typename PixelTraits<Format>::Type;

    static constexpr uint16_t TotalWidth  = PanelWidth * ChainX;
    static constexpr uint16_t TotalHeight = PanelHeight * ChainY;
    static constexpr size_t PixelCount    = TotalWidth * TotalHeight;
    static constexpr size_t BufferSize    = PixelCount * sizeof(Pixel);
    static constexpr bool IsDouble        = (Buffering == BufferMode::Double);

    Framebuffer() = default;
    ~Framebuffer() { free(); }

    // Non-copyable, non-movable
    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    esp_err_t init() {
        uint32_t caps = mallocCaps();

        buffers_[0] = static_cast<Pixel*>(heap_caps_calloc(PixelCount, sizeof(Pixel), caps));
        if (!buffers_[0]) return ESP_ERR_NO_MEM;

        if constexpr (IsDouble) {
            buffers_[1] = static_cast<Pixel*>(heap_caps_calloc(PixelCount, sizeof(Pixel), caps));
            if (!buffers_[1]) {
                heap_caps_free(buffers_[0]);
                buffers_[0] = nullptr;
                return ESP_ERR_NO_MEM;
            }
        }

        return ESP_OK;
    }

    void free() {
        if (buffers_[0]) { heap_caps_free(buffers_[0]); buffers_[0] = nullptr; }
        if constexpr (IsDouble) {
            if (buffers_[1]) { heap_caps_free(buffers_[1]); buffers_[1] = nullptr; }
        }
    }

    // Back buffer: where the application draws
    Pixel* backBuffer() {
        if constexpr (IsDouble) {
            return buffers_[writeIndex_];
        } else {
            return buffers_[0];
        }
    }

    // Front buffer: what the DMA reads from
    const Pixel* frontBuffer() const {
        if constexpr (IsDouble) {
            return buffers_[writeIndex_ ^ 1];
        } else {
            return buffers_[0];
        }
    }

    // Swap front and back buffers (double-buffered only)
    void swap() {
        if constexpr (IsDouble) {
            writeIndex_ ^= 1;
        }
    }

    // Direct pixel access on back buffer
    void setPixel(uint16_t x, uint16_t y, Pixel color) {
        if (x < TotalWidth && y < TotalHeight) {
            backBuffer()[y * TotalWidth + x] = color;
        }
    }

    Pixel getPixel(uint16_t x, uint16_t y) const {
        if (x < TotalWidth && y < TotalHeight) {
            return const_cast<Framebuffer*>(this)->backBuffer()[y * TotalWidth + x];
        }
        return Pixel{};
    }

    void clear(Pixel color = Pixel{}) {
        Pixel* buf = backBuffer();
        if (color == Pixel{}) {
            memset(buf, 0, BufferSize);
        } else {
            for (size_t i = 0; i < PixelCount; ++i) {
                buf[i] = color;
            }
        }
    }

private:
    static constexpr uint32_t mallocCaps() {
        if constexpr (MemPolicy == MemoryPolicy::PsramFramebuf) {
            return MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
        } else {
            return MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
        }
    }

    Pixel* buffers_[IsDouble ? 2 : 1] = {};
    uint8_t writeIndex_ = 0;
};

} // namespace Hub75S3
