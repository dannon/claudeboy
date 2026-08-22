#pragma once
#include <stdint.h>

namespace cbtouch {

// The XPT2046 on this board has its own four wires and shares nothing with
// the panel. TFT_eSPI's TOUCH_CS support drives the display's bus, which this
// chip is not on -- switching it on reads zeroes forever and says nothing.
void begin();

// One tap, reported once on the press that produced it, in panel pixels.
// False when nothing new has happened, which is almost every call.
bool tapped(int& x, int& y);

// Raw controller counts from the last press, for calibrating the two maps
// below. Zero until something has been touched.
void last_raw(int& x, int& y);

// Whether the glass is being touched at this instant. The heartbeat prints
// it so a silent panel can be told from a panel nobody has touched -- the
// two look identical from the tap log alone.
bool down();

}  // namespace cbtouch
