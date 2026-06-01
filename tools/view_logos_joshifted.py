#!/usr/bin/env python3
"""view_logos_joshifted.py - Phase 3.1 Path B static-preview gate.

Renders cd/LOGOS.ATL frame 0 (Sega logo) through a CRAM-shift simulation
that mirrors the Saturn 4-bpp Color Bank 16 runtime: each pixel nibble V
in the packed atlas renders palette[V] directly (no jo down-shift for
4-bpp atlases, since the COLR field selects the bank base in CRAM and
the nibble value is the in-bank offset).

This differs from view_clouds_joshifted.py which targets 8-bpp NBG2
images that go through jo's down-shift convention. The TSONIC.ATL +
ELECTRA.ATL + TITLE3D.ATL pipelines use the COLR-bank path so the
in-atlas nibble is the literal palette index inside the 16-color
bank — see src/mania/Objects/Title/TitleAssets.c::title_tsonic_draw_
frame for the runtime attr.colno + atrb encoding.

MANDATORY BEFORE FLASH: per the Phase 1.29c green-flood lesson, this
preview must visually verify a recognisable Sega logo BEFORE the build
flashes to ISO. If this PNG is scrambled, the builder is wrong; fix
the builder, do not flash.
"""
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("requires Pillow")

ROOT = Path(__file__).resolve().parent.parent
atl_path = ROOT / "cd/LOGOS.ATL"

if not atl_path.exists():
    sys.exit(f"missing {atl_path}; run tools/build_logos_atlas.py first")

data = atl_path.read_bytes()

# Parse v4 header.
magic   = struct.unpack(">H", data[0:2])[0]
version = struct.unpack(">H", data[2:4])[0]
anim_ct = struct.unpack(">H", data[4:6])[0]
pal_sz  = struct.unpack(">H", data[6:8])[0]
if magic != 0x5453 or version != 0x0004:
    sys.exit(f"FAIL: bad magic/version 0x{magic:04x}/0x{version:04x}")
if pal_sz != 16:
    sys.exit(f"FAIL: palette size {pal_sz} != 16")

# Decode 16-color BGR1555 palette into RGB888 (treat MSB as opaque flag —
# slot 0 with MSB=0 is the transparent marker, rendered as a chequerboard).
palette_rgb = []
palette_opaque = []
for i in range(16):
    w = struct.unpack(">H", data[8 + i * 2:8 + i * 2 + 2])[0]
    msb = (w >> 15) & 1
    b5  = (w >> 10) & 0x1F
    g5  = (w >>  5) & 0x1F
    r5  = (w      ) & 0x1F
    palette_rgb.append((r5 << 3 | r5 >> 2,
                        g5 << 3 | g5 >> 2,
                        b5 << 3 | b5 >> 2))
    palette_opaque.append(bool(msb))

# Header(44) + 1 anim record(8) + 1 frame record(14) = 66 bytes; pool follows.
ANIM_REC = 8
FRAME_REC = 14
ANIM_OFF  = 44
FRAME_OFF = ANIM_OFF + anim_ct * ANIM_REC
POOL_OFF  = FRAME_OFF + 1 * FRAME_REC   # only one frame for Path B

# Frame record fields.
fr = data[FRAME_OFF:FRAME_OFF + FRAME_REC]
w  = struct.unpack(">H", fr[0:2])[0]
h  = struct.unpack(">H", fr[2:4])[0]
px = struct.unpack(">h", fr[4:6])[0]
py = struct.unpack(">h", fr[6:8])[0]
dur= struct.unpack(">H", fr[8:10])[0]
off= struct.unpack(">I", fr[10:14])[0]

pool = data[POOL_OFF:]
print(f"Frame {w}x{h} pivot=({px},{py}) duration={dur} pool_off={off}")
print(f"Pool bytes={len(pool)} expected={(w*h)//2}")
expected = (w * h) // 2
if len(pool) < expected:
    sys.exit(f"FAIL: pool too small ({len(pool)} < {expected})")

# Render: each byte = high nibble + low nibble (high = leftmost pixel of
# the pair, per build_logos_atlas.py packing).
img = Image.new("RGB", (w, h), (32, 32, 32))
pxs = img.load()
import collections
slot_hits = collections.Counter()
for y in range(h):
    row_start = off + y * (w // 2)
    for xpair in range(w // 2):
        b = pool[row_start + xpair]
        hi = (b >> 4) & 0x0F
        lo = b & 0x0F
        x0 = xpair * 2
        for ix, v in ((x0, hi), (x0 + 1, lo)):
            slot_hits[v] += 1
            if v == 0:
                # Transparent — chequer to make it visually obvious.
                checker = ((ix >> 3) ^ (y >> 3)) & 1
                pxs[ix, y] = (96, 96, 96) if checker else (160, 160, 160)
            else:
                pxs[ix, y] = palette_rgb[v]

out = ROOT / "tools/qa_golden/logos_simulated.png"
out.parent.mkdir(parents=True, exist_ok=True)
img.save(out)
print(f"wrote {out}")
print(f"palette opaque flags: {palette_opaque}")
print(f"per-slot pixel counts (sorted desc):")
for slot, count in slot_hits.most_common():
    pct = 100.0 * count / (w * h)
    flag = "transparent" if slot == 0 else f"rgb={palette_rgb[slot]}"
    print(f"  slot {slot:2d}: {count:>6} px ({pct:5.2f}%)  {flag}")
