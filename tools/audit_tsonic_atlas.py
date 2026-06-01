#!/usr/bin/env python3
"""audit_tsonic_atlas.py - Quantitative pre-strip audit of cd/TSONIC.ATL.

Phase 1.29a (Task #101) - read the existing TSONIC.ATL v4 file and emit a
table that quantifies dead weight in anim 0 ('Sonic' 49-frame entrance
arc) given that the Saturn runtime only consumes 8 keyframes per
`src/mania/Objects/Title/TitleAssets.c:396` keyframe offset table
`s_tsonic_keyframe_offsets[8] = {0, 6, 12, 18, 24, 30, 36, 48}`.

Also tabulates anim 1 ('Finger Wave' 12-frame loop) which is consumed
in full and contributes zero dead weight.

The selective rebuild target is:
  - anim 0: 8 frames (renumbered 0..7 in the output atlas)
  - anim 1: 12 frames (unchanged, contiguous 0..11)
  - total = 20 frame records
  - file size projection: header(44) + anim records(2*8=16) + frame records
    (20*14=280) + (anim 0 used pixel bytes) + (anim 1 pixel bytes)

OUTPUTS
  stdout : human-readable table
  tools/audit_tsonic_atlas_report.json : machine-readable record

USAGE
  python tools/audit_tsonic_atlas.py
  python tools/audit_tsonic_atlas.py --atl cd/TSONIC.ATL
  python tools/audit_tsonic_atlas.py --atl cd/TSONIC.ATL --json myreport.json
"""
import argparse
import json
import os
import struct
import sys


# Runtime-defined keyframe set (see TitleAssets.c:396)
KEYFRAME_OFFSETS = [0, 6, 12, 18, 24, 30, 36, 48]

HEADER_FMT = ">HHHH" + "H" * 16 + "I"
HEADER_LEN = struct.calcsize(HEADER_FMT)   # 2*4 + 2*16 + 4 = 44
ANIM_REC_FMT = ">HHHH"
ANIM_REC_LEN = struct.calcsize(ANIM_REC_FMT)  # 8
FRAME_REC_FMT = ">HHhhHI"
FRAME_REC_LEN = struct.calcsize(FRAME_REC_FMT)  # 14


def parse_atlas(path):
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < HEADER_LEN:
        sys.exit(f"FAIL: {path} too small ({len(data)} bytes)")
    hdr = struct.unpack_from(HEADER_FMT, data, 0)
    magic, version, anim_count, pal_size = hdr[0], hdr[1], hdr[2], hdr[3]
    pal = hdr[4:4 + 16]
    total_pix = hdr[20]
    if magic != 0x5453:
        sys.exit(f"FAIL: wrong magic 0x{magic:04X} (expected 0x5453 'TS')")
    if version != 0x0004:
        sys.exit(f"FAIL: wrong version 0x{version:04X} (expected 0x0004)")
    if pal_size != 16:
        sys.exit(f"FAIL: wrong palette_size {pal_size} (expected 16)")

    anims = []
    for ai in range(anim_count):
        off = HEADER_LEN + ai * ANIM_REC_LEN
        rec = struct.unpack_from(ANIM_REC_FMT, data, off)
        anims.append({
            "frame_count":   rec[0],
            "loop_index":    rec[1],
            "first_global":  rec[2],
            "total_dur":     rec[3],
        })

    total_frame_count = sum(a["frame_count"] for a in anims)
    frame_table_off = HEADER_LEN + anim_count * ANIM_REC_LEN
    pool_base_off   = frame_table_off + total_frame_count * FRAME_REC_LEN

    frames = []
    for i in range(total_frame_count):
        off = frame_table_off + i * FRAME_REC_LEN
        w, h, px, py, dur, pool_off = struct.unpack_from(FRAME_REC_FMT, data, off)
        pixel_bytes = (w * h) // 2  # 4-bpp nibble-packed
        frames.append({
            "idx":          i,
            "width":        w,
            "height":       h,
            "pivot_x":      px,
            "pivot_y":      py,
            "duration":     dur,
            "pool_off":     pool_off,
            "pixel_bytes":  pixel_bytes,
        })

    return {
        "file_size":      len(data),
        "magic":          magic,
        "version":        version,
        "anim_count":     anim_count,
        "pal_size":       pal_size,
        "total_pix":      total_pix,
        "anims":          anims,
        "frames":         frames,
        "pool_base_off":  pool_base_off,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--atl", default="cd/TSONIC.ATL")
    ap.add_argument("--json", default="tools/audit_tsonic_atlas_report.json")
    args = ap.parse_args()

    if not os.path.isfile(args.atl):
        sys.exit(f"FAIL: atlas not found: {args.atl}")

    atlas = parse_atlas(args.atl)

    # Identify anim 0 vs anim 1 ranges.
    if atlas["anim_count"] < 2:
        sys.exit(f"FAIL: atlas anim_count={atlas['anim_count']} (expected >=2)")
    anim0 = atlas["anims"][0]
    anim1 = atlas["anims"][1]

    anim0_first = anim0["first_global"]
    anim0_count = anim0["frame_count"]
    anim1_first = anim1["first_global"]
    anim1_count = anim1["frame_count"]

    anim0_frames = atlas["frames"][anim0_first:anim0_first + anim0_count]
    anim1_frames = atlas["frames"][anim1_first:anim1_first + anim1_count]

    anim0_pixel_total = sum(f["pixel_bytes"] for f in anim0_frames)
    anim1_pixel_total = sum(f["pixel_bytes"] for f in anim1_frames)

    used_indices = [i for i in KEYFRAME_OFFSETS if i < anim0_count]
    anim0_used_bytes = sum(anim0_frames[i]["pixel_bytes"] for i in used_indices)
    anim0_dead_bytes = anim0_pixel_total - anim0_used_bytes

    # Projected stripped layout.
    new_anim0_count = len(used_indices)         # expect 8
    new_anim1_count = anim1_count                # expect 12
    new_total_frames = new_anim0_count + new_anim1_count  # expect 20
    new_anim_recs = 2 * ANIM_REC_LEN
    new_frame_recs = new_total_frames * FRAME_REC_LEN
    new_pix_pool = anim0_used_bytes + anim1_pixel_total
    projected_bytes = HEADER_LEN + new_anim_recs + new_frame_recs + new_pix_pool

    savings = atlas["file_size"] - projected_bytes

    # Human-readable table.
    print(f"=== TSONIC.ATL audit ({args.atl}) ===")
    print(f"version            : 0x{atlas['version']:04X}")
    print(f"anim_count         : {atlas['anim_count']}")
    print(f"current file size  : {atlas['file_size']:>9,d} B")
    print(f"pixel-pool total   : {atlas['total_pix']:>9,d} B")
    print()
    print(f"-- anim 0 (Sonic, body) --")
    print(f"  frame_count      : {anim0_count}")
    print(f"  pixel total      : {anim0_pixel_total:>9,d} B")
    print(f"  used keyframes   : {used_indices}")
    print(f"  used pixel bytes : {anim0_used_bytes:>9,d} B")
    print(f"  dead pixel bytes : {anim0_dead_bytes:>9,d} B  "
          f"({100.0 * anim0_dead_bytes / anim0_pixel_total:.1f}% dead)")
    print()
    print(f"-- anim 1 (Finger Wave) --")
    print(f"  frame_count      : {anim1_count}")
    print(f"  pixel total      : {anim1_pixel_total:>9,d} B  (all used)")
    print()
    print(f"-- projected stripped atlas --")
    print(f"  anim 0 frames    : {new_anim0_count} (renumbered 0..{new_anim0_count - 1})")
    print(f"  anim 1 frames    : {new_anim1_count}")
    print(f"  total frame recs : {new_total_frames}")
    print(f"  header           : {HEADER_LEN} B")
    print(f"  anim records     : {new_anim_recs} B")
    print(f"  frame records    : {new_frame_recs} B")
    print(f"  pixel pool       : {new_pix_pool:>9,d} B")
    print(f"  projected size   : {projected_bytes:>9,d} B  "
          f"({projected_bytes / 1024:.1f} KB)")
    print(f"  savings vs now   : {savings:>9,d} B  "
          f"({100.0 * savings / atlas['file_size']:.1f}%)")
    print()
    print(f"  per-keyframe breakdown:")
    for ki, src_idx in enumerate(used_indices):
        f = anim0_frames[src_idx]
        print(f"    out-{ki} <- src-{src_idx:2d}  {f['width']}x{f['height']}  "
              f"{f['pixel_bytes']:>6,d} B  pivot=({f['pivot_x']},{f['pivot_y']})  "
              f"dur={f['duration']}")

    report = {
        "atlas_path":              args.atl,
        "current_atlas_bytes":     atlas["file_size"],
        "anim0_frame_count":       anim0_count,
        "anim0_pixel_bytes_total": anim0_pixel_total,
        "anim0_used_keyframes":    used_indices,
        "anim0_used_bytes":        anim0_used_bytes,
        "anim0_dead_bytes":        anim0_dead_bytes,
        "anim1_frame_count":       anim1_count,
        "anim1_pixel_bytes_total": anim1_pixel_total,
        "projected_stripped_bytes": projected_bytes,
        "projected_savings_bytes": savings,
    }
    os.makedirs(os.path.dirname(args.json) or ".", exist_ok=True)
    with open(args.json, "w") as f:
        json.dump(report, f, indent=2)
    print(f"\n[+] report written: {args.json}")


if __name__ == "__main__":
    main()
