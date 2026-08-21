#include "core/frame.h"
#include <string.h>
#include "core/screen.h"

namespace cb {

void render_frame(Canvas& accum, Canvas& out, const UsageSnapshot& snap, int provider_index,
                  int64_t now_ms, const char* clock, const EffectParams& fx,
                  uint32_t frame, uint8_t* ring, size_t ring_bytes,
                  FrameTiming* timing) {
    const bool timed = timing && timing->now_us;
    const uint32_t t0 = timed ? timing->now_us() : 0;

    accum.decay(fx.decay);
    render_ambient(accum, snap, provider_index, now_ms, clock);

    const uint32_t t1 = timed ? timing->now_us() : 0;

    const int w = accum.width(), h = accum.height();
    if (accum.data() && out.data() && out.width() == w && out.height() == h) {
        memcpy(out.data(), accum.data(), static_cast<size_t>(w) * h);
        post_process(out, fx, frame, ring, ring_bytes);
    }

    if (timed) {
        const uint32_t t2 = timing->now_us();
        timing->render_us = t1 - t0;
        timing->post_us = t2 - t1;
    }
}

}  // namespace cb
