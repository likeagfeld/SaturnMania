#!/usr/bin/env python3
# Mass-port plan I2 -- SaturnEntityAt INDIRECTION pass-through (RED-first).
#
# GOAL (plan 7.5 I2): route the single entity-access indirection point
# SaturnEntityAt(slot) (Object.hpp:429) through a slot->pool-slot mapping that is
# 1:1 with the existing dual-stride pool, so I3 can later remap far slots to dormant
# records WITHOUT touching the 211 RSDK_GET_ENTITY call sites. I2 must be
# BEHAVIOUR-IDENTICAL (GHZ byte-identical) and add ZERO new allocation -- the stored
# remap table waits for I3 (when the pool-shrink frees its verified home; placing a
# table now is the BSS-overflow-class risk the I1 note + binding memory forbid).
#
# RESOLVED DESIGN (the indirection is a FUNCTION, not a stored table, in I2):
#   inline int32 SaturnSlotToPoolSlot(int32 slot) { return slot; }   // identity in I2
#   SaturnEntityAt(slot) computes the dual-stride offset for SaturnSlotToPoolSlot(slot).
#   A one-shot load-time self-check walks every slot in [0,ENTITY_COUNT) and asserts
#   SaturnEntityAt(slot) == the pre-I2 direct offset, latching:
#     p6_w_i2_resolve_ok = 1  iff ALL slots resolve to their original address.
#   I3 replaces SaturnSlotToPoolSlot's body with a table lookup (table placed in the
#   pool-shrink-freed home); the same self-check then gates the remap.
#
# This gate is RED now (the witness symbol is absent -- I2 unimplemented) and GREEN
# once I2 lands AND every slot still resolves 1:1 (resolve_ok == 1). Pair it with the
# existing qa_p6_manifest (n==1041) + qa_p6_layout (110/110 byte-exact) gates for the
# full GHZ-byte-identical proof.
#
#   python tools/_portspike/qa_p6_i2.py              # boot+capture+verdict
#   python tools/_portspike/qa_p6_i2.py --mcs X.mcs  # evaluate an existing capture

import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

GATE_FRAME = 130.0     # wall-clock s: GHZ live (matches qa_p6_manifest capture point)
ROOT       = os.path.normpath(os.path.join(HERE, "..", ".."))
SAVESTATE  = os.path.join(ROOT, "tools", "qa_savestate.ps1")
TMP_MCS    = os.path.join(HERE, "_p6_i2.mcs")

NAMES = ["_p6_w_cont_frames", "_p6_w_i2_resolve_ok",
         "_p6_w_manifest_n", "_p6_w_manifest_maxslot"]


def capture(out):
    lck = os.path.join(ROOT, ".mednafen", "mednafen.lck")
    try:
        os.remove(lck)
    except OSError:
        pass
    cmd = ["pwsh", SAVESTATE, "-Cue", "game.cue", "-SaveFrame", str(GATE_FRAME), "-Out", out]
    r = subprocess.run(cmd, capture_output=True, text=True)
    sys.stdout.write(r.stdout[-400:] if r.stdout else "")
    if not os.path.exists(out):
        print("FAIL: capture produced no savestate"); return False
    return True


def main(argv):
    mcs = None
    if "--mcs" in argv:
        mcs = argv[argv.index("--mcs") + 1]
    if mcs is None:
        mcs = TMP_MCS
        if not capture(mcs):
            return 1

    mod = Q.load_harness()
    map_text = Q.read_text(Q.MAP_DEFAULT)
    sections = mod.parse_savestate(Q._as_path(mcs))
    magic_addr = Q.map_symbol(map_text, "_p6_w_magic")
    raw = mod._peek_bytes(sections, magic_addr, 4) if magic_addr else None
    _, perm = Q.calibrate(raw)
    if perm is None:
        print("RED: magic uncalibrated (image not loaded / wrong bank)"); return 1

    v = {}
    for n in NAMES:
        a = Q.map_symbol(map_text, n)
        v[n] = Q.peek_u32(mod, sections, a, perm, signed=True) if a else None

    print("=== qa_p6_i2 (SaturnEntityAt indirection pass-through, mass-port I2) ===")
    print("  mcs            = %s" % os.path.basename(mcs))
    print("  cont_frames    = %s" % Q._dv(v["_p6_w_cont_frames"]))
    print("  manifest n     = %s  maxslot = %s (sanity: GHZ1 1041/1040)"
          % (Q._dv(v["_p6_w_manifest_n"]), Q._dv(v["_p6_w_manifest_maxslot"])))
    print("  i2_resolve_ok  = %s   (expect 1 = ALL slots resolve 1:1 via the indirection)"
          % Q._dv(v["_p6_w_i2_resolve_ok"]))

    ok = v["_p6_w_i2_resolve_ok"]
    if ok is None:
        print("RED: i2_resolve_ok witness absent -- I2 indirection not implemented yet "
              "(expected RED pre-I2)."); return 1
    if ok == 1:
        print("RESULT: GREEN -- SaturnEntityAt routes through the 1:1 indirection; every "
              "slot resolves to its original address (behaviour-identical).")
        return 0
    print("RESULT: RED -- i2_resolve_ok=%s != 1: the indirection diverged from the direct "
          "pool offset for >=1 slot (NOT byte-identical)." % Q._dv(ok))
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
