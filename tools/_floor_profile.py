#!/usr/bin/env python3
# _floor_profile.py -- offline collision floor profile for GHZ1 (signpost
# campaign diagnostics). Parses TileConfig.bin per decomp Scene.cpp
# LoadTileConfig (zlib buffer: per plane 2 x per tile 0x400: 16 heights +
# 16 active + yFlip,floorAngle,lWall,rWall,roof,flag) + the FG layouts from
# Scene1.bin (render_scene R walk), and prints the topmost solid-floor y for
# each x column in a range. Usage:
#   python tools/_floor_profile.py [x0 x1 [ytop ybot]] [--layer both|low|high]
import importlib.util, struct, sys, zlib
from pathlib import Path

spec = importlib.util.spec_from_file_location("rs", "tools/render_scene.py")
rs = importlib.util.module_from_spec(spec); spec.loader.exec_module(rs)
R = rs.R

STAGE = Path("extracted/Data/Stages/GHZ")

# ---- TileConfig ----
d = open(STAGE / "TileConfig.bin", "rb").read()
assert d[:4] == b"TIL\0"
# ReadCompressed: u32 compressed size (incl 4-byte uncompressed size), u32 be uncompressed
csz = struct.unpack_from("<I", d, 4)[0]
buf = zlib.decompress(d[12:12 + csz - 4])
TILE = 16
floor = [[None] * 0x400 for _ in range(2)]   # [plane][tile] = 16 floor heights (None=empty col -> 0xFF)
flags = [[0] * 0x400 for _ in range(2)]
pos = 0
for p in range(2):
    masks = []
    for t in range(0x400):
        mh = buf[pos:pos + 16]; pos += 16
        ma = buf[pos:pos + 16]; pos += 16
        yflip, fa, lw, rw, ra, fl = buf[pos:pos + 6]; pos += 6
        fm = [0xFF] * 16
        for c in range(16):
            if ma[c]:
                fm[c] = 0 if yflip else mh[c]
        floor[p][t] = fm
        flags[p][t] = fl

# ---- scene layouts ----
d = open(STAGE / "Scene1.bin", "rb").read()
r = R(d); r.p = 4; r.skip(0x10)
sl = r.u8(); r.skip(sl + 1)
layer_count = r.u8()
layers = {}
for _ in range(layer_count):
    r.u8()
    name = r.s()
    r.u8(); r.u8()
    xs = r.u16(); ys = r.u16(); r.u16(); r.u16()
    sic = r.u16()
    for _ in range(sic):
        r.u16(); r.u16(); r.u8(); r.u8()
    r.compressed()
    lay = r.compressed()
    if isinstance(name, bytes):
        name = name.decode(errors="replace")
    layers[name.strip("\x00 ")] = (xs, ys, lay)

def col_floor_y(name, wx, ytop, ybot, plane):
    """topmost solid floor pixel y in [ytop,ybot] for world column wx."""
    xs, ys, lay = layers[name]
    tx = wx // 16
    cx = wx % 16
    if tx >= xs:
        return None
    for wy in range(ytop, ybot):
        ty = wy // 16
        cy = wy % 16
        if ty >= ys:
            return None
        w = struct.unpack_from("<H", lay, 2 * (ty * xs + tx))[0]
        ti = w & 0x3FF
        if w == 0xFFFF:
            continue
        # solidity: bits 12-13 plane A, 14-15 plane B (RSDK v5 tile word)
        sol = (w >> (12 + 2 * plane)) & 3
        if sol == 0:
            continue
        fx = 15 - cx if (w >> 10) & 1 else cx
        fm = floor[plane][ti]
        h = fm[fx]
        if h == 0xFF:
            continue
        fy = 15 - h if (w >> 11) & 1 else h
        if cy >= fy:
            return ty * 16 + fy
    return None

if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    x0 = int(args[0]) if args else 900
    x1 = int(args[1]) if len(args) > 1 else 1400
    yt = int(args[2]) if len(args) > 2 else 800
    yb = int(args[3]) if len(args) > 3 else 1200
    print("   x | FGLow.A FGLow.B | FGHigh.A FGHigh.B")
    for wx in range(x0, x1, 8):
        v = [col_floor_y("FG Low", wx, yt, yb, 0), col_floor_y("FG Low", wx, yt, yb, 1),
             col_floor_y("FG High", wx, yt, yb, 0), col_floor_y("FG High", wx, yt, yb, 1)]
        print("%5d | %7s %7s | %8s %8s" % (wx, *[str(x) for x in v]))
