#include <unity.h>
#include <string.h>
#include "core/canvas.h"
#include "core/fixture.h"
#include "core/vaultboy.h"

static uint8_t buf[cb::SCREEN_W * cb::SCREEN_H];
static cb::Canvas mk() { memset(buf, 0, sizeof buf); return cb::Canvas(buf, cb::SCREEN_W, cb::SCREEN_H); }
static int lit_in(const cb::Canvas& c, int x0, int y0, int w, int h) {
    int n = 0;
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) if (c.at(x, y)) n++;
    return n;
}
static const int64_t REF  = cb::FIXTURE_REFERENCE_MS;
static const int64_t HOUR = 3600LL * 1000;

void setUp(void) {}
void tearDown(void) {}

// A provider carrying exactly the windows a test hands it.
static cb::ProgressLine g_lines[4];
static cb::Provider with(const cb::ProgressLine* lines, int n) {
    for (int i = 0; i < n; i++) g_lines[i] = lines[i];
    return cb::Provider{"x", "X", "", REF, g_lines, n, nullptr, 0, nullptr, 0};
}

void test_a_comfortable_provider_gets_a_thumbs_up(void) {
    // Barely touched, most of the window left to run.
    const cb::ProgressLine l[] = {{"SESSION", 5, 100, REF + 4 * HOUR, 5 * HOUR}};
    TEST_ASSERT_EQUAL(cb::BoyMood::Fine, cb::boy_mood(with(l, 1), REF));
}

void test_one_window_burning_fries_the_whole_face(void) {
    // Session is fine, weekly is spent. The fresh session block does not make
    // the weekly ration any less gone.
    const cb::ProgressLine l[] = {
        {"SESSION", 5,   100, REF + 4 * HOUR,  5 * HOUR},
        {"WEEKLY",  100, 100, REF + 40 * HOUR, 168 * HOUR},
    };
    TEST_ASSERT_EQUAL(cb::BoyMood::Fried, cb::boy_mood(with(l, 2), REF));
}

void test_a_window_too_young_to_judge_gets_a_neutral_face(void) {
    // One minute into a five-hour block: no verdict is available, and a
    // thumbs up would be a verdict.
    const cb::ProgressLine l[] = {{"SESSION", 1, 100, REF + 299LL * 60 * 1000, 5 * HOUR}};
    TEST_ASSERT_EQUAL(cb::BoyMood::Steady, cb::boy_mood(with(l, 1), REF));
}

void test_nothing_readable_is_not_good_news(void) {
    const cb::ProgressLine l[] = {{"BROKEN", 12, 0, REF + HOUR, 2 * HOUR}};
    TEST_ASSERT_EQUAL(cb::BoyMood::Steady, cb::boy_mood(with(l, 1), REF));
    cb::Provider empty{"x", "X", "", REF, nullptr, 0, nullptr, 0, nullptr, 0};
    TEST_ASSERT_EQUAL(cb::BoyMood::Steady, cb::boy_mood(empty, REF));
}

void test_every_pose_stays_inside_its_box(void) {
    const cb::BoyMood moods[] = {cb::BoyMood::Fine, cb::BoyMood::Steady, cb::BoyMood::Fried};
    for (cb::BoyMood m : moods) {
        cb::Canvas c = mk();
        cb::draw_vault_boy(c, 40, 60, m);
        TEST_ASSERT_TRUE(lit_in(c, 40, 60, cb::VB_W, cb::VB_H) > 300);
        TEST_ASSERT_EQUAL_INT(lit_in(c, 0, 0, cb::SCREEN_W, cb::SCREEN_H),
                              lit_in(c, 40, 60, cb::VB_W, cb::VB_H));
    }
}

void test_every_art_row_is_the_full_width(void) {
    // A row short of VB_W clips that scanline of the sprite and nothing says
    // so -- draw_vault_boy() stops at the terminator and carries on. The
    // arrays are pasted in from tools/make_vaultboy.py, so this is the check
    // that the paste was complete. It replaces an older assertion that every
    // pose had to touch all four edges of its box, which only held while the
    // art was hand-drawn to fill it: the traced poses are letterboxed, and
    // making them reach the edges would mean stretching them off-aspect.
    const cb::BoyMood moods[] = {cb::BoyMood::Fine, cb::BoyMood::Steady, cb::BoyMood::Fried};
    for (cb::BoyMood m : moods) {
        const char* const* art = cb::boy_art(m);
        TEST_ASSERT_NOT_NULL(art);
        for (int y = 0; y < cb::VB_H; y++) {
            TEST_ASSERT_NOT_NULL(art[y]);
            TEST_ASSERT_EQUAL_INT(cb::VB_W, (int)strlen(art[y]));
        }
    }
}

void test_the_three_poses_are_actually_different(void) {
    static uint8_t fine[sizeof buf], steady[sizeof buf];
    cb::Canvas a = mk(); cb::draw_vault_boy(a, 40, 60, cb::BoyMood::Fine);
    memcpy(fine, buf, sizeof buf);
    cb::Canvas b = mk(); cb::draw_vault_boy(b, 40, 60, cb::BoyMood::Steady);
    memcpy(steady, buf, sizeof buf);
    cb::Canvas d = mk(); cb::draw_vault_boy(d, 40, 60, cb::BoyMood::Fried);
    TEST_ASSERT_TRUE(memcmp(fine, steady, sizeof buf) != 0);
    TEST_ASSERT_TRUE(memcmp(steady, buf, sizeof buf) != 0);
    TEST_ASSERT_TRUE(memcmp(fine, buf, sizeof buf) != 0);
}

void test_drawing_off_the_edge_does_not_write_past_the_canvas(void) {
    cb::Canvas c = mk();
    cb::draw_vault_boy(c, cb::SCREEN_W - 5, cb::SCREEN_H - 5, cb::BoyMood::Fried);
    cb::draw_vault_boy(c, -20, -20, cb::BoyMood::Fine);
    TEST_ASSERT_TRUE(lit_in(c, 0, 0, cb::SCREEN_W, cb::SCREEN_H) > 0);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_a_comfortable_provider_gets_a_thumbs_up);
    RUN_TEST(test_one_window_burning_fries_the_whole_face);
    RUN_TEST(test_a_window_too_young_to_judge_gets_a_neutral_face);
    RUN_TEST(test_nothing_readable_is_not_good_news);
    RUN_TEST(test_every_pose_stays_inside_its_box);
    RUN_TEST(test_every_art_row_is_the_full_width);
    RUN_TEST(test_the_three_poses_are_actually_different);
    RUN_TEST(test_drawing_off_the_edge_does_not_write_past_the_canvas);
    return UNITY_END();
}
