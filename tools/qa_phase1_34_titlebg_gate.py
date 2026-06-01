#!/usr/bin/env python3
"""qa_phase1_34_titlebg_gate.py - Phase 1.34 TitleBG entities gate.

Per `CLAUDE.md` §4.7 (RED-firing gate before fix):
  Phase 1.34 ports the decomp TitleBG class -- 9 sprites that overlay the
  RBG0 island backdrop with the Mountain-Top white-cap strips
  (Mountain1/Mountain2), the water-reflection strips
  (Reflection/WaterSparkle), and four WingShine sparkle sprites near the
  emblem. Decomp source: tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c
  (Update + StaticUpdate + Draw + Create).

  Before any port code lands, this gate runs against the CURRENT (pre-port)
  build and is expected to fire RED. Once TitleBG_Tick_All +
  TitleBG_Draw_All are wired into the title direct-draw path the gate goes
  GREEN.

Predicates (any FAIL = RED):

  P1. cd/TITLE3D.ATL exists and exposes anim 0..4 (TitleBG types) with the
      verified per-anim frame counts (1, 1, 1, 1, 1) -- the same atlas
      Phase 1.32 already shipped, so this predicate is a sanity check that
      the asset surface needed by the port is still present.

  P2. Source: src/mania/Objects/Title/TitleBG.c contains all four of:
        * "TitleBG_Tick_All"
        * "TitleBG_Draw_All"
        * "static .*s_titlebg" or a similar entity-table marker
        * The 9-entry placement table (verified positions per Scene1.bin)

      The decomp ports historically were ECS-driven (per-entity Update/
      Draw callbacks); Phase 1.34 follows the Phase 1.32 Title3DSprite
      precedent of a direct-batched call path so the gate captures the
      structural change.

  P3. Pixel-mass band Y=[120, 170] (Mountain-Top strips at world Y=144
      -- per Scene1.bin slots 0..4 -- map to screen Y=144). In 3 of N
      sample captures the band must show >= 3000 mountain-saturated pixels
      that are NOT pure back-color sky-blue. Pre-port baseline (no TitleBG
      sprites): the band is fully back-color blue, so the count is 0.

  P4. Cross-frame motion: between captures k and k+1 the band's saturated
      centroid X must shift across the capture series. Decomp TitleBG_Update
      (line 13-29) slides Mountain1/Mountain2/Reflection/WaterSparkle LEFT
      at 1 px/frame. 3 fps captures spaced by ~20 ticks each => centroid
      should shift >= 4 px between consecutive captures with mass.

Usage:
  python tools/qa_phase1_34_titlebg_gate.py
    --captures qa_phase1_34_titlebg_*.png
    --atl cd/TITLE3D.ATL
    --src src/mania/Objects/Title/TitleBG.c
"""
import argparse
import glob
import os
import struct
import sys

import numpy as np
from PIL import Image


ATL_MAGIC = 0x5453
ATL_VERSION = 0x0004
EXPECT_TITLEBG_ANIM_COUNT = 5  # anims 0..4 inclusive
EXPECT_PER_ANIM_FC = [1, 1, 1, 1, 1]


def predicate_atl_anims(path):
    if not os.path.exists(path):
        return False, f"missing {path}"
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 44 + 5 * 8:
        return False, f"too small ({len(data)} B)"
    magic, ver, anim_count, pal_size = struct.unpack_from(">HHHH", data, 0)
    if magic != ATL_MAGIC or ver != ATL_VERSION:
        return False, f"bad header magic=0x{magic:04X} ver=0x{ver:04X}"
    if anim_count < EXPECT_TITLEBG_ANIM_COUNT:
        return False, f"anim_count {anim_count} < {EXPECT_TITLEBG_ANIM_COUNT}"
    for ai in range(EXPECT_TITLEBG_ANIM_COUNT):
        rec_off = 44 + ai * 8
        fc, _li, _fg, _td = struct.unpack_from(">HHHH", data, rec_off)
        if fc != EXPECT_PER_ANIM_FC[ai]:
            return False, (f"anim {ai} fc={fc} (expected "
                           f"{EXPECT_PER_ANIM_FC[ai]})")
    return True, (f"OK anim_count={anim_count}, anim0..4 fc="
                  f"{EXPECT_PER_ANIM_FC}")


def predicate_source_port_landed(src_path):
    if not os.path.exists(src_path):
        return False, f"missing {src_path}"
    with open(src_path, "rb") as f:
        body = f.read().decode("utf-8", errors="replace")

    needed = [
        ("TitleBG_Tick_All",   "batched tick entry"),
        ("TitleBG_Draw_All",   "batched draw entry"),
        ("s_titlebg",          "static entity table"),
    ]
    missing = [name for (name, _) in needed if name not in body]
    if missing:
        return False, f"missing markers: {missing}"

    # 9-entity coverage: count occurrences of the per-entity-row literal
    # coords (104, 520, 112, 144, 208, 308, 372). The four WingShine
    # entities share Y=64; the five base entities share Y=144 (with
    # X in {104, 112, 520}). We check the WingShine Xs all appear.
    wingshine_xs = ["144", "208", "308", "372"]
    base_xs = ["104", "112", "520"]
    for x in wingshine_xs:
        if x not in body:
            return False, f"WingShine X={x} missing in TitleBG.c table"
    for x in base_xs:
        if x not in body:
            return False, f"Mountain/Reflection X={x} missing in TitleBG.c"

    return True, ("OK Tick_All/Draw_All entries + 9-entity table coords "
                  "present")


def count_mountaintop_pixels(arr):
    """Count pixels in band Y=[120..170] (Mountain Top strip region in
    native 224-row coords) that are saturated -- explicitly NOT sky-blue
    and NOT pure back-color. The Mountain1/Mountain2 strips ship in
    BG.gif as palette indices with greys/whites near the top edge of the
    island.

    Returns (count, (cx, cy)) where (cx, cy) is the centroid of the
    saturated mask. arr is (H, W, 3) uint8 RGB.
    """
    h, w, _ = arr.shape
    y0 = max(0, min(h, int(round(120 * h / 224.0))))
    y1 = max(0, min(h, int(round(170 * h / 224.0))))
    if y0 >= y1:
        return 0, (w // 2, (y0 + y1) // 2)
    band = arr[y0:y1, :, :]
    r = band[..., 0].astype(np.int32)
    g = band[..., 1].astype(np.int32)
    b = band[..., 2].astype(np.int32)
    summ = r + g + b
    # Foreground = NOT sky-blue (back-color of title) AND saturation band
    # 60..600 (skip pure black + pure white extremes are kept open to
    # accommodate the white-cap top strip) AND not magenta-transparency.
    not_sky_blue = ~((b > r + 20) & (b > g + 10) & (b > 80))
    sat_band = (summ >= 60) & (summ <= 720)
    not_magenta = ~((r > 200) & (b > 200) & (g < 100))
    mask = not_sky_blue & sat_band & not_magenta
    count = int(mask.sum())
    if count > 0:
        ys, xs = np.nonzero(mask)
        cx = int(xs.mean())
        cy = int(ys.mean()) + y0
    else:
        cx, cy = w // 2, (y0 + y1) // 2
    return count, (cx, cy)


def predicate_pixel_mass(capture_paths, threshold=3000, min_passing=3):
    if not capture_paths:
        return False, "no captures supplied", []
    measurements = []
    for p in capture_paths:
        try:
            img = Image.open(p).convert("RGB")
        except Exception as ex:
            return False, f"cannot open {p}: {ex}", measurements
        arr = np.array(img, dtype=np.uint8)
        count, centroid = count_mountaintop_pixels(arr)
        measurements.append({"path": p, "count": count, "centroid": centroid})
    passing = sum(1 for m in measurements if m["count"] >= threshold)
    msg = (f"{passing}/{len(measurements)} captures >= {threshold} px "
           f"(counts: {[m['count'] for m in measurements]})")
    return passing >= min_passing, msg, measurements


def predicate_motion(measurements, min_pairs=2, min_dx=4):
    if len(measurements) < 2:
        return False, "need >=2 captures for motion predicate"
    shifts = []
    for i in range(len(measurements) - 1):
        if (measurements[i]["count"] < 1500 or
                measurements[i + 1]["count"] < 1500):
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
    ap.add_argument("--src",
                    default="src/mania/Objects/Title/TitleBG.c")
    ap.add_argument("--captures", nargs="*",
                    default=sorted(glob.glob("qa_phase1_34_titlebg_*.png")))
    ap.add_argument("--threshold", type=int, default=3000)
    ap.add_argument("--min-passing", type=int, default=3)
    args = ap.parse_args()

    print(f"=== Phase 1.34 TitleBG gate ===")
    print(f"atl       = {args.atl}")
    print(f"src       = {args.src}")
    print(f"captures  = {len(args.captures)}")

    failures = []

    ok, msg = predicate_atl_anims(args.atl)
    print(f"  P1 ATL anims 0..4    : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P1 ATL anims")

    ok, msg = predicate_source_port_landed(args.src)
    print(f"  P2 source port       : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P2 source port")

    ok, msg, measurements = predicate_pixel_mass(args.captures,
                                                  args.threshold,
                                                  args.min_passing)
    print(f"  P3 mountain-top mass : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P3 pixel mass")

    ok, msg = predicate_motion(measurements)
    print(f"  P4 left-slide motion : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P4 motion")

    if failures:
        print(f"\nGate RED: {len(failures)} predicate(s) failed: "
              f"{', '.join(failures)}")
        sys.exit(1)
    print(f"\nGate GREEN.")
    sys.exit(0)


if __name__ == "__main__":
    main()
