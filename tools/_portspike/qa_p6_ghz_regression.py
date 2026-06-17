#!/usr/bin/env python3
# =============================================================================
# qa_p6_ghz_regression.py -- WHOLE-LEVEL GHZ1 regression gate (BINDING).
#
# WHY (user-mandated 2026-06-17): registering Spring/PlaneSwitch silently broke
# the WORKING bridges -- their LoadSpriteAnimation overflowed the shared
# DATASET_STG anim pool, so Bridge.bin's anim alloc failed (brg_frames=0). I had
# verified only the NEW object, not the whole level. This gate re-validates the
# UNION of confirmed GHZ1 features in ONE capture, so any object-add that
# regresses a prior feature is caught BEFORE hand-off. Run it every build.
# See memory/whole-level-regression-gate-every-object-add.md.
#
# Asserts (all must hold in a live GHZ capture):
#   R0  cont_frames   > 0    -- boot healthy (no anim/band hang)
#   R1  brg_classid   > 0    -- Bridge registered
#   R2  brg_frames    > 0    -- Bridge.bin anim LOADED (0 == pool starved == the
#                              regression that vanished the planks)
#   R3  loop_pscount  > 0    -- PlaneSwitch instantiated (loop)
#   R4  live_rings   == 0    -- P0 spawn (no carry-over 100 rings)
#   R5  live_shield  == 0    -- P0 spawn (no fire shield)
#   R6  time_enabled == 1    -- P0 timer ticking
#
#   python tools/_portspike/qa_p6_ghz_regression.py            # boot+capture
#   python tools/_portspike/qa_p6_ghz_regression.py --mcs X.mcs
# =============================================================================
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

# Shipping GHZ load is host/IO-bound (~24s+ emulated; FpsScale does NOT compress
# it) -- the auto-capture must land AFTER the scene is live or every witness reads
# its init value (false RED). 130 is the proven-good shipping frame (see memory
# shipping-ghz-load-timing.md: cont_frames==0 == captured too early, NOT a freeze).
GATE_FRAME = 130.0
ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_p6_ghzreg.mcs")

NAMES = ["_p6_w_cont_frames", "_p6_w_brg_classid", "_p6_w_brg_frames",
         "_p6_w_loop_pscount", "_p6_w_plr_live_rings", "_p6_w_plr_live_shield",
         "_p6_w_time_enabled", "_p6_w_spring_classid", "_p6_w_spring_frames"]


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
        print("RED: magic uncalibrated"); return 1
    v = {n: (Q.peek_u32(mod, sec, Q.map_symbol(mp, n), perm, signed=True)
             if Q.map_symbol(mp, n) else None) for n in NAMES}

    checks = [
        ("R0 boot healthy (cont_frames>0)",      v["_p6_w_cont_frames"],   lambda x: x and x > 0),
        ("R1 Bridge registered (classid>0)",     v["_p6_w_brg_classid"],   lambda x: x and x > 0),
        ("R2 Bridge.bin LOADED (frames>0)",      v["_p6_w_brg_frames"],    lambda x: x and x > 0),
        ("R3 PlaneSwitch live (pscount>0)",      v["_p6_w_loop_pscount"],  lambda x: x and x > 0),
        ("R4 spawn rings==0",                    v["_p6_w_plr_live_rings"], lambda x: x == 0),
        ("R5 spawn shield==0",                   v["_p6_w_plr_live_shield"],lambda x: x == 0),
        ("R6 timer enabled==1",                  v["_p6_w_time_enabled"],   lambda x: x == 1),
        # #254 anim-pool funding: Spring is the first swept object; once it loads it
        # JOINS the confirmed-feature union so the NEXT object-add can't starve it.
        ("R7 Spring registered (classid>0)",     v["_p6_w_spring_classid"], lambda x: x and x > 0),
        ("R8 Spring.bin LOADED (frames>0)",      v["_p6_w_spring_frames"],  lambda x: x and x > 0),
    ]
    print("=" * 64)
    print("GHZ1 WHOLE-LEVEL REGRESSION GATE")
    print("=" * 64)
    ok = True
    for label, val, test in checks:
        passed = False
        try:
            passed = bool(test(val))
        except Exception:
            passed = False
        ok = ok and passed
        print("  [%s] %-34s = %s" % ("GREEN" if passed else " RED ", label, Q._dv(val)))
    print("-" * 64)
    if ok:
        print("RESULT: GREEN -- all confirmed GHZ1 features intact")
        return 0
    print("RESULT: RED -- a confirmed feature REGRESSED (see the RED line above)")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
