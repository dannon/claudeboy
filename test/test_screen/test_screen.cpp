#include <unity.h>
#include <string.h>
#include "core/screen.h"
#include "core/canvas.h"
#include "core/fixture.h"

static uint8_t sbuf[cb::SCREEN_W * cb::SCREEN_H];
static cb::Canvas mks() { memset(sbuf, 0, sizeof sbuf); return cb::Canvas(sbuf, cb::SCREEN_W, cb::SCREEN_H); }
static int lit_in(const cb::Canvas& c, int x0, int y0, int w, int h) {
    int n = 0;
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++)
            if (c.at(x, y)) n++;
    return n;
}

void test_format_duration_minutes(void) {
    char b[16]; cb::format_duration(45LL * 60 * 1000, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("45m", b);
}
void test_format_duration_hours_and_minutes(void) {
    char b[16]; cb::format_duration((1LL * 3600 + 20 * 60) * 1000, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("1h20m", b);
}
void test_format_duration_days_and_hours(void) {
    char b[16]; cb::format_duration((2LL * 86400 + 23 * 3600) * 1000, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("2d23h", b);
}
void test_format_duration_zero_and_negative(void) {
    char b[16];
    cb::format_duration(0, b, sizeof b);   TEST_ASSERT_EQUAL_STRING("0m", b);
    cb::format_duration(-5000, b, sizeof b); TEST_ASSERT_EQUAL_STRING("0m", b);
}

void test_layout_fits_the_panel(void) {
    TEST_ASSERT_TRUE(cb::CELL_Y + cb::CELL_H < cb::CHART_Y);
    TEST_ASSERT_TRUE(cb::CHART_Y + cb::CHART_H < cb::FOOT_Y);
    TEST_ASSERT_TRUE(cb::FOOT_Y + cb::FONT_H < cb::SCREEN_H);
}

void test_tabs_draw_in_the_strip_only(void) {
    cb::Canvas c = mks();
    cb::draw_tabs(c, cb::fixture_snapshot(), 0, "14:22");
    TEST_ASSERT_TRUE(lit_in(c, 0, 0, cb::SCREEN_W, cb::TAB_H + 1) > 40);
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, cb::TAB_H + 2, cb::SCREEN_W, 40));
}

void test_footer_draws_at_the_bottom(void) {
    cb::Canvas c = mks();
    cb::draw_footer(c, "TODAY $55.40", "WORKING");
    TEST_ASSERT_TRUE(lit_in(c, 0, cb::FOOT_Y - 4, cb::SCREEN_W, 20) > 20);
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, 0, cb::SCREEN_W, 100));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_format_duration_minutes);
    RUN_TEST(test_format_duration_hours_and_minutes);
    RUN_TEST(test_format_duration_days_and_hours);
    RUN_TEST(test_format_duration_zero_and_negative);
    RUN_TEST(test_layout_fits_the_panel);
    RUN_TEST(test_tabs_draw_in_the_strip_only);
    RUN_TEST(test_footer_draws_at_the_bottom);
    return UNITY_END();
}
