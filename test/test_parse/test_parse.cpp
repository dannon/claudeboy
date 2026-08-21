#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "core/parse.h"
#include "core/types.h"

// Storage for the parse. Static, because on the device this is exactly how it
// lives: the arena outlives every render_frame() that reads through it.
static char        g_text[4096];
static cb::Provider     g_provs[8];
static cb::ProgressLine g_prog[32];
static cb::TextLine     g_textlines[32];
static cb::ChartPoint   g_chart[128];

static char g_json[8192];
static size_t g_json_len;

static cb::ParseArena full_arena(void) {
    cb::ParseArena a;
    a.text = g_text;            a.text_bytes = sizeof g_text;
    a.providers = g_provs;      a.provider_cap = 8;
    a.progress = g_prog;        a.progress_cap = 32;
    a.text_lines = g_textlines; a.text_line_cap = 32;
    a.chart = g_chart;          a.chart_cap = 128;
    return a;
}

static size_t load(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) {
        char msg[160];
        snprintf(msg, sizeof msg, "%s is missing -- run from the repo root", path);
        TEST_FAIL_MESSAGE(msg);
        return 0;
    }
    const size_t n = fread(g_json, 1, sizeof g_json, f);
    fclose(f);
    TEST_ASSERT_TRUE(n > 0 && n < sizeof g_json);
    g_json_len = n;
    return n;
}

static void assert_in_arena(const char* s, const cb::ParseArena& a) {
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_TRUE_MESSAGE(s >= a.text && s < a.text + a.text_bytes,
                             "string pointer escaped the arena");
}

// --- the real captures ------------------------------------------------------

void test_cyd_fixture_parses(void) {
    load("fixtures/api/snapshot-cyd.json");
    cb::ParseArena a = full_arena();
    cb::UsageSnapshot s{};
    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::parse_snapshot(g_json, g_json_len, a, s));

    TEST_ASSERT_EQUAL_INT(3, s.provider_count);
    TEST_ASSERT_EQUAL_STRING("claude", s.providers[0].id);
    TEST_ASSERT_EQUAL_STRING("codex", s.providers[1].id);
    TEST_ASSERT_EQUAL_STRING("antigravity", s.providers[2].id);
    TEST_ASSERT_EQUAL_STRING("Claude", s.providers[0].display_name);
    TEST_ASSERT_EQUAL_STRING("Antigravity", s.providers[2].display_name);

    const cb::Provider& claude = s.providers[0];
    TEST_ASSERT_EQUAL_INT(3, claude.progress_count);
    TEST_ASSERT_EQUAL_STRING("Session", claude.progress[0].label);
    TEST_ASSERT_EQUAL_STRING("Weekly", claude.progress[1].label);
    TEST_ASSERT_EQUAL_STRING("Fable", claude.progress[2].label);
    TEST_ASSERT_EQUAL_INT32(38, claude.progress[0].used);
    TEST_ASSERT_EQUAL_INT32(100, claude.progress[0].limit);

    TEST_ASSERT_EQUAL_INT(3, claude.text_count);
    TEST_ASSERT_EQUAL_STRING("Today", claude.text[0].label);
    TEST_ASSERT_EQUAL_STRING("Last 30 Days", claude.text[2].label);
    TEST_ASSERT_EQUAL_INT(31, claude.chart_count);
    TEST_ASSERT_EQUAL_STRING("Jul 22", claude.chart[0].label);
    TEST_ASSERT_EQUAL_INT64(7059800LL, claude.chart[0].value);
    TEST_ASSERT_EQUAL_STRING("Aug 21", claude.chart[30].label);
    TEST_ASSERT_EQUAL_INT64(332348508LL, claude.chart[30].value);

    const cb::Provider& codex = s.providers[1];
    TEST_ASSERT_EQUAL_INT(1, codex.progress_count);
    TEST_ASSERT_EQUAL_INT(4, codex.text_count);
    TEST_ASSERT_EQUAL_INT(31, codex.chart_count);
    TEST_ASSERT_EQUAL_INT64(0LL, codex.chart[0].value);

    const cb::Provider& anti = s.providers[2];
    TEST_ASSERT_EQUAL_INT(4, anti.progress_count);
    TEST_ASSERT_EQUAL_STRING("Claude Weekly", anti.progress[3].label);
    TEST_ASSERT_EQUAL_INT(0, anti.text_count);
    TEST_ASSERT_EQUAL_INT(0, anti.chart_count);
}

void test_epoch_seconds_become_milliseconds(void) {
    load("fixtures/api/snapshot-cyd.json");
    cb::ParseArena a = full_arena();
    cb::UsageSnapshot s{};
    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::parse_snapshot(g_json, g_json_len, a, s));

    // The wire carries seconds. Everything downstream is int64 milliseconds.
    TEST_ASSERT_EQUAL_INT64(1787350883000LL, s.server_time_ms);
    TEST_ASSERT_EQUAL_INT64(1787360400000LL, s.providers[0].progress[0].resets_at_ms);
    TEST_ASSERT_EQUAL_INT64(18000000LL, s.providers[0].progress[0].period_ms);
    TEST_ASSERT_EQUAL_INT64(1787576400000LL, s.providers[0].progress[1].resets_at_ms);
    TEST_ASSERT_EQUAL_INT64(604800000LL, s.providers[0].progress[1].period_ms);
    TEST_ASSERT_EQUAL_INT64(1787955324000LL, s.providers[2].progress[3].resets_at_ms);
}

void test_watch_fixture_parses(void) {
    load("fixtures/api/snapshot-watch.json");
    cb::ParseArena a = full_arena();
    cb::UsageSnapshot s{};
    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::parse_snapshot(g_json, g_json_len, a, s));

    TEST_ASSERT_EQUAL_INT(3, s.provider_count);
    TEST_ASSERT_EQUAL_INT64(1787350884000LL, s.server_time_ms);
    TEST_ASSERT_EQUAL_INT(3, s.providers[0].progress_count);
    TEST_ASSERT_EQUAL_STRING("Fable", s.providers[0].progress[2].label);

    // The watch payload carries no text or chart blocks at all.
    for (int i = 0; i < s.provider_count; i++) {
        TEST_ASSERT_EQUAL_INT(0, s.providers[i].text_count);
        TEST_ASSERT_EQUAL_INT(0, s.providers[i].chart_count);
    }
    TEST_ASSERT_EQUAL_STRING("antigravity", s.providers[2].id);
    TEST_ASSERT_EQUAL_INT(0, s.providers[2].text_count);
    TEST_ASSERT_EQUAL_INT(0, s.providers[2].chart_count);
}

// --- ownership --------------------------------------------------------------

void test_every_string_lives_in_the_arena(void) {
    load("fixtures/api/snapshot-cyd.json");
    cb::ParseArena a = full_arena();
    cb::UsageSnapshot s{};
    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::parse_snapshot(g_json, g_json_len, a, s));

    for (int i = 0; i < s.provider_count; i++) {
        const cb::Provider& p = s.providers[i];
        assert_in_arena(p.id, a);
        assert_in_arena(p.display_name, a);
        for (int j = 0; j < p.progress_count; j++) assert_in_arena(p.progress[j].label, a);
        for (int j = 0; j < p.text_count; j++) {
            assert_in_arena(p.text[j].label, a);
            assert_in_arena(p.text[j].value, a);
        }
        for (int j = 0; j < p.chart_count; j++) assert_in_arena(p.chart[j].label, a);
    }
    // and nothing points into the input buffer
    for (int i = 0; i < s.provider_count; i++) {
        const char* id = s.providers[i].id;
        TEST_ASSERT_FALSE(id >= g_json && id < g_json + sizeof g_json);
    }
}

void test_survives_the_input_being_overwritten(void) {
    load("fixtures/api/snapshot-cyd.json");
    cb::ParseArena a = full_arena();
    cb::UsageSnapshot s{};
    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::parse_snapshot(g_json, g_json_len, a, s));

    memset(g_json, 0xFF, sizeof g_json);

    TEST_ASSERT_EQUAL_STRING("claude", s.providers[0].id);
    TEST_ASSERT_EQUAL_STRING("Codex", s.providers[1].display_name);
    TEST_ASSERT_EQUAL_STRING("Session", s.providers[0].progress[0].label);
    TEST_ASSERT_EQUAL_STRING("Fable", s.providers[0].progress[2].label);
    TEST_ASSERT_EQUAL_STRING("Today", s.providers[0].text[0].label);
    TEST_ASSERT_EQUAL_STRING("Aug 21", s.providers[0].chart[30].label);
    TEST_ASSERT_EQUAL_STRING("Claude Weekly", s.providers[2].progress[3].label);
    TEST_ASSERT_EQUAL_INT64(1787360400000LL, s.providers[0].progress[0].resets_at_ms);
}

// --- running out of room ----------------------------------------------------

void test_string_arena_one_byte_short_is_truncated(void) {
    load("fixtures/api/snapshot-cyd.json");

    // Smallest arena that holds every string, found by probing.
    size_t need = 0;
    for (size_t n = 1; n <= sizeof g_text; n++) {
        cb::ParseArena a = full_arena();
        a.text_bytes = n;
        cb::UsageSnapshot s{};
        if (cb::parse_snapshot(g_json, g_json_len, a, s) == cb::ParseResult::Ok) { need = n; break; }
    }
    TEST_ASSERT_TRUE_MESSAGE(need > 0, "no arena size in range parsed cleanly");

    cb::ParseArena a = full_arena();
    a.text_bytes = need - 1;
    memset(g_text + a.text_bytes, 0x5A, sizeof g_text - a.text_bytes);
    cb::UsageSnapshot s{};
    TEST_ASSERT_EQUAL(cb::ParseResult::Truncated, cb::parse_snapshot(g_json, g_json_len, a, s));

    for (size_t i = a.text_bytes; i < sizeof g_text; i++) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x5A, (uint8_t)g_text[i], "parser wrote past the arena end");
    }
}

void test_too_many_providers_keeps_what_fit(void) {
    load("fixtures/api/snapshot-cyd.json");
    cb::ParseArena a = full_arena();
    a.provider_cap = 2;
    cb::UsageSnapshot s{};
    TEST_ASSERT_EQUAL(cb::ParseResult::Truncated, cb::parse_snapshot(g_json, g_json_len, a, s));
    TEST_ASSERT_EQUAL_INT(2, s.provider_count);
    TEST_ASSERT_EQUAL_STRING("claude", s.providers[0].id);
    TEST_ASSERT_EQUAL_STRING("codex", s.providers[1].id);
}

void test_too_many_progress_lines_keeps_what_fit(void) {
    load("fixtures/api/snapshot-cyd.json");
    cb::ParseArena a = full_arena();
    a.progress_cap = 2;
    cb::UsageSnapshot s{};
    TEST_ASSERT_EQUAL(cb::ParseResult::Truncated, cb::parse_snapshot(g_json, g_json_len, a, s));
    TEST_ASSERT_EQUAL_INT(3, s.provider_count);
    TEST_ASSERT_EQUAL_INT(2, s.providers[0].progress_count);
    TEST_ASSERT_EQUAL_STRING("Session", s.providers[0].progress[0].label);
    TEST_ASSERT_EQUAL_STRING("Weekly", s.providers[0].progress[1].label);
    TEST_ASSERT_EQUAL_INT(0, s.providers[1].progress_count);
}

void test_too_many_chart_points_keeps_what_fit(void) {
    load("fixtures/api/snapshot-cyd.json");
    cb::ParseArena a = full_arena();
    a.chart_cap = 10;
    cb::UsageSnapshot s{};
    TEST_ASSERT_EQUAL(cb::ParseResult::Truncated, cb::parse_snapshot(g_json, g_json_len, a, s));
    TEST_ASSERT_EQUAL_INT(10, s.providers[0].chart_count);
    TEST_ASSERT_EQUAL_STRING("Jul 22", s.providers[0].chart[0].label);
}

// --- refusing bad input -----------------------------------------------------

void test_error_payload_is_not_a_snapshot(void) {
    load("fixtures/api/error-no-snapshot.json");
    cb::ParseArena a = full_arena();
    cb::UsageSnapshot s{};
    const cb::ParseResult r = cb::parse_snapshot(g_json, g_json_len, a, s);
    TEST_ASSERT_TRUE(r == cb::ParseResult::Empty || r == cb::ParseResult::Malformed);
    TEST_ASSERT_EQUAL_INT(0, s.provider_count);
}

void test_empty_body(void) {
    cb::ParseArena a = full_arena();
    cb::UsageSnapshot s{};
    const cb::ParseResult r = cb::parse_snapshot("", 0, a, s);
    TEST_ASSERT_TRUE(r == cb::ParseResult::Empty || r == cb::ParseResult::Malformed);
    TEST_ASSERT_EQUAL_INT(0, s.provider_count);
    TEST_ASSERT_EQUAL(cb::ParseResult::Empty, cb::parse_snapshot(nullptr, 100, a, s));
}

void test_truncated_json_never_returns_ok(void) {
    load("fixtures/api/snapshot-cyd.json");
    const size_t cuts[] = {100, 500, 1000, 2000};
    for (size_t k = 0; k < sizeof cuts / sizeof cuts[0]; k++) {
        cb::ParseArena a = full_arena();
        cb::UsageSnapshot s{};
        const cb::ParseResult r = cb::parse_snapshot(g_json, cuts[k], a, s);
        char msg[96];
        snprintf(msg, sizeof msg, "cut at %zu bytes parsed as Ok", cuts[k]);
        TEST_ASSERT_TRUE_MESSAGE(r != cb::ParseResult::Ok, msg);
    }
    // and every cut, byte by byte, to be sure none of them wanders off
    for (size_t n = 0; n < g_json_len; n++) {
        cb::ParseArena a = full_arena();
        cb::UsageSnapshot s{};
        TEST_ASSERT_TRUE(cb::parse_snapshot(g_json, n, a, s) != cb::ParseResult::Ok);
    }
}

// --- shape the server may grow into ----------------------------------------

void test_empty_progress_array_keeps_the_provider(void) {
    const char* j = "{\"serverTime\":1787350883,\"providers\":["
                    "{\"id\":\"claude\",\"displayName\":\"Claude\",\"progress\":[]}]}";
    cb::ParseArena a = full_arena();
    cb::UsageSnapshot s{};
    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::parse_snapshot(j, strlen(j), a, s));
    TEST_ASSERT_EQUAL_INT(1, s.provider_count);
    TEST_ASSERT_EQUAL_STRING("claude", s.providers[0].id);
    TEST_ASSERT_EQUAL_INT(0, s.providers[0].progress_count);
    TEST_ASSERT_EQUAL_INT(0, s.providers[0].text_count);
}

void test_unknown_keys_are_ignored(void) {
    // A field added server-side must not break a board in the field.
    const char* j = "{\"serverTime\":1787350883,\"schema\":2,\"flags\":{\"beta\":true},"
                    "\"providers\":[{\"id\":\"claude\",\"displayName\":\"Claude\","
                    "\"plan\":\"Max 5x\",\"fetchedAt\":1787350828,\"nested\":[1,[2,{\"a\":null}]],"
                    "\"progress\":[{\"label\":\"Session\",\"used\":38,\"limit\":100,"
                    "\"resetsAt\":1787360400,\"periodSec\":18000,\"colour\":\"green\"}]}],"
                    "\"trailing\":\"ignored\"}";
    cb::ParseArena a = full_arena();
    cb::UsageSnapshot s{};
    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::parse_snapshot(j, strlen(j), a, s));
    TEST_ASSERT_EQUAL_INT(1, s.provider_count);
    TEST_ASSERT_EQUAL_INT(1, s.providers[0].progress_count);
    TEST_ASSERT_EQUAL_STRING("Session", s.providers[0].progress[0].label);
    TEST_ASSERT_EQUAL_INT64(1787360400000LL, s.providers[0].progress[0].resets_at_ms);
    TEST_ASSERT_EQUAL_INT64(1787350883000LL, s.server_time_ms);
}

void test_escapes_are_decoded(void) {
    // The live payload really does carry · in its text values.
    const char* j = "{\"serverTime\":1,\"providers\":[{\"id\":\"c\",\"displayName\":\"C\","
                    "\"text\":[{\"label\":\"Today\",\"value\":\"$264.21 \\u00b7 \\\"332\\\"\"}]}]}";
    cb::ParseArena a = full_arena();
    cb::UsageSnapshot s{};
    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::parse_snapshot(j, strlen(j), a, s));
    TEST_ASSERT_EQUAL_INT(1, s.providers[0].text_count);
    // non-ASCII collapses to one '?' -- the 5x7 font draws ASCII only, and a
    // multi-byte sequence would also lie to text_width().
    TEST_ASSERT_EQUAL_STRING("$264.21 ? \"332\"", s.providers[0].text[0].value);
}

void test_bad_escape_is_malformed(void) {
    const char* j = "{\"providers\":[{\"id\":\"c\",\"displayName\":\"\\uZZZZ\"}]}";
    cb::ParseArena a = full_arena();
    cb::UsageSnapshot s{};
    TEST_ASSERT_EQUAL(cb::ParseResult::Malformed, cb::parse_snapshot(j, strlen(j), a, s));
    TEST_ASSERT_EQUAL_INT(0, s.provider_count);
}

void test_empty_string_at_the_arena_boundary(void) {
    // Byte 0 is the reserved NUL and "abc" + NUL fills bytes 1..4, so the arena
    // is exactly full when the empty displayName asks for its terminator.
    const char* j = "{\"providers\":[{\"id\":\"abc\",\"displayName\":\"\"}]}";
    cb::ParseArena a = full_arena();
    a.text_bytes = 5;
    memset(g_text + a.text_bytes, 0x5A, sizeof g_text - a.text_bytes);
    cb::UsageSnapshot s{};
    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::parse_snapshot(j, strlen(j), a, s));
    TEST_ASSERT_EQUAL_INT(1, s.provider_count);
    TEST_ASSERT_EQUAL_STRING("abc", s.providers[0].id);
    TEST_ASSERT_EQUAL_STRING("", s.providers[0].display_name);
    for (size_t i = a.text_bytes; i < sizeof g_text; i++) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x5A, (uint8_t)g_text[i], "parser wrote past the arena end");
    }
}

void test_empty_strings_never_consume_the_arena(void) {
    const char* j = "{\"providers\":[{\"id\":\"\",\"displayName\":\"\","
                    "\"text\":[{\"label\":\"\",\"value\":\"\"}]}]}";
    cb::ParseArena a = full_arena();
    a.text_bytes = 1;   // room for the reserved NUL and nothing else
    memset(g_text + a.text_bytes, 0x5A, sizeof g_text - a.text_bytes);
    cb::UsageSnapshot s{};
    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::parse_snapshot(j, strlen(j), a, s));
    TEST_ASSERT_EQUAL_STRING("", s.providers[0].id);
    TEST_ASSERT_EQUAL_STRING("", s.providers[0].text[0].value);
    for (size_t i = a.text_bytes; i < sizeof g_text; i++) {
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x5A, (uint8_t)g_text[i], "parser wrote past the arena end");
    }
}

void test_unusable_string_arena_is_malformed(void) {
    // Truncated is documented as renderable, so a snapshot whose strings would
    // all be null or unterminated has to be refused outright instead.
    load("fixtures/api/snapshot-cyd.json");

    cb::ParseArena a = full_arena();
    a.text = nullptr; a.text_bytes = 0;
    cb::UsageSnapshot s{};
    TEST_ASSERT_EQUAL(cb::ParseResult::Malformed, cb::parse_snapshot(g_json, g_json_len, a, s));
    TEST_ASSERT_EQUAL_INT(0, s.provider_count);
    TEST_ASSERT_NULL(s.providers);

    cb::ParseArena b = full_arena();
    b.text_bytes = 0;
    cb::UsageSnapshot s2{};
    TEST_ASSERT_EQUAL(cb::ParseResult::Malformed, cb::parse_snapshot(g_json, g_json_len, b, s2));
    TEST_ASSERT_EQUAL_INT(0, s2.provider_count);
    TEST_ASSERT_NULL(s2.providers);
}

void test_trailing_backslash_is_malformed(void) {
    // The escape skip must not step the cursor past end.
    const char* j = "{\"providers\":[{\"id\":\"c\",\"displayName\":\"x\\";
    cb::ParseArena a = full_arena();
    cb::UsageSnapshot s{};
    TEST_ASSERT_EQUAL(cb::ParseResult::Malformed, cb::parse_snapshot(j, strlen(j), a, s));
}

void test_deep_nesting_does_not_blow_the_stack(void) {
    char j[2048];
    size_t n = 0;
    const char* head = "{\"providers\":[{\"id\":\"c\",\"junk\":";
    memcpy(j, head, strlen(head)); n = strlen(head);
    for (int i = 0; i < 400; i++) j[n++] = '[';
    cb::ParseArena a = full_arena();
    cb::UsageSnapshot s{};
    TEST_ASSERT_TRUE(cb::parse_snapshot(j, n, a, s) != cb::ParseResult::Ok);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_cyd_fixture_parses);
    RUN_TEST(test_epoch_seconds_become_milliseconds);
    RUN_TEST(test_watch_fixture_parses);
    RUN_TEST(test_every_string_lives_in_the_arena);
    RUN_TEST(test_survives_the_input_being_overwritten);
    RUN_TEST(test_string_arena_one_byte_short_is_truncated);
    RUN_TEST(test_too_many_providers_keeps_what_fit);
    RUN_TEST(test_too_many_progress_lines_keeps_what_fit);
    RUN_TEST(test_too_many_chart_points_keeps_what_fit);
    RUN_TEST(test_error_payload_is_not_a_snapshot);
    RUN_TEST(test_empty_body);
    RUN_TEST(test_truncated_json_never_returns_ok);
    RUN_TEST(test_empty_progress_array_keeps_the_provider);
    RUN_TEST(test_unknown_keys_are_ignored);
    RUN_TEST(test_escapes_are_decoded);
    RUN_TEST(test_bad_escape_is_malformed);
    RUN_TEST(test_empty_string_at_the_arena_boundary);
    RUN_TEST(test_empty_strings_never_consume_the_arena);
    RUN_TEST(test_unusable_string_arena_is_malformed);
    RUN_TEST(test_trailing_backslash_is_malformed);
    RUN_TEST(test_deep_nesting_does_not_blow_the_stack);
    return UNITY_END();
}
