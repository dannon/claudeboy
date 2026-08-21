#pragma once
#include "core/types.h"

namespace cb {

// Epoch ms matching the capture in fixtures/claude-20260821.json
// (2026-08-21T13:44:34Z). Tests pass this as `now` for stable output;
// the device passes real time so the fixture visibly ages.
constexpr int64_t FIXTURE_REFERENCE_MS = 1787319874000LL;

const UsageSnapshot& fixture_snapshot();

// A capture is frozen, so its reset timestamps go stale the moment the clock
// passes them: reset_in clamps to 0 and the cell reads "SURPLUS 0m" forever
// while the window never rolls over. This walks resets_at_ms forward by whole
// period_ms steps until it is strictly after now_ms, which is what a live
// window would have done on its own. A line with no period is returned
// untouched.
//
// Only the device uses this. The host render paths and the tests stay pinned
// to FIXTURE_REFERENCE_MS -- they need a deterministic frame, not a live one.
ProgressLine roll_window_forward(const ProgressLine& line, int64_t now_ms);

}  // namespace cb
