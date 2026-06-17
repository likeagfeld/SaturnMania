#!/usr/bin/env python3
# Task #181 -- GHZ Bridge object gate (RED-first, data-driven).
#
# BUG: "bridges disappear in level". MEASURED root cause: the GHZ Bridge object
# (SonicMania_Objects_GHZ_Bridge.c) was NOT in the engine pack -- it was never
# compiled, never registered (no RSDK_REGISTER_OBJECT(Bridge) in
# p6_wave1_reg.c's p6_wave1_link). GHZ1's scene places 3 Bridge entities; with
# the class unregistered the engine resolves them to a blank slot -> they never
# Create, never Draw -> the plank sprites are absent (the user's "disappear").
#
# FIX (this gate guards it): compile the VERBATIM decomp Bridge TU into the pack
# (Game_Bridge.o), register it in p6_wave1_link (Game.c alphabetical order:
# between BoundsMarker and Camera), and NULL its only out-of-pack dependency
# (BurningLog -- closure-edge, Bridge_Burn is reachable only via SHIELD_FIRE
# which is unobtainable until ItemBox is ported).
#
# TWO measurable conditions (both must hold) in a GHZ-live capture:
#   B1  p6_w_brg_classid > 0   -- the Bridge class registered with a live classID
#                                 (0/absent == unregistered == the bug).
#   B2  p6_w_brg_count    > 0   -- the engine instantiated the scene's Bridge
#                                 entities (GHZ1 places 3). 0 == they resolved to
#                                 a blank slot == the bug.
#
# Diagnostics printed (not gated here -- the rendered-plank screenshot from the
# P6_WARP_BRIDGE build is the visual proof): brg_posx/posy (first bridge world
# pos), brg_frames (its animator.frames ptr -- 0/-1 would mean the Bridge.bin
# sprite alloc-failed the residency budget #247), brg_onscreen.
#
#   python tools/_portspike/qa_p6_bridge.py              # boot+capture+verdict
#   python tools/_portspike/qa_p6_bridge.py --mcs X.mcs  # evaluate existing
#
# RED on the current build (witness symbols absent / Bridge unregistered);
# GREEN once the verbatim Bridge TU is compiled+registered into the pack.

import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

GATE_FRAME = 50.0          # wall-clock seconds: GHZ live + Bridge witness latched
ROOT       = os.path.normpath(os.path.join(HERE, "..", ".."))
CUE        = os.path.join(ROOT, "game.cue")
SAVESTATE  = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS    = os.path.join(HERE, "_p6_bridge.mcs")

NAMES = ["_p6_w_cont_frames", "_p6_w_brg_classid", "_p6_w_brg_count",
         "_p6_w_brg_posx", "_p6_w_brg_posy", "_p6_w_brg_onscreen",
         "_p6_w_brg_frames"]


def capture(out):
    lck = os.path.join(ROOT, ".mednafen", "mednafen.lck")
    try:
        os.remove(lck)
    except OSError:
        pass
    # qa_savestate.ps1 Join-Paths -Cue to $root unconditionally (no IsPathRooted
    # guard, unlike -Out), so the cue MUST be relative to the repo root.
    cmd = ["pwsh", SAVESTATE, "-Cue", "game.cue", "-SaveFrame", str(GATE_FRAME), "-Out", out]
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

    print("=== qa_p6_bridge (GHZ Bridge object, #181) ===")
    print("  mcs            = %s" % os.path.basename(mcs))
    print("  cont_frames    = %s (GHZ live)" % Q._dv(v["_p6_w_cont_frames"]))
    print("  brg_classid    = %s   (expect > 0 = Bridge registered)" % Q._dv(v["_p6_w_brg_classid"]))
    print("  brg_count      = %s   (expect > 0 = scene Bridge entities)" % Q._dv(v["_p6_w_brg_count"]))
    print("  brg_posx       = %s   (first bridge world x, 16.16 fixed)" % Q._dv(v["_p6_w_brg_posx"]))
    print("  brg_posy       = %s   (first bridge world y, 16.16 fixed)" % Q._dv(v["_p6_w_brg_posy"]))
    print("  brg_frames     = %s   (animator.frames ptr; 0/-1 = sprite alloc-fail)" % Q._dv(v["_p6_w_brg_frames"]))
    print("  brg_onscreen   = %s" % Q._dv(v["_p6_w_brg_onscreen"]))

    cid = v["_p6_w_brg_classid"]
    cnt = v["_p6_w_brg_count"]
    if cid is None or cnt is None:
        print("RED: Bridge witnesses absent (class never compiled into the pack)"); return 1

    b1 = cid > 0
    b2 = cnt > 0
    print("  [%s] B1 Bridge class registered (classid=%s > 0)" % ("GREEN" if b1 else " RED ", Q._dv(cid)))
    print("  [%s] B2 scene Bridge entities instantiated (count=%s > 0)" % ("GREEN" if b2 else " RED ", Q._dv(cnt)))
    if b1 and b2:
        print("RESULT: GREEN -- Bridge ported + registered + instantiated "
              "(screenshot the P6_WARP_BRIDGE build to confirm planks render)")
        return 0
    print("RESULT: RED -- Bridge not in the engine pack (see B1/B2)")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
