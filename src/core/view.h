#pragma once
#include "core/screen.h"

namespace cb {

// The tab row is only 18 pixels tall, which is not a target anyone hits with
// a finger on a resistive panel. The zone that cycles providers is deeper
// than the row it belongs to, and deliberately: a tap that lands two pixels
// low should still do what it looked like it would do.
constexpr int TAB_TOUCH_H = 34;

// What the panel is showing. The device owns one of these and a tap is the
// only thing that changes it.
struct ViewState {
    int provider;
    Page page;
};

constexpr ViewState VIEW_INITIAL{0, Page::Stat};

// A tap, in panel pixels. The top band cycles providers, because that is
// where the provider names are; anywhere else swaps the page. Returns true
// when something actually changed, which is what the caller logs.
bool view_tap(ViewState& v, int x, int y, int provider_count);

// The provider list is server-side and can shrink between polls. Called
// before every render so the selection never points past the array.
void view_clamp(ViewState& v, int provider_count);

}  // namespace cb
