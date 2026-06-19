#!/usr/bin/env python3
# =============================================================================
# qa_p6_pool_window.py -- camera-local pool SIZING PROOF, WHOLE-GAME, OFFLINE (no build).
#
# The authoritative sizing gate for the I3b pool shrink. It answers, with the REAL Scene.bin
# placements for ALL 94 scenes (docs/scene_census.json _coords), the only question the shrink's
# SCENE_PHYS rests on: at the engine's actual camera cull window (P6_SCAN_WINDOW, parsed LIVE from
# p6_io_main.cpp), what is the worst-case count of scene entities simultaneously inside a
# 2*WINDOW-wide slice anywhere in any scene? That count is the camera-near demand the physical
# scene-slot pool must hold; if SCENE_PHYS (P6_POOL_SCENE_PHYS, parsed LIVE from Object.hpp) is
# below it, the shrink would silently DROP near entities in a dense section.
#
# This SUPERSEDES the runtime autorun probe (qa_p6_pool_demand): the autorun only traverses GHZ1's
# sparse opening (measured peak 52), so it UNDER-reports -- the offline slide-window over the true
# placements caught that GHZ1's dense section is 203 and the whole-game worst (TMZ1) is 527, which
# is why SCENE_PHYS was corrected 160 -> 640. Data-driven, no-assume, no emulator.
#
#   S1 constants parsed     SCENE_PHYS + WINDOW both read from live source
#   S2 sized to ceiling     SCENE_PHYS >= whole-game worst-case near demand (+ small always margin)
#
# RED at SCENE_PHYS=160 (32 scenes over); GREEN at 640. Run on every change to the pool geometry
# or P6_SCAN_WINDOW (a wider window raises the demand; a smaller SCENE_PHYS lowers the ceiling).
#
#   python tools/_portspike/qa_p6_pool_window.py
# =============================================================================
import json, os, re, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
ALWAYS_MARGIN = 24  # the non-x-cullable always-iterate scene set adds a handful (GHZ1=4); headroom for it


def _read(p):
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def _define(text, name):
    m = re.search(r"#define\s+%s\s+\(?\s*(0x[0-9A-Fa-f]+|\d+)" % re.escape(name), text)
    return int(m.group(1), 0) if m else None


def max_in_window(coords, half):
    xs = sorted(p[1] for plist in coords.values() for p in plist if p)
    if not xs:
        return 0
    best = j = 0
    for i in range(len(xs)):
        while xs[i] - xs[j] > 2 * half:
            j += 1
        best = max(best, i - j + 1)
    return best


def main():
    objhpp = _read(os.path.join(ROOT, "rsdkv5-src/RSDKv5/RSDK/Scene/Object.hpp"))
    iomain = _read(os.path.join(ROOT, "tools/_portspike/_p6/p6_io_main.cpp"))
    SCENE_PHYS = _define(objhpp, "P6_POOL_SCENE_PHYS")
    WINDOW = _define(iomain, "P6_SCAN_WINDOW")
    sc = json.load(open(os.path.join(ROOT, "docs/scene_census.json")))["scenes"]

    print("=== qa_p6_pool_window (WHOLE-GAME camera-local pool SIZING PROOF, offline) ===")
    print("  P6_POOL_SCENE_PHYS = %s   (physical scene slots the shrink seats)" % SCENE_PHYS)
    print("  P6_SCAN_WINDOW     = %s px each side (2*W = %s px cull slice)" % (WINDOW, (WINDOW * 2) if WINDOW else "?"))
    s1 = SCENE_PHYS is not None and WINDOW is not None
    if not s1:
        print("  [ RED ] S1 constants parsed = SCENE_PHYS=%s WINDOW=%s" % (SCENE_PHYS, WINDOW))
        print("RESULT: RED -- could not parse the live sizing constants.")
        return 1

    rows = []
    for name, rec in sc.items():
        co = rec.get("_coords", {})
        if not isinstance(co, dict):
            continue
        rows.append((max_in_window(co, WINDOW), name))
    rows.sort(reverse=True)
    worst, worst_scene = rows[0]
    need = worst + ALWAYS_MARGIN
    over = [r for r in rows if (r[0] + ALWAYS_MARGIN) > SCENE_PHYS]

    print("-" * 70)
    print("  worst-case near demand (any 2*WINDOW slice, real placements):")
    for cnt, name in rows[:6]:
        flag = "  <-- needs %d > SCENE_PHYS" % (cnt + ALWAYS_MARGIN) if (cnt + ALWAYS_MARGIN) > SCENE_PHYS else ""
        print("     %-22s  %4d  (+%d always = %d)%s" % (name.split("/")[-1] if "/" in name else name, cnt, ALWAYS_MARGIN, cnt + ALWAYS_MARGIN, flag))
    print("  WHOLE-GAME worst = %d in %s  (+%d always margin = %d needed)" % (worst, worst_scene, ALWAYS_MARGIN, need))
    print("-" * 70)

    s2 = SCENE_PHYS >= need
    print("  [%s] S1 constants parsed" % "GREEN")
    print("  [%s] S2 SIZED TO CEILING  SCENE_PHYS(%d) >= whole-game demand(%d)"
          % ("GREEN" if s2 else " RED ", SCENE_PHYS, need))
    if s2:
        print("RESULT: GREEN -- SCENE_PHYS=%d covers EVERY scene's camera-near demand at WINDOW=%d "
              "(worst %d=%s + always margin). The shrink will not drop near entities in any of the %d scenes."
              % (SCENE_PHYS, WINDOW, worst, worst_scene, len(rows)))
        return 0
    print("RESULT: RED -- SCENE_PHYS=%d < %d needed (%d scenes over). Raise P6_POOL_SCENE_PHYS to >= %d "
          "(or lower P6_SCAN_WINDOW, but verify the always-pin count first)." % (SCENE_PHYS, need, len(over), need))
    return 1


if __name__ == "__main__":
    sys.exit(main())
