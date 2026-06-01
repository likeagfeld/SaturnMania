#!/usr/bin/env python3
"""
qa_screencheck.py - Automated correctness check of an emulator screenshot, so
palette/color bugs are caught WITHOUT relying on eyeballing "looks about right".

Two checks:
 1. PLACEHOLDER/GARBAGE colours: pure magenta (255,0,255) is RSDK's transparency
    placeholder and must NEVER appear on screen. Its presence == a palette bug
    (wrong palette source, GIF-GCT seeding, un-overridden rows). This single check
    would have caught the GHZ-ground magenta bug.
 2. REFERENCE COLOUR MATCH: compare the screenshot's colour content against a
    known-correct PC reference render (produced by the offline converters). If the
    screenshot is missing the reference's dominant colours (e.g. Sonic's red shoes)
    or its colour histogram diverges past a threshold, FAIL. This catches subtle
    wrong-palette bugs (Sonic rendered with the GHZ palette -> no red).

Crop the screenshot to the emulator content area first (--crop), since captures
include the window title bar / borders.

Usage:
    python qa_screencheck.py shot.png --crop 8,30,8,8
    python qa_screencheck.py shot.png --crop 8,30,8,8 --ref ghz_render/ref.png \
        --require-colors 224,0,0 0,0,224
"""
import argparse
import sys

import numpy as np
from PIL import Image

MAGENTA = np.array([255, 0, 255])


def load(path, crop):
    im = Image.open(path).convert("RGB")
    if crop:
        l, t, r, b = crop
        w, h = im.size
        im = im.crop((l, t, w - r, h - b))
    return np.asarray(im, np.int16)


def pct_near(arr, color, tol=24):
    d = np.abs(arr - np.array(color)).sum(axis=2)
    return float((d <= tol).mean()) * 100.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("shot")
    ap.add_argument("--crop", default="8,30,8,8",
                    help="left,top,right,bottom px to trim (window chrome)")
    ap.add_argument("--ref", help="known-correct reference PNG")
    ap.add_argument("--require-colors", nargs="*", default=[],
                    help="R,G,B colours that MUST be present (e.g. Sonic red shoes)")
    ap.add_argument("--max-magenta", type=float, default=0.05,
                    help="max %% magenta pixels allowed")
    args = ap.parse_args()

    crop = [int(x) for x in args.crop.split(",")] if args.crop else None
    shot = load(args.shot, crop)
    fails = []

    # 1. magenta / placeholder check
    mag = pct_near(shot, MAGENTA, tol=40)
    print(f"magenta pixels: {mag:.3f}%  (limit {args.max_magenta}%)")
    if mag > args.max_magenta:
        fails.append(f"MAGENTA placeholder on screen ({mag:.2f}%) -> palette bug")

    # 2. required colours present
    for c in args.require_colors:
        rgb = [int(v) for v in c.split(",")]
        p = pct_near(shot, rgb, tol=40)
        print(f"required colour {rgb}: {p:.3f}%")
        if p < 0.02:
            fails.append(f"required colour {rgb} missing ({p:.3f}%) -> wrong palette")

    # 3. reference colour-histogram comparison
    if args.ref:
        ref = load(args.ref, None)
        def hist(a):
            q = (a // 32).reshape(-1, 3)            # 8 levels/channel
            idx = q[:, 0] * 64 + q[:, 1] * 8 + q[:, 2]
            h = np.bincount(idx, minlength=512).astype(float)
            return h / max(h.sum(), 1)
        hs, hr = hist(shot), hist(ref)
        inter = np.minimum(hs, hr).sum()            # histogram intersection 0..1
        print(f"reference histogram match: {inter*100:.1f}%")
        if inter < 0.45:
            fails.append(f"colour content diverges from reference "
                         f"({inter*100:.1f}% match) -> wrong palette/render")

    if fails:
        print("\nQA SCREENCHECK: FAIL")
        for f in fails:
            print("  - " + f)
        sys.exit(1)
    print("\nQA SCREENCHECK: PASS")


if __name__ == "__main__":
    main()
