#!/usr/bin/env python3
"""
phase3_0_plus_scan_assets.py - Whole-game asset reference scan.

Scans EVERY tools/_decomp_raw/SonicMania_Objects_*.c file for RSDK asset
patterns. Skips #if GAME_INCLUDE_EDITOR blocks. Plus parses every
extracted/Data/Stages/<folder>/Scene*.bin for Music entity trackFile attrs.

Emits:
  - tools/_phase3plus_asset_refs.json (list of {file, line, call, resolved})
  - tools/_phase3plus_asset_paths.json (sorted set of unique resolved paths)
  - tools/_phase3plus_per_scene_refs.json (per-folder references)
"""
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from parse_title_entities import parse_entities

REPO = os.path.dirname(HERE)
RAW = os.path.join(HERE, "_decomp_raw")
STAGES = os.path.join(REPO, "extracted", "Data", "Stages")

PATTERNS = [
    (r'RSDK\.LoadSpriteAnimation\s*\(\s*"([^"]+)"', "Data/Sprites/{}"),
    (r'RSDK\.LoadSpriteSheet\s*\(\s*"([^"]+)"',     "Data/Sprites/{}"),
    (r'RSDK\.LoadStringList\s*\(\s*&[^,]+,\s*"([^"]+)"', "Data/Strings/{}"),
    (r'RSDK\.LoadVideo\s*\(\s*"([^"]+)"',           "Data/Video/{}"),
    (r'RSDK\.PlayStream\s*\(\s*"([^"]+)"',          "Data/Music/{}"),
    (r'RSDK\.GetSfx\s*\(\s*"([^"]+)"',              "Data/SoundFX/{}"),
    (r'Music_SetMusicTrack\s*\(\s*"([^"]+)"',       "Data/Music/{}"),
]


def scan_file(fpath):
    refs = []
    with open(fpath, "r", encoding="latin-1") as f:
        in_editor = 0
        for lineno, line in enumerate(f, 1):
            stripped = line.strip()
            if re.match(r'#if\s+GAME_INCLUDE_EDITOR\b', stripped):
                in_editor += 1
                continue
            if in_editor > 0:
                if stripped.startswith("#if"):
                    in_editor += 1
                elif stripped.startswith("#endif"):
                    in_editor -= 1
                continue
            for pat, tmpl in PATTERNS:
                for m in re.finditer(pat, line):
                    arg = m.group(1)
                    resolved = tmpl.format(arg)
                    refs.append({
                        "line": lineno,
                        "call": m.group(0),
                        "arg": arg,
                        "resolved": resolved,
                    })
    return refs


def main():
    # Scan all .c files in decomp cache.
    all_refs = []
    per_file = {}
    for fn in sorted(os.listdir(RAW)):
        if not fn.endswith(".c"):
            continue
        if not fn.startswith("SonicMania_Objects_"):
            continue
        fpath = os.path.join(RAW, fn)
        refs = scan_file(fpath)
        per_file[fn] = refs
        for r in refs:
            r["file"] = fn
            all_refs.append(r)

    # Parse Scene1.bin Music entities across all folders.
    track_refs = []
    if os.path.isdir(STAGES):
        for folder in sorted(os.listdir(STAGES)):
            fpath = os.path.join(STAGES, folder)
            if not os.path.isdir(fpath):
                continue
            for sf in sorted(os.listdir(fpath)):
                if not (sf.startswith("Scene") and sf.endswith(".bin")):
                    continue
                spath = os.path.join(fpath, sf)
                try:
                    objs, _, _ = parse_entities(spath)
                except Exception:
                    continue
                for o in objs:
                    if o["name"] != "Music":
                        continue
                    idx = None
                    for i, (an, _) in enumerate(o["attribs"]):
                        if an == "trackFile":
                            idx = i
                            break
                    if idx is None:
                        continue
                    for ent in o["entities"]:
                        try:
                            _an, _lbl, val = ent["attrs"][idx]
                            if isinstance(val, str) and val:
                                track_refs.append({
                                    "folder": folder,
                                    "scene": sf,
                                    "slot": ent["slot"],
                                    "trackFile": val,
                                    "resolved": f"Data/Music/{val}",
                                })
                        except (IndexError, KeyError):
                            pass

    unique_paths = sorted(
        {r["resolved"] for r in all_refs}
        | {r["resolved"] for r in track_refs}
    )

    out_refs = os.path.join(HERE, "_phase3plus_asset_refs.json")
    out_paths = os.path.join(HERE, "_phase3plus_asset_paths.json")
    out_tracks = os.path.join(HERE, "_phase3plus_track_refs.json")
    out_perfile = os.path.join(HERE, "_phase3plus_per_file_refs.json")
    with open(out_refs, "w") as f:
        json.dump(all_refs, f, indent=2)
    with open(out_paths, "w") as f:
        json.dump(unique_paths, f, indent=2)
    with open(out_tracks, "w") as f:
        json.dump(track_refs, f, indent=2)
    with open(out_perfile, "w") as f:
        json.dump(per_file, f, indent=2)

    print(f"Decomp .c files scanned: {sum(1 for v in per_file.values())}")
    print(f"Total RSDK call sites:   {len(all_refs)}")
    print(f"Scene1.bin trackFile attrs: {len(track_refs)}")
    print(f"Unique resolved paths:   {len(unique_paths)}")
    print(f"Wrote {out_refs}, {out_paths}, {out_tracks}, {out_perfile}")


if __name__ == "__main__":
    main()
