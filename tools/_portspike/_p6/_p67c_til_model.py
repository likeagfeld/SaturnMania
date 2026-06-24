#!/usr/bin/env python3
"""_p67c_til_model.py -- P6.7c offline model of the SH-2 LoadTileConfig result.

Replicates RSDK::LoadTileConfig (rsdkv5-src/RSDKv5/RSDK/Scene/Scene.cpp:733-979,
SATURN ARM: the FlipX/Y/XY expansion at :883-970 is compiled out by
`#if RETRO_PLATFORM != RETRO_SATURN`, COLLISION_FLIPCOUNT==1 Scene.hpp:50)
against the UNTOUCHED retail Data.rsdk, producing the byte-exact SH-2 memory
images of:

  CollisionMask[CPATH_COUNT][TILE_COUNT]   2*1024*64 B = 131,072 B
      per tile (Scene.hpp:177-182, all uint8, no padding):
      floorMasks[16] lWallMasks[16] rWallMasks[16] roofMasks[16]
  TileInfo[CPATH_COUNT][TILE_COUNT]        2*1024*5 B  =  10,240 B
      per tile (Scene.hpp:184-190, all uint8, sizeof==5 verified vs GCC SH-2):
      floorAngle lWallAngle rWallAngle roofAngle flag

and their djb2-xor hashes (h=5381; h=((h<<5)+h)^byte, 32-bit wrap -- the
project's P6 witness-hash convention, e.g. P6.5a tileset hash).

MEASURED PACK FACT (2026-06-10): retail 1.03 Data.rsdk has NO
Data/Stages/Title/TileConfig.bin (hash miss; the Title folder ships only
16x16Tiles.gif / Scene1.bin / StageConfig.bin). LoadSceneFolder still
requests it (Scene.cpp:163-164) and LoadTileConfig NO-OPS on the LoadFile
miss (:738) -- collisionMasks/tileInfo keep their pre-state. The model
mirrors that exactly: a missing TileConfig yields ALL-ZERO images (the P6
pack zero-fills the WRAM-L backing before InitStorage), so the Title gate
hashes are the zero-image constants. The real parse exercise is
Data/Stages/GHZ/TileConfig.bin (2,620 B, encrypted).

On-disk format (verified vs Core/Reader.hpp:466-485 ReadCompressed +
Scene.cpp:738-746):
  u32 LE signature   'TIL\\0' (0x004C4954; bytes 54 49 4C 00)
  u32 LE total       bytes following this field (= 4 + zlib stream length)
  u32 BE uncompressedSize
  zlib stream of (total-4) bytes (0x78 ...)
Uncompressed payload = CPATH_COUNT(2) * TILE_COUNT(1024) tiles * 38 B:
  [16 maskHeights][16 maskActive][u8 yFlip]
  [u8 floorAngle][u8 lWallAngle][u8 rWallAngle][u8 roofAngle][u8 flag]

Usage:
  python tools/_portspike/_p6/_p67c_til_model.py [pack_path] [stageFolder]
  (default pack: <repo>/cd/DATA.RSDK -- byte-identical to <repo>/Data.rsdk,
   both 182,962,115 B; default stageFolder: Title)

API:
  model_hashes(pack_path, stage="Title") -> (cmhash, tihash)
"""
import os
import struct
import sys
import zlib

_REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                      "..", "..", ".."))
sys.path.insert(0, os.path.join(_REPO, "tools"))
import rsdk_extract  # lookup_hash / parse_datapack / decrypt (Reader.cpp port)

TILE_SIZE   = 0x10      # Scene.hpp:8
TILE_COUNT  = 0x400     # Scene.hpp:7
CPATH_COUNT = 2         # Scene.hpp:29
SIG_TIL     = 0x4C4954  # Scene.hpp:59
TILE_REC    = 2 * TILE_SIZE + 6  # 38 B on-disk per tile
CM_BYTES    = CPATH_COUNT * TILE_COUNT * 4 * TILE_SIZE  # 131,072
TI_BYTES    = CPATH_COUNT * TILE_COUNT * 5              #  10,240


def _extract_tileconfig(pack_path, stage):
    """rsdk_extract.lookup_hash + parse_datapack -> (bytes|None, csize|0).

    Path exactly as the engine requests it: Scene.cpp:163
    "Data/Stages/%s/TileConfig.bin". None = hash miss = engine no-op."""
    vpath = "Data/Stages/%s/TileConfig.bin" % stage
    raw, _, entries = rsdk_extract.parse_datapack(pack_path)
    by_hash = {e["hash"]: e for e in entries}
    e = by_hash.get(rsdk_extract.lookup_hash(vpath))
    if e is None:
        return None
    blob = raw[e["offset"]:e["offset"] + e["size"]]
    if e["encrypted"]:
        blob = rsdk_extract.decrypt(blob, vpath, e["size"])
    return blob


def _read_compressed(blob, pos):
    """Reader.hpp:466-485 ReadCompressed framing."""
    total = struct.unpack_from("<I", blob, pos)[0]      # :471 ReadInt32 LE
    pos += 4
    size_be = struct.unpack_from(">I", blob, pos)[0]    # :472+:474 byteswapped
    pos += 4
    csize = total - 4                                   # :471 "- 4"
    payload = zlib.decompress(blob[pos:pos + csize])    # :481 mz_uncompress
    if len(payload) != size_be:
        raise SystemExit("FAIL: inflate %d != header sizeLE %d"
                         % (len(payload), size_be))
    return payload, csize


def model_buffers(pack_path, stage="Title"):
    """Returns (cm_image, ti_image, csize): the two SH-2 memory images +
    the in-pack zlib stream size (0 if the stage has no TileConfig --
    DATASET_TMP budget input). Missing file == engine no-op == all-zero
    images (zero-filled WRAM-L backing pre-state)."""
    cm = bytearray(CM_BYTES)
    ti = bytearray(TI_BYTES)

    blob = _extract_tileconfig(pack_path, stage)
    if blob is None:                                    # Scene.cpp:738 miss
        return bytes(cm), bytes(ti), 0

    sig = struct.unpack_from("<I", blob, 0)[0]          # Scene.cpp:739
    if sig != SIG_TIL:                                  # Scene.cpp:740-742
        return bytes(cm), bytes(ti), 0
    buf, csize = _read_compressed(blob, 4)              # Scene.cpp:746
    expect = CPATH_COUNT * TILE_COUNT * TILE_REC
    if len(buf) != expect:
        raise SystemExit("FAIL: payload %d != %d" % (len(buf), expect))

    pos = 0
    for p in range(CPATH_COUNT):                        # Scene.cpp:749
        for t in range(TILE_COUNT):                     # Scene.cpp:751
            heights = buf[pos:pos + TILE_SIZE]; pos += TILE_SIZE   # :755
            active  = buf[pos:pos + TILE_SIZE]; pos += TILE_SIZE   # :757
            yflip = buf[pos]                                       # :760
            ti_rec = buf[pos + 1:pos + 6]   # floor,lWall,rWall,roof,flag :761-765
            pos += 6

            floor = [0] * TILE_SIZE
            lwall = [0] * TILE_SIZE
            rwall = [0] * TILE_SIZE
            roof  = [0] * TILE_SIZE

            if yflip:                                   # Scene.cpp:767
                for c in range(TILE_SIZE):              # :768-777
                    if active[c]:
                        floor[c] = 0x00
                        roof[c]  = heights[c]
                    else:
                        floor[c] = 0xFF
                        roof[c]  = 0xFF
                # LWall: h scans 0..15 UP, source = roofMasks, cond c <= m :780-799
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
                        h += 1
                # RWall: h scans 15..0 DOWN, source = roofMasks, cond c <= m :802-821
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
                        h -= 1
            else:                                       # Scene.cpp:823 regular
                for c in range(TILE_SIZE):              # :826-835
                    if active[c]:
                        floor[c] = heights[c]
                        roof[c]  = 0x0F
                    else:
                        floor[c] = 0xFF
                        roof[c]  = 0xFF
                # LWall: h scans 0..15 UP, source = floorMasks, cond c >= m :838-857
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
                        h += 1
                # RWall: h scans 15..0 DOWN, source = floorMasks, cond c >= m :860-879
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
                        h -= 1
            # SATURN ARM ends here: Scene.cpp:883 `#if RETRO_PLATFORM !=
            # RETRO_SATURN` skips the FlipX/Y/XY expansion (x1 arrays).

            base = ((p * TILE_COUNT) + t) * 64          # path-major, 64 B/tile
            cm[base +  0:base + 16] = bytes(floor)      # Scene.hpp:178
            cm[base + 16:base + 32] = bytes(lwall)      # Scene.hpp:179
            cm[base + 32:base + 48] = bytes(rwall)      # Scene.hpp:180
            cm[base + 48:base + 64] = bytes(roof)       # Scene.hpp:181
            tb = ((p * TILE_COUNT) + t) * 5             # sizeof(TileInfo)==5
            ti[tb:tb + 5] = ti_rec                      # Scene.hpp:185-189

    return bytes(cm), bytes(ti), csize


def djb2x(data):
    """P6 witness hash: h=5381; h=((h<<5)+h)^byte, 32-bit wrap."""
    h = 5381
    for b in data:
        h = (((h << 5) + h) ^ b) & 0xFFFFFFFF
    return h


def model_hashes(pack_path, stage="Title"):
    """(cmhash, tihash) over the exact SH-2 images of CollisionMask[2][1024]
    and TileInfo[2][1024] after LoadTileConfig for <stage>."""
    cm, ti, _ = model_buffers(pack_path, stage)
    return djb2x(cm), djb2x(ti)


if __name__ == "__main__":
    pack = sys.argv[1] if len(sys.argv) > 1 else os.path.join(_REPO, "cd", "DATA.RSDK")
    stage = sys.argv[2] if len(sys.argv) > 2 else "Title"
    cm, ti, csize = model_buffers(pack, stage)
    print("pack:  %s" % pack)
    print("stage: %s  (TileConfig.bin %s)" % (stage,
          "zlib stream %d B in pack" % csize if csize else
          "ABSENT in pack -> engine no-op -> zero images"))
    print("cm image: %d B   ti image: %d B" % (len(cm), len(ti)))
    print("cmhash = 0x%08X" % djb2x(cm))
    print("tihash = 0x%08X" % djb2x(ti))
    print("tile[0][0] floorMasks: %s" % cm[0:16].hex())
    print("tile[0][0] lWallMasks: %s" % cm[16:32].hex())
    print("tile[0][0] rWallMasks: %s" % cm[32:48].hex())
    print("tile[0][0] roofMasks:  %s" % cm[48:64].hex())
    print("tileInfo[0][0] (floorA,lWallA,rWallA,roofA,flag): %s" % ti[0:5].hex())
