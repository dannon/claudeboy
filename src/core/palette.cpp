#include "core/palette.h"

namespace cb {

Rgb palette_rgb(uint8_t i) {
    const uint32_t sq = (static_cast<uint32_t>(i) * i) / 255u;   // 0..255, squared falloff
    Rgb c;
    c.r = static_cast<uint8_t>((sq * 30u) / 100u);
    c.g = i;
    c.b = static_cast<uint8_t>((sq * 45u) / 100u);
    return c;
}

uint16_t palette_rgb565(uint8_t i) {
    const Rgb c = palette_rgb(i);
    return static_cast<uint16_t>(((c.r & 0xF8) << 8) | ((c.g & 0xFC) << 3) | (c.b >> 3));
}

void palette_build_rgb565_table(uint16_t out[256]) {
    for (int i = 0; i < 256; i++) out[i] = palette_rgb565(static_cast<uint8_t>(i));
}

}  // namespace cb
