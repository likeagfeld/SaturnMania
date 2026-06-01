#!/usr/bin/env python3
"""
build_gameconfig_hash_table.py - Emit RSDK class-name -> MD5-hash mapping
from a decrypted RSDKv5 GameConfig.bin + the per-stage StageConfig.bin
files (which hold the stage-local object classes appended to the global
list).

This is Phase 3.2.a foundation tooling (Task #145 — UIControl +
UIBackground port).  The decomp's hash convention (per
rsdkv5-src/RSDKv5/RSDK/Core/RetroEngine.cpp::RegisterObject and
_RSDKv5_Scene.cpp class-resolution) is:

    name_hash = md5(class_name_ascii)              (16 bytes, raw digest)

The on-disk layout in Scene1.bin stores these as four little-endian u32
words (verified by tools/parse_title_entities.py against shipped data —
the raw MD5 digest bytes match the disk bytes byte-for-byte).

Strategy:
  1. Parse extracted/Data/Game/GameConfig.bin via the existing
     tools/parse_gameconfig.py parser (cfg['objects'] is the global
     class list in registration order).
  2. For every stage folder (cfg['categories'][*]['scenes'][*]['folder']),
     also parse extracted/Data/Stages/<folder>/StageConfig.bin if present.
     StageConfig.bin has the same CFG\\0 prefix as GameConfig.bin and lists
     PER-STAGE additional object classes appended to the global list
     (per rsdkv5-src/RSDKv5/RSDK/Storage/UserStorage.cpp + Scene.cpp
     LoadStage chain).  The Menu scene's UIControl + UIBackground +
     UIWidgets + every UIButton/UIChoice/etc. class lives in
     extracted/Data/Stages/Menu/StageConfig.bin -- NOT in
     GameConfig.bin -- because they're scene-local in Mania's design.
  3. Emit tools/_decomp_raw/class_hash_table.json = {
       "hashes": { "<32hex>": "<class_name>", ... },
       "by_scene": { "Menu": ["UIControl", "UIBackground", ...], ... },
       "global":   ["Player", "Music", "SaveGame", ... ]
     }

Verification: hash every known Menu class name and confirm the byte
sequence matches the hash slot used by parse_title_entities.py against
extracted/Data/Stages/Menu/Scene1.bin.

Usage:
    python tools/build_gameconfig_hash_table.py \\
        [extracted/Data/Game/GameConfig.bin] \\
        [--stages-root extracted/Data/Stages] \\
        [--out tools/_decomp_raw/class_hash_table.json]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import struct
import sys
from pathlib import Path


# --- Minimal CFG\0 reader (mirrors parse_gameconfig.py::Reader) ---------

class Reader:
    def __init__(self, data: bytes) -> None:
        self.d = data
        self.p = 0

    def u8(self) -> int:
        v = self.d[self.p]
        self.p += 1
        return v

    def u16(self) -> int:
        v = struct.unpack_from("<H", self.d, self.p)[0]
        self.p += 2
        return v

    def u32(self) -> int:
        v = struct.unpack_from("<I", self.d, self.p)[0]
        self.p += 4
        return v

    def s(self) -> str:
        n = self.u8()
        v = self.d[self.p:self.p + n].decode("latin-1")
        self.p += n
        return v


# --- GameConfig.bin: parse top-level object list ------------------------

PALETTE_BANK_COUNT = 8


def parse_gameconfig_objects(data: bytes, rev02: bool) -> list[str]:
    """Return the ordered list of class names declared at the top of
    GameConfig.bin (`objects` array).  Mirrors parse_gameconfig.py."""
    r = Reader(data)
    if data[:4] != b"CFG\x00":
        raise ValueError("Not a CFG file")
    r.p = 4
    _ = r.s()  # title
    _ = r.s()  # subtitle
    _ = r.s()  # version
    _ = r.u8()  # active_category
    _ = r.u16()  # start_scene
    obj_cnt = r.u8()
    names = [r.s() for _ in range(obj_cnt)]
    return names


def parse_gameconfig_full(data: bytes) -> dict:
    """Return the full parsed GameConfig (objects + categories) auto-
    detecting REV01 vs REV02."""
    last_err: Exception | None = None
    for rev02 in (False, True):
        try:
            return _parse_full(data, rev02), rev02
        except Exception as e:
            last_err = e
            continue
    raise SystemExit(f"GameConfig parse failed for both revs: {last_err}")


def _parse_full(data: bytes, rev02: bool) -> dict:
    r = Reader(data)
    if data[:4] != b"CFG\x00":
        raise ValueError("Not a CFG file")
    r.p = 4
    title = r.s()
    subtitle = r.s()
    version = r.s()
    _ = r.u8()
    _ = r.u16()

    obj_cnt = r.u8()
    objects = [r.s() for _ in range(obj_cnt)]

    for _ in range(PALETTE_BANK_COUNT):
        rows = r.u16()
        for bit in range(16):
            if (rows >> bit) & 1:
                r.p += 16 * 3

    sfx_cnt = r.u8()
    for _ in range(sfx_cnt):
        _ = r.s()
        r.u8()  # maxConcurrentPlays

    _ = r.u16()  # total_scene_count
    cat_cnt = r.u8()
    cats = []
    for _ in range(cat_cnt):
        cname = r.s()
        sc_cnt = r.u8()
        scenes = []
        for _ in range(sc_cnt):
            sname = r.s()
            folder = r.s()
            sid = r.s()
            if rev02:
                r.u8()  # filter
            scenes.append({"name": sname, "folder": folder, "id": sid})
        cats.append({"name": cname, "scenes": scenes})

    # Validate: every scene's folder must be printable.
    for c in cats:
        for s in c["scenes"]:
            if not s["folder"] or not s["folder"].isprintable():
                raise ValueError("folder garbled (wrong rev)")

    return {
        "title": title, "subtitle": subtitle, "version": version,
        "objects": objects, "categories": cats,
    }


# --- StageConfig.bin: parse per-stage object list -----------------------

def parse_stageconfig_objects(data: bytes) -> list[str]:
    """StageConfig.bin format (verified empirically against
    extracted/Data/Stages/Menu/StageConfig.bin first 32 bytes
    `4346470000280b41504943616c6c6261636b` -> CFG\\0 + 0x00 (useGameObjects=
    false, meaning this stage REPLACES the global object list) + 0x28
    (40 objects) + pstring "APICallback").

    Per RSDKv5-Decompilation Storage/UserStorage.cpp LoadStageConfig
    (mirrored by parse_gameconfig.py top-of-file layout for the top-level
    section), the order is:

       4 bytes "CFG\\0"
       1 byte  useGameObjects (bool — 0 = stage replaces global list,
                                       1 = stage extends global list)
       1 byte  obj_count
       obj_count * pstring (u8 len + ASCII bytes)
       PALETTE_BANK_COUNT * (u16 rowmask + 48*set_bit_count bytes RGB)
       1 byte  sfx_count
       sfx_count * (pstring path + u8 maxConcurrentPlays)
       u8     music_count
       ... (rest not needed for hash table)

    Returns the per-stage object list.  Some StageConfig files (e.g. Menu)
    declare object names that are stage-local, e.g. UIControl, UIButton."""
    r = Reader(data)
    if data[:4] != b"CFG\x00":
        raise ValueError("Not a CFG file")
    r.p = 4
    _use_global = r.u8()  # useGameObjects flag
    obj_cnt = r.u8()
    names = [r.s() for _ in range(obj_cnt)]
    return names


# --- Hash helpers --------------------------------------------------------

def class_hash(name: str) -> bytes:
    """MD5 of the class name's ASCII bytes.  RSDK stores this raw on disk
    (verified in tools/parse_title_entities.py)."""
    return hashlib.md5(name.encode("ascii")).digest()


# --- Main ----------------------------------------------------------------

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("gameconfig", nargs="?",
                    default="extracted/Data/Game/GameConfig.bin")
    ap.add_argument("--stages-root", default="extracted/Data/Stages")
    ap.add_argument("--out",
                    default="tools/_decomp_raw/class_hash_table.json")
    ap.add_argument("--verify-scene", default=None,
                    help="Path to a Scene1.bin to cross-check class "
                         "resolution rate (e.g. extracted/Data/Stages/"
                         "Menu/Scene1.bin)")
    args = ap.parse_args()

    gcfg_path = Path(args.gameconfig)
    if not gcfg_path.exists():
        print(f"FATAL: {gcfg_path} not found", file=sys.stderr)
        return 2

    with open(gcfg_path, "rb") as f:
        gcfg_data = f.read()

    full_cfg, rev02 = parse_gameconfig_full(gcfg_data)
    print(f"GameConfig.bin parsed as {'REV02' if rev02 else 'REV01'}"
          f"; {len(full_cfg['objects'])} global classes, "
          f"{len(full_cfg['categories'])} categories")

    # Global class set (these match the implicit slot 0 hash chain at
    # the head of every Scene1.bin Object table).
    global_classes = list(full_cfg["objects"])

    # Per-scene class additions.
    by_scene: dict[str, list[str]] = {}
    stages_root = Path(args.stages_root)
    scenes_walked = 0
    for cat in full_cfg["categories"]:
        for sc in cat["scenes"]:
            folder = sc["folder"]
            sid = sc["id"]
            scfg = stages_root / folder / "StageConfig.bin"
            if not scfg.exists():
                continue
            try:
                with open(scfg, "rb") as f:
                    sdata = f.read()
                names = parse_stageconfig_objects(sdata)
            except Exception as e:
                print(f"  WARN: StageConfig {scfg} parse failed: {e}",
                      file=sys.stderr)
                continue
            by_scene[folder] = names
            scenes_walked += 1

    print(f"Walked {scenes_walked} stage folders for StageConfig.bin")

    # Build the merged name set (global + every per-stage).  The
    # GameObjects.h hash registry is the union of these.
    all_names: set[str] = set(global_classes)
    for folder, names in by_scene.items():
        for n in names:
            all_names.add(n)

    # Hash table: hex hash -> class name.
    hashes_hex: dict[str, str] = {}
    for n in sorted(all_names):
        h = class_hash(n).hex()
        if h in hashes_hex and hashes_hex[h] != n:
            print(f"  COLLISION: {h} -> {hashes_hex[h]} vs {n}",
                  file=sys.stderr)
        hashes_hex[h] = n

    print(f"Total distinct class names: {len(all_names)} -> "
          f"{len(hashes_hex)} hash slots")

    # Verification step: optional cross-check against Scene1.bin.
    verify_report = None
    if args.verify_scene:
        verify_report = verify_scene(Path(args.verify_scene), hashes_hex)

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "_schema": "rsdk-class-hash-table-v1",
        "_generator": "tools/build_gameconfig_hash_table.py",
        "_source": str(gcfg_path),
        "global": global_classes,
        "by_scene": by_scene,
        "hashes": hashes_hex,
    }
    if verify_report is not None:
        payload["_verify"] = verify_report
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, sort_keys=True)
    print(f"Wrote {out_path}")
    return 0


def verify_scene(scene_path: Path, hashes_hex: dict[str, str]) -> dict:
    """Walk Scene1.bin's object table, count how many class hashes resolve
    against the hash table.  Returns a dict suitable for embedding in the
    JSON output."""
    import zlib
    with open(scene_path, "rb") as f:
        d = f.read()
    if d[:4] != b"SCN\x00":
        return {"path": str(scene_path), "error": "not an SCN file"}

    p = 4 + 0x10
    nl = d[p]; p += 1 + nl + 1
    layer_count = d[p]; p += 1
    for _ in range(layer_count):
        p += 1                                 # visibleInEditor
        nm = d[p]; p += 1 + nm                 # layer name
        p += 1 + 1 + 2 + 2 + 2 + 2             # type+drawGroup+xs+ys+px+sc
        sic = struct.unpack_from("<H", d, p)[0]; p += 2
        p += sic * 6                           # scrollInfo entries
        for _z in range(2):                    # line-scroll + layout blobs
            total = struct.unpack_from("<I", d, p)[0]; p += 4
            _usize = struct.unpack_from(">I", d, p)[0]; p += 4
            clen = total - 4
            p += clen

    obj_count = d[p]; p += 1
    resolved = 0
    unresolved = 0
    resolved_names: list[str] = []
    unresolved_hashes: list[str] = []
    for _ in range(obj_count):
        nhash = d[p:p + 16].hex(); p += 16
        var_count = d[p]; p += 1
        # skip (var_count - 1) attribs of (16-byte hash + u8 type)
        skip = max(0, var_count - 1) * 17
        p += skip
        # entity_count + entities
        entity_count = struct.unpack_from("<H", d, p)[0]; p += 2
        # to skip entity bodies we'd need to re-read attribs.  Per
        # parse_title_entities.py the per-entity record is variable-size,
        # but for hash-resolution we only need the class hash already
        # captured above -- we MUST still advance p past the entities to
        # keep the loop in sync.  Re-read the attribs to size the entity.
        # Walk back: we already advanced past the attribs; we need their
        # type bytes to compute entity record sizes.  Easiest: snapshot
        # the byte offsets we already crossed.

        # Re-read: the attribs were at p_attrib_start.. .  Save and replay.
        # For verification purposes, we'll just truncate after the object
        # list -- since unresolved entities still count toward obj_count
        # and the hash field is what we need.
        if nhash in hashes_hex:
            resolved += 1
            resolved_names.append(hashes_hex[nhash])
        else:
            unresolved += 1
            unresolved_hashes.append(nhash[:8])

        # Without re-reading attribs we can't safely advance p past the
        # entity blob; but we don't need to because we have all obj-class
        # hashes already extracted in this loop iteration via nhash.
        # However if we do NOT advance past the entity table, the NEXT
        # iteration's nhash will be wrong.  Replay-read attribs:
        # Back up p: we need attrib type list.  We already skipped past
        # them.  Skip the entity payload by walking it manually:
        # ...this requires the attrib types.  Simpler approach: re-parse
        # by going back to the saved offset.
        # Implementation: walk the entity body using a sub-parser that
        # mirrors read_attr from parse_title_entities.py.
        # For NOW, save the offsets so the next iteration reads correctly.
        # We've already lost them above.  Approach 2: just count the
        # FIRST occurrence of each unique hash and bail.
        # Since obj_count is the number of CLASSES (not entities), each
        # class hash is read exactly once.  But we still need to skip
        # entity rows.  Easiest: just bail after the first object since
        # we only need the COUNT of resolved class hashes from the obj
        # table header. We were going to skip entity bodies anyway --
        # let's just do it properly:
        # ... actually, since obj_count is the number of classes, and
        # we just need a class-hash resolution rate, we can record the
        # nhash and then walk past the entity table by re-parsing the
        # attribs.

        # Re-parse attribs by going back:
        # We'll do it with a saved-position approach in the next pass.
        # For now break out of the loop after we've sampled enough
        # classes -- this gives an approximate but useful verification
        # rate.
        break

    # Per Phase 3.2.a brief: simple resolution rate without full entity
    # walk.  Do a separate pass that ONLY reads the class-hash bytes
    # without trying to walk entity payloads, by parsing attribs to size
    # them.
    # This second pass is exact.
    classes_at = []
    p2 = 4 + 0x10
    nl = d[p2]; p2 += 1 + nl + 1
    layer_count = d[p2]; p2 += 1
    for _ in range(layer_count):
        p2 += 1
        nm = d[p2]; p2 += 1 + nm
        p2 += 1 + 1 + 2 + 2 + 2 + 2
        sic = struct.unpack_from("<H", d, p2)[0]; p2 += 2
        p2 += sic * 6
        for _z in range(2):
            total = struct.unpack_from("<I", d, p2)[0]; p2 += 4
            _usize = struct.unpack_from(">I", d, p2)[0]; p2 += 4
            clen = total - 4
            p2 += clen

    obj_count = d[p2]; p2 += 1
    resolved = 0
    unresolved = 0
    resolved_names = []
    unresolved_hashes = []
    for _oi in range(obj_count):
        nhash = d[p2:p2 + 16].hex(); p2 += 16
        var_count = d[p2]; p2 += 1
        attribs: list[int] = []
        for _ in range(max(0, var_count - 1)):
            p2 += 16  # attrib hash
            atype = d[p2]; p2 += 1
            attribs.append(atype)
        entity_count = struct.unpack_from("<H", d, p2)[0]; p2 += 2
        for _e in range(entity_count):
            p2 += 2  # slot
            p2 += 4  # x
            p2 += 4  # y
            for at in attribs:
                if at == 0: p2 += 1
                elif at == 1: p2 += 2
                elif at == 2: p2 += 4
                elif at == 3: p2 += 1
                elif at == 4: p2 += 2
                elif at == 5: p2 += 4
                elif at == 6: p2 += 4
                elif at == 7: p2 += 4
                elif at == 8:
                    n = struct.unpack_from("<H", d, p2)[0]; p2 += 2
                    p2 += n * 2
                elif at == 9:
                    p2 += 8
                elif at == 11:
                    p2 += 4
                else:
                    raise ValueError(f"attr type {at}")
        if nhash in hashes_hex:
            resolved += 1
            resolved_names.append(hashes_hex[nhash])
        else:
            unresolved += 1
            unresolved_hashes.append(nhash[:8])

    print(f"  Scene1 verification: {scene_path}")
    print(f"    classes total: {obj_count}")
    print(f"    resolved:      {resolved} ({100.0 * resolved / max(1, obj_count):.1f}%)")
    print(f"    unresolved:    {unresolved}")
    if resolved_names:
        print(f"    sample resolved: {sorted(set(resolved_names))[:8]}")

    return {
        "scene": str(scene_path),
        "classes_total": obj_count,
        "resolved": resolved,
        "unresolved": unresolved,
        "resolved_names": sorted(set(resolved_names)),
        "unresolved_hash_prefixes": sorted(set(unresolved_hashes)),
    }


if __name__ == "__main__":
    sys.exit(main())
