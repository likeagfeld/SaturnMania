#!/usr/bin/env python3
# Task #251 -- GHZ-shipping LOAD-TIME gate (RED-first, data-driven).
#
# ROOT CAUSE (measured 2026-06-16, build with p6_w_load_step breadcrumb):
#   The engine boot load is dominated by the windowed-GFS pack reads. A
#   SaveFrame sweep reading p6_w_load_step + p6_w_gfs_fills localized the cost:
#     - 8 sheet decompresses (load_step 10-17): 0 GFS fills -> CPU (miniz), ~7s
#     - LoadGameConfig     (load_step 21): ~45 GFS window fills, ~8s  IO-BOUND
#     - LoadSceneFolder    (load_step 31): ~45 GFS window fills, ~8s  IO-BOUND
#     - LoadSceneAssets+InitObjects (33-34): ~44 fills,          ~8s  IO-BOUND
#   145 total 4 KB-window fills, each doing GFS_Seek+GFS_Fread ~100-178 ms on
#   the emulated CD. p6_window_fill ALWAYS GFS_Seek'd, even when the read
#   continued sequentially from the prior one -> a CD re-seek every 4 KB.
#
# FIX (this gate guards it): skip GFS_Seek when the shared access pointer is
#   already at the requested sector (sequential continuation). p6_w_gfs_seeks_real
#   counts the fills that STILL needed a real seek; seeks_real << fills proves the
#   sequential reads went seek-free.
#
# THE GATE (user-facing: "did the game load faster"): boot the shipping ISO,
#   capture a savestate at SaveFrame=GATE_FRAME (wall-clock seconds, compute-
#   bound), and assert the GHZ scene is ALREADY LIVE -- p6_w_cont_frames > 0
#   (the first p6_ghz_frame has run). On the pre-fix build the game is not live
#   until ~48 s, so a capture at 44 s shows cont_frames == 0 -> RED. After the
#   seek-skip the load finishes earlier and cont_frames > 0 at 44 s -> GREEN.
#
#   python tools/_portspike/qa_p6_loadtime.py            # boot+capture+verdict
#   python tools/_portspike/qa_p6_loadtime.py --mcs X.mcs  # evaluate existing
#
# Diagnostics printed (not all gated): load_step, cont_frames, gfs_fills,
# gfs_seeks_real (absent on pre-fix builds), lay_resident.

import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

GATE_FRAME = 44.0          # wall-clock seconds after launch (compute-bound load)
ROOT       = os.path.normpath(os.path.join(HERE, "..", ".."))
CUE        = os.path.join(ROOT, "game.cue")
SAVESTATE  = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS    = os.path.join(HERE, "_p6_loadtime.mcs")

NAMES = ["_p6_w_load_step", "_p6_w_cont_frames", "_p6_w_gfs_fills",
         "_p6_w_gfs_seeks_real", "_p6_w_lay_resident", "_p6_w_perf_vblanks"]


def capture(out):
    # Host-quiet capture; clear a stale lock first (qa_savestate only kills its
    # own PID). pwsh per project tooling.
    lck = os.path.join(ROOT, ".mednafen", "mednafen.lck")
    try:
        os.remove(lck)
    except OSError:
        pass
    cmd = ["pwsh", SAVESTATE, "-Cue", CUE, "-SaveFrame", str(GATE_FRAME), "-Out", out]
    r = subprocess.run(cmd, capture_output=True, text=True)
    sys.stdout.write(r.stdout[-400:] if r.stdout else "")
    if not os.path.exists(out):
        print("FAIL: capture produced no savestate"); return False
    return True


def main(argv):
    mcs = None
    if "--mcs" in argv:
        mcs = argv[argv.index("--mcs") + 1]
    if mcs is None:
        mcs = TMP_MCS
        if not capture(mcs):
            return 1

    mod = Q.load_harness()
    map_text = Q.read_text(Q.MAP_DEFAULT)
    sections = mod.parse_savestate(Q._as_path(mcs))
    magic_addr = Q.map_symbol(map_text, "_p6_w_magic")
    raw = mod._peek_bytes(sections, magic_addr, 4) if magic_addr else None
    _, perm = Q.calibrate(raw)
    if perm is None:
        print("RED: magic uncalibrated (image not loaded / wrong bank)"); return 1

    v = {}
    for n in NAMES:
        a = Q.map_symbol(map_text, n)
        v[n] = Q.peek_u32(mod, sections, a, perm) if a else None

    print("=== qa_p6_loadtime (SaveFrame %.0f) ===" % GATE_FRAME)
    print("  mcs            = %s" % os.path.basename(mcs))
    print("  load_step      = %s" % Q._dv(v["_p6_w_load_step"]))
    print("  cont_frames    = %s" % Q._dv(v["_p6_w_cont_frames"]))
    print("  gfs_fills      = %s" % Q._dv(v["_p6_w_gfs_fills"]))
    print("  gfs_seeks_real = %s" % Q._dv(v["_p6_w_gfs_seeks_real"]))
    print("  lay_resident   = %s" % Q._dv(v["_p6_w_lay_resident"]))
    fills = v["_p6_w_gfs_fills"]; seeks = v["_p6_w_gfs_seeks_real"]
    if fills and seeks is not None and fills > 0:
        print("  seq fills (seek-free) = %d/%d (%.0f%%)" %
              (fills - seeks, fills, 100.0 * (fills - seeks) / fills))

    cont = v["_p6_w_cont_frames"]
    if cont is None:
        print("RED: cont_frames witness absent"); return 1
    if cont > 0:
        print("GREEN: GHZ live by SaveFrame %.0f (cont_frames=%d) -- load fits the budget" %
              (GATE_FRAME, cont))
        return 0
    print("RED: GHZ NOT live by SaveFrame %.0f (cont_frames=0) -- load too slow" % GATE_FRAME)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
