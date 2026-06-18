#!/usr/bin/env python3
# =============================================================================
# qa_camera_local_pool.py -- sizing measurement for the UNIFIED S1 + s7.3
# camera-local entity pool (WHOLE_GAME_MASSPORT_PLAN.md S1 / s7.3).
#
# Decision (user, 2026-06-17): solve BOTH the entity-stride wall (S1: 17
# scene-placed classes > the 556 wide tier) AND the dense-act DROP wall (s7.3:
# 15 scenes place > the 1088 scene region + 64 temp) with ONE mechanism --
# instantiate only camera-NEAR entities into a modest pool that includes an
# x-wide tier, streaming the rest in/out as the camera moves. The pool must hold
# the PEAK simultaneously-near population, NOT all placed entities. This tool
# measures that peak offline so the pool is sized from data, not a guess.
#
# Method: for each scene, take every placed entity's pixel position (scene_census
# _coords; x,y already >>16). Slide a camera-active box over the scene and report
# the PEAK simultaneous count within it -- total, over-344 ("WIDE+"), and over-556
# ("X-WIDE"). The box centred on each entity is the density proxy (O(n^2), exact
# enough for sizing; add headroom in the pool).
#
# Active box (RSDKv5 proxy): Mania screen 424x240 + per-object updateRange margin
# (default ~0x800000 = 128 px each side; Object.cpp AddEntity active-bounds test).
# Default box 424+256 = 680 wide x 240+256 = 496 tall. NOTE: a TRUE figure needs
# per-object updateRange + activeType (ACTIVE_ALWAYS objects are live regardless
# of camera) -- this proxy OVER-counts camera-gated objects and UNDER-counts any
# ACTIVE_ALWAYS ones; treat the result as the camera-gated sizing basis and add
# headroom. Run with --win W H to sensitivity-test.
#
# Output: the global peaks SIZE the unified pool (total slots; x-wide slots).
# =============================================================================
import json
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
CENSUS = os.path.join(ROOT, "docs", "scene_census.json")
DROPIN = os.path.join(ROOT, "docs", "dropin_census.json")


def over344_sizes():
    d = json.load(open(DROPIN, "r", encoding="utf-8"))
    out = {}

    def walk(o):
        if isinstance(o, dict):
            s = o.get("struct_over_344")
            if isinstance(s, dict):
                for k, v in s.items():
                    try:
                        out[k] = max(out.get(k, 0), int(v))
                    except (TypeError, ValueError):
                        pass
            for v in o.values():
                walk(v)
        elif isinstance(o, list):
            for v in o:
                walk(v)

    walk(d)
    return out


def peak_in_box(pts, W, H):
    """Max entities within a W x H box, using the box-centred-on-each-point
    proxy. pts = list of (x, y, tier) where tier in {0 narrow,1 wide,2 xwide}.
    Returns (peak_total, peak_wide_plus, peak_xwide)."""
    hw, hh = W / 2.0, H / 2.0
    n = len(pts)
    best_t = best_w = best_x = 0
    # sort by x so we can break early on the x-window
    pts = sorted(pts, key=lambda p: p[0])
    xs = [p[0] for p in pts]
    for i in range(n):
        cx, cy, _ = pts[i]
        t = w = x = 0
        # only j within x-window can count; xs sorted -> bounded scan
        j = i
        while j >= 0 and cx - xs[j] <= hw:
            j -= 1
        j += 1
        k = i
        while k < n and xs[k] - cx <= hw:
            k += 1
        for m in range(j, k):
            if abs(pts[m][1] - cy) <= hh:
                t += 1
                if pts[m][2] >= 1:
                    w += 1
                if pts[m][2] >= 2:
                    x += 1
        if t > best_t:
            best_t = t
        if w > best_w:
            best_w = w
        if x > best_x:
            best_x = x
    return best_t, best_w, best_x


def main():
    W, H = 680, 496
    args = sys.argv[1:]
    if "--win" in args:
        i = args.index("--win")
        W, H = int(args[i + 1]), int(args[i + 2])

    sizes = over344_sizes()
    census = json.load(open(CENSUS, "r", encoding="utf-8"))
    scenes = census.get("scenes", {})

    rows = []
    for key, s in sorted(scenes.items()):
        coords = s.get("_coords")
        if not coords:
            continue
        pts = []
        for cls, lst in coords.items():
            sz = sizes.get(cls, 0)
            tier = 2 if sz > 556 else (1 if sz > 344 else 0)
            for (_slot, x, y) in lst:
                pts.append((x, y, tier))
        if not pts:
            continue
        pt, pw, px = peak_in_box(pts, W, H)
        rows.append((key, len(pts), pt, pw, px))

    gt = max(rows, key=lambda r: r[2])
    gw = max(rows, key=lambda r: r[3])
    gx = max(rows, key=lambda r: r[4])

    print("=== qa_camera_local_pool :: unified S1 + s7.3 pool sizing ===")
    print("active box %dx%d px (RSDKv5 camera+updateRange proxy); %d scenes" % (W, H, len(rows)))
    print("PEAK simultaneous camera-local entities (sizes the pool):")
    print("  total   = %4d   (%s, of %d placed)" % (gt[2], gt[0], gt[1]))
    print("  WIDE+   = %4d   (%s)  [classes >344]" % (gw[3], gw[0]))
    print("  X-WIDE  = %4d   (%s)  [classes >556 -- the new tier]" % (gx[4], gx[0]))

    print("\n-- top 12 scenes by peak total camera-local --")
    print("  %-22s %7s %7s %6s %6s" % ("scene", "placed", "pk_tot", "pk_W+", "pk_XW"))
    for key, n, pt, pw, px in sorted(rows, key=lambda r: -r[2])[:12]:
        print("  %-22s %7d %7d %6d %6d" % (key, n, pt, pw, px))

    print("\nInterpretation: a camera-local pool of ~%d total slots (+headroom)" % gt[2])
    print("with ~%d x-wide(1088B) slots covers every scene's near-population;" % gx[4])
    print("vs the resident 1216-slot table. This is the unified S1 + s7.3 sizing basis.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
