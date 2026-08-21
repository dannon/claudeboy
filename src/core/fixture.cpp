#include "core/fixture.h"

namespace cb {
namespace {

constexpr int64_t HOUR = 3600LL * 1000;
constexpr int64_t REF  = FIXTURE_REFERENCE_MS;

const ProgressLine kClaudeProgress[] = {
    {"SESSION", 21, 100, REF + 45 * HOUR / 60,          5   * HOUR},
    {"WEEKLY",  58, 100, REF + 71 * HOUR + 20 * HOUR/60, 168 * HOUR},
    {"FABLE",   17, 100, REF + 71 * HOUR + 20 * HOUR/60, 168 * HOUR},
};

const TextLine kClaudeText[] = {
    {"TODAY",     "$56.67 - 56.3M"},
    {"YESTERDAY", "$189.52 - 208.3M"},
    {"30 DAYS",   "$4,512 - 4.8B"},
};

const ChartPoint kClaudeChart[] = {
    {"Jul 22", 7059800}, {"Jul 23", 280454831}, {"Jul 24", 75177812},
    {"Jul 25", 5873186}, {"Jul 26", 170205309}, {"Jul 27", 310289801},
    {"Jul 28", 288515597}, {"Jul 29", 153338973}, {"Jul 30", 189217143},
    {"Jul 31", 60061585}, {"Aug 1", 42718893}, {"Aug 2", 69567333},
    {"Aug 3", 262031528}, {"Aug 4", 116112655}, {"Aug 5", 223694312},
    {"Aug 6", 70662090}, {"Aug 7", 32189647}, {"Aug 8", 2399092},
    {"Aug 9", 30911370}, {"Aug 10", 296179122}, {"Aug 11", 193589098},
    {"Aug 12", 77632582}, {"Aug 13", 322582660}, {"Aug 14", 229889008},
    {"Aug 15", 121578616}, {"Aug 16", 110886360}, {"Aug 17", 169128655},
    {"Aug 18", 92107692}, {"Aug 19", 527342458}, {"Aug 20", 208264047},
    {"Aug 21", 56278305},
};
constexpr int kClaudeChartCount = sizeof kClaudeChart / sizeof kClaudeChart[0];

// fetched_at is the reference instant itself, so anything rendering the
// fixture at FIXTURE_REFERENCE_MS -- the host preview, the golden, the tests --
// sees fresh data and no staleness chrome. A zero here would silently turn
// every one of them into NO SIGNAL.
const Provider kProviders[] = {
    {"claude", "CLAUDE", "MAX 5X", REF,
     kClaudeProgress, 3,
     kClaudeText,     3,
     kClaudeChart,    kClaudeChartCount},
};

const UsageSnapshot kSnapshot = {kProviders, 1, REF};

}  // namespace

const UsageSnapshot& fixture_snapshot() { return kSnapshot; }

ProgressLine roll_window_forward(const ProgressLine& line, int64_t now_ms) {
    ProgressLine out = line;
    if (out.period_ms <= 0 || out.resets_at_ms > now_ms) return out;
    // Divide rather than loop: `now` can sit thousands of periods past a
    // capture that has been on the shelf a while.
    const int64_t steps = (now_ms - out.resets_at_ms) / out.period_ms + 1;
    out.resets_at_ms += steps * out.period_ms;
    return out;
}

}  // namespace cb
