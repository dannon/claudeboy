#include <Arduino.h>
#include <TFT_eSPI.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "core/canvas.h"
#include "core/crt.h"
#include "core/fixture.h"
#include "core/frame.h"
#include "core/palette.h"
#include "core/screen.h"
#include "core/types.h"

static TFT_eSPI tft;

static const size_t BUF_BYTES = (size_t)cb::SCREEN_W * cb::SCREEN_H;

static uint8_t* g_buf = nullptr;   // the one and only framebuffer, the accumulator
static uint8_t g_ring[9 * cb::SCREEN_W];
static uint8_t g_out_row[cb::SCREEN_W];   // one post-processed row, on its way to the panel
static uint16_t g_line[cb::SCREEN_W];
static uint16_t g_palette[256];   // rgb565 per intensity, built once in setup()

static uint32_t g_frame = 0;

// cb::render_frame() hands each finished row here instead of filling a second
// canvas -- see core/frame.h for why post-processing stays off the
// accumulator. The address window is set once per frame, so rows must arrive
// in order and each pushPixels() simply continues where the last left off.
static void push_row(void*, int, const uint8_t* row, int w) {
    for (int x = 0; x < w; x++) g_line[x] = g_palette[row[x]];
    tft.pushPixels(g_line, w);
}

static void print_heap(const char* when) {
    Serial.printf("claudeboy: %s, MALLOC_CAP_8BIT free %u bytes, largest block %u bytes\n",
                  when, (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}

// core/ has no clock of its own, so hand render_frame() ours for the
// per-stage timing. micros() returns unsigned long, a distinct type from
// uint32_t here even at the same width, hence the wrapper.
static uint32_t now_us() { return static_cast<uint32_t>(micros()); }

// The fixture is a frozen capture and the device just keeps counting past it,
// so every progress window would expire a few minutes in and sit at
// "SURPLUS 0m" forever. Roll each window forward into the present before
// drawing, which is what a live feed would have done. Only the device does
// this: the host stays pinned to the reference instant so the golden holds.
static cb::ProgressLine g_lines[8];
static cb::Provider g_prov;
static cb::UsageSnapshot g_live;

static const cb::UsageSnapshot& live_snapshot(int64_t now) {
    const cb::UsageSnapshot& base = cb::fixture_snapshot();
    g_prov = base.providers[0];
    int n = g_prov.progress_count;
    if (n > (int)(sizeof g_lines / sizeof g_lines[0])) n = sizeof g_lines / sizeof g_lines[0];
    for (int i = 0; i < n; i++)
        g_lines[i] = cb::roll_window_forward(base.providers[0].progress[i], now);
    g_prov.progress = g_lines;
    g_prov.progress_count = n;
    g_live = {&g_prov, 1, now};
    return g_live;
}

void setup() {
    Serial.begin(115200);
    delay(100);
    // ESP.getFreeHeap() counts ~73KB of 32-bit-only IRAM that cannot back a
    // uint8_t[], so it reports room that does not exist for a framebuffer.
    print_heap("boot");

    // Before anything touches the network: the accumulator needs one
    // contiguous 76,800-byte run, and the WiFi/TLS stacks fragment the heap
    // once they start, so a later allocation can fail while the free total
    // still looks comfortable.
    g_buf = new uint8_t[BUF_BYTES];
    if (!g_buf) {
        Serial.println("claudeboy: FATAL: framebuffer allocation failed");
        while (true) delay(1000);
    }
    memset(g_buf, 0, BUF_BYTES);
    print_heap("after framebuffer");

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tft.init();
    tft.setSwapBytes(true);      // ST7789 wants high byte first; ESP32 is little-endian
    tft.setRotation(3);          // landscape, origin top-left with USB-C at top
    tft.fillScreen(TFT_BLACK);

    cb::palette_build_rgb565_table(g_palette);
}

void loop() {
    const cb::EffectParams fx = cb::EffectParams::defaults();
    cb::Canvas c(g_buf, cb::SCREEN_W, cb::SCREEN_H);

    // Phase 1 has no clock source, so the fixture's own reference time is used
    // and advanced by wall milliseconds since boot. The gauges therefore age.
    const int64_t now = cb::FIXTURE_REFERENCE_MS + (int64_t)millis();
    char clk[8];
    cb::format_clock(now, clk, sizeof clk);   // UTC; phase 2 brings a real zone

    cb::FrameTiming timing{now_us, 0, 0};
    const uint32_t t_frame_start = micros();
    tft.startWrite();
    tft.setAddrWindow(0, 0, cb::SCREEN_W, cb::SCREEN_H);
    cb::render_frame(c, live_snapshot(now), 0, now, clk, fx,
                     g_frame, g_ring, sizeof g_ring,
                     g_out_row, push_row, nullptr, &timing);
    tft.endWrite();
    const uint32_t total_us = micros() - t_frame_start;

    // Post-processing and the SPI push are one stage now: each row goes
    // straight to the panel as it is finished, so they cannot be timed apart.
    if ((g_frame % 30) == 0) {   // roughly every 30 frames, so serial stays readable
        Serial.printf("claudeboy: render=%uus post+push=%uus total=%uus\n",
                      (unsigned)timing.render_us, (unsigned)timing.post_us,
                      (unsigned)total_us);
    }

    g_frame++;

    delay(40);
}
