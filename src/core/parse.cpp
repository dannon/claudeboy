#include "core/parse.h"

namespace cb {
namespace {

// Recursion only happens inside skip_value(), on payload the board does not
// understand. A hostile or broken body must not walk the ESP32's stack off.
constexpr int MAX_DEPTH = 24;

bool key_is(const char* b, const char* e, const char* lit) {
    const size_t n = (size_t)(e - b);
    for (size_t i = 0; i < n; i++) {
        if (lit[i] == '\0' || lit[i] != b[i]) return false;
    }
    return lit[n] == '\0';
}

int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int32_t clamp32(int64_t v) {
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return (int32_t)v;
}

// The wire carries epoch and duration values in seconds; everything downstream
// is int64 milliseconds. Accumulate in 64 bits -- atoi() would overflow here.
int64_t to_ms(int64_t sec) {
    if (sec >  INT64_MAX / 1000) return INT64_MAX;
    if (sec <  INT64_MIN / 1000) return INT64_MIN;
    return sec * 1000;
}

struct P {
    const char* p;
    const char* end;
    ParseArena& a;
    size_t str_used;
    int prog_n, text_n, chart_n, depth;
    bool truncated, bad;

    P(const char* json, size_t len, ParseArena& arena)
        : p(json), end(json + len), a(arena), str_used(0),
          prog_n(0), text_n(0), chart_n(0), depth(0),
          truncated(false), bad(false) {}

    void fail() { bad = true; }

    void ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
    }
    bool eat(char c) {
        ws();
        if (p < end && *p == c) { ++p; return true; }
        return false;
    }

    // Byte 0 of the arena is a reserved NUL -- parse_snapshot() refuses a null
    // or zero-length arena, so there is always a valid in-arena empty string to
    // hand back and no field is ever null.
    const char* empty() const { return a.text; }

    // Advances past a JSON string, reporting the raw span between the quotes.
    bool scan_string(const char*& b, const char*& e) {
        ws();
        if (p >= end || *p != '"') { fail(); return false; }
        ++p;
        b = p;
        while (p < end) {
            // guard the pair so the cursor never forms end + 1
            if (*p == '\\') { if (end - p < 2) break; p += 2; continue; }
            if (*p == '"') { e = p; ++p; return true; }
            ++p;
        }
        fail();
        return false;
    }

    // Copies a raw span into the arena, decoding escapes and NUL-terminating.
    const char* store(const char* b, const char* e) {
        // An empty string is already sitting at byte 0, so hand that back
        // rather than consume a byte the arena may not have.
        if (b == e) return empty();
        const size_t start = str_used;
        bool ok = true;        // still room in the arena
        bool wellformed = true;
        for (const char* q = b; q < e && ok && wellformed; ) {
            char c = *q++;
            if (c != '\\') { ok = put(c); continue; }
            if (q >= e) { wellformed = false; break; }
            const char esc = *q++;
            switch (esc) {
                case 'b': ok = put('\b'); break;
                case 'f': ok = put('\f'); break;
                case 'n': ok = put('\n'); break;
                case 'r': ok = put('\r'); break;
                case 't': ok = put('\t'); break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!read_hex4(q, e, cp)) { wellformed = false; break; }
                    // a surrogate pair is one character, so eat its tail too
                    if (cp >= 0xD800 && cp <= 0xDBFF && e - q >= 6 &&
                        q[0] == '\\' && q[1] == 'u') {
                        const char* save = q;
                        q += 2;
                        uint32_t lo = 0;
                        if (!read_hex4(q, e, lo) || lo < 0xDC00 || lo > 0xDFFF) q = save;
                    }
                    // The 5x7 font is ASCII-only and text_width() counts bytes,
                    // so anything else collapses to a single placeholder.
                    ok = put(cp && cp < 0x80 ? (char)cp : '?');
                    break;
                }
                default: ok = put(esc); break;   // covers \" \\ \/
            }
        }
        if (!wellformed) { str_used = start; fail(); return empty(); }
        if (!ok || str_used >= a.text_bytes) { str_used = start; truncated = true; return empty(); }
        a.text[str_used++] = '\0';
        return a.text + start;
    }

    bool put(char c) {
        if (str_used + 1 >= a.text_bytes) return false;   // +1 reserves the NUL
        a.text[str_used++] = c;
        return true;
    }

    bool read_hex4(const char*& q, const char* e, uint32_t& cp) {
        if (e - q < 4) return false;
        cp = 0;
        for (int i = 0; i < 4; i++) {
            const int h = hexval(*q++);
            if (h < 0) return false;
            cp = cp * 16 + (uint32_t)h;
        }
        return true;
    }

    bool number(int64_t& v) {
        ws();
        bool neg = false;
        if (p < end && (*p == '-' || *p == '+')) { neg = (*p == '-'); ++p; }
        if (p >= end || *p < '0' || *p > '9') { fail(); return false; }
        int64_t acc = 0;
        while (p < end && *p >= '0' && *p <= '9') {
            const int d = *p++ - '0';
            acc = (acc > (INT64_MAX - d) / 10) ? INT64_MAX : acc * 10 + d;
        }
        // no field the board reads is fractional, but tolerate the shape
        if (p < end && *p == '.') { ++p; while (p < end && *p >= '0' && *p <= '9') ++p; }
        if (p < end && (*p == 'e' || *p == 'E')) {
            ++p;
            if (p < end && (*p == '+' || *p == '-')) ++p;
            while (p < end && *p >= '0' && *p <= '9') ++p;
        }
        v = neg ? -acc : acc;
        return true;
    }

    void skip_value() {
        if (++depth > MAX_DEPTH) { fail(); --depth; return; }
        ws();
        if (p >= end) { fail(); --depth; return; }
        const char c = *p;
        if (c == '"') {
            const char* b; const char* e;
            scan_string(b, e);
        } else if (c == '{' || c == '[') {
            const bool obj = (c == '{');
            const char close = obj ? '}' : ']';
            ++p;
            if (!eat(close)) {
                for (;;) {
                    if (obj) {
                        const char* kb; const char* ke;
                        if (!scan_string(kb, ke)) break;
                        if (!eat(':')) { fail(); break; }
                    }
                    skip_value();
                    if (bad) break;
                    if (eat(',')) continue;
                    if (eat(close)) break;
                    fail(); break;
                }
            }
        } else if (c == 't' || c == 'f' || c == 'n') {
            while (p < end && *p >= 'a' && *p <= 'z') ++p;
        } else {
            int64_t v;
            number(v);
        }
        --depth;
    }

    void parse_progress(ProgressLine& o) {
        o.label = empty(); o.used = 0; o.limit = 0; o.resets_at_ms = 0; o.period_ms = 0;
        if (!eat('{')) { fail(); return; }
        if (eat('}')) return;
        for (;;) {
            const char* kb; const char* ke;
            if (!scan_string(kb, ke)) return;
            if (!eat(':')) { fail(); return; }
            int64_t v = 0;
            if (key_is(kb, ke, "label")) {
                const char* b; const char* e;
                if (!scan_string(b, e)) return;
                o.label = store(b, e);
            } else if (key_is(kb, ke, "used")) {
                if (!number(v)) return;
                o.used = clamp32(v);
            } else if (key_is(kb, ke, "limit")) {
                if (!number(v)) return;
                o.limit = clamp32(v);
            } else if (key_is(kb, ke, "resetsAt")) {
                if (!number(v)) return;
                o.resets_at_ms = to_ms(v);
            } else if (key_is(kb, ke, "periodSec")) {
                if (!number(v)) return;
                o.period_ms = to_ms(v);
            } else {
                skip_value();
                if (bad) return;
            }
            if (eat(',')) continue;
            if (eat('}')) return;
            fail(); return;
        }
    }

    void parse_text_line(TextLine& o) {
        o.label = empty(); o.value = empty();
        if (!eat('{')) { fail(); return; }
        if (eat('}')) return;
        for (;;) {
            const char* kb; const char* ke;
            if (!scan_string(kb, ke)) return;
            if (!eat(':')) { fail(); return; }
            if (key_is(kb, ke, "label") || key_is(kb, ke, "value")) {
                const char* b; const char* e;
                if (!scan_string(b, e)) return;
                const char* s = store(b, e);
                if (key_is(kb, ke, "label")) o.label = s; else o.value = s;
            } else {
                skip_value();
                if (bad) return;
            }
            if (eat(',')) continue;
            if (eat('}')) return;
            fail(); return;
        }
    }

    void parse_chart_point(ChartPoint& o) {
        o.label = empty(); o.value = 0;
        if (!eat('{')) { fail(); return; }
        if (eat('}')) return;
        for (;;) {
            const char* kb; const char* ke;
            if (!scan_string(kb, ke)) return;
            if (!eat(':')) { fail(); return; }
            if (key_is(kb, ke, "label")) {
                const char* b; const char* e;
                if (!scan_string(b, e)) return;
                o.label = store(b, e);
            } else if (key_is(kb, ke, "value")) {
                if (!number(o.value)) return;
            } else {
                skip_value();
                if (bad) return;
            }
            if (eat(',')) continue;
            if (eat('}')) return;
            fail(); return;
        }
    }

    void parse_progress_array(Provider& o) {
        if (!eat('[')) { fail(); return; }
        if (eat(']')) return;
        for (;;) {
            if (a.progress && prog_n < a.progress_cap) {
                parse_progress(a.progress[prog_n]);
                if (bad) return;
                prog_n++;
                o.progress_count++;
            } else {
                truncated = true;
                skip_value();
                if (bad) return;
            }
            if (eat(',')) continue;
            if (eat(']')) return;
            fail(); return;
        }
    }

    void parse_text_array(Provider& o) {
        if (!eat('[')) { fail(); return; }
        if (eat(']')) return;
        for (;;) {
            if (a.text_lines && text_n < a.text_line_cap) {
                parse_text_line(a.text_lines[text_n]);
                if (bad) return;
                text_n++;
                o.text_count++;
            } else {
                truncated = true;
                skip_value();
                if (bad) return;
            }
            if (eat(',')) continue;
            if (eat(']')) return;
            fail(); return;
        }
    }

    void parse_chart_array(Provider& o) {
        if (!eat('[')) { fail(); return; }
        if (eat(']')) return;
        for (;;) {
            if (a.chart && chart_n < a.chart_cap) {
                parse_chart_point(a.chart[chart_n]);
                if (bad) return;
                chart_n++;
                o.chart_count++;
            } else {
                truncated = true;
                skip_value();
                if (bad) return;
            }
            if (eat(',')) continue;
            if (eat(']')) return;
            fail(); return;
        }
    }

    void parse_provider(Provider& o) {
        o.id = empty();
        o.display_name = empty();
        o.plan = empty();
        o.fetched_at_ms = 0;
        o.progress = a.progress ? a.progress + prog_n : nullptr;  o.progress_count = 0;
        o.text     = a.text_lines ? a.text_lines + text_n : nullptr; o.text_count = 0;
        o.chart    = a.chart ? a.chart + chart_n : nullptr;        o.chart_count = 0;
        if (!eat('{')) { fail(); return; }
        if (eat('}')) return;
        for (;;) {
            const char* kb; const char* ke;
            if (!scan_string(kb, ke)) return;
            if (!eat(':')) { fail(); return; }
            if (key_is(kb, ke, "id") || key_is(kb, ke, "displayName") ||
                key_is(kb, ke, "plan")) {
                const char* b; const char* e;
                if (!scan_string(b, e)) return;
                const char* s = store(b, e);
                if (key_is(kb, ke, "id")) o.id = s;
                else if (key_is(kb, ke, "displayName")) o.display_name = s;
                else o.plan = s;
            } else if (key_is(kb, ke, "fetchedAt")) {
                int64_t v = 0;
                if (!number(v)) return;
                o.fetched_at_ms = to_ms(v);
            } else if (key_is(kb, ke, "progress")) {
                parse_progress_array(o);
                if (bad) return;
            } else if (key_is(kb, ke, "text")) {
                parse_text_array(o);
                if (bad) return;
            } else if (key_is(kb, ke, "chart")) {
                parse_chart_array(o);
                if (bad) return;
            } else {
                skip_value();
                if (bad) return;
            }
            if (eat(',')) continue;
            if (eat('}')) return;
            fail(); return;
        }
    }

    void parse_providers(UsageSnapshot& out) {
        if (!eat('[')) { fail(); return; }
        out.providers = a.providers;
        if (eat(']')) return;
        for (;;) {
            if (a.providers && out.provider_count < a.provider_cap) {
                parse_provider(a.providers[out.provider_count]);
                if (bad) return;
                out.provider_count++;
            } else {
                truncated = true;
                skip_value();
                if (bad) return;
            }
            if (eat(',')) continue;
            if (eat(']')) return;
            fail(); return;
        }
    }
};

}  // namespace

ParseResult parse_snapshot(const char* json, size_t len, ParseArena& arena,
                           UsageSnapshot& out) {
    out.providers = nullptr;
    out.provider_count = 0;
    out.server_time_ms = 0;
    out.utc_offset_sec = 0;
    // Truncated is a renderable result and the renderer dereferences every
    // const char* it is handed, so a string arena that cannot even hold the
    // reserved NUL is refused here rather than allowed to produce null or
    // unterminated pointers downstream.
    if (!arena.text || arena.text_bytes == 0) return ParseResult::Malformed;
    if (!json || len == 0) return ParseResult::Empty;

    P s(json, len, arena);
    arena.text[0] = '\0';   // the reserved in-arena empty string
    s.str_used = 1;

    if (!s.eat('{')) {
        s.fail();
    } else if (!s.eat('}')) {
        for (;;) {
            const char* kb; const char* ke;
            if (!s.scan_string(kb, ke)) break;
            if (!s.eat(':')) { s.fail(); break; }
            if (key_is(kb, ke, "serverTime")) {
                int64_t v = 0;
                if (!s.number(v)) break;
                out.server_time_ms = to_ms(v);
            } else if (key_is(kb, ke, "utcOffsetSec")) {
                int64_t v = 0;
                if (!s.number(v)) break;
                // Clamp rather than reject: a nonsense offset should cost the
                // clock its zone, not the whole snapshot its numbers.
                if (v < -50400) v = -50400;
                if (v > 50400) v = 50400;
                out.utc_offset_sec = static_cast<int32_t>(v);
            } else if (key_is(kb, ke, "providers")) {
                s.parse_providers(out);
                if (s.bad) break;
            } else {
                s.skip_value();
                if (s.bad) break;
            }
            if (s.eat(',')) continue;
            if (s.eat('}')) break;
            s.fail(); break;
        }
    }

    if (s.bad) {
        out.providers = nullptr;
        out.provider_count = 0;
        out.server_time_ms = 0;
        return ParseResult::Malformed;
    }
    if (s.truncated) return ParseResult::Truncated;
    if (out.provider_count == 0) {
        out.providers = nullptr;
        out.server_time_ms = 0;
        return ParseResult::Empty;
    }
    return ParseResult::Ok;
}

}  // namespace cb
