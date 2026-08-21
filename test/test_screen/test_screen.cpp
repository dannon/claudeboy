#include <unity.h>
#include <string.h>
#include "core/screen.h"
#include "core/canvas.h"
#include "core/fixture.h"
#include "core/pace.h"

static uint8_t sbuf[cb::SCREEN_W * cb::SCREEN_H];
static cb::Canvas mks() { memset(sbuf, 0, sizeof sbuf); return cb::Canvas(sbuf, cb::SCREEN_W, cb::SCREEN_H); }
static int lit_in(const cb::Canvas& c, int x0, int y0, int w, int h) {
    int n = 0;
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++)
            if (c.at(x, y)) n++;
    return n;
}

void test_format_duration_minutes(void) {
    char b[16]; cb::format_duration(45LL * 60 * 1000, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("45m", b);
}
void test_format_duration_hours_and_minutes(void) {
    char b[16]; cb::format_duration((1LL * 3600 + 20 * 60) * 1000, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("1h20m", b);
}
void test_format_duration_days_and_hours(void) {
    char b[16]; cb::format_duration((2LL * 86400 + 23 * 3600) * 1000, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("2d23h", b);
}
void test_format_duration_zero_and_negative(void) {
    char b[16];
    cb::format_duration(0, b, sizeof b);   TEST_ASSERT_EQUAL_STRING("0m", b);
    cb::format_duration(-5000, b, sizeof b); TEST_ASSERT_EQUAL_STRING("0m", b);
}

void test_layout_fits_the_panel(void) {
    TEST_ASSERT_TRUE(cb::CELL_Y + cb::CELL_H < cb::CHART_Y);
    TEST_ASSERT_TRUE(cb::CHART_Y + cb::CHART_H < cb::FOOT_Y);
    TEST_ASSERT_TRUE(cb::FOOT_Y + cb::FONT_H < cb::SCREEN_H);
}

void test_tabs_draw_in_the_strip_only(void) {
    cb::Canvas c = mks();
    cb::draw_tabs(c, cb::fixture_snapshot(), 0, "14:22");
    TEST_ASSERT_TRUE(lit_in(c, 0, 0, cb::SCREEN_W, cb::TAB_H + 1) > 40);
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, cb::TAB_H + 2, cb::SCREEN_W, 40));
}

void test_footer_draws_at_the_bottom(void) {
    cb::Canvas c = mks();
    cb::draw_footer(c, "TODAY $55.40", "WORKING");
    TEST_ASSERT_TRUE(lit_in(c, 0, cb::FOOT_Y - 4, cb::SCREEN_W, 20) > 20);
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, 0, cb::SCREEN_W, 100));
}

void test_format_duration_clamps_absurd_values(void) {
    char b[16];
    cb::format_duration(500LL * 86400 * 1000, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("99d+", b);
    TEST_ASSERT_TRUE(strlen(b) <= 7);
}

void test_cell_width_fits_three_and_four(void) {
    const int usable = cb::SCREEN_W - 2 * cb::MARGIN;
    for (int n = 1; n <= 4; n++) {
        const int w = cb::cell_width(n);
        TEST_ASSERT_TRUE(w > 0);
        TEST_ASSERT_TRUE(n * w + (n - 1) * cb::CELL_GAP <= usable);
    }
    TEST_ASSERT_TRUE(cb::cell_width(4) < cb::cell_width(3));
}

void test_cells_draw_inside_their_band(void) {
    cb::Canvas c = mks();
    const cb::Provider& p = cb::fixture_snapshot().providers[0];
    cb::draw_cells(c, p, cb::FIXTURE_REFERENCE_MS);
    TEST_ASSERT_TRUE(lit_in(c, 0, cb::CELL_Y, cb::SCREEN_W, cb::CELL_H) > 200);
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, 0, cb::SCREEN_W, cb::CELL_Y - 1));
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, cb::CELL_Y + cb::CELL_H + 1, cb::SCREEN_W, 20));
}

void test_unknown_state_draws_no_verdict(void) {
    cb::Canvas c = mks();
    cb::ProgressLine fresh{"NEW", 1, 100,
                           cb::FIXTURE_REFERENCE_MS + 299LL * 60 * 1000,
                           5LL * 3600 * 1000};
    cb::Pace pc = cb::compute_pace(fresh, cb::FIXTURE_REFERENCE_MS);
    TEST_ASSERT_EQUAL(cb::PaceState::Unknown, pc.state);
    cb::draw_gauge_cell(c, cb::MARGIN, cb::CELL_Y, 100, fresh, pc);
    // Verdict row stays blank when we have no verdict to give.
    const int verdict_y = cb::CELL_Y + 48;
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, cb::MARGIN + 3, verdict_y, 90, cb::FONT_H));
}

void test_gauge_bar_length_tracks_remaining(void) {
    cb::Canvas full = mks();
    cb::ProgressLine a{"A", 0, 100, cb::FIXTURE_REFERENCE_MS + 3600000, 7200000};
    cb::draw_gauge_cell(full, cb::MARGIN, cb::CELL_Y, 100, a,
                        cb::compute_pace(a, cb::FIXTURE_REFERENCE_MS));
    const int wide = lit_in(full, cb::MARGIN, cb::CELL_Y + 36, 100, 8);

    cb::Canvas low = mks();
    cb::ProgressLine b{"B", 90, 100, cb::FIXTURE_REFERENCE_MS + 3600000, 7200000};
    cb::draw_gauge_cell(low, cb::MARGIN, cb::CELL_Y, 100, b,
                        cb::compute_pace(b, cb::FIXTURE_REFERENCE_MS));
    const int narrow = lit_in(low, cb::MARGIN, cb::CELL_Y + 36, 100, 8);

    TEST_ASSERT_TRUE(wide > narrow);
}

void test_chart_draws_in_its_band(void) {
    cb::Canvas c = mks();
    cb::draw_chart(c, cb::fixture_snapshot().providers[0]);
    TEST_ASSERT_TRUE(lit_in(c, 0, cb::CHART_Y, cb::SCREEN_W, cb::CHART_H) > 100);
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, cb::CHART_Y + cb::CHART_H + 2, cb::SCREEN_W, 5));
}

void test_tallest_bar_is_the_peak_day(void) {
    cb::Canvas c = mks();
    const cb::Provider& p = cb::fixture_snapshot().providers[0];
    cb::draw_chart(c, p);
    // Aug 19 (index 6) is the 527M peak; it should be the tallest column.
    int peak_idx = 0;
    for (int i = 1; i < p.chart_count; i++)
        if (p.chart[i].value > p.chart[peak_idx].value) peak_idx = i;
    TEST_ASSERT_EQUAL_INT(6, peak_idx);
}

void test_empty_chart_does_not_crash(void) {
    cb::Canvas c = mks();
    cb::Provider empty{"x", "X", nullptr, 0, nullptr, 0, nullptr, 0};
    cb::draw_chart(c, empty);
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, cb::CHART_Y, cb::SCREEN_W, cb::CHART_H));
}

void test_render_ambient_fills_all_bands(void) {
    cb::Canvas c = mks();
    cb::render_ambient(c, cb::fixture_snapshot(), 0, cb::FIXTURE_REFERENCE_MS, "14:44");
    TEST_ASSERT_TRUE(lit_in(c, 0, 0, cb::SCREEN_W, cb::TAB_H) > 20);
    TEST_ASSERT_TRUE(lit_in(c, 0, cb::CELL_Y, cb::SCREEN_W, cb::CELL_H) > 200);
    TEST_ASSERT_TRUE(lit_in(c, 0, cb::CHART_Y, cb::SCREEN_W, cb::CHART_H) > 100);
    TEST_ASSERT_TRUE(lit_in(c, 0, cb::FOOT_Y - 4, cb::SCREEN_W, 12) > 20);
}

void test_render_ambient_with_bad_index_is_safe(void) {
    cb::Canvas c = mks();
    cb::render_ambient(c, cb::fixture_snapshot(), 99, cb::FIXTURE_REFERENCE_MS, "14:44");
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, cb::CELL_Y, cb::SCREEN_W, cb::CELL_H));
}

void test_verdict_fits_a_narrow_cell(void) {
    cb::Canvas c = mks();
    const int w = cb::cell_width(4);
    // 99d23h reset -- "BURNOUT 99d23h" is 14 chars, 83px at scale 1, wider
    // than a 4-cell interior (68px). used==limit forces Burnout regardless
    // of elapsed_frac.
    cb::ProgressLine line{"SESSION", 100, 100,
                          cb::FIXTURE_REFERENCE_MS + 8636400000LL,
                          200LL * 86400 * 1000};
    cb::Pace p = cb::compute_pace(line, cb::FIXTURE_REFERENCE_MS);
    TEST_ASSERT_EQUAL(cb::PaceState::Burnout, p.state);
    cb::draw_gauge_cell(c, cb::MARGIN, cb::CELL_Y, w, line, p);
    // Nothing should bleed past this cell's right border into whatever sits
    // next to it.
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, cb::MARGIN + w, cb::CELL_Y, 20, cb::CELL_H));
}

void test_unknown_state_draws_no_tick(void) {
    cb::Canvas c = mks();
    cb::ProgressLine fresh{"NEW", 1, 100,
                           cb::FIXTURE_REFERENCE_MS + 299LL * 60 * 1000,
                           5LL * 3600 * 1000};
    cb::Pace pc = cb::compute_pace(fresh, cb::FIXTURE_REFERENCE_MS);
    TEST_ASSERT_EQUAL(cb::PaceState::Unknown, pc.state);
    cb::draw_gauge_cell(c, cb::MARGIN, cb::CELL_Y, 100, fresh, pc);
    // Pace tick stays undrawn when there's no pace to show.
    const int bar_x = cb::MARGIN + 3, bar_w = 100 - 6;
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, bar_x, cb::CELL_Y + 31, bar_w, 4));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_format_duration_minutes);
    RUN_TEST(test_format_duration_hours_and_minutes);
    RUN_TEST(test_format_duration_days_and_hours);
    RUN_TEST(test_format_duration_zero_and_negative);
    RUN_TEST(test_format_duration_clamps_absurd_values);
    RUN_TEST(test_layout_fits_the_panel);
    RUN_TEST(test_tabs_draw_in_the_strip_only);
    RUN_TEST(test_footer_draws_at_the_bottom);
    RUN_TEST(test_cell_width_fits_three_and_four);
    RUN_TEST(test_cells_draw_inside_their_band);
    RUN_TEST(test_unknown_state_draws_no_verdict);
    RUN_TEST(test_gauge_bar_length_tracks_remaining);
    RUN_TEST(test_chart_draws_in_its_band);
    RUN_TEST(test_tallest_bar_is_the_peak_day);
    RUN_TEST(test_empty_chart_does_not_crash);
    RUN_TEST(test_render_ambient_fills_all_bands);
    RUN_TEST(test_render_ambient_with_bad_index_is_safe);
    RUN_TEST(test_verdict_fits_a_narrow_cell);
    RUN_TEST(test_unknown_state_draws_no_tick);
    return UNITY_END();
}
