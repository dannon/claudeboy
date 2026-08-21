#pragma once
#include <stddef.h>
#include <stdint.h>
#include "core/canvas.h"
#include "core/crt.h"
#include "core/types.h"

namespace cb {

// Optional per-stage timing for render_frame(). The clock comes from the
// caller so core/ stays free of any platform clock; leave now_us null (or
// pass no FrameTiming at all) to skip timing entirely.
struct FrameTiming {
    uint32_t (*now_us)();
    uint32_t render_us;   // decay + draw
    uint32_t post_us;     // post-processing, plus the sink on the streaming form
};

// One whole frame: decay `accum`, draw the ambient screen into it, and
// post-process the result into the output -- a second canvas here, a row
// stream below.
//
// The split is the point. Post-processing must never write back into the
// accumulator: bloom only ever adds light while the accumulator only sheds
// EffectParams::decay per frame, so post-processed pixels fed back in
// compound frame over frame until bloom saturates the panel (this shipped
// once, and a human caught it from a photo). Every backend goes through
// this function so the invariant lives in one place instead of in prose
// comments in each main().
//
// Nothing is allocated here -- both canvases, and the bloom ring, belong to
// the caller. If `out` is not the same size as `accum` the accumulator is
// still advanced but `out` is left untouched, rather than writing past it.
//
// `out` is scratch for the duration of the call: this overload runs the row
// stream below and lends it `out`'s last row to work in, so `out` only holds
// the frame once the call returns, and it must not overlap `accum`.
void render_frame(Canvas& accum, Canvas& out, const UsageSnapshot& snap, int provider_index,
                  int64_t now_ms, const char* clock, const EffectParams& fx,
                  uint32_t frame, uint8_t* ring, size_t ring_bytes,
                  FrameTiming* timing = nullptr);

// The same frame, emitted a row at a time. This is what the device uses: it
// holds no output framebuffer at all, just the bloom ring and one scratch row,
// and pushes each row to the panel as it arrives.
void render_frame(Canvas& accum, const UsageSnapshot& snap, int provider_index,
                  int64_t now_ms, const char* clock, const EffectParams& fx,
                  uint32_t frame, uint8_t* ring, size_t ring_bytes,
                  uint8_t* out_row, RowSink sink, void* ctx,
                  FrameTiming* timing = nullptr);

}  // namespace cb
