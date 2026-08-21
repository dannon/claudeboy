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

// Applies bloom, scanlines, vignette and flicker in that fixed order.
// Decay and drawing happen before this call, not inside it.
void post_process(Canvas& c, const EffectParams& p, uint32_t frame,
                  uint8_t* ring, size_t ring_bytes);

}  // namespace cb
