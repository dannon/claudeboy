#include "core/screen.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "core/fastdiv.h"
#include "core/vaultboy.h"

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
    // Centred over the WHOLE data region -- heroes, strip, chart and panel --
    // because that is the space the banner is standing in for. Deriving the
    // bottom from any one band means resizing that band quietly walks the
    // banner into its neighbour.
    const int data_bottom = PANEL_Y + PANEL_H;
    const int w = text_width(s, 2);
    draw_text(c, (SCREEN_W - w) / 2,
              HERO_Y + (data_bottom - HERO_Y - 2 * FONT_H) / 2, s, I_BRIGHT, 2);
}

}  // namespace

const char* window_badge(int64_t period_ms) {
    // A day is the dividing line: anything that comes back inside one is a
    // resource you spend and stop worrying about, anything longer is a wound.
    return (period_ms > 0 && period_ms <= 24LL * 3600 * 1000) ? "AP" : "HP";
}

int hero_width() { return (SCREEN_W - 2 * MARGIN - HERO_GAP) / 2; }

void draw_hero(Canvas& c, int x, int y, int w, const ProgressLine& line, const Pace& p) {
    c.rect(x, y, w, HERO_H, I_RULE);

    draw_text(c, x + 5, y + 5, window_badge(line.period_ms), I_BRIGHT, 2);
    draw_text(c, x + 5 + 2 * FONT_ADV * 2 + 4, y + 9, line.label, I_DIM, 1);

    // No period or no limit means there was never a reading to take. Drawing
    // "0%" over an empty bar would be indistinguishable from a window that is
    // genuinely spent, so say nothing instead.
    if (!p.valid) {
        draw_text(c, x + 5, y + 22, "--", I_DIM, 3);
        return;
    }

    char pct[8];
    snprintf(pct, sizeof pct, "%d%%", static_cast<int>(p.remaining_frac * 100.0f + 0.5f));
    draw_text(c, x + 5, y + 22, pct, I_NORMAL, 3);

    // Burnout gets a radiation trefoil in the corner the percent never reaches.
    // It is the one state worth spotting without reading anything.
    if (p.state == PaceState::Burnout) draw_radiation(c, x + w - 17, y + 32, 10, I_BRIGHT);

    const int bar_x = x + 5, bar_w = w - 10;

    // Pace tick: where remaining ought to sit if consumption were on budget.
    if (p.state != PaceState::Unknown && p.state != PaceState::Ready) {
        const float on_pace_remaining = 1.0f - p.elapsed_frac;
        int tx = bar_x + static_cast<int>(on_pace_remaining * (bar_w - 1));
        if (tx < bar_x) tx = bar_x;
        if (tx > bar_x + bar_w - 1) tx = bar_x + bar_w - 1;
        c.vline(tx, y + 44, 3, I_NORMAL);
    }

    // Drain bar.
    c.rect(bar_x, y + 47, bar_w, 9, I_RULE);
    int fill_w = static_cast<int>(p.remaining_frac * (bar_w - 2));
    if (fill_w < 0) fill_w = 0;
    if (fill_w > bar_w - 2) fill_w = bar_w - 2;
    if (fill_w > 0) c.fill(bar_x + 1, y + 48, fill_w, 7, I_NORMAL);

    const char* v = verdict_text(p.state);
    if (v) draw_text(c, bar_x, y + 58, v, verdict_intensity(p.state), 1);

    // The countdown rides the right edge. A window that has not started has
    // nothing to count down to, so it gets no number rather than a zero.
    if (p.state != PaceState::Ready) {
        char dur[12];
        format_duration(p.reset_in_ms, dur, sizeof dur);
        const int dw = text_width(dur, 1);
        if (bar_x + (v ? text_width(v, 1) : 0) + 6 + dw <= bar_x + bar_w)
            draw_text(c, bar_x + bar_w - dw, y + 58, dur, I_DIM, 1);
    }
}

void draw_strip(Canvas& c, int x, int y, int w, const ProgressLine& line, const Pace& p) {
    draw_text(c, x, y + 3, window_badge(line.period_ms), I_DIM, 1);
    draw_text(c, x + 3 * FONT_ADV, y + 3, line.label, I_DIM, 1);

    char pct[8];
    if (p.valid) snprintf(pct, sizeof pct, "%d%%", static_cast<int>(p.remaining_frac * 100.0f + 0.5f));
    else         snprintf(pct, sizeof pct, "--");
    const int pw = text_width(pct, 1);
    draw_text(c, x + w - pw, y + 3, pct, p.valid ? I_NORMAL : I_DIM, 1);

    // Between the label column and the percent, whatever is left.
    const int bar_x = x + 3 * FONT_ADV + 9 * FONT_ADV;
    const int bar_w = (x + w - pw - 6) - bar_x;
    if (bar_w < 8 || !p.valid) return;
    c.rect(bar_x, y + 2, bar_w, 8, I_RULE);
    int fill_w = static_cast<int>(p.remaining_frac * (bar_w - 2));
    if (fill_w < 0) fill_w = 0;
    if (fill_w > bar_w - 2) fill_w = bar_w - 2;
    if (fill_w > 0) c.fill(bar_x + 1, y + 3, fill_w, 6, I_NORMAL);
}

void draw_windows(Canvas& c, const Provider& prov, int64_t now_ms) {
    const int n = prov.progress_count;
    if (n <= 0 || !prov.progress) return;

    // Heroes are the first two windows in the order the provider sent them,
    // which is session then weekly. Sorting by period would put them in the
    // same order on today's data and reorder the screen the day it did not.
    const int heroes = n < 2 ? n : 2;
    const int w = heroes == 1 ? SCREEN_W - 2 * MARGIN : hero_width();
    for (int i = 0; i < heroes; i++) {
        const Pace p = compute_pace(prov.progress[i], now_ms);
        draw_hero(c, MARGIN + i * (w + HERO_GAP), HERO_Y, w, prov.progress[i], p);
    }

    const int strip_w = SCREEN_W - 2 * MARGIN;
    if (n > heroes) {
        const Pace p = compute_pace(prov.progress[heroes], now_ms);
        draw_strip(c, MARGIN, STRIP_Y, strip_w, prov.progress[heroes], p);
    }
    // Everything past the one strip row would land on the chart. Say how many
    // rather than pretend the provider only ever sends what fits.
    const int hidden = n - heroes - 1;
    if (hidden > 0) {
        char tag[8];
        snprintf(tag, sizeof tag, "+%d", hidden);
        draw_text(c, MARGIN + strip_w - text_width(tag, 1), STRIP_Y + 3, tag, I_DIM, 1);
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


void format_rads(int64_t v, char* out, size_t n) {
    if (!out || n == 0) return;
    if (v < 0) { snprintf(out, n, "--"); return; }
    struct Unit { int64_t div; char suffix; };
    static const Unit kUnits[] = {{1000000000LL, 'B'}, {1000000LL, 'M'}, {1000LL, 'K'}};
    for (const Unit& u : kUnits) {
        if (v < u.div) continue;
        // One decimal, by integer division: a float here would drag softfloat
        // formatting into a build that otherwise never needs it. Rounded, not
        // truncated, so the figure matches the one the provider prints.
        int64_t whole = v / u.div;
        int64_t frac  = ((v % u.div) * 10 + u.div / 2) / u.div;
        if (frac >= 10) { whole++; frac = 0; }
        snprintf(out, n, "%lld.%lld%c", (long long)whole, (long long)frac, u.suffix);
        return;
    }
    snprintf(out, n, "%lld", (long long)v);
}

int64_t parse_caps(const char* s) {
    if (!s) return -1;
    while (*s && *s != '$') s++;
    if (*s != '$') return -1;
    s++;
    int64_t v = 0;
    bool any = false;
    // Thousands separators are part of the figure; the decimal point ends it.
    for (; *s; s++) {
        if (*s == ',') continue;
        if (*s < '0' || *s > '9') break;
        v = v * 10 + (*s - '0');
        any = true;
        if (v > 99999999LL) break;   // nothing sane gets here; stop before it wraps
    }
    return any ? v : -1;
}

void format_caps(int64_t v, char* out, size_t n) {
    if (!out || n == 0) return;
    if (v < 0) { snprintf(out, n, "--"); return; }
    char digits[24];
    const int len = snprintf(digits, sizeof digits, "%lld", (long long)v);
    size_t o = 0;
    for (int i = 0; i < len && o + 1 < n; i++) {
        if (i > 0 && (len - i) % 3 == 0) out[o++] = ',';
        if (o + 1 < n) out[o++] = digits[i];
    }
    out[o] = '\0';
}

int64_t chart_total(const Provider& prov, int days) {
    if (!prov.chart || prov.chart_count < days || days <= 0) return -1;
    int64_t sum = 0;
    for (int i = prov.chart_count - days; i < prov.chart_count; i++) sum += prov.chart[i].value;
    return sum;
}

void draw_exposure_log(Canvas& c, int x, int y, int w, const Provider& prov) {
    (void)w;
    static const char* kRows[] = {"TODAY", "YSTRDY", "30 DAY"};
    // Row labels are ours, not the provider's, and the RADS beside them come
    // from the daily chart -- so that column cannot disagree with its label.
    // CAPS is read from prov.text by position, which is the one assumption
    // here: OpenUsage sends today, yesterday, then the thirty-day total. A
    // text array too short for a row simply leaves that row's caps blank.
    const int rads_r = x + 106, caps_r = x + w;

    draw_text(c, rads_r - text_width("RADS", 1), y, "RADS", I_DIM, 1);
    draw_text(c, caps_r - text_width("CAPS", 1), y, "CAPS", I_DIM, 1);

    for (int i = 0; i < 3; i++) {
        const int ry = y + 12 + i * 12;
        draw_text(c, x, ry, kRows[i], I_DIM, 1);

        int64_t tokens = -1;
        if (i == 0) tokens = chart_total(prov, 1);
        else if (i == 1) {
            const int64_t two = chart_total(prov, 2);
            const int64_t one = chart_total(prov, 1);
            if (two >= 0 && one >= 0) tokens = two - one;
        } else {
            tokens = chart_total(prov, 30);
        }
        char rads[12];
        format_rads(tokens, rads, sizeof rads);
        draw_text(c, rads_r - text_width(rads, 1), ry, rads,
                  tokens < 0 ? I_DIM : I_NORMAL, 1);

        const int64_t caps = (prov.text && i < prov.text_count) ? parse_caps(prov.text[i].value) : -1;
        char cs[16];
        format_caps(caps, cs, sizeof cs);
        draw_text(c, caps_r - text_width(cs, 1), ry, cs, caps < 0 ? I_DIM : I_NORMAL, 1);
    }
}

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

void draw_rad_meter(Canvas& c, int x, int y, int w, int64_t rads_per_hour) {
    const int cx = x + w / 2, cy = y + 44, r = 32;
    static const float kPi = 3.14159265f;

    const int title_w = text_width("RAD/HR", 1);
    draw_text(c, cx - title_w / 2, y + 3, "RAD/HR", I_DIM, 1);

    // The dial runs left to right: nothing at all on the left, full scale on
    // the right, so a rising rate sweeps the way a rising needle should.
    const int danger_deg = static_cast<int>((1.0f - RAD_DANGER_FRAC) * 180.0f);
    for (int deg = 0; deg <= 180; deg++) {
        const float a = deg * (kPi / 180.0f);
        const float ca = cosf(a), sa = sinf(a);
        const uint8_t v = deg <= danger_deg ? I_BRIGHT : I_RULE;
        for (int rr = r - 1; rr <= r; rr++)
            c.plot(cx + static_cast<int>(lroundf(rr * ca)),
                   cy - static_cast<int>(lroundf(rr * sa)), v);
    }

    // Quarter ticks. 180 degrees is zero and 0 is full scale, so these read
    // right to left as 0, 25, 50, 75, 100.
    for (int deg = 0; deg <= 180; deg += 45) {
        const float a = deg * (kPi / 180.0f);
        const float ca = cosf(a), sa = sinf(a);
        for (int rr = r - 6; rr < r - 1; rr++)
            c.plot(cx + static_cast<int>(lroundf(rr * ca)),
                   cy - static_cast<int>(lroundf(rr * sa)), I_NORMAL);
    }

    float frac = 0.0f;
    if (rads_per_hour > 0) {
        frac = static_cast<float>(rads_per_hour) / static_cast<float>(RAD_FULL_SCALE);
        if (frac > 1.0f) frac = 1.0f;
    }
    const float na = (1.0f - frac) * kPi;
    const float nca = cosf(na), nsa = sinf(na);
    const uint8_t nv = rads_per_hour < 0 ? I_DIM : I_BRIGHT;
    for (int t = 0; t <= r - 7; t++)
        c.plot(cx + static_cast<int>(lroundf(t * nca)),
               cy - static_cast<int>(lroundf(t * nsa)), nv);
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            if (dx * dx + dy * dy <= 4) c.plot(cx + dx, cy + dy, nv);

    char rate[12];
    format_rads(rads_per_hour, rate, sizeof rate);
    draw_text(c, cx - text_width(rate, 1) / 2, y + 48, rate,
              rads_per_hour < 0 ? I_DIM : I_NORMAL, 1);
}

void render_ambient(Canvas& c, const UsageSnapshot& snap, int provider_index,
                    int64_t now_ms, const char* clock, int64_t rads_per_hour) {
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
    draw_windows(c, prov, now_ms);
    draw_chart(c, prov);
    c.rect(MARGIN, PANEL_Y, SCREEN_W - 2 * MARGIN, PANEL_H, I_RULE);
    draw_exposure_log(c, LOG_X, PANEL_Y + 6, LOG_W, prov);
    draw_rad_meter(c, METER_X, PANEL_Y + 3, METER_W, rads_per_hour);
    // Bottom-anchored: he is a figure standing behind the panel edge, and a
    // figure floating in the middle of a box reads as a sticker.
    draw_vault_boy(c, BOY_X, PANEL_Y + PANEL_H - VB_H - 1, boy_mood(prov, now_ms));
    // Knock the numbers back before the footer goes on, so the annotation
    // saying how old they are ends up brighter than the numbers themselves.
    if (f != Freshness::Fresh) dim_band(c, HERO_Y, PANEL_Y + PANEL_H - HERO_Y, I_STALE_SCALE);

    // The exposure log carries the day's figures now, so the footer's left
    // half is free for something that is not another number.
    draw_footer(c, nullptr, annotated ? note : "WORKING");
}

}  // namespace cb
