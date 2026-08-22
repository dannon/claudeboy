#pragma once
#include <stddef.h>
#include "core/parse.h"
#include "core/types.h"

namespace cb {

// Capacities for one parse of the live `client=cyd` payload, with room above
// what is on the wire today: the capture at fixtures/api/snapshot-cyd.json
// needs 3 providers, 8 progress lines, 7 text lines, 62 chart points and 773
// string bytes. test/test_store asserts these still fit it, so a payload that
// outgrows the board fails at the host suite rather than on the desk.
constexpr size_t STORE_TEXT_BYTES = 2048;
constexpr int    STORE_PROVIDERS  = 8;
constexpr int    STORE_PROGRESS   = 32;
constexpr int    STORE_TEXT_LINES = 32;
constexpr int    STORE_CHART      = 128;

// The backing bytes for one parse. core/ allocates nothing, so the caller
// declares these -- and must declare them somewhere that outlives every
// render_frame() reading through them, which on the device means static
// duration. Every string in a parsed UsageSnapshot points into `text`.
struct ArenaBytes {
    char         text[STORE_TEXT_BYTES];
    Provider     providers[STORE_PROVIDERS];
    ProgressLine progress[STORE_PROGRESS];
    TextLine     text_lines[STORE_TEXT_LINES];
    ChartPoint   chart[STORE_CHART];
};

// Two arenas and the knowledge of which one is on screen.
//
// One arena would not do. A parse writes into its arena as it goes and only
// discovers the body is malformed part way through, by which point the strings
// the renderer is still dereferencing have been overwritten with fragments of
// the bad reply -- silently, with no MMU to catch it. So a fetch always parses
// into the half that is not being shown, and the halves swap only when a whole
// payload landed.
struct SnapshotStore {
    ArenaBytes*   bytes[2] = {nullptr, nullptr};
    UsageSnapshot snap[2]  = {};
    int           shown    = -1;   // -1 until the first Ok
};

void store_init(SnapshotStore& st, ArenaBytes& a, ArenaBytes& b);

// Parse `json` into the idle half and adopt it only on Ok. Every other result
// -- including Truncated, which is renderable but incomplete -- leaves what is
// on screen exactly as it was, so a bad reply shows as data going stale rather
// than as half a screen of numbers.
ParseResult store_accept(SnapshotStore& st, const char* json, size_t len);

// The snapshot to draw. Before the first Ok this is empty, which the staleness
// display reads as NO SIGNAL.
const UsageSnapshot& store_current(const SnapshotStore& st);

}  // namespace cb
