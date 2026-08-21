#include <stdio.h>
#include <string.h>
#include "core/canvas.h"
#include "core/crt.h"
#include "core/fixture.h"
#include "core/screen.h"
#include "core/types.h"
#include "host/png.h"

// test_build_src pulls src/host/ into every test binary too, which already
// supplies its own main() via Unity -- keep this one out of that build.
#ifndef PIO_UNIT_TESTING
static uint8_t g_buf[cb::SCREEN_W * cb::SCREEN_H];
static uint8_t g_ring[9 * cb::SCREEN_W];

int main(int, char**) {
    memset(g_buf, 0, sizeof g_buf);
    cb::Canvas c(g_buf, cb::SCREEN_W, cb::SCREEN_H);

    const cb::EffectParams fx = cb::EffectParams::defaults();
    cb::render_ambient(c, cb::fixture_snapshot(), 0, cb::FIXTURE_REFERENCE_MS, "14:44");
    cb::post_process(c, fx, 0, g_ring, sizeof g_ring);

    if (!cbhost::write_png_from_canvas("out/ambient.png", c)) {
        fprintf(stderr, "write failed -- does out/ exist?\n");
        return 1;
    }
    printf("wrote out/ambient.png\n");
    return 0;
}
#endif  // PIO_UNIT_TESTING
