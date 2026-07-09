#!/usr/bin/env python3
"""build_sprite_packs.py - Task #180 step 5 (resident compressed sprite packs).

WHY THIS EXISTS: GHZ music is CD-DA track 2. The Saturn CD has ONE head, so any
data-sector read during gameplay seeks the head off the audio track -> music/SFX
drop + main-loop stall on GFS (user-reported "super slow, no music, no SFX").
#180 step 4c made the collision layout RAM-resident (cd/GHZ1COL.BIN 'GCO3'). The
biggest remaining gameplay-time CD reader was the PLAYER atlas:

  src/rsdk/player_atlas.c  _cache_load() -> jo_fs_read_file_ptr(SONICnn.SP2)

Task #192 ("player-only resident", user decision 2026-06-02): ONLY the player
is packed. The entity resident pack (formerly GHZENT.SPC) was retired because
it byte-collided with the live #188 FG.CEL LWRAM region and crashed the
foreground after the title card; entity SP2 files revert to occasional CD
streaming (entity_atlas.c _blob_get).

This tool packs the player SP2 pixel slices into a raw-DEFLATE 'SPC1' container
(cd/SONIC.SPC) that the engine loads ONCE into resident LWRAM at scene start,
then inflates RAM->RAM via the embedded puff inflater during play (the same
scheme as GCO3 / colwindow.c). Zero CD access for the frequent player per-anim
changes; full frame parity (every shipped player SP2 frame is still present,
just compressed + resident).

Measured DEFLATE ratio for SP2 sprite art (mostly transparent palette-0 runs) is
~0.10, so the full player set (351 KB raw) packs to ~36 KB and the full GHZ
entity set (544 KB raw) packs to ~46 KB -- both fit the existing 192 KB LWRAM
pools with room to spare.

'SPC1' format (all multi-byte fields big-endian for SH-2):
  [0..3]   magic 'SPC1'
  [4..5]   u16 count
  [6..7]   u16 _reserved (0)
  index table (count x 32 B), in name-sorted order:
     char[16] name      (NUL-padded stem, e.g. "RING" / "SONIC00"; <=15 chars)
     u32 comp_off       (absolute file offset of this entry's DEFLATE payload)
     u32 comp_len       (compressed byte length)
     u32 raw_len        (decompressed SPR2 byte length = original .SP2 size)
     u32 _reserved (0)
  raw-DEFLATE (zlib wbits=-15) payloads, concatenated, in index order.

Each payload inflates to the byte-identical original SPR2 blob ("SPR2" magic +
u16 frame_count + u16 reserved + per-frame u16 w,u16 h,w*h u16 BGR1555 pixels),
so the C-side SP2 parsers are unchanged -- only the SOURCE of the bytes swaps
from a CD file read to a puff() of the resident pack.
"""
import glob
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CD = os.path.join(ROOT, "cd")

INDEX_REC = 32
NAME_LEN = 16


def deflate(raw):
    """Raw DEFLATE (no zlib header/adler) -- exactly what puff() decodes.
    Mirrors tools/build_collayout.py deflate()."""
    co = zlib.compressobj(9, zlib.DEFLATED, -15)
    return co.compress(raw) + co.flush()


def build_pack(entries, out_path):
    """entries = [(stem, raw_bytes), ...]. Writes an 'SPC1' container."""
    entries = sorted(entries, key=lambda e: e[0])
    count = len(entries)
    header_len = 8 + count * INDEX_REC

    payloads = []
    index = bytearray()
    off = header_len
    raw_total = 0
    comp_total = 0
    for stem, raw in entries:
        nm = stem.encode("ascii")
        if len(nm) > NAME_LEN - 1:
            raise ValueError("pack key %r > %d chars" % (stem, NAME_LEN - 1))
        comp = deflate(raw)
        # Round-trip self-check: the C side puff()s this back, so prove it here.
        if zlib.decompress(comp, -15) != raw:
            raise ValueError("DEFLATE round-trip failed for %s" % stem)
        index += nm + b"\x00" * (NAME_LEN - len(nm))
        index += struct.pack(">IIII", off, len(comp), len(raw), 0)
        payloads.append(comp)
        off += len(comp)
        raw_total += len(raw)
        comp_total += len(comp)

    blob = bytearray()
    blob += b"SPC1"
    blob += struct.pack(">HH", count, 0)
    blob += index
    for p in payloads:
        blob += p
    assert len(blob) == off, (len(blob), off)

    with open(out_path, "wb") as f:
        f.write(blob)
    print("  wrote %s: %d entries, %d B (raw %d B -> %.1f%%, %.1fx smaller)"
          % (os.path.relpath(out_path, ROOT), count, len(blob), raw_total,
             100.0 * len(blob) / max(1, raw_total),
             raw_total / max(1, len(blob))))
    return len(blob)


def main():
    all_sp2 = sorted(glob.glob(os.path.join(CD, "*.SP2")))
    player = []
    entity = []
    for f in all_sp2:
        stem = os.path.splitext(os.path.basename(f))[0]
        if stem == "SONIC":
            continue  # combined slice, unused by the runtime
        raw = open(f, "rb").read()
        if stem.startswith("SONIC") and stem[5:7].isdigit():
            player.append((stem, raw))   # SONIC00..SONIC14
        else:
            entity.append((stem, raw))   # RING, MOTOBUG, ... TITLCARD

    if not player:
        print("ERROR: no SONICnn.SP2 player slices found in cd/", file=sys.stderr)
        return 1
    # Task #192 (player-only resident): entities are NOT packed. The entity
    # resident pack byte-collided with the live #188 FG.CEL LWRAM region and
    # crashed the foreground after the title card; entities revert to CD
    # streaming of their <NAME>.SP2 files (entity_atlas.c). Only SONIC.SPC is
    # built. `entity` is intentionally left unpacked.
    _ = entity

    print("=== build_sprite_packs: resident compressed sprite containers ===")
    p = build_pack(player, os.path.join(CD, "SONIC.SPC"))
    print("  resident LWRAM budget = %d B (player-only; entities CD-stream)" % p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
