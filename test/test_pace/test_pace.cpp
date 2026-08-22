#include <unity.h>
#include <math.h>
#include "core/types.h"
#include "core/pace.h"
#include "core/fixture.h"

static cb::ProgressLine line(int32_t used, int64_t resets_in_ms, int64_t period_ms) {
    return cb::ProgressLine{"Test", used, 100, 1000000 + resets_in_ms, period_ms};
}
static const int64_t NOW = 1000000;
static const int64_t HOUR = 3600LL * 1000;

void test_on_pace_midwindow(void) {
    // 58% used, 57.6% elapsed -> ratio ~1.007 -> OnPace
    cb::Pace p = cb::compute_pace(line(58, 71LL*HOUR, 168LL*HOUR), NOW);
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_EQUAL(cb::PaceState::OnPace, p.state);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.42f, p.remaining_frac);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 1.007f, p.ratio);
}

void test_surplus(void) {
    // 20% used, 83% elapsed -> ratio 0.24
    cb::Pace p = cb::compute_pace(line(20, 51LL*HOUR/60, 5LL*HOUR), NOW);
    TEST_ASSERT_EQUAL(cb::PaceState::Surplus, p.state);
    TEST_ASSERT_TRUE(p.ratio < 0.90f);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, p.ratio, p.projected_frac);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.80f, p.remaining_frac);
}

void test_burnout_reports_how_early(void) {
    // 80% used with only 50% elapsed -> ratio 1.6, exhausts well before reset
    // ETA: elapsed * (1-used_frac)/used_frac = 84h * 0.25 = 21h, so 84h - 21h = 63h before reset
    cb::Pace p = cb::compute_pace(line(80, 84LL*HOUR, 168LL*HOUR), NOW);
    TEST_ASSERT_EQUAL(cb::PaceState::Burnout, p.state);
    TEST_ASSERT_TRUE(p.ratio > 1.02f);
    TEST_ASSERT_FLOAT_WITHIN(5LL*60*1000, 63LL*HOUR, p.burnout_early_ms);
}

void test_window_just_reset_does_not_divide_by_zero(void) {
    // elapsed == 0
    cb::Pace p = cb::compute_pace(line(0, 168LL*HOUR, 168LL*HOUR), NOW);
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_EQUAL(cb::PaceState::Unknown, p.state);
    TEST_ASSERT_FALSE(isnan(p.ratio));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, p.remaining_frac);
}

void test_reset_in_the_past_clamps(void) {
    cb::Pace p = cb::compute_pace(line(40, -5LL*HOUR, 168LL*HOUR), NOW);
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_EQUAL_INT64(0, p.reset_in_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, p.elapsed_frac);
}

void test_exhausted_window(void) {
    cb::Pace p = cb::compute_pace(line(100, 10LL*HOUR, 168LL*HOUR), NOW);
    TEST_ASSERT_EQUAL(cb::PaceState::Burnout, p.state);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, p.remaining_frac);
    TEST_ASSERT_EQUAL_INT64(10LL*HOUR, p.burnout_early_ms);
}

void test_fresh_window_does_not_cry_burnout(void) {
    // Antigravity's real shape one minute into a 5h window: a sliver of usage
    // against a sliver of elapsed time computes to a huge ratio.
    cb::Pace p = cb::compute_pace(line(1, 299LL*HOUR/60, 5LL*HOUR), NOW);
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_TRUE(p.ratio > 1.02f);                    // the raw ratio is alarming
    TEST_ASSERT_EQUAL(cb::PaceState::Unknown, p.state);   // but we refuse to call it
    TEST_ASSERT_EQUAL_INT64(0, p.burnout_early_ms);
}

void test_exhausted_reports_even_when_early(void) {
    // Burning the whole window in its first minutes is real news, not noise.
    cb::Pace p = cb::compute_pace(line(100, 299LL*HOUR/60, 5LL*HOUR), NOW);
    TEST_ASSERT_EQUAL(cb::PaceState::Burnout, p.state);
}

void test_verdict_appears_once_past_the_floor(void) {
    // 10% elapsed, comfortably past MIN_ELAPSED_FRAC
    cb::Pace p = cb::compute_pace(line(2, 151LL*HOUR, 168LL*HOUR), NOW);
    TEST_ASSERT_EQUAL(cb::PaceState::Surplus, p.state);
}

void test_reset_further_out_than_one_period_clamps(void) {
    // reset_in 300 hours in a 168-hour window -- nonsense the API could emit
    cb::Pace p = cb::compute_pace(line(40, 300LL*HOUR, 168LL*HOUR), NOW);
    TEST_ASSERT_TRUE(p.valid);
    TEST_ASSERT_EQUAL_INT64(168LL*HOUR, p.reset_in_ms);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, p.elapsed_frac);
    TEST_ASSERT_EQUAL(cb::PaceState::Unknown, p.state);
}

void test_just_below_elapsed_floor_gives_no_verdict(void) {
    // 4% elapsed, just below the MIN_ELAPSED_FRAC floor
    cb::Pace p = cb::compute_pace(line(3, 96LL*HOUR, 100LL*HOUR), NOW);
    TEST_ASSERT_EQUAL(cb::PaceState::Unknown, p.state);
}

void test_just_above_elapsed_floor_gives_a_verdict(void) {
    // 6% elapsed, just above the MIN_ELAPSED_FRAC floor, used_frac 0.03, ratio 0.5
    cb::Pace p = cb::compute_pace(line(3, 94LL*HOUR, 100LL*HOUR), NOW);
    TEST_ASSERT_EQUAL(cb::PaceState::Surplus, p.state);
}

void test_zero_period_is_invalid(void) {
    cb::Pace p = cb::compute_pace(line(50, HOUR, 0), NOW);
    TEST_ASSERT_FALSE(p.valid);
    TEST_ASSERT_EQUAL(cb::PaceState::Unknown, p.state);
}

void test_zero_limit_is_invalid(void) {
    cb::ProgressLine l{"Test", 5, 0, NOW + HOUR, 168LL*HOUR};
    cb::Pace p = cb::compute_pace(l, NOW);
    TEST_ASSERT_FALSE(p.valid);
}

void test_fixture_reproduces_captured_states(void) {
    const cb::UsageSnapshot& s = cb::fixture_snapshot();
    TEST_ASSERT_EQUAL_INT(3, s.provider_count);
    // This test is about the Claude capture specifically -- the numbers below
    // are the ones that were on screen when it was taken.
    const cb::Provider& p = s.providers[0];
    TEST_ASSERT_EQUAL_INT(3, p.progress_count);

    cb::Pace session = cb::compute_pace(p.progress[0], cb::FIXTURE_REFERENCE_MS);
    cb::Pace weekly  = cb::compute_pace(p.progress[1], cb::FIXTURE_REFERENCE_MS);
    cb::Pace fable   = cb::compute_pace(p.progress[2], cb::FIXTURE_REFERENCE_MS);

    TEST_ASSERT_EQUAL(cb::PaceState::Surplus, session.state);
    TEST_ASSERT_EQUAL(cb::PaceState::OnPace,  weekly.state);
    TEST_ASSERT_EQUAL(cb::PaceState::Surplus, fable.state);

    TEST_ASSERT_FLOAT_WITHIN(0.03f, 1.01f, weekly.ratio);
    TEST_ASSERT_FLOAT_WITHIN(0.02f, 0.42f, weekly.remaining_frac);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_on_pace_midwindow);
    RUN_TEST(test_surplus);
    RUN_TEST(test_burnout_reports_how_early);
    RUN_TEST(test_window_just_reset_does_not_divide_by_zero);
    RUN_TEST(test_reset_in_the_past_clamps);
    RUN_TEST(test_exhausted_window);
    RUN_TEST(test_fresh_window_does_not_cry_burnout);
    RUN_TEST(test_exhausted_reports_even_when_early);
    RUN_TEST(test_verdict_appears_once_past_the_floor);
    RUN_TEST(test_reset_further_out_than_one_period_clamps);
    RUN_TEST(test_just_below_elapsed_floor_gives_no_verdict);
    RUN_TEST(test_just_above_elapsed_floor_gives_a_verdict);
    RUN_TEST(test_zero_period_is_invalid);
    RUN_TEST(test_zero_limit_is_invalid);
    RUN_TEST(test_fixture_reproduces_captured_states);
    return UNITY_END();
}
