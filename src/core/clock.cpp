#include "core/clock.h"

namespace cb {

void clock_seed(ServerClock& c, int64_t server_ms, uint32_t local_ms) {
    c.server_ms = server_ms;
    c.local_ms = local_ms;
    c.seeded = true;
}

int64_t clock_now(const ServerClock& c, uint32_t local_ms) {
    if (!c.seeded) return 0;
    return c.server_ms + (int64_t)(uint32_t)(local_ms - c.local_ms);
}

bool clock_elapsed(uint32_t since_ms, uint32_t now_ms, uint32_t span_ms) {
    return (uint32_t)(now_ms - since_ms) >= span_ms;
}

}  // namespace cb
