#include "core/crt.h"
#include <cstddef>

namespace cb {
namespace {

uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

}  // namespace

EffectParams EffectParams::defaults() {
    EffectParams p;
    p.decay = 18;
    p.bloom_radius = 2;
    p.bloom_strength = 90;
    p.scanline_depth = 50;      // ~20%, per the spec's "start subtle"
    p.vignette_strength = 120;
    p.flicker_amount = 6;
    return p;
}

uint8_t flicker_gain(const EffectParams& p, uint32_t frame) {
    if (p.flicker_amount == 0) return 255;
    const uint32_t h = hash32(frame);
    uint32_t dip = h % (static_cast<uint32_t>(p.flicker_amount) + 1u);
    // Rare deeper dropout: roughly one frame in 512.
    if ((hash32(frame ^ 0x9E3779B9u) & 0x1FFu) == 0u) dip += p.flicker_amount * 3u;
    if (dip > 255u) dip = 255u;
    return static_cast<uint8_t>(255u - dip);
}

void apply_gain(Canvas& c, uint8_t gain) {
    if (gain == 255) return;
    uint8_t* d = c.data();
    const size_t n = static_cast<size_t>(c.width()) * c.height();
    for (size_t i = 0; i < n; i++)
        d[i] = static_cast<uint8_t>((static_cast<uint32_t>(d[i]) * gain) / 255u);
}

}  // namespace cb
