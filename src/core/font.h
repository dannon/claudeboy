#pragma once
#include <stdint.h>
#include "core/canvas.h"

namespace cb {

constexpr int FONT_W = 5;
constexpr int FONT_H = 7;
constexpr int FONT_ADV = 6;   // glyph width plus one column of gap

// x,y is the top-left of the glyph cell. scale >= 1 replicates pixels.
// Characters outside ASCII 32..126 draw nothing.
void draw_char(Canvas& c, int x, int y, char ch, uint8_t v, int scale = 1);
void draw_text(Canvas& c, int x, int y, const char* s, uint8_t v, int scale = 1);

// Rendered width in pixels, excluding the trailing inter-character gap.
int text_width(const char* s, int scale = 1);

}  // namespace cb
