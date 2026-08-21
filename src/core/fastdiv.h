#pragma once
#include <stdint.h>

namespace cb {

// Exact integer division by 255 for x in [0, 65534].
// NOT valid at x == 65535, where it yields 256 instead of 257 -- every call
// site here feeds a product of two uint8_t values (max 65025), so the
// restricted domain cannot be exceeded. Do not widen this without retesting.
inline uint32_t div255(uint32_t x) { return (x + 1u + (x >> 8)) >> 8; }

}  // namespace cb
