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
static long sum_in(const cb::Canvas& c, int x0, int y0, int w, int h) {
    long s = 0;
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) s += c.at(x, y);
    return s;
}

static const int64_t REF  = cb::FIXTURE_REFERENCE_MS;
static const int64_t MIN  = 60LL * 1000;
static const int64_t HOUR = 60 * MIN;

// The band the numbers live in: gauge cells through the bottom of the chart.
static const int DATA_Y = cb::CELL_Y;
static const int DATA_H = cb::CHART_Y + cb::CHART_H - cb::CELL_Y;

// The fixture provider with its fetch timestamp moved. Static, because the
// snapshot handed back only points at it.
static cb::Provider g_sp;
static cb::UsageSnapshot with_fetched_at(int64_t fetched_at_ms) {
    g_sp = cb::fixture_snapshot().providers[0];
    g_sp.fetched_at_ms = fetched_at_ms;
    return cb::UsageSnapshot{&g_sp, 1, REF};
}
static cb::UsageSnapshot aged(int64_t age_ms) { return with_fetched_at(REF - age_ms); }

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

void test_format_clock_renders_hh_mm(void) {
    char b[8];
    // The capture instant itself, 2026-08-21T13:44:34Z.
    cb::format_clock(cb::FIXTURE_REFERENCE_MS, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("13:44", b);
    // Midnight UTC must not come out as 24:00, and a negative instant must
    // not produce a negative field.
    cb::format_clock(0, b, sizeof b);       TEST_ASSERT_EQUAL_STRING("00:00", b);
    cb::format_clock(-5000, b, sizeof b);   TEST_ASSERT_EQUAL_STRING("00:00", b);
    cb::format_clock(86399LL * 1000, b, sizeof b); TEST_ASSERT_EQUAL_STRING("23:59", b);
    cb::format_clock(86400LL * 1000, b, sizeof b); TEST_ASSERT_EQUAL_STRING("00:00", b);
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
    // `usable` goes negative long before count does; the width must not.
    TEST_ASSERT_TRUE(cb::cell_width(1000) >= 1);
    TEST_ASSERT_TRUE(cb::cell_width(0) >= 1);
    TEST_ASSERT_TRUE(cb::cell_width(-5) >= 1);
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
    // The fixture carries the full 31-day window from the capture; Aug 19
    // (index 28) is the 527.3M peak and should be the tallest column.
    TEST_ASSERT_EQUAL_INT(31, p.chart_count);
    int peak_idx = 0;
    for (int i = 1; i < p.chart_count; i++)
        if (p.chart[i].value > p.chart[peak_idx].value) peak_idx = i;
    TEST_ASSERT_EQUAL_INT(28, peak_idx);
    TEST_ASSERT_EQUAL_INT64(527342458LL, p.chart[peak_idx].value);
}

void test_empty_chart_does_not_crash(void) {
    cb::Canvas c = mks();
    cb::Provider empty{"x", "X", "", 0, nullptr, 0, nullptr, 0, nullptr, 0};
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

void test_invalid_pace_draws_no_gauge(void) {
    const int w = 100, bar_x = cb::MARGIN + 3, bar_w = w - 6;
    // limit 0: nothing to be a percentage of.
    cb::ProgressLine broken{"WEEKLY", 12, 0, cb::FIXTURE_REFERENCE_MS + 3600000, 7200000};
    cb::Pace p = cb::compute_pace(broken, cb::FIXTURE_REFERENCE_MS);
    TEST_ASSERT_FALSE(p.valid);

    cb::Canvas c = mks();
    cb::draw_gauge_cell(c, cb::MARGIN, cb::CELL_Y, w, broken, p);
    // An empty drain bar would read as a fully exhausted window, so there is
    // no bar, no pace tick and no verdict.
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, bar_x, cb::CELL_Y + 31, bar_w, 4));
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, bar_x, cb::CELL_Y + 36, bar_w, 8));
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, bar_x, cb::CELL_Y + 48, bar_w, cb::FONT_H));
    // The cell still says which line it is, and shows a placeholder reading.
    TEST_ASSERT_TRUE(lit_in(c, bar_x, cb::CELL_Y + 3, bar_w, cb::FONT_H) > 10);
    TEST_ASSERT_TRUE(lit_in(c, bar_x, cb::CELL_Y + 13, bar_w, 2 * cb::FONT_H) > 4);

    // Same for a line with no period at all.
    cb::ProgressLine no_period{"WEEKLY", 12, 100, cb::FIXTURE_REFERENCE_MS + 3600000, 0};
    cb::Pace q = cb::compute_pace(no_period, cb::FIXTURE_REFERENCE_MS);
    TEST_ASSERT_FALSE(q.valid);
    cb::Canvas d = mks();
    cb::draw_gauge_cell(d, cb::MARGIN, cb::CELL_Y, w, no_period, q);
    TEST_ASSERT_EQUAL_INT(0, lit_in(d, bar_x, cb::CELL_Y + 36, bar_w, 8));
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

// --- staleness --------------------------------------------------------------

void test_freshness_boundaries(void) {
    TEST_ASSERT_EQUAL(cb::Freshness::Fresh, cb::freshness_of(aged(0), REF));
    TEST_ASSERT_EQUAL(cb::Freshness::Fresh, cb::freshness_of(aged(10 * MIN - 1), REF));
    // Both boundaries belong to the gentler state: exactly ten minutes is
    // stale, exactly two hours is still stale and not yet a lost signal.
    TEST_ASSERT_EQUAL(cb::Freshness::Stale, cb::freshness_of(aged(10 * MIN), REF));
    TEST_ASSERT_EQUAL(cb::Freshness::Stale, cb::freshness_of(aged(10 * MIN + 1), REF));
    TEST_ASSERT_EQUAL(cb::Freshness::Stale, cb::freshness_of(aged(2 * HOUR - 1), REF));
    TEST_ASSERT_EQUAL(cb::Freshness::Stale, cb::freshness_of(aged(2 * HOUR), REF));
    TEST_ASSERT_EQUAL(cb::Freshness::SignalLost, cb::freshness_of(aged(2 * HOUR + 1), REF));
    TEST_ASSERT_EQUAL(cb::Freshness::SignalLost, cb::freshness_of(aged(30 * HOUR), REF));
}

void test_nothing_ever_fetched_is_no_signal(void) {
    TEST_ASSERT_EQUAL(cb::Freshness::NoSignal, cb::freshness_of(with_fetched_at(0), REF));
    // What the board actually holds before its first successful poll.
    cb::UsageSnapshot none{};
    TEST_ASSERT_EQUAL(cb::Freshness::NoSignal, cb::freshness_of(none, REF));
    TEST_ASSERT_EQUAL_INT64(0, cb::newest_fetched_at_ms(none));
    TEST_ASSERT_EQUAL_INT64(0, cb::snapshot_age_ms(none, REF));
}

void test_the_newest_provider_sets_the_state(void) {
    cb::Provider p[2];
    p[0] = cb::fixture_snapshot().providers[0];
    p[1] = cb::fixture_snapshot().providers[0];
    cb::UsageSnapshot s{p, 2, REF};

    p[0].fetched_at_ms = REF - 3 * HOUR;
    p[1].fetched_at_ms = REF - 1 * MIN;
    TEST_ASSERT_EQUAL_INT64(REF - 1 * MIN, cb::newest_fetched_at_ms(s));
    TEST_ASSERT_EQUAL(cb::Freshness::Fresh, cb::freshness_of(s, REF));

    // Order in the array must not decide it.
    p[0].fetched_at_ms = REF - 1 * MIN;
    p[1].fetched_at_ms = REF - 3 * HOUR;
    TEST_ASSERT_EQUAL(cb::Freshness::Fresh, cb::freshness_of(s, REF));

    // A provider that has never reported at all must not drag the rest down.
    p[1].fetched_at_ms = 0;
    TEST_ASSERT_EQUAL(cb::Freshness::Fresh, cb::freshness_of(s, REF));
}

void test_a_fetch_ahead_of_our_clock_is_not_a_negative_age(void) {
    // The board's clock comes from serverTime plus local millis, so it can sit
    // slightly behind the instant a provider was read.
    const cb::UsageSnapshot s = aged(-5 * MIN);
    TEST_ASSERT_EQUAL_INT64(0, cb::snapshot_age_ms(s, REF));
    TEST_ASSERT_EQUAL(cb::Freshness::Fresh, cb::freshness_of(s, REF));
}

void test_staleness_annotations(void) {
    char b[24];
    TEST_ASSERT_FALSE(cb::format_staleness(cb::Freshness::Fresh, 0, b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("", b);

    TEST_ASSERT_TRUE(cb::format_staleness(cb::Freshness::Stale, 24 * MIN, b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("STALE 24m", b);
    // The ages at the boundaries, through the one duration formatter.
    cb::format_staleness(cb::Freshness::Stale, 10 * MIN, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("STALE 10m", b);
    cb::format_staleness(cb::Freshness::Stale, 2 * HOUR, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("STALE 2h00m", b);
    cb::format_staleness(cb::Freshness::SignalLost, 3 * HOUR, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("SIGNAL LOST 3h00m", b);

    TEST_ASSERT_TRUE(cb::format_staleness(cb::Freshness::NoSignal, 0, b, sizeof b));
    TEST_ASSERT_EQUAL_STRING("NO SIGNAL", b);
}

void test_the_longest_annotation_clears_the_footer_value(void) {
    char b[24];
    cb::format_staleness(cb::Freshness::SignalLost, 500LL * 86400 * 1000, b, sizeof b);
    TEST_ASSERT_EQUAL_STRING("SIGNAL LOST 99d+", b);
    // Footer text is left-anchored and right-anchored; the widest value the
    // live payload carries must not run into the widest annotation.
    const int right_x = cb::SCREEN_W - cb::MARGIN - cb::text_width(b, 1);
    TEST_ASSERT_TRUE(right_x > cb::MARGIN + cb::text_width("$264.21 ? 332.3M tokens", 1));
}

// mks() hands every canvas the same static buffer, so each measurement has to
// be taken before the next render begins.
static long data_sum(const cb::UsageSnapshot& s) {
    cb::Canvas c = mks();
    cb::render_ambient(c, s, 0, REF, "14:44");
    return sum_in(c, 0, DATA_Y, cb::SCREEN_W, DATA_H);
}
static int data_lit(const cb::UsageSnapshot& s) {
    cb::Canvas c = mks();
    cb::render_ambient(c, s, 0, REF, "14:44");
    return lit_in(c, 0, DATA_Y, cb::SCREEN_W, DATA_H);
}
static int footer_right_lit(const cb::UsageSnapshot& s) {
    cb::Canvas c = mks();
    cb::render_ambient(c, s, 0, REF, "14:44");
    return lit_in(c, cb::SCREEN_W / 2, cb::FOOT_Y, cb::SCREEN_W / 2, cb::FONT_H);
}

void test_fresh_numbers_are_drawn_at_full_brightness(void) {
    // Nothing about a fresh frame changes as it ages up to the ten-minute mark.
    const long plain = data_sum(cb::fixture_snapshot());
    TEST_ASSERT_EQUAL_INT64(plain, data_sum(aged(0)));
    TEST_ASSERT_EQUAL_INT64(plain, data_sum(aged(10 * MIN - 1)));
}

void test_stale_render_dims_the_numbers_without_erasing_them(void) {
    const long bright = data_sum(aged(0));
    const int  lit    = data_lit(aged(0));
    const long dimmed = data_sum(aged(24 * MIN));

    TEST_ASSERT_TRUE(dimmed < bright);
    // Turned down, not switched off -- the last known numbers stay readable,
    // and every pixel that was lit is still lit.
    TEST_ASSERT_TRUE(dimmed > bright / 4);
    TEST_ASSERT_EQUAL_INT(lit, data_lit(aged(24 * MIN)));

    TEST_ASSERT_TRUE(data_sum(aged(3 * HOUR)) < bright);
}

void test_the_annotation_is_not_dimmed_with_what_it_annotates(void) {
    cb::Canvas stale = mks();
    cb::render_ambient(stale, aged(24 * MIN), 0, REF, "14:44");
    // draw_footer draws its right-hand text at I_NORMAL; a dim pass reaching
    // the footer would knock the peak down.
    int peak = 0;
    for (int y = cb::FOOT_Y; y < cb::FOOT_Y + cb::FONT_H; y++)
        for (int x = cb::SCREEN_W / 2; x < cb::SCREEN_W; x++)
            if (stale.at(x, y) > peak) peak = stale.at(x, y);
    TEST_ASSERT_EQUAL_INT(cb::I_NORMAL, peak);

    // and it really replaced the fresh-state text rather than sitting under it
    const int annotated = footer_right_lit(aged(24 * MIN));
    TEST_ASSERT_TRUE(annotated != footer_right_lit(aged(0)));
}

void test_no_signal_shows_no_numbers_at_all(void) {
    cb::Canvas c = mks();
    cb::render_ambient(c, with_fetched_at(0), 0, REF, "14:44");
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, cb::CELL_Y, cb::SCREEN_W, cb::CELL_H));
    // Chart bars grow from the floor of the chart band, starting at the left
    // margin. Sampled only across the leftmost bars: the NO SIGNAL banner is
    // centred in the data region, which the chart band is part of, so a
    // full-width sample would be measuring the banner rather than the bars.
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, cb::CHART_Y + cb::CHART_H - 12, 45, 12));
    // no footer value either -- that is a number too
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, cb::FOOT_Y, cb::SCREEN_W, cb::FONT_H));
    // but the board says why, in the space the numbers would have filled
    TEST_ASSERT_TRUE(lit_in(c, 0, DATA_Y, cb::SCREEN_W, DATA_H) > 100);
    // and the frame is still a frame
    TEST_ASSERT_TRUE(lit_in(c, 0, 0, cb::SCREEN_W, cb::TAB_H + 1) > 40);
    TEST_ASSERT_TRUE(lit_in(c, 0, cb::FOOT_Y - 4, cb::SCREEN_W, 1) > 100);
}

void test_an_empty_snapshot_renders_no_signal(void) {
    cb::Canvas c = mks();
    cb::UsageSnapshot none{};
    // provider_index 0 is out of range here; the banner must still be drawn.
    cb::render_ambient(c, none, 0, REF, "14:44");
    TEST_ASSERT_TRUE(lit_in(c, 0, DATA_Y, cb::SCREEN_W, DATA_H) > 100);
}

void test_the_active_plan_rides_with_its_tab(void) {
    cb::Provider p = cb::fixture_snapshot().providers[0];
    cb::UsageSnapshot s{&p, 1, REF};
    const int clock_x = cb::SCREEN_W - cb::MARGIN - cb::text_width("14:44", 1);

    // One shared canvas buffer, so each strip is measured before the next.
    cb::Canvas c = mks();
    cb::draw_tabs(c, s, 0, "14:44");
    const int with_plan = lit_in(c, 0, 0, cb::SCREEN_W, cb::TAB_H);
    // nothing runs into the clock
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, clock_x - 6, 0, 6, cb::TAB_H));

    p.plan = "";
    cb::Canvas bare = mks();
    cb::draw_tabs(bare, s, 0, "14:44");
    const int without_plan = lit_in(bare, 0, 0, cb::SCREEN_W, cb::TAB_H);
    TEST_ASSERT_TRUE(with_plan > without_plan);

    // A plan too long for the room left is dropped rather than drawn under
    // the clock -- the provider list is server-side and can grow.
    p.plan = "SOME ABSURDLY LONG PLAN NAME NOBODY WOULD SELL";
    cb::Canvas crowded = mks();
    cb::draw_tabs(crowded, s, 0, "14:44");
    TEST_ASSERT_EQUAL_INT(without_plan, lit_in(crowded, 0, 0, cb::SCREEN_W, cb::TAB_H));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_format_duration_minutes);
    RUN_TEST(test_format_duration_hours_and_minutes);
    RUN_TEST(test_format_duration_days_and_hours);
    RUN_TEST(test_format_duration_zero_and_negative);
    RUN_TEST(test_format_duration_clamps_absurd_values);
    RUN_TEST(test_format_clock_renders_hh_mm);
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
    RUN_TEST(test_invalid_pace_draws_no_gauge);
    RUN_TEST(test_unknown_state_draws_no_tick);
    RUN_TEST(test_freshness_boundaries);
    RUN_TEST(test_nothing_ever_fetched_is_no_signal);
    RUN_TEST(test_the_newest_provider_sets_the_state);
    RUN_TEST(test_a_fetch_ahead_of_our_clock_is_not_a_negative_age);
    RUN_TEST(test_staleness_annotations);
    RUN_TEST(test_the_longest_annotation_clears_the_footer_value);
    RUN_TEST(test_fresh_numbers_are_drawn_at_full_brightness);
    RUN_TEST(test_stale_render_dims_the_numbers_without_erasing_them);
    RUN_TEST(test_the_annotation_is_not_dimmed_with_what_it_annotates);
    RUN_TEST(test_no_signal_shows_no_numbers_at_all);
    RUN_TEST(test_an_empty_snapshot_renders_no_signal);
    RUN_TEST(test_the_active_plan_rides_with_its_tab);
    return UNITY_END();
}
