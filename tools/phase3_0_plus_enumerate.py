#!/usr/bin/env python3
"""
phase3_0_plus_enumerate.py - Phase 3.0-prep++ whole-game asset enumeration.

For every Stages/<Folder>/StageConfig.bin in extracted/:
  - parse object[] -> per-scene unique class set
  - parse sfx[] -> per-scene unique sfx set

For every Stages/<Folder>/Scene*.bin in extracted/:
  - parse Music entities' trackFile attribute -> per-scene trackFile set

Print a per-folder summary + a global union of:
  - all classes -> needed decomp .c files
  - all sfx
  - all trackFiles

Use shared build_filelist.parse_stageconfig + parse_title_entities for parsing.

Output: tools/_phase3plus_enum.json with the full structured result for
downstream consumption (audit doc + gate).
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from build_filelist import parse_stageconfig
from parse_title_entities import parse_entities

REPO = os.path.dirname(HERE)
STAGES = os.path.join(REPO, "extracted", "Data", "Stages")
OUT_JSON = os.path.join(HERE, "_phase3plus_enum.json")


def main():
    folders = sorted(
        d for d in os.listdir(STAGES)
        if os.path.isdir(os.path.join(STAGES, d))
    )
    per_folder = {}
    all_classes = set()
    all_sfx = set()
    all_tracks = set()
    for folder in folders:
        fpath = os.path.join(STAGES, folder)
        sc = os.path.join(fpath, "StageConfig.bin")
        cls_list = []
        sfx_list = []
        if os.path.isfile(sc):
            try:
                with open(sc, "rb") as f:
                    data = f.read()
                cls_list, sfx_list = parse_stageconfig(data)
            except Exception as e:
                print(f"[warn] {folder}/StageConfig.bin parse: {e}",
                      file=sys.stderr)
        # Per-scene trackFile attribute discovery + Music entity slot enumeration.
        tracks_in_folder = []
        scene_files = sorted(
            x for x in os.listdir(fpath)
            if x.startswith("Scene") and x.endswith(".bin")
        )
        for sf in scene_files:
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
                            tracks_in_folder.append({
                                "scene": sf, "slot": ent["slot"],
                                "trackFile": val
                            })
                    except (IndexError, KeyError):
                        pass
        per_folder[folder] = {
            "objects": cls_list,
            "sfx": sfx_list,
            "scenes": scene_files,
            "tracks": tracks_in_folder,
        }
        all_classes.update(cls_list)
        all_sfx.update(sfx_list)
        for t in tracks_in_folder:
            all_tracks.add(t["trackFile"])

    result = {
        "folders": per_folder,
        "all_classes": sorted(all_classes),
        "all_sfx": sorted(all_sfx),
        "all_tracks": sorted(all_tracks),
    }
    with open(OUT_JSON, "w", encoding="utf-8") as f:
        json.dump(result, f, indent=2)

    # Print summary table.
    print(f"Total folders: {len(folders)}")
    print(f"Unique object classes across all stages: {len(all_classes)}")
    print(f"Unique sfx across all stages:            {len(all_sfx)}")
    print(f"Unique trackFiles across all Scene*.bin: {len(all_tracks)}")
    print()
    print(f"{'Folder':<18}{'Objs':>6}{'SFX':>6}{'Scenes':>8}{'MusEnt':>8}")
    for folder, info in per_folder.items():
        print(f"{folder:<18}{len(info['objects']):>6}{len(info['sfx']):>6}"
              f"{len(info['scenes']):>8}{len(info['tracks']):>8}")
    print()
    print(f"Wrote {OUT_JSON}")


if __name__ == "__main__":
    main()
