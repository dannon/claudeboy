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

// Red and blue are monotonic by construction -- a squared falloff scaled
// linearly -- but only green was ever pinned, so a change to the curve could
// dip or invert either of them and every test would still pass.
void test_red_and_blue_are_monotonic(void) {
    for (int i = 1; i < 256; i++) {
        const cb::Rgb c = cb::palette_rgb(i), prev = cb::palette_rgb(i - 1);
        TEST_ASSERT_TRUE_MESSAGE(c.r >= prev.r, "red channel dips");
        TEST_ASSERT_TRUE_MESSAGE(c.b >= prev.b, "blue channel dips");
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

void test_bloom_reaches_the_top_row(void) {
    cb::Canvas c = mkc();
    c.plot(16, 0, 255);
    cb::EffectParams p = cb::EffectParams::defaults();
    p.bloom_radius = 2; p.bloom_strength = 255;
    uint8_t ring[9 * 32];
    cb::apply_bloom(c, p, ring, sizeof ring);
    TEST_ASSERT_TRUE(c.at(16, 0) >= 200);      // top row stays bright
    TEST_ASSERT_TRUE(c.at(16, 1) > 0);          // blooms downward
    TEST_ASSERT_TRUE(c.at(17, 0) > 0);          // and sideways
}

void test_bloom_reaches_the_bottom_row(void) {
    cb::Canvas c = mkc();
    c.plot(16, 15, 255);
    cb::EffectParams p = cb::EffectParams::defaults();
    p.bloom_radius = 2; p.bloom_strength = 255;
    uint8_t ring[9 * 32];
    cb::apply_bloom(c, p, ring, sizeof ring);
    TEST_ASSERT_TRUE(c.at(16, 15) >= 200);     // bottom row stays bright
    TEST_ASSERT_TRUE(c.at(16, 14) > 0);         // blooms upward
    TEST_ASSERT_TRUE(c.at(17, 15) > 0);         // and sideways
}

void test_bloom_with_undersized_ring_is_skipped(void) {
    cb::Canvas c = mkc();
    c.plot(16, 8, 255);
    cb::EffectParams p = cb::EffectParams::defaults();
    uint8_t tiny[8];
    cb::apply_bloom(c, p, tiny, sizeof tiny);
    TEST_ASSERT_EQUAL_UINT8(0, c.at(17, 8));   // refused rather than corrupted
}

void test_scanlines_darken_odd_rows_only(void) {
    cb::Canvas c = mkc();
    c.fill(0, 0, 32, 16, 200);
    cb::EffectParams p = cb::EffectParams::defaults();
    cb::apply_scanlines(c, p);
    TEST_ASSERT_EQUAL_UINT8(200, c.at(5, 0));
    TEST_ASSERT_TRUE(c.at(5, 1) < 200);
    TEST_ASSERT_EQUAL_UINT8(200, c.at(5, 2));
}

void test_scanline_depth_zero_is_a_noop(void) {
    cb::Canvas c = mkc();
    c.fill(0, 0, 32, 16, 200);
    cb::EffectParams p = cb::EffectParams::defaults();
    p.scanline_depth = 0;
    cb::apply_scanlines(c, p);
    TEST_ASSERT_EQUAL_UINT8(200, c.at(5, 1));
}

void test_scanlines_stay_subtle_at_defaults(void) {
    cb::Canvas c = mkc();
    c.fill(0, 0, 32, 16, 200);
    cb::apply_scanlines(c, cb::EffectParams::defaults());
    TEST_ASSERT_TRUE(c.at(5, 1) > 120);   // dimmed, not halved
}

void test_vignette_darkens_corners_more_than_centre(void) {
    cb::Canvas c = mkc();
    c.fill(0, 0, 32, 16, 200);
    cb::apply_vignette(c, cb::EffectParams::defaults());
    TEST_ASSERT_TRUE(c.at(0, 0) < c.at(16, 8));
    TEST_ASSERT_TRUE(c.at(31, 15) < c.at(16, 8));
}

void test_vignette_leaves_centre_alone(void) {
    cb::Canvas c = mkc();
    c.fill(0, 0, 32, 16, 200);
    cb::apply_vignette(c, cb::EffectParams::defaults());
    TEST_ASSERT_UINT8_WITHIN(6, 200, c.at(16, 8));
}

void test_post_process_runs_all_stages(void) {
    cb::Canvas c = mkc();
    c.fill(8, 4, 16, 8, 220);
    cb::EffectParams p = cb::EffectParams::defaults();
    uint8_t ring[9 * 32];
    cb::post_process(c, p, 7, ring, sizeof ring);
    TEST_ASSERT_TRUE(c.at(16, 8) > 0);                 // centre survives
    TEST_ASSERT_TRUE(c.at(0, 0) < c.at(16, 8));        // vignette applied
    TEST_ASSERT_TRUE(c.at(16, 5) != c.at(16, 4));      // scanlines applied
}

void test_post_process_is_deterministic(void) {
    uint8_t a[32 * 16], b[32 * 16];
    uint8_t ring[9 * 32];
    cb::EffectParams p = cb::EffectParams::defaults();
    for (int pass = 0; pass < 2; pass++) {
        uint8_t* target = pass ? b : a;
        memset(target, 0, 32 * 16);
        cb::Canvas c(target, 32, 16);
        c.fill(8, 4, 16, 8, 220);
        cb::post_process(c, p, 99, ring, sizeof ring);
    }
    TEST_ASSERT_EQUAL_MEMORY(a, b, 32 * 16);
}

void test_post_process_applies_bloom(void) {
    cb::Canvas c = mkc();
    c.fill(12, 6, 8, 4, 255);  // small solid block in the middle
    cb::EffectParams p = cb::EffectParams::defaults();
    p.bloom_strength = 255;    // maximize bloom to ensure adjacent pixel is affected
    uint8_t ring[9 * 32];
    cb::post_process(c, p, 0, ring, sizeof ring);
    // Pixel just outside the block should have gotten light via bloom.
    // Scanlines, vignette, and gain only darken, so a 0 pixel can only become
    // non-zero through bloom.
    TEST_ASSERT_TRUE(c.at(11, 7) > 0);
}

void test_post_process_applies_vignette(void) {
    cb::Canvas c = mkc();
    c.fill(0, 0, 32, 16, 200);  // uniform field
    cb::EffectParams p = cb::EffectParams::defaults();
    uint8_t ring[9 * 32];
    cb::post_process(c, p, 0, ring, sizeof ring);
    // On even rows, scanlines don't apply. Bloom is uniform on a uniform field.
    // Only vignette varies with position, darkening corners.
    TEST_ASSERT_TRUE(c.at(0, 0) < c.at(16, 8));
}

void test_post_process_applies_flicker_gain(void) {
    uint8_t a[32 * 16], b[32 * 16];
    uint8_t ring[9 * 32];
    cb::EffectParams p = cb::EffectParams::defaults();

    // First run with flicker_amount = 0 (flicker_gain returns 255, a no-op)
    memset(a, 0, 32 * 16);
    cb::Canvas c1(a, 32, 16);
    c1.fill(0, 0, 32, 16, 200);
    p.flicker_amount = 0;
    cb::post_process(c1, p, 1, ring, sizeof ring);

    // Second run with flicker_amount = 200, same frame number
    memset(b, 0, 32 * 16);
    cb::Canvas c2(b, 32, 16);
    c2.fill(0, 0, 32, 16, 200);
    p.flicker_amount = 200;
    cb::post_process(c2, p, 1, ring, sizeof ring);

    // The buffers should NOT be identical (flicker_gain should scale pixels differently)
    TEST_ASSERT_FALSE(memcmp(a, b, 32 * 16) == 0);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_zero_intensity_is_black);
    RUN_TEST(test_full_intensity_is_phosphor_green);
    RUN_TEST(test_green_is_monotonic);
    RUN_TEST(test_red_and_blue_are_monotonic);
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
    RUN_TEST(test_bloom_reaches_the_top_row);
    RUN_TEST(test_bloom_reaches_the_bottom_row);
    RUN_TEST(test_bloom_with_undersized_ring_is_skipped);
    RUN_TEST(test_scanlines_darken_odd_rows_only);
    RUN_TEST(test_scanline_depth_zero_is_a_noop);
    RUN_TEST(test_scanlines_stay_subtle_at_defaults);
    RUN_TEST(test_vignette_darkens_corners_more_than_centre);
    RUN_TEST(test_vignette_leaves_centre_alone);
    RUN_TEST(test_post_process_runs_all_stages);
    RUN_TEST(test_post_process_is_deterministic);
    RUN_TEST(test_post_process_applies_bloom);
    RUN_TEST(test_post_process_applies_vignette);
    RUN_TEST(test_post_process_applies_flicker_gain);
    return UNITY_END();
}
