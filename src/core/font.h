#pragma once
#include <stdint.h>
#include "core/canvas.h"

namespace cb {

// Spleen, at the two sizes actually drawn -- see tools/make_font.py. Two
// separate faces rather than one scaled: doubling turns every glyph pixel into
// a 2x2 block, which on the panel reads as exactly what it is.
//
// Both are drawn bead by bead at these sizes rather than rasterised down from
// an outline font, which is what the two before them were. A downscaled TTF
// has to threshold antialiased edges, and the glyphs come out lopsided and
// with their counters closing under bloom -- that is what read as blurry, and
// it was the letterforms rather than the CRT effects.
//
// The small advance has a ceiling of seven: the footer draws a 26-character
// caption and a 16-character annotation on one 308px line, so 42*ADV + 6 <=
// 308. Six clears it comfortably. The inter-character gap is the designer's,
// built into where each glyph sits in its cell, so the advance is the whole
// cell and text_width() subtracts only the trailing one.
constexpr int FONT_W   = 6;
constexpr int FONT_H   = 12;
constexpr int FONT_ADV = 6;

constexpr int BIG_W   = 12;
constexpr int BIG_H   = 24;
constexpr int BIG_ADV = 12;

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
