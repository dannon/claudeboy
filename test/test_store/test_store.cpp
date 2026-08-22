#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "core/store.h"
#include "core/screen.h"

// Both halves, declared exactly as the device declares them: static, so they
// outlive every render that reads through them.
static cb::ArenaBytes g_a;
static cb::ArenaBytes g_b;
static cb::SnapshotStore g_store;

static char g_json[16384];
static size_t g_json_len;

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

static void fresh_store(void) {
    memset(&g_a, 0, sizeof g_a);
    memset(&g_b, 0, sizeof g_b);
    g_store = cb::SnapshotStore();
    cb::store_init(g_store, g_a, g_b);
}

static bool inside(const void* p, const cb::ArenaBytes& a) {
    const char* c = (const char*)p;
    return c >= a.text && c < a.text + sizeof a.text;
}

// --- the payload the board actually has to hold -----------------------------

void test_the_live_cyd_payload_fits_the_device_arena(void) {
    fresh_store();
    load("fixtures/api/snapshot-cyd.json");
    const cb::ParseResult r = cb::store_accept(g_store, g_json, g_json_len);
    // Truncated here would mean the arena capacities in core/store.h no longer
    // cover the live payload, which on the board reads as data going stale
    // forever with nothing on screen to explain it.
    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, r);

    const cb::UsageSnapshot& s = cb::store_current(g_store);
    TEST_ASSERT_EQUAL_INT(3, s.provider_count);
    TEST_ASSERT_EQUAL_STRING("claude", s.providers[0].id);
    TEST_ASSERT_EQUAL_STRING("Max 5x", s.providers[0].plan);
    TEST_ASSERT_EQUAL_INT(3, s.providers[0].progress_count);
    TEST_ASSERT_EQUAL_INT(31, s.providers[0].chart_count);
    TEST_ASSERT_EQUAL_INT64(1787350883000LL, s.server_time_ms);

    int progress = 0, text = 0, chart = 0;
    for (int i = 0; i < s.provider_count; i++) {
        progress += s.providers[i].progress_count;
        text += s.providers[i].text_count;
        chart += s.providers[i].chart_count;
    }
    // Headroom, not a fit: providers appear upstream on their own, and
    // Antigravity showed up mid-design.
    TEST_ASSERT_TRUE(progress * 2 <= cb::STORE_PROGRESS);
    TEST_ASSERT_TRUE(text * 2 <= cb::STORE_TEXT_LINES);
    TEST_ASSERT_TRUE(chart * 2 <= cb::STORE_CHART);
}

// --- what is on screen before, and after, each kind of reply ----------------

void test_nothing_is_shown_before_the_first_reply(void) {
    fresh_store();
    const cb::UsageSnapshot& s = cb::store_current(g_store);
    TEST_ASSERT_EQUAL_INT(0, s.provider_count);
    TEST_ASSERT_NULL(s.providers);
    TEST_ASSERT_EQUAL(cb::Freshness::NoSignal, cb::freshness_of(s, 1787350883000LL));
}

void test_a_malformed_reply_leaves_the_shown_snapshot_intact(void) {
    fresh_store();
    load("fixtures/api/snapshot-cyd.json");
    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::store_accept(g_store, g_json, g_json_len));
    const cb::UsageSnapshot& s = cb::store_current(g_store);
    const cb::Provider* was = s.providers;

    // The same payload cut off mid-chart: a connection dropped part way
    // through the body. It parses far enough to fill most of an arena before
    // it gives up, which with one arena is the bug -- the strings the renderer
    // is holding get rewritten under it and nobody finds out.
    TEST_ASSERT_TRUE(g_json_len > 2000);
    TEST_ASSERT_TRUE(cb::store_accept(g_store, g_json, 2000) != cb::ParseResult::Ok);

    TEST_ASSERT_EQUAL_PTR(was, cb::store_current(g_store).providers);
    TEST_ASSERT_EQUAL_INT(3, cb::store_current(g_store).provider_count);
    TEST_ASSERT_EQUAL_STRING("claude", cb::store_current(g_store).providers[0].id);
    TEST_ASSERT_EQUAL_STRING("Session", cb::store_current(g_store).providers[0].progress[0].label);
    TEST_ASSERT_EQUAL_STRING("Aug 21", cb::store_current(g_store).providers[0].chart[30].label);
}

void test_the_no_snapshot_error_body_does_not_replace_the_numbers(void) {
    fresh_store();
    load("fixtures/api/snapshot-cyd.json");
    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::store_accept(g_store, g_json, g_json_len));

    load("fixtures/api/error-no-snapshot.json");
    TEST_ASSERT_EQUAL(cb::ParseResult::Empty, cb::store_accept(g_store, g_json, g_json_len));
    TEST_ASSERT_EQUAL_INT(3, cb::store_current(g_store).provider_count);
    TEST_ASSERT_EQUAL_STRING("claude", cb::store_current(g_store).providers[0].id);
}

void test_an_empty_body_does_not_replace_the_numbers(void) {
    fresh_store();
    load("fixtures/api/snapshot-cyd.json");
    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::store_accept(g_store, g_json, g_json_len));
    TEST_ASSERT_EQUAL(cb::ParseResult::Empty, cb::store_accept(g_store, "", 0));
    TEST_ASSERT_EQUAL_INT(3, cb::store_current(g_store).provider_count);
}

void test_a_truncated_parse_is_not_put_on_screen(void) {
    fresh_store();
    load("fixtures/api/snapshot-cyd.json");
    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::store_accept(g_store, g_json, g_json_len));

    // Valid JSON carrying more string than the arena holds. Renderable, per
    // core/parse.h, but incomplete -- and half a screen of numbers is a
    // quieter lie than the same numbers marked stale.
    static char big[cb::STORE_TEXT_BYTES + 512];
    const char* head = "{\"serverTime\":1787350883,\"providers\":[{\"id\":\"claude\","
                       "\"displayName\":\"Claude\",\"fetchedAt\":1787350828,"
                       "\"text\":[{\"label\":\"Today\",\"value\":\"";
    const size_t hn = strlen(head);
    memcpy(big, head, hn);
    const size_t pad = sizeof big - hn - 8;
    memset(big + hn, 'x', pad);
    memcpy(big + hn + pad, "\"}]}]}", 7);
    TEST_ASSERT_EQUAL(cb::ParseResult::Truncated,
                      cb::store_accept(g_store, big, strlen(big)));
    TEST_ASSERT_EQUAL_INT(3, cb::store_current(g_store).provider_count);
    TEST_ASSERT_EQUAL_STRING("claude", cb::store_current(g_store).providers[0].id);

    // And the store is not wedged: the next whole payload is adopted normally.
    load("fixtures/api/snapshot-cyd.json");
    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::store_accept(g_store, g_json, g_json_len));
}

// --- the halves themselves --------------------------------------------------

void test_successive_replies_alternate_halves(void) {
    fresh_store();
    load("fixtures/api/snapshot-cyd.json");

    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::store_accept(g_store, g_json, g_json_len));
    TEST_ASSERT_TRUE(inside(cb::store_current(g_store).providers[0].id, g_a));

    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::store_accept(g_store, g_json, g_json_len));
    TEST_ASSERT_TRUE(inside(cb::store_current(g_store).providers[0].id, g_b));

    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::store_accept(g_store, g_json, g_json_len));
    TEST_ASSERT_TRUE(inside(cb::store_current(g_store).providers[0].id, g_a));
}

void test_a_bad_reply_does_not_burn_the_shown_half(void) {
    fresh_store();
    load("fixtures/api/snapshot-cyd.json");
    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::store_accept(g_store, g_json, g_json_len));
    TEST_ASSERT_TRUE(inside(cb::store_current(g_store).providers[0].id, g_a));

    TEST_ASSERT_EQUAL(cb::ParseResult::Empty, cb::store_accept(g_store, "{}", 2));
    // Still half A, so the next good reply lands in B and A stays readable
    // right up to the moment it is replaced.
    TEST_ASSERT_TRUE(inside(cb::store_current(g_store).providers[0].id, g_a));
    TEST_ASSERT_EQUAL(cb::ParseResult::Ok, cb::store_accept(g_store, g_json, g_json_len));
    TEST_ASSERT_TRUE(inside(cb::store_current(g_store).providers[0].id, g_b));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_the_live_cyd_payload_fits_the_device_arena);
    RUN_TEST(test_nothing_is_shown_before_the_first_reply);
    RUN_TEST(test_a_malformed_reply_leaves_the_shown_snapshot_intact);
    RUN_TEST(test_the_no_snapshot_error_body_does_not_replace_the_numbers);
    RUN_TEST(test_an_empty_body_does_not_replace_the_numbers);
    RUN_TEST(test_a_truncated_parse_is_not_put_on_screen);
    RUN_TEST(test_successive_replies_alternate_halves);
    RUN_TEST(test_a_bad_reply_does_not_burn_the_shown_half);
    return UNITY_END();
}
