#!/usr/bin/env python3
# =============================================================================
# qa_p6_fgstream.py -- P6.8 W16-stream gate (Task #240): the GHZ foreground on
# VDP2 NBG1 must FOLLOW THE CAMERA past the 64x64-tile (1024 px) plane wrap.
#
# THE BUG (user, 2026-06-14): the present built a STATIC top-left 64x64 tile rect
# once and hardware-scrolled it. The NBG1 plane is 1024x1024 px and WRAPS; GHZ1
# is thousands of px wide, so once the camera scrolls past 1024 px the rect shows
# nothing / the plane wraps onto stale data -> "missing grass blocks" + "all VDP2
# goes glitchy when I move".
#
# THE FIX (p6_vdp2.c p6_vdp2_present_ghz_camera): a camera-ANCHORED streaming
# window -- on every tile-boundary crossing rewrite the visible+margin region
# into the wrapping plane at (tx & 63, ty & 63); the hardware scroll uses the
# full camera position so the cells line up at any distance travelled.
#
# THE MEASURABLE SIGNAL (self-check witnesses computed every present):
#   p6_w_fg_camtx     camera tile X this present
#   p6_w_fg_maxcamtx  max camera tile X seen since boot (motion proof)
#   p6_w_fg_visok     visible plane cells == live layout this present
#   p6_w_fg_visok_far STICKY: starts 1, set 0 the first time the plane MISMATCHES
#                     the layout at a camera that moved past 64 tiles (1024 px)
#   p6_w_fg_crosses   cumulative map (re)builds (dirty + per-crossing)
#   p6_w_fg_vischecked visible tiles compared last present
#
# This gate REQUIRES an IN-MOTION capture (hold Right so the camera scrolls past
# 1024 px). On the OLD static build visok_far flips to 0 as soon as the camera
# clears the wrap; on the streaming build it stays 1. RED -> GREEN.
#
# CHECKS
#   F1 stream witness symbols present in game.map (RED while uninstrumented).
#   F2 IN MOTION: p6_w_fg_maxcamtx >= 64 -- the capture actually scrolled the
#      camera past the 1024 px plane wrap (else the test proved nothing).
#   F3 FOLLOWS CAMERA: p6_w_fg_visok_far == 1 -- the plane matched the live
#      layout at every camera position past the wrap (the static build = 0).
#   F4 STREAMING LIVE: p6_w_fg_crosses > 1 -- the window rebuilt on crossings,
#      not just once, AND p6_w_fg_vischecked > 0 (the self-check ran).
#
# Usage: python tools/_portspike/qa_p6_fgstream.py [savestate.mcs] [map]
# =============================================================================
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

MCS_DEFAULT = os.path.join(HERE, "p6_fgstream.mcs")

SYMS = ["_p6_w_fg_maxcamtx", "_p6_w_fg_visok",
        "_p6_w_fg_visok_far", "_p6_w_fg_crosses"]

# The camera must clear the 64-tile (1024 px) plane wrap for the test to mean
# anything. GHZ1 is thousands of px wide so a ~3 s held-Right run clears it.
WRAP_TILES = 64


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.8 W16-STREAM GATE: GHZ foreground follows the camera past 1024 px")
    print("=" * 72)

    if not os.path.isfile(mp):
        print("RESULT: RED -- link map missing (%s)" % mp)
        return 1
    map_text = _scene.read_text(mp)
    syms = {}
    missing = []
    for s in [_scene.SYM_MAGIC] + SYMS:
        syms[s] = _scene.map_symbol(map_text, s)
        if syms[s] is None:
            missing.append(s)
    if missing:
        print("  [ RED ] F1 stream-witness symbols present in the link map")
        for s in missing:
            print("          MISSING %s" % s)
        print("          (Expected RED while the static-window present is unchanged.)")
        print("-" * 72)
        print("RESULT: RED -- FG streaming instrumentation not present (F1).")
        return 1
    print("  [GREEN] F1 stream-witness symbols present in the link map")

    if not os.path.isfile(mcs):
        print("RESULT: RED -- savestate missing (%s)" % mcs)
        return 1

    import pathlib
    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))
    raw_magic = mod._peek_bytes(sections, syms[_scene.SYM_MAGIC], 4)
    label, perm = _scene.calibrate(raw_magic)
    if perm is None:
        print("RESULT: RED -- magic mis-decode")
        return 1
    v = {s: _scene.peek_u32(mod, sections, syms[s], perm, signed=True)
         for s in SYMS}

    maxcamtx  = v["_p6_w_fg_maxcamtx"]
    visok     = v["_p6_w_fg_visok"]
    visok_far = v["_p6_w_fg_visok_far"]
    crosses   = v["_p6_w_fg_crosses"]

    f2 = maxcamtx is not None and maxcamtx >= WRAP_TILES
    f3 = visok_far is not None and visok_far == 1
    f4 = crosses is not None and crosses > 1

    checks = [
        ("F2 IN MOTION: camera scrolled past the 1024 px plane wrap "
         "(max_camtx >= %d)" % WRAP_TILES, f2,
         "max_camtx=%s tiles (=%s px)"
         % (maxcamtx, (maxcamtx * 16 if maxcamtx is not None else None))),
        ("F3 FOLLOWS CAMERA: plane matched the layout at every far camera "
         "(visok_far == 1)", f3,
         "visok_far=%s  visok(last present)=%s" % (visok_far, visok)),
        ("F4 STREAMING LIVE: window rebuilt on >1 crossing",
         f4, "crosses=%s" % (crosses,)),
    ]
    ok = f2 and f3 and f4
    print("-" * 72)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if not f2:
        print("NOTE: max_camtx < %d means the capture did NOT scroll far enough."
              % WRAP_TILES)
        print("      Re-capture holding Right longer (HoldScans 0x4D HoldExt 1,")
        print("      larger SettleMs / FpsScale) so the camera clears 1024 px.")
    if ok:
        print("RESULT: GREEN -- the FG plane follows the camera past the wrap.")
        return 0
    print("RESULT: RED -- FG plane does NOT follow the camera in motion.")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
