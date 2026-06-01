#!/usr/bin/env python3
"""
export_sprite.py - Export RSDKv5 sprite-animation frames as Saturn VDP1 sprites
(RGB1555, big-endian). Reads the .bin animation (frame rects + pivots), the
referenced spritesheet GIF, and the GameConfig GLOBAL palette (player sprites
use it, NOT the stage-merged tile palette).

Single frame:  --anim N --frame F            -> raw W*H RGB1555 blob
Whole anim:    --anim N --all-frames         -> [u16 count][u16 W][u16 H] then
                                                count * (W*H RGB1555), every frame
   pivot-aligned into a uniform W*H canvas so the character doesn't jitter and
   one draw offset works for all frames. VDP1 width padded to a multiple of 8.
Palette index 0 -> 0x0000 (VDP1 transparent); else 0x8000 | RGB1555.

Usage:
    python export_sprite.py --anim-bin extracted/Data/Sprites/Players/Sonic.bin \
      --sheet-dir extracted/Data/Sprites/Players \
      --gameconfig extracted/Data/Game/GameConfig.bin --anim 5 --all-frames \
      --out ../cd/SONWALK.SPR
"""
import argparse
import os
import struct

import numpy as np
from PIL import Image

from render_scene import read_global_palette


def parse_anim(path):
    d = open(path, "rb").read()
    p = [0]
    def u8():  v = d[p[0]]; p[0] += 1; return v
    def u16(): v = struct.unpack_from("<H", d, p[0])[0]; p[0] += 2; return v
    def s16(): v = struct.unpack_from("<h", d, p[0])[0]; p[0] += 2; return v
    def u32(): v = struct.unpack_from("<I", d, p[0])[0]; p[0] += 4; return v
    def s():
        n = u8(); v = d[p[0]:p[0] + n].decode("latin-1").rstrip("\x00"); p[0] += n; return v
    assert d[:4] == b"SPR\x00", "not an SPR animation"
    p[0] = 4
    u32()
    sc = u8(); sheets = [s() for _ in range(sc)]
    hc = u8(); [s() for _ in range(hc)]
    ac = u16()
    anims = []
    for _ in range(ac):
        name = s(); fc = u16(); u16(); u8(); u8()
        frames = []
        for _ in range(fc):
            sheet = u8(); u16(); u16()
            sx, sy, w, h = u16(), u16(), u16(), u16()
            px, py = s16(), s16()
            for _ in range(hc):
                s16(); s16(); s16(); s16()
            frames.append({"sheet": sheet, "sx": sx, "sy": sy, "w": w, "h": h,
                           "px": px, "py": py})
        anims.append({"name": name, "frames": frames})
    return sheets, anims


def to_rgb1555(rgb):
    r, g, b = int(rgb[0]) >> 3, int(rgb[1]) >> 3, int(rgb[2]) >> 3
    return 0x8000 | (b << 10) | (g << 5) | r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--anim-bin", required=True)
    ap.add_argument("--sheet-dir", required=True)
    ap.add_argument("--gameconfig", required=True)
    ap.add_argument("--anim", type=int, default=0)
    ap.add_argument("--frame", type=int, default=0)
    ap.add_argument("--all-frames", action="store_true")
    ap.add_argument("--bank", type=int, default=0)
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    sheets, anims = parse_anim(args.anim_bin)
    anim = anims[args.anim]
    pal = read_global_palette(args.gameconfig)[0][args.bank]
    sheet_cache = {}

    def load_sheet(i):
        if i not in sheet_cache:
            name = sheets[i].rstrip("\x00").split("/")[-1]
            sheet_cache[i] = np.array(
                Image.open(os.path.join(args.sheet_dir, name)).convert("P"), np.uint8)
        return sheet_cache[i]

    frames = anim["frames"] if args.all_frames else [anim["frames"][args.frame]]

    # Uniform canvas aligned by pivot: place each frame so the origin (pivot)
    # lands at a common anchor. canvas covers all frames' extents.
    minpx = min(f["px"] for f in frames)
    minpy = min(f["py"] for f in frames)
    cw = max(f["w"] + (f["px"] - minpx) for f in frames)
    ch = max(f["h"] + (f["py"] - minpy) for f in frames)
    cw = (cw + 7) & ~7                         # VDP1 width multiple of 8

    blob = bytearray()
    for f in frames:
        sheet = load_sheet(f["sheet"])
        sub = sheet[f["sy"]:f["sy"] + f["h"], f["sx"]:f["sx"] + f["w"]]
        canvas = np.zeros((ch, cw), dtype=">u2")
        ox = f["px"] - minpx
        oy = f["py"] - minpy
        for y in range(f["h"]):
            for x in range(f["w"]):
                idx = int(sub[y, x])
                if idx != 0:
                    canvas[oy + y, ox + x] = to_rgb1555(pal[idx])
        blob += canvas.tobytes()

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "wb") as fh:
        if args.all_frames:
            fh.write(struct.pack(">HHH", len(frames), cw, ch))
        fh.write(blob)
    print(f'{args.out}: anim "{anim["name"].strip()}" '
          f'{len(frames)} frame(s) -> {cw}x{ch} each '
          f'(anchor offset to feet: drawX-{-minpx}, drawY-{-minpy})')
    print(f"  SPR_W={cw} SPR_H={ch} FRAMES={len(frames)} "
          f"PIVOT_X={-minpx} PIVOT_Y={-minpy}")


if __name__ == "__main__":
    main()
