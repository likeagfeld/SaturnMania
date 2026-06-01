#!/usr/bin/env python3
"""build_collision.py - build a Saturn-friendly per-column surface table from
RSDK's authoritative TileConfig.bin (Mania's collision data).

Format (verified from Sonic Mania Decompilation, RSDKv5/RSDK/Scene/Scene.cpp
`LoadTileConfig`; reading the format only, not copying code):
    4 bytes  : 'TIL\\0' signature
    ReadCompressed payload = [u32 LE total][u32 BE usize][zlib total-4 bytes]
    Then, for each of 2 collision paths (path 0 = primary; path 1 = alt route):
      For each of 1024 tiles (TILE_COUNT):
        16 B  maskHeights[16]  per-column tile-local height (0..15) or 0xFF
        16 B  maskActive[16]   per-column solidity gate (0 = non-solid)
         1 B  yFlip            0 = surface lives at TOP edge of tile (floor);
                               1 = surface lives at BOTTOM edge (ceiling)
         1 B  floorAngle       Q0.8 slope angle (256 = full circle)
         1 B  lWallAngle
         1 B  rWallAngle
         1 B  roofAngle
         1 B  flag             collision-type flags (spikes, breakable, etc.)
      = 38 B per tile (16 + 16 + 1 + 4 + 1) * 1024 = 38912 B per path
      * 2 paths = 77824 B total decompressed

We only need path 0 floor data on Saturn: per-column world-Y of the topmost
solid surface across the level, plus the angle + flag at that surface, so
Sonic can ground-follow / slope-walk / detect spikes / fall into pits.

Output GHZSURF.BIN: big-endian
    u16  surfY    (absolute world pixel Y, 0xFFFF = no floor -> pit)
    u8   angle    (Mania Q0.8 floor angle; 0 = level ground, 64 = +90 wall ...)
    u8   flag     (TileConfig flag byte; bit0 commonly = "interactive" / hazard)
    = 4 B per column * level_width_in_pixels = 4 B * 16384 = 65536 B (=64 KB)
This sits resident in Work RAM; Saturn collision indexes by Sonic's x_px.

Usage:
    python tools/build_collision.py extracted/Data/Stages/GHZ \\
        --scene Scene1.bin --layers 3,4 --out cd/GHZSURF.BIN
"""
import argparse, os, struct, sys
import numpy as np
sys.path.insert(0, os.path.dirname(__file__))
from render_scene import R, parse_scene


SIG_TIL = b"TIL\x00"
TILE_COUNT = 1024
TILE_SIZE = 16
CPATH_COUNT = 2


def load_tileconfig(path):
    """Return (floorHeights[1024,16] int8, floorAngles[1024] uint8,
                flags[1024] uint8) for collision path 0.
       floorHeights[t,c] = pixel offset within the tile where the solid floor
       surface sits at column c (0..15), or -1 if non-solid.
       Tiles with yFlip=1 in the file are CEILING tiles; for ground follow we
       record them as non-solid floors (player can't stand on a ceiling). They
       are still solid for roof-collision purposes, which we'll add later if
       needed."""
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:4] != SIG_TIL:
        raise SystemExit(f"{path}: missing TIL signature, got {raw[:4]!r}")
    r = R(raw); r.p = 4
    payload = r.compressed()

    expect = CPATH_COUNT * TILE_COUNT * 38
    if len(payload) != expect:
        raise SystemExit(f"{path}: decompressed {len(payload)} bytes, expected {expect}")

    fh = np.full((TILE_COUNT, TILE_SIZE), -1, dtype=np.int8)
    fa = np.zeros(TILE_COUNT, dtype=np.uint8)
    fg = np.zeros(TILE_COUNT, dtype=np.uint8)

    pos = 0
    # path 0 only (primary collision)
    for t in range(TILE_COUNT):
        mh = payload[pos:pos + 16]; pos += 16
        ma = payload[pos:pos + 16]; pos += 16
        yflip = payload[pos]; pos += 1
        fa[t] = payload[pos]; pos += 1         # floorAngle
        pos += 3                               # lWall/rWall/roofAngle (unused here)
        fg[t] = payload[pos]; pos += 1         # flag

        if yflip:
            # ceiling tile -> no floor surface for ground-follow
            continue
        for c in range(TILE_SIZE):
            if ma[c]:
                # solid column: surface is at maskHeights[c] from the TOP of
                # the tile (TileConfig.bin convention for floor tiles).
                fh[t, c] = mh[c]
    # path 1 skipped (alt route data; loops/special-paths)
    return fh, fa, fg


def build_surface(stage_dir, scene_name, layer_idxs, out_path):
    fh, fa, fg = load_tileconfig(os.path.join(stage_dir, "TileConfig.bin"))
    layers = parse_scene(os.path.join(stage_dir, scene_name))

    # Composite playfield layout: take topmost (lowest-index) non-empty entry
    # from the listed layers at each (tx, ty), as the renderer does. Mania FG
    # = layer 3 (FG Low) over layer 4 (FG High) with same parallax; both
    # collide.
    ref = layers[layer_idxs[0]]["layout"]
    ys, xs = ref.shape
    layout = np.full((ys, xs), 0xFFFF, dtype=">u2")
    for li in layer_idxs:
        a = layers[li]["layout"]
        m = (layout == 0xFFFF) & (a != 0xFFFF)
        layout[m] = a[m]

    level_w_px = xs * TILE_SIZE
    level_h_px = ys * TILE_SIZE
    surf_y = np.full(level_w_px, 0xFFFF, dtype=">u2")
    surf_a = np.zeros(level_w_px, dtype=np.uint8)
    surf_f = np.zeros(level_w_px, dtype=np.uint8)

    # For each world column x, find the topmost solid tile that is BACKED by
    # a continuous solid mass below it. RSDK's TileConfig marks decoration
    # tiles (sun totems, hanging signs, etc.) as solid -- they CAN be hit by
    # the player -- but the real walkable playfield is the continuous
    # solid mass (grass on top of many tiles of dirt). A naive top-down
    # "first-solid" picks up the decoration; Sonic would float above the
    # actual grass.  Heuristic: require >= MIN_SOLID_RUN tiles of continuous
    # column-solidity below the candidate surface tile.
    NO_FLOOR = 0xFFFF
    MIN_SOLID_RUN = 4                          # 4 tiles = 64 px of solid mass

    def col_solid(entry, cx_world):
        """Per-column tile-solidity (respecting H/V flip in the layout)."""
        if entry == 0xFFFF:
            return False
        tile_id = entry & 0x3FF
        cx = (TILE_SIZE - 1 - cx_world) if (entry & 0x400) else cx_world
        return fh[tile_id, cx] >= 0

    for x in range(level_w_px):
        tx = x >> 4
        cx_world = x & 0x0F
        ty = 0
        while ty < ys:
            entry = int(layout[ty, tx])
            if not col_solid(entry, cx_world):
                ty += 1; continue
            # Candidate: count consecutive solid tile-rows starting here.
            run = 0; ty2 = ty
            while ty2 < ys and col_solid(int(layout[ty2, tx]), cx_world):
                run += 1; ty2 += 1
            if run >= MIN_SOLID_RUN:
                tile_id = entry & 0x3FF
                flip_h  = bool(entry & 0x400)
                flip_v  = bool(entry & 0x800)
                cx = (TILE_SIZE - 1 - cx_world) if flip_h else cx_world
                h  = fh[tile_id, cx]
                local_y = (TILE_SIZE - 1 - h) if flip_v else h
                surf_y[x] = ty * TILE_SIZE + local_y
                surf_a[x] = fa[tile_id]
                surf_f[x] = fg[tile_id]
                break
            ty = ty2                           # skip past this short decoration run

    # Write compact binary (big-endian: SH-2 native).
    with open(out_path, "wb") as f:
        for x in range(level_w_px):
            f.write(struct.pack(">HBB", int(surf_y[x]), int(surf_a[x]), int(surf_f[x])))

    solid = int((surf_y != NO_FLOOR).sum())
    print(f"{out_path}: {level_w_px} columns ({level_w_px*4} B); "
          f"{solid}/{level_w_px} have floor ({100*solid/level_w_px:.1f}%)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stage_dir")
    ap.add_argument("--scene", default="Scene1.bin")
    ap.add_argument("--layers", default="3,4",
                    help="comma-separated layer indices to merge (FG Low + High)")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()
    build_surface(args.stage_dir, args.scene,
                  [int(s) for s in args.layers.split(",")], args.out)


if __name__ == "__main__":
    main()
