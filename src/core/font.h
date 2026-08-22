#pragma once
#include <stdint.h>
#include "core/canvas.h"

namespace cb {

// Ubuntu Mono Bold rasterised at 13px -- see tools/make_font.py. The cell is
// seven wide because the footer has to fit a 26-character caption and a
// 16-character annotation on one 308px line, and 42*ADV + 6 <= 308 caps the
// advance there. Glyph ink stops at column five, so the seventh column is the
// inter-character gap and the advance equals the cell.
constexpr int FONT_W   = 7;
constexpr int FONT_H   = 11;
constexpr int FONT_ADV = 7;

// x,y is the top-left of the glyph cell. scale >= 1 replicates pixels.
// Characters outside ASCII 32..126 draw nothing.
void draw_char(Canvas& c, int x, int y, char ch, uint8_t v, int scale = 1);
void draw_text(Canvas& c, int x, int y, const char* s, uint8_t v, int scale = 1);

// Rendered width in pixels, excluding the trailing inter-character gap.
int text_width(const char* s, int scale = 1);

}  // namespace cb
