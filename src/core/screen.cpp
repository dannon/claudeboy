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

void draw_tabs(Canvas& c, const UsageSnapshot& snap, int active, const char* clock) {
    int x = MARGIN;
    for (int i = 0; i < snap.provider_count; i++) {
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
    return usable / count;
}

void draw_gauge_cell(Canvas& c, int x, int y, int w,
                     const ProgressLine& line, const Pace& p) {
    c.rect(x, y, w, CELL_H, I_RULE);

    draw_text(c, x + 3, y + 3, line.label, I_DIM, 1);

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
        draw_text(c, x + 3, y + 48, row, verdict_intensity(p.state), 1);
    }
}

void draw_cells(Canvas& c, const Provider& prov, int64_t now_ms) {
    const int n = prov.progress_count;
    if (n <= 0) return;
    const int w = cell_width(n);
    for (int i = 0; i < n; i++) {
        const int x = MARGIN + i * (w + CELL_GAP);
        const Pace p = compute_pace(prov.progress[i], now_ms);
        draw_gauge_cell(c, x, CELL_Y, w, prov.progress[i], p);
    }
}

}  // namespace cb
