#pragma once
#include <stddef.h>
#include <stdint.h>
#include "core/types.h"

namespace cb {

// Caller-owned storage for a parse. core/ allocates nothing, so every string
// and every array element produced by parse_snapshot() lands in here and the
// returned UsageSnapshot only ever points inside it. The arena must therefore
// outlive every render_frame() that reads the snapshot.
struct ParseArena {
    char*         text;       size_t text_bytes;   // string arena
    Provider*     providers;  int provider_cap;
    ProgressLine* progress;   int progress_cap;
    TextLine*     text_lines; int text_line_cap;
    ChartPoint*   chart;      int chart_cap;
};

// Ok         -- the whole payload landed in the arena.
// Truncated  -- valid JSON, but the arena ran out; `out` holds what fit.
// Malformed  -- the bytes are not a snapshot, or `text` is null or zero-length;
//               `out` is zeroed.
// Empty      -- nothing to show (no body, or a body with no providers).
//
// Ok and Truncated are both renderable: every const char* in `out` points at a
// NUL-terminated string inside `arena.text` and none of them is null.
enum class ParseResult : uint8_t { Ok, Truncated, Malformed, Empty };

ParseResult parse_snapshot(const char* json, size_t len, ParseArena& arena,
                           UsageSnapshot& out);

}  // namespace cb
