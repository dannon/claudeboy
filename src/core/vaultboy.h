#pragma once
#include <stdint.h>
#include "core/canvas.h"
#include "core/types.h"

namespace cb {

constexpr int VB_W = 84;
constexpr int VB_H = 70;

// Thumb up, flat palm, thumb down. The gesture carries the reading from
// across the room; the face is there for whoever walks over to look.
enum class BoyMood : uint8_t { Fine, Steady, Fried };

// The mood the worst window across a provider earns. Burnout anywhere is
// Fried even if everything else is comfortable -- a weekly ration burning
// down is not made better by a fresh session block.
BoyMood boy_mood(const Provider& prov, int64_t now_ms);

void draw_vault_boy(Canvas& c, int x, int y, BoyMood mood);

}  // namespace cb
