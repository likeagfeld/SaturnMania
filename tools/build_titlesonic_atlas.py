#!/usr/bin/env python3
"""build_titlesonic_atlas.py - Pack ALL TitleSonic animations (anim 0 'Sonic'
49-frame entrance arc + anim 1 'Finger Wave' 12-frame loop) into a single
4-bpp VDP1 sprite atlas file.

Per tools/_decomp_raw/SonicMania_Objects_Title_TitleSonic.c:12-37 the
TitleSonic entity is a COMPOUND sprite: the body anim plays once and holds
on frame 48, at which point the Finger Wave anim STARTS processing and
loops forever as an overlay drawn at the same entity origin. Both
animators must be loaded into VDP1 VRAM up-front.

INPUTS
    extracted/Data/Sprites/Title/Sonic.bin   (RSDKv5 sprite-animation .bin)
    extracted/Data/Sprites/Title/Sonic.gif   (palettized RSDK atlas)

OUTPUT
    cd/TSONIC.ATL   single flat file consumed by src/title_sonic.c

FORMAT v4 (big-endian throughout; bumped from v3 to add anim_count)
    -- header (12 + 32 + 4 = 48 bytes) --
    u16  magic            0x5453 'TS'
    u16  version          0x0004
    u16  anim_count                  (2: body + finger wave)
    u16  palette_size               (always 16)
    u16  palette[16]                BGR1555 MSB=1 (opaque); slot 0 = transparent
    u32  total_pixel_bytes          sum across both anims' pixel pools

    -- per-anim record (8 bytes), anim_count records --
    u16  frame_count
    u16  loop_index                 RSDK loop target (last frame N means hold
                                    on frame N; in our impl with frame_count
                                    sprites, frame N is the final played frame)
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

DESIGN NOTES (verbatim from decomp at
tools/_decomp_raw/SonicMania_Objects_Title_TitleSonic.c)

* `RSDK.SetSpriteAnimation(aniFrames, 0, &animatorSonic, true, 0)` at line 43
  configures the body animator on anim 0; `RSDK.SetSpriteAnimation(...,
  1, &animatorFinger, ...)` configures the finger animator on anim 1.
* Update() at lines 12-20: body anim processes every frame; finger anim
  only processes when body is on its last frame (frame 48).
* Draw() at lines 26-37: body drawn always (clipped to Y < 160); finger
  drawn only when body is on last frame, with no Y clip.
* The palette is shared between both anims (same Sonic.gif sheet pixels).

USAGE
    python tools/build_titlesonic_atlas.py
"""
import argparse
import os
import struct
import sys

import numpy as np
from PIL import Image

# Reuse the existing RSDK .bin parser.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from convert_ring_sprite import parse_spr, load_sheet


MAGIC          = 0x5453   # 'TS'
VERSION        = 0x0004   # v4: per-anim header, 2 anims (body + finger wave)
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
    ap.add_argument("--sonic-bin",
                    default="extracted/Data/Sprites/Title/Sonic.bin")
    ap.add_argument("--sprite-dir", default="extracted/Data/Sprites")
    ap.add_argument("--out", default="cd/TSONIC.ATL")
    ap.add_argument("--selective", action="store_true",
                    help="Phase 1.29a (Task #101): emit ONLY the 8 body "
                         "keyframes {0,6,12,18,24,30,36,48} + 12 finger "
                         "frames. anim 0 frame_count becomes 8, renumbered "
                         "0..7 in the output atlas. The runtime indexes by "
                         "local-frame so g_tsonic_anim0_first_frame and "
                         "g_tsonic_anim1_first_frame absorb the renumber.")
    args = ap.parse_args()

    # Phase 1.29a (Task #101): selective keyframe set.
    # Matches `src/mania/Objects/Title/TitleAssets.c:396`
    #   `s_tsonic_keyframe_offsets[8] = {0, 6, 12, 18, 24, 30, 36, 48}`
    # and Game.c:1615 `s_body_kf_offsets[8] = {0, 6, 12, 18, 24, 30, 36, 48}`.
    # Any change here must be mirrored in both runtime sites or the body
    # walker indexes into the wrong source frame's pivot.
    SELECTIVE_KEYFRAMES_ANIM0 = [0, 6, 12, 18, 24, 30, 36, 48]

    sheets, anims = parse_spr(args.sonic_bin)
    # We need BOTH anim 0 ('Sonic') and anim 1 ('Finger Wave') per the decomp.
    # Hard-fail if either is missing.
    anim_specs = [
        {"index": 0, "name": "Sonic"},
        {"index": 1, "name": "Finger Wave"},
    ]
    selected_anims = []
    for spec in anim_specs:
        if spec["index"] >= len(anims):
            sys.exit(f"FAIL: Sonic.bin missing anim {spec['index']!r} ({spec['name']!r})")
        a = anims[spec["index"]]
        if a["name"] != spec["name"]:
            sys.exit(f"FAIL: anim {spec['index']} name {a['name']!r} != {spec['name']!r}")

        # Phase 1.29a: for anim 0, replace the 49-frame source list with the
        # 8 selective keyframes. Pivot/width/height/duration come from the
        # source-frame index (so output frame i has the metadata of source
        # frame s_tsonic_keyframe_offsets[i]). loop_index also gets
        # renumbered so the decomp's "loop on frame 48" becomes "loop on
        # output index 7".
        if args.selective and spec["index"] == 0:
            kept = []
            for src_idx in SELECTIVE_KEYFRAMES_ANIM0:
                if src_idx >= len(a["frames"]):
                    sys.exit(f"FAIL: selective wants src-frame {src_idx} but "
                             f"anim 0 only has {len(a['frames'])} frames")
                kept.append(a["frames"][src_idx])
            a = {
                "name":     a["name"],
                "speed":    a.get("speed", 1),
                # Decomp loop_index = 48 (last frame). Selective renumbers
                # that to 7 (last output frame).
                "loop":     len(kept) - 1,
                "rotation": a.get("rotation", 0),
                "frames":   kept,
            }
            print(f"[+] Anim {spec['index']} {spec['name']!r}: SELECTIVE "
                  f"{len(SELECTIVE_KEYFRAMES_ANIM0)} keyframes "
                  f"{SELECTIVE_KEYFRAMES_ANIM0} -> renumbered 0..{len(kept) - 1}, "
                  f"loop={a['loop']}")
        else:
            print(f"[+] Anim {spec['index']} {a['name']!r}: {len(a['frames'])} frames, "
                  f"loop={a.get('loop')}")
        selected_anims.append(a)

    assert len(sheets) >= 1, "no sheet in .bin"
    sheet = load_sheet(args.sprite_dir, sheets[0])
    print(f"[+] Sheet {sheets[0]!r}: {sheet.shape[1]}x{sheet.shape[0]} RGBA")

    # ---- Pass 1: collect opaque RGB pixels across ALL anims' frames
    opaque_rgb = []
    for a in selected_anims:
        for (sid, sx, sy, w, h, px, py, dur) in a["frames"]:
            crop = sheet[sy:sy+h, sx:sx+w]
            mask = crop[..., 3] > 0
            opaque_rgb.append(crop[mask][..., :3])
    pix_pool = np.concatenate(opaque_rgb, axis=0)
    print(f"[+] Total opaque pixels across all anims: {pix_pool.shape[0]:,}")

    fg_pal, _ = kmeans_quantize(pix_pool, k=15, max_iter=80)
    palette = np.zeros((16, 3), np.uint8)
    palette[0] = (255, 0, 255)
    palette[1:16] = fg_pal
    print("[+] Built 16-color shared palette")

    # ---- Pass 2: convert each frame across both anims into nibble-packed pixels
    pal_rgb_f = palette.astype(np.float32)
    pool = bytearray()
    all_frame_records = []      # list of (w_padded, h, px, py, dur, pix_offset)
    anim_records = []            # list of (frame_count, loop_index, first_global_idx, total_dur)

    global_frame_cursor = 0
    for ai, a in enumerate(selected_anims):
        first_global_idx = global_frame_cursor
        anim_frame_count = len(a["frames"])
        anim_loop_index = a.get("loop", 0) if a.get("loop") is not None else 0
        anim_total_dur = 0

        for fidx, (sid, sx, sy, w, h, px, py, dur) in enumerate(a["frames"]):
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

            if fidx < 2 or fidx == len(a["frames"]) - 1:
                print(f"    anim{ai} frame {fidx:2d}: {w}x{h} -> {w_padded}x{h}, "
                      f"{len(packed_bytes):5d} B, pivot=({px},{py}), dur={dur}")

        anim_records.append((anim_frame_count, anim_loop_index,
                             first_global_idx, anim_total_dur))
        print(f"    anim{ai} total: {anim_total_dur} ticks")

    total_pix = len(pool)
    print(f"[+] Pixel pool: {total_pix:,} bytes")

    pal_bgr1555 = rgb_to_bgr1555(palette)
    pal_bgr1555[0] = 0x0000

    # ---- Pack header (44 bytes) - reusing the v3 layout for binary compat
    # of the magic/version/palette area; the difference is what FOLLOWS:
    # v3 had frame records immediately; v4 has anim_count records then frames.
    # We patch the second u16 to anim_count (was frame_count in v3); the
    # frame_count is now derivable from sum of anim_records.
    total_frame_count = sum(rec[0] for rec in anim_records)
    header = struct.pack(
        ">HHHH" + "H"*PALETTE_SIZE + "I",
        MAGIC, VERSION,
        len(anim_records),     # u16 anim_count (was frame_count in v3)
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

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "wb") as f:
        f.write(out)

    vdp1_user_area = 0x71D38
    print(f"[+] Output {args.out}: {len(out):,} bytes "
          f"(header={ATL_HEADER_LEN}, anims={len(anim_block)}, "
          f"frames={len(fr_block)} ({total_frame_count} records), "
          f"pixel_pool={total_pix})")
    print(f"[+] VDP1 atlas footprint at boot: {total_pix:,} bytes of "
          f"{vdp1_user_area:,} ({100.0*total_pix/vdp1_user_area:.1f}%)")
    if total_pix >= vdp1_user_area:
        sys.exit(f"FAIL: VDP1 atlas {total_pix} >= JO_VDP1_USER_AREA_SIZE {vdp1_user_area}")


if __name__ == "__main__":
    main()
