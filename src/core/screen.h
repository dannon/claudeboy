#pragma once
#include <stddef.h>
#include "core/canvas.h"
#include "core/font.h"
#include "core/pace.h"
#include "core/types.h"

namespace cb {

// Layout, in panel pixels. Chosen so the chart owns the bottom half and
// nothing collides at three or four cells.
constexpr int MARGIN   = 6;
constexpr int TAB_H    = 12;    // tab text band; the rule sits on this row
constexpr int CELL_Y   = 16;
constexpr int CELL_H   = 60;
constexpr int CELL_GAP = 4;
constexpr int CHART_Y  = 82;
constexpr int CHART_H  = 128;
constexpr int FOOT_Y   = 219;   // footer text row; rule sits three above

// Intensity levels. Pace state is carried by brightness, not colour.
constexpr uint8_t I_DIM    = 110;
constexpr uint8_t I_NORMAL = 200;
constexpr uint8_t I_BRIGHT = 255;
constexpr uint8_t I_RULE   = 150;

// "45m", "1h20m", "2d23h". Never negative, never longer than 7 chars.
void format_duration(int64_t ms, char* out, size_t n);

void draw_tabs(Canvas& c, const UsageSnapshot& snap, int active, const char* clock);
void draw_footer(Canvas& c, const char* left, const char* right);

int cell_width(int count);
void draw_gauge_cell(Canvas& c, int x, int y, int w,
                     const ProgressLine& line, const Pace& p);
void draw_cells(Canvas& c, const Provider& prov, int64_t now_ms);

}  // namespace cb
