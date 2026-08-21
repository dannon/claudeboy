#pragma once
#include <stdint.h>
#include <stddef.h>
#include "core/canvas.h"

namespace cb {

// Every tunable in one place so the contact-sheet tool can sweep them.
struct EffectParams {
    uint8_t decay;              // per-frame phosphor falloff
    uint8_t bloom_radius;       // 0..4; 0 disables bloom
    uint8_t bloom_strength;     // how much blurred light is added back
    uint8_t scanline_depth;     // how far odd rows are darkened
    uint8_t vignette_strength;  // corner darkening
    uint8_t flicker_amount;     // peak per-frame dimming

    static EffectParams defaults();
};

// Deterministic in `frame`, so tests and golden images are stable.
// Returns a 0..255 multiplier; 255 means untouched. Only ever dims.
uint8_t flicker_gain(const EffectParams& p, uint32_t frame);

void apply_gain(Canvas& c, uint8_t gain);

// Bytes of scratch apply_bloom() needs: (2*radius+1) rows of `width`.
size_t bloom_ring_bytes(int radius, int width);

// Adds a blurred copy of the image back over itself, brightening only.
// Uses a ring of horizontally-blurred rows, never a second framebuffer.
// If ring_bytes is too small the call does nothing.
void apply_bloom(Canvas& c, const EffectParams& p, uint8_t* ring, size_t ring_bytes);

void apply_scanlines(Canvas& c, const EffectParams& p);
void apply_vignette(Canvas& c, const EffectParams& p);

// Receives one finished row at a time. `row` is only valid for the duration of
// the call -- copy or push it before returning.
using RowSink = void (*)(void* ctx, int y, const uint8_t* row, int w);

// Bytes `post_process_stream` needs for its single scratch output row.
size_t stream_row_bytes(int width);

// The whole post-processing pipeline as a row stream: bloom, scanlines,
// vignette and flicker gain, in that fixed order, applied per row.
//
// Equivalent to post_process() over a copy of `src`, and pinned as such by
// test_crt_stream. The point is what it does NOT need: bloom reads `src`
// directly and only ever looks ahead of the row it emits, so there is no second
// framebuffer anywhere -- just the ring and one scratch row. On the device that
// is 76,800 bytes that do not have to exist, which is the difference between
// TLS fitting and not.
//
// `src` is never written. Rows arrive in order, y = 0..height-1.
void post_process_stream(const Canvas& src, const EffectParams& p, uint32_t frame,
                         uint8_t* ring, size_t ring_bytes,
                         uint8_t* out_row, RowSink sink, void* ctx);

// Applies bloom, scanlines, vignette and flicker in that fixed order.
// Decay and drawing happen before this call, not inside it.
void post_process(Canvas& c, const EffectParams& p, uint32_t frame,
                  uint8_t* ring, size_t ring_bytes);

}  // namespace cb
