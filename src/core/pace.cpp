#include "core/pace.h"

namespace cb {

Pace compute_pace(const ProgressLine& line, int64_t now_ms) {
    Pace p{};
    p.state = PaceState::Unknown;
    p.valid = false;

    if (line.period_ms <= 0 || line.limit <= 0) return p;

    // No reset time at all: the window has not started. Reporting a rate here
    // would be inventing one -- with resets_at_ms of 0 the clamp below reads the
    // window as fully elapsed and calls an untouched budget SURPLUS 0m, which
    // is worse than saying nothing.
    // Exactly zero, not <= 0: zero is what the parser writes when the key is
    // absent, whereas a negative value is a real timestamp relative to a small
    // epoch and still wants the ordinary clamping path below.
    if (line.resets_at_ms == 0) {
        p.valid = true;
        p.state = PaceState::Ready;
        int32_t u = line.used;
        if (u < 0) u = 0;
        if (u > line.limit) u = line.limit;
        p.remaining_frac = 1.0f - static_cast<float>(u) / static_cast<float>(line.limit);
        return p;
    }

    p.valid = true;

    int64_t reset_in = line.resets_at_ms - now_ms;
    if (reset_in < 0) reset_in = 0;
    if (reset_in > line.period_ms) reset_in = line.period_ms;
    p.reset_in_ms = reset_in;

    int64_t elapsed = line.period_ms - reset_in;
    p.elapsed_frac = static_cast<float>(elapsed) / static_cast<float>(line.period_ms);

    int32_t used = line.used;
    if (used < 0) used = 0;
    if (used > line.limit) used = line.limit;
    const float used_frac = static_cast<float>(used) / static_cast<float>(line.limit);
    p.remaining_frac = 1.0f - used_frac;

    // Window has not started consuming yet: no meaningful rate.
    if (elapsed <= 0) {
        p.ratio = 0.0f;
        p.projected_frac = 0.0f;
        p.state = PaceState::Unknown;
        return p;
    }

    p.ratio = used_frac / p.elapsed_frac;
    p.projected_frac = p.ratio;

    // Too early in the window for the rate to mean anything. Exhaustion is
    // still real news, so that check comes first.
    if (used_frac < 1.0f && p.elapsed_frac < MIN_ELAPSED_FRAC) {
        p.state = PaceState::Unknown;
        return p;
    }

    if (used_frac >= 1.0f) {
        // Already exhausted; the whole remaining window is "early".
        p.state = PaceState::Burnout;
        p.burnout_early_ms = reset_in;
        return p;
    }

    if (p.ratio > BURNOUT_RATIO) {
        p.state = PaceState::Burnout;
        // Time to reach the limit at the observed rate.
        const double eta = static_cast<double>(elapsed) * (1.0 - used_frac) / used_frac;
        const double early = static_cast<double>(reset_in) - eta;
        p.burnout_early_ms = early > 0 ? static_cast<int64_t>(early) : 0;
    } else if (p.ratio >= SURPLUS_RATIO) {
        p.state = PaceState::OnPace;
    } else {
        p.state = PaceState::Surplus;
    }
    return p;
}

}  // namespace cb
