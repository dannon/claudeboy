#pragma once
#include <stddef.h>

// One interface, two implementations, chosen at build time by build_src_filter.
// WiFi pulls -- an HTTPS GET on a timer -- and BLE pushes, the Mac writing when
// the numbers move. main.cpp is written against neither: it asks once a loop
// whether a whole new payload has arrived and does the same thing either way.
//
// Each implementation owns its own cadence and its own failure logging, because
// "retry in fifteen seconds" and "wait for the Mac to say something" have
// nothing useful in common.
namespace cbxport {

// Start the radio. Does not block; poll() drives whatever comes next.
void begin();

// Advance the transport. Call once per loop.
void poll();

// True exactly once per completed payload, with `len` bytes written into `buf`.
// False, and `buf` untouched, when there is nothing new.
bool take_snapshot(char* buf, size_t cap, size_t& len);

// Short uppercase state for the panel and the log.
const char* status_text();

}  // namespace cbxport
