#!/usr/bin/env python3
"""
audit_extract.py - Report what rsdk_extract.py recovered from Data.rsdk and
what is still unmatched.

Reads the datapack directly, re-hashes the supplied filelist, and prints:
  * per-Data-subfolder file counts and total bytes for what we have
  * count of unmatched encrypted blobs binned by size class
  * SHA-style listing of the 20 largest unmatched encrypted blobs (so the
    user can manually identify and name them if needed)

Usage:
  python tools/audit_extract.py \
      --datapack Data.rsdk \
      --extracted extracted2 \
      --filelist tools/maniafilelist.txt
"""

import argparse
import hashlib
import os
import struct
import sys


def _wordswap(d: bytes) -> bytes:
    o = bytearray(16)
    for y in range(0, 16, 4):
        o[y + 0] = d[y + 3]; o[y + 1] = d[y + 2]
        o[y + 2] = d[y + 1]; o[y + 3] = d[y + 0]
    return bytes(o)


def lookup_hash(path: str) -> bytes:
    return _wordswap(hashlib.md5(path.lower().encode("utf-8")).digest())


def _human(n: int) -> str:
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024:
            return f"{n:.1f} {unit}"
        n /= 1024
    return f"{n:.1f} TB"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--datapack", default="Data.rsdk")
    ap.add_argument("--extracted", default="extracted2")
    ap.add_argument("--filelist", default="tools/maniafilelist.txt")
    args = ap.parse_args()

    with open(args.datapack, "rb") as f:
        raw = f.read()
    if raw[:6] != b"RSDKv5":
        sys.exit("not an RSDKv5 datapack (bad signature)")
    file_count = struct.unpack_from("<H", raw, 6)[0]
    entries = []
    for i in range(file_count):
        pos = 8 + i * 24
        eh = raw[pos:pos + 16]
        off, sz = struct.unpack_from("<II", raw, pos + 16)
        enc = (sz & 0x80000000) != 0
        sz &= 0x7FFFFFFF
        entries.append({"hash": eh, "size": sz, "encrypted": enc})

    matched = set()
    with open(args.filelist, "r", encoding="utf-8") as f:
        for line in f:
            p = line.strip()
            if not p or p.startswith("#"):
                continue
            matched.add(lookup_hash(p))

    unmatched = [e for e in entries if e["hash"] not in matched]
    unmatched_enc = [e for e in unmatched if e["encrypted"]]
    unmatched_plain = [e for e in unmatched if not e["encrypted"]]
    total_unmatched_bytes = sum(e["size"] for e in unmatched)

    print("=" * 70)
    print("Datapack inventory:")
    print(f"  Total files                : {file_count}")
    print(f"  Total bytes                : {_human(len(raw))}")
    print(f"  Encrypted files            : {sum(1 for e in entries if e['encrypted'])}")
    print(f"  Plain files                : {sum(1 for e in entries if not e['encrypted'])}")
    print()
    print(f"Filelist:")
    print(f"  Distinct path hashes        : {len(matched)}")
    print()
    print(f"Recovered (matched + extracted): {file_count - len(unmatched)}")
    print(f"Unmatched                      : {len(unmatched)}")
    print(f"  encrypted unmatched          : {len(unmatched_enc)}")
    print(f"  plain unmatched              : {len(unmatched_plain)}")
    print(f"  total unrecovered bytes      : {_human(total_unmatched_bytes)}")

    # Per-Data-subfolder breakdown of what's on disk.
    extracted_root = os.path.abspath(args.extracted)
    print()
    print("=" * 70)
    print(f"On-disk contents of {extracted_root}:")
    for top in ("Game", "Music", "SoundFX", "Sprites", "Stages"):
        base = os.path.join(extracted_root, "Data", top)
        if not os.path.isdir(base):
            print(f"  {top:10}: (missing)")
            continue
        n = 0; total = 0
        for r, _, fs in os.walk(base):
            for fn in fs:
                p = os.path.join(r, fn)
                n += 1
                total += os.path.getsize(p)
        print(f"  {top:10}: {n:4d} files, {_human(total):>10}")

    # Size-class histogram of unmatched encrypted blobs.
    print()
    print("=" * 70)
    print("Unmatched encrypted size histogram:")
    bins = [
        ("<1 KB",        0,         1024),
        ("1-10 KB",      1024,      10240),
        ("10-100 KB",    10240,     102400),
        ("100 KB-1 MB",  102400,    1048576),
        ("1-5 MB",       1048576,   5242880),
        (">5 MB",        5242880,   1 << 31),
    ]
    for label, lo, hi in bins:
        n = sum(1 for e in unmatched_enc if lo <= e["size"] < hi)
        b = sum(e["size"] for e in unmatched_enc if lo <= e["size"] < hi)
        print(f"  {label:13}: {n:4d} blobs   ({_human(b):>10})")

    # Top 20 largest unmatched encrypted blobs (likely music or large sprite atlases).
    print()
    print("=" * 70)
    print("Top 20 largest unmatched encrypted blobs (rename hints):")
    print("  size         md5(wordswapped) — content-class heuristic")
    big = sorted(unmatched_enc, key=lambda e: -e["size"])[:20]
    for e in big:
        sz = e["size"]
        if sz > 1_000_000:
            hint = "(probable music track)"
        elif sz > 200_000:
            hint = "(probable sprite atlas / mid music)"
        elif sz > 50_000:
            hint = "(probable sprite metadata / SFX)"
        else:
            hint = "(probable per-class sprite anim .bin)"
        print(f"  {_human(sz):>10}  {e['hash'].hex()}  {hint}")

    # Recovery percentages.
    print()
    print("=" * 70)
    pct_files = 100.0 * (file_count - len(unmatched)) / file_count
    recovered_bytes = sum(e["size"] for e in entries if e["hash"] in matched)
    pct_bytes = 100.0 * recovered_bytes / sum(e["size"] for e in entries)
    print(f"Coverage: {pct_files:.1f}% of files, {pct_bytes:.1f}% of bytes recovered.")


if __name__ == "__main__":
    main()
