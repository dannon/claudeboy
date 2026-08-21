#include "core/crt.h"
#include "core/fastdiv.h"
#include <cstddef>
#include <cstring>

namespace cb {
namespace {

uint32_t hash32(uint32_t x) {
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// The three pointwise stages, one row at a time. Both the whole-frame
// functions and post_process_stream() go through these, so each formula
// exists exactly once -- a second copy is how the two paths would silently
// drift apart.

void scanline_row(uint8_t* row, int w, int y, const EffectParams& p) {
    if (p.scanline_depth == 0 || (y & 1) == 0) return;
    const uint32_t keep = 255u - p.scanline_depth;
    for (int x = 0; x < w; x++)
        row[x] = static_cast<uint8_t>(div255(static_cast<uint32_t>(row[x]) * keep));
}

void vignette_row(uint8_t* row, int w, int y, int h, const EffectParams& p) {
    if (p.vignette_strength == 0) return;
    const float cx = (w - 1) * 0.5f, cy = (h - 1) * 0.5f;
    const float inv = 1.0f / (cx * cx + cy * cy);
    const float dy = y - cy;
    for (int x = 0; x < w; x++) {
        const float dx = x - cx;
        float t = (dx * dx + dy * dy) * inv;      // 0 at centre, 1 at corners
        // Flat through the middle, falling off toward the edges.
        t = t * t;
        const uint32_t cut = static_cast<uint32_t>(t * p.vignette_strength);
        const uint32_t keep = cut >= 255u ? 0u : 255u - cut;
        row[x] = static_cast<uint8_t>(div255(static_cast<uint32_t>(row[x]) * keep));
    }
}

void gain_row(uint8_t* row, int w, uint8_t gain) {
    if (gain == 255) return;
    for (int x = 0; x < w; x++)
        row[x] = static_cast<uint8_t>(div255(static_cast<uint32_t>(row[x]) * gain));
}

}  // namespace

EffectParams EffectParams::defaults() {
    EffectParams p;
    p.decay = 18;
    p.bloom_radius = 2;
    p.bloom_strength = 240;
    p.scanline_depth = 84;      // strong end of sweep, chosen from contact sheets
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
    const int w = c.width(), h = c.height();
    for (int y = 0; y < h; y++)
        gain_row(c.data() + static_cast<size_t>(y) * w, w, gain);
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

void apply_scanlines(Canvas& c, const EffectParams& p) {
    if (p.scanline_depth == 0) return;
    const int w = c.width(), h = c.height();
    for (int y = 1; y < h; y += 2)
        scanline_row(c.data() + static_cast<size_t>(y) * w, w, y, p);
}

void apply_vignette(Canvas& c, const EffectParams& p) {
    if (p.vignette_strength == 0) return;
    const int w = c.width(), h = c.height();
    for (int y = 0; y < h; y++)
        vignette_row(c.data() + static_cast<size_t>(y) * w, w, y, h, p);
}

void post_process(Canvas& c, const EffectParams& p, uint32_t frame,
                  uint8_t* ring, size_t ring_bytes) {
    apply_bloom(c, p, ring, ring_bytes);
    apply_scanlines(c, p);
    apply_vignette(c, p);
    apply_gain(c, flicker_gain(p, frame));
}

size_t stream_row_bytes(int width) {
    return width > 0 ? static_cast<size_t>(width) : 0u;
}

void post_process_stream(const Canvas& src, const EffectParams& p, uint32_t frame,
                         uint8_t* ring, size_t ring_bytes,
                         uint8_t* out_row, RowSink sink, void* ctx) {
    const int w = src.width(), h = src.height();
    if (!sink || !out_row || !src.data()) return;

    const uint8_t gain = flicker_gain(p, frame);
    const int r = p.bloom_radius;
    const bool bloom_on = r > 0 && p.bloom_strength != 0 && ring &&
                          ring_bytes >= bloom_ring_bytes(r, w);

    const uint8_t* buf = src.data();

    // Emit one finished row: the pointwise stages, in post_process's order.
    auto finish = [&](int y) {
        scanline_row(out_row, w, y, p);
        vignette_row(out_row, w, y, h, p);
        gain_row(out_row, w, gain);
        sink(ctx, y, out_row, w);
    };

    if (!bloom_on) {
        for (int y = 0; y < h; y++) {
            memcpy(out_row, buf + static_cast<size_t>(y) * w, static_cast<size_t>(w));
            finish(y);
        }
        return;
    }

    const int rows = 2 * r + 1;
    const uint32_t recip = (1u << 16) / static_cast<uint32_t>(rows);

    auto load_row = [&](int sy) {
        uint8_t* dst = ring + static_cast<size_t>(sy % rows) * w;
        const uint8_t* s = buf + static_cast<size_t>(sy) * w;
        for (int x = 0; x < w; x++) {
            uint32_t acc = 0;
            for (int k = -r; k <= r; k++) {
                int sx = x + k;
                if (sx < 0) sx = 0;
                if (sx >= w) sx = w - 1;
                acc += s[sx];
            }
            dst[x] = static_cast<uint8_t>(((acc * recip) + (1u << 15)) >> 16);
        }
    };

    // Same walk as apply_bloom: load row y, emit row y - r once its vertical
    // window is full. load_row reads src only, never out_row.
    for (int y = 0; y < h + r; y++) {
        if (y < h) load_row(y);

        const int out_y = y - r;          // lags behind so its window is full
        if (out_y < 0) continue;

        const uint8_t* srow = buf + static_cast<size_t>(out_y) * w;
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
            const uint32_t v = srow[x] + add;
            out_row[x] = static_cast<uint8_t>(v > 255u ? 255u : v);
        }
        finish(out_y);
    }
}

}  // namespace cb
