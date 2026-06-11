#!/usr/bin/env python3
# =============================================================================
# qa_p6_collision.py -- P6.7 collision-packer gate (Task #210): the 4-bit
# PACKED collision residency + EXACT on-the-fly flip accessors, proven over
# the REAL GHZ TileConfig.bin loaded through the engine's own LoadTileConfig
# on SH-2.
#
# WHY: raw x1 collisionMasks are 131,072 B + tileInfo 10,240 B -- the
# declared W2 residency gap. The packed format (16-bit/column: rich nibble +
# 3 sentinels + lWall/rWall nibbles + yFlip bit; gen_collision_model.py
# header) stores the same EXACT geometry in 65,536 B, and the Saturn
# accessors derive FLIP_X/Y/XY masks and angles on the fly via the PC
# pre-expansion formulas (Scene.cpp:889-963) -- retiring the
# COLLISION_TILE_MASK base-mask approximation (Task #203) completely.
#
# Offline model provenance: raw 0x46E71BE8 + info 0x43F6D7C5 == the P6.7d.2
# precomputed contract hashes, reproduced from source by
# gen_collision_model.py (self-tests C1 round-trip over every column, C2
# end-to-end buffer walk, C3 oracle).
#
# CHECKS
#   K1 witnesses present in game.map (RED while unimplemented).
#   K2 the engine loaded GHZ TileConfig from the pack: _p6_w_col_loaded == 1.
#   K3 packed buffer byte-exact: _p6_w_col_packedhash == model packed_hash
#      (djb2 over 65,536 B computed ON SH-2 over the placed window).
#   K4 tileInfo byte-exact: _p6_w_col_infohash == model info_hash (djb2 over
#      10,240 B -- the same 0x43F6D7C5 the contract recorded).
#   K5 accessor probes: _p6_w_col_probes == 128 (96 mask + 32 angle tuples
#      across all four flip variants replayed through the REAL Saturn
#      accessors; first mismatch index reported via _p6_w_col_firstbad).
#
# Usage: python tools/_portspike/qa_p6_collision.py [savestate.mcs] [map]
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
MODEL = os.path.join(HERE, "_p6", "p6_collision_model.json")

EXP_PROBES = 128

SYMS = ["_p6_w_col_loaded", "_p6_w_col_packedhash", "_p6_w_col_infohash",
        "_p6_w_col_probes", "_p6_w_col_firstbad"]


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.7 COLLISION GATE: packed residency + exact flip accessors")
    print("=" * 72)

    model = json.load(open(MODEL))
    exp_packed = int(model["packed_hash"], 16)
    exp_info = int(model["info_hash"], 16)
    print("  model: packed %d B %s | info %d B %s | raw provenance %s "
          "(== P6.7d.2 contract)" %
          (model["packed_bytes"], model["packed_hash"],
           model["info_bytes"], model["info_hash"], model["raw_hash"]))

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
        print("        (Expected while the packer is unimplemented.)")
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
        ("K2 engine loaded GHZ TileConfig from the pack",
         v["_p6_w_col_loaded"] == 1, "loaded=%d" % v["_p6_w_col_loaded"]),
        ("K3 packed buffer byte-exact on SH-2 (djb2 %s over %d B)"
         % (model["packed_hash"], model["packed_bytes"]),
         v["_p6_w_col_packedhash"] == exp_packed,
         "got 0x%08X" % v["_p6_w_col_packedhash"]),
        ("K4 tileInfo byte-exact (djb2 %s over %d B)"
         % (model["info_hash"], model["info_bytes"]),
         v["_p6_w_col_infohash"] == exp_info,
         "got 0x%08X" % v["_p6_w_col_infohash"]),
        ("K5 accessor probes: %d/%d across FLIP_NONE/X/Y/XY" %
         (EXP_PROBES, EXP_PROBES),
         v["_p6_w_col_probes"] == EXP_PROBES,
         "matched=%d firstbad=%d" % (v["_p6_w_col_probes"],
                                     v["_p6_w_col_firstbad"])),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the packed collision residency holds the")
        print("        EXACT LoadTileConfig geometry in 65,536 B and the")
        print("        Saturn accessors reproduce every flip variant of")
        print("        masks AND angles -- the Task #203 base-mask")
        print("        approximation is retired; W2 residency is closed at")
        print("        the contract size.")
        return 0
    print("RESULT: RED -- packed collision not proven (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
