#pragma once
#include <stdint.h>

namespace cb {

// The board has no RTC and no NTP. Every snapshot carries the server's own
// `serverTime`, so the clock is seeded from the reply and then runs on the
// local millisecond counter until the next one lands. Re-seeding every poll
// means drift never accumulates, and it fails in the right direction: with the
// network gone, time is exactly as stale as the data it arrived with.
struct ServerClock {
    int64_t  server_ms = 0;   // serverTime of the last successful fetch
    uint32_t local_ms  = 0;   // the local counter read at that same instant
    bool     seeded    = false;
};

void clock_seed(ServerClock& c, int64_t server_ms, uint32_t local_ms);

// 0 until the first reply lands. The board genuinely does not know what time
// it is before then, and a made-up clock would be worse than no clock.
int64_t clock_now(const ServerClock& c, uint32_t local_ms);

// Unsigned subtraction, so this stays correct across the 49-day wrap of a
// 32-bit millisecond counter.
bool clock_elapsed(uint32_t since_ms, uint32_t now_ms, uint32_t span_ms);

}  // namespace cb
