#!/usr/bin/env python3
"""qa_phase1_32_billboard_gate.py - Phase 1.32 Title3DSprite billboard gate.

Per `CLAUDE.md` §4.7 (RED-firing gate before fix):
  Phase 1.32 ports the decomp Title3DSprite class — 58 software-projected
  billboard sprites (mountain L/M/S + tree + bush) that orbit the title
  island as `TitleBG->angle` increments. Before any port code lands, this
  gate runs against the CURRENT (pre-port) build and is expected to fire
  RED. Once Title3DSprite_Update_All + Title3DSprite_Draw_All are wired
  and TITLE3D.ATL is loaded, the gate goes GREEN.

Predicates (any FAIL = RED):

  P1. cd/TITLE3D.ATL exists, has a valid v4 ATL header (magic 0x5453 'TS',
      version 0x0004, anim_count == 6, palette_size 16, pixel_pool fits
      under VDP1 user area 0x71D38), and the per-anim record for anim 5
      ('Billboard Sprites') reports frame_count == 5.

  P2. At least 3 mednafen 3 fps captures (qa_phase1_32_*.png) show > 800
      "billboard-saturated" pixels in the island region (Y in [128, 200])
      that are NOT just RBG0 backdrop content. Saturated = pixels whose
      RGB is in the {mountain greys + tree green + bush green} bucket,
      i.e. R+G+B sum in [60, 480] AND not a sky-blue (B > R + 30).
      Pre-port baseline: ~0 billboard pixels (only RBG0 backdrop).

  P3. Cross-frame motion: between captures k and k+1 (with k spanning
      stable post-flash window), the centroid X of saturated-pixel cluster
      must shift by >= 1 px in at least 2 frame pairs (evidence the
      rotation transform is being driven by TitleBG->angle++ each frame).
      Static (no-rotation) builds will fail this predicate.

Usage:
  python tools/qa_phase1_32_billboard_gate.py
    --captures qa_phase1_32_billboard_001.png ...
    --atl cd/TITLE3D.ATL
"""
import argparse
import glob
import os
import struct
import sys

import numpy as np
from PIL import Image


ATL_MAGIC = 0x5453      # 'TS' family (matches TSONIC.ATL / ELECTRA.ATL)
ATL_VERSION = 0x0004
EXPECT_ANIM_COUNT = 6
EXPECT_BILLBOARD_ANIM_INDEX = 5
EXPECT_BILLBOARD_FRAMES = 5
VDP1_USER_AREA = 0x71D38


def predicate_atl_well_formed(path):
    if not os.path.exists(path):
        return False, f"cd/TITLE3D.ATL missing at {path}"
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 44:
        return False, f"TITLE3D.ATL too small ({len(data)} B)"
    magic, ver, anim_count, pal_size = struct.unpack_from(">HHHH", data, 0)
    if magic != ATL_MAGIC:
        return False, f"wrong magic 0x{magic:04X} (expected 0x{ATL_MAGIC:04X})"
    if ver != ATL_VERSION:
        return False, f"wrong version 0x{ver:04X} (expected 0x{ATL_VERSION:04X})"
    if anim_count != EXPECT_ANIM_COUNT:
        return False, f"anim_count {anim_count} (expected {EXPECT_ANIM_COUNT})"
    if pal_size != 16:
        return False, f"pal_size {pal_size} (expected 16)"
    total_pix = struct.unpack_from(">I", data, 8 + 16 * 2)[0]
    if total_pix >= VDP1_USER_AREA:
        return False, (f"pixel_pool {total_pix} >= VDP1 user area "
                       f"0x{VDP1_USER_AREA:X}")
    # Per-anim records start at offset 44 (header) = 8 B each.
    anim5_off = 44 + EXPECT_BILLBOARD_ANIM_INDEX * 8
    if anim5_off + 8 > len(data):
        return False, "file too short for anim 5 record"
    fc, _li, _fg, _td = struct.unpack_from(">HHHH", data, anim5_off)
    if fc != EXPECT_BILLBOARD_FRAMES:
        return False, (f"anim 5 frame_count {fc} "
                       f"(expected {EXPECT_BILLBOARD_FRAMES})")
    return True, (f"OK magic=0x{magic:04X} ver={ver} anim_count={anim_count} "
                  f"pool={total_pix}B anim5_fc={fc}")


def count_billboard_pixels(arr):
    """Count pixels in island region [Y 128..200] that look like billboard
    foreground (mountain grey/green/brown, NOT sky-blue).

    arr: (H, W, 3) uint8 RGB array.
    """
    h, w, _ = arr.shape
    # Y band [128..200] in 224-row native; scale to capture height (mednafen
    # captures at 749 px / 224 rows = 3.34x). Apply proportionally.
    y0 = max(0, min(h, int(round(128 * h / 224.0))))
    y1 = max(0, min(h, int(round(200 * h / 224.0))))
    if y0 >= y1:
        return 0, (w // 2, (y0 + y1) // 2)
    band = arr[y0:y1, :, :]
    r = band[..., 0].astype(np.int32)
    g = band[..., 1].astype(np.int32)
    b = band[..., 2].astype(np.int32)
    summ = r + g + b
    # Foreground mask: not sky-blue + saturation band 60..480 (skip pure
    # black + pure white) + not magenta transparent.
    not_sky = b <= (r + 30)
    sat_band = (summ >= 60) & (summ <= 480)
    not_magenta = ~((r > 200) & (b > 200) & (g < 100))
    mask = not_sky & sat_band & not_magenta
    count = int(mask.sum())
    if count > 0:
        ys, xs = np.nonzero(mask)
        cx = int(xs.mean())
        cy = int(ys.mean()) + y0
    else:
        cx, cy = w // 2, (y0 + y1) // 2
    return count, (cx, cy)


def predicate_captures(capture_paths, threshold=800, min_passing=3):
    if not capture_paths:
        return False, "no captures supplied", []
    measurements = []
    for p in capture_paths:
        try:
            img = Image.open(p).convert("RGB")
        except Exception as ex:
            return False, f"cannot open {p}: {ex}", measurements
        arr = np.array(img, dtype=np.uint8)
        count, centroid = count_billboard_pixels(arr)
        measurements.append({"path": p, "count": count, "centroid": centroid})
    passing = sum(1 for m in measurements if m["count"] >= threshold)
    msg = (f"{passing}/{len(measurements)} captures >= {threshold} px "
           f"(counts: {[m['count'] for m in measurements]})")
    return passing >= min_passing, msg, measurements


def predicate_rotation(measurements, min_pairs=2, min_dx=1):
    if len(measurements) < 2:
        return False, "need >=2 captures for rotation predicate"
    shifts = []
    for i in range(len(measurements) - 1):
        if measurements[i]["count"] < 200 or measurements[i + 1]["count"] < 200:
            continue
        dx = abs(measurements[i + 1]["centroid"][0]
                 - measurements[i]["centroid"][0])
        shifts.append(dx)
    passing = sum(1 for d in shifts if d >= min_dx)
    msg = f"{passing}/{len(shifts)} pairs shift >= {min_dx}px (shifts={shifts})"
    return passing >= min_pairs, msg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--atl", default="cd/TITLE3D.ATL")
    ap.add_argument("--captures", nargs="*",
                    default=sorted(glob.glob("qa_phase1_32_billboard_*.png")))
    ap.add_argument("--threshold", type=int, default=800)
    ap.add_argument("--min-passing", type=int, default=3)
    args = ap.parse_args()

    print(f"=== Phase 1.32 Title3DSprite billboard gate ===")
    print(f"atl       = {args.atl}")
    print(f"captures  = {len(args.captures)}")

    failures = []

    ok, msg = predicate_atl_well_formed(args.atl)
    print(f"  P1 ATL header        : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P1 ATL header")

    ok, msg, measurements = predicate_captures(args.captures,
                                                args.threshold,
                                                args.min_passing)
    print(f"  P2 billboard pixels  : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P2 billboard pixels")

    ok, msg = predicate_rotation(measurements)
    print(f"  P3 rotation centroid : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P3 rotation centroid")

    if failures:
        print(f"\nGate RED: {len(failures)} predicate(s) failed: "
              f"{', '.join(failures)}")
        sys.exit(1)
    print(f"\nGate GREEN.")
    sys.exit(0)


if __name__ == "__main__":
    main()
