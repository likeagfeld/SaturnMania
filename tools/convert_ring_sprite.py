#!/usr/bin/env python3
"""convert_ring_sprite.py - extract the spinning Ring frames from
Data/Sprites/Global/Ring.bin and emit cd/RING.SPR in the multi-frame VDP1
RGB1555 format the Saturn runtime already uses for SONWALK.SPR:
    u16 BE  frame count
    u16 BE  frame width  (px)
    u16 BE  frame height (px)
    frames * width * height * u16 BE  (Saturn BGR1555: bit15=opaque,
        bits 10-14=B, 5-9=G, 0-4=R. Pixel 0x0000 = transparent.)

Mania .bin sprite-animation format (RSDKv5/RSDK/Graphics/Animation.cpp; format
only, no source copied):
    4B    "SPR\\0"
    u32 LE  frameCountTotal
    u8     sheetCount
    For each sheet: Pascal string (u8 len + bytes) -> sheet GIF filename.
    u8     hitboxCount
    For each hitbox: Pascal string (hitbox name).
    u16 LE  animationCount
    For each animation:
      Pascal string  -> animation name (e.g. "Idle")
      u16 LE  frameCount
      u16 LE  animationSpeed
      u8     loopIndex
      u8     rotationStyle
      For each frame:
        u8     sheetID
        u16 LE  duration
        u16 LE  unicodeChar
        u16 LE  sprX, sprY, width, height
        i16 LE  pivotX, pivotY
        For each hitbox: 4 i16 LE (left, top, right, bottom)
"""
import argparse, os, struct, sys
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
from render_scene import R, read_global_palette, read_stage_palette, build_palette
from convert_vdp2 import to_rgb555


SIG_SPR = b"SPR\x00"


def read_pstr(r):
    n = r.u8()
    s = r.d[r.p:r.p + n].decode("latin-1", errors="ignore")
    r.p += n
    return s.rstrip("\x00").strip()                 # strip trailing nulls/space


def parse_spr(path):
    with open(path, "rb") as f:
        r = R(f.read())
    assert r.d[:4] == SIG_SPR, f"{path}: missing SPR signature"
    r.p = 4
    r.u32()                                         # total frame count (unused)
    sheet_count = r.u8()
    sheets = [read_pstr(r) for _ in range(sheet_count)]
    hitbox_count = r.u8()
    for _ in range(hitbox_count):
        read_pstr(r)

    anim_count = r.u16()
    anims = []
    for _ in range(anim_count):
        name = read_pstr(r)
        fc = r.u16(); spd = r.u16(); loop = r.u8(); rot = r.u8()
        frames = []
        for _ in range(fc):
            sheet_id = r.u8()
            dur = r.u16(); uc = r.u16()
            sx = r.u16(); sy = r.u16()
            w = r.u16(); h = r.u16()
            px = struct.unpack_from("<h", r.d, r.p)[0]; r.p += 2
            py = struct.unpack_from("<h", r.d, r.p)[0]; r.p += 2
            for _h in range(hitbox_count):
                r.p += 8                            # skip hitbox
            # `uc` is the per-frame unicodeChar (Animation.cpp:33 format).
            # TitleCard's "Name Letters" font anim maps each glyph to its
            # ASCII/UTF-16 codepoint here; SetSpriteString (Animation.cpp:
            # 211-231) matches string chars against it. Carried in the
            # frame tuple so build_entity_atlas can emit it to the MET
            # sidecar. (Backwards compatible: existing 8-tuple consumers
            # index [0]..[7] and ignore the trailing unicode at [8].)
            frames.append((sheet_id, sx, sy, w, h, px, py, dur, uc))
        anims.append({"name": name, "speed": spd, "loop": loop,
                       "rotation": rot, "frames": frames})
    return sheets, anims


def load_sheet(sprite_dir, sheet_name):
    """Sheet filenames in Ring.bin are like 'Global/Ring.gif' relative to
    Data/Sprites. RSDK GIFs use palette index 0 as transparent placeholder
    (which typically renders as magenta 255,0,255 in the GCT). Return an
    (H, W, 4) RGBA array with alpha=0 wherever the palette index is 0
    (independent of whether the GIF declares a transparency chunk)."""
    # Normalise path: the .bin's sheet field uses forward slashes; on Windows
    # os.path.join then introduces a backslash which PIL rejects on some
    # builds. Force all forward slashes.
    p = (sprite_dir.rstrip("/\\") + "/" + sheet_name.replace("\\", "/"))
    img = Image.open(p)
    if img.mode != "P":
        # nothing to do; trust file's own alpha
        return np.array(img.convert("RGBA"))
    indices = np.array(img, dtype=np.uint8)                 # (H, W) palette indices
    pal_rgb = np.array(img.getpalette()[:768], np.uint8).reshape(256, 3)
    h, w = indices.shape
    rgba = np.empty((h, w, 4), np.uint8)
    rgba[..., :3] = pal_rgb[indices]
    rgba[..., 3]  = np.where(indices == 0, 0, 255)
    return rgba


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("ring_bin", help="extracted/Data/Sprites/Global/Ring.bin")
    ap.add_argument("--sprite-dir", default="extracted/Data/Sprites",
                    help="root of the Sprites/ data dir (resolves sheet paths)")
    ap.add_argument("--anim", default=None,
                    help="animation name to extract (default = first non-empty)")
    ap.add_argument("--out", required=True)
    args = ap.parse_args()

    sheets, anims = parse_spr(args.ring_bin)
    if not anims:
        raise SystemExit("no animations in Ring.bin")

    target = None
    if args.anim:
        for a in anims:
            if a["name"].strip("\x00") == args.anim:
                target = a; break
    if target is None:
        # default: first animation that actually has frames
        for a in anims:
            if a["frames"]:
                target = a; break
    print(f"using animation: {target['name']!r}  "
          f"({len(target['frames'])} frames, speed={target['speed']}, "
          f"loop={target['loop']})")

    # All frames in the spinning ring share the same w/h. Verify + load sheet.
    first = target["frames"][0]
    w, h = first[3], first[4]
    print(f"frame size: {w}x{h}")

    sheet_imgs = {}
    for i, name in enumerate(sheets):
        sheet_imgs[i] = load_sheet(args.sprite_dir, name)

    # Convert each frame to BGR1555 with MSB=opaque-flag.
    out = bytearray()
    out += struct.pack(">HHH", len(target["frames"]), w, h)
    for (sheet_id, sx, sy, fw, fh, _px, _py, _dur, _uc) in target["frames"]:
        atlas = sheet_imgs[sheet_id]
        if fw != w or fh != h:
            # if frames differ, pad to first frame's size with transparent
            pad = np.zeros((h, w, 4), dtype=np.uint8)
            pad[:fh, :fw] = atlas[sy:sy + fh, sx:sx + fw]
            patch = pad
        else:
            patch = atlas[sy:sy + h, sx:sx + w]
        # patch is RGBA; alpha == 0 -> transparent (0x0000); else BGR555 + MSB.
        for row in range(h):
            for col in range(w):
                r, g, b, a = (int(v) for v in patch[row, col])
                if a == 0:
                    px = 0x0000
                else:
                    px = 0x8000 | to_rgb555_rgb(r, g, b)
                out += struct.pack(">H", px)

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(out)
    print(f"{args.out}: {len(target['frames'])} frames x {w}x{h} "
          f"({len(out)} B)")


def to_rgb555_rgb(r, g, b):
    """Saturn BGR1555 (the existing tools/convert_vdp2.py uses the same):
       bits 0-4 = R, 5-9 = G, 10-14 = B."""
    return ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)


if __name__ == "__main__":
    main()
