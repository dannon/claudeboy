#pragma once
#include <stdint.h>

// The radio, kept off the render loop's back. Association takes seconds and
// the screen has its own state to show meanwhile, so nothing here blocks:
// wifi_begin() starts the attempt and returns, wifi_poll() advances it a
// little each frame.
namespace cbnet {

// Start associating with the network in secrets.h. `timeout_ms` bounds one
// attempt, not this call. Returns false if the radio would not start.
bool wifi_begin(uint32_t timeout_ms);

// Drive the attempt: call once per loop. Handles the timeout, reassociation
// after a drop, and the retry after a failure.
void wifi_poll();

bool wifi_connected();

// Short uppercase state for the panel and the log: "WIFI OFF",
// "WIFI LINKING", "WIFI LINKED", "WIFI NO LINK".
const char* wifi_status_text();

}  // namespace cbnet
