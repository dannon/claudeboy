#pragma once
#include <stdint.h>

namespace cb {

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 240;

// Ready means the window exists but has not started -- OpenUsage omits resetsAt
// for a session block with no usage yet, since the five hours begin on first
// use. Distinct from Unknown, which means too little of a RUNNING window has
// elapsed for the rate to mean anything.
enum class PaceState : uint8_t { Surplus, OnPace, Burnout, Unknown, Ready };

struct ProgressLine {
    const char* label;
    int32_t used;
    int32_t limit;
    int64_t resets_at_ms;
    int64_t period_ms;
};

struct TextLine { const char* label; const char* value; };
struct ChartPoint { const char* label; int64_t value; };

struct Provider {
    const char* id;
    const char* display_name;
    const char* plan;             // "Max 5x"; empty when the provider reports none
    // When the agent last read this provider, not when the board last got a
    // reply: a fast response carrying hour-old numbers is an hour old. Zero
    // means nothing was ever fetched.
    int64_t fetched_at_ms;
    const ProgressLine* progress; int progress_count;
    const TextLine* text;         int text_count;
    const ChartPoint* chart;      int chart_count;
};

struct UsageSnapshot {
    const Provider* providers; int provider_count;
    int64_t server_time_ms;
};

struct Pace {
    PaceState state;
    float remaining_frac;
    float elapsed_frac;
    float ratio;
    float projected_frac;
    int64_t reset_in_ms;
    int64_t burnout_early_ms;
    bool valid;
};

}  // namespace cb
