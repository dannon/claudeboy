#pragma once
#include "core/types.h"

namespace cb {

// Epoch ms matching the capture in fixtures/claude-20260821.json
// (2026-08-21T13:44:34Z). Tests pass this as `now` for stable output;
// the device passes real time so the fixture visibly ages.
constexpr int64_t FIXTURE_REFERENCE_MS = 1787319874000LL;

const UsageSnapshot& fixture_snapshot();

}  // namespace cb
