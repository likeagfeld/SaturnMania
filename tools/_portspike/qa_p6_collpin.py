#!/usr/bin/env python3
# =============================================================================
# qa_p6_collpin.py -- Task #249/#250 corruption PIN diagnostic (NOT a gate).
#
# The qa_p6_collgeom gate proves the packed-collision geometry @0x060E0000 is
# corrupt at CAPTURE time (RED) when the resident pre-inflate is active. This
# diagnostic answers the decisive follow-up: WHEN does it corrupt?
#
#   p6_w_col_t1hash  = djb2 of 0x060E0000 RIGHT AFTER the gameplay LoadTileConfig
#                      (nested in p6_scene_load_and_arm's LoadSceneFolder).
#   p6_w_col_nowhash = the live djb2 at the latest frame (frozen once pinned).
#   p6_w_col_badframe = the FIRST continuous frame the live hash diverged from
#                       the GROUNDED golden 0x643A3A5D (-1 = never diverged).
#   GOLDEN djb2       = 0x643A3A5D (computed over p6_collgeom_golden.bin).
#
# VERDICT:
#   * t1hash != GOLDEN          -> packed WRONG at load: the resident pre-inflate
#                                  disturbs LoadTileConfig's 0x060E0000 packing.
#                                  (badframe will be ~1, the first frame.)
#   * t1hash == GOLDEN, badframe>1 -> packed RIGHT, a per-frame loop writer
#                                  clobbers the table at frame `badframe`.
#   * t1hash == GOLDEN, badframe<0, collgeom GREEN -> not reproduced this run.
#
# Also prints the continuous player Y (grounded vs fell-through) + lay_resident.
#
# Usage: python tools/_portspike/qa_p6_collpin.py <savestate.mcs> [game.map]
# =============================================================================
import importlib.util
import os
import pathlib
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GOLDEN_DJB2 = 0x643A3A5D
GOLDEN_BIN = os.path.join(HERE, "_p6", "p6_collgeom_golden.bin")

_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

SYMS = [
    "_p6_lean_boot",
    "_p6_w_cont_frames",
    "_p6_w_cont_plr_x", "_p6_w_cont_plr_y",
    "_p6_w_lay_resident",
    "_p6_w_col_t1hash", "_p6_w_col_nowhash", "_p6_w_col_badframe",
]


def u32(v):
    return None if v is None else (v & 0xFFFFFFFF)


def unswap(b):
    o = bytearray(len(b))
    for i in range(0, len(b) - 1, 2):
        o[i] = b[i + 1]
        o[i + 1] = b[i]
    return bytes(o)


def main(argv):
    if len(argv) < 2:
        print("usage: qa_p6_collpin.py <savestate.mcs> [game.map]")
        return 2
    mcs = _scene._as_path(argv[1])
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("TASK #249/#250 PIN: WHEN does packed collision @0x060E0000 corrupt?")
    print("=" * 72)
    if not os.path.isfile(mp):
        print("RED -- link map missing (%s)" % mp)
        return 1
    if not os.path.isfile(mcs):
        print("RED -- savestate missing (%s)" % mcs)
        return 1

    map_text = _scene.read_text(mp)
    syms = {s: _scene.map_symbol(map_text, s) for s in [_scene.SYM_MAGIC] + SYMS}
    missing = [s for s in SYMS if syms[s] is None]

    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))
    raw_magic = mod._peek_bytes(sections, syms[_scene.SYM_MAGIC], 4)
    label, perm = _scene.calibrate(raw_magic)
    if perm is None:
        print("RED -- magic mis-decode")
        return 1

    v = {}
    for s in SYMS:
        v[s] = (None if syms[s] is None
                else _scene.peek_u32(mod, sections, syms[s], perm, signed=True))

    t1 = u32(v["_p6_w_col_t1hash"])
    now = u32(v["_p6_w_col_nowhash"])
    badframe = v["_p6_w_col_badframe"]
    cont = v["_p6_w_cont_frames"]
    res = v["_p6_w_lay_resident"]
    lean = v["_p6_lean_boot"]
    px = v["_p6_w_cont_plr_x"]
    py = v["_p6_w_cont_plr_y"]

    # t3: the actual 0x060E0000 bytes at capture vs golden (same as the gate).
    t3_miss = None
    if os.path.isfile(GOLDEN_BIN):
        golden = open(GOLDEN_BIN, "rb").read()
        cap = unswap(mod._peek_bytes(sections, 0x060E0000, 0x10000))
        t3_miss = sum(1 for i in range(len(golden)) if cap[i] != golden[i])
        t3_first = next((i for i in range(len(golden)) if cap[i] != golden[i]), -1)

    def hx(x):
        return "None" if x is None else ("0x%08X" % x)

    print("  lean_boot         = %s" % lean)
    print("  cont_frames       = %s" % cont)
    print("  lay_resident      = %s  (layers pre-inflated to cart; expect 4-5)" % res)
    print("  GOLDEN djb2       = 0x%08X" % GOLDEN_DJB2)
    print("  t1 (post-pack)    = %s  %s"
          % (hx(t1), "" if t1 is None
             else ("== golden" if t1 == GOLDEN_DJB2 else "!= GOLDEN  <-- packed WRONG at load")))
    print("  nowhash (latest)  = %s  %s"
          % (hx(now), "" if now is None
             else ("== golden" if now == GOLDEN_DJB2 else "!= golden")))
    print("  badframe          = %s  (first frame live hash != golden; -1 = never)" % badframe)
    if t3_miss is not None:
        print("  t3 (capture bytes)= %d / 65536 mismatches%s"
              % (t3_miss, "" if t3_miss == 0 else (" (first @ +0x%X)" % t3_first)))
    if py is not None:
        grounded = (0 <= py <= (1004 << 16))
        print("  player            = (%s, %s)  [y=%d px] %s"
              % (px, py, (py >> 16),
                 "GROUNDED" if grounded else "FELL THROUGH <-- no floor"))
    if missing:
        print("  NOTE missing syms: %s" % ", ".join(missing))
    print("-" * 72)

    # Verdict.
    if t1 is None:
        print("VERDICT: INCONCLUSIVE -- t1hash symbol unresolved/zero (build/map).")
        return 1
    if t1 != GOLDEN_DJB2:
        print("VERDICT: PACKED-WRONG-AT-LOAD. The resident pre-inflate (Mount,")
        print("         BEFORE LoadTileConfig) corrupts the 0x060E0000 packing.")
        print("         FIX DIRECTION: isolate/defer the pre-inflate's heap+scratch")
        print("         so LoadTileConfig's inflate is undisturbed (or run the")
        print("         pre-inflate AFTER LoadTileConfig).")
        return 0
    if badframe is not None and badframe > 0:
        print("VERDICT: PACKED-RIGHT, CLOBBERED-AT-FRAME %d. A per-frame loop" % badframe)
        print("         writer overruns into 0x060E0000 after a correct pack.")
        print("         FIX DIRECTION: find the frame-%d writer (resident Refill /" % badframe)
        print("         present / object scan) whose destination underflows the table.")
        return 0
    if t3_miss == 0:
        print("VERDICT: NOT REPRODUCED this run (t1 + capture both == golden).")
        return 0
    print("VERDICT: t1==golden but capture corrupt with badframe<=0 -- recheck")
    print("         badframe wiring (the pin should have fired).")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
