#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "core/canvas.h"
#include "core/crt.h"
#include "core/fixture.h"
#include "core/screen.h"

// post_process_stream() must be arithmetically identical to post_process(),
// not merely close: the device path will use the streaming form to save a
// whole framebuffer, and the golden pins the whole-frame one. Two independent
// implementations only stay honest if something asserts they agree, so this
// sweeps every effect parameter rather than trusting the defaults.

static const size_t N = static_cast<size_t>(cb::SCREEN_W) * cb::SCREEN_H;

static uint8_t accum_buf[cb::SCREEN_W * cb::SCREEN_H];
static uint8_t pristine_buf[cb::SCREEN_W * cb::SCREEN_H];
static uint8_t reference_buf[cb::SCREEN_W * cb::SCREEN_H];
static uint8_t streamed_buf[cb::SCREEN_W * cb::SCREEN_H];
static uint8_t ring[9 * cb::SCREEN_W];
static uint8_t out_row[cb::SCREEN_W];

struct SinkState {
    uint8_t* dst;
    int calls;
    int bad_y;      // first y that arrived out of order, or -1
    int bad_w;      // first w that was not SCREEN_W, or -1
};

static void collect_row(void* ctx, int y, const uint8_t* row, int w) {
    SinkState* s = static_cast<SinkState*>(ctx);
    if (y != s->calls && s->bad_y < 0) s->bad_y = y;
    if (w != cb::SCREEN_W && s->bad_w < 0) s->bad_w = w;
    s->calls++;
    if (y >= 0 && y < cb::SCREEN_H && w == cb::SCREEN_W)
        memcpy(s->dst + static_cast<size_t>(y) * cb::SCREEN_W, row, static_cast<size_t>(w));
}

// Real content, not a gradient: bloom seams show up at edges and glyph strokes.
static void draw_fixture(void) {
    memset(pristine_buf, 0, N);
    cb::Canvas c(pristine_buf, cb::SCREEN_W, cb::SCREEN_H);
    cb::render_ambient(c, cb::fixture_snapshot(), 0, cb::FIXTURE_REFERENCE_MS, "14:44");
}

// Returns 0 on agreement, otherwise fails the test with the offending params.
static void check_equivalence(const cb::EffectParams& p, uint32_t frame) {
    memcpy(reference_buf, pristine_buf, N);
    cb::Canvas ref(reference_buf, cb::SCREEN_W, cb::SCREEN_H);
    cb::post_process(ref, p, frame, ring, sizeof ring);

    memcpy(accum_buf, pristine_buf, N);
    memset(streamed_buf, 0xA5, N);
    const cb::Canvas src(accum_buf, cb::SCREEN_W, cb::SCREEN_H);
    SinkState s = { streamed_buf, 0, -1, -1 };
    cb::post_process_stream(src, p, frame, ring, sizeof ring,
                            out_row, collect_row, &s);

    char msg[256];
    if (s.calls != cb::SCREEN_H || s.bad_y >= 0 || s.bad_w >= 0) {
        snprintf(msg, sizeof msg,
                 "sink contract broken (r=%u str=%u scan=%u vig=%u flick=%u frame=%u): "
                 "%d calls (want %d), first out-of-order y=%d, first bad w=%d",
                 p.bloom_radius, p.bloom_strength, p.scanline_depth,
                 p.vignette_strength, p.flicker_amount, (unsigned)frame,
                 s.calls, cb::SCREEN_H, s.bad_y, s.bad_w);
        TEST_FAIL_MESSAGE(msg);
    }

    if (memcmp(accum_buf, pristine_buf, N) != 0) {
        size_t i = 0;
        while (i < N && accum_buf[i] == pristine_buf[i]) i++;
        snprintf(msg, sizeof msg,
                 "post_process_stream wrote to src (r=%u str=%u scan=%u vig=%u "
                 "flick=%u frame=%u): byte %zu %u -> %u",
                 p.bloom_radius, p.bloom_strength, p.scanline_depth,
                 p.vignette_strength, p.flicker_amount, (unsigned)frame,
                 i, (unsigned)pristine_buf[i], (unsigned)accum_buf[i]);
        TEST_FAIL_MESSAGE(msg);
    }

    for (size_t i = 0; i < N; i++) {
        if (streamed_buf[i] != reference_buf[i]) {
            snprintf(msg, sizeof msg,
                     "stream differs from post_process (r=%u str=%u scan=%u vig=%u "
                     "flick=%u frame=%u): byte %zu at (%zu,%zu) ref=%u stream=%u",
                     p.bloom_radius, p.bloom_strength, p.scanline_depth,
                     p.vignette_strength, p.flicker_amount, (unsigned)frame,
                     i, i % cb::SCREEN_W, i / cb::SCREEN_W,
                     (unsigned)reference_buf[i], (unsigned)streamed_buf[i]);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

void test_stream_matches_post_process_at_defaults(void) {
    draw_fixture();
    check_equivalence(cb::EffectParams::defaults(), 0);
}

void test_stream_matches_post_process_across_the_parameter_sweep(void) {
    draw_fixture();

    static const uint8_t radii[]    = { 0, 1, 2, 3, 4 };
    static const uint8_t strength[] = { 0, 240 };
    static const uint8_t scan[]     = { 0, 84 };
    static const uint8_t vig[]      = { 0, 120 };
    static const uint8_t flick[]    = { 0, 6 };
    // 511 lands on the rare deep dropout branch in flicker_gain().
    static const uint32_t frames[]  = { 0, 1, 7, 511 };

    cb::EffectParams p = cb::EffectParams::defaults();
    for (size_t a = 0; a < sizeof radii; a++)
    for (size_t b = 0; b < sizeof strength; b++)
    for (size_t c = 0; c < sizeof scan; c++)
    for (size_t d = 0; d < sizeof vig; d++)
    for (size_t e = 0; e < sizeof flick; e++)
    for (size_t f = 0; f < sizeof frames / sizeof frames[0]; f++) {
        p.bloom_radius = radii[a];
        p.bloom_strength = strength[b];
        p.scanline_depth = scan[c];
        p.vignette_strength = vig[d];
        p.flicker_amount = flick[e];
        check_equivalence(p, frames[f]);
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_stream_matches_post_process_at_defaults);
    RUN_TEST(test_stream_matches_post_process_across_the_parameter_sweep);
    return UNITY_END();
}
