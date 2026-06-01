#!/usr/bin/env python3
"""build_electricity_atlas.py - Pack the TitleSetup Electricity arc anim
(per `extracted/Data/Sprites/Title/Electricity.bin` -> 40 frames across
sheets `Title/Electricity1.gif` (frames 0..30) + `Title/Electricity2.gif`
(frames 31..39)) into a single 4-bpp VDP1 sprite atlas file.

Per decomp `tools/_decomp_raw/SonicMania_Objects_Title_TitleSetup.c:393-402`
(`Draw_DrawRing`) the title scene draws the electricity animator twice
each tick (FLIP_NONE then FLIP_X) at world drawPos = (256, 108). The
animator advances per-tick during `State_AnimateUntilFlash:152-174` until
`frame_id == 31` (the decomp's threshold) triggers the flash transition.

Saturn-side VDP1 char-RAM budget audit:
    Pre-Phase-1.27 baseline: ~302 KB resident.
    Full 40 frames at avg ~150x150/2 = ~360 KB. OVER 391 KB cliff.
    Solution: cull to 8 keyframes evenly sampled across frames 0..30
    (skipping the degenerate frame 30, which the decomp .bin marks as
    0x0 dimensions and the parse output shows w=0/h=0). 8 keyframes at
    avg ~10 KB each = ~80 KB resident; combined with the title scene
    baseline ~382 KB total, still under cliff.

INPUTS
    extracted/Data/Sprites/Title/Electricity.bin   (RSDKv5 .bin)
    extracted/Data/Sprites/Title/Electricity1.gif   (sheet 0)
    extracted/Data/Sprites/Title/Electricity2.gif   (sheet 1)

OUTPUT
    cd/ELECTRA.ATL   single flat file consumed by src/mania/Objects/Title/TitleAssets.c

FORMAT v4 (big-endian throughout; matches TSONIC.ATL exactly)
    -- header (44 bytes) --
    u16  magic            0x5453 'TS'  (Title-Sprite; same family as TSONIC)
    u16  version          0x0004
    u16  anim_count                  (1 for electricity)
    u16  palette_size               (always 16)
    u16  palette[16]                BGR1555 MSB=1 (opaque); slot 0 = transparent
    u32  total_pixel_bytes          sum of culled frames' pixel pools

    -- per-anim record (8 bytes), 1 record --
    u16  frame_count                (8 keyframes after cull)
    u16  loop_index                 (last frame N means hold on N)
    u16  first_frame_global_idx     (0 — only one anim)
    u16  total_duration_ticks       (sum of dur across keyframes)

    -- global frame-record table (14 bytes/frame), 8 records --
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
    python tools/build_electricity_atlas.py
"""
import argparse
import os
import struct
import sys

import numpy as np
from PIL import Image

# Reuse parse_spr + load_sheet from convert_ring_sprite.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from convert_ring_sprite import parse_spr, load_sheet


MAGIC          = 0x5453   # 'TS' family (TitleSprite-style atlas)
VERSION        = 0x0004   # v4 (mirrors TSONIC.ATL loader)
PALETTE_SIZE   = 16
WIDTH_ALIGN    = 8        # VDP1 character width unit
ANIM_REC_FMT   = ">HHHH"  # frame_count, loop_index, first_frame_global_idx, total_dur
ANIM_REC_LEN   = 8
FRAME_REC_FMT  = ">HHhhHI"  # w, h, px, py, dur, pixel_offset
FRAME_REC_LEN  = 14

# Cull selection: 8 keyframes evenly sampled across the arc.
# Sheet 0 frames span 0..30 (30 = degenerate w=h=0).  Sheet 1 frames span
# 31..39 (the dissipation tail).  We pick from the BUILD-UP portion since
# the arc grows from a small fragment into a full electricity ring; the
# dissipation tail is barely visible because of frame_count<32 fallback
# auto-advancing past it.  Selected indices:
#     3   small-ish initial arc (sheet 0 frame 3 ~ 50x52)
#     7   first sheet0 row 2 frame ~ 75x83
#     11  ~ 77x145 — climbing arc
#     15  ~ 130x150 — full half-ring
#     19  ~ 154x156 — full electricity halo
#     23  ~ 148x167 — peak intensity
#     27  ~ 145x148 — late buildup
#     31  ~ 145x146 — first sheet1 frame (dissipation start, still strong)
KEYFRAME_INDICES = [3, 7, 11, 15, 19, 23, 27, 31]


def rgb_to_bgr1555(rgb):
    r5 = (rgb[..., 0].astype(np.uint16) >> 3) & 0x1F
    g5 = (rgb[..., 1].astype(np.uint16) >> 3) & 0x1F
    b5 = (rgb[..., 2].astype(np.uint16) >> 3) & 0x1F
    return np.uint16(0x8000) | (b5 << 10) | (g5 << 5) | r5


def kmeans_quantize(pixels, k, max_iter=64, seed=42):
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
    ap.add_argument("--electricity-bin",
                    default="extracted/Data/Sprites/Title/Electricity.bin")
    ap.add_argument("--sprite-dir", default="extracted/Data/Sprites")
    ap.add_argument("--out", default="cd/ELECTRA.ATL")
    args = ap.parse_args()

    sheets, anims = parse_spr(args.electricity_bin)
    if len(anims) == 0:
        sys.exit("FAIL: Electricity.bin has no animations")
    arc = anims[0]
    if arc["name"] != "Electricity":
        sys.exit(f"FAIL: anim 0 name {arc['name']!r} != 'Electricity'")

    print(f"[+] Anim 0 'Electricity': {len(arc['frames'])} total frames, "
          f"speed={arc['speed']}, loop={arc['loop']}")
    if len(sheets) < 2:
        sys.exit(f"FAIL: expected 2 sheets, got {len(sheets)}: {sheets}")
    print(f"[+] Sheets: {sheets}")

    sheet_imgs = {}
    for i, name in enumerate(sheets):
        sheet_imgs[i] = load_sheet(args.sprite_dir, name)
        print(f"    sheet {i} {name!r}: "
              f"{sheet_imgs[i].shape[1]}x{sheet_imgs[i].shape[0]} RGBA")

    # Pre-filter keyframes: skip any with w==0 or h==0 (degenerate placeholder).
    selected = []
    for kfi in KEYFRAME_INDICES:
        if kfi >= len(arc["frames"]):
            print(f"    [!] keyframe index {kfi} >= frame count {len(arc['frames'])}, skip")
            continue
        f = arc["frames"][kfi]
        sheet_id, sx, sy, w, h, px, py, dur = f
        if w == 0 or h == 0:
            print(f"    [!] keyframe {kfi} degenerate w={w} h={h}, skip")
            continue
        selected.append((kfi, f))
    if not selected:
        sys.exit("FAIL: no usable keyframes")
    print(f"[+] Culled to {len(selected)} keyframes: "
          f"{[kfi for kfi, _ in selected]}")

    # Pass 1: collect opaque RGB pixels across culled frames for palette build.
    opaque_rgb = []
    for kfi, (sheet_id, sx, sy, w, h, px, py, dur) in selected:
        crop = sheet_imgs[sheet_id][sy:sy+h, sx:sx+w]
        mask = crop[..., 3] > 0
        opaque_rgb.append(crop[mask][..., :3])
    pix_pool_rgb = np.concatenate(opaque_rgb, axis=0)
    print(f"[+] Total opaque pixels across keyframes: {pix_pool_rgb.shape[0]:,}")

    fg_pal, _ = kmeans_quantize(pix_pool_rgb, k=15, max_iter=80)
    palette = np.zeros((16, 3), np.uint8)
    palette[0] = (255, 0, 255)   # transparent placeholder
    palette[1:16] = fg_pal
    print("[+] Built 16-color shared palette")

    # Pass 2: convert each keyframe into nibble-packed 4-bpp pixels.
    pal_rgb_f = palette.astype(np.float32)
    pool = bytearray()
    frame_records = []
    total_dur = 0

    for fi_local, (kfi, (sheet_id, sx, sy, w, h, px, py, dur)) in enumerate(selected):
        crop = sheet_imgs[sheet_id][sy:sy+h, sx:sx+w]
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
        frame_records.append((w_padded, h, px, py, dur, pix_offset))
        total_dur += dur
        print(f"    kf {fi_local} (atlas idx {kfi:2d}): {w}x{h} -> "
              f"{w_padded}x{h}, {len(packed_bytes):5d} B, "
              f"pivot=({px},{py}), dur={dur}")

    total_pix = len(pool)
    print(f"[+] Pixel pool: {total_pix:,} bytes")

    pal_bgr1555 = rgb_to_bgr1555(palette)
    pal_bgr1555[0] = 0x0000

    # Pack header (44 bytes).
    anim_count = 1
    header = struct.pack(
        ">HHHH" + "H"*PALETTE_SIZE + "I",
        MAGIC, VERSION,
        anim_count,
        PALETTE_SIZE,
        *(int(c) for c in pal_bgr1555),
        int(total_pix)
    )

    # Anim record: frame_count, loop_index, first_global_idx, total_dur.
    # loop_index = frame_count - 1 means "hold on the last frame after loop",
    # but the Saturn-side state machine never loops past frame_count -- the
    # frame_count<32 fallback at TitleSetup.c:170-175 triggers the flash
    # transition.  So set loop_index = frame_count - 1 (hold last) and let
    # the fallback drive state.
    frame_count = len(frame_records)
    loop_index = frame_count - 1
    anim_block = struct.pack(ANIM_REC_FMT,
                              frame_count, loop_index, 0, total_dur)

    fr_block = b"".join(
        struct.pack(FRAME_REC_FMT,
                    int(w), int(h), int(px), int(py), int(dur), int(off))
        for (w, h, px, py, dur, off) in frame_records
    )

    out = header + anim_block + fr_block + bytes(pool)
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(out)

    vdp1_user_area = 0x71D38
    print(f"[+] Output {args.out}: {len(out):,} bytes "
          f"(header=44, anim=8, frames={len(fr_block)} ({frame_count} records), "
          f"pixel_pool={total_pix})")
    print(f"[+] VDP1 atlas footprint at boot: {total_pix:,} bytes of "
          f"{vdp1_user_area:,} ({100.0*total_pix/vdp1_user_area:.1f}%)")
    if total_pix >= vdp1_user_area:
        sys.exit(f"FAIL: VDP1 atlas {total_pix} >= JO_VDP1_USER_AREA_SIZE {vdp1_user_area}")


if __name__ == "__main__":
    main()
