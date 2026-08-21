#include <unity.h>
#include "core/fastdiv.h"

// The identity `x / 255 == (x + 1 + (x >> 8)) >> 8` is exact for every x in
// [0, 65534]. It fails only at x == 65535 (see below). Every real call site
// in this codebase feeds div255() a product of two uint8_t values (max
// 255*255 = 65025), which sits well inside this proven-exact domain.
void test_div255_is_exact_over_its_domain(void) {
    for (uint32_t x = 0; x <= 65534u; x++) {
        TEST_ASSERT_EQUAL_UINT32(x / 255u, cb::div255(x));
    }
}

// Pin the known boundary so nobody "fixes" div255() into a wider domain
// without noticing it goes wrong right at the edge.
void test_div255_documents_its_upper_limit(void) {
    TEST_ASSERT_EQUAL_UINT32(257u, 65535u / 255u);
    TEST_ASSERT_EQUAL_UINT32(256u, cb::div255(65535u));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_div255_is_exact_over_its_domain);
    RUN_TEST(test_div255_documents_its_upper_limit);
    return UNITY_END();
}
