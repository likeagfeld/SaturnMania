#!/usr/bin/env python3
# =============================================================================
# build_dropin_census.py -- CAPSTONE: fuse every census dimension into one
# per-scene "drop-in readiness" verdict for the engine-shipping port.
#
# Reads the dimension manifests (run build_object_census.py + build_scene_census.py
# first) and the existing struct-size probe, adds the asset BYTE budget + the RSDK
# draw-stub gate, and emits per scene: what must be registered, which objects bust
# the 344 B entity-slot stride, which sheets must be staged + their byte cost vs
# the 384 KB cart store, and which visuals are stub-gated. No guessing -- every
# number traces to a measured source:
#   - logic/deps/residency  : docs/object_census.json   (build_object_census.py)
#   - per-act placement      : docs/scene_census.json    (build_scene_census.py)
#   - entity struct size     : tools/_portspike/_p67d_sizing/entity_sizes_103.json
#                              vs the 344 B slot (SaturnMemoryMap.h:242-245; stride
#                              STAYS 344, >344 needs a per-class Saturn shrink)
#   - sheet decoded bytes    : PIL over extracted/Data/Sprites (8bpp w*h)
#   - draw-stub gate         : p6_stubs.cpp:203-224 (DrawTile/AniTile/String/...)
#   - sheet store budget     : 384 KB cart (SaturnSheet.cpp), banded ~0.22*decoded
#                              (measured: GHZ/Objects 131072->27665 = 0.211)
#
# Emits docs/dropin_census.json + prints GHZ Act 1 verdict + a 42-zone rollup.
# =============================================================================
import json
import os
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OC = os.path.join(ROOT, "docs", "object_census.json")
SC = os.path.join(ROOT, "docs", "scene_census.json")
SZ = os.path.join(ROOT, "tools", "_portspike", "_p67d_sizing", "entity_sizes_103.json")
SPRITES = os.path.join(ROOT, "extracted", "Data", "Sprites")
OUT = os.path.join(ROOT, "docs", "dropin_census.json")

ENTITY_SLOT = 344                 # SaturnMemoryMap.h:244 -- stride stays 344
STORE_BYTES = 393216              # 384 KB cart SaturnSheet store
STAGED_BANDED = 305135            # current 9-sheet banded total (build_sheet_bands)
BAND_RATIO = 0.22                 # measured GHZ/Objects 27665/131072 = 0.211
DRAW_STUBS = {                    # p6_stubs.cpp:203-224 -- gate visuals
    "DrawTile": "tile debris", "DrawAniTile": "animated tiles",
    "DrawString": "text", "DrawDeformedSprite": "deform FX",
    "FillScreen": "screen flash", "SwapDrawListEntries": "draw-order swap",
    "LoadVideo": "FMV",
}
# Sheets the GHZ-Sonic gameplay path never loads (cross-zone CheckSceneFolder
# branches + alt-character competition sheets) -- excluded from a zone's real
# residency. A zone keeps Global/* + its own <ZONE>/* + the active chars.
ALT_CHAR = ("Players/Chibi", "Players/Knux", "Players/Tails2", "Players/Tails3")

_gif = {}
def decoded_bytes(sheet):
    if sheet in _gif:
        return _gif[sheet]
    try:
        from PIL import Image
        im = Image.open(os.path.join(SPRITES, sheet.replace("/", os.sep)))
        v = im.size[0] * im.size[1]
    except Exception:
        v = None
    _gif[sheet] = v
    return v

def zone_relevant(sheet, zone_prefix):
    # A sheet is loaded by THIS zone's gameplay path iff it is shared (Global/,
    # active-character Players/, Editor/) or lives in this zone's own folder.
    # Any other top folder is a different zone's CheckSceneFolder branch that
    # never executes here. ALT_CHAR (Chibi/Knux/Tails2-3) = competition/other
    # character, excluded from the Sonic gameplay path.
    if sheet.startswith(ALT_CHAR):
        return False
    top = sheet.split("/")[0]
    if top in ("Global", "Players", "Editor"):
        return True
    return sheet.startswith(zone_prefix)

def main():
    for p in (OC, SC):
        if not os.path.isfile(p):
            print("MISSING %s -- run build_object_census.py + build_scene_census.py first" % p)
            return 1
    oc = json.load(open(OC, encoding="utf-8"))
    sc = json.load(open(SC, encoding="utf-8"))
    sizes = json.load(open(SZ, encoding="utf-8")) if os.path.isfile(SZ) else {}
    objs = oc["objects"]
    staged = set(oc["staged_sheets"])

    def verdict_for_scene(scene_key, placed):
        zone = scene_key.split("/")[0] + "/"
        names = [k for k, c in placed.items() if c > 0 and not k.startswith("?")]
        need_reg, struct_bust, stub_gated = [], {}, {}
        sheets_new, sheet_bytes = set(), 0
        for n in names:
            o = objs.get(n)
            if not o:
                need_reg.append(n + "(no-census)"); continue
            if not o["registered"]:
                need_reg.append(n)
            sz = sizes.get(n)
            if sz and sz > ENTITY_SLOT:
                struct_bust[n] = sz
            if o["stub_gated_visuals"]:
                stub_gated[n] = sorted(o["stub_gated_visuals"].keys())
            for s in o["all_sheets"]:
                if s not in staged and zone_relevant(s, zone):
                    if s not in sheets_new:
                        sheets_new.add(s)
                        db = decoded_bytes(s)
                        if db:
                            sheet_bytes += db
        banded = int(sheet_bytes * BAND_RATIO)
        fits = (STAGED_BANDED + banded) <= STORE_BYTES
        return {
            "placed_objects": len(names),
            "entities": sum(placed[n] for n in names),
            "need_register": sorted(set(need_reg)),
            "need_register_count": len(set(need_reg)),
            "struct_over_344": struct_bust,
            "sheets_to_stage": sorted(sheets_new),
            "sheets_decoded_bytes": sheet_bytes,
            "sheets_banded_est": banded,
            "store_fits": fits,
            "store_after_est": STAGED_BANDED + banded,
            "stub_gated_visuals": stub_gated,
            "dropin_ready": (len(set(need_reg)) == 0 and not struct_bust and fits),
        }

    out = {"_comment": "GENERATED by build_dropin_census.py -- per-scene drop-in verdict",
           "entity_slot_bytes": ENTITY_SLOT, "store_bytes": STORE_BYTES,
           "staged_banded_bytes": STAGED_BANDED, "scenes": {}}
    for sk, s in sc["scenes"].items():
        if "objects" in s:
            out["scenes"][sk] = verdict_for_scene(sk, s["objects"])
    json.dump(out, open(OUT, "w", encoding="utf-8"), indent=1)

    # ---- GHZ Act 1 detail + whole-game rollup ----------------------------
    g = out["scenes"].get("GHZ/Scene1.bin", {})
    print("=" * 74)
    print("DROP-IN CENSUS -- GHZ Act 1 (GHZ/Scene1.bin)")
    print("=" * 74)
    print("placed objects: %d (%d entities)" % (g.get("placed_objects", 0), g.get("entities", 0)))
    print("NEED REGISTER (%d): %s" % (g.get("need_register_count", 0), ", ".join(g.get("need_register", []))))
    print("STRUCT > 344 B slot (needs Saturn shrink): %s"
          % (", ".join("%s=%d" % (k, v) for k, v in g.get("struct_over_344", {}).items()) or "(none)"))
    print("NEW sheets to stage: %s" % (", ".join(g.get("sheets_to_stage", [])) or "(none -- all reuse staged)"))
    print("  decoded=%d B  banded~=%d B  store after~=%d/%d B  fits=%s"
          % (g.get("sheets_decoded_bytes", 0), g.get("sheets_banded_est", 0),
             g.get("store_after_est", 0), STORE_BYTES, g.get("store_fits")))
    print("STUB-GATED visuals: %s"
          % (", ".join("%s(%s)" % (k, "/".join(v)) for k, v in g.get("stub_gated_visuals", {}).items()) or "(none)"))
    print("DROP-IN READY: %s" % g.get("dropin_ready"))
    print()
    print("=" * 74)
    print("WHOLE-GAME ROLLUP (gameplay acts: Scene1/Scene2 per zone)")
    print("=" * 74)
    print("%-22s %5s %5s %7s %8s %s" % ("scene", "place", "reg!", "struct!", "newKB", "ready"))
    tot_reg = set()
    for sk in sorted(out["scenes"]):
        v = out["scenes"][sk]
        if not sk.endswith(("Scene1.bin", "Scene2.bin")):
            continue
        tot_reg |= set(v["need_register"])
        print("%-22s %5d %5d %7d %8.0f %s"
              % (sk, v["placed_objects"], v["need_register_count"],
                 len(v["struct_over_344"]), v["sheets_banded_est"] / 1024.0,
                 "YES" if v["dropin_ready"] else "no"))
    print("-" * 74)
    print("UNION of objects to register across all gameplay acts: %d" % len(tot_reg))
    print("manifest -> %s" % os.path.relpath(OUT, ROOT))
    return 0

if __name__ == "__main__":
    sys.exit(main())
