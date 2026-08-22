#!/usr/bin/env python3
"""Trace a Vault Boy image down to a bead grid for src/core/vaultboy.cpp.

Drawing him from shape primitives did not work. Five passes of ellipses and
polygons produced something between an emoji and a frog, and the character
never arrived -- so this traces the actual artwork instead.

The source is flat-colour line art, which is the whole reason this is tractable:
every pixel belongs to one of five classes, and each class maps to one phosphor
level. Black line work becomes a CUT rather than ink, because on a phosphor
panel lit means ink and the drawing has to be holes punched in a lit field --
render the lines as the bright part and the sprite arrives as wireframe.

    hair, collar, trim (yellow)  -> '+'  200
    skin (peach)                 -> '.'  110
    vault suit (blue)            -> ':'   45
    outline (black)              -> ' '   cut
    outside the figure           -> ' '

Downscaling is a majority vote per bead, except that CUT wins on a much lower
share than a majority: at this reduction a six-pixel outline covers well under
half a bead, and letting it lose every vote welds the eyes, the grin and the
fingers into one blob.

Run:  python3 tools/trace_vaultboy.py SRC.png BW BH [--crop L,T,R,B] [--png OUT.png]

--crop takes fractions of the source, applied before anything else. Sources
are sprite sheets and full-body figures; the head has to fill a good share of
the grid or the face lands on two beads and turns to mush.
"""
import sys
from PIL import Image

EMPTY, CUT, SUIT, SKIN, HAIR = 0, 1, 2, 3, 4
CH = {EMPTY: ' ', CUT: ' ', SUIT: ':', SKIN: '.', HAIR: '+'}

# A bead goes dark on this share of line pixels rather than needing a majority.
CUT_SHARE = 0.30
# Below this, a bead that is mostly transparent is left out of the silhouette.
FIGURE_SHARE = 0.45

def classify(px):
    r, g, b, a = px
    if a < 128: return EMPTY
    # Blue is tested BEFORE luminance. Royal blue is darker than the black
    # threshold -- the suit scored a luminance of about 23 -- so a luminance
    # test first swallows the entire vault suit into the line work and leaves
    # a head floating over nothing.
    if b > 100 and b > r + 50 and b > g + 40: return SUIT
    lum = (r * 299 + g * 587 + b * 114) // 1000
    if lum < 90: return CUT
    if r > 205 and g > 205 and b > 205: return EMPTY      # the white disc behind him
    if r > 180 and g > 160 and b < 150: return HAIR       # yellow before peach:
    return SKIN                                           # they part on blue

def _load(path, crop):
    im = Image.open(path).convert('RGBA')
    if crop:
        w, h = im.size
        l, t, r, b = crop
        im = im.crop((int(l * w), int(t * h), int(r * w), int(b * h)))
    return im

def trace(path, bw, bh, crop=None):
    im = _load(path, crop)
    # Crop to the figure and letterbox it into the grid. Source art is padded
    # unpredictably, and scaling the padding costs beads that the face needs.
    box = im.split()[3].point(lambda v: 255 if v > 128 else 0).getbbox()
    if box: im = im.crop(box)
    sw, sh = im.size
    scale = min(bw / sw, bh / sh)
    tw, th = max(1, int(sw * scale)), max(1, int(sh * scale))
    fitted = Image.new('RGBA', (bw, bh), (0, 0, 0, 0))
    fitted.paste(im.resize((tw, th), Image.LANCZOS), ((bw - tw) // 2, (bh - th) // 2))
    # Re-expand so the per-bead vote below still averages real source pixels
    # rather than one already-resampled one.
    im = Image.new('RGBA', (bw * 8, bh * 8), (0, 0, 0, 0))
    src_fit = _load(path, crop)
    if box: src_fit = src_fit.crop(box)
    im.paste(src_fit.resize((tw * 8, th * 8), Image.LANCZOS),
             (((bw - tw) // 2) * 8, ((bh - th) // 2) * 8))
    W, H = im.size
    src = im.load()
    cls = [[classify(src[x, y]) for x in range(W)] for y in range(H)]

    grid = []
    for by in range(bh):
        row = []
        y0, y1 = by * H // bh, max(by * H // bh + 1, (by + 1) * H // bh)
        for bx in range(bw):
            x0, x1 = bx * W // bw, max(bx * W // bw + 1, (bx + 1) * W // bw)
            n = (y1 - y0) * (x1 - x0)
            count = [0] * 5
            for y in range(y0, y1):
                cr = cls[y]
                for x in range(x0, x1): count[cr[x]] += 1
            if count[CUT] >= CUT_SHARE * n:
                row.append(CUT)
            elif (n - count[EMPTY]) < FIGURE_SHARE * n:
                row.append(EMPTY)
            else:
                row.append(max((SUIT, SKIN, HAIR), key=lambda k: count[k]))
        grid.append(row)
    return [''.join(CH[c] for c in r) for r in grid]

def upscale(rows, s=2):
    out = []
    for r in rows:
        line = ''.join(c * s for c in r)
        for _ in range(s): out.append(line)
    return out

if __name__ == '__main__':
    src, bw, bh = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    crop = None
    if '--crop' in sys.argv:
        crop = tuple(float(v) for v in sys.argv[sys.argv.index('--crop') + 1].split(','))
    rows = trace(src, bw, bh, crop)
    if '--png' in sys.argv:
        import zlib, struct
        art = upscale(rows)
        lut = {'#': 255, '+': 200, '.': 110, ':': 45, ' ': 0}
        scale, w, h = 4, len(art[0]), len(art)
        raw = b''
        for y in range(h * scale):
            row = bytearray()
            for x in range(w * scale):
                v = lut[art[y // scale][x // scale]]
                row += bytes((int(v * .15), int(v * .85), int(v * .25)))
            raw += b'\x00' + bytes(row)
        def ck(t, d):
            return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
        open(sys.argv[sys.argv.index('--png') + 1], 'wb').write(
            b'\x89PNG\r\n\x1a\n'
            + ck(b'IHDR', struct.pack('>IIBBBBB', w * scale, h * scale, 8, 2, 0, 0, 0))
            + ck(b'IDAT', zlib.compress(raw)) + ck(b'IEND', b''))
    else:
        for r in upscale(rows): print(f'    "{r}",')
