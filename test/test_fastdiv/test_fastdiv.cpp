#include <unity.h>
#include "core/fastdiv.h"

// Exhaustively checks the shift-based identity against real division for
// every x in [0, 65535], as instructed before trusting it enough to wire it
// into crt.cpp / palette.cpp.
//
// This is intentionally left failing. It fails at x == 65535 (shift form
// gives 256, real division gives 257); every other value in the range
// matches. Per instructions, a failure anywhere in the proven range means
// STOP and report rather than use an approximation -- so this test result is
// the report, not a bug to quietly patch by narrowing the loop bounds. The
// substitution was NOT applied to crt.cpp / palette.cpp as a result.
void test_div255_matches_real_division_exhaustively(void) {
    for (uint32_t x = 0; x <= 65535u; x++) {
        TEST_ASSERT_EQUAL_UINT32(x / 255u, cb::div255(x));
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_div255_matches_real_division_exhaustively);
    return UNITY_END();
}
