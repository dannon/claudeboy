#include "core/reassemble.h"

#include <string.h>

namespace cb {

void reassemble_init(Reassembler& r, char* buf, size_t cap) {
    r.buf = buf;
    r.cap = cap;
    r.declared = 0;
    r.received = 0;
    r.active = false;
}

XferResult reassemble_frame(Reassembler& r, const uint8_t* frame, size_t len, size_t& out_len) {
    if (!r.buf || !frame || len == 0) return XferResult::Malformed;

    if (frame[0] == XFER_START) {
        if (len < 5) return XferResult::Malformed;
        const uint32_t total = (uint32_t)frame[1] | ((uint32_t)frame[2] << 8) |
                               ((uint32_t)frame[3] << 16) | ((uint32_t)frame[4] << 24);
        // A START always abandons whatever was in flight: the Mac only sends one
        // when it is starting over, and keeping the old bytes would splice two
        // payloads together.
        r.active = false;
        r.received = 0;
        r.declared = 0;
        if (total == 0) return XferResult::Malformed;
        if (total > r.cap) return XferResult::TooLarge;
        r.declared = total;
        r.active = true;
        return XferResult::NeedMore;
    }

    if (frame[0] != XFER_DATA) return XferResult::Malformed;
    if (!r.active) return XferResult::NoTransfer;

    const size_t n = len - 1;
    if (r.received + n > r.declared) {
        r.active = false;
        r.received = 0;
        return XferResult::Overflow;
    }
    memcpy(r.buf + r.received, frame + 1, n);
    r.received += n;
    if (r.received < r.declared) return XferResult::NeedMore;

    out_len = r.received;
    r.active = false;
    r.received = 0;
    return XferResult::Complete;
}

}  // namespace cb
