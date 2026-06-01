#!/usr/bin/env python3
"""view_title_dat.py - Render cd/TITLE.DAT (224x512 8bpp) + cd/TITLE.PAL
(256 entries, 2 bytes/entry RGB555 little-endian per Saturn CRAM) into a
viewable PNG.  Phase 1.29c diagnostic: confirm what backdrop the user is
currently seeing before building TITLE3D.DAT."""
import struct, sys
from pathlib import Path
try:
    from PIL import Image
except ImportError:
    sys.exit("requires Pillow (pip install Pillow)")

W, H = 224, 512
ROOT = Path(__file__).resolve().parent.parent
dat = (ROOT / "cd/TITLE.DAT").read_bytes()
pal = (ROOT / "cd/TITLE.PAL").read_bytes()
assert len(dat) == W * H, f"TITLE.DAT size {len(dat)} != {W*H}"
assert len(pal) >= 512, f"TITLE.PAL size {len(pal)} < 512"

# Saturn CRAM: RGB555 packed as 16-bit big-endian per jo's convention;
# bits 14..10 = R, 9..5 = G, 4..0 = B (per ST-058-R2 § Color RAM).
rgb_palette = []
for i in range(256):
    word = struct.unpack(">H", pal[i*2:i*2+2])[0]
    r5 = (word >> 10) & 0x1F
    g5 = (word >>  5) & 0x1F
    b5 = (word      ) & 0x1F
    rgb_palette.append((r5 << 3 | r5 >> 2, g5 << 3 | g5 >> 2, b5 << 3 | b5 >> 2))

img = Image.new("RGB", (W, H))
px = img.load()
for y in range(H):
    for x in range(W):
        px[x, y] = rgb_palette[dat[y*W + x]]

out_path = ROOT / "tools/qa_golden/title_dat_current.png"
out_path.parent.mkdir(parents=True, exist_ok=True)
img.save(out_path)
print(f"wrote {out_path} ({W}x{H} RGB from 224x512 8bpp + RGB555 palette)")
print(f"non-zero palette entries: {sum(1 for c in rgb_palette if c != (0,0,0))}/256")
print(f"first 8 palette entries: {rgb_palette[:8]}")
