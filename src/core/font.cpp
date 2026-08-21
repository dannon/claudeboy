#include "core/font.h"
#include "core/font_data.h"

namespace cb {

void draw_char(Canvas& c, int x, int y, char ch, uint8_t v, int scale) {
    if (scale < 1) return;
    if (ch < FONT_FIRST || ch > FONT_LAST) return;
    const uint8_t* g = &FONT_DATA[(ch - FONT_FIRST) * FONT_W];
    for (int col = 0; col < FONT_W; col++) {
        const uint8_t bits = g[col];
        for (int row = 0; row < FONT_H; row++) {
            if (!(bits & (1u << row))) continue;
            if (scale == 1) {
                c.plot(x + col, y + row, v);
            } else {
                c.fill(x + col * scale, y + row * scale, scale, scale, v);
            }
        }
    }
}

void draw_text(Canvas& c, int x, int y, const char* s, uint8_t v, int scale) {
    if (!s || scale < 1) return;
    int cx = x;
    for (; *s; ++s) {
        draw_char(c, cx, y, *s, v, scale);
        cx += FONT_ADV * scale;
    }
}

int text_width(const char* s, int scale) {
    if (!s || !*s || scale < 1) return 0;
    int n = 0;
    for (const char* p = s; *p; ++p) n++;
    return (n * FONT_ADV - 1) * scale;
}

}  // namespace cb
