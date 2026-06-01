#!/usr/bin/env python3
"""
qa_locate.py - Locate a specific on-screen element/sprite/artwork by signature
colour, so QA can verify (autonomously) that it is PRESENT and at the EXPECTED
position - catching "sprite missing", "drawn off-screen", "mispositioned" without
a human eyeballing the frame.

Reports each matching colour cluster's pixel count, bounding box and centroid (in
*game* pixels, i.e. the emulator window's inner 320x224 area, auto-detected by
cropping the window border). Exit code 1 if the element is absent or (with
--expect) lands outside the expected box.

Usage:
  # Is Sonic present, and where? (his signature blue body)
  python tools/qa_locate.py qa.png --color 0,104,240 --tol 60 --min-px 80 --name Sonic
  # Verify Sonic is near screen (60,148):
  python tools/qa_locate.py qa.png --color 0,104,240 --tol 60 --expect 30,90,140,200 --name Sonic
"""
import argparse
import sys

import numpy as np
from PIL import Image


def detect_game_area(a):
    """Crop the Mednafen window chrome to the inner game image (border is a near
    -uniform blue/black frame). Returns (x0,y0,x1,y1) of the active area."""
    h, w, _ = a.shape
    # The play area is large and varied; find the bounding box of "non-border"
    # rows/cols by looking for variance. Simple heuristic: trim a fixed window
    # border (title bar ~28px top, ~8px sides/bottom) then return.
    return (8, 28, w - 8, h - 8)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image")
    ap.add_argument("--color", required=True, help="R,G,B signature colour")
    ap.add_argument("--tol", type=int, default=48, help="per-channel tolerance")
    ap.add_argument("--min-px", type=int, default=40, help="min pixels to count as present")
    ap.add_argument("--name", default="element")
    ap.add_argument("--expect", default=None,
                    help="x0,y0,x1,y1 expected centroid box in GAME px (320x224)")
    args = ap.parse_args()

    tgt = np.array([int(x) for x in args.color.split(",")])
    a = np.asarray(Image.open(args.image).convert("RGB")).astype(int)
    gx0, gy0, gx1, gy1 = detect_game_area(a)
    crop = a[gy0:gy1, gx0:gx1]
    ch, cw, _ = crop.shape
    sx = 320.0 / cw
    sy = 224.0 / ch

    mask = (np.abs(crop - tgt).max(axis=2) <= args.tol)
    n = int(mask.sum())
    if n < args.min_px:
        print(f"FAIL: {args.name} NOT found (only {n} px match {tuple(tgt)} +/-{args.tol}; "
              f"need >= {args.min_px}) -> missing or off-screen")
        sys.exit(1)

    ys, xs = np.nonzero(mask)
    cx_px, cy_px = xs.mean() * sx, ys.mean() * sy
    bx0, bx1 = xs.min() * sx, xs.max() * sx
    by0, by1 = ys.min() * sy, ys.max() * sy
    print(f"{args.name}: {n} px; centroid game=({cx_px:.0f},{cy_px:.0f}); "
          f"bbox game=({bx0:.0f},{by0:.0f})-({bx1:.0f},{by1:.0f})")

    if args.expect:
        ex0, ey0, ex1, ey1 = [int(v) for v in args.expect.split(",")]
        if not (ex0 <= cx_px <= ex1 and ey0 <= cy_px <= ey1):
            print(f"FAIL: {args.name} centroid ({cx_px:.0f},{cy_px:.0f}) outside expected "
                  f"box ({ex0},{ey0})-({ex1},{ey1})")
            sys.exit(1)
        print(f"OK: {args.name} within expected box.")
    sys.exit(0)


if __name__ == "__main__":
    main()
