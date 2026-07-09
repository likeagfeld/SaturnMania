#!/usr/bin/env python3
"""build_tilemasks.py - Task #180 step 1 (step 3: compacted).

Build cd/GHZ1MASK.BIN: the resident collision-mask + tile-info tables for the
decomp-faithful 6-sensor collision model. This is a mechanical port of
RSDKv5 Scene.cpp::LoadTileConfig (lines 719-866) over the shipped
extracted/Data/Stages/GHZ/TileConfig.bin, followed by a Saturn-only
COMPACTION pass that keeps only the collision tiles the shipped GHZ1 layout
actually references.

Source contract (Scene.cpp:719-751):
  "TIL\\0" signature (u32) + ReadCompressed payload.
  Decompressed = CPATH_COUNT(2) planes x TILE_COUNT(1024) tiles x 38 B/tile.
  Per-tile 38 B record:
    maskHeights[16] + maskActive[16] + yFlip(1)
    + floorAngle(1) + lWallAngle(1) + rWallAngle(1) + roofAngle(1) + flag(1)

Derived (Scene.cpp:753-866) per the regular / yFlip branch into the runtime
CollisionMask (Scene.hpp:134-139, 64 B):
    floorMasks[16], lWallMasks[16], rWallMasks[16], roofMasks[16]
and TileInfo (Scene.hpp:141-147, 5 B):
    floorAngle, lWallAngle, rWallAngle, roofAngle, flag

The decomp precomputes 3 extra flip variants per tile (FLIP_X/Y/XY ->
collisionMasks[plane][TILE_COUNT*4]). Saturn ships ONLY the base per plane
and applies the flip transform at lookup time (collision.c m_* / a_* helpers).

#180 step-3 COMPACTION (the LWRAM budget fix)
---------------------------------------------
The full base-1024 GMSK table is 147464 B (~144 KB). The only LWRAM region
freed by #180 (the deleted GHZ1SURF heightmap, 0x200000..+0x10000) is 64 KB.
Measured: cd/GHZ1COL.BIN references only ~217 DISTINCT base tiles across the
two GHZ1 collision layers (planeA-solid bits 0x3000, planeB-solid 0xC000).
So we emit ONLY those referenced tiles plus a reserved blank slot 0, indexed
through a tileID->slot remap table. Result ~33 KB, fits the 64 KB carve with
room for the streamed 2D collision-layout window.

The remap is SHARED across both planes (collision.c indexes
`slot = remap[tile & 0x3FF]` independent of plane); a tile solid in only one
plane still carries both planes' derived masks (harmless ~few KB).

GHZ1MASK.BIN layout (GMS2, all multi-byte fields big-endian for SH-2):
    [0..3]   magic 'GMS2'
    [4..5]   u16 num_planes  (2)
    [6..7]   u16 slot_count  (S = 1 + referenced-union size)
    [8..9]   u16 tile_count  (1024)            -- remap-table length
    [10..11] u16 reserved    (0)
    remap block: tile_count u16 (tileID -> slot; 0 = blank/unreferenced)
    masks block: plane0 [S * 64 B], plane1 [S * 64 B]
    info  block: plane0 [S *  8 B], plane1 [S *  8 B]
                 (8 B = floorAngle,lWallAngle,rWallAngle,roofAngle,flag,0,0,0)
  Slot 0 is the synthetic blank tile: masks all 0xFF, info all 0.

GHZ1COL.BIN (GCOL) must be built FIRST (tools/build_collayout.py); build.bat
sequences build_collayout before build_tilemasks.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from render_scene import R  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TILECONFIG = os.path.join(ROOT, "extracted", "Data", "Stages", "GHZ",
                          "TileConfig.bin")
COLBIN = os.path.join(ROOT, "cd", "GHZ1COL.BIN")
OUT = os.path.join(ROOT, "cd", "GHZ1MASK.BIN")

TILE_COUNT = 0x400   # 1024 (Scene.hpp)
TILE_SIZE = 0x10     # 16
CPATH_COUNT = 2

# Solidity nibble bits (layout entry bits 12-15), per collision.c:166-170.
#   planeA: floor(down) 0x1000 | roof/wall(up) 0x2000  -> 0x3000
#   planeB: floor(down) 0x4000 | roof/wall(up) 0x8000  -> 0xC000
SOLID_A = 0x3000
SOLID_B = 0xC000


def derive_tile(mask_heights, mask_active, yflip):
    """Port of Scene.cpp:753-866. Returns (floor[16],lwall[16],rwall[16],
    roof[16]) for ONE base tile (no flip variants)."""
    floor = [0] * TILE_SIZE
    lwall = [0] * TILE_SIZE
    rwall = [0] * TILE_SIZE
    roof = [0] * TILE_SIZE

    if yflip:
        # Scene.cpp:753-807
        for c in range(TILE_SIZE):
            if mask_active[c]:
                floor[c] = 0x00
                roof[c] = mask_heights[c]
            else:
                floor[c] = 0xFF
                roof[c] = 0xFF
        # LWall rotations (scan roof columns) 766-785
        for c in range(TILE_SIZE):
            h = 0
            while True:
                if h == TILE_SIZE:
                    lwall[c] = 0xFF
                    break
                m = roof[h]
                if m != 0xFF and c <= m:
                    lwall[c] = h
                    break
                else:
                    h += 1
                    if h <= -1:
                        break
        # RWall rotations 788-807
        for c in range(TILE_SIZE):
            h = TILE_SIZE - 1
            while True:
                if h == -1:
                    rwall[c] = 0xFF
                    break
                m = roof[h]
                if m != 0xFF and c <= m:
                    rwall[c] = h
                    break
                else:
                    h -= 1
                    if h >= TILE_SIZE:
                        break
    else:
        # Regular tile Scene.cpp:809-866
        for c in range(TILE_SIZE):
            if mask_active[c]:
                floor[c] = mask_heights[c]
                roof[c] = 0x0F
            else:
                floor[c] = 0xFF
                roof[c] = 0xFF
        # LWall rotations (scan floor columns) 823-843
        for c in range(TILE_SIZE):
            h = 0
            while True:
                if h == TILE_SIZE:
                    lwall[c] = 0xFF
                    break
                m = floor[h]
                if m != 0xFF and c >= m:
                    lwall[c] = h
                    break
                else:
                    h += 1
                    if h <= -1:
                        break
        # RWall rotations 845-865
        for c in range(TILE_SIZE):
            h = TILE_SIZE - 1
            while True:
                if h == -1:
                    rwall[c] = 0xFF
                    break
                m = floor[h]
                if m != 0xFF and c >= m:
                    rwall[c] = h
                    break
                else:
                    h -= 1
                    if h >= TILE_SIZE:
                        break
    return floor, lwall, rwall, roof


def referenced_union():
    """Parse cd/GHZ1COL.BIN (GCOL) and return the sorted list of DISTINCT
    base tile IDs that any solid cell references (planeA OR planeB)."""
    if not os.path.exists(COLBIN):
        print("ERROR: %s not found - run build_collayout.py first" % COLBIN)
        return None
    with open(COLBIN, "rb") as f:
        d = f.read()
    # Accept 'GCOL' (legacy row-major), 'GCO2' (#180 step 3d column-major) and
    # 'GCO3' (#180 step 4c block-DEFLATE column-major); the referenced-tile
    # union iterates every cell, so the on-disk ordering / compression is
    # irrelevant to this pass.
    if d[:4] == b"GCO3":
        return _referenced_union_gco3(d)
    if d[:4] not in (b"GCOL", b"GCO2"):
        print("ERROR: bad GHZ1COL signature: %r" % d[:4])
        return None
    num_layers, _ts = struct.unpack(">HH", d[4:8])
    off = 8
    ref = set()
    for _ in range(num_layers):
        _lid, xs, ys, _wsh = struct.unpack(">HHHH", d[off:off + 8])
        off += 8
        cnt = xs * ys
        grid = struct.unpack(">%dH" % cnt, d[off:off + cnt * 2])
        off += cnt * 2
        for e in grid:
            if e == 0xFFFF:
                continue
            if e & SOLID_A or e & SOLID_B:
                ref.add(e & 0x3FF)
    return sorted(ref)


def _referenced_union_gco3(d):
    """Distinct solid base-tile IDs from a 'GCO3' file: inflate every block
    (raw DEFLATE, zlib wbits=-15) and scan all decoded u16 BE entries. Cell
    order is irrelevant to the union, so no transpose is needed."""
    import zlib
    num_layers, _ts, _bc, _res = struct.unpack(">HHHH", d[4:12])
    off = 12
    descs = []
    for _ in range(num_layers):
        lid, xs, ys, wsh, nblk, _pad = struct.unpack(">HHHHHH", d[off:off + 12])
        descs.append((nblk,))
        off += 12
    tables = []
    for (nblk,) in descs:
        offs = struct.unpack(">%dI" % (nblk + 1), d[off:off + 4 * (nblk + 1)])
        tables.append(offs)
        off += 4 * (nblk + 1)
    ref = set()
    for li, (nblk,) in enumerate(descs):
        offs = tables[li]
        for b in range(nblk):
            raw = zlib.decompress(d[offs[b]:offs[b + 1]], -15)
            for e in struct.unpack(">%dH" % (len(raw) // 2), raw):
                if e == 0xFFFF:
                    continue
                if e & SOLID_A or e & SOLID_B:
                    ref.add(e & 0x3FF)
    return sorted(ref)


def main():
    if not os.path.exists(TILECONFIG):
        print("ERROR: TileConfig.bin not found: %s" % TILECONFIG)
        return 1
    union = referenced_union()
    if union is None:
        return 1

    with open(TILECONFIG, "rb") as f:
        d = f.read()
    if d[:4] != b"TIL\x00":
        print("ERROR: bad TileConfig signature: %r" % d[:4])
        return 1
    r = R(d)
    r.p = 4
    buf = r.compressed()
    expect = CPATH_COUNT * TILE_COUNT * 38
    if len(buf) != expect:
        print("ERROR: decompressed %d bytes, expected %d" % (len(buf), expect))
        return 1

    masks = [[None] * TILE_COUNT for _ in range(CPATH_COUNT)]
    info = [[None] * TILE_COUNT for _ in range(CPATH_COUNT)]

    pos = 0
    for p in range(CPATH_COUNT):
        for t in range(TILE_COUNT):
            mh = list(buf[pos:pos + TILE_SIZE]); pos += TILE_SIZE
            ma = list(buf[pos:pos + TILE_SIZE]); pos += TILE_SIZE
            yflip = buf[pos]; pos += 1
            floor_a = buf[pos]; pos += 1
            lwall_a = buf[pos]; pos += 1
            rwall_a = buf[pos]; pos += 1
            roof_a = buf[pos]; pos += 1
            flag = buf[pos]; pos += 1
            fl, lw, rw, rf = derive_tile(mh, ma, yflip)
            masks[p][t] = (fl, lw, rw, rf)
            info[p][t] = (floor_a, lwall_a, rwall_a, roof_a, flag)

    # Build remap: slot 0 reserved (blank); referenced tiles -> 1..N (sorted).
    remap = [0] * TILE_COUNT
    slot_tiles = [None]          # slot 0 = blank sentinel
    for tid in union:
        remap[tid] = len(slot_tiles)
        slot_tiles.append(tid)
    slot_count = len(slot_tiles)

    out = bytearray()
    out += b"GMS2"
    out += struct.pack(">HHHH", CPATH_COUNT, slot_count, TILE_COUNT, 0)
    # remap table
    out += struct.pack(">%dH" % TILE_COUNT, *remap)
    # masks: per plane, slot 0 blank then referenced tiles
    BLANK_MASK = bytes([0xFF] * 64)
    for p in range(CPATH_COUNT):
        out += BLANK_MASK
        for tid in slot_tiles[1:]:
            fl, lw, rw, rf = masks[p][tid]
            out += bytes(fl) + bytes(lw) + bytes(rw) + bytes(rf)
    # info: per plane, slot 0 blank (all zero) then referenced tiles
    for p in range(CPATH_COUNT):
        out += bytes(8)
        for tid in slot_tiles[1:]:
            fa, la, ra, roa, flag = info[p][tid]
            out += bytes((fa, la, ra, roa, flag, 0, 0, 0))

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(out)

    full = 8 + CPATH_COUNT * TILE_COUNT * (64 + 8)
    print("build_tilemasks: wrote %s (%d bytes, GMS2 compacted)" %
          (OUT, len(out)))
    print("  planes=%d referenced-tiles=%d slot_count=%d (full would be %d B)" %
          (CPATH_COUNT, len(union), slot_count, full))
    print("  saved %d B vs full-1024 table" % (full - len(out)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
