#!/usr/bin/env python3
"""
export_vdp2_image.py - Export a WxH region of an RSDK layer as an 8bpp indexed
image + RGB555 palette, for display on a Saturn VDP2 NBG0 cell background via
Jo Engine's jo_vdp2_set_nbg0_8bits_image (first on-Saturn GHZ milestone, before
the full raw-SGL streaming renderer).

Outputs (big-endian; SH-2 reads u16 big-endian natively):
    <out>.DAT  W*H bytes, 8bpp palette indices (index 0 = transparent)
    <out>.PAL  256 * uint16 Saturn RGB555 (0x8000 opaque bit)

NBG0 cell VRAM bank is 128 KB, so keep W*H <= 131072 (e.g. 512x224 = 114688).

Usage:
    python export_vdp2_image.py extracted/Data/Stages/GHZ --scene Scene1.bin \
      --layer 3 --bank 1 --x 0 --y 768 --w 512 --h 224 --out ../cd/GHZBG
"""
import argparse
import os
import struct

import numpy as np

from render_scene import load_tiles, read_stage_palette, parse_scene, build_palette
from convert_vdp2 import to_rgb555


def render_indices(layer, tiles, x0, y0, w, h):
    """Render a w x h pixel region to 8bpp indices (0 = transparent)."""
    out = np.zeros((h, w), np.uint8)
    lay = layer["layout"]; ys, xs = lay.shape
    for py in range(h):
        wy = y0 + py
        ty = wy >> 4
        if ty < 0 or ty >= ys:
            continue
        sy = wy & 15
        for px in range(w):
            wx = x0 + px
            tx = wx >> 4
            if tx < 0 or tx >= xs:
                continue
            e = int(lay[ty, tx])
            if e >= 0xFFFF:
                continue
            idx = e & 0x3FF
            if idx >= tiles.shape[0]:
                continue
            yy = (15 - (sy)) if (e & 0x800) else sy
            xx = (15 - (wx & 15)) if (e & 0x400) else (wx & 15)
            out[py, px] = tiles[idx, yy, xx]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stage_dir")
    ap.add_argument("--scene", default="Scene1.bin")
    ap.add_argument("--layers", default="3,4",
                    help="comma-separated layer indices, composited in order "
                         "(GHZ foreground = FG Low 3 + FG High 4)")
    ap.add_argument("--bank", type=int, default=0)
    ap.add_argument("--x", type=int, default=0)
    ap.add_argument("--y", type=int, default=0)
    ap.add_argument("--w", type=int, default=512)
    ap.add_argument("--h", type=int, default=224)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    assert args.w * args.h <= 131072, "image exceeds 128 KB VDP2 bank"

    gif = os.path.join(args.stage_dir, "16x16Tiles.gif")
    tiles = load_tiles(gif)
    banks, masks = read_stage_palette(os.path.join(args.stage_dir, "StageConfig.bin"))
    pal_rgb = build_palette(gif, banks, masks, args.bank)
    layers = parse_scene(os.path.join(args.stage_dir, args.scene))

    # Completeness guard: GHZ-style scenes split the playfield across multiple
    # layers at the same (max) parallax. Silently converting only one leaves
    # black gaps where the others have tiles. List all layers and WARN if any
    # playfield-parallax layer is omitted from --layers.
    chosen = [int(x) for x in args.layers.split(",")]
    maxpar = max(l["parallax"] for l in layers)
    print("scene layers:")
    for i, l in enumerate(layers):
        flag = " <- playfield" if l["parallax"] == maxpar else ""
        mark = "*" if i in chosen else " "
        print(f"  [{mark}] {i} '{l['name'].strip()}' parallax={l['parallax']}"
              f" {l['xs']}x{l['ys']}{flag}")
    missing = [i for i, l in enumerate(layers)
               if l["parallax"] == maxpar and i not in chosen]
    if missing:
        print(f"  WARNING: playfield-parallax layers {missing} NOT included "
              f"-> expect black gaps. Add them to --layers.")

    # Composite the requested layers in order (later layers overlay where they
    # have non-transparent pixels) so FG High fills the gaps in FG Low.
    idx = np.zeros((args.h, args.w), np.uint8)
    for li in (int(x) for x in args.layers.split(",")):
        layer_idx = render_indices(layers[li], tiles, args.x, args.y, args.w, args.h)
        m = layer_idx != 0
        idx[m] = layer_idx[m]

    # Jo's jo_vdp2_set_nbgX_8bits_image reads the image h-mirrored + column-major
    # (the documented "clockwise rotated" optimisation). Pre-rotate 90 deg CW so
    # it displays upright. After rotation, on-Saturn width=args.h, height=args.w.
    idx = np.rot90(idx, k=-1)               # CW; shape (w, h) -> rows=w, cols=h
    out_w, out_h = idx.shape[1], idx.shape[0]

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out + ".DAT", "wb") as f:
        f.write(np.ascontiguousarray(idx).tobytes())
    print(f"(rotated CW; pass width={out_w} height={out_h} to Jo)")
    # pre-shifted UP by 1 -- see tools/shift_pal.py for the jo CRAM off-by-one.
    pal555 = [to_rgb555(pal_rgb[i + 1]) if i < 255 else 0 for i in range(256)]
    with open(args.out + ".PAL", "wb") as f:
        f.write(struct.pack(">256H", *pal555))

    nonzero = int((idx > 0).sum())
    print(f"Wrote {args.out}.DAT ({args.w}x{args.h} = {args.w*args.h} B, "
          f"{nonzero} non-transparent px) and {args.out}.PAL (256 colors)")


if __name__ == "__main__":
    main()
