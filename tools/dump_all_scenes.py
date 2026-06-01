#!/usr/bin/env python3
"""
dump_all_scenes.py - For every scene folder under extracted/Data/Stages, parse
StageConfig.bin and Scene*.bin and emit a per-scene catalog including:
    - object list (StageConfig "objects" plus Scene "object names")
    - global SFX list (stage-scope)
    - music slot names referenced
    - per-scene tile-layer (background) names

Format references:
    StageConfig: RSDKv5-Decompilation Scene.cpp::LoadStageConfig
                 "CFG\0" magic, loadGlobals u8, then strings, palette banks,
                 then SFX list, then "stage" music count + music entries.
    Scene.bin:   "SCN\0" magic, then editor metadata, then tile-layer count
                 with names, then a u8 objectCount with each object name and
                 each entity's data (we just collect names).

Usage:
    python tools/dump_all_scenes.py [--root extracted] [--out docs/scene_objects.json]
"""

from __future__ import annotations
import argparse
import json
import os
import struct
import sys

PALETTE_BANK_COUNT = 8


class Reader:
    def __init__(self, data: bytes):
        self.d = data
        self.p = 0

    def u8(self) -> int:
        v = self.d[self.p]; self.p += 1; return v

    def u16(self) -> int:
        v = struct.unpack_from("<H", self.d, self.p)[0]; self.p += 2; return v

    def u32(self) -> int:
        v = struct.unpack_from("<I", self.d, self.p)[0]; self.p += 4; return v

    def s(self) -> str:
        n = self.u8()
        v = self.d[self.p:self.p + n].decode("latin-1"); self.p += n
        return v

    def skip(self, n: int) -> None:
        self.p += n


def parse_stage_config(data: bytes) -> dict:
    r = Reader(data)
    if data[:4] != b"CFG\x00":
        raise ValueError("not a StageConfig.bin")
    r.p = 4
    load_globals = r.u8()
    obj_cnt = r.u8()
    objects = []
    for _ in range(obj_cnt):
        objects.append(r.s())

    # Palette banks: per bank a u16 row-mask; each set bit => 16 RGB triples.
    for _ in range(PALETTE_BANK_COUNT):
        rows = r.u16()
        for bit in range(16):
            if (rows >> bit) & 1:
                r.skip(16 * 3)

    sfx = []
    sfx_cnt = r.u8()
    for _ in range(sfx_cnt):
        name = r.s(); r.u8()  # maxConcurrentPlays
        sfx.append(name)

    # No music block in RSDKv5 StageConfig (music is in GameConfig globally
    # and per-scene Music entities are spawned via scene entities).
    return {
        "load_globals": bool(load_globals),
        "objects": objects,
        "sfx": sfx,
    }


def parse_scene(data: bytes) -> dict:
    """Best-effort Scene*.bin parse - we only need the object name list."""
    if data[:4] != b"SCN\x00":
        raise ValueError("not a Scene.bin")
    r = Reader(data)
    r.p = 4
    # editorLayerCount? RSDKv5 layout (from Scene.cpp::LoadScene):
    #   u8 _unused?
    # The format varies; we scan for printable strings preceded by length bytes
    # because we just need the object-name list.
    # Robust approach: read header bytes; layers; then objectCount + names.

    # Skip a few editor metadata bytes if present (variable). Instead use a
    # heuristic: scan for sequences of <len><len ascii printable identifier>
    # which look like object class names ("Player", "Ring", etc).
    # We just collect ALL such sequences from the file; duplicates filtered.
    names = []
    seen = set()
    i = 4
    n = len(data)
    while i < n:
        ln = data[i]
        if 3 <= ln <= 32 and i + 1 + ln <= n:
            chunk = data[i + 1:i + 1 + ln]
            if all(0x20 <= b <= 0x7E for b in chunk):
                s = chunk.decode("ascii")
                # heuristic: object names use [A-Za-z][A-Za-z0-9_]*
                if s[0].isalpha() and all(c.isalnum() or c == "_" for c in s):
                    if s not in seen:
                        seen.add(s)
                        names.append(s)
        i += 1
    return {"names_seen": names}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default="extracted",
                    help="extracted RSDK root (containing Data/)")
    ap.add_argument("--out", default="docs/scene_objects.json")
    args = ap.parse_args()

    stages_root = os.path.join(args.root, "Data", "Stages")
    if not os.path.isdir(stages_root):
        print(f"Not found: {stages_root}", file=sys.stderr)
        return 2

    catalog: dict[str, dict] = {}
    for folder in sorted(os.listdir(stages_root)):
        path = os.path.join(stages_root, folder)
        if not os.path.isdir(path):
            continue
        entry: dict = {"folder": folder, "scenes": {}}
        stage_cfg_p = os.path.join(path, "StageConfig.bin")
        if os.path.isfile(stage_cfg_p):
            with open(stage_cfg_p, "rb") as f:
                try:
                    entry["stage_config"] = parse_stage_config(f.read())
                except Exception as e:
                    entry["stage_config_error"] = str(e)
        for fn in sorted(os.listdir(path)):
            if not fn.startswith("Scene") or not fn.endswith(".bin"):
                continue
            with open(os.path.join(path, fn), "rb") as f:
                try:
                    entry["scenes"][fn] = parse_scene(f.read())
                except Exception as e:
                    entry["scenes"][fn] = {"error": str(e)}
        catalog[folder] = entry

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as o:
        json.dump(catalog, o, indent=2, ensure_ascii=False)
    print(f"Wrote {args.out}: {len(catalog)} stage folders")
    return 0


if __name__ == "__main__":
    sys.exit(main())
