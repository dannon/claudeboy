// The board's main loop, turned inside out for the browser. device/main.cpp
// owns its loop and polls the transport; here the page owns the loop and the
// network, and calls in once per fetched reply, once per tap and once per
// frame. Everything between those calls is the same core the panel runs, fed
// the same way, so the page is the board rather than a drawing of it.
#include <emscripten/emscripten.h>
#include <string.h>
#include "core/burn.h"
#include "core/canvas.h"
#include "core/clock.h"
#include "core/crt.h"
#include "core/fixture.h"
#include "core/frame.h"
#include "core/palette.h"
#include "core/screen.h"
#include "core/store.h"
#include "core/types.h"
#include "core/view.h"

static uint8_t g_buf[cb::SCREEN_W * cb::SCREEN_H];   // the accumulator
static uint8_t g_ring[9 * cb::SCREEN_W];
static uint8_t g_out_row[cb::SCREEN_W];
static uint32_t g_rgba[cb::SCREEN_W * cb::SCREEN_H];
static uint32_t g_palette[256];   // RGBA per intensity, little-endian as ImageData wants it

static uint32_t g_frame = 0;

static cb::ArenaBytes g_arena_a;
static cb::ArenaBytes g_arena_b;
static cb::SnapshotStore g_store;

// Same cap as the board: a reply that would not fit on the desk does not get
// drawn here either, or the two would disagree about what "too big" means.
static char g_body[6144];

static cb::ServerClock g_clock;
static cb::ViewState g_view = cb::VIEW_INITIAL;
static cb::BurnHistory g_burn;

static void push_row(void*, int y, const uint8_t* row, int w) {
    uint32_t* dst = g_rgba + y * cb::SCREEN_W;
    for (int x = 0; x < w; x++) dst[x] = g_palette[row[x]];
}

extern "C" {

EMSCRIPTEN_KEEPALIVE void cb_init() {
    for (int i = 0; i < 256; i++) {
        // Through the RGB565 the panel actually receives, not palette_rgb():
        // the board's greens are quantised to six bits and its reds and blues
        // to five, and the page should show the steps the glass does.
        const uint16_t p = cb::palette_rgb565(static_cast<uint8_t>(i));
        const uint32_t r = ((p >> 11) & 0x1F) * 255 / 31;
        const uint32_t g = ((p >> 5) & 0x3F) * 255 / 63;
        const uint32_t b = (p & 0x1F) * 255 / 31;
        g_palette[i] = 0xFF000000u | (b << 16) | (g << 8) | r;
    }
    memset(g_buf, 0, sizeof g_buf);
    cb::store_init(g_store, g_arena_a, g_arena_b);
    cb::burn_init(g_burn);
}

EMSCRIPTEN_KEEPALIVE char* cb_body() { return g_body; }
EMSCRIPTEN_KEEPALIVE int cb_body_cap() { return (int)sizeof g_body; }
EMSCRIPTEN_KEEPALIVE uint32_t* cb_pixels() { return g_rgba; }
EMSCRIPTEN_KEEPALIVE int cb_width() { return cb::SCREEN_W; }
EMSCRIPTEN_KEEPALIVE int cb_height() { return cb::SCREEN_H; }

// `age_ms` is how long ago the reply left the server. A live fetch passes 0,
// exactly as the board does. A reply restored from storage on a cold start
// passes how long it sat there: the board never restarts holding old data, but
// a page does, and seeding the clock from a day-old serverTime would make a
// day-old snapshot read as fresh.
EMSCRIPTEN_KEEPALIVE int cb_accept(int len, uint32_t local_ms, double age_ms) {
    if (len < 0 || len > (int)sizeof g_body) return -1;
    const cb::ParseResult r = cb::store_accept(g_store, g_body, (size_t)len);
    const int64_t served = cb::store_current(g_store).server_time_ms;
    if (r == cb::ParseResult::Ok && served > 0)
        cb::clock_seed(g_clock, served + (int64_t)age_ms, local_ms);

    // Only a live reply is a new reading. A restored one would put a stale
    // total into the history at today's timestamp.
    if (r == cb::ParseResult::Ok && age_ms == 0) {
        const cb::UsageSnapshot& s = cb::store_current(g_store);
        const int64_t at = cb::clock_now(g_clock, local_ms);
        if (at > 0 && s.providers && s.provider_count > 0)
            cb::burn_observe(g_burn, at, cb::chart_total(s.providers[0], 1));
    }
    return (int)r;
}

EMSCRIPTEN_KEEPALIVE int cb_tap(int x, int y) {
    return cb::view_tap(g_view, x, y, cb::store_current(g_store).provider_count) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void cb_frame(uint32_t local_ms) {
    const cb::EffectParams fx = cb::EffectParams::defaults();
    cb::Canvas c(g_buf, cb::SCREEN_W, cb::SCREEN_H);

    const cb::UsageSnapshot& snap = cb::store_current(g_store);
    cb::view_clamp(g_view, snap.provider_count);
    const int64_t now = cb::clock_now(g_clock, local_ms);
    char clk[8];
    const char* clock_text = nullptr;
    if (now > 0) {
        const int64_t local = now + (int64_t)snap.utc_offset_sec * 1000;
        cb::format_clock(local, clk, sizeof clk);
        clock_text = clk;
    }
    const int64_t tok = g_view.provider == 0
                            ? cb::burn_rate_per_hour(g_burn, now, cb::BURN_WINDOW_MS)
                            : -1;

    cb::render_frame(c, snap, g_view.provider, now, clock_text, fx,
                     g_frame, g_ring, sizeof g_ring,
                     g_out_row, push_row, nullptr, nullptr, tok, g_view.page);
    g_frame++;
}

// The golden reference frame, rendered here exactly as src/host renders it, so
// web/test-golden.mjs can hold this build to the same bytes as the board's.
static uint8_t g_ref[cb::SCREEN_W * cb::SCREEN_H];

EMSCRIPTEN_KEEPALIVE uint8_t* cb_reference(int page) {
    static uint8_t accum[cb::SCREEN_W * cb::SCREEN_H];
    memset(accum, 0, sizeof accum);
    cb::Canvas a(accum, cb::SCREEN_W, cb::SCREEN_H);
    cb::Canvas out(g_ref, cb::SCREEN_W, cb::SCREEN_H);
    cb::render_frame(a, out, cb::fixture_snapshot(), 0, cb::FIXTURE_REFERENCE_MS, "14:44",
                     cb::EffectParams::defaults(), 0, g_ring, sizeof g_ring, nullptr, -1,
                     static_cast<cb::Page>(page));
    return g_ref;
}

}  // extern "C"
