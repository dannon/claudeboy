#include "core/view.h"

namespace cb {

bool view_tap(ViewState& v, int x, int y, int provider_count) {
    if (x < 0 || x >= SCREEN_W || y < 0 || y >= SCREEN_H) return false;

    if (y < TAB_TOUCH_H) {
        // One provider is not a carousel. Falling through to the page swap
        // would be worse than doing nothing: the tap would land on the tabs
        // and change the page, which is not what the tabs say they do.
        if (provider_count <= 1) return false;
        v.provider = (v.provider + 1) % provider_count;
        return true;
    }

    v.page = v.page == Page::Stat ? Page::Data : Page::Stat;
    return true;
}

void view_clamp(ViewState& v, int provider_count) {
    if (provider_count <= 0) { v.provider = 0; return; }
    if (v.provider < 0 || v.provider >= provider_count) v.provider = 0;
}

}  // namespace cb
