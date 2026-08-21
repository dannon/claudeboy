#include <unity.h>
#include "core/palette.h"

void test_zero_intensity_is_black(void) {
    cb::Rgb c = cb::palette_rgb(0);
    TEST_ASSERT_EQUAL_UINT8(0, c.r);
    TEST_ASSERT_EQUAL_UINT8(0, c.g);
    TEST_ASSERT_EQUAL_UINT8(0, c.b);
}

void test_full_intensity_is_phosphor_green(void) {
    cb::Rgb c = cb::palette_rgb(255);
    TEST_ASSERT_EQUAL_UINT8(255, c.g);
    TEST_ASSERT_TRUE(c.r < c.g);
    TEST_ASSERT_TRUE(c.b < c.g);
    TEST_ASSERT_TRUE(c.r > 0);      // slight warm bloom at the top end
}

void test_green_is_monotonic(void) {
    for (int i = 1; i < 256; i++) {
        TEST_ASSERT_TRUE(cb::palette_rgb(i).g >= cb::palette_rgb(i - 1).g);
    }
}

void test_midtones_stay_green_dominant(void) {
    cb::Rgb c = cb::palette_rgb(128);
    TEST_ASSERT_TRUE(c.g > c.r * 2);
    TEST_ASSERT_TRUE(c.g > c.b * 2);
}

void test_rgb565_packs_black_and_green(void) {
    TEST_ASSERT_EQUAL_UINT16(0x0000, cb::palette_rgb565(0));
    const uint16_t full = cb::palette_rgb565(255);
    TEST_ASSERT_EQUAL_UINT16(0x3F, (full >> 5) & 0x3F);   // green channel maxed
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_zero_intensity_is_black);
    RUN_TEST(test_full_intensity_is_phosphor_green);
    RUN_TEST(test_green_is_monotonic);
    RUN_TEST(test_midtones_stay_green_dominant);
    RUN_TEST(test_rgb565_packs_black_and_green);
    return UNITY_END();
}
