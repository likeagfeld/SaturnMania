#!/usr/bin/env python3
"""build_logos_atlas.py - Phase 3.1 Path B (Sega-only abbreviated splash).

Per `tools/_decomp_raw/SonicMania_Objects_Menu_LogoSetup.c:46-67` the LogoSetup
scene loads `Logos/Logos.bin` once and the UIPicture entities placed in the
Logos scene reference its 4 frames (Sega, CW, HC, PWG). The full 4-logo
sequence is ~16 seconds and violates the CLAUDE.md §4.5.1 boot-delay budget
(<5 s pre-title). Path B (user-chosen 2026-05-28) ships ONLY the Sega frame.

This builder packs the SEGA frame (anim 0) into a 4-bpp Color Bank 16 atlas
that mirrors the TSONIC.ATL + TITLE3D.ATL + ELECTRA.ATL v4 layout consumed by
Saturn-side `load_*_atlas` routines (src/mania/Objects/Title/TitleAssets.c).

SOURCE  extracted/Data/Sprites/Logos/Logos.bin   (4-anim RSDKv5 manifest)
        extracted/Data/Sprites/Logos/Logos.gif   (composite atlas)
        Logos.bin anim 0 'Sega Logo': sheet(0) src=(1,1,187,58) pivot=(-93,-29)

OUTPUT  cd/LOGOS.ATL    Saturn-native 4-bpp atlas (single-anim, single-frame)

FORMAT v4 (matches TSONIC/ELECTRA/TITLE3D — see build_title3d_atlas.py docstring)
    -- 44-byte header --
    u16  magic            0x5453 'TS'
    u16  version          0x0004
    u16  anim_count       1
    u16  palette_size     16
    u16  palette[16]      BGR1555 MSB=1 (opaque)
    u32  total_pixel_bytes

    -- 8-byte anim record * 1 --
    u16  frame_count       1
    u16  loop_index        0
    u16  first_frame_idx   0
    u16  total_dur_ticks   0

    -- 14-byte frame record * 1 --
    u16  width        (padded to multiple of 8)
    u16  height
    i16  pivot_x
    i16  pivot_y
    u16  duration
    u32  pixel_offset

    -- pixel pool (nibble-packed, hi-nibble = leftmost pixel)

USAGE
    python tools/build_logos_atlas.py
"""
import argparse
import os
import struct
import sys

import numpy as np

# Reuse the existing RSDK .bin parser + sheet loader (same path as Phase 1.32
# build_title3d_atlas.py and Phase 1.27 build_electricity_atlas.py).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from convert_ring_sprite import parse_spr, load_sheet


MAGIC          = 0x5453
VERSION        = 0x0004
PALETTE_SIZE   = 16
WIDTH_ALIGN    = 8

ATL_HEADER_FMT = ">HHHH" + "H" * PALETTE_SIZE + "I"
ATL_HEADER_LEN = 2 * 4 + 2 * PALETTE_SIZE + 4   # 44 bytes

ANIM_REC_FMT   = ">HHHH"
ANIM_REC_LEN   = 8
FRAME_REC_FMT  = ">HHhhHI"
FRAME_REC_LEN  = 14


def rgb_to_bgr1555(rgb):
    r5 = (rgb[..., 0].astype(np.uint16) >> 3) & 0x1F
    g5 = (rgb[..., 1].astype(np.uint16) >> 3) & 0x1F
    b5 = (rgb[..., 2].astype(np.uint16) >> 3) & 0x1F
    return np.uint16(0x8000) | (b5 << 10) | (g5 << 5) | r5


def kmeans_quantize(pixels, k, max_iter=80, seed=42):
    """Same k-means as TSONIC/TITLE3D atlas builders. Seeds with k-means++
    then iterates until centres converge (atol=0.5)."""
    rng = np.random.default_rng(seed)
    n = pixels.shape[0]
    if n <= k:
        pal = np.zeros((k, 3), np.uint8)
        pal[:n] = pixels
        idx = np.arange(n, dtype=np.int32)
        return pal, idx
    centers = np.zeros((k, 3), np.float32)
    first = rng.integers(0, n)
    centers[0] = pixels[first]
    for ci in range(1, k):
        d2 = np.min(
            np.sum((pixels.astype(np.float32) - centers[:ci, None]) ** 2, axis=2),
            axis=0,
        )
        if d2.sum() == 0:
            centers[ci] = pixels[rng.integers(0, n)]
        else:
            probs = d2 / d2.sum()
            pick = rng.choice(n, p=probs)
            centers[ci] = pixels[pick]
    pix32 = pixels.astype(np.float32)
    for _ in range(max_iter):
        d2 = np.sum((pix32[:, None, :] - centers[None, :, :]) ** 2, axis=2)
        idx = np.argmin(d2, axis=1)
        new_centers = centers.copy()
        for ci in range(k):
            mask = idx == ci
            if mask.any():
                new_centers[ci] = pix32[mask].mean(axis=0)
        if np.allclose(new_centers, centers, atol=0.5):
            centers = new_centers
            break
        centers = new_centers
    pal = np.clip(np.round(centers), 0, 255).astype(np.uint8)
    return pal, idx.astype(np.int32)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--logos-bin",
                    default="extracted/Data/Sprites/Logos/Logos.bin")
    ap.add_argument("--sprite-dir", default="extracted/Data/Sprites")
    ap.add_argument("--out", default="cd/LOGOS.ATL")
    ap.add_argument("--anim-index", type=int, default=0,
                    help="0=Sega (Path B default), 1=CW, 2=HC, 3=PWG")
    args = ap.parse_args()

    sheets, anims = parse_spr(args.logos_bin)
    print(f"[+] Logos.bin: {len(anims)} anims across {len(sheets)} sheets")
    if not anims:
        sys.exit("FAIL: no anims in Logos.bin")
    if args.anim_index < 0 or args.anim_index >= len(anims):
        sys.exit(f"FAIL: anim_index {args.anim_index} out of range [0,{len(anims)})")

    chosen = anims[args.anim_index]
    nm = chosen["name"].rstrip("\x00").strip()
    print(f"[+] Selected anim[{args.anim_index}] {nm!r}: {len(chosen['frames'])} frames")
    if len(chosen["frames"]) != 1:
        sys.exit(f"FAIL: expected 1 frame, got {len(chosen['frames'])}")

    sheet = load_sheet(args.sprite_dir, sheets[0])
    print(f"[+] Sheet {sheets[0]!r}: {sheet.shape[1]}x{sheet.shape[0]} RGBA")

    sid, sx, sy, w, h, px, py, dur = chosen["frames"][0]
    print(f"[+] Frame: src=({sx},{sy},{w},{h}) pivot=({px},{py}) dur={dur}")

    crop = sheet[sy:sy + h, sx:sx + w]
    mask = crop[..., 3] > 0
    opaque_rgb = crop[mask][..., :3]
    print(f"[+] Opaque pixels: {opaque_rgb.shape[0]:,} of {w * h:,}")
    if opaque_rgb.shape[0] == 0:
        sys.exit("FAIL: no opaque pixels in selected frame")

    fg_pal, _ = kmeans_quantize(opaque_rgb, k=15, max_iter=80)
    palette = np.zeros((16, 3), np.uint8)
    palette[0]    = (255, 0, 255)        # transparent magenta marker
    palette[1:16] = fg_pal
    print("[+] Built 16-color shared palette (slot 0 = transparent)")

    # Map every pixel to its nearest palette index 1..15 (or 0 if transparent).
    pal_rgb_f = palette.astype(np.float32)
    rgb = crop[..., :3].astype(np.float32)
    d2  = np.sum((rgb[..., None, :] - pal_rgb_f[None, None, 1:, :]) ** 2, axis=3)
    idx = (np.argmin(d2, axis=2) + 1).astype(np.uint8)
    idx[crop[..., 3] == 0] = 0

    # Pad width to VDP1 character unit (multiple of 8).
    w_padded = (w + WIDTH_ALIGN - 1) & ~(WIDTH_ALIGN - 1)
    if w_padded != w:
        padded = np.zeros((h, w_padded), np.uint8)
        padded[:, :w] = idx
        idx = padded
        print(f"[+] Padded width {w} -> {w_padded} (VDP1 char-unit alignment)")

    # Nibble-pack 4-bpp: hi-nibble = leftmost pixel of each pair.
    hi = idx[:, 0::2]
    lo = idx[:, 1::2]
    packed = ((hi & 0x0F) << 4) | (lo & 0x0F)
    packed_bytes = packed.astype(np.uint8).tobytes()
    print(f"[+] Packed pixels: {len(packed_bytes):,} bytes (4-bpp)")

    pal_bgr1555 = rgb_to_bgr1555(palette)
    pal_bgr1555[0] = 0x0000      # transparent (MSB=0)

    header = struct.pack(
        ATL_HEADER_FMT,
        MAGIC, VERSION,
        1,                       # anim_count
        PALETTE_SIZE,
        *(int(c) for c in pal_bgr1555),
        len(packed_bytes),
    )

    anim_block = struct.pack(ANIM_REC_FMT,
                             1,    # frame_count
                             0,    # loop_index
                             0,    # first_frame_global_idx
                             0)    # total_duration_ticks

    fr_block = struct.pack(FRAME_REC_FMT,
                           w_padded, h, px, py,
                           1,        # duration (synthetic)
                           0)        # pixel_offset (only one frame)

    out_bytes = header + anim_block + fr_block + packed_bytes

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(out_bytes)

    print(f"[+] Output {args.out}: {len(out_bytes):,} bytes "
          f"(header={ATL_HEADER_LEN}, anims={ANIM_REC_LEN}, "
          f"frames={FRAME_REC_LEN}, pixel_pool={len(packed_bytes)})")

    vdp1_user_area = 0x71D38
    print(f"[+] VDP1 char-RAM footprint: {len(packed_bytes):,} bytes "
          f"({100.0 * len(packed_bytes) / vdp1_user_area:.2f}% of "
          f"JO_VDP1_USER_AREA_SIZE)")


if __name__ == "__main__":
    main()
