#!/usr/bin/env python3
"""
convert_stream.py - Convert the full GHZ foreground into a STREAMING VDP2 tilemap:
the whole level's unique 8x8 cells (fit one VRAM bank, kept resident) + a small
tile-pattern table + a per-tile map. The Saturn renderer keeps the cells resident
and streams only pattern-name columns into a wrapping plane as the camera scrolls.

Cells are written in the documented VDP2 256-colour cell format (raster /
row-major: byte[row*8+col] = pixel(row,col)) so they can be copied verbatim into
VRAM and rendered by a raw-SGL NBG cell-scroll plane (SGL.PDF ch.8 sample_8_8_1).

Outputs (big-endian):
    <out>.CEL   numCells * 64 B, 8bpp row-major cells (cell 0 = transparent)
    <out>.PAT   [u16 numPat] then numPat * 4 u16 cell indices (TL,TR,BL,BR)
    <out>.TMP   [u16 wTiles][u16 hTiles] then w*h u16 pattern ids (0 = blank)
    <out>.PAL   256 u16 RGB555

Usage:
    python convert_stream.py extracted/Data/Stages/GHZ --scene Scene1.bin \
      --layers 3,4 --bank 1 --out ../cd/GHZFG

NOTE: --layers indices are PER SCENE (position in that scene's layer table).
GHZ Scene1.bin has FG Low/High at 3,4 but Scene2.bin has six layers with
FG Low/High at 4,5. The tool refuses mismatched layer shapes (the telltale
of selecting a BG layer) and prints the scene's layer table to pick from.
"""
import argparse
import os
import struct
import sys

import numpy as np

from render_scene import load_tiles, read_stage_palette, build_palette, parse_scene
from convert_vdp2 import to_rgb555


def jo_cell_bytes(cell8):
    """8x8 index block -> 64 bytes in the VDP2 256-colour cell format.

    Per the Sega VDP2 User's Manual, a 256-colour 8x8 character pattern is 64
    consecutive bytes in raster (row-major) order: byte[row*8 + col] = palette
    index of pixel (row, col). This is the same format the SGL DTS examples copy
    verbatim with Cel2VRAM. (Jo's jo_img_to_vdp2_cells uses a different internal
    order paired with a 90deg-rotated source and a transposing map builder; the
    raw-SGL streaming path here uses neither, so it must emit the documented
    hardware format directly.)
    """
    out = bytearray(64)
    for r in range(8):
        for c in range(8):
            out[r * 8 + c] = int(cell8[r, c])
    return bytes(out)


def jo_cell_decode(b):
    """Inverse of jo_cell_bytes (for round-trip validation)."""
    t = np.zeros((8, 8), np.uint8)
    for r in range(8):
        for c in range(8):
            t[r, c] = b[r * 8 + c]
    return t


def tile_subcells(tiles, e):
    idx = e & 0x3FF
    t = tiles[idx]
    if e & 0x400:
        t = t[:, ::-1]
    if e & 0x800:
        t = t[::-1, :]
    return (t[0:8, 0:8], t[0:8, 8:16], t[8:16, 0:8], t[8:16, 8:16])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stage_dir")
    ap.add_argument("--scene", default="Scene1.bin")
    ap.add_argument("--layers", default="3,4")
    ap.add_argument("--bank", type=int, default=1)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    gif = os.path.join(args.stage_dir, "16x16Tiles.gif")
    tiles = load_tiles(gif)
    pal_rgb = build_palette(gif, *read_stage_palette(
        os.path.join(args.stage_dir, "StageConfig.bin"))[:2], args.bank)
    layers = parse_scene(os.path.join(args.stage_dir, args.scene))
    lis = [int(x) for x in args.layers.split(",")]
    ref = layers[lis[0]]["layout"]
    ys, xs = ref.shape

    # Layer indices are per scene (GHZ Scene1: FG at 3,4; Scene2: FG at 4,5).
    # Selecting a BG layer by mistake shows up as a layout-shape mismatch;
    # refuse it up front instead of writing garbage streaming data.
    shapes = {li: layers[li]["layout"].shape for li in lis}
    if len(set(shapes.values())) != 1:
        print(f"ERROR: --layers {args.layers} selects layers with different "
              f"layout shapes {shapes} in {args.scene}; layer indices are "
              f"per-scene. Layer table:", file=sys.stderr)
        for i, L in enumerate(layers):
            print(f"  {i}: {L.get('name', '?'):<16} {L['layout'].shape}",
                  file=sys.stderr)
        sys.exit(2)

    blank = bytes(64)
    cells = {blank: 0}                       # cell bytes -> index
    cell_list = [blank]
    pats = {(0, 0, 0, 0): 0}                 # 4-cell tuple -> pattern id
    pat_list = [(0, 0, 0, 0)]
    tmap = np.zeros((ys, xs), dtype=">u2")

    def cell_id(block):
        b = jo_cell_bytes(block)
        i = cells.get(b)
        if i is None:
            i = len(cell_list); cells[b] = i; cell_list.append(b)
        return i

    for y in range(ys):
        for x in range(xs):
            e = 0xFFFF                        # composite: topmost non-blank layer
            for li in lis:
                v = int(layers[li]["layout"][y, x])
                if v < 0xFFFF:
                    e = v; break
            if e >= 0xFFFF:
                continue
            tl, tr, bl, br = tile_subcells(tiles, e)
            quad = (cell_id(tl), cell_id(tr), cell_id(bl), cell_id(br))
            pid = pats.get(quad)
            if pid is None:
                pid = len(pat_list); pats[quad] = pid; pat_list.append(quad)
            tmap[y, x] = pid

    assert len(cell_list) <= 4096, f"{len(cell_list)} cells > CN_12BIT 4096"

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    # Write via temp file + rename so a failure part-way never leaves a
    # corrupt CEL/PAT/TMP/PAL on disk (a crashed run used to clobber them).
    with open(args.out + ".CEL.tmpwrite", "wb") as f:
        for c in cell_list:
            f.write(c)
    with open(args.out + ".PAT.tmpwrite", "wb") as f:
        f.write(struct.pack(">H", len(pat_list)))
        for q in pat_list:
            f.write(struct.pack(">4H", *q))
    with open(args.out + ".TMP.tmpwrite", "wb") as f:
        f.write(struct.pack(">HH", xs, ys))
        f.write(tmap.tobytes())
    # jo_create_palette_from off-by-one: jo allocates user palettes at
    # CRAM[bank+1..bank+256] (vdp2_malloc.c __jo_cram = CRAM + 256 + 1, "first
    # palette reserved" + 1 entry per bank for jo's printf text color), but
    # VDP2 256-color mode reads CRAM[bank+pixel]. So cell-pixel V reads jo
    # PAL[V-1] -- the palette appears shifted DOWN by 1. Compensate by writing
    # the PAL pre-shifted UP by 1 (old[0]/transparent placeholder is dropped).
    pal555 = [to_rgb555(pal_rgb[i + 1]) if i < 255 else 0 for i in range(256)]
    with open(args.out + ".PAL.tmpwrite", "wb") as f:
        f.write(struct.pack(">256H", *pal555))
    for ext in (".CEL", ".PAT", ".TMP", ".PAL"):
        os.replace(args.out + ext + ".tmpwrite", args.out + ext)

    print(f"{args.out}: {len(cell_list)} cells ({len(cell_list)*64} B), "
          f"{len(pat_list)} tile patterns, map {xs}x{ys} ({xs*ys*2} B)")

    # round-trip: rebuild a region from CEL+PAT+TMAP and compare to direct render
    from render_scene import render_layer
    pal_q = pal_rgb
    # reconstruct tiles for a 40x14 test region via the streaming data
    ok = True
    for ty in range(min(48, ys - 1), min(60, ys)):
        for tx in range(0, min(40, xs)):
            pid = int(tmap[ty, tx])
            quad = pat_list[pid]
            # rebuild the 16x16 tile from its 4 cells
            rb = np.zeros((16, 16), np.uint8)
            for qi, (oy, ox) in enumerate(((0, 0), (0, 8), (8, 0), (8, 8))):
                rb[oy:oy+8, ox:ox+8] = jo_cell_decode(cell_list[quad[qi]])
            # expected (composited)
            e = 0xFFFF
            for li in lis:
                v = int(layers[li]["layout"][ty, tx])
                if v < 0xFFFF:
                    e = v; break
            exp = np.zeros((16, 16), np.uint8)
            if e < 0xFFFF:
                tl, tr, bl, br = tile_subcells(tiles, e)
                exp[0:8, 0:8], exp[0:8, 8:16], exp[8:16, 0:8], exp[8:16, 8:16] = tl, tr, bl, br
            if not np.array_equal(rb, exp):
                ok = False
    print("  round-trip (CEL jo-order + PAT + TMAP rebuilds tiles):",
          "OK" if ok else "MISMATCH")


if __name__ == "__main__":
    main()
