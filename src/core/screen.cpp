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
    if (days > 0)       snprintf(out, n, "%lldd%lldh", (long long)days, (long long)hours);
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

}  // namespace cb
