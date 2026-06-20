#!/usr/bin/env python3
# =============================================================================
# qa_p6_backtrack.py -- I3b 2b camera-local pool LIFECYCLE (backtrack) BEHAVIORAL proof (BINDING).
#
# WHY: the per-frame stream re-materializes any newly-near scene entity from the DORM store. Without a
# lifecycle guard that would re-spawn a COLLECTED ring / BROKEN badnik every time the camera scrolls
# back over its slot -- infinite rings, resurrected enemies. The stream carries a per-logical-slot
# "destroyed" bit (set when a resident entity's classID hits 0, exactly how RSDK destroyEntity signals
# a kill) that the materialize path checks to keep it dead. That bit's INDEXING is proven in-bounds
# (qa_p6_lifecycle_index, static), but the RUNTIME retire/skip BEHAVIOR was unobserved -- the lean boot
# never destroys anything. This gate observes it.
#
# HOW: build with P6_BACKTRACK_PROOF -- the overlay's stream, once settled (call 30), synthetically
# DESTROYS the first resident scene entity (classID=0) and records, every frame after:
#   bt_cid     = its classID BEFORE the destroy   (sanity: a REAL registered entity was hit)
#   bt_life    = 1 once the stream RETIRED it      (the retire path set the destroyed bit)
#   bt_reappear= 1 if it RE-MATERIALIZED           (THE BUG -- the skip-destroyed path failed)
# The entity stays NEAR the whole time (camera fixed at spawn), so only the lifecycle bit keeps it dead.
#
#   B1 cid_before != 0     -- a real registered entity was destroyed (the test actually ran)
#   B2 life == 1           -- the stream RETIRED it (set the lifecycle bit)
#   B3 reappear == 0       -- it NEVER re-materialized (the collected-ring-stays-collected invariant)
#
# RED on a build with the skip disabled (-DP6_BT_NOSKIP forces materialize -> reappear=1); GREEN on the
# real lifecycle. Run on any change to the stream's retire / skip-destroyed / free-list logic.
#
#   1. build the proof ISO (inside Docker), GREEN variant:
#        MSYS_NO_PATHCONV=1 docker run --rm -e P6_BACKTRACK_PROOF=1 -v D:/sonicmaniasaturn:/work \
#            -w /work joengine-saturn:latest bash tools/_portspike/_p6/build_shipping.sh
#      RED variant adds  -e P6_BT_NOSKIP=1 .  Then re-master CD-DA on the host.
#   2. python tools/_portspike/qa_p6_backtrack.py            # boot + capture + assert
#      python tools/_portspike/qa_p6_backtrack.py --mcs X.mcs
# =============================================================================
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

# Destroy fires at stream-call 30 (well after the spawn near-set settles, ~2000 stream calls before the
# frame-130 capture), leaving ~all remaining frames to prove "stays dead".
GATE_FRAME = 130.0
ROOT      = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS   = os.path.join(HERE, "_p6_backtrack.mcs")

NAMES = ["_p6_w_cont_frames", "_p6_w_bt_logical", "_p6_w_bt_cid", "_p6_w_bt_life",
         "_p6_w_bt_reappear", "_p6_w_stream_mat", "_p6_w_stream_resident"]


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
        ("B1 a real registered entity was destroyed (cid_before>0)", v["_p6_w_bt_cid"],      lambda x: x is not None and x > 0),
        ("B2 stream RETIRED it (life bit set ==1)",                  v["_p6_w_bt_life"],     lambda x: x == 1),
        ("B3 it NEVER re-materialized (reappear==0)",                v["_p6_w_bt_reappear"], lambda x: x == 0),
    ]
    info = ("destroyed logical slot=%s  stream(mat=%s resident=%s)  cont_frames=%s" % (
        v.get("_p6_w_bt_logical"), v.get("_p6_w_stream_mat"),
        v.get("_p6_w_stream_resident"), v.get("_p6_w_cont_frames")))
    print("=" * 64)
    print("I3b 2b CAMERA-LOCAL POOL -- LIFECYCLE (BACKTRACK) BEHAVIORAL PROOF")
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
        print("RESULT: GREEN -- a destroyed entity was RETIRED and NEVER re-materialized despite staying "
              "near. The lifecycle keeps collected rings collected / broken badniks broken on scroll-back.")
        return 0
    print("RESULT: RED -- the destroyed entity reappeared or was not retired (reappear=%s life=%s). The "
          "stream would resurrect collected rings / broken badniks." % (v.get("_p6_w_bt_reappear"), v.get("_p6_w_bt_life")))
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
