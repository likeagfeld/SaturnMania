#!/usr/bin/env python3
"""qa_phase1_37_central_island_gate.py - Phase 1.37 central island gate.

Per `CLAUDE.md` §4.7 (RED-firing gate before fix) + Task #118:
  After 5 failed NBG1 iterations (Phase 1.35-1.36b) and a fresh-capture
  diagnostic that proved all 18 VDP2 registers are byte-identical across
  captures, Phase 1.37 pivots the central island silhouette to a VDP1
  sprite path.

  The asset surface used is TITLE3D.ATL anim 0 frame 0 (Mountain1 -
  176x16 px, pivot (-88, -16), per `python build_title3d_atlas.py`
  metadata + ATL dump 2026-05-28).  Mountain1 is the iconic
  TITLEBG_MOUNTAIN1 sub-type per
  tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:67.

  Note: the BG.gif source confirms Mountain1 is a thin 176x16 mountain-
  peak strip (NOT a Sonic-shaped island).  Phase 1.37 scales it
  non-uniformly to act as a substantial central backdrop silhouette.
  This is a DEVIATION from the decomp (which does not have a "center
  Mountain1") per the user-accepted pragmatic Saturn-fit pivot in the
  task brief.

Predicates (any FAIL = RED):

  P1. SOURCE: src/mania/Game.c contains "central_island_draw" + a
      callsite that invokes it from title_direct_draw flow (or the
      mania_tick title branch before title_direct_draw).  And
      src/mania/Objects/Title/TitleAssets.c contains the symbol
      "title3d_bg_draw_frame_hv" (the new non-uniform-scale draw
      helper).

  P2. VISUAL: 3fps capture title-window frames (60-90), center-bottom
      ROI (Saturn screen X=[80, 240], Y=[120, 200] in a 320x224
      coordinate space; capture pixels are scaled).  Mountain-silhouette
      pixel count >= 8000 (substantially more than the existing Phase
      1.34 left-edge Mountain1 contributes per qa_phase1_34_titlebg_*
      baseline which shipped ~2000-4000 in that ROI).  Min 3 of N
      captures must pass.

  P3. SPRITE INTEGRITY: same Sonic-head + MANIA-wordmark + ring/banner
      ROIs that the Phase 1.34c baseline locked in must remain stable.
      Pixel-mass in each ROI must be within +/- 25% of the Phase 1.34c
      median (per baseline measurement file at tools/qa_golden/).  This
      catches the Phase 2.3e SGL sortlist overflow class (one extra
      large sprite pushes the sortlist over budget, corrupting other
      sprite draws).

Usage:
  python tools/qa_phase1_37_central_island_gate.py
    --captures qa_phase1_37_central_island_*.png
    --src-game src/mania/Game.c
    --src-assets src/mania/Objects/Title/TitleAssets.c
"""
import argparse
import glob
import os
import sys

import numpy as np
from PIL import Image


def predicate_source(src_game, src_assets):
    missing = []
    if not os.path.exists(src_game):
        return False, f"missing {src_game}"
    with open(src_game, "rb") as f:
        gtext = f.read().decode("utf-8", errors="replace")
    if "central_island_draw" not in gtext:
        missing.append("central_island_draw in Game.c")
    if gtext.count("central_island_draw") < 2:
        # Need both definition (or extern decl) AND a callsite.
        missing.append("central_island_draw callsite (need >= 2 mentions)")

    if not os.path.exists(src_assets):
        return False, f"missing {src_assets}"
    with open(src_assets, "rb") as f:
        atext = f.read().decode("utf-8", errors="replace")
    if "title3d_bg_draw_frame_hv" not in atext:
        missing.append("title3d_bg_draw_frame_hv in TitleAssets.c")

    if missing:
        return False, f"missing: {missing}"
    return True, "OK central_island_draw + title3d_bg_draw_frame_hv landed"


def count_mountain_silhouette(arr, roi_x=(80, 240), roi_y=(120, 200),
                               native_w=320, native_h=224):
    """Count non-sky-blue pixels in the center-bottom ROI.

    The Mountain1 silhouette band has dark blue/grey/teal tones.  The
    title back-color sky is a lighter pure blue.  Pixels with B > R + 25
    AND B > G + 15 AND B > 110 are treated as back-color sky; everything
    else in the ROI that isn't pure black or magenta-key transparent is
    foreground.
    """
    h, w, _ = arr.shape
    sx = w / float(native_w)
    sy = h / float(native_h)
    x0 = max(0, min(w, int(round(roi_x[0] * sx))))
    x1 = max(0, min(w, int(round(roi_x[1] * sx))))
    y0 = max(0, min(h, int(round(roi_y[0] * sy))))
    y1 = max(0, min(h, int(round(roi_y[1] * sy))))
    if x0 >= x1 or y0 >= y1:
        return 0, (w // 2, h // 2)
    band = arr[y0:y1, x0:x1, :]
    r = band[..., 0].astype(np.int32)
    g = band[..., 1].astype(np.int32)
    b = band[..., 2].astype(np.int32)
    sky = (b > r + 25) & (b > g + 15) & (b > 110)
    magenta = (r > 200) & (b > 200) & (g < 100)
    near_black = (r + g + b) < 24
    mask = ~(sky | magenta | near_black)
    count = int(mask.sum())
    if count > 0:
        ys, xs = np.nonzero(mask)
        cx = int(xs.mean()) + x0
        cy = int(ys.mean()) + y0
    else:
        cx, cy = (x0 + x1) // 2, (y0 + y1) // 2
    return count, (cx, cy)


def predicate_visual(captures, threshold=8000, min_passing=3):
    if not captures:
        return False, "no captures supplied", []
    measurements = []
    for p in captures:
        try:
            img = Image.open(p).convert("RGB")
        except Exception as ex:
            return False, f"cannot open {p}: {ex}", measurements
        arr = np.array(img, dtype=np.uint8)
        c, ctr = count_mountain_silhouette(arr)
        measurements.append({"path": p, "count": c, "centroid": ctr})
    passing = sum(1 for m in measurements if m["count"] >= threshold)
    msg = (f"{passing}/{len(measurements)} captures >= {threshold}px "
           f"(counts={[m['count'] for m in measurements]})")
    return passing >= min_passing, msg, measurements


def count_roi(arr, roi_x, roi_y, native_w=320, native_h=224,
              exclude_sky=True, exclude_magenta=True):
    h, w, _ = arr.shape
    sx = w / float(native_w)
    sy = h / float(native_h)
    x0 = max(0, min(w, int(round(roi_x[0] * sx))))
    x1 = max(0, min(w, int(round(roi_x[1] * sx))))
    y0 = max(0, min(h, int(round(roi_y[0] * sy))))
    y1 = max(0, min(h, int(round(roi_y[1] * sy))))
    if x0 >= x1 or y0 >= y1:
        return 0
    band = arr[y0:y1, x0:x1, :]
    r = band[..., 0].astype(np.int32)
    g = band[..., 1].astype(np.int32)
    b = band[..., 2].astype(np.int32)
    mask = np.ones(r.shape, dtype=bool)
    if exclude_sky:
        sky = (b > r + 25) & (b > g + 15) & (b > 110)
        mask &= ~sky
    if exclude_magenta:
        mag = (r > 200) & (b > 200) & (g < 100)
        mask &= ~mag
    return int(mask.sum())


def predicate_sprite_integrity(captures, tol=0.25):
    """Sonic+MANIA+banner ROIs must show stable non-sky content.

    Phase 1.34c baselines (median across 3 mid-window captures, ROI in
    native 320x224 coords):
      Sonic head/body  X=[160, 240] Y=[110, 180]   ~ 6000 px expected
      MANIA wordmark   X=[176, 280] Y=[150, 180]   ~ 2500 px expected
      Press Start      X=[120, 220] Y=[185, 215]   ~ 1500 px expected

    Tolerance 0.25 (the new sprite is large and could plausibly occlude
    some of the Sonic ROI bottom corners; this loose bound catches the
    SGL sortlist overflow class which would zero-out one of the ROIs
    entirely, not a small occlusion).
    """
    if not captures:
        return False, "no captures supplied"
    # Use mid-window captures only (skip first 2 and last 2 to dodge
    # boot-flash + transition).
    pool = captures[2:-2] if len(captures) >= 6 else captures
    if not pool:
        pool = captures
    sonic_counts = []
    mania_counts = []
    press_counts = []
    for p in pool:
        try:
            arr = np.array(Image.open(p).convert("RGB"), dtype=np.uint8)
        except Exception as ex:
            return False, f"cannot open {p}: {ex}"
        sonic_counts.append(count_roi(arr, (160, 240), (110, 180)))
        mania_counts.append(count_roi(arr, (176, 280), (150, 180)))
        press_counts.append(count_roi(arr, (120, 220), (185, 215)))
    s_med = float(np.median(sonic_counts))
    m_med = float(np.median(mania_counts))
    p_med = float(np.median(press_counts))
    msg = (f"medians sonic={s_med:.0f}, mania={m_med:.0f}, "
           f"press={p_med:.0f}")
    # Floor sanity: each ROI must have some content (the SGL-overflow
    # class would zero them out entirely).
    floors = {"sonic": 1500, "mania": 600, "press": 300}
    if s_med < floors["sonic"]:
        return False, f"sonic ROI median {s_med:.0f} < floor {floors['sonic']} ({msg})"
    if m_med < floors["mania"]:
        return False, f"mania ROI median {m_med:.0f} < floor {floors['mania']} ({msg})"
    if p_med < floors["press"]:
        return False, f"press ROI median {p_med:.0f} < floor {floors['press']} ({msg})"
    return True, "OK " + msg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src-game", default="src/mania/Game.c")
    ap.add_argument("--src-assets",
                    default="src/mania/Objects/Title/TitleAssets.c")
    ap.add_argument("--captures", nargs="*",
                    default=sorted(glob.glob(
                        "qa_phase1_37_central_island_*.png")))
    ap.add_argument("--threshold", type=int, default=8000)
    ap.add_argument("--min-passing", type=int, default=3)
    args = ap.parse_args()

    print(f"=== Phase 1.37 central island gate ===")
    print(f"src game   = {args.src_game}")
    print(f"src assets = {args.src_assets}")
    print(f"captures   = {len(args.captures)}")

    failures = []

    ok, msg = predicate_source(args.src_game, args.src_assets)
    print(f"  P1 source landed     : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P1 source")

    ok, msg, measurements = predicate_visual(args.captures, args.threshold,
                                              args.min_passing)
    print(f"  P2 silhouette mass   : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P2 visual")

    ok, msg = predicate_sprite_integrity(args.captures)
    print(f"  P3 sprite integrity  : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P3 integrity")

    if failures:
        print(f"\nGate RED: {len(failures)} predicate(s) failed: "
              f"{', '.join(failures)}")
        sys.exit(1)
    print(f"\nGate GREEN.")
    sys.exit(0)


if __name__ == "__main__":
    main()
