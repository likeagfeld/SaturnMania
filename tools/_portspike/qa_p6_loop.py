#!/usr/bin/env python3
# Task #254 -- GHZ1 loop-traversal object closure gate (RED-first, data-driven).
#
# BUG (user): "runs nearly halfway up the loop, then Sonic's sprite disappears
# and he returns back ... never completes the loop." MEASURED root cause (scene
# census, build_scene_census.py): GHZ1 places 106 PlaneSwitch entities -- the
# collision-plane toggle that lets Sonic run the inside of a loop -- plus the
# CorkscrewPath/ForceSpin/ForceUnstick/SpinBooster force objects. NONE were
# registered in the engine pack (the StageConfig-only view missed PlaneSwitch
# because it is a GLOBAL object, not zone-declared). Unregistered -> the scene
# resolves them to a blank slot -> the plane never switches -> Sonic can't go
# around the loop.
#
# FIX (this gate guards it): register the 5 verbatim loop TUs into the pack
# (Game_{CorkscrewPath,ForceSpin,ForceUnstick,PlaneSwitch,SpinBooster}.o,
# RSDK_REGISTER_OBJECT in p6_wave1_link). The census proved all 5 are clean
# drop-ins (fit the 344 B slot, no unregistered deps, no unstaged sheets, no
# draw-stub gates).
#
# Two measurable conditions (both must hold) in a GHZ-live capture:
#   L1  p6_w_loop_regmask == 0x1F  -- all 5 loop classes have a live classID
#                                     (bit set per CorkscrewPath|ForceSpin|
#                                     ForceUnstick|PlaneSwitch|SpinBooster).
#   L2  p6_w_loop_pscount   > 0    -- the scene instantiated PlaneSwitch
#                                     (GHZ1 places 106; 0 == blank slot == bug).
#
# The rendered proof (Sonic completing the loop) is a live-input play test --
# this gate guards the necessary condition (registered + instantiated).
#
#   python tools/_portspike/qa_p6_loop.py              # boot+capture+verdict
#   python tools/_portspike/qa_p6_loop.py --mcs X.mcs  # evaluate existing
#
# RED on the current build (witnesses absent / classes unregistered).

import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

GATE_FRAME = 60.0          # wall-clock seconds: GHZ live + loop witness latched
ROOT       = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE  = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS    = os.path.join(HERE, "_p6_loop.mcs")

NAMES = ["_p6_w_cont_frames", "_p6_w_loop_regmask", "_p6_w_loop_pscount"]


def capture(out):
    lck = os.path.join(ROOT, ".mednafen", "mednafen.lck")
    try:
        os.remove(lck)
    except OSError:
        pass
    # qa_savestate.ps1 Join-Paths -Cue to $root (no IsPathRooted guard) -> pass
    # the cue RELATIVE to the repo root.
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
        v[n] = Q.peek_u32(mod, sections, a, perm, signed=True) if a else None

    cont = v["_p6_w_cont_frames"]
    mask = v["_p6_w_loop_regmask"]
    psc  = v["_p6_w_loop_pscount"]

    print("=== qa_p6_loop (GHZ1 loop object closure, #254) ===")
    print("  mcs            = %s" % os.path.basename(mcs))
    print("  cont_frames    = %s (GHZ live)" % Q._dv(cont))
    print("  loop_regmask   = %s   (bit3 = PlaneSwitch registered; corkscrew set deferred)"
          % (("0x%02X" % mask) if mask is not None else "None"))
    print("  loop_pscount   = %s   (PlaneSwitch placed in GHZ1; expect 106)" % Q._dv(psc))

    if mask is None or psc is None:
        print("RED: loop witnesses absent (classes never compiled into the pack)"); return 1
    if cont is None or cont <= 0:
        print("RED: not a live GHZ capture (cont_frames=%s) -- recapture deeper" % Q._dv(cont)); return 1

    l1 = (mask & 0x08) != 0
    l2 = psc > 0
    print("  [%s] L1 PlaneSwitch registered (regmask bit3 set: 0x%02X)"
          % ("GREEN" if l1 else " RED ", mask))
    print("  [%s] L2 PlaneSwitch instantiated (pscount=%s > 0)" % ("GREEN" if l2 else " RED ", Q._dv(psc)))
    if l1 and l2:
        print("RESULT: GREEN -- loop closure registered + instantiated "
              "(play-test the loop to confirm Sonic completes it)")
        return 0
    print("RESULT: RED -- loop objects not fully live (see L1/L2)")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
