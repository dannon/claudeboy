#include <Arduino.h>
#include <TFT_eSPI.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "core/burn.h"
#include "core/canvas.h"
#include "core/clock.h"
#include "core/crt.h"
#include "core/frame.h"
#include "core/palette.h"
#include "core/screen.h"
#include "core/store.h"
#include "core/types.h"
#include "device/net.h"

static TFT_eSPI tft;

static const size_t BUF_BYTES = (size_t)cb::SCREEN_W * cb::SCREEN_H;

static uint8_t* g_buf = nullptr;   // the one and only framebuffer, the accumulator
static uint8_t g_ring[9 * cb::SCREEN_W];
static uint8_t g_out_row[cb::SCREEN_W];   // one post-processed row, on its way to the panel
static uint16_t g_line[cb::SCREEN_W];
static uint16_t g_palette[256];   // rgb565 per intensity, built once in setup()

static uint32_t g_frame = 0;

// One association attempt. Longer than a healthy router needs, short enough
// that a wrong password shows up as a state on the panel rather than a hang.
static const uint32_t WIFI_TIMEOUT_MS = 15000;

// The two parse arenas and the snapshot on screen. Static, because every
// string cb::Provider hands the renderer is a bare pointer into one of these
// and the renderer dereferences it while drawing.
static cb::ArenaBytes g_arena_a;
static cb::ArenaBytes g_arena_b;
static cb::SnapshotStore g_store;

// The whole reply body, before it is parsed. The live client=cyd payload is
// 3,519 bytes; this holds a couple more providers than exist today and
// refuses anything larger rather than parsing half a snapshot.
static char g_body[6144];

// No RTC, no NTP: the clock is seeded from each reply's serverTime.
static cb::ServerClock g_clock;

// Today's running token total, sampled once per successful poll. This is the
// only history the board keeps, and the only thing on screen that is measured
// here rather than reported by the server -- so it starts empty at boot and
// the needle reads "--" until two polls a few minutes apart have landed.
static cb::BurnHistory g_burn;

// Matching the agent, which polls OpenUsage on the same period. Faster would
// only buy latency the source does not have.
static const uint32_t POLL_MS = 60000;
// A failed fetch is usually a transient one, and until the first success the
// board has nothing on screen at all, so a failure is worth retrying well
// inside the minute. Not much sooner, though: a fetch against a network that
// is associated but going nowhere blocks the render loop until its timeouts
// expire, and retrying on top of that would leave the panel frozen more than
// it draws.
static const uint32_t RETRY_MS = 15000;

static uint32_t g_last_poll_ms = 0;
static uint32_t g_poll_wait_ms = 0;   // 0, so the first poll goes out as soon as WiFi links

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

// One HTTPS GET, parsed and adopted, when one is due. Called between frames
// and never inside one: the TLS handshake blocks for a second or two and the
// panel simply holds its last finished frame across it.
//
// roll_window_forward() is deliberately not used here. It exists so a shelved
// fixture still shows moving windows; a live resetsAt is already in the
// future, and walking it forward would invent a window that has not happened.
static void poll_snapshot() {
    if (!cbnet::wifi_connected()) return;
    if (!cb::clock_elapsed(g_last_poll_ms, millis(), g_poll_wait_ms)) return;

    size_t len = 0;
    int status = 0;
    const uint32_t t0 = millis();
    const bool got = cbnet::fetch_snapshot(g_body, sizeof g_body, len, status);
    const uint32_t took_ms = millis() - t0;

    g_last_poll_ms = millis();
    // A body that arrived but would not parse is a server-side problem, and
    // hammering it every ten seconds would not fix it. Only a failed fetch
    // retries early.
    g_poll_wait_ms = got ? POLL_MS : RETRY_MS;

    int parsed = -1;
    if (got) {
        const cb::ParseResult r = cb::store_accept(g_store, g_body, len);
        parsed = (int)r;
        const int64_t served = cb::store_current(g_store).server_time_ms;
        // Seeded from the reply that carried it, against the counter as it
        // reads now: serverTime was stamped when the Worker served, a few
        // hundred milliseconds ago, so this runs a touch behind rather than
        // ahead -- the safe direction for an age. A reply with no serverTime
        // at all leaves the clock alone rather than throwing it back to 1970.
        if (r == cb::ParseResult::Ok && served > 0) cb::clock_seed(g_clock, served, millis());

        // Sampled against the server's clock, not millis(): a board that
        // reboots would otherwise look like it consumed a day's tokens in a
        // second. store_accept() only swaps the snapshot on Ok, so anything
        // else leaves the history alone rather than sampling stale numbers.
        if (r == cb::ParseResult::Ok) {
            const cb::UsageSnapshot& s = cb::store_current(g_store);
            const int64_t at = cb::clock_now(g_clock, millis());
            if (at > 0 && s.providers && s.provider_count > 0)
                cb::burn_observe(g_burn, at, cb::chart_total(s.providers[0], 1));
        }
    }

    // parse is a cb::ParseResult, or -1 when there was no body to parse. The
    // heap figures are taken after the session came down, so they are what the
    // next handshake will have to fit into.
    Serial.printf("claudeboy: poll status=%d bytes=%u parse=%d took=%ums "
                  "free=%u largest=%u\n",
                  status, (unsigned)len, parsed, (unsigned)took_ms,
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
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
    // heap_caps_malloc, not new[]: this build has exceptions off, so a failed
    // operator new aborts with a heap backtrace instead of returning null, and
    // the FATAL line below would never print.
    g_buf = (uint8_t*)heap_caps_malloc(BUF_BYTES, MALLOC_CAP_8BIT);
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
    cb::store_init(g_store, g_arena_a, g_arena_b);
    cb::burn_init(g_burn);

    // Last, and deliberately: the framebuffer already holds its contiguous run
    // and the panel is lit, so the screen has something to show while the
    // radio associates. wifi_begin() does not block -- loop() drives it.
    cbnet::wifi_begin(WIFI_TIMEOUT_MS);
    print_heap("after wifi start");
}

void loop() {
    cbnet::wifi_poll();
    poll_snapshot();

    const cb::EffectParams fx = cb::EffectParams::defaults();
    cb::Canvas c(g_buf, cb::SCREEN_W, cb::SCREEN_H);

    const cb::UsageSnapshot& snap = cb::store_current(g_store);
    const int64_t now = cb::clock_now(g_clock, millis());
    // Until the first reply lands the board does not know what time it is, and
    // a clock counting up from the epoch would be a worse answer than none.
    char clk[8];
    const char* clock_text = nullptr;
    if (now > 0) {
        // The agent reports its own UTC offset with every push, so this is real
        // local time without an NTP client or a timezone database on the board.
        const int64_t local = now + (int64_t)snap.utc_offset_sec * 1000;
        cb::format_clock(local, clk, sizeof clk);
        clock_text = clk;
    }

    cb::FrameTiming timing{now_us, 0, 0};
    const uint32_t t_frame_start = micros();
    tft.startWrite();
    tft.setAddrWindow(0, 0, cb::SCREEN_W, cb::SCREEN_H);
    cb::render_frame(c, snap, 0, now, clock_text, fx,
                     g_frame, g_ring, sizeof g_ring,
                     g_out_row, push_row, nullptr, &timing,
                     cb::burn_rate_per_hour(g_burn, now, cb::BURN_WINDOW_MS));
    tft.endWrite();
    const uint32_t total_us = micros() - t_frame_start;

    // Post-processing and the SPI push are one stage now: each row goes
    // straight to the panel as it is finished, so they cannot be timed apart.
    if ((g_frame % 30) == 0) {   // roughly every 30 frames, so serial stays readable
        // The heap figures ride along because the WiFi stack allocates lazily
        // as traffic starts: the one reading taken at the association instant
        // is the most optimistic the board will ever produce, and the
        // largest-block number is what decides whether TLS fits.
        Serial.printf("claudeboy: render=%uus post+push=%uus total=%uus %s "
                      "free=%u largest=%u\n",
                      (unsigned)timing.render_us, (unsigned)timing.post_us,
                      (unsigned)total_us, cbnet::wifi_status_text(),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    }

    g_frame++;

    delay(40);
}
