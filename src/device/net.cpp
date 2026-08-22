#include "device/net.h"

#include <Arduino.h>
#include <WiFi.h>

#include "esp_heap_caps.h"

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

// Unsigned subtraction, so this stays correct across the 49-day millis() wrap.
bool elapsed(uint32_t since, uint32_t span) { return (uint32_t)(millis() - since) >= span; }

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
                WiFi.disconnect(true);
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
