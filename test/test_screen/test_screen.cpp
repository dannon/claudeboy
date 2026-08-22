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

// The band the numbers live in: hero cards through the bottom of the panel.
static const int DATA_Y = cb::HERO_Y;
static const int DATA_H = cb::PANEL_Y + cb::PANEL_H - cb::HERO_Y;

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
    // Every band clears the next, top to bottom, with nothing overlapping.
    TEST_ASSERT_TRUE(cb::TAB_H < cb::HERO_Y);
    TEST_ASSERT_TRUE(cb::HERO_Y + cb::HERO_H < cb::STRIP_Y);
    TEST_ASSERT_TRUE(cb::STRIP_Y + cb::STRIP_H < cb::CHART_Y);
    TEST_ASSERT_TRUE(cb::CHART_Y + cb::CHART_H < cb::PANEL_Y);
    TEST_ASSERT_TRUE(cb::PANEL_Y + cb::PANEL_H < cb::FOOT_Y - 4);
    TEST_ASSERT_TRUE(cb::FOOT_Y + cb::FONT_H < cb::SCREEN_H);
}

void test_window_badge_splits_on_a_day(void) {
    // A session block refills before the next fight; a weekly ration does not.
    TEST_ASSERT_EQUAL_STRING("AP", cb::window_badge(5LL * 3600 * 1000));
    TEST_ASSERT_EQUAL_STRING("AP", cb::window_badge(24LL * 3600 * 1000));
    TEST_ASSERT_EQUAL_STRING("HP", cb::window_badge(24LL * 3600 * 1000 + 1));
    TEST_ASSERT_EQUAL_STRING("HP", cb::window_badge(168LL * 3600 * 1000));
    // A line with no period at all is not a thing that refills.
    TEST_ASSERT_EQUAL_STRING("HP", cb::window_badge(0));
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

void test_hero_width_fits_two_side_by_side(void) {
    const int w = cb::hero_width();
    TEST_ASSERT_TRUE(w > 0);
    TEST_ASSERT_TRUE(2 * w + cb::HERO_GAP <= cb::SCREEN_W - 2 * cb::MARGIN);
}

void test_windows_draw_inside_their_bands(void) {
    cb::Canvas c = mks();
    const cb::Provider& p = cb::fixture_snapshot().providers[0];
    cb::draw_windows(c, p, cb::FIXTURE_REFERENCE_MS);
    TEST_ASSERT_TRUE(lit_in(c, 0, cb::HERO_Y, cb::SCREEN_W, cb::HERO_H) > 200);
    // The fixture's third window lands on the strip row.
    TEST_ASSERT_TRUE(lit_in(c, 0, cb::STRIP_Y, cb::SCREEN_W, cb::STRIP_H) > 40);
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, 0, cb::SCREEN_W, cb::HERO_Y - 1));
    // Nothing spills into the gutter between the bands, or onto the chart.
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, cb::HERO_Y + cb::HERO_H, cb::SCREEN_W,
                                    cb::STRIP_Y - cb::HERO_Y - cb::HERO_H));
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, cb::STRIP_Y + cb::STRIP_H, cb::SCREEN_W,
                                    cb::CHART_Y - cb::STRIP_Y - cb::STRIP_H));
}

void test_a_window_that_does_not_fit_is_counted_not_dropped(void) {
    cb::Canvas c = mks();
    const cb::ProgressLine five[] = {
        {"SESSION", 21, 100, REF + HOUR, 5LL * HOUR},
        {"WEEKLY",  58, 100, REF + 40 * HOUR, 168LL * HOUR},
        {"FABLE",   17, 100, REF + 40 * HOUR, 168LL * HOUR},
        {"EXTRA1",  10, 100, REF + 40 * HOUR, 168LL * HOUR},
        {"EXTRA2",  10, 100, REF + 40 * HOUR, 168LL * HOUR},
    };
    cb::Provider prov{"x", "X", "", REF, five, 5, nullptr, 0, nullptr, 0};
    cb::draw_windows(c, prov, REF);
    // Two heroes and one strip is all the layout holds; the two that did not
    // fit are announced as "+2" at the right of the strip row rather than
    // silently vanishing.
    const int tag_w = cb::text_width("+2", 1);
    TEST_ASSERT_TRUE(lit_in(c, cb::SCREEN_W - cb::MARGIN - tag_w, cb::STRIP_Y,
                            tag_w, cb::STRIP_H) > 4);
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, cb::STRIP_Y + cb::STRIP_H, cb::SCREEN_W,
                                    cb::CHART_Y - cb::STRIP_Y - cb::STRIP_H));
}

void test_unknown_state_draws_no_verdict(void) {
    cb::Canvas c = mks();
    cb::ProgressLine fresh{"NEW", 1, 100,
                           cb::FIXTURE_REFERENCE_MS + 299LL * 60 * 1000,
                           5LL * 3600 * 1000};
    cb::Pace pc = cb::compute_pace(fresh, cb::FIXTURE_REFERENCE_MS);
    TEST_ASSERT_EQUAL(cb::PaceState::Unknown, pc.state);
    cb::draw_hero(c, cb::MARGIN, cb::HERO_Y, cb::hero_width(), fresh, pc);
    // No verdict word, because we have no verdict to give. The countdown is
    // a separate fact and still true, so it keeps its place on the right.
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, cb::MARGIN + 5, cb::HERO_Y + 58,
                                    cb::text_width("SURPLUS", 1), cb::FONT_H));
    TEST_ASSERT_TRUE(lit_in(c, cb::MARGIN + cb::hero_width() / 2, cb::HERO_Y + 58,
                            cb::hero_width() / 2 - 5, cb::FONT_H) > 4);
}

void test_gauge_bar_length_tracks_remaining(void) {
    const int w = cb::hero_width();
    cb::Canvas full = mks();
    cb::ProgressLine a{"A", 0, 100, REF + 3600000, 7200000};
    cb::draw_hero(full, cb::MARGIN, cb::HERO_Y, w, a, cb::compute_pace(a, REF));
    const int wide = lit_in(full, cb::MARGIN, cb::HERO_Y + 48, w, 7);

    cb::Canvas low = mks();
    cb::ProgressLine b{"B", 90, 100, REF + 3600000, 7200000};
    cb::draw_hero(low, cb::MARGIN, cb::HERO_Y, w, b, cb::compute_pace(b, REF));
    const int narrow = lit_in(low, cb::MARGIN, cb::HERO_Y + 48, w, 7);

    TEST_ASSERT_TRUE(wide > narrow);
}

void test_strip_bar_length_tracks_remaining(void) {
    const int w = cb::SCREEN_W - 2 * cb::MARGIN;
    cb::Canvas full = mks();
    cb::ProgressLine a{"A", 0, 100, REF + 3600000, 7200000};
    cb::draw_strip(full, cb::MARGIN, cb::STRIP_Y, w, a, cb::compute_pace(a, REF));
    const int wide = lit_in(full, cb::MARGIN, cb::STRIP_Y + 3, w, 6);

    cb::Canvas low = mks();
    cb::ProgressLine b{"B", 90, 100, REF + 3600000, 7200000};
    cb::draw_strip(low, cb::MARGIN, cb::STRIP_Y, w, b, cb::compute_pace(b, REF));
    const int narrow = lit_in(low, cb::MARGIN, cb::STRIP_Y + 3, w, 6);

    TEST_ASSERT_TRUE(wide > narrow);
    // Both still say which window they are and what percent is left.
    TEST_ASSERT_TRUE(lit_in(low, cb::MARGIN, cb::STRIP_Y, 3 * cb::FONT_ADV, cb::STRIP_H) > 4);
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
    TEST_ASSERT_TRUE(lit_in(c, 0, cb::HERO_Y, cb::SCREEN_W, cb::HERO_H) > 200);
    TEST_ASSERT_TRUE(lit_in(c, 0, cb::STRIP_Y, cb::SCREEN_W, cb::STRIP_H) > 40);
    TEST_ASSERT_TRUE(lit_in(c, 0, cb::CHART_Y, cb::SCREEN_W, cb::CHART_H) > 100);
    TEST_ASSERT_TRUE(lit_in(c, 0, cb::FOOT_Y - 4, cb::SCREEN_W, 12) > 20);
}

void test_render_ambient_with_bad_index_is_safe(void) {
    cb::Canvas c = mks();
    cb::render_ambient(c, cb::fixture_snapshot(), 99, cb::FIXTURE_REFERENCE_MS, "14:44");
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, cb::HERO_Y, cb::SCREEN_W, cb::HERO_H));
}

void test_verdict_and_countdown_stay_inside_the_hero(void) {
    cb::Canvas c = mks();
    const int w = cb::hero_width();
    // used == limit forces Burnout regardless of elapsed_frac, so this also
    // exercises the trefoil, which is drawn nearest the right border.
    cb::ProgressLine line{"SESSION", 100, 100,
                          REF + 8636400000LL, 200LL * 86400 * 1000};
    cb::Pace p = cb::compute_pace(line, REF);
    TEST_ASSERT_EQUAL(cb::PaceState::Burnout, p.state);
    cb::draw_hero(c, cb::MARGIN, cb::HERO_Y, w, line, p);
    // Nothing bleeds past the right border into the card next to it.
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, cb::MARGIN + w, cb::HERO_Y, cb::HERO_GAP, cb::HERO_H));
    // The trefoil is there.
    TEST_ASSERT_TRUE(lit_in(c, cb::MARGIN + w - 27, cb::HERO_Y + 22, 21, 21) > 40);
}

void test_invalid_pace_draws_no_gauge(void) {
    const int w = cb::hero_width(), bar_x = cb::MARGIN + 5, bar_w = w - 10;
    // limit 0: nothing to be a percentage of.
    cb::ProgressLine broken{"WEEKLY", 12, 0, REF + 3600000, 7200000};
    cb::Pace p = cb::compute_pace(broken, REF);
    TEST_ASSERT_FALSE(p.valid);

    cb::Canvas c = mks();
    cb::draw_hero(c, cb::MARGIN, cb::HERO_Y, w, broken, p);
    // An empty drain bar would read as a fully exhausted window, so there is
    // no bar, no pace tick and no verdict.
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, bar_x, cb::HERO_Y + 44, bar_w, 3));
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, bar_x, cb::HERO_Y + 47, bar_w, 9));
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, bar_x, cb::HERO_Y + 58, bar_w, cb::FONT_H));
    // The card still says which line it is, and shows a placeholder reading.
    TEST_ASSERT_TRUE(lit_in(c, bar_x, cb::HERO_Y + 5, bar_w, 2 * cb::FONT_H) > 10);
    TEST_ASSERT_TRUE(lit_in(c, bar_x, cb::HERO_Y + 22, bar_w, 3 * cb::FONT_H) > 4);

    // Same for a line with no period at all.
    cb::ProgressLine no_period{"WEEKLY", 12, 100, REF + 3600000, 0};
    cb::Pace q = cb::compute_pace(no_period, REF);
    TEST_ASSERT_FALSE(q.valid);
    cb::Canvas d = mks();
    cb::draw_hero(d, cb::MARGIN, cb::HERO_Y, w, no_period, q);
    TEST_ASSERT_EQUAL_INT(0, lit_in(d, bar_x, cb::HERO_Y + 47, bar_w, 9));
    // A strip with nothing to plot draws no bar either.
    cb::Canvas e = mks();
    cb::draw_strip(e, cb::MARGIN, cb::STRIP_Y, cb::SCREEN_W - 2 * cb::MARGIN, no_period, q);
    TEST_ASSERT_EQUAL_INT(0, lit_in(e, cb::MARGIN + 12 * cb::FONT_ADV, cb::STRIP_Y,
                                    100, cb::STRIP_H));
}

void test_unknown_state_draws_no_tick(void) {
    cb::Canvas c = mks();
    cb::ProgressLine fresh{"NEW", 1, 100,
                           cb::FIXTURE_REFERENCE_MS + 299LL * 60 * 1000,
                           5LL * 3600 * 1000};
    cb::Pace pc = cb::compute_pace(fresh, cb::FIXTURE_REFERENCE_MS);
    TEST_ASSERT_EQUAL(cb::PaceState::Unknown, pc.state);
    cb::draw_hero(c, cb::MARGIN, cb::HERO_Y, cb::hero_width(), fresh, pc);
    // Pace tick stays undrawn when there's no pace to show.
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, cb::MARGIN + 5, cb::HERO_Y + 44,
                                    cb::hero_width() - 10, 3));
}

// --- exposure log -----------------------------------------------------------

void test_format_tok_abbreviates_with_one_decimal(void) {
    char b[12];
    cb::format_tok(0, b, sizeof b);             TEST_ASSERT_EQUAL_STRING("0", b);
    cb::format_tok(742, b, sizeof b);           TEST_ASSERT_EQUAL_STRING("742", b);
    cb::format_tok(999, b, sizeof b);           TEST_ASSERT_EQUAL_STRING("999", b);
    cb::format_tok(1000, b, sizeof b);          TEST_ASSERT_EQUAL_STRING("1.0K", b);
    cb::format_tok(56278305, b, sizeof b);      TEST_ASSERT_EQUAL_STRING("56.3M", b);
    cb::format_tok(208264047, b, sizeof b);     TEST_ASSERT_EQUAL_STRING("208.3M", b);
    cb::format_tok(4800000000LL, b, sizeof b);  TEST_ASSERT_EQUAL_STRING("4.8B", b);
    // Rounding must carry into the whole part rather than print "9.10M".
    cb::format_tok(9990000, b, sizeof b);       TEST_ASSERT_EQUAL_STRING("10.0M", b);
    // A count we could not work out reads as absent, not as zero.
    cb::format_tok(-1, b, sizeof b);            TEST_ASSERT_EQUAL_STRING("--", b);
    // Nothing here may outgrow the column it is right-aligned in.
    cb::format_tok(999900000000LL, b, sizeof b);
    TEST_ASSERT_TRUE(cb::text_width(b, 1) <= cb::LOG_TOK_R - cb::LOG_X);
}

void test_parse_caps_reads_the_dollar_figure(void) {
    // The shapes the provider has actually sent, old separator and new.
    TEST_ASSERT_EQUAL_INT64(56, cb::parse_caps("$56.67 - 56.3M"));
    TEST_ASSERT_EQUAL_INT64(4512, cb::parse_caps("$4,512 - 4.8B"));
    TEST_ASSERT_EQUAL_INT64(68, cb::parse_caps("$68.52 \xc2\xb7 98.1M tokens"));
    TEST_ASSERT_EQUAL_INT64(0, cb::parse_caps("$0.00"));
    TEST_ASSERT_EQUAL_INT64(4869, cb::parse_caps("$4,869.05"));
    // Nothing to read is -1, not 0 -- a blank column and a genuine zero are
    // different facts.
    TEST_ASSERT_EQUAL_INT64(-1, cb::parse_caps("56.3M tokens"));
    TEST_ASSERT_EQUAL_INT64(-1, cb::parse_caps("$"));
    TEST_ASSERT_EQUAL_INT64(-1, cb::parse_caps("$abc"));
    TEST_ASSERT_EQUAL_INT64(-1, cb::parse_caps(""));
    TEST_ASSERT_EQUAL_INT64(-1, cb::parse_caps(nullptr));
}

void test_format_caps_groups_thousands(void) {
    char b[16];
    cb::format_caps(0, b, sizeof b);      TEST_ASSERT_EQUAL_STRING("0", b);
    cb::format_caps(68, b, sizeof b);     TEST_ASSERT_EQUAL_STRING("68", b);
    cb::format_caps(999, b, sizeof b);    TEST_ASSERT_EQUAL_STRING("999", b);
    cb::format_caps(1000, b, sizeof b);   TEST_ASSERT_EQUAL_STRING("1,000", b);
    cb::format_caps(4869, b, sizeof b);   TEST_ASSERT_EQUAL_STRING("4,869", b);
    cb::format_caps(1234567, b, sizeof b); TEST_ASSERT_EQUAL_STRING("1,234,567", b);
    cb::format_caps(-1, b, sizeof b);     TEST_ASSERT_EQUAL_STRING("--", b);
    // A buffer too small truncates and still terminates.
    char small[4];
    cb::format_caps(1234567, small, sizeof small);
    TEST_ASSERT_TRUE(strlen(small) < sizeof small);
}

void test_chart_total_sums_the_tail(void) {
    const cb::Provider& p = cb::fixture_snapshot().providers[0];
    TEST_ASSERT_EQUAL_INT64(56278305LL, cb::chart_total(p, 1));
    TEST_ASSERT_EQUAL_INT64(56278305LL + 208264047LL, cb::chart_total(p, 2));
    // 31 days on the wire, so a 30-day total is available and a 31-day one is
    // the whole array. Asking for more than exists is -1, not a short sum.
    TEST_ASSERT_TRUE(cb::chart_total(p, 30) > 0);
    TEST_ASSERT_TRUE(cb::chart_total(p, 31) > cb::chart_total(p, 30));
    TEST_ASSERT_EQUAL_INT64(-1, cb::chart_total(p, 32));
    TEST_ASSERT_EQUAL_INT64(-1, cb::chart_total(p, 0));

    cb::Provider empty{"x", "X", "", 0, nullptr, 0, nullptr, 0, nullptr, 0};
    TEST_ASSERT_EQUAL_INT64(-1, cb::chart_total(empty, 1));
}

void test_exposure_log_stays_in_the_panel(void) {
    cb::Canvas c = mks();
    const cb::Provider& p = cb::fixture_snapshot().providers[0];
    cb::draw_exposure_log(c, cb::LOG_X, cb::LOG_Y, cb::LOG_W, p);
    TEST_ASSERT_TRUE(lit_in(c, cb::LOG_X, cb::PANEL_Y, cb::LOG_W, cb::PANEL_H) > 200);
    // Nothing left of the log's column, nothing past the panel's right border,
    // and nothing above or below the panel.
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, 0, cb::LOG_X, cb::SCREEN_H));
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, cb::LOG_X + cb::LOG_W, 0,
                                    cb::SCREEN_W - cb::LOG_X - cb::LOG_W, cb::SCREEN_H));
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, 0, cb::SCREEN_W, cb::PANEL_Y));
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, cb::PANEL_Y + cb::PANEL_H, cb::SCREEN_W,
                                    cb::SCREEN_H - cb::PANEL_Y - cb::PANEL_H));
}

void test_a_provider_with_no_text_still_draws_the_log(void) {
    cb::Canvas c = mks();
    // Chart but no text: the TOK column is derived from the chart and stands
    // on its own, so it must still be there with the caps column blank.
    cb::Provider p = cb::fixture_snapshot().providers[0];
    p.text = nullptr; p.text_count = 0;
    cb::draw_exposure_log(c, cb::LOG_X, cb::LOG_Y, cb::LOG_W, p);
    const int tok_r = cb::LOG_TOK_R;
    const int caps_w = cb::LOG_X + cb::LOG_W - tok_r;
    // mks() hands out the one shared buffer, so read both counts off this
    // render before starting the next one.
    const int rads_no_text = lit_in(c, cb::LOG_X, cb::PANEL_Y, tok_r - cb::LOG_X, cb::PANEL_H);
    const int caps_no_text = lit_in(c, tok_r, cb::PANEL_Y, caps_w, cb::PANEL_H);

    cb::Canvas full = mks();
    cb::draw_exposure_log(full, cb::LOG_X, cb::LOG_Y, cb::LOG_W,
                          cb::fixture_snapshot().providers[0]);
    const int rads_with_text = lit_in(full, cb::LOG_X, cb::PANEL_Y, tok_r - cb::LOG_X, cb::PANEL_H);
    const int caps_with_text = lit_in(full, tok_r, cb::PANEL_Y, caps_w, cb::PANEL_H);

    // The tok column comes from the chart and does not care about text at all.
    TEST_ASSERT_TRUE(rads_no_text > 100);
    TEST_ASSERT_EQUAL_INT(rads_with_text, rads_no_text);

    // The caps column keeps its header and says "--" rather than inventing
    // figures, so it is still lit but far emptier than one carrying numbers.
    TEST_ASSERT_TRUE(caps_no_text > 0);
    TEST_ASSERT_TRUE(caps_no_text * 2 < caps_with_text);
}

// --- caption ----------------------------------------------------------------

void test_worst_pace_reports_the_window_in_the_most_trouble(void) {
    const cb::ProgressLine burning[] = {
        {"SESSION", 5,   100, REF + 4 * HOUR,  5 * HOUR},
        {"WEEKLY",  100, 100, REF + 40 * HOUR, 168 * HOUR},
    };
    cb::Provider p{"x", "X", "", REF, burning, 2, nullptr, 0, nullptr, 0};
    TEST_ASSERT_EQUAL(cb::PaceState::Burnout, cb::worst_pace(p, REF));
}

void test_windows_tied_on_severity_resolve_to_the_first(void) {
    // A session block that has not started sits beside a comfortable weekly.
    // Both are severity zero; the session is the newsworthy one and comes
    // first, so that is what gets reported.
    const cb::ProgressLine tied[] = {
        {"SESSION", 0,  100, 0,               5 * HOUR},
        {"WEEKLY",  10, 100, REF + 40 * HOUR, 168 * HOUR},
    };
    cb::Provider p{"x", "X", "", REF, tied, 2, nullptr, 0, nullptr, 0};
    TEST_ASSERT_EQUAL(cb::PaceState::Ready, cb::worst_pace(p, REF));
}

void test_nothing_readable_is_an_unknown_pace(void) {
    const cb::ProgressLine broken[] = {{"X", 12, 0, REF + HOUR, 2 * HOUR}};
    cb::Provider p{"x", "X", "", REF, broken, 1, nullptr, 0, nullptr, 0};
    TEST_ASSERT_EQUAL(cb::PaceState::Unknown, cb::worst_pace(p, REF));
    cb::Provider none{"x", "X", "", REF, nullptr, 0, nullptr, 0, nullptr, 0};
    TEST_ASSERT_EQUAL(cb::PaceState::Unknown, cb::worst_pace(none, REF));
}

void test_a_stale_reading_gets_a_remark_about_the_link(void) {
    // Whatever the pace was when these numbers were fetched, saying it in the
    // present tense over hour-old figures would be a lie.
    TEST_ASSERT_EQUAL_STRING("AWAITING TELEMETRY",
                             cb::vault_caption(cb::Freshness::Stale, cb::PaceState::Surplus));
    TEST_ASSERT_EQUAL_STRING("TELEMETRY LINK SEVERED",
                             cb::vault_caption(cb::Freshness::SignalLost, cb::PaceState::Burnout));
}

void test_every_pace_has_something_to_say(void) {
    const cb::PaceState all[] = {cb::PaceState::Surplus, cb::PaceState::OnPace,
                                 cb::PaceState::Burnout, cb::PaceState::Unknown,
                                 cb::PaceState::Ready};
    const char* seen[5];
    for (int i = 0; i < 5; i++) {
        seen[i] = cb::vault_caption(cb::Freshness::Fresh, all[i]);
        TEST_ASSERT_NOT_NULL(seen[i]);
        TEST_ASSERT_TRUE(strlen(seen[i]) > 0);
        for (int j = 0; j < i; j++) TEST_ASSERT_TRUE(strcmp(seen[i], seen[j]) != 0);
    }
}

void test_no_caption_collides_with_the_longest_annotation(void) {
    char note[24];
    cb::format_staleness(cb::Freshness::SignalLost, 500LL * 86400 * 1000, note, sizeof note);
    const int right = cb::text_width(note, 1);
    const cb::Freshness fs[] = {cb::Freshness::Fresh, cb::Freshness::Stale,
                                cb::Freshness::SignalLost};
    const cb::PaceState ps[] = {cb::PaceState::Surplus, cb::PaceState::OnPace,
                                cb::PaceState::Burnout, cb::PaceState::Unknown,
                                cb::PaceState::Ready};
    for (cb::Freshness f : fs)
        for (cb::PaceState s : ps) {
            const int left = cb::text_width(cb::vault_caption(f, s), 1);
            TEST_ASSERT_TRUE(left + 6 + right <= cb::SCREEN_W - 2 * cb::MARGIN);
        }
}

void test_burnout_is_the_only_caption_drawn_bright(void) {
    cb::Canvas c = mks();
    const cb::ProgressLine burning[] = {{"SESSION", 100, 100, REF + 4 * HOUR, 5 * HOUR}};
    cb::Provider prov{"claude", "CLAUDE", "", REF, burning, 1, nullptr, 0, nullptr, 0};
    cb::UsageSnapshot snap{&prov, 1, REF, 0};
    cb::render_ambient(c, snap, 0, REF, "14:44");
    const long hot = sum_in(c, cb::MARGIN, cb::FOOT_Y, cb::SCREEN_W / 2, cb::FONT_H);

    cb::Canvas d = mks();
    const cb::ProgressLine easy[] = {{"SESSION", 1, 100, REF + 4 * HOUR, 5 * HOUR}};
    cb::Provider prov2{"claude", "CLAUDE", "", REF, easy, 1, nullptr, 0, nullptr, 0};
    cb::UsageSnapshot snap2{&prov2, 1, REF, 0};
    cb::render_ambient(d, snap2, 0, REF, "14:44");
    const long calm = sum_in(d, cb::MARGIN, cb::FOOT_Y, cb::SCREEN_W / 2, cb::FONT_H);

    // Both captions are there; the one worth reading first is brighter, per
    // lit pixel, not merely longer.
    TEST_ASSERT_TRUE(hot > 0 && calm > 0);
    const int hot_len = (int)strlen(cb::vault_caption(cb::Freshness::Fresh, cb::PaceState::Burnout));
    const int calm_len = (int)strlen(cb::vault_caption(cb::Freshness::Fresh, cb::PaceState::Surplus));
    TEST_ASSERT_TRUE(hot / hot_len > calm / calm_len);
}

// --- tok meter --------------------------------------------------------------

// Where the needle's own pixels fall, clear of the arc and of the quarter
// ticks: the needle reaches r-7 from the pivot and the innermost tick starts
// at r-6, so these three points can only be lit by the needle.
static const int MET_CX = cb::METER_X + cb::METER_W / 2;
static const int MET_CY = cb::METER_Y + 44;

void test_the_needle_deflects_with_the_rate(void) {
    // Zero points left.
    cb::Canvas c = mks();
    cb::draw_tok_meter(c, cb::METER_X, cb::METER_Y, cb::METER_W, 0);
    TEST_ASSERT_EQUAL_UINT8(cb::I_BRIGHT, c.at(MET_CX - 21, MET_CY));
    TEST_ASSERT_EQUAL_UINT8(0, c.at(MET_CX, MET_CY - 20));
    TEST_ASSERT_EQUAL_UINT8(0, c.at(MET_CX + 22, MET_CY));

    // Half scale points straight up.
    cb::Canvas d = mks();
    cb::draw_tok_meter(d, cb::METER_X, cb::METER_Y, cb::METER_W, cb::TOK_FULL_SCALE / 2);
    TEST_ASSERT_EQUAL_UINT8(cb::I_BRIGHT, d.at(MET_CX, MET_CY - 20));
    TEST_ASSERT_EQUAL_UINT8(0, d.at(MET_CX - 21, MET_CY));

    // Full scale points right.
    cb::Canvas e = mks();
    cb::draw_tok_meter(e, cb::METER_X, cb::METER_Y, cb::METER_W, cb::TOK_FULL_SCALE);
    TEST_ASSERT_EQUAL_UINT8(cb::I_BRIGHT, e.at(MET_CX + 22, MET_CY));
    TEST_ASSERT_EQUAL_UINT8(0, e.at(MET_CX, MET_CY - 20));
}

void test_a_rate_past_full_scale_pins_rather_than_overshooting(void) {
    // The dial itself: everything above the readout, so the comparison is of
    // needles and not of how many digits each rate happens to print.
    const int dial_h = MET_CY + 3 - (cb::METER_Y);
    cb::Canvas c = mks();
    cb::draw_tok_meter(c, cb::METER_X, cb::METER_Y, cb::METER_W, cb::TOK_FULL_SCALE * 5);
    TEST_ASSERT_EQUAL_UINT8(cb::I_BRIGHT, c.at(MET_CX + 22, MET_CY));
    const int over = lit_in(c, cb::METER_X, cb::METER_Y, cb::METER_W, dial_h);

    cb::Canvas d = mks();
    cb::draw_tok_meter(d, cb::METER_X, cb::METER_Y, cb::METER_W, cb::TOK_FULL_SCALE);
    // Five times over the scale draws exactly what full scale draws: the
    // needle stops at the end of the dial instead of carrying on round it.
    TEST_ASSERT_EQUAL_INT(lit_in(d, cb::METER_X, cb::METER_Y, cb::METER_W, dial_h), over);
    // But the figure beside it still says what the rate actually was.
    char full[12], past[12];
    cb::format_tok(cb::TOK_FULL_SCALE, full, sizeof full);
    cb::format_tok(cb::TOK_FULL_SCALE * 5, past, sizeof past);
    TEST_ASSERT_TRUE(strcmp(full, past) != 0);
}

void test_an_unknown_rate_rests_dim_and_shows_no_figure(void) {
    cb::Canvas c = mks();
    cb::draw_tok_meter(c, cb::METER_X, cb::METER_Y, cb::METER_W, -1);
    // At rest, like zero -- but dim, because "no reading yet" and "nothing
    // being consumed" are different claims and must not look the same.
    TEST_ASSERT_EQUAL_UINT8(cb::I_DIM, c.at(MET_CX - 21, MET_CY));

    const int unknown_readout = lit_in(c, cb::METER_X, cb::METER_Y + 48, cb::METER_W, cb::FONT_H);
    cb::Canvas d = mks();
    cb::draw_tok_meter(d, cb::METER_X, cb::METER_Y, cb::METER_W, 15000000);
    const int known_readout = lit_in(d, cb::METER_X, cb::METER_Y + 48, cb::METER_W, cb::FONT_H);
    TEST_ASSERT_TRUE(unknown_readout > 0);          // "--", not a blank
    TEST_ASSERT_TRUE(unknown_readout * 2 < known_readout);
}

void test_the_meter_stays_in_its_third_of_the_panel(void) {
    cb::Canvas c = mks();
    cb::draw_tok_meter(c, cb::METER_X, cb::METER_Y, cb::METER_W, cb::TOK_FULL_SCALE / 3);
    TEST_ASSERT_TRUE(lit_in(c, cb::METER_X, cb::PANEL_Y, cb::METER_W, cb::PANEL_H) > 100);
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, 0, cb::METER_X, cb::SCREEN_H));
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, cb::METER_X + cb::METER_W, 0,
                                    cb::SCREEN_W - cb::METER_X - cb::METER_W, cb::SCREEN_H));
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, 0, cb::SCREEN_W, cb::PANEL_Y));
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, cb::PANEL_Y + cb::PANEL_H, cb::SCREEN_W,
                                    cb::SCREEN_H - cb::PANEL_Y - cb::PANEL_H));
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
    TEST_ASSERT_EQUAL_INT(0, lit_in(c, 0, cb::HERO_Y, cb::SCREEN_W, cb::HERO_H));
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
    RUN_TEST(test_window_badge_splits_on_a_day);
    RUN_TEST(test_hero_width_fits_two_side_by_side);
    RUN_TEST(test_windows_draw_inside_their_bands);
    RUN_TEST(test_a_window_that_does_not_fit_is_counted_not_dropped);
    RUN_TEST(test_unknown_state_draws_no_verdict);
    RUN_TEST(test_gauge_bar_length_tracks_remaining);
    RUN_TEST(test_strip_bar_length_tracks_remaining);
    RUN_TEST(test_chart_draws_in_its_band);
    RUN_TEST(test_tallest_bar_is_the_peak_day);
    RUN_TEST(test_empty_chart_does_not_crash);
    RUN_TEST(test_render_ambient_fills_all_bands);
    RUN_TEST(test_render_ambient_with_bad_index_is_safe);
    RUN_TEST(test_verdict_and_countdown_stay_inside_the_hero);
    RUN_TEST(test_invalid_pace_draws_no_gauge);
    RUN_TEST(test_unknown_state_draws_no_tick);
    RUN_TEST(test_format_tok_abbreviates_with_one_decimal);
    RUN_TEST(test_parse_caps_reads_the_dollar_figure);
    RUN_TEST(test_format_caps_groups_thousands);
    RUN_TEST(test_chart_total_sums_the_tail);
    RUN_TEST(test_exposure_log_stays_in_the_panel);
    RUN_TEST(test_a_provider_with_no_text_still_draws_the_log);
    RUN_TEST(test_worst_pace_reports_the_window_in_the_most_trouble);
    RUN_TEST(test_windows_tied_on_severity_resolve_to_the_first);
    RUN_TEST(test_nothing_readable_is_an_unknown_pace);
    RUN_TEST(test_a_stale_reading_gets_a_remark_about_the_link);
    RUN_TEST(test_every_pace_has_something_to_say);
    RUN_TEST(test_no_caption_collides_with_the_longest_annotation);
    RUN_TEST(test_burnout_is_the_only_caption_drawn_bright);
    RUN_TEST(test_the_needle_deflects_with_the_rate);
    RUN_TEST(test_a_rate_past_full_scale_pins_rather_than_overshooting);
    RUN_TEST(test_an_unknown_rate_rests_dim_and_shows_no_figure);
    RUN_TEST(test_the_meter_stays_in_its_third_of_the_panel);
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
