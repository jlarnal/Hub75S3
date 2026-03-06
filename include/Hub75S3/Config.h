#pragma once

#include <cstdint>
#include <cstddef>

namespace Hub75S3 {

enum class PixelFormat : uint8_t {
    RGB888,
    RGB565
};

enum class BufferMode : uint8_t {
    Single,
    Double
};

enum class MemoryPolicy : uint8_t {
    PsramFramebuf,
    InternalOnly
};

// Pixel type traits — resolved at compile time
template<PixelFormat F>
struct PixelTraits;

template<>
struct PixelTraits<PixelFormat::RGB888> {
    using Type = uint32_t; // 0x00RRGGBB — upper byte unused
    static constexpr size_t BytesPerPixel = 3;
    static constexpr uint8_t r(Type c) { return (c >> 16) & 0xFF; }
    static constexpr uint8_t g(Type c) { return (c >> 8)  & 0xFF; }
    static constexpr uint8_t b(Type c) { return  c        & 0xFF; }
    static constexpr Type from_rgb(uint8_t r, uint8_t g, uint8_t b) {
        return (static_cast<uint32_t>(r) << 16) |
               (static_cast<uint32_t>(g) << 8)  |
                static_cast<uint32_t>(b);
    }
};

template<>
struct PixelTraits<PixelFormat::RGB565> {
    using Type = uint16_t;
    static constexpr size_t BytesPerPixel = 2;
    static constexpr uint8_t r(Type c) { return (c >> 11) << 3; }
    static constexpr uint8_t g(Type c) { return ((c >> 5) & 0x3F) << 2; }
    static constexpr uint8_t b(Type c) { return (c & 0x1F) << 3; }
    static constexpr Type from_rgb(uint8_t r, uint8_t g, uint8_t b) {
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
};

} // namespace Hub75S3
