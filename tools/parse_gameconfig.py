#!/usr/bin/env python3
"""
parse_gameconfig.py - Parse a decrypted RSDKv5 GameConfig.bin and emit the stage
list + a filelist of each stage's tile assets (for rsdk_extract.py --filelist).

Format ported verbatim from RSDKv5-Decompilation RetroEngine.cpp::LoadGameConfig
(REV02 / RETRO_REVISION 3, PALETTE_BANK_COUNT 8). ReadString = [u8 len][len bytes].

Usage:
    python parse_gameconfig.py extracted/Data/Game/GameConfig.bin [--filelist out.txt]
"""
import argparse
import struct
import sys

PALETTE_BANK_COUNT = 8

# Standard per-stage-folder asset files in RSDKv5 Mania stages.
FOLDER_ASSETS = ["16x16Tiles.gif", "TileConfig.bin", "StageConfig.bin"]


class Reader:
    def __init__(self, data):
        self.d = data
        self.p = 0

    def u8(self):
        v = self.d[self.p]; self.p += 1; return v

    def u16(self):
        v = struct.unpack_from("<H", self.d, self.p)[0]; self.p += 2; return v

    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]; self.p += 4; return v

    def s(self):
        n = self.u8()
        v = self.d[self.p:self.p + n].decode("latin-1"); self.p += n
        return v


def parse(data, rev02):
    r = Reader(data)
    if data[:4] != b"CFG\x00":
        raise SystemExit("Not a GameConfig.bin (missing CFG signature).")
    r.p = 4
    title, subtitle, version = r.s(), r.s(), r.s()
    active_category = r.u8()
    start_scene = r.u16()

    obj_cnt = r.u8()
    objects = [r.s() for _ in range(obj_cnt)]

    # Palette banks: per bank a u16 row-mask; each set bit => 16 RGB triples.
    for _ in range(PALETTE_BANK_COUNT):
        rows = r.u16()
        for bit in range(16):
            if (rows >> bit) & 1:
                r.p += 16 * 3

    sfx_cnt = r.u8()
    sfx = []
    for _ in range(sfx_cnt):
        name = r.s(); r.u8()  # maxConcurrentPlays
        sfx.append(name)

    total_scene_count = r.u16()
    category_count = r.u8()

    categories = []
    for _ in range(category_count):
        cname = r.s()
        scene_count = r.u8()
        scenes = []
        for _ in range(scene_count):
            sname = r.s()
            folder = r.s()
            sid = r.s()
            filt = r.u8() if rev02 else 0xFF  # REV02-only filter byte
            scenes.append({"name": sname, "folder": folder, "id": sid,
                           "filter": filt})
        categories.append({"name": cname, "scenes": scenes})

    return {"title": title, "subtitle": subtitle, "version": version,
            "objects": objects, "sfx": sfx,
            "total_scene_count": total_scene_count, "categories": categories}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gameconfig")
    ap.add_argument("--filelist")
    args = ap.parse_args()

    with open(args.gameconfig, "rb") as f:
        data = f.read()

    # Auto-detect REV01 (no per-scene filter byte) vs REV02 (filter byte).
    cfg = None
    for rev02 in (False, True):
        try:
            c = parse(data, rev02)
        except Exception:
            continue
        folders_ok = all(
            sc["folder"].isprintable() and sc["folder"]
            for cat in c["categories"] for sc in cat["scenes"])
        if folders_ok and c["categories"]:
            cfg = c
            print(f"(parsed as {'REV02' if rev02 else 'REV01'})")
            break
    if cfg is None:
        raise SystemExit("Could not parse GameConfig with either revision.")

    print(f"Title:    {cfg['title']}")
    print(f"Subtitle: {cfg['subtitle']}")
    print(f"Version:  {cfg['version']}")
    print(f"Objects:  {len(cfg['objects'])}   Global SFX: {len(cfg['sfx'])}")
    print(f"Scenes:   {cfg['total_scene_count']} across "
          f"{len(cfg['categories'])} categories\n")

    paths = set()
    folders = set()
    for cat in cfg["categories"]:
        print(f"[{cat['name']}]  ({len(cat['scenes'])} scenes)")
        for sc in cat["scenes"]:
            print(f"    {sc['name']:<24} folder={sc['folder']:<12} "
                  f"id={sc['id']}")
            base = f"Data/Stages/{sc['folder']}"
            paths.add(f"{base}/Scene{sc['id']}.bin")
            folders.add(base)
    for base in folders:
        for asset in FOLDER_ASSETS:
            paths.add(f"{base}/{asset}")

    if args.filelist:
        with open(args.filelist, "w", encoding="utf-8") as o:
            for p in sorted(paths):
                o.write(p + "\n")
        print(f"\nWrote {len(paths)} candidate stage-asset paths -> "
              f"{args.filelist}")


if __name__ == "__main__":
    main()
