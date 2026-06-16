#!/usr/bin/env python3
# =============================================================================
# qa_p6_collgeom.py -- Task #249/#250 gate: the GHZ packed collision GEOMETRY
# (packedCollisionMasks @ WRAM-H 0x060E0000, 64 KB) must be byte-exact.
#
# ROOT CAUSE (measured, qa_p6_collpin triangulation): the resident layout
# pre-inflate (the #249 band-crossing stall fix) ran at boot INSIDE
# SaturnLayout_Mount -- BEFORE the gameplay LoadTileConfig packed 0x060E0000.
# Running it first corrupted the pack (every real collision cell mis-encoded,
# 10731/10746) so Sonic/Tails fell through the floor. FIX: defer the pre-inflate
# to SaturnLayout_PreInflateResident(), called at the END of
# p6_scene_load_and_arm AFTER the pack is finalized. The pre-inflate then fills
# the cart resident store with the collision table already correct.
#
# This gate fingerprints a capture's 0x060E0000 with the SAME djb2 the firmware
# computes for p6_w_col_t1hash and compares it to the GROUNDED golden hash. It is
# map-INDEPENDENT (fixed WRAM-H address), pair-swap-corrected (mcs_extract WRAM
# swap, Task #136), and carries NO copyrighted-derived blob -- only the 4-byte
# fingerprint (the collision table itself derives from retail TileConfig.bin and
# is never committed). It fires RED on a corrupt pack and GREEN on a correct one.
#
#   GREEN: djb2 == 0x643A3A5D  -> collision geometry intact -> floor present.
#   RED:   djb2 != 0x643A3A5D  -> geometry corrupted -> fall-through.
#
# Usage: python tools/_portspike/qa_p6_collgeom.py <savestate.mcs>
# =============================================================================
import importlib.util
import os
import pathlib
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ADDR = 0x060E0000
N = 0x10000  # 64 KB packedCollisionMasks window
GOLDEN_DJB2 = 0x643A3A5D  # djb2 of the GROUNDED-build 0x060E0000 (firmware basis)


def unswap(b):
    o = bytearray(len(b))
    for i in range(0, len(b) - 1, 2):
        o[i] = b[i + 1]
        o[i + 1] = b[i]
    return bytes(o)


def djb2(b):
    h = 5381
    for x in b:
        h = (((h << 5) + h) ^ x) & 0xFFFFFFFF
    return h


def main(argv):
    if len(argv) < 2:
        print("usage: qa_p6_collgeom.py <savestate.mcs>")
        return 2
    mcs = argv[1]
    print("=" * 72)
    print("TASK #249/#250 GATE: GHZ packed collision GEOMETRY byte-exact "
          "(0x060E0000)")
    print("=" * 72)
    if not os.path.isfile(mcs):
        print("RESULT: RED -- savestate missing (%s)" % mcs)
        return 1

    spec = importlib.util.spec_from_file_location(
        "mcs", os.path.join(HERE, "..", "mcs_extract.py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    sec = m.parse_savestate(pathlib.Path(mcs))
    cap = unswap(m._peek_bytes(sec, ADDR, N))
    h = djb2(cap)

    print("  capture[0:16] = %s" % cap[:16].hex())
    print("  capture djb2  = 0x%08X" % h)
    print("  golden  djb2  = 0x%08X" % GOLDEN_DJB2)
    print("-" * 72)
    if h == GOLDEN_DJB2:
        print("RESULT: GREEN -- collision geometry byte-exact; floor present.")
        return 0
    print("RESULT: RED -- collision geometry CORRUPTED; "
          "the player will fall through.")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
