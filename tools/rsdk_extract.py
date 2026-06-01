#!/usr/bin/env python3
"""
rsdk_extract.py - Extract files from an RSDKv5 datapack (Sonic Mania Data.rsdk).

Algorithm is a faithful port of RSDKv5-Decompilation RSDKv5/RSDK/Core/Reader.cpp
(LoadDataPack / GenerateELoadKeys / DecryptBytes), verified against the on-disk
header of the real Data.rsdk (sig "RSDKv5", fileCount, per-file 16-byte MD5 +
LE u32 offset + LE u32 size with bit31 = encrypted; first file offset == header
size 8 + N*24, confirmed).

Files are stored by MD5(lowercase(path)); encrypted files need their real path
because the cipher key is MD5(uppercase(path)). So extraction is driven by a
filelist of candidate paths. Unmatched / unnamed hashes are reported.

Usage:
    python rsdk_extract.py Data.rsdk --filelist maniafilelist.txt --out extracted
    python rsdk_extract.py Data.rsdk --probe          # test path convention only
"""
import argparse
import hashlib
import os
import struct
import sys


def _wordswap(d: bytes) -> bytes:
    """Byte-reverse each consecutive 4-byte word of a 16-byte digest.

    The engine reads stored hashes as big-endian u32s but computes file hashes
    as little-endian u32s, so the on-disk hash equals the MD5 digest with each
    4-byte group reversed. This also matches GenerateELoadKeys' key layout.
    """
    out = bytearray(16)
    for y in range(0, 16, 4):
        out[y + 0] = d[y + 3]
        out[y + 1] = d[y + 2]
        out[y + 2] = d[y + 1]
        out[y + 3] = d[y + 0]
    return bytes(out)


def lookup_hash(path: str) -> bytes:
    """Datapack lookup key = wordswap(MD5(lowercase(path)))."""
    return _wordswap(hashlib.md5(path.lower().encode("utf-8")).digest())


def make_key(seed: str) -> bytes:
    """RSDK cipher key = wordswap(MD5(seed))."""
    return _wordswap(hashlib.md5(seed.encode("utf-8")).digest())


def decrypt(data: bytes, filename: str, size: int) -> bytes:
    """Exact port of RSDK::DecryptBytes with the LoadFile init state."""
    key_a = make_key(filename.upper())          # GenerateELoadKeys key1 = path
    key_b = make_key(str(size))                  # key2 = file size
    out = bytearray(data)

    e_key_no = (size // 4) & 0x7F
    pos_a = 0
    pos_b = 8
    nybble_swap = False

    for i in range(len(out)):
        b = out[i]
        b ^= e_key_no ^ key_b[pos_b]
        b &= 0xFF
        if nybble_swap:
            b = ((b << 4) + (b >> 4)) & 0xFF
        b ^= key_a[pos_a]
        out[i] = b & 0xFF

        pos_a += 1
        pos_b += 1

        if pos_a <= 15:
            if pos_b > 12:
                pos_b = 0
                nybble_swap = not nybble_swap
        elif pos_b <= 8:
            pos_a = 0
            nybble_swap = not nybble_swap
        else:
            e_key_no = (e_key_no + 2) & 0x7F
            if nybble_swap:
                nybble_swap = False
                pos_a = e_key_no % 7
                pos_b = (e_key_no % 12) + 2
            else:
                nybble_swap = True
                pos_a = (e_key_no % 12) + 3
                pos_b = e_key_no % 7
    return bytes(out)


def parse_datapack(path: str):
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:6] != b"RSDKv5":
        raise SystemExit("Not an RSDKv5 datapack (bad signature).")
    file_count = struct.unpack_from("<H", raw, 6)[0]
    entries = []
    pos = 8
    for _ in range(file_count):
        h = raw[pos:pos + 16]
        offset, size = struct.unpack_from("<II", raw, pos + 16)
        encrypted = (size & 0x80000000) != 0
        size &= 0x7FFFFFFF
        entries.append({"hash": h, "offset": offset, "size": size,
                        "encrypted": encrypted})
        pos += 24
    return raw, file_count, entries


# Known/likely Mania path conventions to discover how paths are hashed.
PROBE_PATHS = [
    "Data/Game/GameConfig.bin",
    "data/game/gameconfig.bin",
    "Data/Game/ObjectList.txt",
    "Data/Palettes/Default.act",
    "Data/Stages/StageList.txt",
    "Data/Music/Stage.ogg",
    "GameConfig.bin",
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("datapack")
    ap.add_argument("--filelist")
    ap.add_argument("--out", default="extracted")
    ap.add_argument("--probe", action="store_true")
    args = ap.parse_args()

    raw, file_count, entries = parse_datapack(args.datapack)
    by_hash = {e["hash"]: e for e in entries}
    enc = sum(1 for e in entries if e["encrypted"])
    print(f"Datapack: {file_count} files ({enc} encrypted, "
          f"{file_count - enc} plain). Total {len(raw):,} bytes.")

    if args.probe:
        print("\nProbing path conventions against stored hashes:")
        for p in PROBE_PATHS:
            h = lookup_hash(p)
            hit = "MATCH" if h in by_hash else "-"
            print(f"  [{hit:5}] {p}  ->  {h.hex()}")
        return

    if not args.filelist:
        raise SystemExit("Provide --filelist (or use --probe).")

    with open(args.filelist, "r", encoding="utf-8") as fl:
        paths = [ln.strip().replace("\\", "/") for ln in fl
                 if ln.strip() and not ln.startswith("#")]

    matched = 0
    os.makedirs(args.out, exist_ok=True)
    seen = set()
    for p in paths:
        h = lookup_hash(p)
        e = by_hash.get(h)
        if not e:
            continue
        matched += 1
        seen.add(e["hash"])
        blob = raw[e["offset"]:e["offset"] + e["size"]]
        if e["encrypted"]:
            blob = decrypt(blob, p, e["size"])
        dest = os.path.join(args.out, p)
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        with open(dest, "wb") as o:
            o.write(blob)

    unnamed = [e for e in entries if e["hash"] not in seen]
    print(f"\nMatched & extracted: {matched}/{file_count}")
    print(f"Unnamed remaining:   {len(unnamed)} "
          f"({sum(1 for e in unnamed if e['encrypted'])} of them encrypted)")
    # Dump unnamed plain files by hash so nothing is lost.
    if unnamed:
        raw_dir = os.path.join(args.out, "_unnamed")
        os.makedirs(raw_dir, exist_ok=True)
        for e in unnamed:
            if e["encrypted"]:
                continue  # can't decrypt without the path
            blob = raw[e["offset"]:e["offset"] + e["size"]]
            with open(os.path.join(raw_dir, e["hash"].hex() + ".bin"), "wb") as o:
                o.write(blob)


if __name__ == "__main__":
    main()
