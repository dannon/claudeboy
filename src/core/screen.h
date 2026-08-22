#pragma once
#include <stddef.h>
#include "core/canvas.h"
#include "core/font.h"
#include "core/pace.h"
#include "core/types.h"
#include "core/vaultboy.h"

namespace cb {

// Two pages, swapped by a tap. One screen carrying everything at once was
// legible but read as a dashboard; a Pip-Boy is sparse, and the numbers worth
// reading from the sofa are not the numbers worth reading up close.
enum class Page : uint8_t { Stat, Data };
constexpr int PAGE_COUNT = 2;

// --- chrome, on both pages --------------------------------------------------
constexpr int MARGIN = 6;
constexpr int TAB_Y  = 3;
constexpr int TAB_H  = 18;    // the rule row; tab text sits at TAB_Y
constexpr int FOOT_Y = 212;   // footer text row; its rule sits four above

// --- STAT ------------------------------------------------------------------
// Vault Boy is the hero here, not a thumbnail in a corner. Drawn 1:1 now that
// the art is traced at the size it is shown -- upscaling was a way of buying
// detail the old hand-drawn bead grid did not have.
constexpr int BOY_NUM = 1, BOY_DEN = 1;
constexpr int BOY_W = VB_W * BOY_NUM / BOY_DEN;   // 126
constexpr int BOY_H = VB_H * BOY_NUM / BOY_DEN;   // 105
constexpr int BOY_X = MARGIN;
constexpr int BOY_Y = 24;

// The session block and the weekly ration, stacked down the right. These are
// the two figures the whole device exists to show, so they get the big type.
constexpr int HERO_X     = 126;
constexpr int HERO_W     = SCREEN_W - MARGIN - HERO_X;
constexpr int HERO_Y     = 26;
constexpr int HERO_PITCH = 66;   // top of the second gauge, from the first
constexpr int HERO_H     = 55;   // badge row through the verdict row

// Inside one gauge, measured from its own top.
constexpr int HERO_BAR_DY  = 26;
constexpr int HERO_BAR_H   = 13;
constexpr int HERO_TICK_DY = 21;   // pace tick, just above the bar
constexpr int HERO_FOOT_DY = 43;   // verdict on the left, countdown on the right

// Windows past the first two get one compact row each, under Vault Boy and
// across the full width. Antigravity sends four, so two rows is not a
// hypothetical -- and anything past those is still counted, not dropped.
constexpr int STRIP_Y    = 152;
constexpr int STRIP_H    = 16;
constexpr int STRIP_ROWS = 2;

// --- DATA ------------------------------------------------------------------
constexpr int CHART_Y = 26;
constexpr int CHART_H = 84;

// The provider sends 31 days; we draw the tail. A month of bars at this width
// is a grey smear that answers no question you actually have.
constexpr int CHART_DAYS = 7;

// The lower band: the exposure log on the left, the burn meter on the right.
constexpr int PANEL_Y = 118;
constexpr int PANEL_H = 84;

constexpr int LOG_X = MARGIN;
constexpr int LOG_Y = PANEL_Y + 6;
constexpr int LOG_W = 168;
constexpr int LOG_ROW_H = 15;
// Where the two right-hand columns end. Both are right-aligned, so these are
// the pixel after the last one they may light. The tok column is placed from
// the right edge rather than the left so the two stay clear of each other
// when the log's width changes.
constexpr int LOG_CAPS_COL_W = 52;
constexpr int LOG_TOK_R  = LOG_X + LOG_W - LOG_CAPS_COL_W;
constexpr int LOG_CAPS_R = LOG_X + LOG_W;

constexpr int METER_X = 190;
constexpr int METER_W = SCREEN_W - MARGIN - METER_X;
constexpr int METER_Y = PANEL_Y + 2;

// Intensity levels. Pace state is carried by brightness, not colour.
constexpr uint8_t I_DIM    = 110;
constexpr uint8_t I_NORMAL = 200;
constexpr uint8_t I_BRIGHT = 255;
constexpr uint8_t I_RULE   = 150;
// Large filled areas only -- see vaultboy.cpp for why this sits so low.
constexpr uint8_t I_WASH   = 45;

// How much of its brightness the data band keeps once it is no longer fresh,
// out of 255. Old numbers must stay readable -- the point is to keep showing
// them, dimmed, rather than to blank the screen on a failed poll.
constexpr uint8_t I_STALE_SCALE = 128;

// How old the numbers on screen are, from the newest fetchedAt in the
// snapshot. Not from when the board last got a reply: a fast response
// carrying hour-old numbers is an hour old.
enum class Freshness : uint8_t { Fresh, Stale, SignalLost, NoSignal };

// Both boundaries belong to the gentler state: exactly 10 minutes is Stale,
// exactly 2 hours is still Stale.
constexpr int64_t STALE_AFTER_MS = 10LL * 60 * 1000;
constexpr int64_t LOST_AFTER_MS  = 2LL * 60 * 60 * 1000;

// The newest fetchedAt across the snapshot, or 0 if nothing in it was ever
// fetched. Providers come and go server-side, so one that never reported must
// not drag down the ones that did.
int64_t newest_fetched_at_ms(const UsageSnapshot& snap);

// Age of that newest reading, never negative and 0 when nothing was fetched.
int64_t snapshot_age_ms(const UsageSnapshot& snap, int64_t now_ms);

Freshness freshness_of(const UsageSnapshot& snap, int64_t now_ms);

// Writes the annotation for `f` and returns true, or writes an empty string
// and returns false when the data is fresh and needs no comment. 24 bytes is
// enough for the longest of them.
bool format_staleness(Freshness f, int64_t age_ms, char* out, size_t n);

// "45m", "1h20m", "2d23h". Never negative, never longer than 7 chars.
void format_duration(int64_t ms, char* out, size_t n);

// "14:44" from an epoch-ms instant, in UTC -- core/ has no timezone database
// and the device has no zone to read. Always 5 chars.
void format_clock(int64_t now_ms, char* out, size_t n);

void draw_tabs(Canvas& c, const UsageSnapshot& snap, int active, const char* clock);
void draw_footer(Canvas& c, const char* left, const char* right, uint8_t left_v = I_DIM);

// The worst verdict across a provider's windows: Burnout beats OnPace and
// Unknown, which beat Surplus and Ready. Windows tied on severity resolve to
// the first one the provider sent, so a session block that has not started
// yet is reported ahead of a weekly ration that is merely comfortable.
// Unknown when nothing in the provider is readable at all.
PaceState worst_pace(const Provider& prov, int64_t now_ms);

// What the terminal has to say about it, in Vault-Tec's house style: sunny
// about figures that are not. Never longer than 26 characters, so it always
// clears the staleness annotation on the other end of the footer.
const char* vault_caption(Freshness f, PaceState worst);

// "AP" for a window that refills within a day, "HP" for one that does not.
// Action points come back before the next fight; health does not, which is
// exactly the difference between a session block and a weekly ration.
const char* window_badge(int64_t period_ms);

int hero_width();

// One of the two big gauges: badge, label, percent remaining, drain bar, and
// a verdict-and-countdown row under it. No surrounding box -- the numbers are
// large enough to hold the space on their own, and a card border around each
// was most of what made the old screen read as a dashboard.
void draw_hero(Canvas& c, int x, int y, int w, const ProgressLine& line, const Pace& p);

// A demoted window: one row, badge and bar and percent, no verdict.
void draw_strip(Canvas& c, int x, int y, int w, const ProgressLine& line, const Pace& p);

// Both gauges, the extra rows under them, and the "+N" tag when a window did
// not fit even there.
void draw_windows(Canvas& c, const Provider& prov, int64_t now_ms);

void draw_chart(Canvas& c, const Provider& prov);

// Tokens, abbreviated: "742", "56.3K", "98.1M", "5.3B". One decimal above a
// thousand, never wider than six characters. A negative count is "--".
void format_tok(int64_t tokens, char* out, size_t n);

// Whole dollars from the head of an OpenUsage text value -- "$4,512.30 - 4.8B"
// is 4512. Cents are dropped, not rounded: the panel has no room for them and
// nobody reads a bottle-cap count to two places. -1 when there is no figure
// there to read.
int64_t parse_caps(const char* s);

// Comma-grouped: "4,869".
void format_caps(int64_t dollars, char* out, size_t n);

// Total tokens over the last `days` entries of the chart, or -1 when the chart
// does not go back that far.
int64_t chart_total(const Provider& prov, int days);

// TODAY / YESTERDAY / 30 DAYS as a TOK column and a CAPS column.
void draw_exposure_log(Canvas& c, int x, int y, int w, const Provider& prov);

// Full deflection on the meter, in tokens per hour. A heavy day at the
// pace the chart shows sits around a third of this. The needle pins here
// rather than the scale rescaling: a gauge whose scale moves is not a gauge,
// and the whole point is that the same deflection means the same thing today
// as it did yesterday.
constexpr int64_t TOK_FULL_SCALE = 60000000LL;

// Where the arc turns bright, as a fraction of full scale.
constexpr float TOK_DANGER_FRAC = 0.6f;

// A needle whose deflection is the measured burn rate. A negative rate means
// the history is too short to say, and draws a dim needle at rest with no
// figure beside it -- an honest "warming up" rather than a confident zero.
void draw_tok_meter(Canvas& c, int x, int y, int w, int64_t tok_per_hour);

// Radiation trefoil, drawn procedurally rather than as a bitmap -- at this size
// a hand-pixelled one reads as a smudge.
void draw_radiation(Canvas& c, int cx, int cy, int r, uint8_t v);

void render_ambient(Canvas& c, const UsageSnapshot& snap, int provider_index,
                    int64_t now_ms, const char* clock, int64_t tok_per_hour = -1,
                    Page page = Page::Stat);

}  // namespace cb
