#!/usr/bin/env python3
"""convert_anim_sprite.py - Generic RSDK animation -> Saturn multi-frame SPR.

Reads any RSDKv5 sprite-animation .bin (Sonic.bin, Ring.bin, Motobug.bin, ...)
plus its referenced GIF atlas(es) and writes a multi-frame Saturn BGR1555 SPR
that the existing engine consumes with `jo_sprite_draw3D` (centre-origin).

The key engineering point is **pivot-aware composition**. Mania frames within
an animation have different bounding boxes and different pivots (the engine
draws each frame with its own pivot offset). The Saturn engine draws every
frame of an animation with one fixed centre-origin anchor. So we:

  1. Read every frame's (sprX, sprY, width, height, pivotX, pivotY).
  2. Compute the smallest canvas (W,H) that contains every frame when each
     frame's pivot is aligned to the canvas centre.
  3. Render each frame into a W*H BGRA buffer at the right pivot-aligned
     offset, alpha=0 elsewhere.
  4. Emit the standard SPR header (u16 BE count, w, h) + per-frame BGR1555
     pixel data with MSB=opaque-flag.

Output format (matches existing SONIC.SPR / SONWALK.SPR / RING.SPR layout):
    u16 BE   frame count
    u16 BE   frame width  (canvas, pixels)
    u16 BE   frame height (canvas, pixels)
    N frames * width * height * u16 BE   BGR1555, MSB = opaque, 0x0000 = transparent.

Usage:
    python tools/convert_anim_sprite.py extracted/Data/Sprites/Players/Sonic.bin \\
        --anim "Idle" --out cd/SONIC.SPR
    python tools/convert_anim_sprite.py extracted/Data/Sprites/Players/Sonic.bin \\
        --anim "Walk" --out cd/SONWALK.SPR
"""
import argparse
import os
import struct
import sys

import numpy as np
from PIL import Image

# Reuse the .bin parser already proven against Ring.bin.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from convert_ring_sprite import parse_spr, load_sheet, to_rgb555_rgb


def compose_animation(sprite_dir, sheets, anim):
    """Return (canvas_w, canvas_h, frames_bgra) for the given animation.

    `frames_bgra` is a list of (canvas_h, canvas_w, 4) numpy arrays.
    """
    frames = anim["frames"]
    if not frames:
        raise ValueError(f"animation {anim['name']!r} has 0 frames")

    # Frame tuple = (sheet_id, sx, sy, w, h, pivotX, pivotY, dur).
    # In RSDK the on-screen draw is at: dst = (drawX + pivotX, drawY + pivotY).
    # So pivot is the *offset from sprite origin to the top-left of the
    # blit*. The sprite origin (where the engine anchors) is at
    # (-pivotX, -pivotY) in atlas pixel space relative to the frame
    # rectangle. To centre every frame's origin on a single canvas point,
    # we compute the canvas bounding box around origin (0,0):
    #     left   = min(pivotX)        (most negative pivotX)
    #     right  = max(pivotX + w)
    #     top    = min(pivotY)
    #     bottom = max(pivotY + h)
    lefts, rights, tops, bottoms = [], [], [], []
    for (sid, sx, sy, w, h, px, py, dur) in frames:
        # px/py from the .bin are stored as i16 (sign-extend handled in
        # parse_spr already).
        lefts.append(px)
        rights.append(px + w)
        tops.append(py)
        bottoms.append(py + h)
    left, right = min(lefts), max(rights)
    top, bottom = min(tops), max(bottoms)

    # Origin sits at (-left, -top) inside the canvas; canvas spans [left,right) x [top,bottom).
    canvas_w = right - left
    canvas_h = bottom - top
    origin_x = -left
    origin_y = -top

    # Saturn sprite dimensions must be even (and ideally multiples of 8 for
    # alignment); pad symmetrically as needed. jo_sprite_add tolerates odd
    # widths but VDP1 prefers multiples of 8.
    pad_w = (-canvas_w) & 7
    pad_h = (-canvas_h) & 7
    if pad_w:
        # split the pad: half left, half right (keep origin centred-ish)
        canvas_w += pad_w
        origin_x += pad_w // 2
    if pad_h:
        canvas_h += pad_h
        origin_y += pad_h // 2

    # Load each referenced sheet once.
    sheet_imgs = {}
    for i, sheet_path in enumerate(sheets):
        sheet_imgs[i] = load_sheet(sprite_dir, sheet_path)

    out_frames = []
    for (sid, sx, sy, w, h, px, py, dur) in frames:
        canvas = np.zeros((canvas_h, canvas_w, 4), dtype=np.uint8)
        atlas = sheet_imgs[sid]
        # Source rectangle on the atlas.
        ah, aw = atlas.shape[:2]
        if sx < 0 or sy < 0 or sx + w > aw or sy + h > ah:
            raise ValueError(
                f"frame rectangle ({sx},{sy} {w}x{h}) outside atlas {aw}x{ah}")
        patch = atlas[sy:sy + h, sx:sx + w]
        # Destination top-left = origin + (px, py).
        dx = origin_x + px
        dy = origin_y + py
        canvas[dy:dy + h, dx:dx + w] = patch
        out_frames.append(canvas)

    return canvas_w, canvas_h, out_frames


def write_spr(out_path, frames_bgra):
    h, w = frames_bgra[0].shape[:2]
    buf = bytearray()
    buf += struct.pack(">HHH", len(frames_bgra), w, h)
    for frame in frames_bgra:
        # vectorised BGR1555 conversion.
        r = (frame[..., 0] >> 3).astype(np.uint16)
        g = (frame[..., 1] >> 3).astype(np.uint16)
        b = (frame[..., 2] >> 3).astype(np.uint16)
        a = frame[..., 3]
        px = (b << 10) | (g << 5) | r
        px = np.where(a == 0, 0, 0x8000 | px).astype(">u2")
        buf += px.tobytes()
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "wb") as f:
        f.write(buf)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("anim_bin", help="path to RSDK .bin (Sonic.bin, Ring.bin, ...)")
    ap.add_argument("--sprite-dir", default="extracted/Data/Sprites",
                    help="root that sheet paths in the .bin resolve against")
    ap.add_argument("--anim", required=True,
                    help='animation name (e.g. "Idle", "Walk", "Normal Ring", "Move")')
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    sheets, anims = parse_spr(args.anim_bin)
    target = None
    for a in anims:
        if a["name"].strip() == args.anim.strip():
            target = a
            break
    if target is None:
        names = ", ".join(repr(a["name"]) for a in anims)
        raise SystemExit(f"animation {args.anim!r} not in {args.anim_bin}; "
                         f"available: {names}")

    w, h, frames = compose_animation(args.sprite_dir, sheets, target)
    write_spr(args.out, frames)
    print(f"{args.out}: anim={args.anim!r}  {len(frames)} frames "
          f"x {w}x{h}  ({os.path.getsize(args.out):,} B)")


if __name__ == "__main__":
    main()
