#!/usr/bin/env python3
"""Phase 1.11 back-color pixel sampler.

Reads qa_probe_phase111.png and samples pixels at multiple coordinates,
including:
  - the (~30px) NBG2-uncovered sidebands at far-left / far-right of the
    Saturn 320-wide framebuffer (where back-color shows through).
  - the chrome border outside the Saturn framebuffer (for control reference).
  - the center of the NBG2 backdrop (for control reference).

Three possible outcomes per docs/COMPREHENSIVE_PLAN.md §11.17:
  Outcome A — sentinel-green ~ RGB (0, 255, 0)  -> SC-3 (sprite_id < 0)
  Outcome B — cyan-tint     ~ RGB (R<32, ~0x84, ~0x84)  -> bug downstream
  Outcome C — sky-blue      ~ RGB (0, 56, 200)  -> probe_draw not called
"""
from PIL import Image
import sys, os

PNG = os.path.join(os.path.dirname(__file__), "..", "qa_probe_phase111.png")
img = Image.open(PNG).convert("RGB")
W, H = img.size
print(f"image: {PNG} -> {W}x{H}")

# Mednafen's window contains the emulator chrome at top + bottom + sides.
# The 320x256 PAL framebuffer is centered in this 912x749 window. The Saturn
# back-color only appears in the ~36px NBG2-uncovered sidebands.
#
# qa_probe_bcol.png pixel-sampling from Phase 1.10 read (col 20, row 400) =
# RGB(0, 56, 200), (col 450, row 40) = RGB(0, 56, 200) — both sky-blue. Those
# coords land in the back-color region. We'll re-use them as primary samples
# and also scan a grid of edge candidates.

points = [
    ("Phase1.10 sample A (col 20, row 400)", 20, 400),
    ("Phase1.10 sample C (col 450, row 40)", 450, 40),
    ("far-left sideband (col 5, row 374)",   5,  374),
    ("far-left sideband (col 10, row 200)",  10, 200),
    ("far-left sideband (col 30, row 200)",  30, 200),
    ("far-right sideband (col W-6, row 374)", W-6, 374),
    ("far-right sideband (col W-30, row 200)", W-30, 200),
    ("top strip (col 450, row 20)",  450, 20),
    ("bottom strip (col 450, row H-20)", 450, H-20),
    ("center NBG2 (col 450, row 374)", 450, 374),
]

print()
print(f"{'sample':45s}  {'RGB':>16s}  outcome")
print("-" * 80)
for label, x, y in points:
    x = max(0, min(W-1, x))
    y = max(0, min(H-1, y))
    r,g,b = img.getpixel((x, y))
    # Classify against thresholds
    if g > 200 and r < 80 and b < 80:
        outc = "SENTINEL-GREEN (Outcome A: probe_draw runs, sprite_id<0)"
    elif r < 80 and 30 < g < 90 and 150 < b < 255:
        outc = "SKY-BLUE (Outcome C: probe_draw NOT running)"
    elif b > 100 and g > 100 and r < 80:
        outc = "CYAN-TINT? (Outcome B: descriptor valid, downstream bug)"
    else:
        outc = "(other)"
    print(f"{label:45s}  ({r:3d},{g:3d},{b:3d})       {outc}")
