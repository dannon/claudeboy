#include <Arduino.h>
#include <TFT_eSPI.h>
#include <string.h>
#include "core/canvas.h"
#include "core/font.h"
#include "core/palette.h"
#include "core/types.h"

static TFT_eSPI tft;
static uint8_t g_buf[cb::SCREEN_W * cb::SCREEN_H];
static uint16_t g_line[cb::SCREEN_W];

static void push_frame(const cb::Canvas& c) {
    tft.startWrite();
    tft.setAddrWindow(0, 0, cb::SCREEN_W, cb::SCREEN_H);
    for (int y = 0; y < cb::SCREEN_H; y++) {
        const uint8_t* row = c.data() + (size_t)y * cb::SCREEN_W;
        for (int x = 0; x < cb::SCREEN_W; x++) g_line[x] = cb::palette_rgb565(row[x]);
        tft.pushPixels(g_line, cb::SCREEN_W);
    }
    tft.endWrite();
}

void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.printf("claudeboy: free heap at boot      %u\n", (unsigned)ESP.getFreeHeap());

    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    tft.init();
    tft.setSwapBytes(true);          // ILI9341 wants high byte first; ESP32 is little-endian
    tft.setRotation(3);              // landscape 320x240, origin top-left with USB-C at top
    tft.fillScreen(TFT_BLACK);

    memset(g_buf, 0, sizeof g_buf);
    cb::Canvas c(g_buf, cb::SCREEN_W, cb::SCREEN_H);

    c.rect(4, 4, cb::SCREEN_W - 8, cb::SCREEN_H - 8, 120);
    cb::draw_text(c, 12, 20, "CLAUDEBOY 3000", 255, 2);
    cb::draw_text(c, 12, 48, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 200, 1);
    cb::draw_text(c, 12, 60, "0123456789 %.:-/", 200, 1);
    cb::draw_text(c, 12, 76, "SURPLUS", 110, 1);
    cb::draw_text(c, 90, 76, "ON PACE", 200, 1);
    cb::draw_text(c, 168, 76, "BURNOUT", 255, 1);

    // Legibility check: the smallest text the real screen must carry.
    cb::draw_text(c, 12, 96, "WEEKLY 42% 2d23h", 200, 1);

    // Intensity ramp, so the phosphor curve can be judged on real glass.
    for (int x = 0; x < cb::SCREEN_W - 24; x++)
        c.fill(12 + x, 200, 1, 24, (uint8_t)(x * 255 / (cb::SCREEN_W - 25)));

    push_frame(c);

    Serial.printf("claudeboy: free heap after buffers %u\n", (unsigned)ESP.getFreeHeap());
    Serial.println("claudeboy: static smoke frame pushed");
}

void loop() { delay(1000); }
