#include "core/screen.h"
#include <stdio.h>
#include <string.h>

namespace cb {

void format_duration(int64_t ms, char* out, size_t n) {
    if (!out || n == 0) return;
    if (ms < 0) ms = 0;
    const int64_t total_min = ms / 60000;
    const int64_t days  = total_min / 1440;
    const int64_t hours = (total_min % 1440) / 60;
    const int64_t mins  = total_min % 60;
    if (days > 99)      snprintf(out, n, "99d+");
    else if (days > 0)  snprintf(out, n, "%lldd%lldh", (long long)days, (long long)hours);
    else if (hours > 0) snprintf(out, n, "%lldh%02lldm", (long long)hours, (long long)mins);
    else                snprintf(out, n, "%lldm", (long long)mins);
}

void format_clock(int64_t now_ms, char* out, size_t n) {
    if (!out || n == 0) return;
    if (now_ms < 0) now_ms = 0;
    const int64_t total_min = now_ms / 60000;
    snprintf(out, n, "%02d:%02d", static_cast<int>((total_min / 60) % 24),
             static_cast<int>(total_min % 60));
}

void draw_tabs(Canvas& c, const UsageSnapshot& snap, int active, const char* clock) {
    int x = MARGIN;
    for (int i = 0; snap.providers && i < snap.provider_count; i++) {
        const char* name = snap.providers[i].display_name;
        const int w = text_width(name, 1);
        // Inverse video would need a hole punched in a filled bar, which this
        // canvas cannot do -- plot() blends with max, so nothing can darken a
        // pixel once lit. Bright text plus an underline reads the same on a
        // phosphor screen and needs no special case.
        draw_text(c, x, 3, name, i == active ? I_BRIGHT : I_DIM, 1);
        if (i == active) c.hline(x, 3 + FONT_H + 1, w, I_BRIGHT);
        x += w + 12;
    }
    if (clock) draw_text(c, SCREEN_W - MARGIN - text_width(clock, 1), 3, clock, I_NORMAL, 1);
    c.hline(MARGIN, TAB_H, SCREEN_W - 2 * MARGIN, I_RULE);
}

void draw_footer(Canvas& c, const char* left, const char* right) {
    c.hline(MARGIN, FOOT_Y - 4, SCREEN_W - 2 * MARGIN, I_RULE);
    if (left)  draw_text(c, MARGIN, FOOT_Y, left, I_DIM, 1);
    if (right) draw_text(c, SCREEN_W - MARGIN - text_width(right, 1), FOOT_Y, right, I_NORMAL, 1);
}

namespace {

const char* verdict_text(PaceState s) {
    switch (s) {
        case PaceState::Surplus: return "SURPLUS";
        case PaceState::OnPace:  return "ON PACE";
        case PaceState::Burnout: return "BURNOUT";
        default:                 return nullptr;   // no verdict worth giving
    }
}

uint8_t verdict_intensity(PaceState s) {
    switch (s) {
        case PaceState::Surplus: return I_DIM;
        case PaceState::OnPace:  return I_NORMAL;
        case PaceState::Burnout: return I_BRIGHT;
        default:                 return I_DIM;
    }
}

}  // namespace

int cell_width(int count) {
    if (count < 1) count = 1;
    const int usable = SCREEN_W - 2 * MARGIN - (count - 1) * CELL_GAP;
    const int w = usable / count;
    return w < 1 ? 1 : w;   // usable goes negative long before count does
}

void draw_gauge_cell(Canvas& c, int x, int y, int w,
                     const ProgressLine& line, const Pace& p) {
    c.rect(x, y, w, CELL_H, I_RULE);

    draw_text(c, x + 3, y + 3, line.label, I_DIM, 1);

    // No period or no limit means there was never a reading to take. Drawing
    // "0%" over an empty bar would be indistinguishable from a window that is
    // genuinely spent, so say nothing instead: the label and a dim placeholder,
    // no tick, no bar, no verdict.
    if (!p.valid) {
        draw_text(c, x + 3, y + 13, "--", I_DIM, 2);
        return;
    }

    char pct[8];
    snprintf(pct, sizeof pct, "%d%%", static_cast<int>(p.remaining_frac * 100.0f + 0.5f));
    draw_text(c, x + 3, y + 13, pct, I_NORMAL, 2);

    // Pace tick: where remaining ought to sit if consumption were on budget.
    const int bar_x = x + 3, bar_w = w - 6;
    if (p.state != PaceState::Unknown) {
        const float on_pace_remaining = 1.0f - p.elapsed_frac;
        int tx = bar_x + static_cast<int>(on_pace_remaining * (bar_w - 1));
        if (tx < bar_x) tx = bar_x;
        if (tx > bar_x + bar_w - 1) tx = bar_x + bar_w - 1;
        c.vline(tx, y + 31, 4, I_NORMAL);
        c.plot(tx - 1, y + 31, I_DIM);
        c.plot(tx + 1, y + 31, I_DIM);
    }

    // Drain bar.
    c.rect(bar_x, y + 36, bar_w, 8, I_RULE);
    int fill_w = static_cast<int>(p.remaining_frac * (bar_w - 2));
    if (fill_w < 0) fill_w = 0;
    if (fill_w > bar_w - 2) fill_w = bar_w - 2;
    if (fill_w > 0) c.fill(bar_x + 1, y + 37, fill_w, 6, I_NORMAL);

    const char* v = verdict_text(p.state);
    if (v) {
        char row[24];
        char dur[12];
        format_duration(p.reset_in_ms, dur, sizeof dur);
        snprintf(row, sizeof row, "%s %s", v, dur);
        // A 4-cell layout is narrower than the verdict-plus-duration text can
        // fit; drop the duration and keep the verdict, which is the part
        // read at a glance.
        if (text_width(row, 1) > w - 6) snprintf(row, sizeof row, "%s", v);
        draw_text(c, x + 3, y + 48, row, verdict_intensity(p.state), 1);
    }
}

void draw_cells(Canvas& c, const Provider& prov, int64_t now_ms) {
    const int n = prov.progress_count;
    if (n <= 0 || !prov.progress) return;
    const int w = cell_width(n);
    for (int i = 0; i < n; i++) {
        const int x = MARGIN + i * (w + CELL_GAP);
        const Pace p = compute_pace(prov.progress[i], now_ms);
        draw_gauge_cell(c, x, CELL_Y, w, prov.progress[i], p);
    }
}

void draw_chart(Canvas& c, const Provider& prov) {
    const int n = prov.chart_count;
    if (n <= 0 || !prov.chart) return;

    const int x0 = MARGIN, w = SCREEN_W - 2 * MARGIN;
    c.rect(x0, CHART_Y, w, CHART_H, I_RULE);
    // The window is whatever the provider actually sent, not a fixed 30 days.
    char title[32];
    snprintf(title, sizeof title, "DAILY CONSUMPTION - %dD", n);
    draw_text(c, x0 + 3, CHART_Y + 3, title, I_DIM, 1);

    int64_t peak = 1;
    for (int i = 0; i < n; i++) if (prov.chart[i].value > peak) peak = prov.chart[i].value;

    const int plot_y = CHART_Y + 14;
    const int plot_h = CHART_H - 18;
    const int inner_w = w - 6;
    const int gap = (n > 40) ? 0 : 1;
    int bw = (inner_w - (n - 1) * gap) / n;
    if (bw < 1) bw = 1;

    for (int i = 0; i < n; i++) {
        int bh = static_cast<int>((prov.chart[i].value * plot_h) / peak);
        if (bh < 1 && prov.chart[i].value > 0) bh = 1;
        const int bx = x0 + 3 + i * (bw + gap);
        // The last point is today, still filling: draw it dim so it does not
        // read as a finished day.
        const uint8_t v = (i == n - 1) ? I_DIM : I_NORMAL;
        if (bh > 0) c.fill(bx, plot_y + plot_h - bh, bw, bh, v);
    }
}

void render_ambient(Canvas& c, const UsageSnapshot& snap, int provider_index,
                    int64_t now_ms, const char* clock) {
    draw_tabs(c, snap, provider_index, clock);
    if (!snap.providers || provider_index < 0 || provider_index >= snap.provider_count) return;

    const Provider& prov = snap.providers[provider_index];
    draw_cells(c, prov, now_ms);
    draw_chart(c, prov);

    const char* left = (prov.text && prov.text_count > 0) ? prov.text[0].value : "";
    draw_footer(c, left, "WORKING");
}

}  // namespace cb
