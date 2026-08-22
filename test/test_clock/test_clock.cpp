#include <unity.h>
#include "core/clock.h"

// A plausible serverTime: 2026-08-21T22:22:28Z in epoch ms.
static const int64_t SERVER_MS = 1787358148000LL;

void test_unseeded_reports_no_time(void) {
    cb::ServerClock c;
    TEST_ASSERT_FALSE(c.seeded);
    TEST_ASSERT_EQUAL_INT64(0, cb::clock_now(c, 12345));
}

void test_seeded_reads_back_the_server_instant(void) {
    cb::ServerClock c;
    cb::clock_seed(c, SERVER_MS, 900000);
    TEST_ASSERT_TRUE(c.seeded);
    TEST_ASSERT_EQUAL_INT64(SERVER_MS, cb::clock_now(c, 900000));
}

void test_time_advances_with_the_local_counter(void) {
    cb::ServerClock c;
    cb::clock_seed(c, SERVER_MS, 900000);
    TEST_ASSERT_EQUAL_INT64(SERVER_MS + 60000, cb::clock_now(c, 960000));
}

void test_reseeding_re_anchors_rather_than_accumulating(void) {
    cb::ServerClock c;
    cb::clock_seed(c, SERVER_MS, 900000);
    // A minute later the next reply says a minute and a half went by. The
    // server is the authority, so the board jumps to it instead of averaging.
    cb::clock_seed(c, SERVER_MS + 90000, 960000);
    TEST_ASSERT_EQUAL_INT64(SERVER_MS + 90000, cb::clock_now(c, 960000));
    TEST_ASSERT_EQUAL_INT64(SERVER_MS + 91000, cb::clock_now(c, 961000));
}

void test_the_49_day_wrap_does_not_throw_time_backwards(void) {
    // Seeded two seconds before millis() wraps, read one second after.
    const uint32_t before = 0xFFFFF830u;   // UINT32_MAX - 1999
    cb::ServerClock c;
    cb::clock_seed(c, SERVER_MS, before);
    TEST_ASSERT_EQUAL_INT64(SERVER_MS + 3000, cb::clock_now(c, (uint32_t)(before + 3000)));
}

void test_elapsed_is_wrap_safe_at_the_boundary(void) {
    const uint32_t since = 0xFFFFFF00u;
    TEST_ASSERT_FALSE(cb::clock_elapsed(since, since + 999, 1000));
    TEST_ASSERT_TRUE(cb::clock_elapsed(since, since + 1000, 1000));   // spans the wrap
    TEST_ASSERT_TRUE(cb::clock_elapsed(since, since + 60000, 1000));
}

void test_elapsed_is_true_the_instant_the_span_is_reached(void) {
    TEST_ASSERT_TRUE(cb::clock_elapsed(1000, 1000, 0));   // a zero span is always due
    TEST_ASSERT_FALSE(cb::clock_elapsed(1000, 60999, 60000));
    TEST_ASSERT_TRUE(cb::clock_elapsed(1000, 61000, 60000));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_unseeded_reports_no_time);
    RUN_TEST(test_seeded_reads_back_the_server_instant);
    RUN_TEST(test_time_advances_with_the_local_counter);
    RUN_TEST(test_reseeding_re_anchors_rather_than_accumulating);
    RUN_TEST(test_the_49_day_wrap_does_not_throw_time_backwards);
    RUN_TEST(test_elapsed_is_wrap_safe_at_the_boundary);
    RUN_TEST(test_elapsed_is_true_the_instant_the_span_is_reached);
    return UNITY_END();
}
