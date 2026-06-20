#!/usr/bin/env python3
# =============================================================================
# qa_p6_streamscan.py -- I3b 2b PERF #2: per-frame stream-scan cost gate (RED-first).
#
# MEASURED (2026-06-20, P6_STREAM_PERF build, SaveFrame 160 spawn-idle capture): the per-frame
# p6_ovl_stream manager iterates ALL 1088 scene logical slots [R, R+SCN) EVERY frame, reading
# remap[L] from the wait-stated A-bus CART each iteration -- even at an idle camera with no
# materialize/dormant transitions. COST = 2941 FRC ticks = 3.512 ms = 32.4% of the 10.83 ms
# ProcessObjects (cyc_obj), and compute-FULL 17.54 ms sits 0.87 ms OVER the 16.67 ms 60fps floor.
# So the 1088-scan is the dominant remaining lever: it acts on only ~83 slots (scancull_near 41 +
# stream_resident 42) yet scans 1088.
#
# THE FIX THIS GATES (the narrowing): replace the [R,R+SCN) full scan with
#   (A) MATERIALIZE pass over the camera near-window slice (the slots p6_scan_update_near found
#       near -- binary-searched over the sorted-by-x index, ~41), and
#   (B) DORMANT/RETIRE pass over a maintained RESIDENT-LIST (~42),
# so the per-frame work drops from 1088 to ~83 iterations (~13x) -> ~0.3 ms, pulling compute-FULL
# under the 60fps floor on steady frames.
#
# This gate reads the diag-only witness p6_w_perf_stream_frt (present ONLY in a P6_STREAM_PERF
# build; zero shipping cost) and converts ticks->ms via p6_w_perf_cks (the FRT divider). It is a
# THRESHOLD gate:
#   X1  stream_ms < THRESH_MS (1.0)   -- the narrowed scan is cheap (RED now at 3.51 ms full-scan)
#   X2  stream_ms < cyc_obj_ms        -- sanity: the stream is a SUBSET of ProcessObjects
#   X3  cont_frames > 0               -- a live GHZ capture
#
# RED on the current full-scan build (3.51 ms >= 1.0). GREEN once the scan is narrowed.
#
#   1. build the measurement ISO (inside Docker):
#        MSYS_NO_PATHCONV=1 docker run --rm -e P6_STREAM_PERF=1 -v D:/sonicmaniasaturn:/work \
#            -w /work joengine-saturn:latest bash tools/_portspike/_p6/build_shipping.sh
#      then re-master CD-DA on the host (python tools/build_cdda.py ...).
#   2. capture (deep, continuous-GHZ): SaveFrame 160 -> _streamperf.mcs
#   3. python tools/_portspike/qa_p6_streamscan.py --mcs tools/_portspike/_streamperf.mcs
# =============================================================================
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

GATE_FRAME = 160.0
THRESH_MS  = 1.0                 # narrowed near+resident scan must be well under 1 ms (full-scan is 3.51)
ROOT       = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE  = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS    = os.path.join(HERE, "_streamperf.mcs")

NAMES = ["_p6_w_perf_stream_frt", "_p6_w_perf_cyc_obj", "_p6_w_perf_full_frt", "_p6_w_perf_cks",
         "_p6_w_cont_frames", "_p6_w_stream_resident", "_p6_w_scancull_near"]


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

    cks = v["_p6_w_perf_cks"]
    if v["_p6_w_perf_stream_frt"] is None:
        print("RED: p6_w_perf_stream_frt absent -- this gate needs a P6_STREAM_PERF build "
              "(docker run -e P6_STREAM_PERF=1 ... build_shipping.sh).")
        return 1
    if cks is None or cks < 0:
        print("RED: cks divider unreadable"); return 1

    def ms(t):
        return None if t is None else t * (8 << (2 * cks)) / 26.8 / 1000.0

    stream_ms = ms(v["_p6_w_perf_stream_frt"])
    obj_ms    = ms(v["_p6_w_perf_cyc_obj"])
    full_ms   = ms(v["_p6_w_perf_full_frt"])
    cont      = v["_p6_w_cont_frames"]

    print("=" * 64)
    print("I3b 2b PERF #2 -- per-frame stream-scan cost (narrowing gate)")
    print("=" * 64)
    print("  cont_frames   = %s   (resident=%s near=%s of 1088 scanned)"
          % (Q._dv(cont), Q._dv(v["_p6_w_stream_resident"]), Q._dv(v["_p6_w_scancull_near"])))
    print("  cks divider   = %s  (tick = %.4f us)" % (cks, (8 << (2 * cks)) / 26.8))
    print("  stream_scan   = %6s ticks = %7.3f ms   (the per-frame p6_ovl_stream)"
          % (Q._dv(v["_p6_w_perf_stream_frt"]), stream_ms))
    print("  cyc_obj       = %6s ticks = %7.3f ms   (ProcessObjects total)"
          % (Q._dv(v["_p6_w_perf_cyc_obj"]), obj_ms if obj_ms else 0.0))
    print("  compute-FULL  = %6s ticks = %7.3f ms"
          % (Q._dv(v["_p6_w_perf_full_frt"]), full_ms if full_ms else 0.0))
    if obj_ms:
        print("  stream %% obj   = %5.1f%%" % (100.0 * stream_ms / obj_ms))
    print("-" * 64)

    x1 = stream_ms < THRESH_MS
    x2 = (obj_ms is not None) and (stream_ms < obj_ms)
    x3 = (cont is not None) and (cont > 0)
    print("  [%s] X1 stream_scan < %.1f ms (narrowed)        = %.3f ms"
          % ("GREEN" if x1 else " RED ", THRESH_MS, stream_ms))
    print("  [%s] X2 stream < ProcessObjects (sanity subset) = %s"
          % ("GREEN" if x2 else " RED ", x2))
    print("  [%s] X3 live GHZ capture (cont_frames>0)        = %s"
          % ("GREEN" if x3 else " RED ", Q._dv(cont)))
    print("-" * 64)
    if x1 and x2 and x3:
        print("RESULT: GREEN -- the per-frame stream scan is narrowed to the near+resident set "
              "(%.3f ms < %.1f ms). compute-FULL headroom recovered toward the 60fps floor." % (stream_ms, THRESH_MS))
        return 0
    print("RESULT: RED -- the per-frame stream scan is %.3f ms (>= %.1f ms). It still scans all 1088 "
          "scene slots; narrow it to the camera near-window slice (materialize) + a maintained "
          "resident-list (dormant)." % (stream_ms, THRESH_MS))
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
