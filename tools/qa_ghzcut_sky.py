#!/usr/bin/env python
# qa_ghzcut_sky.py -- Task #309 #2b RED/GREEN gate for the GHZCutscene "black sky
# / Sonic in the ground like a corpse" fix.
#
# SYMPTOM (measured): the front-end FG present (p6_present_config) hardwired the
# VDP2 backdrop to 0x8000 (black) and armed NBG1ON|SPRON only, so the revealed
# GHZCutscene showed Sonic/Tails warping in (ANI_FAN) against a BLACK void. In
# RSDK the GHZ sky is a flat clear color (BG Outside's sky region is a transparent
# index-0 -- verified from Scene1.bin/StageConfig.bin), so the decomp-true base is
# a sky-blue backdrop.
#
# GATE: measure the SKY region of a settled GHZCutscene capture (upper-center,
# above the grass band). PASS (GREEN) iff it is predominantly sky-blue (blue
# channel clearly dominant and not dark); FAIL (RED) iff it is a dark/black void.
#
#   python tools/qa_ghzcut_sky.py <capture.png> [--expect black|sky]
#
# --expect black : assert the region IS the black void (RED-baseline proof on the
#                  pre-fix build; exit 0 iff black).
# --expect sky   : (default) assert the region IS sky-blue (post-fix GREEN).
import sys
from PIL import Image

def sky_stats(path):
    im = Image.open(path).convert("RGB")
    W, H = im.size
    # The capture includes the OS window chrome (~26px title bar top). Sample the
    # SKY: a central band in the upper third of the frame, safely above the grass
    # band (which sits in the bottom ~20%). Fractions are robust to window size.
    x0, x1 = int(W * 0.32), int(W * 0.68)
    y0, y1 = int(H * 0.14), int(H * 0.42)
    px = im.load()
    n = 0
    sr = sg = sb = 0
    for y in range(y0, y1, 2):
        for x in range(x0, x1, 2):
            r, g, b = px[x, y]
            sr += r; sg += g; sb += b; n += 1
    return (sr / n, sg / n, sb / n, n)

def main():
    if len(sys.argv) < 2:
        print("usage: qa_ghzcut_sky.py <capture.png> [--expect black|sky]")
        return 2
    path = sys.argv[1]
    expect = "sky"
    if "--expect" in sys.argv:
        expect = sys.argv[sys.argv.index("--expect") + 1]
    r, g, b, n = sky_stats(path)
    # Classify: "sky" = blue clearly dominant AND not dark. "black" = dark void.
    is_sky = (b > 70) and (b > r + 25) and (b >= g)
    is_black = (r < 40 and g < 40 and b < 40)
    verdict = "SKY" if is_sky else ("BLACK" if is_black else "OTHER")
    print(f"qa_ghzcut_sky: {path}")
    print(f"  sky-region mean RGB = ({r:.0f},{g:.0f},{b:.0f})  n={n}px  -> {verdict}")
    if expect == "black":
        ok = is_black
        print(f"  expect=black (RED-baseline)  -> {'PASS' if ok else 'FAIL'}")
        return 0 if ok else 1
    else:
        ok = is_sky
        print(f"  expect=sky (post-fix GREEN)  -> {'PASS' if ok else 'FAIL'}")
        return 0 if ok else 1

if __name__ == "__main__":
    sys.exit(main())
