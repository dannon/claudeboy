#include <unity.h>
#include <string.h>
#include "core/canvas.h"
#include "core/types.h"

static uint8_t buf[16 * 8];
static cb::Canvas mk() { memset(buf, 0, sizeof buf); return cb::Canvas(buf, 16, 8); }

void test_plot_and_read(void) {
    cb::Canvas c = mk();
    c.plot(3, 4, 200);
    TEST_ASSERT_EQUAL_UINT8(200, c.at(3, 4));
    TEST_ASSERT_EQUAL_UINT8(0, c.at(4, 4));
}

void test_plot_is_max_blend(void) {
    cb::Canvas c = mk();
    c.plot(1, 1, 100);
    c.plot(1, 1, 40);   // dimmer must not overwrite
    TEST_ASSERT_EQUAL_UINT8(100, c.at(1, 1));
    c.plot(1, 1, 220);
    TEST_ASSERT_EQUAL_UINT8(220, c.at(1, 1));
}

void test_out_of_bounds_is_ignored(void) {
    cb::Canvas c = mk();
    c.plot(-1, 0, 255);
    c.plot(0, -1, 255);
    c.plot(16, 0, 255);
    c.plot(0, 8, 255);
    for (int i = 0; i < 16 * 8; i++) TEST_ASSERT_EQUAL_UINT8(0, buf[i]);
}

void test_decay_saturates_at_zero(void) {
    cb::Canvas c = mk();
    c.plot(2, 2, 30);
    c.decay(10);
    TEST_ASSERT_EQUAL_UINT8(20, c.at(2, 2));
    c.decay(100);
    TEST_ASSERT_EQUAL_UINT8(0, c.at(2, 2));
}

void test_hline_and_vline(void) {
    cb::Canvas c = mk();
    c.hline(2, 3, 4, 90);
    TEST_ASSERT_EQUAL_UINT8(0,  c.at(1, 3));
    TEST_ASSERT_EQUAL_UINT8(90, c.at(2, 3));
    TEST_ASSERT_EQUAL_UINT8(90, c.at(5, 3));
    TEST_ASSERT_EQUAL_UINT8(0,  c.at(6, 3));
    c.vline(7, 1, 3, 80);
    TEST_ASSERT_EQUAL_UINT8(80, c.at(7, 1));
    TEST_ASSERT_EQUAL_UINT8(80, c.at(7, 3));
    TEST_ASSERT_EQUAL_UINT8(0,  c.at(7, 4));
}

void test_rect_is_outline_only(void) {
    cb::Canvas c = mk();
    c.rect(1, 1, 5, 4, 70);
    TEST_ASSERT_EQUAL_UINT8(70, c.at(1, 1));
    TEST_ASSERT_EQUAL_UINT8(70, c.at(5, 4));
    TEST_ASSERT_EQUAL_UINT8(0,  c.at(3, 2));   // interior stays empty
}

void test_fill_is_solid_and_clipped(void) {
    cb::Canvas c = mk();
    c.fill(14, 6, 10, 10, 60);   // runs off both edges
    TEST_ASSERT_EQUAL_UINT8(60, c.at(15, 7));
    TEST_ASSERT_EQUAL_UINT8(60, c.at(14, 6));
    TEST_ASSERT_EQUAL_UINT8(0,  c.at(13, 6));
}

void test_clear(void) {
    cb::Canvas c = mk();
    c.fill(0, 0, 16, 8, 255);
    c.clear(0);
    TEST_ASSERT_EQUAL_UINT8(0, c.at(8, 4));
}

void test_at_returns_zero_out_of_bounds(void) {
    cb::Canvas c = mk();
    c.plot(3, 2, 150);
    TEST_ASSERT_EQUAL_UINT8(150, c.at(3, 2));
    // Out-of-bounds coords must all return 0
    TEST_ASSERT_EQUAL_UINT8(0, c.at(-1, 0));
    TEST_ASSERT_EQUAL_UINT8(0, c.at(0, -1));
    TEST_ASSERT_EQUAL_UINT8(0, c.at(16, 0));
    TEST_ASSERT_EQUAL_UINT8(0, c.at(0, 8));
    TEST_ASSERT_EQUAL_UINT8(0, c.at(-5, -5));
}

void test_degenerate_canvas_is_inert(void) {
    // Fill buffer with sentinel to detect any accidental writes
    memset(buf, 0xAB, sizeof buf);
    // Construct with negative width; must degrade to 0x0
    cb::Canvas bad(buf, -4, 8);
    TEST_ASSERT_EQUAL_INT(0, bad.width());
    TEST_ASSERT_EQUAL_INT(0, bad.height());
    TEST_ASSERT_EQUAL_UINT8(0, bad.at(0, 0));
    // All operations must be no-ops on a 0x0 canvas
    bad.clear(255);
    bad.decay(10);
    bad.plot(0, 0, 255);
    bad.fill(0, 0, 100, 100, 255);
    bad.rect(0, 0, 50, 50, 255);
    // Buffer must be completely untouched
    for (int i = 0; i < 16 * 8; i++) {
        TEST_ASSERT_EQUAL_UINT8(0xAB, buf[i]);
    }
}

#include "core/font.h"
#include "core/font_big_data.h"
#include "core/font_data.h"

static uint8_t wide[64 * 16];
static cb::Canvas mkwide() { memset(wide, 0, sizeof wide); return cb::Canvas(wide, 64, 16); }

void test_text_width_advances_one_cell_per_char(void) {
    TEST_ASSERT_EQUAL_INT(0, cb::text_width("", 1));
    // The last glyph carries no trailing gap, so a run of n is n cells wide
    // less the one gap column at the end.
    TEST_ASSERT_EQUAL_INT(cb::FONT_ADV - 1, cb::text_width("A", 1));
    TEST_ASSERT_EQUAL_INT(2 * cb::FONT_ADV - 1, cb::text_width("AB", 1));
    TEST_ASSERT_EQUAL_INT(2 * (2 * cb::FONT_ADV - 1), cb::text_width("AB", 2));
}

void test_draw_char_marks_pixels(void) {
    cb::Canvas c = mkwide();
    cb::draw_char(c, 0, 0, 'A', 255, 1);
    int lit = 0;
    for (int y = 0; y < cb::FONT_H; y++)
        for (int x = 0; x < cb::FONT_W; x++)
            if (c.at(x, y)) lit++;
    TEST_ASSERT_TRUE(lit > 4);       // 'A' is not blank
    // and nothing of it reaches the next cell along
    for (int y = 0; y < cb::FONT_H; y++)
        TEST_ASSERT_EQUAL_UINT8(0, c.at(cb::FONT_ADV, y));
}

void test_space_draws_nothing(void) {
    cb::Canvas c = mkwide();
    cb::draw_char(c, 0, 0, ' ', 255, 1);
    for (int i = 0; i < 64 * 16; i++) TEST_ASSERT_EQUAL_UINT8(0, wide[i]);
}

void test_scale_two_doubles_each_pixel(void) {
    cb::Canvas c1 = mkwide();
    cb::draw_char(c1, 0, 0, 'L', 255, 1);
    bool lit_at_1_1 = c1.at(0, 0) != 0;
    cb::Canvas c2 = mkwide();
    cb::draw_char(c2, 0, 0, 'L', 255, 2);
    if (lit_at_1_1) {
        TEST_ASSERT_EQUAL_UINT8(255, c2.at(0, 0));
        TEST_ASSERT_EQUAL_UINT8(255, c2.at(1, 1));
    }
}

void test_unprintable_char_is_skipped(void) {
    cb::Canvas c = mkwide();
    cb::draw_text(c, 0, 0, "\x01\x02", 255, 1);
    for (int i = 0; i < 64 * 16; i++) TEST_ASSERT_EQUAL_UINT8(0, wide[i]);
}

void test_the_font_holds_every_printable_glyph(void) {
    // Row-major: one byte per row, FONT_H rows per glyph, bit 0 leftmost.
    TEST_ASSERT_EQUAL_INT(32, cb::FONT_FIRST);
    TEST_ASSERT_EQUAL_INT(126, cb::FONT_LAST);
    TEST_ASSERT_EQUAL_INT((cb::FONT_LAST - cb::FONT_FIRST + 1) * cb::FONT_H,
                          (int)sizeof(cb::FONT_DATA));

    // Space is the only blank cell; every other code point draws something.
    for (int ch = cb::FONT_FIRST; ch <= cb::FONT_LAST; ch++) {
        const uint8_t* g = &cb::FONT_DATA[(ch - cb::FONT_FIRST) * cb::FONT_H];
        int ink = 0;
        for (int r = 0; r < cb::FONT_H; r++) ink |= g[r];
        if (ch == ' ') TEST_ASSERT_EQUAL_INT(0, ink);
        else           TEST_ASSERT_TRUE(ink != 0);
    }
}

void test_the_big_font_holds_every_printable_glyph(void) {
    // Same shape of check as the small face. The big header is generated by
    // the same script and so carries the same risks -- a dropped glyph shifts
    // every code point after it, silently.
    TEST_ASSERT_EQUAL_INT((cb::FONT_LAST - cb::FONT_FIRST + 1) * cb::BIG_H,
                          (int)(sizeof(cb::FONT_BIG_DATA) / sizeof(cb::FONT_BIG_DATA[0])));
    for (int ch = cb::FONT_FIRST; ch <= cb::FONT_LAST; ch++) {
        const uint16_t* g = &cb::FONT_BIG_DATA[(ch - cb::FONT_FIRST) * cb::BIG_H];
        int ink = 0;
        for (int r = 0; r < cb::BIG_H; r++) ink |= g[r];
        if (ch == ' ') TEST_ASSERT_EQUAL_INT(0, ink);
        else           TEST_ASSERT_TRUE(ink != 0);
    }
}

void test_the_big_face_is_a_raster_not_the_small_one_doubled(void) {
    // The big face exists so that nothing on screen is a scaled-up small
    // glyph. Compared as bitmaps rather than as metrics: the two cells can
    // coincide in size by chance -- 12x24 happens to be exactly twice 6x12 --
    // and it is the shapes that have to differ.
    static uint8_t doubled[64 * 16];
    cb::Canvas d = mkwide();
    cb::draw_char(d, 0, 0, 'A', 255, 2);
    memcpy(doubled, wide, sizeof doubled);

    cb::Canvas n = mkwide();
    cb::draw_char_big(n, 0, 0, 'A', 255);

    int lit_doubled = 0, lit_native = 0;
    for (int i = 0; i < 64 * 16; i++) {
        if (doubled[i]) lit_doubled++;
        if (wide[i]) lit_native++;
    }
    TEST_ASSERT_TRUE(lit_doubled > 0 && lit_native > 0);
    TEST_ASSERT_TRUE(memcmp(doubled, wide, sizeof doubled) != 0);
}

void test_big_text_width_advances_one_cell_per_char(void) {
    TEST_ASSERT_EQUAL_INT(0, cb::text_width_big(""));
    TEST_ASSERT_EQUAL_INT(cb::BIG_ADV - 1, cb::text_width_big("A"));
    TEST_ASSERT_EQUAL_INT(4 * cb::BIG_ADV - 1, cb::text_width_big("100%"));
    // The widest banner the board can draw still clears the margins.
    TEST_ASSERT_TRUE(cb::text_width_big("SIGNAL LOST 99d+") <= cb::SCREEN_W - 12);
}

void test_the_brackets_still_mirror_each_other(void) {
    // A raw backslash at the end of a // comment is a line continuation, and
    // it once swallowed the ']' row out of the generated header: every glyph
    // past '\\' shifted down one and ']' drew as '^'. Mirroring is the
    // cheapest check that the array is still in register with ASCII, and the
    // tab row draws [CLAUDE], so these two are worth being right.
    //
    // The axis is found from the pair's own ink rather than fixed at the
    // middle of the cell. Where a glyph sits inside its cell is the font
    // designer's business, and it moved the first time the faces changed.
    const uint8_t* open  = &cb::FONT_DATA[('[' - cb::FONT_FIRST) * cb::FONT_H];
    const uint8_t* close = &cb::FONT_DATA[(']' - cb::FONT_FIRST) * cb::FONT_H];
    int lo = cb::FONT_W, hi = -1;
    for (int r = 0; r < cb::FONT_H; r++)
        for (int b = 0; b < cb::FONT_W; b++)
            if ((open[r] | close[r]) & (1u << b)) {
                if (b < lo) lo = b;
                if (b > hi) hi = b;
            }
    TEST_ASSERT_TRUE(hi > lo);
    for (int r = 0; r < cb::FONT_H; r++) {
        uint8_t flipped = 0;
        for (int b = 0; b < cb::FONT_W; b++)
            if (open[r] & (1u << b)) flipped |= static_cast<uint8_t>(1u << (lo + hi - b));
        TEST_ASSERT_EQUAL_UINT8(flipped, close[r]);
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_plot_and_read);
    RUN_TEST(test_plot_is_max_blend);
    RUN_TEST(test_out_of_bounds_is_ignored);
    RUN_TEST(test_decay_saturates_at_zero);
    RUN_TEST(test_hline_and_vline);
    RUN_TEST(test_rect_is_outline_only);
    RUN_TEST(test_fill_is_solid_and_clipped);
    RUN_TEST(test_clear);
    RUN_TEST(test_at_returns_zero_out_of_bounds);
    RUN_TEST(test_degenerate_canvas_is_inert);
    RUN_TEST(test_text_width_advances_one_cell_per_char);
    RUN_TEST(test_draw_char_marks_pixels);
    RUN_TEST(test_space_draws_nothing);
    RUN_TEST(test_scale_two_doubles_each_pixel);
    RUN_TEST(test_unprintable_char_is_skipped);
    RUN_TEST(test_the_font_holds_every_printable_glyph);
    RUN_TEST(test_the_brackets_still_mirror_each_other);
    RUN_TEST(test_the_big_font_holds_every_printable_glyph);
    RUN_TEST(test_the_big_face_is_a_raster_not_the_small_one_doubled);
    RUN_TEST(test_big_text_width_advances_one_cell_per_char);
    return UNITY_END();
}
