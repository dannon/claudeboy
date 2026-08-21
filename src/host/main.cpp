#include <stdio.h>
#include "core/canvas.h"
#include "core/font.h"
#include "core/types.h"
#include "host/png.h"

// test_build_src pulls src/host/ into every test binary too, which already
// supplies its own main() via Unity -- keep this one out of that build.
#ifndef PIO_UNIT_TESTING
static uint8_t g_buf[cb::SCREEN_W * cb::SCREEN_H];

int main(int, char**) {
    cb::Canvas c(g_buf, cb::SCREEN_W, cb::SCREEN_H);
    c.clear(0);

    // An intensity ramp, so the palette curve is visible.
    for (int x = 0; x < cb::SCREEN_W; x++)
        c.fill(x, 200, 1, 30, uint8_t(x * 255 / (cb::SCREEN_W - 1)));

    c.rect(4, 4, cb::SCREEN_W - 8, cb::SCREEN_H - 8, 120);
    cb::draw_text(c, 12, 20, "CLAUDEBOY 3000", 255, 2);
    cb::draw_text(c, 12, 48, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 200, 1);
    cb::draw_text(c, 12, 60, "0123456789 %.:-/", 200, 1);

    if (!cbhost::write_png_from_canvas("out/smoke.png", c)) {
        fprintf(stderr, "write failed -- does out/ exist?\n");
        return 1;
    }
    printf("wrote out/smoke.png\n");
    return 0;
}
#endif  // PIO_UNIT_TESTING
