#!/usr/bin/env python3
# Mass-port I3d -- loop1 LIVE-ITERATION (the 60fps step, RED-first).
#
# I3c (183f92f, GHZ-GREEN) spatial-culled the FAR scene entities' slow WRAM-L
# position READS, but loop1 STILL ITERATED all 1216 slots (the ~9.88ms bare scan
# -> compute-full 19.66ms > the 16.67ms 1-vblank cliff -> 2 vbl/frame -> 30fps).
# I3d SKIPS the far scene slots ENTIRELY (no WRAM-L classID/active touch) via the
# combined near|always-iterate bit (p6_scan_near |= p6_scan_always), cutting loop1
# to ~2ms -> compute-full < 16.67ms -> 1 vbl/frame -> 60fps.
#
# WHY parity-exact (not a new approximation): the skip-set == I3c's already-proven
# {scene, ACTIVE_BOUNDS/XBOUNDS, x-far} inRange-forced-false set. The always-iterate
# bitfield (seeded at load + maintained per-frame) pins every NORMAL/ALWAYS/YBOUNDS/
# RBOUNDS/manager scene slot so they are NEVER wrongly skipped; inRange is engine-
# private (nothing on Saturn reads it -- loops 2/3 + draw run off s_p6_inrange /
# drawGroups), so leaving a far slot's stale inRange untouched is behaviorally inert.
#
# THE GATE (RED on the I3c build, GREEN once I3d lands):
#   D1 boots                 cont_frames > 0                  (loop1 restructure didn't fault)
#   D2 I3d built             scan_always present              (always-iterate seeded at load)
#   D3 60fps CLIFF CROSSED   compute-full (full_frt) <= 16.67ms (the 1-vblank budget)
# Correctness (entities still live) = qa_p6_ghz_regression R0-R16; cull sanity =
# qa_p6_scancull; WRAM-H-safe = qa_p6_mapoverlap. RED now: D3 ~19.66ms > 16.67ms.
#
#   python tools/_portspike/qa_p6_i3d.py <savestate.mcs> [map]

import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

CLOCK_MHZ = 26.8742          # NTSC 320-mode SH-2 sysclk (matches qa_p6_perf)
VBL_MS = 1000.0 / 60.0       # one true NTSC vblank = 16.667 ms (the 60fps budget)


def main(argv):
    if len(argv) < 1:
        print("usage: qa_p6_i3d.py <savestate.mcs> [map]"); return 2
    mcs = Q._as_path(argv[0])
    mp = Q._as_path(argv[1]) if len(argv) > 1 else Q.MAP_DEFAULT
    map_text = Q.read_text(mp)
    mod = Q.load_harness()
    sections = mod.parse_savestate(mcs)
    magic = Q.map_symbol(map_text, "_p6_w_magic")
    raw = mod._peek_bytes(sections, magic, 4) if magic else None
    _, perm = Q.calibrate(raw)
    if perm is None:
        print("RED: magic uncalibrated (image not loaded / captured too early)"); return 1

    def peek(sym):
        a = Q.map_symbol(map_text, sym)
        return Q.peek_u32(mod, sections, a, perm, signed=True) if a else None

    cont = peek("_p6_w_cont_frames")
    always = peek("_p6_w_scan_always")
    full = peek("_p6_w_perf_full_frt")
    synch = peek("_p6_w_perf_synch_frt")
    cks = peek("_p6_w_perf_cks")

    _c = cks if (cks is not None and 0 <= cks <= 3) else 1
    tick_us = (8 << (2 * _c)) / CLOCK_MHZ
    full_ms = (full * tick_us / 1000.0) if (full is not None and full > 0) else None
    synch_ms = (synch * tick_us / 1000.0) if (synch is not None and synch >= 0) else 0.0
    # LOCKED-60 is decided by MASTER TOTAL = compute-full + jo-body/slSynch (the swap wait),
    # NOT compute-full alone. A frame fits ONE vblank (-> 60fps) iff master <= 16.67ms; if
    # compute-full is under but master is over, slSynch swaps a vblank late -> 2 vbl -> 30fps.
    # (Measured: the body-skip build had full 15.87 < 16.67 but master 16.72 > 16.67 = 38fps.)
    master_ms = (full_ms + synch_ms) if full_ms is not None else None

    print("=== qa_p6_i3d (loop1 live-iteration, mass-port I3d -- the LOCKED-60 step) ===")
    print("  cont_frames    = %s" % Q._dv(cont))
    print("  scan_always    = %s   (always-iterate scene slots seeded at load)" % Q._dv(always))
    print("  compute-full   = %s ms   + slSynch %s ms" %
          (("%.2f" % full_ms) if full_ms is not None else "?", "%.2f" % synch_ms))
    print("  MASTER TOTAL   = %s ms   (1-vblank budget = %.2f ms; <= => 1 vbl/frame = 60fps)"
          % (("%.2f" % master_ms) if master_ms is not None else "?", VBL_MS))
    print("-" * 64)

    d1 = cont is not None and cont > 0
    d2 = always is not None        # symbol present in the map == I3d compiled in
    d3 = master_ms is not None and master_ms <= VBL_MS
    for tag, ok, d in [("D1 boots (cont_frames>0)", d1, Q._dv(cont)),
                       ("D2 I3d built (scan_always present)", d2, Q._dv(always)),
                       ("D3 LOCKED-60: MASTER TOTAL <= %.2f ms" % VBL_MS, d3,
                        ("%.2f ms" % master_ms) if master_ms is not None else "absent")]:
        print("  [%s] %s = %s" % ("GREEN" if ok else " RED ", tag, d))
    if d1 and d2 and d3:
        print("RESULT: GREEN -- LOCKED-60: master total %.2f ms <= %.2f ms vblank -> the frame "
              "fits ONE vblank -> 60fps. Confirm entities live via qa_p6_ghz_regression "
              "(R0-R16) + cull sanity via qa_p6_scancull." % (master_ms, VBL_MS))
        return 0
    if not d2:
        print("RESULT: RED -- I3d not built (scan_always absent); expected RED pre-I3d.")
    elif not d3:
        print("RESULT: RED -- master total %s ms > %.2f ms vblank -> slSynch swaps late -> "
              "frame spans 2 vbl -> ~30-38fps. loop1 iteration not yet cut enough."
              % (("%.2f" % master_ms) if master_ms else "?", VBL_MS))
    else:
        print("RESULT: RED -- did not boot (see D1).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
