#!/usr/bin/env python3
"""Emit the three Vault Boy sprites in src/core/vaultboy.cpp.

This used to draw him from ellipses and polygons. It never worked -- six
passes produced something between an emoji and a frog, and the character
never arrived. Vault Boy is a specific drawing, not a set of proportions, so
this traces actual artwork down to the bead grid instead. See
tools/trace_vaultboy.py for how a colour cel becomes phosphor levels.

The sources in tools/assets/vaultboy/ are third-party Fallout artwork, kept
here so the sprites can be regenerated rather than only hand-edited. They are
fine for a device on your own desk; strip them before this repo goes anywhere.

Each pose is cropped so the HEAD fills a good share of the grid. Traced whole,
a full-body figure puts the face on two beads and it turns to mush -- which is
the same mistake the hand-drawn versions made, arrived at from the other
direction.

Run:  python3 tools/make_vaultboy.py            (emits the arrays)
      python3 tools/make_vaultboy.py --png o.png (look at them first)

ALWAYS judge the result on the post-processed frame -- render out/ambient.png
and zoom that -- never on the raw sprite on black.
"""
import os, sys
from trace_vaultboy import trace, upscale

BW, BH = 54, 60          # beads; stored at 2x, so VB_W=108 VB_H=120, drawn 1:1

HERE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'assets', 'vaultboy')

# name -> (file, crop as fractions L,T,R,B)
POSES = [
    ('Fine',   'surplus-thumbs-up.png', (0.00, 0.00, 1.00, 0.58)),
    ('Steady', 'onpace-pointing.png',   (0.00, 0.00, 1.00, 0.60)),
    # The source is an eight-cell sheet of slump animation frames; this is the
    # top-right one, which has the most open mouth and reads worst-off.
    ('Fried',  'burnout-slumped.png',   (0.75, 0.00, 1.00, 0.52)),
]

art = {name: upscale(trace(os.path.join(HERE, f), BW, BH, crop))
       for name, f, crop in POSES}

if '--png' in sys.argv:
    from trace_vaultboy import CH
    import zlib, struct
    rows = [''.join(art[n][y].ljust(BW * 2) + '    ' for n, _, _ in POSES)
            for y in range(BH * 2)]
    lut, scale = {'#': 255, '+': 200, '.': 110, ':': 45, ' ': 0}, 4
    w, h = len(rows[0]), len(rows)
    raw = b''
    for y in range(h * scale):
        row = bytearray()
        for x in range(w * scale):
            v = lut[rows[y // scale][x // scale]]
            row += bytes((int(v * .15), int(v * .85), int(v * .25)))
        raw += b'\x00' + bytes(row)
    def ck(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(sys.argv[sys.argv.index('--png') + 1], 'wb').write(
        b'\x89PNG\r\n\x1a\n'
        + ck(b'IHDR', struct.pack('>IIBBBBB', w * scale, h * scale, 8, 2, 0, 0, 0))
        + ck(b'IDAT', zlib.compress(raw)) + ck(b'IEND', b''))
    sys.exit(0)

for name, _, _ in POSES:
    print(f'const char* const kBoy{name}[VB_H] = {{')
    for row in art[name]: print(f'    "{row}",')
    print('};\n')
