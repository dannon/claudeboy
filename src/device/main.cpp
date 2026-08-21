#include <Arduino.h>
#include <TFT_eSPI.h>
#include <string.h>
#include "core/canvas.h"
#include "core/crt.h"
#include "core/fixture.h"
#include "core/frame.h"
#include "core/palette.h"
#include "core/screen.h"
#include "core/types.h"

static TFT_eSPI tft;

static uint8_t g_buf[cb::SCREEN_W * cb::SCREEN_H];   // the one accumulator
// The post-processed copy that actually gets pushed. cb::render_frame() owns
// the accumulator/copy split and the reason for it -- see core/frame.h.
//
// This buffer is heap-allocated in setup(), NOT a second static array like
// g_buf: this board's static .bss/.data DRAM segment (dram0_0_seg) is only
// ~124,580 bytes and the Arduino/TFT_eSPI framework already statically
// consumes ~102,612 of that (~83%), leaving ~21,968 bytes of static
// headroom -- nowhere near the 76,800 bytes SCREEN_W*SCREEN_H needs. The
// linker refuses to build a second static buffer that size ("region
// `dram0_0_seg' overflowed by 54840 bytes"). Free HEAP (~270KB) is a
// separate, much larger pool that a compile-time static array cannot draw
// from, which is why a `static uint8_t g_shown[...]` does not fit even
// though free heap looks comfortable. `new` here instead.
static uint8_t* g_shown = nullptr;
static uint8_t g_ring[9 * cb::SCREEN_W];
static uint16_t g_line[cb::SCREEN_W];
static uint16_t g_palette[256];   // rgb565 per intensity, built once in setup()

static uint32_t g_frame = 0;

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
    Serial.printf("claudeboy: free heap %u bytes\n", (unsigned)ESP.getFreeHeap());

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);

    tft.init();
    tft.setSwapBytes(true);      // ST7789 wants high byte first; ESP32 is little-endian
    tft.setRotation(3);          // landscape, origin top-left with USB-C at top
    tft.fillScreen(TFT_BLACK);

    memset(g_buf, 0, sizeof g_buf);
    cb::palette_build_rgb565_table(g_palette);

    g_shown = new uint8_t[cb::SCREEN_W * cb::SCREEN_H];
    if (!g_shown) {
        Serial.println("claudeboy: FATAL: g_shown heap allocation failed");
        while (true) delay(1000);
    }
    memset(g_shown, 0, cb::SCREEN_W * cb::SCREEN_H);

    Serial.printf("claudeboy: after buffers, free heap %u bytes\n",
                  (unsigned)ESP.getFreeHeap());
}

void loop() {
    const cb::EffectParams fx = cb::EffectParams::defaults();
    cb::Canvas c(g_buf, cb::SCREEN_W, cb::SCREEN_H);

    // Phase 1 has no clock source, so the fixture's own reference time is used
    // and advanced by wall milliseconds since boot. The gauges therefore age.
    const int64_t now = cb::FIXTURE_REFERENCE_MS + (int64_t)millis();
    char clk[8];
    cb::format_clock(now, clk, sizeof clk);   // UTC; phase 2 brings a real zone

    cb::Canvas shown(g_shown, cb::SCREEN_W, cb::SCREEN_H);
    cb::FrameTiming timing{now_us, 0, 0};
    const uint32_t t_frame_start = micros();
    cb::render_frame(c, shown, live_snapshot(now), 0, now, clk, fx,
                     g_frame, g_ring, sizeof g_ring, &timing);
    const uint32_t t_post_end = micros();

    tft.startWrite();
    tft.setAddrWindow(0, 0, cb::SCREEN_W, cb::SCREEN_H);
    for (int y = 0; y < cb::SCREEN_H; y++) {
        const uint8_t* row = g_shown + (size_t)y * cb::SCREEN_W;
        for (int x = 0; x < cb::SCREEN_W; x++) g_line[x] = g_palette[row[x]];
        tft.pushPixels(g_line, cb::SCREEN_W);
    }
    tft.endWrite();
    const uint32_t t_push_end = micros();

    const uint32_t render_us = timing.render_us;
    const uint32_t post_us = timing.post_us;
    const uint32_t push_us = t_push_end - t_post_end;
    const uint32_t total_us = t_push_end - t_frame_start;

    // Print roughly every 30 frames so the serial output stays readable.
    if ((g_frame % 30) == 0) {
        Serial.printf("claudeboy: render=%uus post=%uus push=%uus total=%uus\n",
                      (unsigned)render_us, (unsigned)post_us, (unsigned)push_us,
                      (unsigned)total_us);
    }

    g_frame++;

    delay(40);
}
