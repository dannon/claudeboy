#include "core/font.h"
#include "core/font_big_data.h"
#include "core/font_data.h"

namespace cb {
namespace {

// Both faces are row-major, one word per row, bit 0 = leftmost. A word per
// COLUMN would be the tidier loop but caps a cell at eight rows.
//
// One body for both, templated only on the storage width: the small face fits
// a uint8_t per row and the big one does not, and duplicating the loop to say
// so is how the two drift apart.
template <typename Row>
void blit(Canvas& c, int x, int y, const Row* g, int w, int h, uint8_t v, int scale) {
    for (int row = 0; row < h; row++) {
        const Row bits = g[row];
        if (!bits) continue;
        for (int col = 0; col < w; col++) {
            if (!(bits & (static_cast<Row>(1) << col))) continue;
            if (scale == 1) c.plot(x + col, y + row, v);
            else            c.fill(x + col * scale, y + row * scale, scale, scale, v);
        }
    }
}

}  // namespace

void draw_char(Canvas& c, int x, int y, char ch, uint8_t v, int scale) {
    if (scale < 1) return;
    if (ch < FONT_FIRST || ch > FONT_LAST) return;
    blit(c, x, y, &FONT_DATA[(ch - FONT_FIRST) * FONT_H], FONT_W, FONT_H, v, scale);
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

void draw_char_big(Canvas& c, int x, int y, char ch, uint8_t v) {
    if (ch < FONT_FIRST || ch > FONT_LAST) return;
    blit(c, x, y, &FONT_BIG_DATA[(ch - FONT_FIRST) * BIG_H], BIG_W, BIG_H, v, 1);
}

void draw_text_big(Canvas& c, int x, int y, const char* s, uint8_t v) {
    if (!s) return;
    for (int cx = x; *s; ++s, cx += BIG_ADV) draw_char_big(c, cx, y, *s, v);
}

int text_width_big(const char* s) {
    if (!s || !*s) return 0;
    int n = 0;
    for (const char* p = s; *p; ++p) n++;
    return n * BIG_ADV - 1;
}

}  // namespace cb
