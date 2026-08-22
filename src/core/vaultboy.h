#pragma once
#include <stdint.h>
#include "core/canvas.h"
#include "core/types.h"

namespace cb {

// 54x60 beads stored at 2x, drawn 1:1, so a bead is two pixels -- the floor
// for surviving scanlines and bloom. Traced from artwork rather than drawn:
// see tools/make_vaultboy.py for why every hand-drawn attempt failed.
constexpr int VB_W = 108;
constexpr int VB_H = 120;

// Thumbs up, finger guns, or slumped and miserable. The pose carries the
// verdict from across the room; the face is there for whoever walks over to
// look. At this size the silhouette does nearly all of the work, which is why
// the three sources were picked for having different outlines rather than
// different expressions.
enum class BoyMood : uint8_t { Fine, Steady, Fried };

// The mood the worst window across a provider earns. Burnout anywhere is
// Fried even if everything else is comfortable -- a weekly ration burning
// down is not made better by a fresh session block.
BoyMood boy_mood(const Provider& prov, int64_t now_ms);

// Nearest-neighbour scaled by num/den, top-left anchored at x,y. The art is
// authored on a 42x35 bead grid and stored at 2x, so no feature is finer than
// two stored pixels and a 3/2 upscale still lands three pixels on every bead.
// That is why the hero on the STAT page costs no second sprite.
void draw_vault_boy(Canvas& c, int x, int y, BoyMood mood, int num = 1, int den = 1);

// The rows of one pose, VB_H of them. Exposed so a test can check every row is
// exactly VB_W characters: a short row clips the sprite silently, and these
// arrays are pasted in from a generator rather than hand-kept, so that is a
// real risk and not a theoretical one.
const char* const* boy_art(BoyMood mood);

}  // namespace cb
