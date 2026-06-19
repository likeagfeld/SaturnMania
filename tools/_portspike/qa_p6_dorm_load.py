#!/usr/bin/env python3
# Mass-port plan I3b streaming shrink, increment 2a -- DORMANT STORE LOADS TO CART.
#
# The offline dormant store (cd/<TAG>DORM.BIN, build_dormant_store.py) is chain-loaded to the
# verified-free cart slot 0x226C8000 at scene-arm, as DATA (zero WRAM-H code -- the runtime-capture
# form was #228-blocked). This gate proves that DATA PATH (the foundation the increment-2 materialize
# reads), RED on the current build (witnesses absent) until the load lands:
#
#   D1 boot healthy     cont_frames > 0
#   D2 store loaded     dorm_bytes > 0  (GFS read the .BIN into cart)
#   D3 header parsed    dorm_magic == 0x4D443650 ('P6DM')
#   D4 index sized      dorm_slots == EXPECT_SLOTS (GHZ1 = 1041, matches build_dormant_store + census)
#
#   python tools/_portspike/qa_p6_dorm_load.py <savestate.mcs> [map]

import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

MAGIC = 0x4D443650          # 'P6DM'
EXPECT_SLOTS = 1041         # GHZ1 scene entities (build_dormant_store: GHZ1DORM.BIN, == scene_census)


def main(argv):
    if len(argv) < 1:
        print("usage: qa_p6_dorm_load.py <savestate.mcs> [map]"); return 2
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
    by = peek("_p6_w_dorm_bytes")
    mg = peek("_p6_w_dorm_magic")
    sl = peek("_p6_w_dorm_slots")
    mg_u = (mg & 0xFFFFFFFF) if mg is not None else None

    print("=== qa_p6_dorm_load (I3b 2a -- offline dormant store -> cart data path) ===")
    print("  cont_frames = %s" % Q._dv(cont))
    print("  dorm_bytes  = %s   (GFS bytes of GHZ1DORM.BIN; want > 0, ~39530)" % Q._dv(by))
    print("  dorm_magic  = %s   (want 0x%08X 'P6DM')" % (("0x%08X" % mg_u) if mg_u is not None else "None", MAGIC))
    print("  dorm_slots  = %s   (want %d = GHZ1 scene entities)" % (Q._dv(sl), EXPECT_SLOTS))
    print("-" * 64)

    d1 = cont is not None and cont > 0
    d2 = by is not None and by > 0
    d3 = mg_u == MAGIC
    d4 = sl == EXPECT_SLOTS
    for tag, ok, detail in [
        ("D1 boot healthy (cont>0)", d1, Q._dv(cont)),
        ("D2 store loaded (bytes>0)", d2, Q._dv(by)),
        ("D3 header parsed (magic P6DM)", d3, ("0x%08X" % mg_u) if mg_u is not None else "None"),
        ("D4 index sized (slots==%d)" % EXPECT_SLOTS, d4, Q._dv(sl)),
    ]:
        print("  [%s] %s = %s" % ("GREEN" if ok else " RED ", tag, detail))

    if d1 and d2 and d3 and d4:
        print("RESULT: GREEN -- the offline dormant store ships + loads into cart correctly (%s B, "
              "magic OK, %d-slot index). The increment-2 materialize can read placements from it." % (Q._dv(by), EXPECT_SLOTS))
        return 0
    print("RESULT: RED -- the dormant store did not load/parse (witnesses absent or wrong). Expected "
          "before 2a lands; after, GHZ1DORM.BIN must be in cd/ + the ISO.")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
