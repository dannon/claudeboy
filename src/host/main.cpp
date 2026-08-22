#include <stdio.h>
#include <string.h>
#include <time.h>
#include "core/canvas.h"
#include "core/crt.h"
#include "core/fixture.h"
#include "core/frame.h"
#include "core/screen.h"
#include "core/types.h"
#include "host/png.h"
#include "host/tui.h"

// test_build_src pulls src/host/ into every test binary too, which already
// supplies its own main() via Unity -- keep this one out of that build.
#ifndef PIO_UNIT_TESTING
static uint8_t g_accum[cb::SCREEN_W * cb::SCREEN_H];   // the accumulator
static uint8_t g_shown[cb::SCREEN_W * cb::SCREEN_H];   // post-processed output
static uint8_t g_ring[9 * cb::SCREEN_W];

// The clock string is a literal, not derived from clock_string(): localtime_r
// would render a different hour on a machine in another timezone, and the
// golden would stop matching for a reason that has nothing to do with the
// renderer. The golden test passes the same literal.
//
// Frame 0 from a cleared accumulator, which is what every still image here
// wants. render_frame() owns the decay/draw/copy/post-process order.
static void render_reference(cb::Canvas& out, const cb::EffectParams& fx,
                             cb::Page page = cb::Page::Stat) {
    memset(g_accum, 0, sizeof g_accum);
    cb::Canvas accum(g_accum, cb::SCREEN_W, cb::SCREEN_H);
    cb::render_frame(accum, out, cb::fixture_snapshot(), 0, cb::FIXTURE_REFERENCE_MS,
                     "14:44", fx, 0, g_ring, sizeof g_ring, nullptr, -1, page);
}

// Both pages, because a change to one is usually a change to the other's
// spacing too and half a look is how the last layout regression shipped.
static int run_png() {
    cb::Canvas c(g_shown, cb::SCREEN_W, cb::SCREEN_H);
    struct Shot { cb::Page page; const char* path; };
    static const Shot kShots[] = {{cb::Page::Stat, "out/ambient.png"},
                                  {cb::Page::Data, "out/ambient-data.png"},
                                  {cb::Page::All,  "out/ambient-all.png"}};
    for (const Shot& s : kShots) {
        render_reference(c, cb::EffectParams::defaults(), s.page);
        if (!cbhost::write_png_from_canvas(s.path, c)) {
            fprintf(stderr, "write failed -- does out/ exist?\n");
            return 1;
        }
        printf("wrote %s\n", s.path);
    }
    return 0;
}

// Regenerates both goldens (and a .png each to eyeball them with). Run this
// deliberately, after a look at the result: the golden test treats whatever is
// in those files as the truth.
static int run_bless() {
    cb::Canvas c(g_shown, cb::SCREEN_W, cb::SCREEN_H);
    struct Shot { cb::Page page; const char* raw; const char* png; };
    static const Shot kShots[] = {
        {cb::Page::Stat, "goldens/ambient-claude.raw", "goldens/ambient-claude.png"},
        {cb::Page::Data, "goldens/ambient-data.raw",   "goldens/ambient-data.png"},
        {cb::Page::All,  "goldens/ambient-all.raw",    "goldens/ambient-all.png"},
    };
    for (const Shot& s : kShots) {
        render_reference(c, cb::EffectParams::defaults(), s.page);
        FILE* f = fopen(s.raw, "wb");
        if (!f) { fprintf(stderr, "cannot write goldens/ -- does it exist?\n"); return 1; }
        fwrite(c.data(), 1, static_cast<size_t>(cb::SCREEN_W) * cb::SCREEN_H, f);
        fclose(f);
        cbhost::write_png_from_canvas(s.png, c);
        printf("blessed %s\n", s.raw);
    }
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

        cb::Canvas tile(g_shown, cb::SCREEN_W, cb::SCREEN_H);
        render_reference(tile, fx);

        // Labeled after post_process so the label stays crisp regardless of
        // scanline/bloom settings, in the chart band's dark top-right corner
        // (the "DAILY CONSUMPTION" title occupies the top-left; bars
        // start below CHART_Y+14).
        char label[24];
        snprintf(label, sizeof label, "%s=%d", label_prefix, value);
        const int lw = cb::text_width(label, 1);
        const int lx = cb::MARGIN + (cb::SCREEN_W - 2 * cb::MARGIN) - 3 - lw;
        cb::draw_text(tile, lx, cb::CHART_Y + 3, label, 255, 1);

        const int ox = (i % 3) * cb::SCREEN_W, oy = (i / 3) * cb::SCREEN_H;
        for (int y = 0; y < cb::SCREEN_H; y++)
            memcpy(sheet + static_cast<size_t>(oy + y) * TW + ox,
                   g_shown + static_cast<size_t>(y) * cb::SCREEN_W, cb::SCREEN_W);
    }

    cb::Canvas s(sheet, TW, TH);
    if (!cbhost::write_png_from_canvas(path, s)) return 1;
    printf("wrote %s\n", path);
    return 0;
}

// bloom_strength alone saturates almost immediately (blur * strength / 255,
// clamped): a mostly-black screen with thin bright strokes stays dim under
// blur regardless of strength. bloom_radius controls how far light actually
// spreads, which is what reads as glow. This sheet varies both: radius by
// row (1, 2, 3), strength by column (80, 160, 240), and prints both the lit
// pixel count and the summed intensity per tile so a tile maps back to a
// pair of numbers and a brightness total.
static int run_contact_bloom_radius() {
    const int TW = cb::SCREEN_W * 3, TH = cb::SCREEN_H * 3;
    static uint8_t sheet[static_cast<size_t>(cb::SCREEN_W) * 3 * cb::SCREEN_H * 3];
    memset(sheet, 0, sizeof sheet);

    const uint8_t radii[3] = {1, 2, 3};
    const uint8_t strengths[3] = {80, 160, 240};

    printf("bloom radius x strength sweep (lit>=8 count / intensity sum):\n");
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            cb::EffectParams fx = cb::EffectParams::defaults();
            fx.bloom_radius = radii[row];
            fx.bloom_strength = strengths[col];

            cb::Canvas tile(g_shown, cb::SCREEN_W, cb::SCREEN_H);
            render_reference(tile, fx);

            uint64_t lit = 0, sum = 0;
            const size_t n = static_cast<size_t>(cb::SCREEN_W) * cb::SCREEN_H;
            for (size_t p = 0; p < n; p++) {
                const uint8_t v = g_shown[p];
                sum += v;
                if (v >= 8) lit++;
            }
            printf("  R=%d S=%d: lit=%llu sum=%llu\n", radii[row], strengths[col],
                   static_cast<unsigned long long>(lit), static_cast<unsigned long long>(sum));

            // Labeled after post_process (and after metrics are taken, so
            // the label's own pixels don't skew the counts) in the chart
            // band's dark top-right corner, same spot the other sheets use.
            char label[24];
            snprintf(label, sizeof label, "R=%d S=%d", radii[row], strengths[col]);
            const int lw = cb::text_width(label, 1);
            const int lx = cb::MARGIN + (cb::SCREEN_W - 2 * cb::MARGIN) - 3 - lw;
            cb::draw_text(tile, lx, cb::CHART_Y + 3, label, 255, 1);

            const int ox = col * cb::SCREEN_W, oy = row * cb::SCREEN_H;
            for (int y = 0; y < cb::SCREEN_H; y++)
                memcpy(sheet + static_cast<size_t>(oy + y) * TW + ox,
                       g_shown + static_cast<size_t>(y) * cb::SCREEN_W, cb::SCREEN_W);
        }
    }

    cb::Canvas s(sheet, TW, TH);
    if (!cbhost::write_png_from_canvas("out/contact-bloom-radius.png", s)) return 1;
    printf("wrote out/contact-bloom-radius.png\n");
    return 0;
}

static int run_contact() {
    int rc = 0;
    // The important one: is bloom invisible, or does it run away?
    rc |= render_sweep_sheet("out/contact-bloom.png", "BLOOM",
                             &cb::EffectParams::bloom_strength, 31);
    rc |= render_sweep_sheet("out/contact-scanlines.png", "SCAN",
                             &cb::EffectParams::scanline_depth, 14);
    rc |= run_contact_bloom_radius();
    return rc;
}

static void clock_string(int64_t now_ms, char* out, size_t n) {
    const time_t t = static_cast<time_t>(now_ms / 1000);
    struct tm tmv;
    localtime_r(&t, &tmv);
    snprintf(out, n, "%02d:%02d", tmv.tm_hour, tmv.tm_min);
}

static int64_t now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

// The only host mode that runs successive frames. A frame-over-frame bug
// (post_process output leaking back into the accumulator and compounding)
// would show up here and nowhere else in this file -- test_frame covers the
// same ground headlessly.
static int run_tui(bool live) {
    int cols = 0, rows = 0;
    if (!cbhost::tui_size(cols, rows)) { fprintf(stderr, "not a terminal\n"); return 1; }
    const int scale = cbhost::tui_scale(cols, rows);

    const cb::EffectParams fx = cb::EffectParams::defaults();
    memset(g_accum, 0, sizeof g_accum);
    cb::Canvas accum(g_accum, cb::SCREEN_W, cb::SCREEN_H);
    cb::Canvas out(g_shown, cb::SCREEN_W, cb::SCREEN_H);

    cbhost::tui_begin();
    for (uint32_t frame = 0; !cbhost::tui_interrupted(); frame++) {
        const int64_t t = live ? now_ms() : cb::FIXTURE_REFERENCE_MS;
        char clk[8]; clock_string(t, clk, sizeof clk);

        cb::render_frame(accum, out, cb::fixture_snapshot(), 0, t, clk, fx,
                         frame, g_ring, sizeof g_ring);

        cbhost::tui_draw(out, scale);
        if (cbhost::tui_interrupted()) break;
        struct timespec nap{0, 50 * 1000 * 1000};   // ~20fps
        nanosleep(&nap, nullptr);
    }
    cbhost::tui_end();
    return 0;
}

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "--png";
    if (!strcmp(mode, "--png"))        return run_png();
    if (!strcmp(mode, "--bless"))      return run_bless();
    if (!strcmp(mode, "--contact"))    return run_contact();
    if (!strcmp(mode, "--tui"))        return run_tui(true);
    if (!strcmp(mode, "--tui-static")) return run_tui(false);
    fprintf(stderr, "usage: program [--png|--bless|--contact|--tui|--tui-static]\n");
    return 2;
}
#endif  // PIO_UNIT_TESTING
