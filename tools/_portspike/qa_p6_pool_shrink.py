#!/usr/bin/env python3
# Mass-port plan I3 step 3 -- camera-local pool SHRINK (RED-first gate).
#
# GOAL (plan 7.5 I3b / WHOLE_GAME_MASSPORT_PLAN.md 7.5 "I-PHASE CONCRETE WRAM-L MAP"):
# shrink the resident 1216-slot objectEntityList (445,440 B @ WRAM-L 0x243000) to a
# camera-local pool of 288 PHYSICAL slots (RESERVE 64 + SCENE_PHYS 160 + TEMP 64 = 126,208 B,
# the freed ~306 KB + the remap/inverse/dormant tables seated in the in-place freed gap so
# every region below 0x2AFC00 stays put -- ZERO #249/#250 shift). The pool materializes only
# the camera-near + resident set; loop1 then iterates the 288 physical slots, not 1216 logical.
#
# THIS GATE is the GREEN criterion for that shrink, RED on the CURRENT (un-shrunk) build:
#   S1 boot healthy            cont_frames > 0                 (still boots; reuses R0)
#   S2 scan BOUNDED            scan_maxslot < 288              <-- the fps lever; RED now (1082)
#   S3 scan population sane    0 < scan_pop <= 288             RED now (765 populated of 1216)
#   S4 GHZ features intact     defer to qa_p6_ghz_regression   (run separately; must stay GREEN)
#   S5 no boot-trap            _end < P6_HW_ANIMPAK            (qa_p6_mapoverlap; layout safe)
# Reuses the perf witnesses (p6_w_scan_pop / p6_w_scan_maxslot, present in the shipping build).
#
#   python tools/_portspike/qa_p6_pool_shrink.py <savestate.mcs> [map]

import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

POOL_PHYS = 288   # RESERVE 64 + SCENE_PHYS 160 + TEMP 64 (whole-game-safe: peak near 101 + pins + headroom)


def main(argv):
    if len(argv) < 1:
        print("usage: qa_p6_pool_shrink.py <savestate.mcs> [map]"); return 2
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
    pop = peek("_p6_w_scan_pop")
    mx = peek("_p6_w_scan_maxslot")

    print("=== qa_p6_pool_shrink (camera-local pool 1216 -> 288 physical, mass-port I3b) ===")
    print("  cont_frames   = %s" % Q._dv(cont))
    print("  scan_pop      = %s   (populated physical slots; want 0 < pop <= %d)" % (Q._dv(pop), POOL_PHYS))
    print("  scan_maxslot  = %s   (highest populated slot; want < %d = pool bounded)" % (Q._dv(mx), POOL_PHYS))
    print("-" * 64)

    s1 = cont is not None and cont > 0
    s2 = mx is not None and 0 <= mx < POOL_PHYS
    s3 = pop is not None and 0 < pop <= POOL_PHYS
    for tag, ok, detail in [
        ("S1 boot healthy (cont_frames>0)", s1, Q._dv(cont)),
        ("S2 scan BOUNDED (maxslot<%d)" % POOL_PHYS, s2, Q._dv(mx)),
        ("S3 scan pop sane (0<pop<=%d)" % POOL_PHYS, s3, Q._dv(pop)),
    ]:
        print("  [%s] %s = %s" % ("GREEN" if ok else " RED ", tag, detail))

    if s1 and s2 and s3:
        print("RESULT: GREEN -- the pool is shrunk to <=%d physical slots; loop1 iterates the "
              "near+resident set, not 1216 logical. Run qa_p6_ghz_regression (S4) + qa_p6_mapoverlap "
              "(S5) to confirm features + layout." % POOL_PHYS)
        return 0
    print("RESULT: RED -- the pool is NOT yet shrunk (scan still spans the full 1216-slot table). "
          "Expected RED before I3b lands.")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
