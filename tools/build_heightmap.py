#!/usr/bin/env python3
"""
build_heightmap.py - Build a per-pixel-column ground-surface height map for the
displayed GHZ region from TileConfig.bin collision, so Sonic can follow the
terrain (dip/rise with the grass) instead of a flat floor.

TileConfig.bin (RSDKv5, verified vs Scene.cpp::LoadTileConfig): "TIL" sig, then
ReadCompressed (zlib) of CPATH_COUNT(2) planes * TILE_COUNT(1024) tiles, each:
  [16 maskHeights][16 maskActive][u8 yFlip][u8 floor/lWall/rWall/roof angle][u8 flag]
maskActive[c]=solid in column c; maskHeights[c]=floor surface row within the tile.

Scans the composited playfield layers (FG Low + FG High) top-down per column to
find the topmost solid floor, giving the surface row in the region.

Output: <out> = [u16 width] then width * s16 region-relative surface row
(0..regionH); 0x7FFF = no ground in that column.

Usage:
    python build_heightmap.py extracted/Data/Stages/GHZ --scene Scene1.bin \
      --layers 3,4 --x 0 --y 48 --w-tiles 32 --h-tiles 15 --out ../cd/GHZHGT.bin
"""
import argparse
import os
import struct
import zlib

import numpy as np

from render_scene import parse_scene

TILE_COUNT = 1024
TILE_SIZE = 16
REC = 16 + 16 + 1 + 4 + 1            # 38 bytes per tile per plane


def load_tileconfig(path):
    d = open(path, "rb").read()
    assert d[:4] == b"TIL\x00", "not a TileConfig"
    pos = 4
    total = struct.unpack_from("<I", d, pos)[0]; pos += 4
    struct.unpack_from(">I", d, pos); pos += 4   # uncompressed size (BE), unused
    raw = zlib.decompress(d[pos:pos + total - 4])
    # plane 0 only (path A)
    heights = np.zeros((TILE_COUNT, TILE_SIZE), np.uint8)
    active = np.zeros((TILE_COUNT, TILE_SIZE), np.uint8)
    bp = 0
    for t in range(TILE_COUNT):
        heights[t] = np.frombuffer(raw[bp:bp + TILE_SIZE], np.uint8); bp += TILE_SIZE
        active[t] = np.frombuffer(raw[bp:bp + TILE_SIZE], np.uint8); bp += TILE_SIZE
        bp += 6                                  # yflip + 4 angles + flag
    return heights, active


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stage_dir")
    ap.add_argument("--scene", default="Scene1.bin")
    ap.add_argument("--layers", default="3,4")
    ap.add_argument("--x", type=int, default=0)
    ap.add_argument("--y", type=int, default=48)
    ap.add_argument("--w-tiles", type=int, default=32)
    ap.add_argument("--h-tiles", type=int, default=15)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    heights, active = load_tileconfig(os.path.join(args.stage_dir, "TileConfig.bin"))
    layers = parse_scene(os.path.join(args.stage_dir, args.scene))
    lis = [int(x) for x in args.layers.split(",")]

    W = args.w_tiles * TILE_SIZE
    H = args.h_tiles * TILE_SIZE
    surf = np.full(W, 0x7FFF, np.int16)

    for px in range(W):
        tx = args.x + px // TILE_SIZE
        c = px % TILE_SIZE
        found = 0x7FFF
        for ty in range(args.h_tiles):                  # top-down within region
            wy = args.y + ty
            # composite: topmost layer that has a solid tile here wins
            for li in lis:
                lay = layers[li]["layout"]
                if wy >= lay.shape[0] or tx >= lay.shape[1]:
                    continue
                e = int(lay[wy, tx])
                if e >= 0xFFFF:
                    continue
                # Floor only if the layout marks path-A solidity top-solid
                # (solidA odd: 1=top, 3=all). Excludes decorations (solidA 0)
                # and jump-through-from-above (solidA 2).
                if not (((e >> 12) & 3) & 1):
                    continue
                idx = e & 0x3FF
                if active[idx, c]:                       # solid in this column
                    # absolute world surface row (so a full-level map drives the
                    # camera regardless of the scan's --y start).
                    found = (args.y + ty) * TILE_SIZE + int(heights[idx, c])
                    break
            if found != 0x7FFF:
                break
        surf[px] = found

    # Smooth: ground tiles are flat (maskHeights 0), so the raw surface steps
    # 16px at tile boundaries and has isolated single-tile outliers -> jittery.
    # (1) fill empties from neighbours, (2) median-filter to kill outlier tiles,
    # (3) box-average to ramp the tile steps into smooth slopes.
    s = surf.astype(np.float64)
    valid = s != 0x7FFF
    if valid.any():
        s[~valid] = np.interp(np.flatnonzero(~valid), np.flatnonzero(valid), s[valid])
    med = s.copy()
    R = 28
    for i in range(W):
        med[i] = np.median(s[max(0, i - R):min(W, i + R + 1)])
    k = np.ones(17) / 17.0
    sm = np.convolve(np.pad(med, 8, mode="edge"), k, mode="valid")
    surf = np.rint(sm).astype(np.int16)

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(struct.pack(">H", W))
        f.write(surf.astype(">i2").tobytes())

    valid = surf[surf != 0x7FFF]
    print(f"{args.out}: {W} columns, surface rows min={valid.min()} "
          f"max={valid.max()} (region 0..{H}); {int((surf==0x7FFF).sum())} empty cols")

    # SMOOTHNESS GATE: a single screenshot can't catch motion jitter, so validate
    # it in the data. Abrupt per-column steps make the player visibly snap up/down.
    MAX_JUMP = 2
    jumps = np.abs(np.diff(surf.astype(int)))
    bad = int((jumps > MAX_JUMP).sum())
    print(f"  smoothness: max adjacent jump {int(jumps.max()) if len(jumps) else 0}px"
          f", {bad} steps > {MAX_JUMP}px")
    if bad:
        print(f"  WARNING: heightmap not smooth ({bad} abrupt steps) -> player will "
              f"jitter. Increase smoothing window.")

    # debug overlay on the reference render to verify the line traces the grass
    try:
        from PIL import Image
        ref = Image.open("ghz_render/ghz_ref.png").convert("RGB")
        a = np.asarray(ref).copy()
        for px in range(min(W, a.shape[1])):
            s = surf[px]
            if s != 0x7FFF and s < a.shape[0]:
                a[max(0, s - 1):s + 1, px] = (255, 0, 0)
        Image.fromarray(a).save("ghz_render/_heightmap_overlay.png")
        print("  wrote ghz_render/_heightmap_overlay.png (red = detected surface)")
    except Exception as ex:
        print("  (overlay skipped:", ex, ")")


if __name__ == "__main__":
    main()
