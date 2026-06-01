#!/usr/bin/env python3
"""Phase 1.12 three-call-site back-color pixel sampler (refined).

Per the qa_probe_phase112.png inspection:
  - The Mednafen window is 912x749.
  - The Saturn 320-wide framebuffer is centred and uses host cols
    ~14..906 (RED vertical bars visible at cols 0-13 and 907-911).
  - INSIDE the Saturn framebuffer the back-color shows in a wide
    sky-blue annulus around the NBG2 title-backdrop art.
  - OUTSIDE the Saturn framebuffer (the red bars) Mednafen renders
    the VDP2 H-blank border colour, which is a SEPARATE field from
    the active back-color word (see ST-058-R2 BKTAU/BKTAL).

Two-zone classification per docs/COMPREHENSIVE_PLAN.md Sec 11.18:
  zone "active"  - the sky-blue annulus inside the framebuffer.
                   The mania_tick / probe_draw back-color writes
                   should land here on the FRAME the call fires.
  zone "border"  - the red side-bars outside the framebuffer.
                   Only writes that target the BORDER colour (which
                   slBack1ColSet may or may not write, depending on
                   the back-color VRAM allocation) land here.

Sentinel encodings (jo BGR1555 layout, capture-RGB after Mednafen):
  GREEN   0x83E0 -> (0,   ~248, 0)
  MAGENTA 0xFC1F -> (~248, 0,   ~248)
  RED     0x801F -> (~248, 0,   0)
  CYAN-T  0xC2NN -> (low, ~128, ~128)
  SKY-BLUE BASELINE -> (0,   56,  200)  jo_core_init RGB(96,128,224)
"""
from PIL import Image
import os, sys

PNG = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(__file__), "..", "qa_probe_phase112.png")
img = Image.open(PNG).convert("RGB")
W, H = img.size
print(f"image: {PNG} -> {W}x{H}")

# Sample points distributed across both zones.
points = [
    # Active-zone (inside Saturn framebuffer, in the sky-blue back-color
    # annulus surrounding the NBG2 art).
    ("ACTIVE: col 20,  row 400",  20,    400),
    ("ACTIVE: col 450, row 40 ",  450,   40 ),
    ("ACTIVE: col 30,  row 200",  30,    200),
    ("ACTIVE: col 882, row 200",  W-30,  200),
    ("ACTIVE: col 450, row 729",  450,   H-20),
    ("ACTIVE: col 100, row 100",  100,   100),
    ("ACTIVE: col 800, row 100",  800,   100),
    ("ACTIVE: col 100, row 700",  100,   700),
    ("ACTIVE: col 800, row 700",  800,   700),
    # Border-zone (outside Saturn framebuffer, the side-bar regions).
    ("BORDER: col 0,   row 374",  0,     374),
    ("BORDER: col 5,   row 374",  5,     374),
    ("BORDER: col 10,  row 374",  10,    374),
    ("BORDER: col 906, row 374",  906,   374),
    ("BORDER: col 911, row 374",  W-1,   374),
    # Controls
    ("CONTROL: col 450, row 374 (NBG2 art)", 450, 374),
    ("CONTROL: col 200, row 8 (chrome)",     200, 8),
]


def classify(r, g, b):
    if r > 180 and b > 180 and g < 80:
        return "MAGENTA  (probe_draw head fired)"
    if g > 180 and r < 80 and b < 80:
        return "GREEN    (mania_tick head fired, probe_draw did NOT)"
    if r > 180 and g < 80 and b < 80:
        return "RED      (fg_vblank fired)"
    if r < 100 and 90 < g < 170 and 90 < b < 170:
        return "CYAN-TINT (descriptor valid)"
    if r < 40 and 30 < g < 90 and 150 < b < 230:
        return "SKY-BLUE  (baseline jo_core_init)"
    return "(other)"


print()
print(f"{'sample':45s}  {'RGB':>16s}  outcome")
print("-" * 100)
for label, x, y in points:
    x = max(0, min(W - 1, x))
    y = max(0, min(H - 1, y))
    r, g, b = img.getpixel((x, y))
    outc = classify(r, g, b)
    print(f"{label:45s}  ({r:3d},{g:3d},{b:3d})       {outc}")
