#pragma once
#include <stddef.h>
#include "core/canvas.h"
#include "core/font.h"
#include "core/pace.h"
#include "core/types.h"

namespace cb {

// Layout, in panel pixels. The session block and the weekly ration are the
// two numbers worth reading from across the room, so they get half the panel
// each and everything else is demoted to a strip.
constexpr int MARGIN   = 6;
constexpr int TAB_H    = 12;    // tab text band; the rule sits on this row

constexpr int HERO_Y   = 16;
constexpr int HERO_H   = 68;
constexpr int HERO_GAP = 6;

// One row for whatever windows did not make hero. The band holds exactly one:
// the provider ships three windows today, and a fixed layout is worth more
// than room for a fourth that does not exist. A window that will not fit is
// counted in a "+N" tag rather than dropped in silence.
constexpr int STRIP_Y  = 88;
constexpr int STRIP_H  = 13;

constexpr int CHART_Y  = 105;
constexpr int CHART_H  = 44;

// The provider sends 31 days; we draw the tail. A month of bars at this width
// is a grey smear that answers no question you actually have.
constexpr int CHART_DAYS = 7;

// The bottom panel: Vault Boy, the rad meter, and the exposure log share it.
constexpr int PANEL_Y  = 153;
constexpr int PANEL_H  = 60;

constexpr int FOOT_Y   = 219;   // footer text row; rule sits three above

// Intensity levels. Pace state is carried by brightness, not colour.
constexpr uint8_t I_DIM    = 110;
constexpr uint8_t I_NORMAL = 200;
constexpr uint8_t I_BRIGHT = 255;
constexpr uint8_t I_RULE   = 150;

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
void draw_footer(Canvas& c, const char* left, const char* right);

// "AP" for a window that refills within a day, "HP" for one that does not.
// Action points come back before the next fight; health does not, which is
// exactly the difference between a session block and a weekly ration.
const char* window_badge(int64_t period_ms);

int hero_width();

// One of the two big cards: badge, percent remaining, drain bar, verdict.
void draw_hero(Canvas& c, int x, int y, int w, const ProgressLine& line, const Pace& p);

// A demoted window: one row, badge and bar and percent, no verdict.
void draw_strip(Canvas& c, int x, int y, int w, const ProgressLine& line, const Pace& p);

// Both heroes, the strip, and the "+N" tag when a window did not fit.
void draw_windows(Canvas& c, const Provider& prov, int64_t now_ms);

void draw_chart(Canvas& c, const Provider& prov);

// Radiation trefoil, drawn procedurally rather than as a bitmap -- at this size
// a hand-pixelled one reads as a smudge.
void draw_radiation(Canvas& c, int cx, int cy, int r, uint8_t v);

void render_ambient(Canvas& c, const UsageSnapshot& snap, int provider_index,
                    int64_t now_ms, const char* clock);

}  // namespace cb
