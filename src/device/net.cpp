#include "device/net.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include "core/clock.h"
#include "esp_heap_caps.h"

#if !__has_include("device/secrets.h")
#error "src/device/secrets.h is missing: copy src/device/secrets.h.example to src/device/secrets.h and fill it in. The real file is git-ignored."
#endif
#include "device/secrets.h"

namespace cbnet {
namespace {

enum class State { Off, Linking, Linked, NoLink };

State g_state = State::Off;
uint32_t g_timeout_ms = 0;
uint32_t g_since_ms = 0;   // millis() at the last state change

// After a failed association, wait this long before trying again. Long enough
// that a router which is simply off does not keep the radio hot, short enough
// that the board recovers on its own once it is back.
const uint32_t RETRY_MS = 30000;

bool elapsed(uint32_t since, uint32_t span) { return cb::clock_elapsed(since, millis(), span); }

void enter(State s) {
    g_state = s;
    g_since_ms = millis();
}

bool associate() {
    if (!WiFi.mode(WIFI_STA)) return false;
    WiFi.setAutoReconnect(false);   // this poll owns reconnection, so the states stay honest
    if (WiFi.begin(CB_WIFI_SSID, CB_WIFI_PASSWORD) == WL_CONNECT_FAILED) return false;
    enter(State::Linking);
    return true;
}

void report_association() {
    const IPAddress ip = WiFi.localIP();
    // The two heap figures are the whole point of this build. ESP.getFreeHeap()
    // would flatter them by ~73KB of 32-bit-only IRAM that cannot back a
    // uint8_t[], so ask for the 8-bit-capable heap specifically.
    Serial.printf("claudeboy: wifi linked, ip %u.%u.%u.%u, rssi %d dBm, "
                  "MALLOC_CAP_8BIT free %u bytes, largest block %u bytes\n",
                  (unsigned)ip[0], (unsigned)ip[1], (unsigned)ip[2], (unsigned)ip[3],
                  (int)WiFi.RSSI(),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

// Both live at file scope rather than on the stack of the fetch: the Arduino
// loop task gets 8KB, and a WiFiClientSecure carries the mbedtls contexts by
// value. They hold nothing between polls -- stop() releases the session.
WiFiClientSecure g_tls;
HTTPClient g_http;

// A fetch blocks the render loop, so these three add up to the longest the
// panel can sit on one frame: patience for a slow reply, not for a dead one.
// A handshake on this board runs one to three seconds, so eight is generous
// without being a visible hang.
const uint32_t CONNECT_TIMEOUT_MS   = 5000;
const uint32_t HANDSHAKE_TIMEOUT_S  = 8;
const uint32_t READ_TIMEOUT_MS      = 5000;

void release() {
    g_http.end();
    g_tls.stop();
}

bool read_body(char* buf, size_t cap, size_t& out_len, int& status) {
    WiFiClient* s = g_http.getStreamPtr();
    if (!s) { status = FETCH_NO_STREAM; return false; }

    // This is the raw stream, so a chunked body would arrive with its chunk
    // framing still in it and parse as garbage. The Worker returns a
    // fixed-length JSON body; anything else is refused rather than
    // half-understood.
    const int len = g_http.getSize();
    if (len < 0) { status = FETCH_NO_LENGTH; return false; }
    if ((size_t)len > cap) { status = FETCH_TOO_LARGE; return false; }

    size_t n = 0;
    uint32_t last_ms = millis();
    while (n < (size_t)len) {
        const int avail = s->available();
        if (avail > 0) {
            size_t want = (size_t)avail;
            if (want > (size_t)len - n) want = (size_t)len - n;
            // read(), not readBytes(): Stream::readBytes goes through the
            // single-byte virtual read, which is 3,500 calls into mbedtls for
            // one of these bodies.
            const int got = s->read((uint8_t*)buf + n, want);
            if (got < 0) break;   // the session broke mid-body
            if (got > 0) {
                n += (size_t)got;
                last_ms = millis();
                continue;
            }
        }
        if (!g_http.connected()) break;
        if (cb::clock_elapsed(last_ms, millis(), READ_TIMEOUT_MS)) {
            status = FETCH_TIMEOUT;
            return false;
        }
        delay(1);   // yield rather than spin: the TLS record has not arrived yet
    }
    if (n != (size_t)len) { status = FETCH_SHORT; return false; }
    out_len = n;
    return true;
}

}  // namespace

bool wifi_begin(uint32_t timeout_ms) {
    g_timeout_ms = timeout_ms;
    if (!associate()) {
        enter(State::NoLink);
        return false;
    }
    return true;
}

void wifi_poll() {
    switch (g_state) {
        case State::Off:
            break;
        case State::Linking:
            if (WiFi.status() == WL_CONNECTED) {
                enter(State::Linked);
                report_association();
            } else if (elapsed(g_since_ms, g_timeout_ms)) {
                // false, not true. disconnect(true) routes to enableSTA(false),
                // which stops the driver outright, so the 30s retry pays a
                // synchronous esp_wifi_stop/start inside loop() and frees and
                // reallocates the driver's heap buffers each cycle -- the
                // contiguity the framebuffer-first ordering exists to protect.
                WiFi.disconnect(false);
                enter(State::NoLink);
                Serial.println("claudeboy: wifi association timed out");
            }
            break;
        case State::Linked:
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("claudeboy: wifi dropped, reassociating");
                if (!associate()) enter(State::NoLink);
            }
            break;
        case State::NoLink:
            if (!elapsed(g_since_ms, RETRY_MS)) break;
            if (!associate()) enter(State::NoLink);   // failed again: restart the retry timer
            break;
    }
}

bool wifi_connected() { return g_state == State::Linked; }

bool fetch_snapshot(char* buf, size_t cap, size_t& out_len, int& status) {
    out_len = 0;
    status = FETCH_NO_LINK;
    if (!buf || cap == 0 || g_state != State::Linked) return false;

    // setInsecure(), not a pinned root, and deliberately for now: the chain is
    // GTS Root R4 -> WE1 -> the leaf, and pinning a root that Cloudflare can
    // rotate turns a silent reissue into a board that shows SIGNAL LOST until
    // someone reflashes it. The read token this carries is semi-public by
    // construction -- it ships inside firmware -- and the payload is usage
    // percentages, so the exposure of an unauthenticated peer is small. Pin it
    // once the board is back on the desk to confirm the handshake still
    // completes, which is the part that cannot be checked from here.
    g_tls.setInsecure();
    g_tls.setHandshakeTimeout(HANDSHAKE_TIMEOUT_S);
    g_tls.setTimeout(READ_TIMEOUT_MS / 1000);   // seconds on ESP32, unlike HTTPClient's

    g_http.setReuse(false);   // the session is too expensive to hold between polls
    g_http.setConnectTimeout((int32_t)CONNECT_TIMEOUT_MS);
    g_http.setTimeout((uint16_t)READ_TIMEOUT_MS);
    if (!g_http.begin(g_tls, CB_API_HOST, 443, CB_API_PATH, true)) {
        status = FETCH_NO_BEGIN;
        release();
        return false;
    }
    g_http.addHeader("Authorization", "Bearer " CB_READ_TOKEN);

    status = g_http.GET();
    const bool ok = (status == HTTP_CODE_OK) && read_body(buf, cap, out_len, status);
    if (!ok) out_len = 0;
    release();
    return ok;
}

const char* wifi_status_text() {
    switch (g_state) {
        case State::Linking: return "WIFI LINKING";
        case State::Linked:  return "WIFI LINKED";
        case State::NoLink:  return "WIFI NO LINK";
        case State::Off:     break;
    }
    return "WIFI OFF";
}

}  // namespace cbnet
