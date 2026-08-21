#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "core/canvas.h"
#include "core/crt.h"
#include "core/fixture.h"
#include "core/screen.h"

static uint8_t actual[cb::SCREEN_W * cb::SCREEN_H];
static uint8_t golden[cb::SCREEN_W * cb::SCREEN_H];
static uint8_t ring[9 * cb::SCREEN_W];
static const size_t N = sizeof actual;

// No golden has been blessed yet -- the CRT effect defaults (bloom_strength
// in particular) are still being tuned against the real panel, and blessing
// before that would freeze a look we already know is wrong. Skip cleanly
// rather than fail until a human runs `--bless` on approved defaults.
void test_ambient_matches_golden(void) {
    FILE* f = fopen("goldens/ambient-claude.raw", "rb");
    if (!f) {
        TEST_IGNORE_MESSAGE("no golden yet -- run .pio/build/native/program --bless "
                            "once EffectParams::defaults() is approved");
    }
    const size_t got = fread(golden, 1, N, f);
    fclose(f);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)N, (uint32_t)got);

    memset(actual, 0, N);
    cb::Canvas c(actual, cb::SCREEN_W, cb::SCREEN_H);
    cb::render_ambient(c, cb::fixture_snapshot(), 0, cb::FIXTURE_REFERENCE_MS, "14:44");
    cb::post_process(c, cb::EffectParams::defaults(), 0, ring, sizeof ring);

    size_t diff = 0;
    for (size_t i = 0; i < N; i++) if (actual[i] != golden[i]) diff++;
    if (diff) {
        FILE* o = fopen("out/golden-actual.raw", "wb");
        if (o) { fwrite(actual, 1, N, o); fclose(o); }
        char msg[128];
        snprintf(msg, sizeof msg,
                 "%zu of %zu pixels differ; wrote out/golden-actual.raw", diff, N);
        TEST_FAIL_MESSAGE(msg);
    }
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ambient_matches_golden);
    return UNITY_END();
}
