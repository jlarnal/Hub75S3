#pragma once

#include <cstdint>
#include <cstring>
#include <algorithm>
#include "Config.h"

namespace Hub75S3 {

// GFX-like drawing primitives — non-virtual, inlined.
// Operates on a Framebuffer via CRTP (Curiously Recurring Template Pattern):
// the derived Display class passes itself as Derived.
template<typename Derived, PixelFormat Format>
class Gfx {
public:
    using Pixel = typename PixelTraits<Format>::Type;

    void drawPixel(int16_t x, int16_t y, Pixel color) {
        self().framebuffer().setPixel(x, y, color);
    }

    void fillScreen(Pixel color) {
        self().framebuffer().clear(color);
    }

    void drawFastHLine(int16_t x, int16_t y, int16_t w, Pixel color) {
        if (y < 0 || y >= height()) return;
        if (x < 0) { w += x; x = 0; }
        if (x + w > width()) { w = width() - x; }
        if (w <= 0) return;

        Pixel* buf = self().framebuffer().backBuffer();
        Pixel* row = buf + y * width() + x;
        for (int16_t i = 0; i < w; ++i) {
            row[i] = color;
        }
    }

    void drawFastVLine(int16_t x, int16_t y, int16_t h, Pixel color) {
        if (x < 0 || x >= width()) return;
        if (y < 0) { h += y; y = 0; }
        if (y + h > height()) { h = height() - y; }
        if (h <= 0) return;

        Pixel* buf = self().framebuffer().backBuffer();
        for (int16_t i = 0; i < h; ++i) {
            buf[(y + i) * width() + x] = color;
        }
    }

    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, Pixel color) {
        // Clip
        if (x < 0) { w += x; x = 0; }
        if (y < 0) { h += y; y = 0; }
        if (x + w > width())  { w = width() - x; }
        if (y + h > height()) { h = height() - y; }
        if (w <= 0 || h <= 0) return;

        Pixel* buf = self().framebuffer().backBuffer();
        for (int16_t row = y; row < y + h; ++row) {
            Pixel* p = buf + row * width() + x;
            for (int16_t i = 0; i < w; ++i) {
                p[i] = color;
            }
        }
    }

    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, Pixel color) {
        // Bresenham
        int16_t dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
        int16_t dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
        int16_t err = dx + dy;

        for (;;) {
            drawPixel(x0, y0, color);
            if (x0 == x1 && y0 == y1) break;
            int16_t e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }

    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, Pixel color) {
        drawFastHLine(x, y, w, color);
        drawFastHLine(x, y + h - 1, w, color);
        drawFastVLine(x, y, h, color);
        drawFastVLine(x + w - 1, y, h, color);
    }

    // TODO: drawChar(), print(), text rendering with Adafruit GFX-compatible fonts

    int16_t width()  const { return self().TotalWidth; }
    int16_t height() const { return self().TotalHeight; }

private:
    Derived& self() { return static_cast<Derived&>(*this); }
    const Derived& self() const { return static_cast<const Derived&>(*this); }
};

} // namespace Hub75S3
