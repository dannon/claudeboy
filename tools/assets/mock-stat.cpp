#include <stdio.h>
#include <string.h>
#include "core/canvas.h"
#include "core/crt.h"
#include "core/screen.h"
#include "core/vaultboy.h"
#include "host/png.h"
#include "../../tmp/pipfont.h"

using namespace cb;
static uint8_t accum[SCREEN_W*SCREEN_H], shown[SCREEN_W*SCREEN_H], ring[9*SCREEN_W];
static uint8_t boybuf[SCREEN_W*SCREEN_H];

static const int PW = 7, PH = 11, PADV = 8;   // 7x11 cell, 8px advance
static void pchar(Canvas& c, int x, int y, char ch, uint8_t v, int s=1) {
    if (ch < FONT_FIRST || ch > FONT_LAST) return;
    const uint8_t* g = &FONT_DATA[(ch - FONT_FIRST) * PH];
    for (int r = 0; r < PH; r++)
        for (int col = 0; col < PW; col++)
            if (g[r] & (1u << col)) {
                if (s == 1) c.plot(x+col, y+r, v);
                else c.fill(x+col*s, y+r*s, s, s, v);
            }
}
static void ptext(Canvas& c, int x, int y, const char* t, uint8_t v, int s=1) {
    for (; *t; ++t) { pchar(c, x, y, *t, v, s); x += PADV*s; }
}
static int pw(const char* t, int s=1) { int n=0; for(;*t;++t)n++; return n*PADV*s - s; }

// Vault Boy drawn at 84x70 then nearest-neighbour scaled -- the bead art has
// no detail finer than two pixels, so upscaling costs nothing.
static void boy_scaled(Canvas& dst, int x, int y, BoyMood m, int num, int den) {
    memset(boybuf, 0, sizeof boybuf);
    Canvas tmp(boybuf, SCREEN_W, SCREEN_H);
    draw_vault_boy(tmp, 0, 0, m);
    for (int sy = 0; sy < VB_H*num/den; sy++)
        for (int sx = 0; sx < VB_W*num/den; sx++) {
            const uint8_t v = tmp.at(sx*den/num, sy*den/num);
            if (v) dst.plot(x+sx, y+sy, v);
        }
}

static void bar(Canvas& c, int x, int y, int w, int h, float frac, uint8_t v) {
    c.rect(x, y, w, h, I_RULE);
    int fw = (int)(frac * (w-4));
    if (fw > 0) c.fill(x+2, y+2, fw, h-4, v);
}

int main() {
    memset(accum, 0, sizeof accum);
    Canvas c(accum, SCREEN_W, SCREEN_H), out(shown, SCREEN_W, SCREEN_H);

    // --- tab row, Pip-Boy style: bracketed active tab, rule under -----------
    int tx = 8;
    const char* tabs[] = {"CLAUDE", "CODEX", "ANTIGRAVITY"};
    for (int i = 0; i < 3; i++) {
        if (i == 0) { ptext(c, tx, 2, "[", I_BRIGHT); tx += PADV; }
        ptext(c, tx, 2, tabs[i], i == 0 ? I_BRIGHT : I_DIM);
        tx += pw(tabs[i]) + 2;
        if (i == 0) { ptext(c, tx, 2, "]", I_BRIGHT); tx += PADV; }
        tx += 10;
    }
    ptext(c, SCREEN_W - 8 - pw("20:44"), 2, "20:44", I_NORMAL);
    c.fill(8, 16, SCREEN_W-16, 2, I_RULE);

    // --- Vault Boy as the hero, left ----------------------------------------
    boy_scaled(c, 6, 26, BoyMood::Fried, 3, 2);     // 126 x 105

    // --- AP / HP, right -----------------------------------------------------
    const int rx = 146;
    ptext(c, rx, 30, "AP", I_BRIGHT, 2);
    ptext(c, rx + 42, 34, "SESSION", I_DIM);
    ptext(c, SCREEN_W - 8 - pw("76%", 2), 30, "76%", I_NORMAL, 2);
    bar(c, rx, 56, SCREEN_W-8-rx, 12, 0.76f, I_NORMAL);
    ptext(c, rx, 72, "SURPLUS", I_DIM);
    ptext(c, SCREEN_W - 8 - pw("35m"), 72, "35m", I_DIM);

    ptext(c, rx, 100, "HP", I_BRIGHT, 2);
    ptext(c, rx + 42, 104, "WEEKLY", I_DIM);
    ptext(c, SCREEN_W - 8 - pw("20%", 2), 100, "20%", I_NORMAL, 2);
    bar(c, rx, 126, SCREEN_W-8-rx, 12, 0.20f, I_NORMAL);
    draw_radiation(c, rx + 6, 146, 6, I_BRIGHT);
    ptext(c, rx + 16, 142, "BURNOUT", I_BRIGHT);
    ptext(c, SCREEN_W - 8 - pw("1d16h"), 142, "1d16h", I_DIM);

    // --- bottom strip -------------------------------------------------------
    c.fill(8, 206, SCREEN_W-16, 2, I_RULE);
    ptext(c, 8, 214, "DOSE EXCEEDS SAFE LIMITS", I_BRIGHT);
    ptext(c, SCREEN_W - 8 - pw("29.8M"), 214, "29.8M", I_DIM);

    post_process(c, EffectParams::defaults(), 0, ring, sizeof ring);
    memcpy(shown, accum, sizeof shown);
    cbhost::write_png_from_canvas("out/mock-stat.png", out);
    printf("out/mock-stat.png\n");
    return 0;
}
