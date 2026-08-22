#pragma once
#include <stdint.h>
#include "core/canvas.h"

namespace cb {

// Two rasters of Ubuntu Mono Bold, cut at the two sizes actually drawn -- see
// tools/make_font.py. The big one is not the small one doubled: doubling turns
// every glyph pixel into a 2x2 block, and on the panel that reads as exactly
// what it is, a low-resolution font stretched.
//
// The small cell is seven wide because the footer has to fit a 26-character
// caption and a 16-character annotation on one 308px line, and 42*ADV + 6 <=
// 308 caps the advance there. Glyph ink stops one column short of the cell in
// both fonts, so that column is the inter-character gap and the advance
// equals the cell.
constexpr int FONT_W   = 7;
constexpr int FONT_H   = 11;
constexpr int FONT_ADV = 7;

constexpr int BIG_W   = 13;
constexpr int BIG_H   = 20;
constexpr int BIG_ADV = 13;

// x,y is the top-left of the glyph cell. scale >= 1 replicates pixels.
// Characters outside ASCII 32..126 draw nothing.
void draw_char(Canvas& c, int x, int y, char ch, uint8_t v, int scale = 1);
void draw_text(Canvas& c, int x, int y, const char* s, uint8_t v, int scale = 1);

// Rendered width in pixels, excluding the trailing inter-character gap.
int text_width(const char* s, int scale = 1);

// The same three in the big face. No scale: the whole point of a second raster
// is that nothing here is being multiplied up.
void draw_char_big(Canvas& c, int x, int y, char ch, uint8_t v);
void draw_text_big(Canvas& c, int x, int y, const char* s, uint8_t v);
int text_width_big(const char* s);

}  // namespace cb
