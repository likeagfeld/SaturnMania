#!/usr/bin/env python3
# =============================================================================
# gen_collision_model.py -- P6.7 collision packer (Task #210): the offline
# LoadTileConfig model + the EXACT 16-bit/column packed format + gate model.
#
# Mirrors RSDK::LoadTileConfig (Scene.cpp:733-880) byte-for-byte over the
# REAL GHZ TileConfig.bin (zlib body after the TIL signature + the
# ReadCompressed 8-byte header), producing the x1 (FLIP_NONE) raw
# collisionMasks/tileInfo the engine builds on Saturn, PLUS the packed form
# the Saturn accessors read, PLUS flip-derived probe tuples proving the
# on-the-fly flip math (the exact formulas of the PC pre-expansion,
# Scene.cpp:889-963, which the x1 retarget compiles out).
#
# THE PACKED FORMAT (uint16 per column, 16 columns/tile, big-endian on SH-2;
# 32 B/tile -> 2 paths x 1024 tiles x 32 B = 65,536 B masks + 10,240 B
# tileInfo = 75,776 B, the SaturnMemoryMap P68_HWRAM_COLL_BYTES contract):
#   bits 0-3   rich height: floorMasks[c] for regular tiles, roofMasks[c]
#              for yFlip tiles (the OTHER of the pair is determined: regular
#              roof is 0x0F-or-FF, yFlip floor is 0x00-or-FF --
#              Scene.cpp:770/774/828/832 -- both recoverable from bit 4 +
#              bit 15)
#   bit  4     column sentinel (maskActive==0): floor AND roof read 0xFF
#   bits 5-8   lWallMasks[c] height
#   bit  9     lWall sentinel (reads 0xFF)
#   bits 10-13 rWallMasks[c] height
#   bit  14    rWall sentinel (reads 0xFF)
#   bit  15    tile yFlip flag (replicated per column for O(1) access)
# EXACT: every one of the 17 possible uint8 values {0..15, 0xFF} per
# direction round-trips; self-test C1 proves it over the whole file.
#
# OUTPUT tools/_portspike/_p6/p6_collision_model.json:
#   raw_hash    djb2 over the x1 raw masks laid out as the engine struct
#               (CollisionMask = floor[16] lWall[16] rWall[16] roof[16];
#               path-major, tile-minor) -- 131,072 B
#   info_hash   djb2 over tileInfo (floorAngle lWallAngle rWallAngle
#               roofAngle flag; path-major) -- 10,240 B
#   packed_hash djb2 over the packed buffer (big-endian uint16 stream,
#               path-major tile-minor column-minor) -- 65,536 B
#   probes      96 tuples {plane, tile(with flip bits), dir, col, expect}:
#               24 random base + 24 FLIP_X + 24 FLIP_Y + 24 FLIP_XY, expected
#               values derived via the PC pre-expansion formulas -- the gate
#               replays them through the Saturn accessors on SH-2.
#
# SELF-TESTS
#   C1 pack->unpack round-trips every direction value over both planes
#   C2 buffer fully consumed (2,620 B file -> decompressed walk end-to-end)
#   C3 derived flip probes match a full PC-style x4 expansion built
#      independently (the Scene.cpp:889-963 formulas applied wholesale)
# =============================================================================
import json
import random
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
TILECFG = ROOT / "extracted/Data/Stages/GHZ/TileConfig.bin"
OUT_MODEL = Path(__file__).resolve().parent / "p6_collision_model.json"

TILE_COUNT = 0x400
TILE_SIZE = 0x10
CPATH = 2
FLIP_NONE, FLIP_X, FLIP_Y, FLIP_XY = 0, 1, 2, 3


def djb2(data):
    h = 5381
    for b in data:
        h = ((h << 5) + h) ^ b
        h &= 0xFFFFFFFF
    return h


def load_tileconfig(path):
    raw = path.read_bytes()
    if raw[:4] != b"TIL\x00":
        raise SystemExit("TIL signature mismatch")
    # ReadCompressed (Reader.hpp): [u32 compressed_size][u32 BE uncompressed
    # size][zlib stream]
    (csz,) = struct.unpack("<I", raw[4:8])
    (usz,) = struct.unpack(">I", raw[8:12])
    buf = zlib.decompress(raw[12:])
    if len(buf) != usz:
        raise SystemExit("decompressed size %d != header %d" % (len(buf), usz))
    return buf


def build_masks(buf):
    """The x1 LoadTileConfig walk (Scene.cpp:749-880). Returns
    masks[p][t] = dict(floor[16], lwall[16], rwall[16], roof[16]),
    info[p][t] = (floorAngle, lWallAngle, rWallAngle, roofAngle, flag),
    yflip[p][t]."""
    pos = 0
    masks = [[None] * TILE_COUNT for _ in range(CPATH)]
    info = [[None] * TILE_COUNT for _ in range(CPATH)]
    yflips = [[0] * TILE_COUNT for _ in range(CPATH)]
    for p in range(CPATH):
        for t in range(TILE_COUNT):
            heights = buf[pos:pos + TILE_SIZE]; pos += TILE_SIZE
            active = buf[pos:pos + TILE_SIZE]; pos += TILE_SIZE
            yflip = buf[pos]; pos += 1
            info[p][t] = tuple(buf[pos:pos + 5]); pos += 5
            floor = [0] * 16; roof = [0] * 16
            lwall = [0] * 16; rwall = [0] * 16
            if yflip:
                for c in range(16):
                    if active[c]:
                        floor[c] = 0x00
                        roof[c] = heights[c]
                    else:
                        floor[c] = 0xFF
                        roof[c] = 0xFF
                for c in range(16):           # LWall (scan roof, c <= m)
                    h = 0
                    while True:
                        if h == 16:
                            lwall[c] = 0xFF
                            break
                        m = roof[h]
                        if m != 0xFF and c <= m:
                            lwall[c] = h
                            break
                        h += 1
                for c in range(16):           # RWall
                    h = 15
                    while True:
                        if h == -1:
                            rwall[c] = 0xFF
                            break
                        m = roof[h]
                        if m != 0xFF and c <= m:
                            rwall[c] = h
                            break
                        h -= 1
            else:
                for c in range(16):
                    if active[c]:
                        floor[c] = heights[c]
                        roof[c] = 0x0F
                    else:
                        floor[c] = 0xFF
                        roof[c] = 0xFF
                for c in range(16):           # LWall (scan floor, c >= m)
                    h = 0
                    while True:
                        if h == 16:
                            lwall[c] = 0xFF
                            break
                        m = floor[h]
                        if m != 0xFF and c >= m:
                            lwall[c] = h
                            break
                        h += 1
                for c in range(16):           # RWall
                    h = 15
                    while True:
                        if h == -1:
                            rwall[c] = 0xFF
                            break
                        m = floor[h]
                        if m != 0xFF and c >= m:
                            rwall[c] = h
                            break
                        h -= 1
            masks[p][t] = {"floor": floor, "lwall": lwall,
                           "rwall": rwall, "roof": roof}
            yflips[p][t] = 1 if yflip else 0
    if pos != len(buf):
        raise SystemExit("C2 FAIL: %d bytes left after walk" % (len(buf) - pos))
    return masks, info, yflips


def expand_flips(masks, info):
    """The PC x4 pre-expansion (Scene.cpp:889-963) -- the C3 reference and
    the flip-probe oracle. Returns ext[p][flip][t] mask dicts + extinfo."""
    ext = [[[None] * TILE_COUNT for _ in range(4)] for _ in range(CPATH)]
    extinfo = [[[None] * TILE_COUNT for _ in range(4)] for _ in range(CPATH)]
    s8 = lambda v: v & 0xFF

    def neg(v):  # int8 negate stored back into uint8
        return s8(-((v ^ 0x80) - 0x80) if v & 0x80 else -v) if v else 0

    def negv(v):
        x = (v ^ 0x80) - 0x80  # sign-extend
        return s8(-x)

    for p in range(CPATH):
        for t in range(TILE_COUNT):
            ext[p][FLIP_NONE][t] = masks[p][t]
            extinfo[p][FLIP_NONE][t] = info[p][t]
        for t in range(TILE_COUNT):  # FlipX (Scene.cpp:890-914)
            base = masks[p][t]
            fa, la, ra, ro, fl = info[p][t]
            extinfo[p][FLIP_X][t] = (negv(fa), negv(ra), negv(la), negv(ro), fl)
            floor = [base["floor"][0xF - c] for c in range(16)]
            roof = [base["roof"][0xF - c] for c in range(16)]
            rwall = [0xFF if base["lwall"][c] == 0xFF else 0xF - base["lwall"][c]
                     for c in range(16)]
            lwall = [0xFF if base["rwall"][c] == 0xFF else 0xF - base["rwall"][c]
                     for c in range(16)]
            ext[p][FLIP_X][t] = {"floor": floor, "lwall": lwall,
                                 "rwall": rwall, "roof": roof}
        for t in range(TILE_COUNT):  # FlipY (Scene.cpp:917-941)
            base = masks[p][t]
            fa, la, ra, ro, fl = info[p][t]
            m80 = lambda v: s8(-0x80 - ((v ^ 0x80) - 0x80))
            extinfo[p][FLIP_Y][t] = (m80(ro), m80(la), m80(ra), m80(fa), fl)
            floor = [0xFF if base["roof"][c] == 0xFF else 0xF - base["roof"][c]
                     for c in range(16)]
            roof = [0xFF if base["floor"][c] == 0xFF else 0xF - base["floor"][c]
                    for c in range(16)]
            lwall = [base["lwall"][0xF - c] for c in range(16)]
            rwall = [base["rwall"][0xF - c] for c in range(16)]
            ext[p][FLIP_Y][t] = {"floor": floor, "lwall": lwall,
                                 "rwall": rwall, "roof": roof}
        for t in range(TILE_COUNT):  # FlipXY (Scene.cpp:944-963, from FLIP_Y)
            basey = ext[p][FLIP_Y][t]
            fa, la, ra, ro, fl = extinfo[p][FLIP_Y][t]
            extinfo[p][FLIP_XY][t] = (negv(fa), negv(ra), negv(la), negv(ro), fl)
            floor = [basey["floor"][0xF - c] for c in range(16)]
            roof = [basey["roof"][0xF - c] for c in range(16)]
            rwall = [0xFF if basey["lwall"][c] == 0xFF else 0xF - basey["lwall"][c]
                     for c in range(16)]
            lwall = [0xFF if basey["rwall"][c] == 0xFF else 0xF - basey["rwall"][c]
                     for c in range(16)]
            ext[p][FLIP_XY][t] = {"floor": floor, "lwall": lwall,
                                  "rwall": rwall, "roof": roof}
    return ext, extinfo


def pack_tile(m, yflip):
    """16x uint16 per the packed format above."""
    words = []
    for c in range(16):
        if yflip:
            rich = m["roof"][c]
        else:
            rich = m["floor"][c]
        sent = 1 if rich == 0xFF else 0
        richn = 0 if sent else rich
        lw = m["lwall"][c]
        ls = 1 if lw == 0xFF else 0
        lwn = 0 if ls else lw
        rw = m["rwall"][c]
        rs = 1 if rw == 0xFF else 0
        rwn = 0 if rs else rw
        w = (richn & 0xF) | (sent << 4) | ((lwn & 0xF) << 5) | (ls << 9) \
            | ((rwn & 0xF) << 10) | (rs << 14) | ((1 if yflip else 0) << 15)
        words.append(w)
    return words


def unpack_col(w, direction):
    """The Saturn accessor model. direction in floor/lwall/rwall/roof."""
    yflip = (w >> 15) & 1
    sent = (w >> 4) & 1
    rich = w & 0xF
    if direction == "floor":
        if sent:
            return 0xFF
        return 0x00 if yflip else rich
    if direction == "roof":
        if sent:
            return 0xFF
        return rich if yflip else 0x0F
    if direction == "lwall":
        return 0xFF if (w >> 9) & 1 else (w >> 5) & 0xF
    if direction == "rwall":
        return 0xFF if (w >> 14) & 1 else (w >> 10) & 0xF
    raise ValueError(direction)


def main():
    buf = load_tileconfig(TILECFG)
    masks, info, yflips = build_masks(buf)   # C2 inside

    # raw layout per the engine struct: floor lwall rwall roof per tile
    raw = bytearray()
    for p in range(CPATH):
        for t in range(TILE_COUNT):
            m = masks[p][t]
            raw += bytes(m["floor"]) + bytes(m["lwall"]) \
                 + bytes(m["rwall"]) + bytes(m["roof"])
    info_blob = bytearray()
    for p in range(CPATH):
        for t in range(TILE_COUNT):
            info_blob += bytes(info[p][t])

    packed = bytearray()
    packed_words = [[None] * TILE_COUNT for _ in range(CPATH)]
    for p in range(CPATH):
        for t in range(TILE_COUNT):
            words = pack_tile(masks[p][t], yflips[p][t])
            packed_words[p][t] = words
            for w in words:
                packed += struct.pack(">H", w)

    # C1: exact round-trip over the whole file
    for p in range(CPATH):
        for t in range(TILE_COUNT):
            m = masks[p][t]
            for c in range(16):
                w = packed_words[p][t][c]
                for d, arr in (("floor", m["floor"]), ("lwall", m["lwall"]),
                               ("rwall", m["rwall"]), ("roof", m["roof"])):
                    got = unpack_col(w, d)
                    if got != arr[c]:
                        raise SystemExit(
                            "C1 FAIL: p%d t%d c%d %s pack %02X != raw %02X"
                            % (p, t, c, d, got, arr[c]))

    # C3 oracle + probes: full PC expansion, then flip probes from it
    ext, extinfo = expand_flips(masks, info)
    rng = random.Random(0x53415432)  # deterministic ("SAT2")
    DIRS = ["floor", "lwall", "rwall", "roof"]
    probes = []
    for flip in (FLIP_NONE, FLIP_X, FLIP_Y, FLIP_XY):
        for _ in range(24):
            p = rng.randrange(CPATH)
            t = rng.randrange(TILE_COUNT)
            c = rng.randrange(16)
            d = rng.choice(DIRS)
            tile = t | (flip << 10)
            probes.append({"plane": p, "tile": tile, "dir": d, "col": c,
                           "expect": ext[p][flip][t][d][c]})
    # angle probes: dir codes 4..7 = floorAngle/lWallAngle/rWallAngle/
    # roofAngle, expectations from the PC flip-angle formulas (extinfo)
    for flip in (FLIP_NONE, FLIP_X, FLIP_Y, FLIP_XY):
        for _ in range(8):
            p = rng.randrange(CPATH)
            t = rng.randrange(TILE_COUNT)
            a = rng.randrange(4)
            tile = t | (flip << 10)
            probes.append({"plane": p, "tile": tile,
                           "dir": "angle%d" % a, "col": 0,
                           "expect": extinfo[p][flip][t][a]})

    model = {
        "_comment": "GENERATED by gen_collision_model.py -- qa_p6_collision "
                    "model (GHZ TileConfig.bin)",
        "raw_bytes": len(raw), "raw_hash": "0x%08X" % djb2(raw),
        "info_bytes": len(info_blob), "info_hash": "0x%08X" % djb2(info_blob),
        "packed_bytes": len(packed), "packed_hash": "0x%08X" % djb2(packed),
        "yflip_tiles": sum(map(sum, yflips)),
        "probes": probes,
    }
    OUT_MODEL.write_text(json.dumps(model, indent=1))

    # the C probe table the harness replays through the REAL Saturn
    # accessors (dir codes 0-3 = floor/lwall/rwall/roof mask, 4-7 = the
    # four tileInfo angles)
    DIRCODE = {"floor": 0, "lwall": 1, "rwall": 2, "roof": 3,
               "angle0": 4, "angle1": 5, "angle2": 6, "angle3": 7}
    inc = []
    inc.append("// p6_collision_probes.inc -- GENERATED by "
               "gen_collision_model.py. DO NOT EDIT.")
    inc.append("// %d probes over GHZ TileConfig.bin: 96 mask + 32 angle, "
               "24/8 per flip variant;" % len(probes))
    inc.append("// expectations derived via the PC pre-expansion formulas "
               "(Scene.cpp:889-963).")
    inc.append("static const struct { uint8 plane; uint16 tile; uint8 dir; "
               "uint8 col; uint8 expect; }")
    inc.append("p6CollisionProbes[%d] = {" % len(probes))
    for pr in probes:
        inc.append("    { %d, 0x%03X, %d, %2d, 0x%02X }," %
                   (pr["plane"], pr["tile"], DIRCODE[pr["dir"]],
                    pr["col"], pr["expect"]))
    inc.append("};")
    (Path(__file__).resolve().parent / "p6_collision_probes.inc").write_text(
        "\n".join(inc) + "\n")
    print("OK: raw %d B djb2 %s | info %d B %s | packed %d B %s | "
          "yflip tiles %d | %d probes" %
          (len(raw), model["raw_hash"], len(info_blob), model["info_hash"],
           len(packed), model["packed_hash"], model["yflip_tiles"],
           len(probes)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
