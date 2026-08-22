#include <unity.h>
#include "core/fixture.h"
#include "core/pace.h"

static const int64_t REF = cb::FIXTURE_REFERENCE_MS;
static const int64_t HOUR = 3600LL * 1000;

void test_future_window_is_left_alone(void) {
    cb::ProgressLine line{"S", 21, 100, REF + 45 * HOUR / 60, 5 * HOUR};
    const cb::ProgressLine rolled = cb::roll_window_forward(line, REF);
    TEST_ASSERT_EQUAL_INT64(line.resets_at_ms, rolled.resets_at_ms);
}

void test_a_window_that_just_expired_rolls_one_period(void) {
    cb::ProgressLine line{"S", 21, 100, REF, 5 * HOUR};
    // Reset exactly now still has to move: the new window starts here.
    const cb::ProgressLine rolled = cb::roll_window_forward(line, REF);
    TEST_ASSERT_EQUAL_INT64(REF + 5 * HOUR, rolled.resets_at_ms);
}

void test_rolls_forward_across_many_periods(void) {
    const int64_t period = 5 * HOUR;
    cb::ProgressLine line{"S", 21, 100, REF + 45 * HOUR / 60, period};
    // 1,000 windows and change past the capture -- a device left running.
    const int64_t now = line.resets_at_ms + 1000 * period + 37 * 60 * 1000;
    const cb::ProgressLine rolled = cb::roll_window_forward(line, now);

    TEST_ASSERT_TRUE(rolled.resets_at_ms > now);
    TEST_ASSERT_TRUE(rolled.resets_at_ms - now <= period);
    // Still on the original window boundaries, just a whole number of them on.
    TEST_ASSERT_EQUAL_INT64(0, (rolled.resets_at_ms - line.resets_at_ms) % period);
    TEST_ASSERT_EQUAL_INT64(1001, (rolled.resets_at_ms - line.resets_at_ms) / period);
}

void test_only_the_reset_time_moves(void) {
    cb::ProgressLine line{"S", 21, 100, REF - HOUR, 5 * HOUR};
    const cb::ProgressLine rolled = cb::roll_window_forward(line, REF);
    TEST_ASSERT_EQUAL_STRING(line.label, rolled.label);
    TEST_ASSERT_EQUAL_INT32(line.used, rolled.used);
    TEST_ASSERT_EQUAL_INT32(line.limit, rolled.limit);
    TEST_ASSERT_EQUAL_INT64(line.period_ms, rolled.period_ms);
}

void test_a_line_with_no_period_is_untouched(void) {
    cb::ProgressLine line{"S", 21, 100, REF - HOUR, 0};
    const cb::ProgressLine rolled = cb::roll_window_forward(line, REF);
    TEST_ASSERT_EQUAL_INT64(line.resets_at_ms, rolled.resets_at_ms);
}

// The point of the whole exercise: the cell keeps counting down instead of
// pinning at "0m" once the capture's own window has gone by.
void test_rolled_window_still_has_time_left_on_it(void) {
    const int64_t period = 5 * HOUR;
    cb::ProgressLine line{"S", 21, 100, REF + 45 * HOUR / 60, period};
    const int64_t now = REF + 3 * period;

    TEST_ASSERT_EQUAL_INT64(0, cb::compute_pace(line, now).reset_in_ms);
    const cb::Pace p = cb::compute_pace(cb::roll_window_forward(line, now), now);
    TEST_ASSERT_TRUE(p.reset_in_ms > 0);
    TEST_ASSERT_TRUE(p.reset_in_ms <= period);
}

// The fixture itself is still the capture, unrolled -- nothing here may drift
// the host renders or the golden off the reference instant.
void test_the_snapshot_stays_pinned_to_the_capture(void) {
    const cb::UsageSnapshot& s = cb::fixture_snapshot();
    TEST_ASSERT_EQUAL_INT64(REF, s.server_time_ms);
    TEST_ASSERT_EQUAL_INT(3, s.provider_count);
    TEST_ASSERT_EQUAL_INT(3, s.providers[0].progress_count);
    TEST_ASSERT_EQUAL_INT(31, s.providers[0].chart_count);
    TEST_ASSERT_EQUAL_INT64(REF + 45 * HOUR / 60, s.providers[0].progress[0].resets_at_ms);
    // Antigravity is in the fixture for its shape, not its numbers: four
    // windows, no text and no chart. If it ever grows either, the "+N" tag and
    // the NO DAILY HISTORY message stop being covered by the golden.
    TEST_ASSERT_EQUAL_STRING("ANTIGRAVITY", s.providers[2].display_name);
    TEST_ASSERT_EQUAL_INT(4, s.providers[2].progress_count);
    TEST_ASSERT_EQUAL_INT(0, s.providers[2].text_count);
    TEST_ASSERT_EQUAL_INT(0, s.providers[2].chart_count);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_future_window_is_left_alone);
    RUN_TEST(test_a_window_that_just_expired_rolls_one_period);
    RUN_TEST(test_rolls_forward_across_many_periods);
    RUN_TEST(test_only_the_reset_time_moves);
    RUN_TEST(test_a_line_with_no_period_is_untouched);
    RUN_TEST(test_rolled_window_still_has_time_left_on_it);
    RUN_TEST(test_the_snapshot_stays_pinned_to_the_capture);
    return UNITY_END();
}
