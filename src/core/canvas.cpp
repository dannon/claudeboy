#include "core/canvas.h"
#include <string.h>

namespace cb {

void Canvas::clear(uint8_t v) { memset(buf_, v, static_cast<size_t>(w_) * h_); }

void Canvas::decay(uint8_t amount) {
    const size_t n = static_cast<size_t>(w_) * h_;
    for (size_t i = 0; i < n; i++) buf_[i] = buf_[i] > amount ? buf_[i] - amount : 0;
}

void Canvas::plot(int x, int y, uint8_t v) {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
    uint8_t& d = buf_[static_cast<size_t>(y) * w_ + x];
    if (v > d) d = v;
}

uint8_t Canvas::at(int x, int y) const {
    if (x < 0 || y < 0 || x >= w_ || y >= h_) return 0;
    return buf_[static_cast<size_t>(y) * w_ + x];
}

void Canvas::hline(int x, int y, int w, uint8_t v) {
    for (int i = 0; i < w; i++) plot(x + i, y, v);
}

void Canvas::vline(int x, int y, int h, uint8_t v) {
    for (int i = 0; i < h; i++) plot(x, y + i, v);
}

void Canvas::rect(int x, int y, int w, int h, uint8_t v) {
    if (w <= 0 || h <= 0) return;
    hline(x, y, w, v);
    hline(x, y + h - 1, w, v);
    vline(x, y, h, v);
    vline(x + w - 1, y, h, v);
}

void Canvas::fill(int x, int y, int w, int h, uint8_t v) {
    for (int j = 0; j < h; j++) hline(x, y + j, w, v);
}

}  // namespace cb
