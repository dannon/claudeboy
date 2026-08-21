#pragma once
#include <stdint.h>

namespace cb {

struct Rgb { uint8_t r, g, b; };

// Maps 0..255 intensity onto a P1-ish phosphor ramp: green rises linearly,
// red and blue follow a squared curve so only the brightest pixels wash toward
// white, the way a real tube blooms.
Rgb palette_rgb(uint8_t intensity);
uint16_t palette_rgb565(uint8_t intensity);

// Fills a caller-provided 256-entry table with the RGB565 value for each
// intensity, so hot loops can look up instead of recomputing. Caller owns
// the storage; this never allocates.
void palette_build_rgb565_table(uint16_t out[256]);

}  // namespace cb
