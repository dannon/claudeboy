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
    {"TODAY",     "$55.40 - 54.8M"},
    {"YESTERDAY", "$189.52 - 208.3M"},
    {"30 DAYS",   "$4,511 - 4.8B"},
};

const ChartPoint kClaudeChart[] = {
    {"Aug 13", 322582660}, {"Aug 14", 229889008}, {"Aug 15", 121578616},
    {"Aug 16", 110886360}, {"Aug 17", 169128655}, {"Aug 18",  92107692},
    {"Aug 19", 527342458}, {"Aug 20", 208264047}, {"Aug 21",  54800000},
};

const Provider kProviders[] = {
    {"claude", "CLAUDE",
     kClaudeProgress, 3,
     kClaudeText,     3,
     kClaudeChart,    9},
};

const UsageSnapshot kSnapshot = {kProviders, 1, REF};

}  // namespace

const UsageSnapshot& fixture_snapshot() { return kSnapshot; }

}  // namespace cb
