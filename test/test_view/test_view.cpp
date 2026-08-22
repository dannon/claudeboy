#include <unity.h>
#include "core/view.h"

static cb::ViewState fresh() { return cb::VIEW_INITIAL; }

void test_the_board_boots_on_the_stat_page(void) {
    const cb::ViewState v = fresh();
    TEST_ASSERT_EQUAL_INT(0, v.provider);
    TEST_ASSERT_EQUAL(cb::Page::Stat, v.page);
}

void test_a_tap_below_the_tabs_swaps_the_page(void) {
    cb::ViewState v = fresh();
    TEST_ASSERT_TRUE(cb::view_tap(v, 160, 120, 3));
    TEST_ASSERT_EQUAL(cb::Page::Data, v.page);
    TEST_ASSERT_EQUAL_INT(0, v.provider);
    // and back again -- there are two pages, so one gesture has to do both
    TEST_ASSERT_TRUE(cb::view_tap(v, 160, 120, 3));
    TEST_ASSERT_EQUAL(cb::Page::Stat, v.page);
}

void test_the_page_swaps_from_anywhere_below_the_band(void) {
    const int spots[][2] = {{0, cb::TAB_TOUCH_H}, {cb::SCREEN_W - 1, cb::SCREEN_H - 1},
                            {0, cb::SCREEN_H - 1}, {cb::SCREEN_W - 1, cb::TAB_TOUCH_H}};
    for (const auto& s : spots) {
        cb::ViewState v = fresh();
        TEST_ASSERT_TRUE(cb::view_tap(v, s[0], s[1], 3));
        TEST_ASSERT_EQUAL(cb::Page::Data, v.page);
    }
}

void test_a_tap_on_the_tabs_cycles_providers(void) {
    cb::ViewState v = fresh();
    TEST_ASSERT_TRUE(cb::view_tap(v, 40, 5, 3));
    TEST_ASSERT_EQUAL_INT(1, v.provider);
    TEST_ASSERT_TRUE(cb::view_tap(v, 40, 5, 3));
    TEST_ASSERT_EQUAL_INT(2, v.provider);
    // wraps rather than sticking on the last one
    TEST_ASSERT_TRUE(cb::view_tap(v, 40, 5, 3));
    TEST_ASSERT_EQUAL_INT(0, v.provider);
    // and it never touches the page
    TEST_ASSERT_EQUAL(cb::Page::Stat, v.page);
}

void test_the_tab_band_is_deeper_than_the_tab_row(void) {
    // A finger on a resistive panel is not a five-pixel instrument.
    TEST_ASSERT_TRUE(cb::TAB_TOUCH_H > cb::TAB_H);
    cb::ViewState v = fresh();
    cb::view_tap(v, 40, cb::TAB_TOUCH_H - 1, 3);
    TEST_ASSERT_EQUAL_INT(1, v.provider);
    TEST_ASSERT_EQUAL(cb::Page::Stat, v.page);
}

void test_one_provider_makes_the_tab_band_inert(void) {
    // Falling through to the page swap would be worse than doing nothing:
    // the tap would land on the tabs and change something else.
    cb::ViewState v = fresh();
    TEST_ASSERT_FALSE(cb::view_tap(v, 40, 5, 1));
    TEST_ASSERT_EQUAL_INT(0, v.provider);
    TEST_ASSERT_EQUAL(cb::Page::Stat, v.page);
    TEST_ASSERT_FALSE(cb::view_tap(v, 40, 5, 0));
    TEST_ASSERT_EQUAL_INT(0, v.provider);
}

void test_a_tap_off_the_panel_is_ignored(void) {
    cb::ViewState v = fresh();
    const int spots[][2] = {{-1, 120}, {cb::SCREEN_W, 120}, {160, -1}, {160, cb::SCREEN_H}};
    for (const auto& s : spots) {
        TEST_ASSERT_FALSE(cb::view_tap(v, s[0], s[1], 3));
        TEST_ASSERT_EQUAL(cb::Page::Stat, v.page);
        TEST_ASSERT_EQUAL_INT(0, v.provider);
    }
}

void test_a_shrinking_provider_list_does_not_leave_us_pointing_past_it(void) {
    cb::ViewState v = fresh();
    cb::view_tap(v, 40, 5, 3);
    cb::view_tap(v, 40, 5, 3);
    TEST_ASSERT_EQUAL_INT(2, v.provider);
    // the server drops one between polls
    cb::view_clamp(v, 2);
    TEST_ASSERT_EQUAL_INT(0, v.provider);
    // and the zeroed snapshot the board holds before its first poll
    cb::view_clamp(v, 0);
    TEST_ASSERT_EQUAL_INT(0, v.provider);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_the_board_boots_on_the_stat_page);
    RUN_TEST(test_a_tap_below_the_tabs_swaps_the_page);
    RUN_TEST(test_the_page_swaps_from_anywhere_below_the_band);
    RUN_TEST(test_a_tap_on_the_tabs_cycles_providers);
    RUN_TEST(test_the_tab_band_is_deeper_than_the_tab_row);
    RUN_TEST(test_one_provider_makes_the_tab_band_inert);
    RUN_TEST(test_a_tap_off_the_panel_is_ignored);
    RUN_TEST(test_a_shrinking_provider_list_does_not_leave_us_pointing_past_it);
    return UNITY_END();
}
