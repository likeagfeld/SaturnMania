#!/usr/bin/env python3
# Mass-port plan I3b.1 -- camera-local pool remap-table INFRASTRUCTURE (RED-first).
#
# GOAL (plan 7.5 I3b, the first GHZ-green build step of the pool shrink): route
# SaturnSlotToPoolSlot (Object.hpp) through a real cart-resident remap table
# (p6_pool_remap @ the verified-free cart slot 0x226B8000), initialized to IDENTITY by
# p6_pool_remap_init() first thing in the boot. This is BYTE-IDENTICAL (identity remap ->
# the same pool slot) -- it de-risks the cart-read + the table indirection at RUNTIME
# before the actual SHRINK populates the table non-trivially (and the WRAM-L pool backing
# is shrunk). The ready-flag gate makes a pre-init access return the raw slot (crash-safe).
#
# GREEN criteria:
#   I1 boots               cont_frames > 0          (the init + cart-read didn't fault)
#   I2 table identity+read  p6_w_remap_ok == 1      (p6_pool_remap[i]==i for all, read back)
#   I3 BYTE-IDENTICAL       p6_w_i2_resolve_ok == 1 (SaturnEntityAt via the table == the
#                                                    direct dual-stride oracle, all slots)
# RED now: the witnesses are absent (unbuilt). GREEN once I3b.1 lands AND stays byte-identical.
#
#   python tools/_portspike/qa_p6_i3b.py <savestate.mcs> [map]

import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q


def main(argv):
    if len(argv) < 1:
        print("usage: qa_p6_i3b.py <savestate.mcs> [map]"); return 2
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
    remap = peek("_p6_w_remap_ok")
    resolve = peek("_p6_w_i2_resolve_ok")

    print("=== qa_p6_i3b (camera-local pool remap-table infra, identity = byte-identical) ===")
    print("  cont_frames      = %s" % Q._dv(cont))
    print("  remap_ok         = %s   (1 = cart table identity + readable @ 0x226B8000)" % Q._dv(remap))
    print("  i2_resolve_ok    = %s   (1 = SaturnEntityAt via the table == direct oracle, byte-identical)" % Q._dv(resolve))
    print("-" * 64)

    if remap is None or resolve is None:
        print("RED: I3b.1 witness(es) absent -- the remap table is not built yet (expected RED pre-I3b.1).")
        return 1
    i1 = cont is not None and cont > 0
    i2 = remap == 1
    i3 = resolve == 1
    for tag, ok in [("I1 boots (cont_frames>0)", i1), ("I2 table identity+read (remap_ok==1)", i2),
                    ("I3 byte-identical (resolve_ok==1)", i3)]:
        print("  [%s] %s" % ("GREEN" if ok else " RED ", tag))
    if i1 and i2 and i3:
        print("RESULT: GREEN -- SaturnSlotToPoolSlot routes through the cart remap table, identity =>"
              " byte-identical, boots clean. The pool SHRINK can now populate the table non-trivially.")
        return 0
    print("RESULT: RED -- the remap-table infra is not byte-identical / not booting (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
