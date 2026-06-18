#!/usr/bin/env python3
# =============================================================================
# qa_p6_phantom.py -- gate for the phantom-sidekick purge (#256 banked fix).
#
# GHZ1's Scene.bin places TWO Player markers (scene slotIDs 887,888 -> runtime
# slots 951,952 via Scene.cpp:646 slotID+RESERVE_ENTITY_COUNT). Real Mania's
# Player_LoadSprites (Player.c:779 foreach_all(Player)) CopyEntity(clearSrc)'s the
# chosen character into SLOT_PLAYER1 and destroyEntity's the REST -> exactly two
# live Players (Sonic + the SLOT_PLAYER2 sidekick spawned by Player_Create). On
# Saturn that consumption is incomplete: the 2nd marker survives as a STRAY
# sidekick at a scene slot (slot!=0 -> Player_Create sets sidekick +
# Player_Input_P2_AI), double-running Player_Input_P2_Delay every frame and
# clobbering the SHARED Player->leaderPositionBuffer + input shift-registers.
#
# FIX (p6_io_main.cpp p6_purge_scene_players, called post-InitObjects from the
# scene-load path): complete the consumption by zeroing the classID + interaction
# of any Player-classID entity in [RESERVE_ENTITY_COUNT, ENTITY_COUNT). The
# witness p6_w_phantom_purged counts how many strays it zeroed.
#
# Expected on the SHIPPING build (lean boot -> exactly ONE GHZ1 LoadScene via
# p6_scene_load_and_arm): the purge finds the single surviving 2nd marker ->
# p6_w_phantom_purged == 1. (The earlier diag co-location vehicle ran a different/
# doubled load path and measured 2; this gate fixes the SHIPPING flavor at frame
# 130, where the single-load count is deterministic.)
#
# Asserts (live shipping GHZ1 capture):
#   P0  cont_frames        > 0   boot healthy (captured after the scene is live)
#   P1  phantom_purged    == 1   the one stray GHZ1 scene-Player marker was purged
#
# RED  : pre-fix build has no helper/witness -> map_symbol None -> RED; or the fix
#        is disabled -> witness 0 -> RED.
# GREEN: the fix links + runs; a frame-130 GHZ1 capture reads phantom_purged == 1.
#
#   python tools/_portspike/qa_p6_phantom.py            # boot+capture
#   python tools/_portspike/qa_p6_phantom.py --mcs X.mcs
# =============================================================================
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

GATE_FRAME = 130.0   # proven-good shipping GHZ1-live frame (shipping-ghz-load-timing)
ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_p6_phantom.mcs")

GHZ1_PURGED = 1   # shipping single-load GHZ1: one surviving 2nd scene-Player marker

NAMES = ["_p6_w_cont_frames", "_p6_w_phantom_purged"]


def capture(out):
    try:
        os.remove(os.path.join(ROOT, ".mednafen", "mednafen.lck"))
    except OSError:
        pass
    r = subprocess.run(["pwsh", SAVESTATE, "-Cue", "game.cue",
                        "-SaveFrame", str(GATE_FRAME), "-Out", out],
                       capture_output=True, text=True)
    sys.stdout.write(r.stdout[-200:] if r.stdout else "")
    return os.path.exists(out)


def main(argv):
    mcs = argv[argv.index("--mcs") + 1] if "--mcs" in argv else TMP_MCS
    if "--mcs" not in argv and not capture(mcs):
        print("FAIL: no savestate"); return 1

    mod = Q.load_harness()
    mp = Q.read_text(Q.MAP_DEFAULT)
    sec = mod.parse_savestate(Q._as_path(mcs))
    ma = Q.map_symbol(mp, "_p6_w_magic")
    _, perm = Q.calibrate(mod._peek_bytes(sec, ma, 4) if ma else None)
    if perm is None:
        print("RED: magic uncalibrated (image not loaded / wrong bank)"); return 1
    v = {n: (Q.peek_u32(mod, sec, Q.map_symbol(mp, n), perm, signed=True)
             if Q.map_symbol(mp, n) else None) for n in NAMES}

    checks = [
        ("P0 boot healthy (cont_frames>0)",          v["_p6_w_cont_frames"],    lambda x: x and x > 0),
        ("P1 stray sidekick purged (purged==1)",     v["_p6_w_phantom_purged"], lambda x: x == GHZ1_PURGED),
    ]
    print("=" * 64)
    print("PHANTOM-SIDEKICK PURGE GATE (#256 banked fix)")
    print("=" * 64)
    ok = True
    for label, val, test in checks:
        try:
            passed = bool(test(val))
        except Exception:
            passed = False
        ok = ok and passed
        print("  [%s] %-40s = %s" % ("GREEN" if passed else " RED ", label, Q._dv(val)))
    print("-" * 64)
    if ok:
        print("RESULT: GREEN -- the stray GHZ1 scene-Player marker is purged (#256)")
        return 0
    print("RESULT: RED -- phantom purge absent/incorrect")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
