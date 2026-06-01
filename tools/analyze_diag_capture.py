#!/usr/bin/env python3
"""analyze_diag_capture.py - Per-frame quantitative analysis of qa_diag_*.png
sequence to identify (1) when title content lands, (2) when/if crash hits,
(3) magenta + neon-green artifact pixel counts vs expected sky-blue/green
backdrop, (4) frame-to-frame mean diff (detects motion / freeze / crash)."""
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
print(f"analyzing {len(frames)} frames")

# Saturn display ROI: Mednafen window is 912x749 with chrome.  The actual
# Saturn 320x240 region centers in roughly the middle of the window.  Use a
# central ROI for analysis to skip the title-bar / menus.
ROI = (60, 60, 850, 690)


def hue_buckets(arr_rgb):
    """Return per-bucket pixel counts for {magenta, neon_green, sky_blue,
    green_foliage, dark, white_or_near}.  arr_rgb shape: (H,W,3) uint8."""
    r = arr_rgb[..., 0].astype(np.int32)
    g = arr_rgb[..., 1].astype(np.int32)
    b = arr_rgb[..., 2].astype(np.int32)

    # Approximate HSV without going through float
    max_c = np.maximum(np.maximum(r, g), b)
    min_c = np.minimum(np.minimum(r, g), b)
    delta = max_c - min_c
    sat = np.where(max_c > 0, (delta * 255) // np.maximum(max_c, 1), 0)
    val = max_c

    # Hue indicators (without computing actual hue angle):
    # magenta = R>G AND B>G AND (R+B)/2 > G (purple-pink range)
    magenta  = ((r > g) & (b > g) & ((r + b) // 2 > g + 20) &
                (sat > 100) & (val > 80))
    # neon green = G dominant AND highly saturated
    neon_grn = ((g > r + 40) & (g > b + 40) & (sat > 120) & (val > 120))
    # sky blue = B dominant, moderate green, low red
    sky_blue = ((b > r + 20) & (b > g - 20) & (b > 100) & (r < 200))
    # green foliage = G dominant, B and R both lower, mid sat
    grn_foliage = ((g > r) & (g > b) & (sat > 50) & (sat < 200) &
                   (val > 50) & (val < 200) & (g - b > 10))
    # dark = all channels low
    dark = (val < 30)
    white = (val > 200) & (sat < 40)

    total = arr_rgb.shape[0] * arr_rgb.shape[1]
    return {
        "total": int(total),
        "magenta": int(magenta.sum()),
        "neon_green": int(neon_grn.sum()),
        "sky_blue": int(sky_blue.sum()),
        "green_foliage": int(grn_foliage.sum()),
        "dark": int(dark.sum()),
        "white_near": int(white.sum()),
    }


print(f"\n{'frame':>5} {'time_s':>7} {'dark%':>7} {'magenta':>9} "
      f"{'neon_grn':>9} {'sky_blu':>9} {'foliage':>9} {'mean':>6} "
      f"{'delta_prev':>11}")
prev_arr = None
events = []
for i, p in enumerate(frames):
    n = int(p.stem.split("_")[-1])
    t = (n - 1) * 0.33                   # 3fps
    img = Image.open(p).convert("RGB").crop(ROI)
    arr = np.array(img)
    b = hue_buckets(arr)
    pct = lambda k: b[k] * 100.0 / b["total"]
    mean = int(arr.mean())
    if prev_arr is not None:
        delta = float(np.abs(arr.astype(np.int32) - prev_arr.astype(np.int32)).mean())
    else:
        delta = 0.0
    prev_arr = arr
    # Print every 5th frame + every frame where delta > 8 (motion / transition)
    if n % 5 == 1 or delta > 8 or pct("dark") > 80:
        print(f"{n:>5} {t:>7.2f} {pct('dark'):>6.1f}% {b['magenta']:>9} "
              f"{b['neon_green']:>9} {b['sky_blue']:>9} "
              f"{b['green_foliage']:>9} {mean:>6} {delta:>11.2f}")
    # Crash detection: frame goes very dark AND mostly uniform
    if pct("dark") > 90 and delta < 1.0:
        events.append((n, t, "STUCK_DARK", f"dark%={pct('dark'):.1f} delta={delta:.2f}"))
    # Big visual change
    if delta > 30 and n > 5:
        events.append((n, t, "BIG_CHANGE", f"delta={delta:.2f}"))

print("\n=== EVENTS ===")
for e in events:
    print(f"  frame {e[0]:>3} t={e[1]:>5.2f}s  {e[2]}  {e[3]}")
