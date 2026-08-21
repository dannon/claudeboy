#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "core/canvas.h"
#include "core/crt.h"
#include "core/fixture.h"
#include "core/frame.h"
#include "core/screen.h"

// Nothing else in the suite runs successive frames. The bloom-feedback bug
// that reached the panel -- post-processed pixels fed back into the
// accumulator, compounding until bloom saturated -- was structurally
// invisible to every single-frame test, including the golden. These two
// tests close that gap.

static const size_t N = static_cast<size_t>(cb::SCREEN_W) * cb::SCREEN_H;

static uint8_t accum_buf[cb::SCREEN_W * cb::SCREEN_H];
static uint8_t out_buf[cb::SCREEN_W * cb::SCREEN_H];
static uint8_t snapshot_buf[cb::SCREEN_W * cb::SCREEN_H];
static uint8_t stream_accum_buf[cb::SCREEN_W * cb::SCREEN_H];
static uint8_t stream_out_buf[cb::SCREEN_W * cb::SCREEN_H];
static uint8_t stream_row[cb::SCREEN_W];
static uint8_t ring[9 * cb::SCREEN_W];

static void one_frame(cb::Canvas& accum, cb::Canvas& out, uint32_t frame) {
    cb::render_frame(accum, out, cb::fixture_snapshot(), 0, cb::FIXTURE_REFERENCE_MS,
                     "14:44", cb::EffectParams::defaults(), frame, ring, sizeof ring);
}

// Reassembles the streamed rows so they can be compared with the canvas the
// other overload fills. The device does not do this -- it pushes each row to
// the panel and keeps nothing.
static void collect_row(void* ctx, int y, const uint8_t* row, int w) {
    uint8_t* base = static_cast<uint8_t*>(ctx);
    memcpy(base + static_cast<size_t>(y) * w, row, static_cast<size_t>(w));
}

static void one_frame_streamed(cb::Canvas& accum, uint8_t* dst, uint32_t frame) {
    cb::render_frame(accum, cb::fixture_snapshot(), 0, cb::FIXTURE_REFERENCE_MS,
                     "14:44", cb::EffectParams::defaults(), frame, ring, sizeof ring,
                     stream_row, collect_row, dst);
}

// Drawing blends with max() and the fixture is static, so the accumulator
// reaches a fixed point after one frame: decay takes every pixel down by
// EffectParams::decay and the identical redraw puts it straight back. N
// frames must therefore leave the accumulator byte-identical to one frame.
// Any path that leaks post-processed light back in breaks this immediately,
// because bloom adds more than decay removes.
void test_frame_loop_reaches_a_fixed_point(void) {
    memset(accum_buf, 0, N);
    cb::Canvas accum(accum_buf, cb::SCREEN_W, cb::SCREEN_H);
    cb::Canvas out(out_buf, cb::SCREEN_W, cb::SCREEN_H);

    one_frame(accum, out, 0);
    memcpy(snapshot_buf, accum_buf, N);

    // Sanity: the fixed point is a drawn screen, not an empty buffer, or the
    // comparison below would pass on nothing at all.
    size_t lit = 0;
    for (size_t i = 0; i < N; i++) if (snapshot_buf[i]) lit++;
    TEST_ASSERT_TRUE(lit > 5000);

    for (uint32_t f = 1; f <= 20; f++) one_frame(accum, out, f);

    size_t diff = 0, first = 0;
    for (size_t i = 0; i < N; i++) {
        if (accum_buf[i] != snapshot_buf[i]) { if (!diff) first = i; diff++; }
    }
    if (diff) {
        char msg[160];
        snprintf(msg, sizeof msg,
                 "accumulator drifted over 21 frames: %zu of %zu pixels differ, "
                 "first at (%zu,%zu) %u -> %u",
                 diff, N, first % cb::SCREEN_W, first / cb::SCREEN_W,
                 (unsigned)snapshot_buf[first], (unsigned)accum_buf[first]);
        TEST_FAIL_MESSAGE(msg);
    }
}

// The same invariant inside a single call: post_process may only ever touch
// the copy handed to it.
void test_post_processing_never_touches_the_accumulator(void) {
    memset(accum_buf, 0, N);
    cb::Canvas accum(accum_buf, cb::SCREEN_W, cb::SCREEN_H);
    cb::Canvas out(out_buf, cb::SCREEN_W, cb::SCREEN_H);

    // Draw the frame the way render_frame() does, and keep what the
    // accumulator holds the instant drawing finishes.
    const cb::EffectParams fx = cb::EffectParams::defaults();
    accum.decay(fx.decay);
    cb::render_ambient(accum, cb::fixture_snapshot(), 0, cb::FIXTURE_REFERENCE_MS, "14:44");
    memcpy(snapshot_buf, accum_buf, N);

    // Now the real thing from the same starting state: it redraws the same
    // content (max blend, so no change) and then post-processes the copy.
    one_frame(accum, out, 0);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(snapshot_buf, accum_buf, N);
    // And post_process really did do something, so the assertion above is
    // not comparing two untouched buffers.
    TEST_ASSERT_TRUE(memcmp(out_buf, accum_buf, N) != 0);
}

// The device runs the streaming overload and the host runs the canvas one, so
// they have to agree byte for byte -- otherwise the golden pins only half the
// code that ships. Several frames, because flicker is keyed on the frame
// counter and the bloom ring carries state between rows.
void test_streaming_and_canvas_overloads_agree(void) {
    memset(accum_buf, 0, N);
    memset(out_buf, 0, N);
    cb::Canvas accum(accum_buf, cb::SCREEN_W, cb::SCREEN_H);
    cb::Canvas out(out_buf, cb::SCREEN_W, cb::SCREEN_H);
    for (uint32_t f = 0; f < 4; f++) one_frame(accum, out, f);
    memcpy(snapshot_buf, accum_buf, N);

    memset(stream_accum_buf, 0, N);
    memset(stream_out_buf, 0, N);
    cb::Canvas stream_accum(stream_accum_buf, cb::SCREEN_W, cb::SCREEN_H);
    for (uint32_t f = 0; f < 4; f++) one_frame_streamed(stream_accum, stream_out_buf, f);

    TEST_ASSERT_EQUAL_UINT8_ARRAY(out_buf, stream_out_buf, N);
    // And the accumulator is left in exactly the same state, so the fixed-point
    // test above covers the streaming path too.
    TEST_ASSERT_EQUAL_UINT8_ARRAY(snapshot_buf, stream_accum_buf, N);
}

// A mismatched output canvas must leave the caller's memory alone rather
// than writing past it -- there is no MMU on the device.
void test_mismatched_output_canvas_is_left_alone(void) {
    memset(accum_buf, 0, N);
    memset(out_buf, 0x5A, N);
    cb::Canvas accum(accum_buf, cb::SCREEN_W, cb::SCREEN_H);
    cb::Canvas small(out_buf, 16, 16);

    one_frame(accum, small, 0);

    for (size_t i = 0; i < N; i++) TEST_ASSERT_EQUAL_UINT8(0x5A, out_buf[i]);
    // The accumulator still advanced.
    size_t lit = 0;
    for (size_t i = 0; i < N; i++) if (accum_buf[i]) lit++;
    TEST_ASSERT_TRUE(lit > 5000);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_frame_loop_reaches_a_fixed_point);
    RUN_TEST(test_post_processing_never_touches_the_accumulator);
    RUN_TEST(test_mismatched_output_canvas_is_left_alone);
    RUN_TEST(test_streaming_and_canvas_overloads_agree);
    return UNITY_END();
}
