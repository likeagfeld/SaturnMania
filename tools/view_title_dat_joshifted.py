#!/usr/bin/env python3
"""view_title_dat_joshifted.py - Render cd/TITLE.DAT through the SAME
CRAM-shift lookup jo's vdp2 path uses on Saturn: pixel byte V displays as
palette[V-1] (per memory/jo-cram-off-by-one-shift.md + tools/shift_pal.py
docstring).  This is what's ACTUALLY on screen, vs view_title_dat.py which
shows the raw byte->palette mapping (wrong by 1)."""
import struct, sys
from pathlib import Path
try:
    from PIL import Image
except ImportError:
    sys.exit("requires Pillow")

W, H = 224, 512
ROOT = Path(__file__).resolve().parent.parent
dat = (ROOT / "cd/TITLE.DAT").read_bytes()
pal = (ROOT / "cd/TITLE.PAL").read_bytes()

# Decode palette as RGB555 BE (Saturn CRAM convention).
rgb = []
for i in range(256):
    w = struct.unpack(">H", pal[i*2:i*2+2])[0]
    r5 = (w >> 10) & 0x1F
    g5 = (w >>  5) & 0x1F
    b5 = (w      ) & 0x1F
    rgb.append((r5 << 3 | r5 >> 2, g5 << 3 | g5 >> 2, b5 << 3 | b5 >> 2))

# Histogram of what colors are actually visible AT THE CRAM SLOT pixel V reads:
import collections
slot_hits = collections.Counter()
img = Image.new("RGB", (W, H))
px = img.load()
for y in range(H):
    for x in range(W):
        v = dat[y * W + x]
        slot = (v - 1) & 0xFF                 # jo's CRAM down-shift
        slot_hits[slot] += 1
        px[x, y] = rgb[slot]

out = ROOT / "tools/qa_golden/title_dat_joshifted.png"
img.save(out)
print(f"wrote {out}")
print(f"top 16 most-rendered CRAM slots (after jo down-shift):")
for slot, count in slot_hits.most_common(16):
    print(f"  slot {slot:3d}: {count:>6} px  rgb={rgb[slot]}")
