#pragma once
#include <stdint.h>

namespace cb {

// An 8-bit intensity surface. Caller owns the buffer; Canvas never allocates.
// plot() and the shape helpers blend with max(), so drawing over a decaying
// phosphor brightens it rather than replacing it.
class Canvas {
public:
    Canvas(uint8_t* buf, int w, int h)
        : buf_(buf),
          w_((buf && w > 0 && h > 0) ? w : 0),
          h_((buf && w > 0 && h > 0) ? h : 0) {
        // No MMU on device: degrade to 0x0 on bad args to avoid silent buffer
        // corruption from out-of-bounds writes. Zero dimensions make all ops no-ops.
    }

    void clear(uint8_t v = 0);
    void decay(uint8_t amount);                     // saturating subtract
    void plot(int x, int y, uint8_t v);
    void hline(int x, int y, int w, uint8_t v);
    void vline(int x, int y, int h, uint8_t v);
    void rect(int x, int y, int w, int h, uint8_t v);   // outline
    void fill(int x, int y, int w, int h, uint8_t v);   // solid

    uint8_t at(int x, int y) const;
    uint8_t* data() { return buf_; }
    const uint8_t* data() const { return buf_; }
    int width() const { return w_; }
    int height() const { return h_; }

private:
    uint8_t* buf_;
    int w_, h_;
};

}  // namespace cb
