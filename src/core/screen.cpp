#include "core/screen.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "core/fastdiv.h"

namespace cb {

int64_t newest_fetched_at_ms(const UsageSnapshot& snap) {
    int64_t newest = 0;
    for (int i = 0; snap.providers && i < snap.provider_count; i++)
        if (snap.providers[i].fetched_at_ms > newest) newest = snap.providers[i].fetched_at_ms;
    return newest;
}

int64_t snapshot_age_ms(const UsageSnapshot& snap, int64_t now_ms) {
    const int64_t newest = newest_fetched_at_ms(snap);
    if (newest <= 0) return 0;
    // Our clock is serverTime plus local millis, so it can sit a little behind
    // the instant a provider was read. That is not data from the future.
    const int64_t age = now_ms - newest;
    return age > 0 ? age : 0;
}

Freshness freshness_of(const UsageSnapshot& snap, int64_t now_ms) {
    if (newest_fetched_at_ms(snap) <= 0) return Freshness::NoSignal;
    const int64_t age = snapshot_age_ms(snap, now_ms);
    if (age < STALE_AFTER_MS) return Freshness::Fresh;
    if (age <= LOST_AFTER_MS)  return Freshness::Stale;
    return Freshness::SignalLost;
}

bool format_staleness(Freshness f, int64_t age_ms, char* out, size_t n) {
    if (!out || n == 0) return false;
    out[0] = '\0';
    if (f == Freshness::Fresh) return false;
    if (f == Freshness::NoSignal) { snprintf(out, n, "NO SIGNAL"); return true; }
    char dur[12];
    format_duration(age_ms, dur, sizeof dur);
    snprintf(out, n, "%s %s", f == Freshness::Stale ? "STALE" : "SIGNAL LOST", dur);
    return true;
}

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
    const int clock_x = SCREEN_W - MARGIN - (clock ? text_width(clock, 1) : 0);
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
        x += w;
        // The plan tier is the only thing on screen saying which account these
        // numbers belong to, so it rides with the provider it describes. The
        // provider list is server-side and can grow, so a plan with no room
        // left is dropped rather than drawn under the clock.
        const char* plan = snap.providers[i].plan;
        if (i == active && plan && plan[0]) {
            const int pw = text_width(plan, 1);
            if (x + 6 + pw < clock_x - 6) { draw_text(c, x + 6, 3, plan, I_DIM, 1); x += 6 + pw; }
        }
        x += 12;
    }
    if (clock) draw_text(c, clock_x, 3, clock, I_NORMAL, 1);
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
        case PaceState::Ready:   return "READY";
        default:                 return nullptr;   // no verdict worth giving
    }
}

uint8_t verdict_intensity(PaceState s) {
    switch (s) {
        case PaceState::Surplus: return I_DIM;
        case PaceState::OnPace:  return I_NORMAL;
        case PaceState::Burnout: return I_BRIGHT;
        case PaceState::Ready:   return I_DIM;
        default:                 return I_DIM;
    }
}

// Multiplicative, unlike Canvas::decay(): a constant subtract erases the faint
// parts of a picture and barely touches the bright ones, which reads as damage
// rather than as the same picture turned down.
//
// Run on the accumulator after drawing, this settles instead of compounding:
// the next frame's draw is a max blend, so every pixel is restored to full
// before this runs again.
void dim_band(Canvas& c, int y0, int h, uint8_t scale) {
    uint8_t* buf = c.data();
    if (!buf) return;
    if (y0 < 0) { h += y0; y0 = 0; }
    if (y0 + h > c.height()) h = c.height() - y0;
    for (int y = y0; y < y0 + h; y++) {
        uint8_t* row = buf + static_cast<size_t>(y) * c.width();
        for (int x = 0; x < c.width(); x++)
            row[x] = static_cast<uint8_t>(div255(static_cast<uint32_t>(row[x]) * scale));
    }
}

// Centred in the band the gauges and the chart would have filled.
void draw_banner(Canvas& c, const char* s) {
    // Centred over the WHOLE data region -- cells, chart and scope -- because
    // that is the space the banner is standing in for. Deriving the bottom from
    // the chart alone meant shrinking the chart quietly walked the banner up
    // into the gauge cells.
    const int data_bottom = SCOPE_Y + SCOPE_H;
    const int w = text_width(s, 2);
    draw_text(c, (SCREEN_W - w) / 2,
              CELL_Y + (data_bottom - CELL_Y - 2 * FONT_H) / 2, s, I_BRIGHT, 2);
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
    if (p.state != PaceState::Unknown && p.state != PaceState::Ready) {
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
        if (p.state == PaceState::Ready) {
            // No window running, so there is no countdown to show. "READY 0m"
            // would be as misleading as the SURPLUS 0m this replaces.
            snprintf(row, sizeof row, "%s", v);
        } else {
            char dur[12];
            format_duration(p.reset_in_ms, dur, sizeof dur);
            snprintf(row, sizeof row, "%s %s", v, dur);
            // A 4-cell layout is narrower than the verdict-plus-duration text
            // can fit; drop the duration and keep the verdict, which is the
            // part read at a glance.
            if (text_width(row, 1) > w - 6) snprintf(row, sizeof row, "%s", v);
        }
        // Burnout gets a radiation trefoil beside the words. It is the one
        // state worth spotting from across the room without reading anything.
        int text_x = x + 3;
        if (p.state == PaceState::Burnout) {
            draw_radiation(c, x + 8, y + 51, 5, I_BRIGHT);
            text_x = x + 16;
        }
        draw_text(c, text_x, y + 48, row, verdict_intensity(p.state), 1);
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
    if (prov.chart_count <= 0 || !prov.chart) return;

    // Draw the tail of whatever was sent. The provider still ships 31 days --
    // the wire did not change, only how much of it is worth the panel space.
    const int total = prov.chart_count;
    const int n = total < CHART_DAYS ? total : CHART_DAYS;
    const ChartPoint* pts = prov.chart + (total - n);

    const int x0 = MARGIN, w = SCREEN_W - 2 * MARGIN;
    c.rect(x0, CHART_Y, w, CHART_H, I_RULE);
    char title[32];
    snprintf(title, sizeof title, "CONSUMPTION - %dD", n);
    draw_text(c, x0 + 3, CHART_Y + 3, title, I_DIM, 1);

    int64_t peak = 1;
    for (int i = 0; i < n; i++) if (pts[i].value > peak) peak = pts[i].value;

    const int plot_y = CHART_Y + 14;
    const int plot_h = CHART_H - 18;
    const int inner_w = w - 6;
    const int gap = 2;   // wide bars now that there are only seven of them
    int bw = (inner_w - (n - 1) * gap) / n;
    if (bw < 1) bw = 1;

    for (int i = 0; i < n; i++) {
        int bh = static_cast<int>((pts[i].value * plot_h) / peak);
        if (bh < 1 && pts[i].value > 0) bh = 1;
        const int bx = x0 + 3 + i * (bw + gap);
        // The last point is today, still filling: draw it dim so it does not
        // read as a finished day.
        const uint8_t v = (i == n - 1) ? I_DIM : I_NORMAL;
        if (bh > 0) c.fill(bx, plot_y + plot_h - bh, bw, bh, v);
    }
}


// 64-entry quarter-symmetric sine, 0..255 amplitude. A table rather than sinf()
// because this runs 300+ times a frame on a chip whose FPU is not free, and
// because a table is exactly reproducible across host and device -- the golden
// depends on that.
static const uint8_t kSine[65] = {
    128,140,152,165,176,188,198,208,218,226,234,240,245,250,253,254,
    255,254,253,250,245,240,234,226,218,208,198,188,176,165,152,140,
    128,115,103, 90, 79, 67, 57, 47, 37, 29, 21, 15, 10,  5,  2,  1,
      0,  1,  2,  5, 10, 15, 21, 29, 37, 47, 57, 67, 79, 90,103,115,
    128
};

static int sine_at(uint32_t phase) { return kSine[phase & 63] - 128; }   // -128..127

void draw_radiation(Canvas& c, int cx, int cy, int r, uint8_t v) {
    // Three 60-degree blades separated by 60-degree gaps, plus a hub. Drawn by
    // testing each pixel rather than blitting a bitmap: a hand-pixelled trefoil
    // at this size reads as a smudge, and this stays crisp at any radius.
    //
    // atan2f rather than an integer angle approximation. This covers about 120
    // pixels per icon and at most one icon per cell, so the cost is nothing --
    // and the approximation this replaces produced lopsided, unevenly spaced
    // blades, which is worse than a smudge because it looks deliberate.
    const int hub = r / 3 > 0 ? r / 3 : 1;
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            const int d2 = dx * dx + dy * dy;
            if (d2 > r * r) continue;
            if (d2 <= hub * hub) { c.plot(cx + dx, cy + dy, v); continue; }
            if (d2 < (hub + 1) * (hub + 1) + 1) continue;    // clear ring round the hub
            float a = atan2f(static_cast<float>(-dy), static_cast<float>(dx));
            a = a * (180.0f / 3.14159265f) + 90.0f;          // one blade points up
            while (a < 0.0f) a += 360.0f;
            if (fmodf(a, 120.0f) < 60.0f) c.plot(cx + dx, cy + dy, v);
        }
    }
}

void draw_scope(Canvas& c, const Provider& prov, int64_t now_ms, uint32_t frame) {
    const int x0 = MARGIN, w = SCREEN_W - 2 * MARGIN;
    c.rect(x0, SCOPE_Y, w, SCOPE_H, I_RULE);

    // Agitation is the worst pace ratio across this provider's windows: a calm
    // line when nothing is being burned, a violent one when something is.
    float worst = 0.0f;
    for (int i = 0; i < prov.progress_count; i++) {
        const Pace p = compute_pace(prov.progress[i], now_ms);
        if (p.valid && p.state != PaceState::Ready && p.ratio > worst) worst = p.ratio;
    }
    if (worst > 2.0f) worst = 2.0f;

    const int mid = SCOPE_Y + SCOPE_H / 2;
    const int head = (SCOPE_H / 2) - 4;
    // Half scale at ratio 1.0, full at 2.0. Scaling straight off the ratio put
    // the trace at full deflection for perfectly ordinary usage, which left it
    // nothing to say when something actually went wrong.
    const int amp = 2 + static_cast<int>((worst * 0.5f) * (head - 2));

    const int ix = x0 + 2, iw = w - 4;
    int prev = mid;
    for (int i = 0; i < iw; i++) {
        // Two beats at different rates so it never looks like it is repeating,
        // plus a slow drift so the whole trace breathes.
        const uint32_t t = frame;
        int y = mid;
        y += (sine_at(i * 2 + t) * amp) / 256;
        y += (sine_at(i * 5 + t * 3) * amp) / 512;
        y += (sine_at(i + t / 2) * amp) / 640;
        if (y < SCOPE_Y + 2) y = SCOPE_Y + 2;
        if (y > SCOPE_Y + SCOPE_H - 3) y = SCOPE_Y + SCOPE_H - 3;
        if (i == 0) prev = y;
        const int top = y < prev ? y : prev;
        const int len = (y > prev ? y : prev) - top + 1;
        c.fill(ix + i, top, 1, len, I_NORMAL);
        prev = y;
    }
}

void render_ambient(Canvas& c, const UsageSnapshot& snap, int provider_index,
                    int64_t now_ms, const char* clock) {
    draw_tabs(c, snap, provider_index, clock);

    const Freshness f = freshness_of(snap, now_ms);
    char note[24];
    const bool annotated = format_staleness(f, snapshot_age_ms(snap, now_ms), note, sizeof note);

    // Nothing was ever fetched -- before the first poll of a boot, this is the
    // zeroed snapshot the board starts with. There are no numbers to show and
    // no age to put on them, so say that where the numbers would have been.
    // Checked before the index bounds because that snapshot has no providers
    // for an index to be valid against.
    if (f == Freshness::NoSignal) {
        draw_banner(c, note);
        draw_footer(c, nullptr, nullptr);
        return;
    }

    if (!snap.providers || provider_index < 0 || provider_index >= snap.provider_count) return;

    const Provider& prov = snap.providers[provider_index];
    draw_cells(c, prov, now_ms);
    draw_chart(c, prov);
    // Phase comes from the clock rather than a frame counter so render_ambient
    // keeps its signature and every caller stays unchanged -- and so the golden,
    // which renders at a fixed reference instant, stays reproducible.
    draw_scope(c, prov, now_ms, static_cast<uint32_t>((now_ms / 50) & 0xFFFFFFFF));
    // Knock the numbers back before the footer goes on, so the annotation
    // saying how old they are ends up brighter than the numbers themselves.
    if (f != Freshness::Fresh) dim_band(c, CELL_Y, CHART_Y + CHART_H - CELL_Y, I_STALE_SCALE);

    const char* left = (prov.text && prov.text_count > 0) ? prov.text[0].value : "";
    draw_footer(c, left, annotated ? note : "WORKING");
}

}  // namespace cb
