#include <unity.h>
#include "core/palette.h"

void test_rgb565_table_matches_palette_rgb565(void) {
    uint16_t table[256];
    cb::palette_build_rgb565_table(table);
    for (int i = 0; i < 256; i++) {
        TEST_ASSERT_EQUAL_UINT16(cb::palette_rgb565(static_cast<uint8_t>(i)), table[i]);
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rgb565_table_matches_palette_rgb565);
    return UNITY_END();
}
