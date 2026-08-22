#!/usr/bin/env python3
"""Four corner taps -> the constants in src/device/touch.cpp.

Run:  python3 tools/solve_touch.py TLx,TLy TRx,TRy BRx,BRy BLx,BLy
with the raw pairs from the "tap raw=(...)" lines the board prints while
you tap the corners.

Taps arrive in order: top-left, top-right, bottom-right, bottom-left, i.e.
panel (0,0), (W-1,0), (W-1,H-1), (0,H-1). Whichever raw axis moves between
tap 0 and tap 1 is the one that tracks panel x; if that is raw_y, the axes
are swapped. The sign of the move gives the flip. Corners are extrapolated
from the mean of the two readings at each edge, so one sloppy tap costs
accuracy rather than orientation.
"""
import sys
W, H = 320, 240

def solve(taps):
    rx = [t[0] for t in taps]
    ry = [t[1] for t in taps]
    # How much each raw axis moves along panel x (0->1) and along panel y (1->2).
    dx_along_x, dx_along_y = rx[1] - rx[0], rx[2] - rx[1]
    dy_along_x, dy_along_y = ry[1] - ry[0], ry[2] - ry[1]
    swap = abs(dy_along_x) > abs(dx_along_x)

    # x_axis is the raw axis that tracks panel x; y_axis tracks panel y.
    x_axis, y_axis = (ry, rx) if swap else (rx, ry)
    flip_x = (x_axis[1] - x_axis[0]) < 0
    flip_y = (y_axis[2] - y_axis[1]) < 0

    # Two readings per edge; average them.
    x_left  = (x_axis[0] + x_axis[3]) / 2
    x_right = (x_axis[1] + x_axis[2]) / 2
    y_top   = (y_axis[0] + y_axis[1]) / 2
    y_bot   = (y_axis[2] + y_axis[3]) / 2
    lo_x, hi_x = (x_right, x_left) if flip_x else (x_left, x_right)
    lo_y, hi_y = (y_bot, y_top) if flip_y else (y_top, y_bot)
    return dict(swap=swap, flip_x=flip_x, flip_y=flip_y,
                RAW_X_MIN=round(lo_x if not swap else lo_y),
                RAW_X_MAX=round(hi_x if not swap else hi_y),
                RAW_Y_MIN=round(lo_y if not swap else lo_x),
                RAW_Y_MAX=round(hi_y if not swap else hi_x),
                spans=dict(dx_along_x=dx_along_x, dx_along_y=dx_along_y,
                           dy_along_x=dy_along_x, dy_along_y=dy_along_y))

def check(c, taps):
    """Replay the four taps through the same arithmetic touch.cpp uses."""
    def scale(v, lo, hi, span):
        if hi <= lo: return 0
        s = (v - lo) * span // (hi - lo)
        return max(0, min(span - 1, s))
    out = []
    for rx, ry in taps:
        a = scale(rx, c['RAW_X_MIN'], c['RAW_X_MAX'], H if c['swap'] else W)
        b = scale(ry, c['RAW_Y_MIN'], c['RAW_Y_MAX'], W if c['swap'] else H)
        if c['swap']: a, b = b, a
        if c['flip_x']: a = W - 1 - a
        if c['flip_y']: b = H - 1 - b
        out.append((a, b))
    return out

if __name__ == '__main__':
    taps = [tuple(int(n) for n in a.split(',')) for a in sys.argv[1:]]
    assert len(taps) == 4, "need four taps: TL TR BR BL"
    c = solve(taps)
    for k, v in c.items(): print(f"{k:12} {v}")
    print("\nreplay (want ~(0,0) (319,0) (319,239) (0,239)):")
    for want, got in zip([(0,0),(W-1,0),(W-1,H-1),(0,H-1)], check(c, taps)):
        print(f"  want {want}  got {got}")
