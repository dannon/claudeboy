#include <unity.h>
#include "core/types.h"

void test_screen_dimensions(void) {
    TEST_ASSERT_EQUAL_INT(320, cb::SCREEN_W);
    TEST_ASSERT_EQUAL_INT(240, cb::SCREEN_H);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_screen_dimensions);
    return UNITY_END();
}
