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

#include <string.h>
#include "core/crt.h"
#include "core/canvas.h"

static uint8_t cbuf[32 * 16];
static cb::Canvas mkc() { memset(cbuf, 0, sizeof cbuf); return cb::Canvas(cbuf, 32, 16); }

void test_defaults_are_sane(void) {
    cb::EffectParams p = cb::EffectParams::defaults();
    TEST_ASSERT_TRUE(p.decay > 0);
    TEST_ASSERT_TRUE(p.bloom_radius <= 4);
    TEST_ASSERT_TRUE(p.scanline_depth < 128);   // subtle, not half-brightness
}

void test_flicker_is_deterministic(void) {
    cb::EffectParams p = cb::EffectParams::defaults();
    TEST_ASSERT_EQUAL_UINT8(cb::flicker_gain(p, 42), cb::flicker_gain(p, 42));
}

void test_flicker_only_dims(void) {
    cb::EffectParams p = cb::EffectParams::defaults();
    for (uint32_t f = 0; f < 2000; f++) TEST_ASSERT_TRUE(cb::flicker_gain(p, f) <= 255);
}

void test_flicker_varies_between_frames(void) {
    cb::EffectParams p = cb::EffectParams::defaults();
    uint8_t first = cb::flicker_gain(p, 0);
    bool differs = false;
    for (uint32_t f = 1; f < 200; f++) if (cb::flicker_gain(p, f) != first) { differs = true; break; }
    TEST_ASSERT_TRUE(differs);
}

void test_zero_flicker_amount_is_constant(void) {
    cb::EffectParams p = cb::EffectParams::defaults();
    p.flicker_amount = 0;
    for (uint32_t f = 0; f < 100; f++) TEST_ASSERT_EQUAL_UINT8(255, cb::flicker_gain(p, f));
}

void test_apply_gain_scales(void) {
    cb::Canvas c = mkc();
    c.plot(1, 1, 200);
    cb::apply_gain(c, 128);
    TEST_ASSERT_UINT8_WITHIN(2, 100, c.at(1, 1));
    cb::apply_gain(c, 255);
    TEST_ASSERT_UINT8_WITHIN(2, 100, c.at(1, 1));   // full gain is a no-op
}

void test_ring_size_matches_radius(void) {
    TEST_ASSERT_EQUAL_UINT32(5u * 32u, (uint32_t)cb::bloom_ring_bytes(2, 32));
    TEST_ASSERT_EQUAL_UINT32(9u * 32u, (uint32_t)cb::bloom_ring_bytes(4, 32));
}

void test_bloom_spreads_a_point(void) {
    cb::Canvas c = mkc();
    c.plot(16, 8, 255);
    cb::EffectParams p = cb::EffectParams::defaults();
    p.bloom_radius = 2; p.bloom_strength = 255;
    uint8_t ring[9 * 32];
    cb::apply_bloom(c, p, ring, sizeof ring);
    TEST_ASSERT_TRUE(c.at(16, 8) >= 200);      // centre stays bright
    TEST_ASSERT_TRUE(c.at(17, 8) > 0);          // light bleeds sideways
    TEST_ASSERT_TRUE(c.at(16, 9) > 0);          // and vertically
    TEST_ASSERT_TRUE(c.at(17, 8) < c.at(16, 8));
}

void test_bloom_radius_zero_is_a_noop(void) {
    cb::Canvas c = mkc();
    c.plot(16, 8, 255);
    cb::EffectParams p = cb::EffectParams::defaults();
    p.bloom_radius = 0;
    uint8_t ring[9 * 32];
    cb::apply_bloom(c, p, ring, sizeof ring);
    TEST_ASSERT_EQUAL_UINT8(255, c.at(16, 8));
    TEST_ASSERT_EQUAL_UINT8(0, c.at(17, 8));
}

void test_bloom_never_darkens(void) {
    cb::Canvas c = mkc();
    for (int x = 0; x < 32; x++) c.plot(x, 4, 120);
    cb::EffectParams p = cb::EffectParams::defaults();
    uint8_t ring[9 * 32];
    cb::apply_bloom(c, p, ring, sizeof ring);
    for (int x = 0; x < 32; x++) TEST_ASSERT_TRUE(c.at(x, 4) >= 120);
}

void test_bloom_with_undersized_ring_is_skipped(void) {
    cb::Canvas c = mkc();
    c.plot(16, 8, 255);
    cb::EffectParams p = cb::EffectParams::defaults();
    uint8_t tiny[8];
    cb::apply_bloom(c, p, tiny, sizeof tiny);
    TEST_ASSERT_EQUAL_UINT8(0, c.at(17, 8));   // refused rather than corrupted
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_zero_intensity_is_black);
    RUN_TEST(test_full_intensity_is_phosphor_green);
    RUN_TEST(test_green_is_monotonic);
    RUN_TEST(test_midtones_stay_green_dominant);
    RUN_TEST(test_rgb565_packs_black_and_green);
    RUN_TEST(test_defaults_are_sane);
    RUN_TEST(test_flicker_is_deterministic);
    RUN_TEST(test_flicker_only_dims);
    RUN_TEST(test_flicker_varies_between_frames);
    RUN_TEST(test_zero_flicker_amount_is_constant);
    RUN_TEST(test_apply_gain_scales);
    RUN_TEST(test_ring_size_matches_radius);
    RUN_TEST(test_bloom_spreads_a_point);
    RUN_TEST(test_bloom_radius_zero_is_a_noop);
    RUN_TEST(test_bloom_never_darkens);
    RUN_TEST(test_bloom_with_undersized_ring_is_skipped);
    return UNITY_END();
}
