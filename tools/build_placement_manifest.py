#!/usr/bin/env python3
"""build_placement_manifest.py -- authored per-entity (class,x,y) ground-truth.

Parses EVERY extracted Scene.bin (94 scenes) via parse_title_entities.parse_entities
and emits docs/scene_placements.json:
  { "<folder>": { "<sceneFile>": { "<className>": [[x_px, y_px], ...] } } }
x/y are PIXELS (Scene.bin stores Q16.16 world coords -> >>16). This is the decomp
ground-truth the oracle's PLACEMENT dimension (G1) diffs live spawn positions against,
from LIVE MEMORY, NO visual comparison (closes the placement gap the manifest
previously couldn't serve -- scene_objects.json had class NAMES only, no coords).

Usage: python tools/build_placement_manifest.py   (writes docs/scene_placements.json)
"""
from __future__ import annotations
import importlib.util, json, sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_ROOT = _HERE.parent


def _load(fn):
    spec = importlib.util.spec_from_file_location(fn.replace(".py", ""), _HERE / fn)
    m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m); return m


def full_class_names() -> list:
    """EVERY registered Mania class name from the decomp Game.c (RSDK_REGISTER_OBJECT
    + _STATIC). Scene.bin stores MD5(name); without the COMPLETE name list most
    GHZ/AIZ classes hash to <unknown> and are lost (the title-only KNOWN_NAMES bug)."""
    import re
    gc = _ROOT / "tools" / "_decomp_raw" / "SonicMania_Game.c"
    names = set()
    if gc.exists():
        txt = gc.read_text(errors="replace")
        names |= set(re.findall(r"RSDK_REGISTER_OBJECT(?:_STATIC)?\(([A-Za-z0-9_]+)\)", txt))
    return sorted(names)


def main() -> int:
    pte = _load("parse_title_entities.py")
    full = full_class_names()
    if full:
        # union with the module's existing KNOWN_NAMES so nothing regresses
        pte.KNOWN_NAMES = sorted(set(getattr(pte, "KNOWN_NAMES", [])) | set(full))
        print(f"class name table: {len(pte.KNOWN_NAMES)} names (decomp Game.c + KNOWN_NAMES)")
    stages = _ROOT / "extracted" / "Data" / "Stages"
    if not stages.is_dir():
        sys.stderr.write(f"no Stages dir at {stages}\n"); return 2
    scenes = sorted(stages.glob("*/Scene*.bin"))
    out = {}
    ok = fail = 0
    for sp in scenes:
        folder = sp.parent.name
        scene = sp.name
        try:
            objects, _consumed, _total = pte.parse_entities(str(sp))
        except Exception as e:
            fail += 1
            sys.stderr.write(f"  parse FAIL {folder}/{scene}: {str(e)[:70]}\n")
            continue
        by_class = {}
        for o in objects:
            nm = o["name"]
            if nm.startswith("<unknown"):
                continue
            pts = [[e["x"] >> 16, e["y"] >> 16] for e in o["entities"]]
            if pts:
                by_class.setdefault(nm, []).extend(pts)
        out.setdefault(folder, {})[scene] = by_class
        ok += 1
    dst = _ROOT / "docs" / "scene_placements.json"
    dst.write_text(json.dumps(out, separators=(",", ":")))
    # summary
    nclass = sum(len(sc) for f in out.values() for sc in f.values())
    nent = sum(len(v) for f in out.values() for sc in f.values() for v in sc.values())
    print(f"scene_placements.json: {ok} scenes parsed ({fail} failed), "
          f"{nclass} class-groups, {nent} placed entities -> {dst}")
    # spot-check GHZ Scene1 (the autorun path)
    ghz = out.get("GHZ", {}).get("Scene1.bin", {})
    if ghz:
        sample = {k: (len(v), v[0] if v else None) for k, v in list(ghz.items())[:8]}
        print(f"  GHZ/Scene1.bin sample (class -> count, first-xy): {sample}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
