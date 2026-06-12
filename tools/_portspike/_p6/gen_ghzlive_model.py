#!/usr/bin/env python3
# =============================================================================
# gen_ghzlive_model.py -- P6.7 W11b (Task #226): offline ground-truth model
# for the GHZ-LIVE gate (qa_p6_ghzlive.py).
#
# Source of truth: extracted/Data/Stages/GHZ/Scene1.bin parsed by the proven
# tools/parse_title_entities.py walker (the same parser that produced the
# 1,041-entity census in qa_p6_memmap M2 and the W11 layout census).
#
# Emits:
#   p6_ghzlive_model.json  -- {entity_count, probes:[{slot,x,y}]} with x/y as
#                             the RAW fixed-point int32 words from the scene
#                             blob (byte-exact comparison, no float rounding)
#   p6_ghzlive_probes.inc  -- the probe slot list for the diag witness fill
#                             (p6_io_main.cpp copies objectEntityList[slot]
#                             position into p6_w_ghz_probes at these slots)
#
# Self-tests (S1-S4) run on every invocation; non-zero exit on any failure.
# =============================================================================
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
SCENE = os.path.join(ROOT, "extracted", "Data", "Stages", "GHZ", "Scene1.bin")
PROBE_COUNT = 16

spec = importlib.util.spec_from_file_location(
    "pte", os.path.join(ROOT, "tools", "parse_title_entities.py"))
pte = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pte)

spec2 = importlib.util.spec_from_file_location(
    "blb", os.path.join(ROOT, "tools", "build_layout_bands.py"))
blb = importlib.util.module_from_spec(spec2)
spec2.loader.exec_module(blb)


def main():
    objects, consumed, total = pte.parse_entities(SCENE)
    ents = []
    for o in objects:
        for e in o["entities"]:
            ents.append({"slot": e["slot"], "x": e["x"], "y": e["y"],
                         "object": o["name"]})
    ents.sort(key=lambda e: e["slot"])

    # S1: parser consumed the whole blob (same proof as the census runs)
    if consumed != total:
        print("S1 FAIL: parser consumed %d of %d bytes" % (consumed, total))
        return 1
    # S2: the known measured census (qa_p6_memmap M2, 2026-06-10) -- if the
    # scene file changes underneath us this fires instead of silently
    # generating a stale model.
    if len(ents) != 1041:
        print("S2 FAIL: GHZ1 entity count %d != measured 1041" % len(ents))
        return 1
    # S3: slots unique
    slots = [e["slot"] for e in ents]
    if len(set(slots)) != len(slots):
        print("S3 FAIL: duplicate slots in scene entity table")
        return 1

    # Evenly-spread probe picks across the slot-sorted list (first, last,
    # and 14 interior strides) -- spans the whole stage, not one cluster.
    picks = [ents[(i * (len(ents) - 1)) // (PROBE_COUNT - 1)]
             for i in range(PROBE_COUNT)]
    # S4: picks unique by slot (guaranteed by S3 + distinct indices, checked
    # anyway so a future PROBE_COUNT change cannot alias)
    if len(set(p["slot"] for p in picks)) != PROBE_COUNT:
        print("S4 FAIL: probe picks alias")
        return 1

    # Engine-seam tile probes: expected layout words for the two COLLIDABLE
    # FG layers, read straight from the Scene.bin inflate (the same bytes
    # build_layout_bands.py banded -- W11a gate L2 proved the band store
    # byte-exact against this). The diag reads these THROUGH the engine's
    # RSDK_LAYER_TILE seam (windowed SaturnLayout path), proving the
    # Scene.cpp Saturn arm + bind + accessor chain end-to-end.
    # Registered-class census: Scene.cpp leaves classID 0 for unmatched
    # objects but STILL writes every position (the probes prove that for all
    # 1,041 slots). The live witness counts classID != 0, so its expectation
    # is the entity count over the ACTUALLY-REGISTERED class set (Player
    # wave, Task #227: 23 classes; Player/GameOver/ImageTrail are refused by
    # the Object.cpp Saturn entity-stride guard until that wall closes --
    # keep this list in lockstep with qa_p6_stagecfg.REGISTERED).
    REGISTERED_GAME = ["Ring", "BoundsMarker", "Camera", "DebugMode",
                       "DrawHelpers", "Dust", "ERZStart", "GameOver", "HUD",
                       "Ice", "ImageTrail",
                       "Localization", "LogHelpers", "MathHelpers", "Music",
                       "Options", "PauseMenu", "Player", "SaveGame",
                       "ScoreBonus", "Shield", "SizeLaser", "Soundboard",
                       "Zone"]
    reg_hashes = {pte.rsdk_hash(n) for n in REGISTERED_GAME}
    registered_census = sum(len(o["entities"]) for o in objects
                            if o["hash"] in reg_hashes)
    ring_hash = pte.rsdk_hash("Ring")
    ring_count = sum(len(o["entities"]) for o in objects if o["hash"] == ring_hash)
    # S6: the known measured census (P6.7b: largest class = 446 rings)
    if ring_count != 446:
        print("S6 FAIL: GHZ1 Ring count %d != measured 446" % ring_count)
        return 1

    layers = blb.parse_layers(SCENE)
    fg = [(i, n, xs, ys, lay) for i, (n, xs, ys, lay) in enumerate(layers)
          if n in ("FG Low", "FG High")]
    # S5: both collidable layers present by name
    if len(fg) != 2:
        print("S5 FAIL: FG Low/High not found (layers: %s)" % [l[0] for l in layers])
        return 1
    tile_probes = []
    for (li, name, xs, ys, lay) in fg:
        for tx, ty in [(1, 1), (xs // 4, ys // 2), (xs // 2, 1),
                       (3 * xs // 4, ys - 2)]:
            word = lay[(ty * xs + tx) * 2] | (lay[(ty * xs + tx) * 2 + 1] << 8)
            tile_probes.append({"layer": li, "name": name, "tx": tx, "ty": ty,
                                "word": word})

    model = {
        "scene": "Stages/GHZ/Scene1.bin",
        "entity_count": len(ents),
        "ring_count": ring_count,
        "registered_census": registered_census,
        "max_slot": ents[-1]["slot"],
        "probes": [{"slot": p["slot"], "x": p["x"], "y": p["y"],
                    "object": p["object"]} for p in picks],
        "fg_layers": [{"index": li, "name": n, "xs": xs, "ys": ys}
                      for (li, n, xs, ys, _) in fg],
        "tile_probes": tile_probes,
    }
    mpath = os.path.join(HERE, "..", "p6_ghzlive_model.json")
    with open(mpath, "w") as f:
        json.dump(model, f, indent=2)

    ipath = os.path.join(HERE, "p6_ghzlive_probes.inc")
    with open(ipath, "w") as f:
        f.write("// generated by gen_ghzlive_model.py -- DO NOT EDIT\n")
        f.write("// probe slots into the GHZ1 scene entity table (raw scene\n")
        f.write("// slots; the diag adds RESERVE_ENTITY_COUNT when indexing\n")
        f.write("// objectEntityList, mirroring Scene.cpp's slotID placement).\n")
        f.write("#define P6_GHZLIVE_PROBE_COUNT %d\n" % PROBE_COUNT)
        f.write("static const int32 p6_ghzlive_probe_slots[P6_GHZLIVE_PROBE_COUNT] = {\n    ")
        f.write(", ".join(str(p["slot"]) for p in picks))
        f.write("\n};\n")
        f.write("// engine-seam tile probes: {engine layer index, tx, ty} --\n")
        f.write("// the diag reads tileLayers[layer] through RSDK_LAYER_TILE\n")
        f.write("#define P6_GHZLIVE_TILEPROBE_COUNT %d\n" % len(tile_probes))
        f.write("static const int32 p6_ghzlive_tile_probes[P6_GHZLIVE_TILEPROBE_COUNT][3] = {\n")
        for tp in tile_probes:
            f.write("    { %d, %d, %d },\n" % (tp["layer"], tp["tx"], tp["ty"]))
        f.write("};\n")

    print("model: %d entities, max slot %d" % (len(ents), ents[-1]["slot"]))
    print("probes: %s" % ", ".join("%d@(%d,%d)" % (p["slot"], p["x"], p["y"]) for p in picks[:4]))
    print("wrote %s + %s" % (os.path.normpath(mpath), os.path.normpath(ipath)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
