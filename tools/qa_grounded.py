#!/usr/bin/env python3
"""qa_grounded.py - assert the player sprite is GROUNDED (feet on visible
ground), not floating in mid-air. Detects the bug class I shipped twice:
qa_locate says Sonic is on-screen, motion says the game is animating, but a
visual inspection shows him hovering above the grass.

How:
  1. Locate the player (signature blue body) in the screenshot, take the
     bottom edge of that blob as the player's FEET-row in window pixels.
  2. Scan straight DOWN from the feet for the first GROUND pixel column.
     "Ground" = a green grass top OR brown dirt -- the visible playfield
     palette colours (anything that isn't sky, water reflection, or
     decoration). For GHZ:
        grass green:   (R<160, G>120, B<120)
        dirt brown:    (R>120, G<120, B<140)  (also catches orange dirt)
  3. The vertical gap between feet-row and ground-row in window pixels is
     scaled back to the game's 240-px height. PASS if gap <= --max-gap-px.

This is the gate that should have caught the 67-px Sonic float I shipped.

Usage:
    python tools/qa_grounded.py <shot.png> [--max-gap-px 4]
                                [--player-rgb 0,104,240] [--tol 22]
"""
import argparse, sys
import numpy as np
from PIL import Image


def crop_game(img):
    a = np.asarray(img)
    chrome = a[0, 0].astype(int)
    diff = np.abs(a.astype(int) - chrome).sum(axis=2)
    nz = diff > 20
    if not nz.any():
        return img
    rmask = nz.any(axis=1); cmask = nz.any(axis=0)
    y0 = int(np.argmax(rmask));        y1 = nz.shape[0] - int(np.argmax(rmask[::-1]))
    x0 = int(np.argmax(cmask));        x1 = nz.shape[1] - int(np.argmax(cmask[::-1]))
    return img.crop((x0, y0, x1, y1))


def find_player_feet(arr, rgb, tol):
    R, G, B = rgb
    m = (np.abs(arr[:, :, 0].astype(int) - R) <= tol) & \
        (np.abs(arr[:, :, 1].astype(int) - G) <= tol) & \
        (np.abs(arr[:, :, 2].astype(int) - B) <= tol)
    ys, xs = np.where(m)
    if ys.size < 25:
        return None
    # Player's blue body is on his torso; the visible feet/shoes are RED. Take
    # the bottom of the blue mass and add a small constant for the shoe pixels.
    feet_y = int(ys.max()) + 4
    feet_x = int(np.median(xs))
    return feet_y, feet_x


def find_ground_below(arr, feet_x, start_y):
    """From (start_y, feet_x), scan straight down for the FIRST pixel that
    looks like ground (green grass top or brown/orange dirt). Returns the
    window-y row of that pixel, or None if none found.

    Ground colour thresholds widened for the real Mania GHZ palette
    (sampled from gameplay capture): grass renders darker than the
    placeholder (G ~85-110 vs G > 120), dirt is more red-saturated
    (R ~140-200, G < 110, B < 100)."""
    h = arr.shape[0]
    # widen the column scan slightly to be robust to anti-aliasing
    x0 = max(feet_x - 3, 0); x1 = min(feet_x + 4, arr.shape[1])
    for y in range(start_y, h):
        row = arr[y, x0:x1].astype(int)
        R, G, B = row[:, 0], row[:, 1], row[:, 2]
        # Mania grass: greenish (G > R + 20) and (G > B - 20), G in [60..200]
        grass = (G > R + 15) & (G > B - 30) & (G > 60) & (G < 210)
        # Mania dirt/orange-brown: R > G > B and R > 100
        dirt  = (R > 90) & (R > G + 15) & (G > B) & (B < 120)
        if grass.any() or dirt.any():
            return y
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("shot")
    # Default tuned for real Mania Sonic body palette (Phase 1 swap).
    # Sampled directly from a real-gameplay capture: the dominant body
    # fill renders as (24, 32, 184) on Mednafen's 912x749 windowed
    # framebuffer. Tol must be tight (<= 22) to avoid the sky/water/
    # decoration cyan-blues which span a wide R+G range and flood the
    # match set when the tolerance is loose.
    ap.add_argument("--player-rgb", default="24,32,184")
    ap.add_argument("--tol", type=int, default=20)
    ap.add_argument("--max-gap-px", type=int, default=4,
                    help="max allowed gap (in game px) between feet and ground")
    args = ap.parse_args()

    img = crop_game(Image.open(args.shot).convert("RGB"))
    arr = np.asarray(img)
    rgb = tuple(int(v) for v in args.player_rgb.split(","))
    feet = find_player_feet(arr, rgb, args.tol)
    if not feet:
        print(f"FAIL: player not found in {args.shot}")
        sys.exit(1)
    feet_y, feet_x = feet
    ground_y = find_ground_below(arr, feet_x, feet_y + 1)
    if ground_y is None:
        print(f"FAIL: no ground detected below player at col {feet_x}")
        sys.exit(1)
    # Convert window-pixel gap to game pixels (game is 240 rows tall, scaled).
    win_gap = ground_y - feet_y
    game_gap = int(round(win_gap * 240.0 / arr.shape[0]))
    print(f"qa_grounded: feet_win_y={feet_y} ground_win_y={ground_y} "
          f"gap={win_gap}px ({game_gap}px game-space)")
    if game_gap > args.max_gap_px:
        print(f"FAIL: player floating -- gap {game_gap}px > "
              f"{args.max_gap_px}px tolerance.", file=sys.stderr)
        sys.exit(1)
    print("PASS: player grounded.")
    sys.exit(0)


if __name__ == "__main__":
    main()
