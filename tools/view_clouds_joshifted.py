#!/usr/bin/env python3
"""view_clouds_joshifted.py - Phase 1.34b static-preview gate.

Renders cd/CLOUDS.DAT through the SAME jo CRAM-shift lookup the Saturn
will use at runtime: pixel byte V renders palette[V-1] (per
memory/jo-cram-off-by-one-shift.md + tools/shift_pal.py).

MANDATORY BEFORE FLASH: per the Phase 1.29c green-flood lesson, the
asset MUST be visually verified at THE Saturn-effective palette mapping
BEFORE the ISO build.  If this preview looks scrambled, the builder is
wrong -- fix builder, don't flash.
"""
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("requires Pillow")

W, H = 256, 256
ROOT = Path(__file__).resolve().parent.parent
dat_path = ROOT / "cd/CLOUDS.DAT"
pal_path = ROOT / "cd/CLOUDS.PAL"

if not dat_path.exists() or not pal_path.exists():
    sys.exit(f"missing {dat_path} or {pal_path}; run "
             f"tools/build_clouds_bg.py first")

dat = dat_path.read_bytes()
pal = pal_path.read_bytes()
assert len(dat) == W * H, f"DAT size {len(dat)} != {W*H}"
assert len(pal) == 512, f"PAL size {len(pal)} != 512"

# RGB555 BE decode of disk palette
rgb = []
for i in range(256):
    w = struct.unpack(">H", pal[i*2:i*2+2])[0]
    r5 = (w >> 10) & 0x1F
    g5 = (w >>  5) & 0x1F
    b5 = (w      ) & 0x1F
    rgb.append((r5 << 3 | r5 >> 2,
                g5 << 3 | g5 >> 2,
                b5 << 3 | b5 >> 2))

import collections
slot_hits = collections.Counter()
img = Image.new("RGB", (W, H))
px = img.load()
for y in range(H):
    for x in range(W):
        v = dat[y * W + x]
        slot = (v - 1) & 0xFF
        slot_hits[slot] += 1
        px[x, y] = rgb[slot]

out = ROOT / "tools/qa_golden/clouds_simulated.png"
out.parent.mkdir(parents=True, exist_ok=True)
img.save(out)
print(f"wrote {out}")
print(f"top 16 most-rendered CRAM slots (after jo down-shift):")
for slot, count in slot_hits.most_common(16):
    print(f"  slot {slot:3d}: {count:>6} px  rgb={rgb[slot]}")
