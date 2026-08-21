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

size_t bloom_ring_bytes(int radius, int width) {
    if (radius < 0) radius = 0;
    return static_cast<size_t>(2 * radius + 1) * static_cast<size_t>(width);
}

void apply_bloom(Canvas& c, const EffectParams& p, uint8_t* ring, size_t ring_bytes) {
    const int r = p.bloom_radius;
    if (r <= 0 || p.bloom_strength == 0 || !ring) return;

    const int w = c.width(), h = c.height();
    const int rows = 2 * r + 1;
    if (ring_bytes < bloom_ring_bytes(r, w)) return;

    const int taps = rows;
    uint8_t* buf = c.data();

    // taps is only ever 1, 3, 5, 7 or 9 (radius 0..4), so a fixed-point
    // reciprocal multiply replaces the runtime divide -- the Xtensa LX6
    // has no hardware divider and this loop runs ~153,600 times/frame.
    const uint32_t recip = (1u << 16) / static_cast<uint32_t>(taps);

    // Horizontally blur source row `sy` into ring slot `sy % rows`.
    auto load_row = [&](int sy) {
        uint8_t* dst = ring + static_cast<size_t>(sy % rows) * w;
        const uint8_t* src = buf + static_cast<size_t>(sy) * w;
        for (int x = 0; x < w; x++) {
            uint32_t acc = 0;
            for (int k = -r; k <= r; k++) {
                int sx = x + k;
                if (sx < 0) sx = 0;
                if (sx >= w) sx = w - 1;
                acc += src[sx];
            }
            dst[x] = static_cast<uint8_t>(((acc * recip) + (1u << 15)) >> 16);
        }
    };

    for (int y = 0; y < h + r; y++) {
        if (y < h) load_row(y);

        const int out_y = y - r;          // lags behind so its window is full
        if (out_y < 0) continue;

        uint8_t* out = buf + static_cast<size_t>(out_y) * w;
        for (int x = 0; x < w; x++) {
            uint32_t acc = 0;
            for (int k = -r; k <= r; k++) {
                int sy = out_y + k;
                if (sy < 0) sy = 0;
                if (sy >= h) sy = h - 1;
                acc += ring[static_cast<size_t>(sy % rows) * w + x];
            }
            const uint32_t blur = ((acc * recip) + (1u << 15)) >> 16;
            const uint32_t add = (blur * p.bloom_strength) / 255u;
            const uint32_t v = out[x] + add;
            out[x] = static_cast<uint8_t>(v > 255u ? 255u : v);
        }
    }
}

}  // namespace cb
