#pragma once
#include "core/types.h"

namespace cb {

constexpr float BURNOUT_RATIO = 1.02f;
constexpr float SURPLUS_RATIO = 0.90f;

// Below this much of the window elapsed, the ratio is too noisy to mean
// anything: one burst in the first minutes of a five-hour window computes to a
// pace of 3x and would claim burnout on every single reset. Under this floor
// the state is Unknown and the UI shows no verdict. 5% is 15 minutes of a
// session window and 8 hours of a weekly one -- in both cases genuinely too
// little to extrapolate from.
constexpr float MIN_ELAPSED_FRAC = 0.05f;

// Derives remaining, elapsed, pace ratio and projected exhaustion for one line.
// now_ms and the line's timestamps must share an epoch. Never divides by zero.
Pace compute_pace(const ProgressLine& line, int64_t now_ms);

}  // namespace cb
