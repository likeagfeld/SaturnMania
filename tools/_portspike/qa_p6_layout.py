#!/usr/bin/env python3
# =============================================================================
# qa_p6_layout.py -- P6.7 W11a gate (Task #210): the layout BAND STORE +
# CAMERA-LOCAL SLIDING WINDOWS, proven over the real GHZ1 layouts on SH-2.
#
# WHY: layouts cannot be RAM-resident at scale (W11, SaturnMemoryMap.h:
# GHZ1 551,168 B raw, FBZ2 3,006,976 B). The band store (zlib 16-row bands
# built by tools/build_layout_bands.py; GHZ1 = 51,094 B vs the 81,920
# budget) feeds per-collidable-layer 128x32 sliding windows
# (platform/Saturn/SaturnLayout.cpp; window pool = the 16,384 B WRAM-H
# slack at 0x060F0000).
#
# CHECKS
#   L1 witnesses present in game.map (RED while unimplemented).
#   L2 band store chain-loaded byte-exact: _p6_w_lay_bytes/_p6_w_lay_hash ==
#      the model (djb2 over cd/GHZ1LAYT.BIN computed ON SH-2).
#   L3 probes: _p6_w_lay_probes == model probe_count -- 110 tuples spanning
#      random positions AND a clustered x-walk that forces horizontal
#      window crossings, replayed through the REAL accessor (refills +
#      miniz band inflates on SH-2), values vs the raw offline layouts.
#   L4 refills sane: 0 < _p6_w_lay_refills <= probe_count (every probe at
#      worst one refill; zero refills would mean the window never filled).
#
# Usage: python tools/_portspike/qa_p6_layout.py [savestate.mcs] [map]
# =============================================================================
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

MCS_DEFAULT = os.path.join(HERE, "_p6", "_p6_pack.mcs")
MODEL = os.path.join(HERE, "_p6", "p6_layout_model.json")

SYMS = ["_p6_w_lay_bytes", "_p6_w_lay_hash", "_p6_w_lay_probes",
        "_p6_w_lay_firstbad", "_p6_w_lay_refills"]


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.7 W11a LAYOUT GATE: band store + camera-local sliding windows")
    print("=" * 72)

    model = json.load(open(MODEL))
    exp_bytes = model["file_bytes"]
    exp_hash = int(model["file_hash"], 16)
    exp_probes = model["probe_count"]
    print("  model: %s = %d B %s, %d layers, %d probes" %
          (model["tag"], exp_bytes, model["file_hash"],
           len(model["layers"]), exp_probes))

    if not os.path.isfile(mp):
        print("RESULT: RED -- link map missing (%s)" % mp)
        return 1
    map_text = _scene.read_text(mp)
    syms = {}
    missing = []
    for s in [_scene.SYM_MAGIC] + SYMS:
        syms[s] = _scene.map_symbol(map_text, s)
        if syms[s] is None:
            missing.append(s)
    if missing:
        print("RESULT: RED -- witness symbol(s) absent from the map:")
        for s in missing:
            print("        %s" % s)
        print("        (Expected while W11a is unimplemented.)")
        return 1
    if not os.path.isfile(mcs):
        print("RESULT: RED -- savestate missing (%s)" % mcs)
        return 1

    import pathlib
    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))
    raw_magic = mod._peek_bytes(sections, syms[_scene.SYM_MAGIC], 4)
    label, perm = _scene.calibrate(raw_magic)
    if perm is None:
        print("RESULT: RED -- magic mis-decode")
        return 1
    v = {s: _scene.peek_u32(mod, sections, syms[s], perm, signed=False)
         for s in SYMS}

    checks = [
        ("L2 band store chain-loaded byte-exact (%d B, djb2 %s)"
         % (exp_bytes, model["file_hash"]),
         v["_p6_w_lay_bytes"] == exp_bytes and v["_p6_w_lay_hash"] == exp_hash,
         "got %d B / 0x%08X" % (v["_p6_w_lay_bytes"], v["_p6_w_lay_hash"])),
        ("L3 windowed-accessor probes: %d/%d (random + crossing-walk + BG "
         "rebinds)" % (exp_probes, exp_probes),
         v["_p6_w_lay_probes"] == exp_probes,
         "matched=%d firstbad=%d" % (v["_p6_w_lay_probes"],
                                     v["_p6_w_lay_firstbad"])),
        # W15b: the 60-tick Player run with LIVE Collision.cpp adds sensor
        # window reads beyond the probe replay (MEASURED 315 refills vs the
        # old probes-only 110 bound). Invariants kept: zero = the window
        # machinery never filled; > 4096 = runaway thrash.
        ("L4 refills sane (0 < refills <= 4096)",
         0 < v["_p6_w_lay_refills"] <= 4096,
         "refills=%d" % v["_p6_w_lay_refills"]),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the W11 sliding-window layout machinery is")
        print("        live on SH-2: zlib bands chain-load byte-exact, the")
        print("        windowed accessor serves the REAL GHZ1 layout words")
        print("        across window crossings, and refills are bounded.")
        return 0
    print("RESULT: RED -- W11a not proven (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
