#pragma once
#include <stdint.h>
#include "core/canvas.h"

namespace cbhost {

// Writes an 8-bit RGB PNG. rgb is w*h*3 bytes, row-major, no padding.
bool write_png_rgb(const char* path, const uint8_t* rgb, int w, int h);

// Maps the canvas through the phosphor palette and writes it.
bool write_png_from_canvas(const char* path, const cb::Canvas& c);

}  // namespace cbhost
