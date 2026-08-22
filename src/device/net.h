#pragma once
#include <stddef.h>
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

// What fetch_snapshot() reports in `status` when it never got as far as an
// HTTP status of its own. Below -11, where HTTPClient's own error codes stop.
constexpr int FETCH_NO_LINK   = -100;   // the radio is not associated
constexpr int FETCH_NO_BEGIN  = -101;   // HTTPClient would not take the URL
constexpr int FETCH_NO_STREAM = -102;
constexpr int FETCH_NO_LENGTH = -103;   // no Content-Length, so possibly chunk-framed
constexpr int FETCH_TOO_LARGE = -104;   // the body does not fit `cap`
constexpr int FETCH_SHORT     = -105;   // the connection ended mid-body
constexpr int FETCH_TIMEOUT   = -106;   // the body stopped arriving

// One HTTPS GET of the snapshot into the caller's `buf`. Returns true only
// when a 200 arrived whole, and then `out_len` is the body length and
// `status` is 200. Otherwise `status` carries the HTTP status, an HTTPClient
// error, or one of the codes above, and `buf` holds nothing worth parsing.
//
// Synchronous, and the TLS handshake alone is several hundred milliseconds:
// call this between frames, never inside one. The session is torn down before
// this returns, because mbedtls holds 33,434 bytes of in/out buffer from the
// handshake until stop() and that is far too much to keep for the rest of a
// minute.
bool fetch_snapshot(char* buf, size_t cap, size_t& out_len, int& status);

}  // namespace cbnet
