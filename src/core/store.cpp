#include "core/store.h"

namespace cb {
namespace {

const UsageSnapshot kNothing = {nullptr, 0, 0};

ParseArena view(ArenaBytes& b) {
    ParseArena a;
    a.text = b.text;             a.text_bytes = sizeof b.text;
    a.providers = b.providers;   a.provider_cap = STORE_PROVIDERS;
    a.progress = b.progress;     a.progress_cap = STORE_PROGRESS;
    a.text_lines = b.text_lines; a.text_line_cap = STORE_TEXT_LINES;
    a.chart = b.chart;           a.chart_cap = STORE_CHART;
    return a;
}

}  // namespace

void store_init(SnapshotStore& st, ArenaBytes& a, ArenaBytes& b) {
    st.bytes[0] = &a;
    st.bytes[1] = &b;
    st.snap[0] = kNothing;
    st.snap[1] = kNothing;
    st.shown = -1;
}

ParseResult store_accept(SnapshotStore& st, const char* json, size_t len) {
    if (!st.bytes[0] || !st.bytes[1]) return ParseResult::Malformed;

    const int idle = (st.shown == 0) ? 1 : 0;
    ParseArena arena = view(*st.bytes[idle]);
    UsageSnapshot scratch;
    const ParseResult r = parse_snapshot(json, len, arena, scratch);
    if (r == ParseResult::Ok) {
        st.snap[idle] = scratch;
        st.shown = idle;
    }
    return r;
}

const UsageSnapshot& store_current(const SnapshotStore& st) {
    if (st.shown < 0 || st.shown > 1) return kNothing;
    return st.snap[st.shown];
}

}  // namespace cb
