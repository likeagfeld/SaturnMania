#!/usr/bin/env python3
"""
qa_chain_flicker.py -- #314 punch-list 3 (#302) RED/GREEN gate: periodic black
flicker in the AIZ intro / Green Hill Zone landing legs of the full chain.

Measures the BLACK-FRAME RATIO over a window of frames. A frame is "black" if
the mean luma of its game area (window chrome stripped) is below LUMA_BLACK.
The #302 symptom is whole-frame blackouts at a periodic cadence (~30-50% of
frames in the affected legs), NOT a dark-scene art choice -- the same scenes'
lit frames carry mean luma far above the threshold.

Inputs (either):
  --video FILE --t0 SEC --t1 SEC [--fps 10]   extract frames via ffmpeg (temp)
  FRAME.png [...]                              a burst of capture PNGs

Threshold: --max-black-ratio (default 0.20 GREEN ceiling).
--expect red : RED-baseline proof (exit 0 iff ratio >= --min-red, default 0.30).

Usage:
  python tools/qa_chain_flicker.py --video chain_video.mp4 --t0 430 --t1 465 --expect red
  python tools/qa_chain_flicker.py _burst_*.png
"""
import os
import subprocess
import sys
import tempfile
import numpy as np
from PIL import Image

LUMA_BLACK = 18.0


def frame_luma(path):
    im = np.asarray(Image.open(path).convert("L")).astype(float)
    H, W = im.shape[:2]
    g = im[34:H - 8, 8:W - 8] if H > 60 and W > 32 else im
    return float(g.mean())


def main(argv):
    max_black = 0.20
    min_red = 0.30
    expect = "green"
    video = t0 = t1 = None
    fps = 10
    pngs = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == "--video":
            video = argv[i + 1]; i += 2
        elif a == "--t0":
            t0 = float(argv[i + 1]); i += 2
        elif a == "--t1":
            t1 = float(argv[i + 1]); i += 2
        elif a == "--fps":
            fps = int(argv[i + 1]); i += 2
        elif a == "--max-black-ratio":
            max_black = float(argv[i + 1]); i += 2
        elif a == "--min-red":
            min_red = float(argv[i + 1]); i += 2
        elif a == "--expect":
            expect = argv[i + 1]; i += 2
        else:
            pngs.append(a); i += 1

    tmpd = None
    if video is not None:
        if t0 is None or t1 is None:
            print("--video needs --t0/--t1"); return 2
        tmpd = tempfile.mkdtemp(prefix="qa_flicker_")
        out = os.path.join(tmpd, "f_%05d.png")
        cmd = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-ss", str(t0),
               "-to", str(t1), "-i", video, "-vf", f"fps={fps}", out]
        subprocess.run(cmd, check=True)
        pngs = sorted(os.path.join(tmpd, f) for f in os.listdir(tmpd) if f.endswith(".png"))

    if not pngs:
        print("no frames"); return 2
    lumas = [frame_luma(p) for p in pngs]
    nblack = sum(1 for v in lumas if v < LUMA_BLACK)
    ratio = nblack / len(lumas)
    lit = [v for v in lumas if v >= LUMA_BLACK]
    print(f"qa_chain_flicker: n={len(lumas)} black={nblack} ratio={ratio:.3f} "
          f"(luma<{LUMA_BLACK}); lit-mean={np.mean(lit):.1f}" if lit else
          f"qa_chain_flicker: n={len(lumas)} black={nblack} ratio={ratio:.3f} (ALL black)")
    if tmpd:
        for f in os.listdir(tmpd):
            os.remove(os.path.join(tmpd, f))
        os.rmdir(tmpd)
    if expect == "red":
        ok = ratio >= min_red
        print(f"  expect=red (baseline >= {min_red})  -> {'PASS' if ok else 'FAIL'}")
    else:
        ok = ratio <= max_black
        print(f"  expect=green (ratio <= {max_black})  -> {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
