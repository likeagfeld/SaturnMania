#!/usr/bin/env python3
"""
convert_vdp2_tilemap.py - Convert an RSDK layer to a Saturn VDP2 cell tilemap:
a COMPACT tileset (only the tiles the layer actually uses, remapped 0..N-1) plus
a 1-word pattern-name map. Designed for raw-SGL CHAR_SIZE_2x2 / CN_10BIT NBG.

Why compact: GHZ FG Low uses only ~316 of 1024 tiles (~79 KB at 256 B/tile), so
the whole layer's tileset fits ONE 128 KB VRAM bank and stays resident; only the
pattern-name window streams.

Outputs (big-endian; SH-2 native):
    <out>.CEL  N tiles * 256 B, 2x2-cell order (TL,TR,BL,BR). Tile 0 = all-zero
               (transparent) blank. N <= 1024 (CN_10BIT).
    <out>.PAL  256 * u16 RGB555 (index 0 = 0/transparent)
    <out>.MAP  [u16 wTiles][u16 hTiles] then w*h u16 pattern names:
               pn = compact_idx | (rsdk_entry & 0xC00)   # bit10 Hflip, bit11 Vflip
               blank (0xFFFF) -> 0. Palette bits (12-15) and cell-base offset are
               added by the Saturn loader.

Usage:
    python convert_vdp2_tilemap.py extracted/Data/Stages/GHZ --scene Scene1.bin \
      --layer 3 --bank 1 --out ../cd/GHZFG
"""
import argparse
import os
import struct

import numpy as np

from render_scene import load_tiles, read_stage_palette, parse_scene, build_palette
from convert_vdp2 import to_rgb555, tile_to_cells


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stage_dir")
    ap.add_argument("--scene", default="Scene1.bin")
    ap.add_argument("--layer", type=int, default=3)
    ap.add_argument("--bank", type=int, default=0)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    gif = os.path.join(args.stage_dir, "16x16Tiles.gif")
    tiles = load_tiles(gif)
    banks, masks = read_stage_palette(os.path.join(args.stage_dir, "StageConfig.bin"))
    pal_rgb = build_palette(gif, banks, masks, args.bank)
    layer = parse_scene(os.path.join(args.stage_dir, args.scene))[args.layer]
    lay = layer["layout"]
    ys, xs = lay.shape

    # Unique tile indices used (excluding blank).
    nb = lay[lay != 0xFFFF]
    used = sorted(set(int(v) & 0x3FF for v in nb))
    # compact index 0 reserved for the transparent blank tile.
    remap = {old: i + 1 for i, old in enumerate(used)}
    ncompact = len(used) + 1
    assert ncompact <= 1024, f"{ncompact} tiles exceeds CN_10BIT (1024)"

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)

    # CEL: blank tile (all zero) then each used tile in 2x2-cell order.
    with open(args.out + ".CEL", "wb") as f:
        f.write(bytes(256))                       # compact tile 0 = transparent
        for old in used:
            f.write(tile_to_cells(tiles[old]))

    # PAL -- pre-shifted UP by 1 (jo CRAM off-by-one; see tools/shift_pal.py).
    pal555 = [to_rgb555(pal_rgb[i + 1]) if i < 255 else 0 for i in range(256)]
    with open(args.out + ".PAL", "wb") as f:
        f.write(struct.pack(">256H", *pal555))

    # MAP: pattern names (compact idx + flip bits), big-endian.
    pn = np.zeros((ys, xs), dtype=">u2")
    for y in range(ys):
        for x in range(xs):
            e = int(lay[y, x])
            if e >= 0xFFFF:
                continue
            pn[y, x] = remap[e & 0x3FF] | (e & 0xC00)   # flip bits pass through
    with open(args.out + ".MAP", "wb") as f:
        f.write(struct.pack(">HH", xs, ys))
        f.write(pn.tobytes())

    print(f"{args.out}: {ncompact} compact tiles ({ncompact*256} B CEL), "
          f"map {xs}x{ys} ({xs*ys*2} B)")


if __name__ == "__main__":
    main()
