#!/usr/bin/env python3
# =============================================================================
# qa_p6_manifest.py -- I1 gate for the unified S1+7.3 camera-local entity pool
# (WHOLE_GAME_MASSPORT_PLAN.md 7.5, increment I1).
#
# I1 is the FIRST, additive, can't-break-GHZ step toward the camera-local pool:
# it instruments the engine LoadScene per-entity loop (Scene.cpp:620-653) to fold
# EVERY placed scene entity -- including the ones currently DROPPED at
# slotID>=1152 -- into three WRAM witnesses. It proves the "enumerate every
# placed entity at load with correct class/slot/pos" path that the I3 STORED
# manifest will build on, WITH ZERO NEW ALLOCATION (no manifest array yet -- its
# real home waits for I3 when the pool-shrink frees a verified ~24KB region;
# placing it today would be a guess, and the entity pool's BSS-overflow history
# forbids that). It touches neither SaturnEntityAt nor the pool, so GHZ stays
# byte-identical.
#
# Witnesses (p6_io_main.cpp, RSDK namespace, read from a Mednafen savestate via
# the shared qa_p6_scene harness; reset at the top of each LoadScene):
#   p6_w_manifest_n        count of placed entities folded this load
#   p6_w_manifest_maxslot  max raw slotID seen (the s7.3 drop discriminator)
#   p6_w_manifest_csum     djb2-ish over (classID, slotID, x>>16, y>>16) -- proves
#                          the per-entity fields were read, not just counted
#
# Asserts (live GHZ1 capture; the values are the MEASURED ground truth --
# Object.hpp note "GHZ1 places 1,041 scene entities (max raw slot 1,040)" +
# qa_entity_slots GHZ/Scene1.bin n_ent=1041 max_slot=1040):
#   M0  cont_frames    > 0      boot healthy (captured after the scene is live)
#   M1  manifest_n    == 1041   every placed GHZ1 entity enumerated
#   M2  manifest_maxslot==1040  max slotID matches the offline census
#   M3  manifest_csum  != 0     per-entity fields actually folded
#
# RED  : current shipping build has no manifest witnesses -> map_symbol returns
#        None -> the checks read None -> RED (proven on the current game.map).
# GREEN: the I1 build links the witnesses; a frame-130 GHZ1 capture populates them
#        to 1041 / 1040 / nonzero.
#
#   python tools/_portspike/qa_p6_manifest.py            # boot+capture
#   python tools/_portspike/qa_p6_manifest.py --mcs X.mcs
# =============================================================================
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

GATE_FRAME = 130.0   # proven-good shipping GHZ1-live frame (shipping-ghz-load-timing)
ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_p6_manifest.mcs")

# GHZ1 ground truth (Object.hpp note + qa_entity_slots GHZ/Scene1.bin).
GHZ1_N, GHZ1_MAXSLOT = 1041, 1040

NAMES = ["_p6_w_cont_frames", "_p6_w_manifest_n",
         "_p6_w_manifest_maxslot", "_p6_w_manifest_csum"]


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
        ("M0 boot healthy (cont_frames>0)",        v["_p6_w_cont_frames"],     lambda x: x and x > 0),
        ("M1 placed entities enumerated (n==1041)", v["_p6_w_manifest_n"],      lambda x: x == GHZ1_N),
        ("M2 max slotID matches census (==1040)",   v["_p6_w_manifest_maxslot"],lambda x: x == GHZ1_MAXSLOT),
        ("M3 per-entity fields folded (csum!=0)",    v["_p6_w_manifest_csum"],   lambda x: x is not None and x != 0),
    ]
    print("=" * 64)
    print("I1 MANIFEST-ENUMERATION GATE (camera-local pool)")
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
        print("RESULT: GREEN -- LoadScene enumerates every placed GHZ1 entity (I1)")
        return 0
    print("RESULT: RED -- manifest enumeration absent/incorrect")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
