#pragma once
#include <stdint.h>

namespace cb {

// NOT currently used by production code -- see test/test_fastdiv for why.
//
// The identity `x / 255 == (x + 1 + (x >> 8)) >> 8` was proposed as exact for
// any x in [0, 65535]. An exhaustive test over that full range disproves it:
// it fails at x == 65535 (gives 256, real division gives 257) and is exact
// for every other value, i.e. exact on [0, 65534] only.
//
// Every real call site in this codebase only ever feeds this a product of
// two uint8_t values (max 255*255 = 65025), which is comfortably inside the
// proven-exact range. But since the identity does not hold over the range
// specified for verification, the optimization pass held off on wiring this
// into crt.cpp / palette.cpp rather than relying on a narrower domain than
// what was asked to be proven. Left here, tested, as a documented finding.
inline uint32_t div255(uint32_t x) {
    return (x + 1u + (x >> 8)) >> 8;
}

}  // namespace cb
