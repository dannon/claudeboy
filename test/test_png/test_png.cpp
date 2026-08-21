#include <unity.h>
#include <stdio.h>
#include "core/canvas.h"
#include "host/png.h"

static const char* PATH = "out/test-png-guard.png";

// Canvas degrades to 0x0 when it is handed bad dimensions, and an empty
// payload makes the stored-block deflate emit no BFINAL block -- a stream no
// decoder accepts. Writing that file would look like success.
void test_empty_dimensions_are_refused(void) {
    const uint8_t one[3] = {1, 2, 3};
    remove(PATH);
    TEST_ASSERT_FALSE(cbhost::write_png_rgb(PATH, one, 0, 0));
    TEST_ASSERT_FALSE(cbhost::write_png_rgb(PATH, one, 4, 0));
    TEST_ASSERT_FALSE(cbhost::write_png_rgb(PATH, one, 0, 4));
    TEST_ASSERT_FALSE(cbhost::write_png_rgb(PATH, one, -4, -4));
    TEST_ASSERT_FALSE(cbhost::write_png_rgb(PATH, nullptr, 1, 1));
    TEST_ASSERT_NULL(fopen(PATH, "rb"));   // and no half-written file left behind
}

void test_a_degraded_canvas_is_refused(void) {
    uint8_t px = 200;
    cb::Canvas bad(&px, -1, -1);           // degrades to 0x0
    TEST_ASSERT_EQUAL_INT(0, bad.width());
    remove(PATH);
    TEST_ASSERT_FALSE(cbhost::write_png_from_canvas(PATH, bad));
    TEST_ASSERT_NULL(fopen(PATH, "rb"));
}

void test_a_real_canvas_still_writes(void) {
    uint8_t px[4] = {0, 90, 180, 255};
    cb::Canvas c(px, 2, 2);
    remove(PATH);
    TEST_ASSERT_TRUE(cbhost::write_png_from_canvas(PATH, c));

    FILE* f = fopen(PATH, "rb");
    TEST_ASSERT_NOT_NULL(f);
    uint8_t head[24] = {0};
    const size_t got = fread(head, 1, sizeof head, f);
    fclose(f);
    remove(PATH);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)sizeof head, (uint32_t)got);
    const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(sig, head, 8);
    TEST_ASSERT_EQUAL_UINT32(2u, (uint32_t)head[19]);   // IHDR width
    TEST_ASSERT_EQUAL_UINT32(2u, (uint32_t)head[23]);   // IHDR height
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_empty_dimensions_are_refused);
    RUN_TEST(test_a_degraded_canvas_is_refused);
    RUN_TEST(test_a_real_canvas_still_writes);
    return UNITY_END();
}
