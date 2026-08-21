#include "host/tui.h"
#include "core/palette.h"
#include "core/types.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string>

namespace cbhost {
namespace {

volatile sig_atomic_t g_stop = 0;
void on_sigint(int) { g_stop = 1; }

// Average a scale x scale block so downsampling keeps thin strokes visible.
uint8_t sample(const cb::Canvas& c, int px, int py, int scale) {
    uint32_t acc = 0; int n = 0;
    for (int dy = 0; dy < scale; dy++)
        for (int dx = 0; dx < scale; dx++) {
            const int x = px + dx, y = py + dy;
            if (x < c.width() && y < c.height()) { acc += c.at(x, y); n++; }
        }
    return n ? static_cast<uint8_t>(acc / n) : 0;
}

}  // namespace

bool tui_size(int& cols, int& rows) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0 || ws.ws_col == 0) return false;
    cols = ws.ws_col; rows = ws.ws_row;
    return true;
}

int tui_scale(int cols, int rows) {
    if (cols <= 0 || rows <= 0) return 2;
    int s = 1;
    while ((cb::SCREEN_W / s) > cols || (cb::SCREEN_H / s) > (rows - 1) * 2) s++;
    return s;
}

void tui_begin() {
    signal(SIGINT, on_sigint);
    fputs("\x1b[?25l", stdout);   // hide cursor
    fflush(stdout);
}

void tui_end() {
    fputs("\x1b[0m\x1b[?25h\n", stdout);
    fflush(stdout);
}

bool tui_interrupted() { return g_stop != 0; }

void tui_draw(const cb::Canvas& c, int scale) {
    if (scale < 1) scale = 1;
    const int out_w = c.width() / scale;
    const int out_h = c.height() / scale;

    std::string buf;
    buf.reserve(static_cast<size_t>(out_w) * (out_h / 2 + 1) * 40);
    buf += "\x1b[H";   // home, without clearing, to avoid flicker

    char cell[48];
    for (int oy = 0; oy + 1 < out_h; oy += 2) {
        int last_fg = -1, last_bg = -1;
        for (int ox = 0; ox < out_w; ox++) {
            const cb::Rgb top = cb::palette_rgb(sample(c, ox * scale, oy * scale, scale));
            const cb::Rgb bot = cb::palette_rgb(sample(c, ox * scale, (oy + 1) * scale, scale));
            const int fg = (top.r << 16) | (top.g << 8) | top.b;
            const int bg = (bot.r << 16) | (bot.g << 8) | bot.b;
            if (fg != last_fg || bg != last_bg) {
                snprintf(cell, sizeof cell, "\x1b[38;2;%d;%d;%dm\x1b[48;2;%d;%d;%dm",
                         top.r, top.g, top.b, bot.r, bot.g, bot.b);
                buf += cell;
                last_fg = fg; last_bg = bg;
            }
            buf += "\xe2\x96\x80";   // U+2580 UPPER HALF BLOCK
        }
        buf += "\x1b[0m\n";
    }
    fwrite(buf.data(), 1, buf.size(), stdout);
    fflush(stdout);
}

}  // namespace cbhost
