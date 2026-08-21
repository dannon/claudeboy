#include "core/frame.h"
#include <string.h>
#include "core/screen.h"

namespace cb {
namespace {

struct RowCopy {
    uint8_t* base;
    int stride;
};

void copy_row(void* ctx, int y, const uint8_t* row, int w) {
    RowCopy* rc = static_cast<RowCopy*>(ctx);
    uint8_t* dst = rc->base + static_cast<size_t>(y) * rc->stride;
    if (dst != row) memcpy(dst, row, static_cast<size_t>(w));
}

}  // namespace

void render_frame(Canvas& accum, const UsageSnapshot& snap, int provider_index,
                  int64_t now_ms, const char* clock, const EffectParams& fx,
                  uint32_t frame, uint8_t* ring, size_t ring_bytes,
                  uint8_t* out_row, RowSink sink, void* ctx,
                  FrameTiming* timing) {
    const bool timed = timing && timing->now_us;
    const uint32_t t0 = timed ? timing->now_us() : 0;

    accum.decay(fx.decay);
    render_ambient(accum, snap, provider_index, now_ms, clock);

    const uint32_t t1 = timed ? timing->now_us() : 0;

    post_process_stream(accum, fx, frame, ring, ring_bytes, out_row, sink, ctx);

    if (timed) {
        const uint32_t t2 = timing->now_us();
        timing->render_us = t1 - t0;
        timing->post_us = t2 - t1;
    }
}

void render_frame(Canvas& accum, Canvas& out, const UsageSnapshot& snap, int provider_index,
                  int64_t now_ms, const char* clock, const EffectParams& fx,
                  uint32_t frame, uint8_t* ring, size_t ring_bytes,
                  FrameTiming* timing) {
    const int w = accum.width(), h = accum.height();
    const bool usable = accum.data() && out.data() && out.width() == w && out.height() == h &&
                        w > 0 && h > 0;
    if (!usable) {
        // Advance the accumulator, write nothing: a null sink makes the
        // stream a no-op without a second code path to keep in step.
        render_frame(accum, snap, provider_index, now_ms, clock, fx, frame, ring, ring_bytes,
                     nullptr, nullptr, nullptr, timing);
        return;
    }

    // The last row of `out` doubles as the stream's scratch row, so this
    // wrapper still allocates nothing. Rows arrive in order, so that row is
    // only ever overwritten on the step that also finishes it.
    RowCopy rc{out.data(), w};
    uint8_t* scratch = out.data() + static_cast<size_t>(h - 1) * w;
    render_frame(accum, snap, provider_index, now_ms, clock, fx, frame, ring, ring_bytes,
                 scratch, copy_row, &rc, timing);
}

}  // namespace cb
