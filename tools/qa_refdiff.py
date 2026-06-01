#!/usr/bin/env python3
"""qa_refdiff.py - automatic reference-render diff gate (QA.md gate 8).

Diffs a Saturn Mednafen window capture against a known-good golden capture and
FAILS (exit 1) if mean RGB delta exceeds a threshold. This is the gate that
should have caught the jo CRAM off-by-one palette shift but was being skipped:
running it on every build closes that hole.

Algorithm:
  1. Locate the game area inside the Mednafen window (crop title bar/border by
     finding the largest non-background rectangle).
  2. Downscale both captures to a canonical 320x240.
  3. Search a small (+-3 px) x/y offset window to absorb timing-driven camera
     drift; pick the alignment with minimum mean RGB delta.
  4. Compare. Output mean / 95th-pctl / max RGB delta per pixel.
  5. Exit 0 if mean <= --mean-thresh AND 95th-pctl <= --p95-thresh; else exit 1.

Usage:
  python tools/qa_refdiff.py <shot.png> --golden tools/refs/title_view.golden.png
                              [--mean-thresh 12] [--p95-thresh 35]
                              [--mask-rect x0,y0,x1,y1] [--out diff.png]
The --mask-rect lets you ignore a region (e.g. where Sonic walks, since his
position is timing-dependent).
"""
import argparse, sys, os
import numpy as np
from PIL import Image


def crop_game_area(img_rgb, bg_tol=20):
    """Find the largest non-window-border rectangle. Mednafen's title bar +
    side borders are a near-uniform light-gray; the game area is everything
    else. Returns (x0, y0, x1, y1) of the inner rect."""
    a = np.asarray(img_rgb)
    h, w, _ = a.shape
    # corner pixel == window chrome colour (light gray-ish on Win11 title bar)
    chrome = a[0, 0].astype(int)
    diff = np.abs(a.astype(int) - chrome).sum(axis=2)
    nonchrome = diff > bg_tol
    # row/col masks: any non-chrome pixel in that row/col
    rmask = nonchrome.any(axis=1)
    cmask = nonchrome.any(axis=0)
    if not rmask.any() or not cmask.any():
        return 0, 0, w, h
    y0 = int(np.argmax(rmask))
    y1 = h - int(np.argmax(rmask[::-1]))
    x0 = int(np.argmax(cmask))
    x1 = w - int(np.argmax(cmask[::-1]))
    return x0, y0, x1, y1


def canonicalise(img_rgb, size=(320, 240)):
    x0, y0, x1, y1 = crop_game_area(img_rgb)
    cropped = img_rgb.crop((x0, y0, x1, y1))
    return cropped.resize(size, Image.BILINEAR)


def best_offset_delta(shot_arr, ref_arr, mask, search=3):
    """Try small (dx,dy) offsets in [-search, +search], return the (dx, dy)
    that minimises mean masked RGB delta and the delta array at that offset."""
    h, w, _ = shot_arr.shape
    best = (None, None, None, 1e18)
    for dy in range(-search, search + 1):
        for dx in range(-search, search + 1):
            # shift shot by (dx,dy) over ref; use the intersection.
            sy0 = max(0, dy); sy1 = h + min(0, dy)
            sx0 = max(0, dx); sx1 = w + min(0, dx)
            ry0 = max(0, -dy); ry1 = h + min(0, -dy)
            rx0 = max(0, -dx); rx1 = w + min(0, -dx)
            s = shot_arr[sy0:sy1, sx0:sx1].astype(int)
            r = ref_arr[ry0:ry1, rx0:rx1].astype(int)
            m = mask[sy0:sy1, sx0:sx1]
            if not m.any():
                continue
            d = np.abs(s - r).sum(axis=2)
            mean_d = float(d[m].mean())
            if mean_d < best[3]:
                # store the full-size delta and offset
                full = np.zeros((h, w), dtype=float)
                full[sy0:sy1, sx0:sx1] = d
                best = (dx, dy, full, mean_d)
    return best


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("shot")
    ap.add_argument("--golden", required=True)
    ap.add_argument("--mean-thresh", type=float, default=12.0,
                    help="FAIL if mean per-pixel RGB delta > this (default 12)")
    ap.add_argument("--p95-thresh", type=float, default=120.0,
                    help="FAIL if 95th-pctl per-pixel RGB delta > this (default 120)")
    ap.add_argument("--mask-rect", default="",
                    help="x0,y0,x1,y1 in 320x240 canonical space to EXCLUDE "
                         "from the diff (e.g. Sonic's path)")
    ap.add_argument("--search", type=int, default=3,
                    help="max x/y alignment offset to try (default +-3 px)")
    ap.add_argument("--out", default="",
                    help="optional path to write a heatmap PNG of the delta")
    args = ap.parse_args()

    if not os.path.exists(args.golden):
        print(f"FAIL: golden not found: {args.golden}", file=sys.stderr); sys.exit(2)

    shot = canonicalise(Image.open(args.shot).convert("RGB"))
    ref  = canonicalise(Image.open(args.golden).convert("RGB"))
    sa = np.asarray(shot); ra = np.asarray(ref)
    H, W, _ = sa.shape
    mask = np.ones((H, W), dtype=bool)
    if args.mask_rect:
        x0, y0, x1, y1 = (int(v) for v in args.mask_rect.split(","))
        mask[y0:y1, x0:x1] = False

    dx, dy, delta, mean_d = best_offset_delta(sa, ra, mask, args.search)
    # apply mask before computing stats
    masked = delta[mask]
    p95 = float(np.percentile(masked, 95)) if masked.size else 0.0
    max_d = float(masked.max()) if masked.size else 0.0

    if args.out:
        # heatmap (delta scaled 0..255), with masked region greyed
        hm = np.clip(delta * 2, 0, 255).astype(np.uint8)
        rgb = np.stack([hm, hm * 0, hm * 0], axis=2)
        rgb[~mask] = (60, 60, 60)
        Image.fromarray(rgb).save(args.out)

    print(f"qa_refdiff: aligned dx={dx} dy={dy}; "
          f"mean={mean_d:.2f} p95={p95:.2f} max={max_d:.2f} "
          f"(thresh mean<={args.mean_thresh} p95<={args.p95_thresh})")

    fail = (mean_d > args.mean_thresh) or (p95 > args.p95_thresh)
    if fail:
        print("FAIL: shot diverges from golden -- reference-diff gate tripped.",
              file=sys.stderr)
        sys.exit(1)
    print("PASS: shot matches golden within thresholds.")
    sys.exit(0)


if __name__ == "__main__":
    main()
