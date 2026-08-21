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

// The clock string is a literal, not derived from clock_string(): localtime_r
// would render a different hour on a machine in another timezone, and the
// golden would stop matching for a reason that has nothing to do with the
// renderer. The golden test passes the same literal.
static void render_reference(cb::Canvas& c, const cb::EffectParams& fx) {
    memset(c.data(), 0, static_cast<size_t>(c.width()) * c.height());
    cb::render_ambient(c, cb::fixture_snapshot(), 0, cb::FIXTURE_REFERENCE_MS, "14:44");
    cb::post_process(c, fx, 0, g_ring, sizeof g_ring);
}

static int run_png() {
    cb::Canvas c(g_buf, cb::SCREEN_W, cb::SCREEN_H);
    render_reference(c, cb::EffectParams::defaults());
    if (!cbhost::write_png_from_canvas("out/ambient.png", c)) {
        fprintf(stderr, "write failed -- does out/ exist?\n");
        return 1;
    }
    printf("wrote out/ambient.png\n");
    return 0;
}

// Not run in this task -- effect defaults (bloom_strength in particular) are
// about to change based on a look at the real panel, and blessing now would
// freeze a look we already know is wrong. A human picks values first via
// --contact, then blesses.
static int run_bless() {
    cb::Canvas c(g_buf, cb::SCREEN_W, cb::SCREEN_H);
    render_reference(c, cb::EffectParams::defaults());
    FILE* f = fopen("goldens/ambient-claude.raw", "wb");
    if (!f) { fprintf(stderr, "cannot write goldens/ -- does it exist?\n"); return 1; }
    fwrite(c.data(), 1, static_cast<size_t>(cb::SCREEN_W) * cb::SCREEN_H, f);
    fclose(f);
    cbhost::write_png_from_canvas("goldens/ambient-claude.png", c);
    printf("blessed goldens/ambient-claude.raw\n");
    return 0;
}

// Sweeps one EffectParams field across nine values (i*step for i in 0..8)
// and tiles the results 3x3 into a 960x720 sheet, labeling each tile with
// its parameter value so a tile maps back to a number. post_process mutates
// the canvas it is given, so every tile is rendered fresh via
// render_reference() rather than reused from a prior tile.
static int render_sweep_sheet(const char* path, const char* label_prefix,
                              uint8_t cb::EffectParams::* field, uint8_t step) {
    const int TW = cb::SCREEN_W * 3, TH = cb::SCREEN_H * 3;
    static uint8_t sheet[static_cast<size_t>(cb::SCREEN_W) * 3 * cb::SCREEN_H * 3];
    memset(sheet, 0, sizeof sheet);

    for (int i = 0; i < 9; i++) {
        cb::EffectParams fx = cb::EffectParams::defaults();
        const uint8_t value = static_cast<uint8_t>(i * step);
        fx.*field = value;

        cb::Canvas tile(g_buf, cb::SCREEN_W, cb::SCREEN_H);
        render_reference(tile, fx);

        // Labeled after post_process so the label stays crisp regardless of
        // scanline/bloom settings, in the chart band's dark top-right corner
        // (the "DAILY CONSUMPTION - 30D" title occupies the top-left; bars
        // start below CHART_Y+14).
        char label[24];
        snprintf(label, sizeof label, "%s=%d", label_prefix, value);
        const int lw = cb::text_width(label, 1);
        const int lx = cb::MARGIN + (cb::SCREEN_W - 2 * cb::MARGIN) - 3 - lw;
        cb::draw_text(tile, lx, cb::CHART_Y + 3, label, 255, 1);

        const int ox = (i % 3) * cb::SCREEN_W, oy = (i / 3) * cb::SCREEN_H;
        for (int y = 0; y < cb::SCREEN_H; y++)
            memcpy(sheet + static_cast<size_t>(oy + y) * TW + ox,
                   g_buf + static_cast<size_t>(y) * cb::SCREEN_W, cb::SCREEN_W);
    }

    cb::Canvas s(sheet, TW, TH);
    if (!cbhost::write_png_from_canvas(path, s)) return 1;
    printf("wrote %s\n", path);
    return 0;
}

static int run_contact() {
    int rc = 0;
    // The important one: is bloom invisible, or does it run away?
    rc |= render_sweep_sheet("out/contact-bloom.png", "BLOOM",
                             &cb::EffectParams::bloom_strength, 31);
    rc |= render_sweep_sheet("out/contact-scanlines.png", "SCAN",
                             &cb::EffectParams::scanline_depth, 14);
    return rc;
}

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "--png";
    if (!strcmp(mode, "--png"))     return run_png();
    if (!strcmp(mode, "--bless"))   return run_bless();
    if (!strcmp(mode, "--contact")) return run_contact();
    fprintf(stderr, "usage: program [--png|--bless|--contact]\n");
    return 2;
}
#endif  // PIO_UNIT_TESTING
