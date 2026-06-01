#!/usr/bin/env python3
"""build_title3d_atlas.py - Pack ALL Title/Background.bin animations into a
single 4-bpp VDP1 sprite atlas file consumed by Saturn-side TitleAssets.c.

Per `tools/_decomp_raw/SonicMania_Objects_Title_Title3DSprite.c:42-64` and
`tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:56-92` the Title scene
loads `Title/Background.bin` ONCE and both `TitleBG` and `Title3DSprite`
read from it. Background.bin (size 321 B, sig "SPR\\0") contains 6 anims
totalling 10 frames against a single sheet `Title/BG.gif`:

  anim 0 'MountainTop 1'     1 frame  176x16  pivot (-88,-16)  TitleBG type 0
  anim 1 'Mountain Top 2'    1 frame  192x16  pivot (-96,-16)  TitleBG type 1
  anim 2 'Reflection'        1 frame  176x16  pivot (-88,  0)  TitleBG type 2
  anim 3 'Water Sparkle'     1 frame  192x16  pivot (-96,  0)  TitleBG type 3
  anim 4 'Wing Shine'        1 frame   64x128 pivot (-32,-64)  TitleBG type 4
  anim 5 'Billboard Sprites' 5 frames  varies (-8..-3,-30..-9) Title3DSprite

For Phase 1.32 the asset bridge consumes anim 5 (the 5 billboard frames:
Mountain L/M/S + Tree + Bush). Anims 0..4 are loaded but not yet wired
(TitleBG already renders via the existing RBG0 backdrop pipeline; future
Phase Z can wire them as overlays if needed).

The 4-bpp Color Bank 16 path matches the existing TSONIC.ATL + ELECTRA.ATL
pipelines: VDP1 PMOD field encoding per VDP1 Manual §5.5.4 + CRAM palette
upload at a fresh non-colliding slot.

INPUTS
    extracted/Data/Sprites/Title/Background.bin   (RSDKv5 .bin manifest)
    extracted/Data/Sprites/Title/BG.gif           (palettized RSDK atlas)

OUTPUT
    cd/TITLE3D.ATL   single flat file consumed by TitleAssets.c

FORMAT v4 (big-endian throughout; matches TSONIC.ATL + ELECTRA.ATL exactly)
    -- header (44 bytes) --
    u16  magic            0x5453 'TS'  (Title-Sprite family)
    u16  version          0x0004
    u16  anim_count                  (6)
    u16  palette_size               (always 16)
    u16  palette[16]                BGR1555 MSB=1 (opaque); slot 0 = transparent
    u32  total_pixel_bytes          sum across all anims' pixel pools

    -- per-anim record (8 bytes), anim_count records --
    u16  frame_count
    u16  loop_index                 RSDK loop target
    u16  first_frame_global_idx     index into the global frame-record table
    u16  total_duration_ticks       sum of dur across this anim's frames

    -- global frame-record table (14 bytes/frame), total = sum(anim.frame_count) --
    u16  width                      padded to multiple of 8 (VDP1 char H unit)
    u16  height                     1..255
    i16  pivot_x                    RSDK pivot, preserved as-is
    i16  pivot_y                    RSDK pivot
    u16  duration                   ticks at 60Hz
    u32  pixel_offset               byte offset into pixel pool

    -- pixel pool, concatenated --
    u8   pixels[...]                nibble-packed 4-bpp indices
                                    (high nibble = leftmost pixel of pair)

USAGE
    python tools/build_title3d_atlas.py
"""
import argparse
import os
import struct
import sys

import numpy as np
from PIL import Image

# Reuse the existing RSDK .bin parser + sheet loader.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from convert_ring_sprite import parse_spr, load_sheet


MAGIC          = 0x5453   # 'TS' (Title-Sprite family, matches TSONIC/ELECTRA)
VERSION        = 0x0004   # v4 (anim_count + per-anim records + per-frame records)
PALETTE_SIZE   = 16
WIDTH_ALIGN    = 8        # VDP1 character width unit
ATL_HEADER_FMT = ">HHHH" + "H"*PALETTE_SIZE + "I"
ATL_HEADER_LEN = 2*4 + 2*PALETTE_SIZE + 4    # 8 + 32 + 4 = 44 bytes

ANIM_REC_FMT   = ">HHHH"  # frame_count, loop_index, first_frame_global_idx, total_dur
ANIM_REC_LEN   = 8

FRAME_REC_FMT  = ">HHhhHI"  # w, h, px, py, dur, pixel_offset
FRAME_REC_LEN  = 14


def rgb_to_bgr1555(rgb):
    """Convert a (..., 3) uint8 RGB to Saturn BGR1555 with MSB=1 (opaque)."""
    r5 = (rgb[..., 0].astype(np.uint16) >> 3) & 0x1F
    g5 = (rgb[..., 1].astype(np.uint16) >> 3) & 0x1F
    b5 = (rgb[..., 2].astype(np.uint16) >> 3) & 0x1F
    return np.uint16(0x8000) | (b5 << 10) | (g5 << 5) | r5


def kmeans_quantize(pixels, k, max_iter=64, seed=42):
    """Standard k-means used by TSONIC.ATL and ELECTRA.ATL builders."""
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
        d2 = np.min(np.sum((pixels.astype(np.float32) - centers[:ci, None])**2, axis=2), axis=0)
        if d2.sum() == 0:
            centers[ci] = pixels[rng.integers(0, n)]
        else:
            probs = d2 / d2.sum()
            pick = rng.choice(n, p=probs)
            centers[ci] = pixels[pick]
    pix32 = pixels.astype(np.float32)
    for _ in range(max_iter):
        d2 = np.sum((pix32[:, None, :] - centers[None, :, :])**2, axis=2)
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
    ap.add_argument("--background-bin",
                    default="extracted/Data/Sprites/Title/Background.bin")
    ap.add_argument("--sprite-dir", default="extracted/Data/Sprites")
    ap.add_argument("--out", default="cd/TITLE3D.ATL")
    args = ap.parse_args()

    sheets, anims = parse_spr(args.background_bin)
    print(f"[+] Background.bin: {len(anims)} anims across {len(sheets)} sheets")
    if len(anims) < 6:
        sys.exit(f"FAIL: expected 6 anims, got {len(anims)}")
    if len(sheets) != 1:
        sys.exit(f"FAIL: expected 1 sheet, got {len(sheets)}: {sheets}")

    for i, a in enumerate(anims):
        nm = a["name"].rstrip("\x00").strip()
        print(f"    anim[{i}] {nm!r}: {len(a['frames'])} frames "
              f"speed={a.get('speed')} loop={a.get('loop')}")

    sheet = load_sheet(args.sprite_dir, sheets[0])
    print(f"[+] Sheet {sheets[0]!r}: {sheet.shape[1]}x{sheet.shape[0]} RGBA")

    # ---- Pass 1: collect opaque RGB pixels across ALL anims' frames
    opaque_rgb = []
    for a in anims:
        for (sid, sx, sy, w, h, px, py, dur) in a["frames"]:
            if w == 0 or h == 0:
                continue
            crop = sheet[sy:sy+h, sx:sx+w]
            mask = crop[..., 3] > 0
            opaque_rgb.append(crop[mask][..., :3])
    if not opaque_rgb:
        sys.exit("FAIL: no opaque pixels across all anims")
    pix_pool_rgb = np.concatenate(opaque_rgb, axis=0)
    print(f"[+] Total opaque pixels: {pix_pool_rgb.shape[0]:,}")

    fg_pal, _ = kmeans_quantize(pix_pool_rgb, k=15, max_iter=80)
    palette = np.zeros((16, 3), np.uint8)
    palette[0] = (255, 0, 255)
    palette[1:16] = fg_pal
    print("[+] Built 16-color shared palette")

    # ---- Pass 2: convert each frame across all anims to nibble-packed 4-bpp
    pal_rgb_f = palette.astype(np.float32)
    pool = bytearray()
    all_frame_records = []
    anim_records = []

    global_frame_cursor = 0
    for ai, a in enumerate(anims):
        first_global_idx = global_frame_cursor
        anim_frame_count = len(a["frames"])
        anim_loop_index = a.get("loop", 0) if a.get("loop") is not None else 0
        anim_total_dur = 0

        for fidx, (sid, sx, sy, w, h, px, py, dur) in enumerate(a["frames"]):
            if w == 0 or h == 0:
                # Degenerate placeholder frame — emit zero-sized record.
                all_frame_records.append((0, 0, px, py, dur, len(pool)))
                global_frame_cursor += 1
                anim_total_dur += dur
                continue
            crop = sheet[sy:sy+h, sx:sx+w]
            rgb  = crop[..., :3].astype(np.float32)
            d2 = np.sum((rgb[..., None, :] - pal_rgb_f[None, None, 1:, :])**2, axis=3)
            idx = (np.argmin(d2, axis=2) + 1).astype(np.uint8)
            idx[crop[..., 3] == 0] = 0

            w_padded = (w + WIDTH_ALIGN - 1) & ~(WIDTH_ALIGN - 1)
            if w_padded != w:
                padded = np.zeros((h, w_padded), np.uint8)
                padded[:, :w] = idx
                idx = padded
            hi = idx[:, 0::2]
            lo = idx[:, 1::2]
            packed = ((hi & 0x0F) << 4) | (lo & 0x0F)
            packed_bytes = packed.astype(np.uint8).tobytes()
            pix_offset = len(pool)
            pool.extend(packed_bytes)
            all_frame_records.append((w_padded, h, px, py, dur, pix_offset))
            global_frame_cursor += 1
            anim_total_dur += dur

            print(f"    anim{ai} frame {fidx}: {w}x{h} -> {w_padded}x{h}, "
                  f"{len(packed_bytes):5d} B, pivot=({px},{py}), dur={dur}")

        anim_records.append((anim_frame_count, anim_loop_index,
                             first_global_idx, anim_total_dur))

    total_pix = len(pool)
    total_frame_count = sum(rec[0] for rec in anim_records)
    print(f"[+] Pixel pool: {total_pix:,} bytes, "
          f"{total_frame_count} frames across {len(anim_records)} anims")

    pal_bgr1555 = rgb_to_bgr1555(palette)
    pal_bgr1555[0] = 0x0000

    header = struct.pack(
        ATL_HEADER_FMT,
        MAGIC, VERSION,
        len(anim_records),
        PALETTE_SIZE,
        *(int(c) for c in pal_bgr1555),
        int(total_pix)
    )

    anim_block = b"".join(
        struct.pack(ANIM_REC_FMT, fc, li, fg, td)
        for (fc, li, fg, td) in anim_records
    )

    fr_block = b"".join(
        struct.pack(FRAME_REC_FMT,
                    int(w), int(h), int(px), int(py), int(dur), int(off))
        for (w, h, px, py, dur, off) in all_frame_records
    )

    out = header + anim_block + fr_block + bytes(pool)

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(out)

    vdp1_user_area = 0x71D38
    print(f"[+] Output {args.out}: {len(out):,} bytes "
          f"(header={ATL_HEADER_LEN}, anims={len(anim_block)}, "
          f"frames={len(fr_block)} ({total_frame_count} records), "
          f"pixel_pool={total_pix})")
    print(f"[+] VDP1 atlas footprint at boot: {total_pix:,} bytes of "
          f"{vdp1_user_area:,} ({100.0*total_pix/vdp1_user_area:.2f}%)")
    if total_pix >= vdp1_user_area:
        sys.exit(f"FAIL: VDP1 atlas {total_pix} >= JO_VDP1_USER_AREA_SIZE {vdp1_user_area}")


if __name__ == "__main__":
    main()
