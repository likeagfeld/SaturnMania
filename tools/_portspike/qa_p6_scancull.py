#!/usr/bin/env python3
# Mass-port plan I3c -- loop1 SPATIAL-CULL (the camera-local pool fps win, RED-first).
#
# GOAL (plan 7.5 I3c): cut the GHZ ProcessObjects loop1 cost -- ~757 ACTIVE_BOUNDS scene
# entities reading position.x/y from slow WRAM-L EVERY frame (Object.cpp:602-660). A
# sorted-by-x index of the populated scene entities (built once per scene load) + a per-frame
# WRAM-H near-bitfield (p6_scan_near) lets loop1 set inRange=false for FAR scene entities via
# a fast bit, WITHOUT the position read. PROVABLY CONSERVATIVE (768 px window >= the full
# ACTIVE_BOUNDS/XBOUNDS x-range), parity-safe for GHZ1 (ghz-scan-split-parity-audit). Full
# backing + IDENTITY remap kept (byte-identical SaturnEntityAt); the WRAM-L-saving compaction
# is the separate later step.
#
# GREEN (this gate -- the cull is live + sane):
#   C1 boots                  cont_frames > 0           (the cull/index didn't fault)
#   C2 index built            scancull_n > 0            (sorted-by-x scene index populated)
#   C3 near set BOUNDED       0 < scancull_near <= scancull_n  AND scancull_near < scancull_n
#                             (only the camera-window slice is near -- proof the cull culls)
# The fps win is read from qa_p6_perf (compute-full should drop); GHZ-green from
# qa_p6_ghz_regression (R0-R16). RED now: the witnesses are absent (unbuilt).
#
#   python tools/_portspike/qa_p6_scancull.py <savestate.mcs> [map]

import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q


def main(argv):
    if len(argv) < 1:
        print("usage: qa_p6_scancull.py <savestate.mcs> [map]"); return 2
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
    n = peek("_p6_w_scancull_n")
    near = peek("_p6_w_scancull_near")
    capped = peek("_p6_w_scancull_capped")  # WHOLE-GAME: 1 == index hit the cap -> un-indexed scene slots

    print("=== qa_p6_scancull (loop1 spatial-cull, mass-port I3c -- the fps win) ===")
    print("  cont_frames    = %s" % Q._dv(cont))
    print("  scancull_n     = %s   (sorted-by-x scene-entity index entries)" % Q._dv(n))
    print("  scancull_near  = %s   (entities in the camera-window slice last frame)" % Q._dv(near))
    print("-" * 64)

    if n is None or near is None:
        print("RED: I3c witness(es) absent -- the spatial cull is not built yet (expected RED pre-I3c).")
        return 1
    c1 = cont is not None and cont > 0
    c2 = n > 0
    c3 = near is not None and 0 < near <= n and near < n
    # C4 WHOLE-GAME: the sorted index covered EVERY populated scene slot (the cap was not the
    # limiter). RED if capped==1 (un-indexed scene slots -> their ACTIVE_BOUNDS entities skipped)
    # OR if the witness is absent (pre-fix build -- the GHZ1-ism cap=800 under-covered 1034).
    c4 = capped is not None and capped == 0
    for tag, ok, d in [("C1 boots (cont_frames>0)", c1, Q._dv(cont)),
                       ("C2 index built (scancull_n>0)", c2, Q._dv(n)),
                       ("C3 near set BOUNDED (0<near<n)", c3, "%s of %s" % (Q._dv(near), Q._dv(n))),
                       ("C4 WHOLE-GAME index not truncated (capped==0)", c4, Q._dv(capped))]:
        print("  [%s] %s = %s" % ("GREEN" if ok else " RED ", tag, d))
    if c1 and c2 and c3 and c4:
        pct = (100.0 * near / n) if n else 0.0
        print("RESULT: GREEN -- the spatial cull is live: loop1 skips the WRAM-L position read for the "
              "%d of %d (%.0f%%) FAR scene entities. Confirm the fps drop via qa_p6_perf (compute-full) "
              "+ GHZ-green via qa_p6_ghz_regression (R0-R16)." % (n - near, n, 100.0 - pct))
        return 0
    print("RESULT: RED -- the cull is not live/sane (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
