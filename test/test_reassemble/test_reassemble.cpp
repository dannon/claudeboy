#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "core/reassemble.h"

static char g_buf[6144];
static cb::Reassembler g_re;

static void fresh(void) { cb::reassemble_init(g_re, g_buf, sizeof g_buf); }

// Build a START frame for `total` bytes. Little-endian, matching the Swift side.
static size_t start_frame(uint8_t* out, uint32_t total) {
    out[0] = cb::XFER_START;
    out[1] = (uint8_t)(total & 0xff);
    out[2] = (uint8_t)((total >> 8) & 0xff);
    out[3] = (uint8_t)((total >> 16) & 0xff);
    out[4] = (uint8_t)((total >> 24) & 0xff);
    return 5;
}

static size_t data_frame(uint8_t* out, const char* src, size_t n) {
    out[0] = cb::XFER_DATA;
    memcpy(out + 1, src, n);
    return n + 1;
}

// Push `payload` through as `chunk`-sized DATA frames and return the result of
// the final frame. Mirrors what the Swift helper does at a negotiated MTU.
static cb::XferResult push_all(const char* payload, size_t len, size_t chunk, size_t& out_len) {
    uint8_t f[600];
    size_t n = start_frame(f, (uint32_t)len);
    cb::XferResult r = cb::reassemble_frame(g_re, f, n, out_len);
    for (size_t off = 0; off < len; off += chunk) {
        const size_t take = (len - off) < chunk ? (len - off) : chunk;
        n = data_frame(f, payload + off, take);
        r = cb::reassemble_frame(g_re, f, n, out_len);
    }
    return r;
}

void setUp(void) { fresh(); }
void tearDown(void) {}

void test_a_whole_payload_reassembles(void) {
    const char* p = "{\"serverTime\":1,\"providers\":[]}";
    size_t len = 0;
    TEST_ASSERT_EQUAL(cb::XferResult::Complete, push_all(p, strlen(p), 8, len));
    TEST_ASSERT_EQUAL(strlen(p), len);
    TEST_ASSERT_EQUAL_MEMORY(p, g_buf, len);
}

void test_chunk_size_does_not_change_the_result(void) {
    const char* p = "0123456789abcdefghijklmnopqrstuvwxyz";
    for (size_t chunk = 1; chunk <= 64; chunk++) {
        fresh();
        size_t len = 0;
        TEST_ASSERT_EQUAL(cb::XferResult::Complete, push_all(p, strlen(p), chunk, len));
        TEST_ASSERT_EQUAL(strlen(p), len);
        TEST_ASSERT_EQUAL_MEMORY(p, g_buf, len);
    }
}

void test_data_before_start_is_refused(void) {
    uint8_t f[16];
    size_t len = 0;
    const size_t n = data_frame(f, "hello", 5);
    TEST_ASSERT_EQUAL(cb::XferResult::NoTransfer, cb::reassemble_frame(g_re, f, n, len));
}

void test_a_payload_larger_than_the_buffer_is_refused(void) {
    uint8_t f[8];
    size_t len = 0;
    const size_t n = start_frame(f, (uint32_t)sizeof g_buf + 1);
    TEST_ASSERT_EQUAL(cb::XferResult::TooLarge, cb::reassemble_frame(g_re, f, n, len));
}

void test_a_payload_exactly_the_buffer_size_is_accepted(void) {
    static char p[sizeof g_buf];
    memset(p, 'x', sizeof p);
    size_t len = 0;
    TEST_ASSERT_EQUAL(cb::XferResult::Complete, push_all(p, sizeof p, 244, len));
    TEST_ASSERT_EQUAL(sizeof p, len);
    TEST_ASSERT_EQUAL_MEMORY(p, g_buf, sizeof p);
}

void test_an_overflowed_transfer_does_not_wedge_the_reassembler(void) {
    uint8_t f[64];
    size_t len = 0;
    size_t n = start_frame(f, 4);
    cb::reassemble_frame(g_re, f, n, len);
    n = data_frame(f, "toolong", 7);
    TEST_ASSERT_EQUAL(cb::XferResult::Overflow, cb::reassemble_frame(g_re, f, n, len));
    // The aborted transfer must be gone -- a DATA frame right after Overflow,
    // with no new START in between, has nothing to append to. (A subsequent
    // START would reset this state on its own, so this has to be checked
    // before one arrives to actually exercise the Overflow branch's reset.)
    n = data_frame(f, "x", 1);
    TEST_ASSERT_EQUAL(cb::XferResult::NoTransfer, cb::reassemble_frame(g_re, f, n, len));
    // A sane transfer right afterwards still works.
    const char* p = "ok";
    TEST_ASSERT_EQUAL(cb::XferResult::Complete, push_all(p, strlen(p), 4, len));
    TEST_ASSERT_EQUAL(2, len);
}

void test_a_truncated_start_mid_transfer_abandons_it(void) {
    uint8_t f[64];
    size_t len = 0;
    size_t n = start_frame(f, 10);
    cb::reassemble_frame(g_re, f, n, len);
    n = data_frame(f, "abc", 3);
    TEST_ASSERT_EQUAL(cb::XferResult::NeedMore, cb::reassemble_frame(g_re, f, n, len));
    f[0] = cb::XFER_START;   // truncated START: opcode only, no length bytes
    TEST_ASSERT_EQUAL(cb::XferResult::Malformed, cb::reassemble_frame(g_re, f, 1, len));
    // The partial transfer must be gone -- a following DATA frame has nothing to append to.
    n = data_frame(f, "x", 1);
    TEST_ASSERT_EQUAL(cb::XferResult::NoTransfer, cb::reassemble_frame(g_re, f, n, len));
}

void test_a_refused_start_does_not_wedge_the_reassembler(void) {
    uint8_t f[8];
    size_t len = 0;
    size_t n = start_frame(f, (uint32_t)sizeof g_buf + 1);
    cb::reassemble_frame(g_re, f, n, len);
    // A sane transfer right afterwards still works.
    const char* p = "ok";
    TEST_ASSERT_EQUAL(cb::XferResult::Complete, push_all(p, strlen(p), 4, len));
    TEST_ASSERT_EQUAL(2, len);
}

void test_a_second_start_abandons_the_partial_transfer(void) {
    uint8_t f[64];
    size_t len = 0;
    size_t n = start_frame(f, 10);
    cb::reassemble_frame(g_re, f, n, len);
    n = data_frame(f, "abc", 3);
    TEST_ASSERT_EQUAL(cb::XferResult::NeedMore, cb::reassemble_frame(g_re, f, n, len));
    // The Mac reconnected and started again; the three stale bytes must go.
    const char* p = "fresh";
    TEST_ASSERT_EQUAL(cb::XferResult::Complete, push_all(p, strlen(p), 2, len));
    TEST_ASSERT_EQUAL(5, len);
    TEST_ASSERT_EQUAL_MEMORY(p, g_buf, 5);
}

void test_more_data_than_declared_is_refused(void) {
    uint8_t f[64];
    size_t len = 0;
    size_t n = start_frame(f, 4);
    cb::reassemble_frame(g_re, f, n, len);
    n = data_frame(f, "toolong", 7);
    TEST_ASSERT_EQUAL(cb::XferResult::Overflow, cb::reassemble_frame(g_re, f, n, len));
}

void test_junk_is_refused(void) {
    uint8_t f[8];
    size_t len = 0;
    f[0] = 0x7f;   // not an opcode we know
    TEST_ASSERT_EQUAL(cb::XferResult::Malformed, cb::reassemble_frame(g_re, f, 1, len));
    TEST_ASSERT_EQUAL(cb::XferResult::Malformed, cb::reassemble_frame(g_re, f, 0, len));
    f[0] = cb::XFER_START;   // START without its four length bytes
    TEST_ASSERT_EQUAL(cb::XferResult::Malformed, cb::reassemble_frame(g_re, f, 3, len));
    f[0] = cb::XFER_START;
    const size_t n = start_frame(f, 0);   // a snapshot is never zero bytes
    TEST_ASSERT_EQUAL(cb::XferResult::Malformed, cb::reassemble_frame(g_re, f, n, len));
}

void test_the_live_fixture_survives_a_round_trip(void) {
    FILE* fp = fopen("fixtures/api/snapshot-cyd.json", "rb");
    TEST_ASSERT_NOT_NULL_MESSAGE(fp, "fixtures/api/snapshot-cyd.json is missing -- run from the repo root");
    static char src[16384];
    const size_t n = fread(src, 1, sizeof src, fp);
    fclose(fp);
    size_t len = 0;
    // 244 is a realistic negotiated MTU less the ATT and opcode overhead.
    TEST_ASSERT_EQUAL(cb::XferResult::Complete, push_all(src, n, 244, len));
    TEST_ASSERT_EQUAL(n, len);
    TEST_ASSERT_EQUAL_MEMORY(src, g_buf, n);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_a_whole_payload_reassembles);
    RUN_TEST(test_chunk_size_does_not_change_the_result);
    RUN_TEST(test_data_before_start_is_refused);
    RUN_TEST(test_a_payload_larger_than_the_buffer_is_refused);
    RUN_TEST(test_a_payload_exactly_the_buffer_size_is_accepted);
    RUN_TEST(test_a_refused_start_does_not_wedge_the_reassembler);
    RUN_TEST(test_an_overflowed_transfer_does_not_wedge_the_reassembler);
    RUN_TEST(test_a_second_start_abandons_the_partial_transfer);
    RUN_TEST(test_a_truncated_start_mid_transfer_abandons_it);
    RUN_TEST(test_more_data_than_declared_is_refused);
    RUN_TEST(test_junk_is_refused);
    RUN_TEST(test_the_live_fixture_survives_a_round_trip);
    return UNITY_END();
}
