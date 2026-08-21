#pragma once
#include "core/canvas.h"

namespace cbhost {

// Terminal size in character cells. False if not a tty.
bool tui_size(int& cols, int& rows);

// Integer downsample factor so the frame fits. One cell is two pixels tall,
// so the vertical budget is rows*2. Never returns less than 1.
int tui_scale(int cols, int rows);

void tui_begin();   // hide cursor, install SIGINT handler
void tui_end();     // restore cursor, reset colours
bool tui_interrupted();

void tui_draw(const cb::Canvas& c, int scale);

}  // namespace cbhost
