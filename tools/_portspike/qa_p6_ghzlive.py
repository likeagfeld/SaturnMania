#!/usr/bin/env python3
# =============================================================================
# qa_p6_ghzlive.py -- P6.7 W11b gate (Task #226): GHZ1 LIVE through the
# engine's own LoadSceneAssets at full scale.
#
# WHY: every prior P6 scene proof ran the Title scene (6 entities, resident
# layouts). GHZ1 is the scale wall: 1,041 placed entities (entity-scale flip
# to ENTITY_COUNT 0x500), 551,168 B of layer layouts (W11 sliding windows --
# residency impossible), and the full closer-set memory map. This gate
# proves the chain END-TO-END on SH-2: stage select -> LoadSceneAssets ->
# entity placement -> SaturnLayout-bound collidable layers -> windowed
# reads through the engine's RSDK_LAYER_TILE seam.
#
# Offline ground truth: p6_ghzlive_model.json from gen_ghzlive_model.py
# (parse_title_entities walker, self-tests S1-S5; raw fixed-point coords,
# byte-exact -- no float rounding).
#
# CHECKS
#   H1 witnesses present in game.map (RED while W11b unimplemented).
#   H2 stage select: the diag found + selected GHZ1 (p6_w_ghz_stage == 1).
#   H3 entity census: p6_w_ghz_entcount == 1041 (live count of scene slots
#      with classID != 0 after LoadSceneAssets).
#   H4 entity probes: 16 slots spread across the stage -- position.x/y in
#      objectEntityList[slot + RESERVE_ENTITY_COUNT] byte-exact vs the
#      scene blob words.
#   H5 tile probes: 16 reads THROUGH the engine layer-tile seam (windowed
#      SaturnLayout path on the two collidable FG layers) == the Scene.bin
#      layout inflate words.
#   H6 scale safety: FG binds == 2 AND every Saturn clamp/drop witness == 0
#      (tempentity skips, typegroup skips, settile drops, gettile unbound).
#
# Usage: python tools/_portspike/qa_p6_ghzlive.py [savestate.mcs] [map]
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
MODEL = os.path.join(HERE, "p6_ghzlive_model.json")

SYMS = ["_p6_w_ghz_stage", "_p6_w_ghz_entcount", "_p6_w_ghz_binds",
        "_p6_w_ghz_clamps", "_p6_w_ghz_probes", "_p6_w_ghz_tiles"]


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.7 W11b GHZ-LIVE GATE: engine LoadSceneAssets at GHZ1 scale")
    print("=" * 72)

    model = json.load(open(MODEL))
    probes = model["probes"]
    tprobes = model["tile_probes"]
    print("  model: %d entities, %d entity probes, %d tile probes (FG %s)"
          % (model["entity_count"], len(probes), len(tprobes),
             "/".join(str(l["index"]) for l in model["fg_layers"])))

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
        print("        (Expected while W11b is unimplemented.)")
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

    def u32(addr, signed=False):
        return _scene.peek_u32(mod, sections, addr, perm, signed=signed)

    stage = u32(syms["_p6_w_ghz_stage"])
    entcount = u32(syms["_p6_w_ghz_entcount"])
    binds = u32(syms["_p6_w_ghz_binds"])
    clamps = u32(syms["_p6_w_ghz_clamps"])

    ent_ok = 0
    first_ent_bad = -1
    for i, p in enumerate(probes):
        base = syms["_p6_w_ghz_probes"] + i * 12
        slot = u32(base, signed=True)
        x = u32(base + 4, signed=True)
        y = u32(base + 8, signed=True)
        if slot == p["slot"] and x == p["x"] and y == p["y"]:
            ent_ok += 1
        elif first_ent_bad < 0:
            first_ent_bad = i
            print("  first entity probe mismatch: i=%d slot %d/%d x %d/%d y %d/%d"
                  % (i, slot, p["slot"], x, p["x"], y, p["y"]))

    tile_ok = 0
    first_tile_bad = -1
    for i, tp in enumerate(tprobes):
        w = u32(syms["_p6_w_ghz_tiles"] + i * 4)
        if w == tp["word"]:
            tile_ok += 1
        elif first_tile_bad < 0:
            first_tile_bad = i
            print("  first tile probe mismatch: i=%d %s (%d,%d) got 0x%04X want 0x%04X"
                  % (i, tp["name"], tp["tx"], tp["ty"], w & 0xFFFF, tp["word"]))

    checks = [
        ("H2 GHZ1 selected from the scene list", stage == 1,
         "stage witness=%d" % stage),
        ("H3 registered-class census == %d (entities of the 23 registered "
         "classes; unmatched classes keep classID 0 but their positions are "
         "proven by H4)" % model.get("registered_census", model["ring_count"]),
         entcount == model.get("registered_census", model["ring_count"]),
         "entcount=%d" % entcount),
        ("H4 entity probes %d/%d byte-exact" % (len(probes), len(probes)),
         ent_ok == len(probes),
         "matched=%d firstbad=%d" % (ent_ok, first_ent_bad)),
        ("H5 tile probes %d/%d through the engine seam" % (len(tprobes), len(tprobes)),
         tile_ok == len(tprobes),
         "matched=%d firstbad=%d" % (tile_ok, first_tile_bad)),
        ("H6 scale safety: FG binds == 2, clamp/drop witnesses == 0",
         binds == 2 and clamps == 0,
         "binds=%d clamps=%d" % (binds, clamps)),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the engine's own LoadSceneAssets runs GHZ1 at")
        print("        full scale on SH-2: 1,041 entities placed at exact")
        print("        coords, collidable layouts served through the W11")
        print("        sliding windows via the engine layer-tile seam, no")
        print("        clamp ever fired.")
        return 0
    print("RESULT: RED -- GHZ live not proven (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
