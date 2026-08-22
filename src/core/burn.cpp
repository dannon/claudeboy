#include "core/burn.h"

namespace cb {

void burn_init(BurnHistory& h) {
    h.count = 0;
    h.head = 0;
}

void burn_observe(BurnHistory& h, int64_t now_ms, int64_t total_tokens) {
    if (total_tokens < 0) return;

    if (h.count > 0) {
        const int last = (h.head + BURN_SLOTS - 1) % BURN_SLOTS;
        if (now_ms <= h.at_ms[last]) return;
        if (total_tokens < h.total[last]) burn_init(h);
    }

    h.at_ms[h.head] = now_ms;
    h.total[h.head] = total_tokens;
    h.head = (h.head + 1) % BURN_SLOTS;
    if (h.count < BURN_SLOTS) h.count++;
}

int64_t burn_rate_per_hour(const BurnHistory& h, int64_t now_ms, int64_t window_ms) {
    if (h.count < 2 || window_ms <= 0) return -1;
    const int64_t cutoff = now_ms - window_ms;

    // Walk back from newest to oldest, keeping the last reading still inside
    // the window. Sixty-four steps at worst, once a frame.
    int newest = -1, oldest = -1;
    for (int i = 0; i < h.count; i++) {
        const int slot = (h.head + BURN_SLOTS - 1 - i) % BURN_SLOTS;
        if (h.at_ms[slot] < cutoff) break;
        if (h.at_ms[slot] > now_ms) continue;   // a reading from the future is not evidence
        if (newest < 0) newest = slot;
        oldest = slot;
    }
    if (newest < 0 || oldest == newest) return -1;

    const int64_t span = h.at_ms[newest] - h.at_ms[oldest];
    if (span < BURN_MIN_SPAN_MS) return -1;

    const int64_t used = h.total[newest] - h.total[oldest];
    if (used <= 0) return 0;
    return used * 3600000LL / span;
}

}  // namespace cb
