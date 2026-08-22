#include <unity.h>
#include "core/burn.h"

static const int64_t MIN = 60LL * 1000;
static const int64_t T0 = 1787000000000LL;

static cb::BurnHistory h;

void setUp(void) { cb::burn_init(h); }
void tearDown(void) {}

void test_no_history_has_no_rate(void) {
    TEST_ASSERT_EQUAL_INT64(-1, cb::burn_rate_per_hour(h, T0, cb::BURN_WINDOW_MS));
    cb::burn_observe(h, T0, 1000);
    // One reading is a level, not a rate.
    TEST_ASSERT_EQUAL_INT64(-1, cb::burn_rate_per_hour(h, T0, cb::BURN_WINDOW_MS));
}

void test_two_readings_a_span_apart_give_a_rate(void) {
    cb::burn_observe(h, T0, 0);
    cb::burn_observe(h, T0 + 10 * MIN, 1000000);
    // A million tokens in ten minutes is six million an hour.
    TEST_ASSERT_EQUAL_INT64(6000000, cb::burn_rate_per_hour(h, T0 + 10 * MIN, cb::BURN_WINDOW_MS));
}

void test_readings_too_close_together_report_nothing(void) {
    cb::burn_observe(h, T0, 0);
    cb::burn_observe(h, T0 + 2 * MIN, 500000);
    // Two minutes apart is poll jitter, not a measurement.
    TEST_ASSERT_EQUAL_INT64(-1, cb::burn_rate_per_hour(h, T0 + 2 * MIN, cb::BURN_WINDOW_MS));
    cb::burn_observe(h, T0 + 5 * MIN, 500000);
    TEST_ASSERT_TRUE(cb::burn_rate_per_hour(h, T0 + 5 * MIN, cb::BURN_WINDOW_MS) >= 0);
}

void test_idle_reads_zero_not_unknown(void) {
    cb::burn_observe(h, T0, 900);
    cb::burn_observe(h, T0 + 5 * MIN, 900);
    cb::burn_observe(h, T0 + 10 * MIN, 900);
    // Nothing consumed is a fact, and a different one from having no idea.
    TEST_ASSERT_EQUAL_INT64(0, cb::burn_rate_per_hour(h, T0 + 10 * MIN, cb::BURN_WINDOW_MS));
}

void test_a_total_going_backwards_starts_over(void) {
    cb::burn_observe(h, T0, 500000000);
    cb::burn_observe(h, T0 + 5 * MIN, 600000000);
    // Midnight: today's cumulative count drops back to nearly nothing. The
    // difference across that boundary is not a negative burn rate.
    cb::burn_observe(h, T0 + 10 * MIN, 1000);
    TEST_ASSERT_EQUAL_INT64(-1, cb::burn_rate_per_hour(h, T0 + 10 * MIN, cb::BURN_WINDOW_MS));
    cb::burn_observe(h, T0 + 20 * MIN, 2000000);
    const int64_t r = cb::burn_rate_per_hour(h, T0 + 20 * MIN, cb::BURN_WINDOW_MS);
    TEST_ASSERT_TRUE(r > 0);
    // Measured from the fresh start, not from before the rollover.
    TEST_ASSERT_EQUAL_INT64((2000000LL - 1000) * 3600000 / (10 * MIN), r);
}

void test_a_stale_history_reports_unknown(void) {
    cb::burn_observe(h, T0, 0);
    cb::burn_observe(h, T0 + 10 * MIN, 1000000);
    // An hour later, with no new readings, the last known rate is history and
    // must not be quoted as if it were current.
    TEST_ASSERT_EQUAL_INT64(-1, cb::burn_rate_per_hour(h, T0 + 70 * MIN, cb::BURN_WINDOW_MS));
}

void test_repeated_and_backwards_timestamps_are_dropped(void) {
    cb::burn_observe(h, T0, 1000);
    cb::burn_observe(h, T0, 9999);              // same instant
    cb::burn_observe(h, T0 - 5 * MIN, 9999);    // clock jumped back
    cb::burn_observe(h, T0 + 10 * MIN, 1000000);
    TEST_ASSERT_EQUAL_INT64((1000000LL - 1000) * 3600000 / (10 * MIN),
                            cb::burn_rate_per_hour(h, T0 + 10 * MIN, cb::BURN_WINDOW_MS));
}

void test_the_ring_wraps_without_losing_the_rate(void) {
    // Three times round the ring, a minute and a thousand tokens per step.
    for (int i = 0; i <= 3 * cb::BURN_SLOTS; i++)
        cb::burn_observe(h, T0 + i * MIN, (int64_t)i * 1000);
    const int64_t now = T0 + 3 * cb::BURN_SLOTS * MIN;
    // A thousand a minute is sixty thousand an hour, whatever the window saw.
    TEST_ASSERT_EQUAL_INT64(60000, cb::burn_rate_per_hour(h, now, cb::BURN_WINDOW_MS));
}

void test_a_negative_total_is_not_a_reading(void) {
    cb::burn_observe(h, T0, -5);
    cb::burn_observe(h, T0 + 10 * MIN, -5);
    TEST_ASSERT_EQUAL_INT64(-1, cb::burn_rate_per_hour(h, T0 + 10 * MIN, cb::BURN_WINDOW_MS));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_no_history_has_no_rate);
    RUN_TEST(test_two_readings_a_span_apart_give_a_rate);
    RUN_TEST(test_readings_too_close_together_report_nothing);
    RUN_TEST(test_idle_reads_zero_not_unknown);
    RUN_TEST(test_a_total_going_backwards_starts_over);
    RUN_TEST(test_a_stale_history_reports_unknown);
    RUN_TEST(test_repeated_and_backwards_timestamps_are_dropped);
    RUN_TEST(test_the_ring_wraps_without_losing_the_rate);
    RUN_TEST(test_a_negative_total_is_not_a_reading);
    return UNITY_END();
}
