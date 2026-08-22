#include "device/touch.h"
#include <Arduino.h>
#include <SPI.h>
#include "core/types.h"

namespace cbtouch {
namespace {

constexpr int PIN_CLK = 25, PIN_CS = 33, PIN_MOSI = 32, PIN_MISO = 39, PIN_IRQ = 36;

// The panel is on VSPI -- TFT_eSPI uses the global SPI object unless
// USE_HSPI_PORT is defined, and it is not. So the touch controller gets the
// other peripheral outright and neither driver has to know about the other.
SPIClass g_spi(HSPI);

// The chip is specified to 2MHz in differential mode and there is nothing to
// gain by pushing it: this reads six 24-bit frames about twenty times a
// second.
constexpr uint32_t SPI_HZ = 2000000;

// Raw counts at the edges of the glass. Measured on this board by tapping the
// corners and reading the RAW line the poll prints; the controller's usable
// span is well inside its 12-bit range and differs from panel to panel.
constexpr int RAW_X_MIN = 300, RAW_X_MAX = 3800;
constexpr int RAW_Y_MIN = 300, RAW_Y_MAX = 3800;

// The controller's axes against the display at rotation 3. Its X runs down
// the display's height and its Y across the width, both backwards.
constexpr bool SWAP_XY = true, FLIP_X = true, FLIP_Y = true;

// Three conversions per axis, averaged. One is noisy enough to walk a tap
// into the next zone; the median of three would need sorting for no gain
// over a mean at this sample count.
constexpr int SAMPLES = 3;

// A press held down must not read as a stream of taps, and the panel bounces
// for a few milliseconds on contact.
constexpr uint32_t DEBOUNCE_MS = 250;

int g_raw_x = 0, g_raw_y = 0;
bool g_was_down = false;
uint32_t g_last_ms = 0;

uint16_t read12(uint8_t cmd) {
    g_spi.transfer(cmd);
    const uint8_t hi = g_spi.transfer(0x00);
    const uint8_t lo = g_spi.transfer(0x00);
    return static_cast<uint16_t>(((static_cast<uint16_t>(hi) << 8) | lo) >> 3);
}

// PENIRQ is active low and only asserts while the chip is between
// conversions with its power-down bits clear, which the 0xD0/0x90 commands
// below leave it in. Reading the pin is cheaper and steadier than reading
// the pressure channels.
bool read_raw(int& rx, int& ry) {
    if (digitalRead(PIN_IRQ) == HIGH) return false;

    g_spi.beginTransaction(SPISettings(SPI_HZ, MSBFIRST, SPI_MODE0));
    digitalWrite(PIN_CS, LOW);
    read12(0xD0);   // thrown away: the first conversion after a wake settles late
    int sx = 0, sy = 0;
    for (int i = 0; i < SAMPLES; i++) {
        sx += read12(0xD0);
        sy += read12(0x90);
    }
    digitalWrite(PIN_CS, HIGH);
    g_spi.endTransaction();

    rx = sx / SAMPLES;
    ry = sy / SAMPLES;
    // A finger lifting mid-burst reads as a rail. Neither end is a position.
    return rx > 100 && rx < 4000 && ry > 100 && ry < 4000;
}

int scale(int v, int lo, int hi, int span) {
    if (hi <= lo) return 0;
    long s = static_cast<long>(v - lo) * span / (hi - lo);
    if (s < 0) s = 0;
    if (s > span - 1) s = span - 1;
    return static_cast<int>(s);
}

void to_panel(int rx, int ry, int& x, int& y) {
    int a = scale(rx, RAW_X_MIN, RAW_X_MAX, SWAP_XY ? cb::SCREEN_H : cb::SCREEN_W);
    int b = scale(ry, RAW_Y_MIN, RAW_Y_MAX, SWAP_XY ? cb::SCREEN_W : cb::SCREEN_H);
    if (SWAP_XY) { const int t = a; a = b; b = t; }
    if (FLIP_X) a = cb::SCREEN_W - 1 - a;
    if (FLIP_Y) b = cb::SCREEN_H - 1 - b;
    x = a;
    y = b;
}

}  // namespace

void begin() {
    pinMode(PIN_IRQ, INPUT);
    pinMode(PIN_CS, OUTPUT);
    digitalWrite(PIN_CS, HIGH);
    g_spi.begin(PIN_CLK, PIN_MISO, PIN_MOSI, PIN_CS);
}

bool tapped(int& x, int& y) {
    int rx = 0, ry = 0;
    const bool down = read_raw(rx, ry);
    bool fired = false;
    // Leading edge only: a finger resting on the glass is one tap, not forty.
    if (down && !g_was_down && millis() - g_last_ms > DEBOUNCE_MS) {
        g_raw_x = rx;
        g_raw_y = ry;
        to_panel(rx, ry, x, y);
        g_last_ms = millis();
        fired = true;
    }
    g_was_down = down;
    return fired;
}

void last_raw(int& x, int& y) { x = g_raw_x; y = g_raw_y; }

bool down() { return digitalRead(PIN_IRQ) == LOW; }

}  // namespace cbtouch
