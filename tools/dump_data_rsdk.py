#!/usr/bin/env python3
"""dump_data_rsdk.py — offline extractor for RSDKv5 Data.rsdk packs.

Implements the format documented in docs/rsdkv5_engine_catalog.md §1.1:

    offset  size   field
    ------  -----  ------------------------------------------
    0x00    4      signature        = "RSDK" (0x4B445352 LE)
    0x04    1      'v'
    0x05    1      version char     (typically 'B' for Mania)
    0x06    2      fileCount        uint16 LE (≤ 0x1000)
    0x08    N*0x18 file table       (24 bytes per entry):
                     +0x00 16  md5Hash       uint8[16]
                     +0x10  4  offset        int32 LE
                     +0x14  4  sizeAndFlag   int32 LE
                                bit 31 = encrypted flag
                                bits 0..30 = file size
    ...                file data blobs

Mania ships almost entirely UNENCRYPTED. The cipher (eLoad) is documented
in §1.3 but not implemented here — encountered encrypted files produce
a warning and are skipped. Add the cipher later if any shipping file
trips it.

Hash lookup: MD5 of the LOWERCASED forward-slash POSIX file path
(e.g. "data/sprites/global/playertails.bin"). Since the pack stores only
the hash, the extractor needs a NAME LIST to recover original paths.
Without a name list we still extract files but they're named
"<hashhex>.bin" — useful for inspection but not for downstream tools.

Usage:
    python tools/dump_data_rsdk.py <Data.rsdk> --out extracted/
    python tools/dump_data_rsdk.py <Data.rsdk> --out extracted/ \
        --names tools/rsdk_name_dict.txt

The name dictionary is one path per line; the script MD5s each line and
maps matching hashes to filenames. Existing files under --out are
overwritten."""
import argparse
import hashlib
import os
import struct
import sys


RSDK_SIG = 0x4B445352  # "RSDK"


def read_pack(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < 8:
        sys.exit(f"{path}: file too small")
    sig, vmark, vchar, count = struct.unpack_from("<IcBH", data, 0)
    if sig != RSDK_SIG:
        sys.exit(f"{path}: signature mismatch (got 0x{sig:08X}, expected 0x{RSDK_SIG:08X})")
    if vmark != b"v":
        sys.exit(f"{path}: version marker missing (got {vmark!r})")
    if count == 0 or count > 0x1000:
        sys.exit(f"{path}: implausible fileCount = {count}")
    print(f"Pack: '{vmark.decode()}{chr(vchar)}', {count} files, {len(data):,} bytes total")
    return data, count


def parse_table(data, count):
    """Yield (hash_bytes_16, offset, size, encrypted) for each entry."""
    p = 8
    for i in range(count):
        md5 = data[p:p+16]
        off, sz_flag = struct.unpack_from("<ii", data, p + 16)
        enc = bool(sz_flag & 0x80000000)
        sz = sz_flag & 0x7FFFFFFF
        yield md5, off, sz, enc
        p += 24


def _wordswap16(d):
    """Reverse each 4-byte group of a 16-byte MD5 digest. RSDKv5 reads
    stored hashes as big-endian u32s but computes file hashes as little-
    endian u32s, so the on-disk hash is the digest with each 4-byte word
    reversed. (Matches tools/rsdk_extract.py:_wordswap; differs from the
    plain MD5 used inside Scene.bin's class-name hashes.)"""
    out = bytearray(16)
    for y in range(0, 16, 4):
        out[y+0], out[y+1], out[y+2], out[y+3] = d[y+3], d[y+2], d[y+1], d[y+0]
    return bytes(out)


def load_name_dict(path):
    """Return {hash_hex: filename} for each line in the name dictionary.
    Hash is wordswap(MD5(lowercase(path))) per RSDKv5 Data.rsdk
    convention (verified against tools/rsdk_extract.py, which has
    successfully recovered 993 Mania files from the same pack)."""
    if not path or not os.path.exists(path):
        return {}
    out = {}
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            name = line.strip()
            if not name or name.startswith("#"):
                continue
            digest = _wordswap16(hashlib.md5(name.lower().encode("utf-8")).digest())
            out[digest.hex()] = name
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pack", help="path to Data.rsdk")
    ap.add_argument("--out", default="extracted",
                    help="output directory root (default: extracted/)")
    ap.add_argument("--names", default=None,
                    help="optional name-dictionary file (one path per line) "
                         "to resolve hashes back to filenames")
    ap.add_argument("--max-files", type=int, default=0,
                    help="dump only the first N entries (0 = all)")
    args = ap.parse_args()

    data, count = read_pack(args.pack)
    name_dict = load_name_dict(args.names)
    if args.names:
        print(f"Name dict: {len(name_dict)} entries loaded from {args.names}")

    n_named = n_unnamed = n_encrypted = 0
    n_done = 0
    for md5, off, sz, enc in parse_table(data, count):
        if args.max_files and n_done >= args.max_files:
            break
        n_done += 1
        hexhash = md5.hex()
        name = name_dict.get(hexhash)
        if enc:
            n_encrypted += 1
            print(f"  [skip enc] {hexhash} @ {off:#x} size={sz}")
            continue
        if name:
            outpath = os.path.join(args.out, name)
            n_named += 1
        else:
            outpath = os.path.join(args.out, "_unnamed", hexhash + ".bin")
            n_unnamed += 1
        os.makedirs(os.path.dirname(outpath) or ".", exist_ok=True)
        with open(outpath, "wb") as f:
            f.write(data[off:off+sz])
    print(f"Done: {n_named} named, {n_unnamed} unnamed, {n_encrypted} encrypted "
          f"(skipped).  Output root: {args.out}")
    if n_unnamed and not args.names:
        print("Hint: pass --names tools/rsdk_name_dict.txt to recover "
              "original filenames. The dict file is a flat list of POSIX "
              "lowercase paths (e.g. 'data/sprites/global/ring.bin'); each "
              "line gets MD5'd to match the pack entry.")


if __name__ == "__main__":
    main()
