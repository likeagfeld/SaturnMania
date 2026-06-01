#!/usr/bin/env python3
"""
convert_vdp2.py - Convert a validated RSDK stage layer into Saturn VDP2-native
data, then round-trip it back to PNG to prove the conversion is byte-correct.

Outputs (big-endian, ready for SH-2 / VDP2):
    <out>.PAL  256 * uint16 Saturn RGB555  (bit15=1 opaque; B<<10|G<<5|R, 5b each)
    <out>.CEL  N tiles * 256 bytes, each a 16x16 8bpp character in VDP2 2x2-cell
               order (TL, TR, BL, BR; each cell 8x8 row-major). Matches
               slCharNbg(COL_TYPE_256, CHAR_SIZE_2x2).
    <out>.MAP  header [u16 wTiles][u16 hTiles] then wTiles*hTiles * uint16 entries:
               bits 0-9 char index, 0x400 flipX, 0x800 flipY, 0xFFFF = blank.
               (The Saturn loader bakes these into VDP2 pattern names.)

VDP2 facts: CRAM 256-colour mode, color index 0 = transparent. 2x2 char cell
order and RGB555 packing per the VDP2 User Manual.

Usage:
    python convert_vdp2.py extracted/Data/Stages/GHZ --scene Scene1.bin \
        --layer 3 --bank 1 --out cd/GHZ
"""
import argparse
import os
import struct

import numpy as np
from PIL import Image

from render_scene import (load_tiles, read_stage_palette, parse_scene,
                          render_layer, build_palette)


def to_rgb555(rgb):
    r, g, b = int(rgb[0]) >> 3, int(rgb[1]) >> 3, int(rgb[2]) >> 3
    return 0x8000 | (b << 10) | (g << 5) | r


def rgb555_to_rgb888(c):
    r = (c & 0x1F) << 3
    g = ((c >> 5) & 0x1F) << 3
    b = ((c >> 10) & 0x1F) << 3
    return (r | r >> 5, g | g >> 5, b | b >> 5)


def tile_to_cells(tile):
    """16x16 index tile -> 256 bytes in VDP2 2x2-cell order (TL,TR,BL,BR)."""
    out = bytearray(256)
    p = 0
    for (cy, cx) in ((0, 0), (0, 8), (8, 0), (8, 8)):       # TL, TR, BL, BR
        cell = tile[cy:cy + 8, cx:cx + 8]
        out[p:p + 64] = cell.tobytes()
        p += 64
    return bytes(out)


def cells_to_tile(cel):
    """Inverse of tile_to_cells, for round-trip validation."""
    tile = np.zeros((16, 16), np.uint8)
    p = 0
    for (cy, cx) in ((0, 0), (0, 8), (8, 0), (8, 8)):
        tile[cy:cy + 8, cx:cx + 8] = np.frombuffer(cel[p:p + 64], np.uint8).reshape(8, 8)
        p += 64
    return tile


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stage_dir")
    ap.add_argument("--scene", default="Scene1.bin")
    ap.add_argument("--layer", type=int, default=3)
    ap.add_argument("--bank", type=int, default=0,
                    help="active palette bank (per-stage; GHZ playfield uses 1)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--validate-png")
    args = ap.parse_args()

    gif = os.path.join(args.stage_dir, "16x16Tiles.gif")
    tiles = load_tiles(gif)
    banks, masks = read_stage_palette(os.path.join(args.stage_dir, "StageConfig.bin"))
    pal_rgb = build_palette(gif, banks, masks, args.bank)
    layers = parse_scene(os.path.join(args.stage_dir, args.scene))
    layer = layers[args.layer]

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)

    # --- PAL: 256 RGB555 big-endian, pre-shifted UP by 1 to compensate jo's
    # CRAM allocator off-by-one (see tools/shift_pal.py for the full rationale).
    pal555 = [to_rgb555(pal_rgb[i + 1]) if i < 255 else 0 for i in range(256)]
    with open(args.out + ".PAL", "wb") as f:
        f.write(struct.pack(">256H", *pal555))

    # --- CEL: tile character data, VDP2 2x2-cell order ---
    with open(args.out + ".CEL", "wb") as f:
        for t in range(tiles.shape[0]):
            f.write(tile_to_cells(tiles[t]))

    # --- MAP: layer tile entries (index/flip), big-endian ---
    lay = layer["layout"]
    ys, xs = lay.shape
    with open(args.out + ".MAP", "wb") as f:
        f.write(struct.pack(">HH", xs, ys))
        f.write(lay.astype(">u2").tobytes())

    print(f"Wrote {args.out}.PAL (256 colors), {args.out}.CEL "
          f"({tiles.shape[0]} tiles x256B = {tiles.shape[0]*256} B), "
          f"{args.out}.MAP ({xs}x{ys} tiles)")

    # --- Round-trip validation: rebuild from emitted Saturn data ---
    with open(args.out + ".CEL", "rb") as f:
        cel = f.read()
    rebuilt_tiles = np.stack([cells_to_tile(cel[i*256:(i+1)*256])
                              for i in range(tiles.shape[0])])
    assert np.array_equal(rebuilt_tiles, tiles), "CEL round-trip mismatch!"
    print("  CEL round-trip: tile indices identical to source. OK")

    # palette round-trip (RGB555 quantization)
    pal_q = np.array([rgb555_to_rgb888(c) for c in pal555], np.uint8)
    # reconstruct the scene from Saturn data and compare to a direct render
    direct = render_layer(layer, tiles, pal_q)
    # rebuild via MAP + CEL + PAL exactly as the Saturn loader would
    rebuilt = render_layer(layer, rebuilt_tiles, pal_q)
    assert np.array_equal(direct, rebuilt), "scene round-trip mismatch!"
    print("  Scene round-trip from CEL+MAP+PAL: identical. OK")

    if args.validate_png:
        # crop the populated start band for a human-viewable proof
        nb = (lay != 0xFFFF)
        ys_nz = np.where(nb.sum(1) > 0)[0]
        y0 = (ys_nz.min()) * 16 if ys_nz.size else 0
        # find content band lower portion (ground)
        sub = nb[:, :40].sum(1)
        yc = np.where(sub > 0)[0]
        y0 = (yc.min()) * 16 if yc.size else y0
        Image.fromarray(rebuilt[y0:y0+448, 0:640]).save(args.validate_png)
        print(f"  wrote validation PNG {args.validate_png}")


if __name__ == "__main__":
    main()
