#!/usr/bin/env python3
"""
convert_vdp2_cells8.py - Convert a 32x32-tile region of an RSDK layer into a
Saturn VDP2 NBG cell page using the SAME proven config Jo Engine uses
(CHAR_SIZE_1x1, COL_TYPE_256, PNB_1WORD | CN_12BIT). Tile flips are BAKED into
the 8x8 cell pixels and cells are de-duplicated, so no pattern-name flip bits are
needed and the encoding is identical to Jo's __jo_create_map:
    PN = (cellIndex*2 | (paletteId<<12)) + cellBaseOffset    (base added on Saturn)

A 32x32-tile region = 64x64 cells = exactly one PL_SIZE_1x1 page (512x512 px).
GHZ start uses ~195 unique cells (idx*2 < 1024), so it fits comfortably.

Outputs (big-endian; SH-2 native):
    <out>.CEL   N cells * 64 bytes (8bpp). Cell 0 = transparent blank.
    <out>.CMAP  64*64 u16 cell indices in page row-major order (row*64+col).
    <out>.PAL   256 * u16 RGB555 (index 0 = 0/transparent)

Usage:
    python convert_vdp2_cells8.py extracted/Data/Stages/GHZ --scene Scene1.bin \
      --layer 3 --bank 1 --x 0 --y 48 --out ../cd/GHZP
"""
import argparse
import os
import struct

import numpy as np

from render_scene import load_tiles, read_stage_palette, parse_scene, build_palette
from convert_vdp2 import to_rgb555

REGION_TILES = 32          # 32x32 tiles -> 64x64 cells -> one PL_SIZE_1x1 page
PAGE_CELLS = 64


def tile_cells(tiles, e):
    """Return the 4 flip-baked 8x8 cells (TL,TR,BL,BR) of a tile entry."""
    idx = e & 0x3FF
    t = tiles[idx].copy()
    if e & 0x400:
        t = t[:, ::-1]
    if e & 0x800:
        t = t[::-1, :]
    return [t[0:8, 0:8], t[0:8, 8:16], t[8:16, 0:8], t[8:16, 8:16]]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stage_dir")
    ap.add_argument("--scene", default="Scene1.bin")
    ap.add_argument("--layer", type=int, default=3)
    ap.add_argument("--bank", type=int, default=0)
    ap.add_argument("--x", type=int, default=0)
    ap.add_argument("--y", type=int, default=48)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    gif = os.path.join(args.stage_dir, "16x16Tiles.gif")
    tiles = load_tiles(gif)
    banks, masks = read_stage_palette(os.path.join(args.stage_dir, "StageConfig.bin"))
    pal_rgb = build_palette(gif, banks, masks, args.bank)
    lay = parse_scene(os.path.join(args.stage_dir, args.scene))[args.layer]["layout"]
    ys, xs = lay.shape

    blank = bytes(64)
    cell_index = {blank: 0}
    cell_data = [blank]
    cmap = np.zeros((PAGE_CELLS, PAGE_CELLS), dtype=">u2")   # cell idx per page cell

    for ty in range(REGION_TILES):
        for tx in range(REGION_TILES):
            wy, wx = args.y + ty, args.x + tx
            if wy >= ys or wx >= xs:
                continue
            e = int(lay[wy, wx])
            if e >= 0xFFFF:
                continue
            sub = tile_cells(tiles, e)                       # TL,TR,BL,BR
            for si, (oy, ox) in enumerate(((0, 0), (0, 1), (1, 0), (1, 1))):
                # VDP2 cell byte order matches Jo's loader (column-major write);
                # transpose the row-major 8x8 to match.
                key = np.ascontiguousarray(sub[si].T, np.uint8).tobytes()
                ci = cell_index.get(key)
                if ci is None:
                    ci = len(cell_data)
                    cell_index[key] = ci
                    cell_data.append(key)
                cmap[ty * 2 + oy, tx * 2 + ox] = ci

    assert len(cell_data) * 2 <= 4096, f"{len(cell_data)} cells overflow CN_12BIT"

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out + ".CEL", "wb") as f:
        for c in cell_data:
            f.write(c)
    with open(args.out + ".CMAP", "wb") as f:
        f.write(cmap.tobytes())
    # pre-shifted UP by 1 -- see tools/shift_pal.py for the jo CRAM off-by-one.
    pal555 = [to_rgb555(pal_rgb[i + 1]) if i < 255 else 0 for i in range(256)]
    with open(args.out + ".PAL", "wb") as f:
        f.write(struct.pack(">256H", *pal555))

    print(f"{args.out}: {len(cell_data)} cells ({len(cell_data)*64} B), "
          f"page {PAGE_CELLS}x{PAGE_CELLS}, idx*2 max {2*(len(cell_data)-1)}")


if __name__ == "__main__":
    main()
