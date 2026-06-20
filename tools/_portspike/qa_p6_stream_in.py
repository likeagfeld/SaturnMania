#!/usr/bin/env python3
# =============================================================================
# qa_p6_stream_in.py -- I3b 2b camera-local pool: POSITIVE streaming-in proof (BINDING).
#
# WHY: the GHZ1 whole-level regression gate (qa_p6_ghz_regression) captures at the x~108 SPAWN
# camera, where the nearest PlaneSwitch (x=3352) is >2200px outside the +/-1024 near window -- so
# pscount=0 there is CORRECT (offline-proven by scene_census), and R3 was relaxed to a camera-
# INDEPENDENT registration check. That leaves one thing the spawn capture CANNOT show: that a
# PlaneSwitch actually MATERIALIZES (live, via the per-frame stream) when the camera reaches it.
# This gate proves exactly that.
#
# HOW: build the SHIPPING image with P6_STREAM_PROOF defined (build_p6scene_objs.sh forwards
# -DP6_STREAM_PROOF to Object.cpp). That pins the camera EVERY frame onto the GHZ1 PlaneSwitch
# cluster near x=2500 (scene_census: 7 PlaneSwitches inside the +/-1024 window there). The camera
# jumps from the x~108 spawn into PlaneSwitch range -> those PlaneSwitches become NEWLY-NEAR ->
# the per-frame p6_ovl_stream MATERIALIZES + Creates them from the DORM store -> the foreach_all
# pscount latch goes > 0. The shipping (no-proof) spawn capture has pscount == 0; this proof
# capture's pscount > 0 is the direct RED->GREEN observation of PlaneSwitch streaming-in.
#
#   S1  pscount > 0        -- >=1 PlaneSwitch MATERIALIZED near the pinned x=2500 camera (the proof)
#   S2  starve == 0        -- no free-list starvation while materializing the near set (pool sized OK)
#   S3  cont_frames > 0    -- the proof build still boots (the camera pin did not hang the load)
#
# RED on a build where the per-frame stream fails to materialize a near PlaneSwitch (pscount stays 0
# despite the camera being parked on a cluster). GREEN once it streams them in.
#
#   1. build the proof ISO (inside Docker):
#        MSYS_NO_PATHCONV=1 docker run --rm -e P6_STREAM_PROOF=1 -v D:/sonicmaniasaturn:/work \
#            -w /work joengine-saturn:latest bash tools/_portspike/_p6/build_shipping.sh
#      then re-master CD-DA on the host (python tools/build_cdda.py ...).
#   2. python tools/_portspike/qa_p6_stream_in.py            # boot + capture + assert
#      python tools/_portspike/qa_p6_stream_in.py --mcs X.mcs
# =============================================================================
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

# The camera pin is active from gameplay frame 1, but the camera "jumps" 108 -> 2500, the FG/collision
# band re-inflates for x=2500, and the stream materializes the ~93 near entities -- give it margin past
# the proven-live shipping frame (130) so the pin + materialize have settled.
GATE_FRAME = 160.0
ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_p6_streamin.mcs")

NAMES = ["_p6_w_cont_frames", "_p6_w_loop_pscount", "_p6_w_loop_regmask",
         "_p6_w_scancull_near", "_p6_w_stream_mat", "_p6_w_stream_dorm",
         "_p6_w_stream_resident", "_p6_w_stream_free", "_p6_w_stream_starve"]


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
        ("S1 PlaneSwitch MATERIALIZED near pinned cam (pscount>0)", v["_p6_w_loop_pscount"], lambda x: x is not None and x > 0),
        ("S2 no free-list starve while materializing (starve==0)",  v["_p6_w_stream_starve"], lambda x: x == 0),
        ("S3 proof build still boots (cont_frames>0)",              v["_p6_w_cont_frames"],  lambda x: x and x > 0),
    ]
    info = ("regmask=0x%s  scancull_near=%s  stream(mat=%s dorm=%s resident=%s free=%s)" % (
        ("%X" % v["_p6_w_loop_regmask"]) if v.get("_p6_w_loop_regmask") is not None else "?",
        v.get("_p6_w_scancull_near"), v.get("_p6_w_stream_mat"), v.get("_p6_w_stream_dorm"),
        v.get("_p6_w_stream_resident"), v.get("_p6_w_stream_free")))
    print("=" * 64)
    print("I3b 2b CAMERA-LOCAL POOL -- PlaneSwitch STREAMING-IN PROOF")
    print("=" * 64)
    ok = True
    for label, val, test in checks:
        passed = False
        try:
            passed = bool(test(val))
        except Exception:
            passed = False
        ok = ok and passed
        print("  [%s] %-52s = %s" % ("GREEN" if passed else " RED ", label, Q._dv(val)))
    print("  info: " + info)
    print("-" * 64)
    if ok:
        print("RESULT: GREEN -- a near PlaneSwitch MATERIALIZED live via the per-frame stream "
              "(pscount=%s). The camera-local pool streams entities IN on approach." % v["_p6_w_loop_pscount"])
        return 0
    print("RESULT: RED -- the pinned-on-a-PlaneSwitch-cluster camera did NOT materialize one "
          "(pscount=%s). The per-frame stream is not bringing near entities in." % v["_p6_w_loop_pscount"])
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
