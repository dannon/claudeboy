#pragma once
#include <stdint.h>

namespace cb {

// A short memory of the running token total, one reading per poll, so the
// panel can show a burn rate that was measured rather than one that was
// synthesised. Sixty-four slots is an hour of minute polls and costs a
// kilobyte -- less than two scan lines of the framebuffer.
constexpr int BURN_SLOTS = 64;

// How far back the rate looks. Long enough to ride out one missed poll, short
// enough to move while you are still in the session that caused it.
constexpr int64_t BURN_WINDOW_MS = 15LL * 60 * 1000;

// Two readings closer together than this say more about poll jitter than
// about consumption, so a span that short reports nothing at all.
constexpr int64_t BURN_MIN_SPAN_MS = 4LL * 60 * 1000;

struct BurnHistory {
    int64_t at_ms[BURN_SLOTS];
    int64_t total[BURN_SLOTS];
    int count;
    int head;      // next slot to write
};

void burn_init(BurnHistory& h);

// Record one reading of the cumulative token total.
//
// A total that has gone backwards means the day rolled over or the provider
// reset its counter, and the difference across that boundary is not a burn
// rate. The history starts again from the new reading rather than reporting a
// negative one. A reading no newer than the last is dropped: two polls that
// land on the same millisecond would divide by zero, and a clock that jumped
// backwards would do worse.
void burn_observe(BurnHistory& h, int64_t now_ms, int64_t total_tokens);

// Tokens per hour across the readings within `window_ms` of now, or -1 when
// there is not enough history to say. Never negative.
//
// The window is measured from `now_ms`, not from the newest reading, so a
// board that stopped polling an hour ago reports "unknown" rather than
// quoting an hour-old rate as if it were current.
int64_t burn_rate_per_hour(const BurnHistory& h, int64_t now_ms, int64_t window_ms);

}  // namespace cb
