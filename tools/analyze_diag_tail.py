#!/usr/bin/env python3
"""analyze_diag_tail.py - Drill into last 15 frames of qa_diag capture to
check for the stuck-frame condition described in the Phase 1.31 brief.

Prints every frame in [80,90] inclusive with full metrics including delta
to previous frame, so we can verify whether the title->GHZ transition at
frame 87 lands in a stable game state or stalls."""
import sys
from pathlib import Path
try:
    from PIL import Image
    import numpy as np
except ImportError:
    sys.exit("requires Pillow + numpy")

ROOT = Path(__file__).resolve().parent.parent
frames = sorted(ROOT.glob("qa_diag_*.png"),
                key=lambda p: int(p.stem.split("_")[-1]))
if not frames:
    sys.exit("no qa_diag_*.png found in project root")

ROI = (60, 60, 850, 690)

print(f"{'frame':>5} {'time_s':>7} {'dark%':>7} {'magenta':>9} "
      f"{'neon_g':>9} {'sky_b':>9} {'green':>9} {'mean':>6} "
      f"{'delta_prev':>11}")

prev_arr = None
for p in frames:
    n = int(p.stem.split("_")[-1])
    img = Image.open(p).convert("RGB").crop(ROI)
    arr = np.array(img)
    if prev_arr is not None:
        delta = float(np.abs(arr.astype(np.int32) - prev_arr.astype(np.int32)).mean())
    else:
        delta = 0.0
    if n >= 80:
        r = arr[..., 0].astype(np.int32)
        g = arr[..., 1].astype(np.int32)
        b = arr[..., 2].astype(np.int32)
        max_c = np.maximum(np.maximum(r, g), b)
        min_c = np.minimum(np.minimum(r, g), b)
        delta_c = max_c - min_c
        sat = np.where(max_c > 0, (delta_c * 255) // np.maximum(max_c, 1), 0)
        val = max_c
        magenta = ((r > g) & (b > g) & ((r + b) // 2 > g + 20) &
                   (sat > 100) & (val > 80))
        neon_g = ((g > r + 40) & (g > b + 40) & (sat > 120) & (val > 120))
        sky_b = ((b > r + 20) & (b > g - 20) & (b > 100) & (r < 200))
        green = ((g > r) & (g > b) & (sat > 50) & (sat < 200) &
                 (val > 50) & (val < 200) & (g - b > 10))
        dark = (val < 30)
        total = arr.shape[0] * arr.shape[1]
        t = (n - 1) * 0.33
        print(f"{n:>5} {t:>7.2f} {(int(dark.sum())*100/total):>6.1f}% "
              f"{int(magenta.sum()):>9} {int(neon_g.sum()):>9} "
              f"{int(sky_b.sum()):>9} {int(green.sum()):>9} "
              f"{int(arr.mean()):>6} {delta:>11.2f}")
    prev_arr = arr
