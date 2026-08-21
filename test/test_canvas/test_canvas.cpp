#include <unity.h>
#include <string.h>
#include "core/canvas.h"

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
    return UNITY_END();
}
