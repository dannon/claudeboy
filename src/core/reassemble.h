#pragma once
#include <stddef.h>
#include <stdint.h>

namespace cb {

// The first byte of every frame the transport hands us.
constexpr uint8_t XFER_START = 0x01;   // + uint32 little-endian total length
constexpr uint8_t XFER_DATA  = 0x02;   // + payload bytes

enum class XferResult : uint8_t {
    NeedMore,     // accepted, the transfer is still short of its declared length
    Complete,     // the declared length has arrived; out_len says how much
    Malformed,    // unknown opcode, a frame too short for its header, or a zero length
    TooLarge,     // START declared more than the buffer can hold
    Overflow,     // DATA ran past the declared length
    NoTransfer,   // DATA with no START in front of it
};

// Reassembles a length-prefixed payload from framed writes. This is the same
// contract read_body() enforces over HTTP -- a declared length up front, refuse
// what does not fit, refuse a short transfer -- because the failure it prevents
// is the same one: half a snapshot parsing as though it were whole.
struct Reassembler {
    char*  buf      = nullptr;
    size_t cap      = 0;
    size_t declared = 0;
    size_t received = 0;
    bool   active   = false;
};

void reassemble_init(Reassembler& r, char* buf, size_t cap);

// Feed exactly one frame. On Complete, `out_len` is the payload length in
// `r.buf`; on anything else `out_len` is left alone.
XferResult reassemble_frame(Reassembler& r, const uint8_t* frame, size_t len, size_t& out_len);

}  // namespace cb
