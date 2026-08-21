#include <unity.h>
#include <stdio.h>
#include <string.h>
#include "core/canvas.h"
#include "core/crt.h"
#include "core/fixture.h"
#include "core/frame.h"
#include "core/screen.h"

static uint8_t accum[cb::SCREEN_W * cb::SCREEN_H];
static uint8_t actual[cb::SCREEN_W * cb::SCREEN_H];
static uint8_t golden[cb::SCREEN_W * cb::SCREEN_H];
static uint8_t ring[9 * cb::SCREEN_W];
static const size_t N = sizeof actual;

// The strongest test in the suite: one byte of drift anywhere in the frame,
// from the layout, the font, the palette-independent intensities or any CRT
// stage, shows up here. A missing golden is a FAILURE, not a skip -- an
// absent file used to turn this into a silent pass, which is the one failure
// mode a golden test must not have. Regenerate deliberately with
// `.pio/build/native/program --bless` after looking at the result.
void test_ambient_matches_golden(void) {
    FILE* f = fopen("goldens/ambient-claude.raw", "rb");
    if (!f) {
        TEST_FAIL_MESSAGE("goldens/ambient-claude.raw is missing or unreadable -- "
                          "run it from the repo root, and if the golden really is "
                          "gone, regenerate it with .pio/build/native/program --bless");
    }
    const size_t got = fread(golden, 1, N, f);
    uint8_t extra = 0;
    const size_t tail = fread(&extra, 1, 1, f);
    fclose(f);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)N, (uint32_t)got);
    TEST_ASSERT_EQUAL_UINT32(0u, (uint32_t)tail);   // golden must be exactly one frame

    memset(accum, 0, N);
    cb::Canvas a(accum, cb::SCREEN_W, cb::SCREEN_H);
    cb::Canvas out(actual, cb::SCREEN_W, cb::SCREEN_H);
    cb::render_frame(a, out, cb::fixture_snapshot(), 0, cb::FIXTURE_REFERENCE_MS,
                     "14:44", cb::EffectParams::defaults(), 0, ring, sizeof ring);

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
