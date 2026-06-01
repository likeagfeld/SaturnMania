#!/usr/bin/env python3
"""audit_finger_pivot.py - Phase 1.27b quantitative audit.

Per CLAUDE.md §4.5.1 Audit 3 (pivot+flip composite math): before writing
the fix at src/mania/Game.c:1683-1691, measure the per-frame drift
introduced by using the asset-level (frozen frame-0) pivot/width for all
12 finger frames instead of the per-frame metadata.

INPUT
    extracted/Data/Sprites/Title/Sonic.bin  (RSDK sprite-animation .bin)

The .bin contains:
    anim 0 'Sonic'          49 frames (body)
    anim 1 'Finger Wave'    12 frames (overlay; loops while body holds on
                                       its final frame)

WHAT WE COMPUTE PER FINGER FRAME
    (1) per-frame width, height, pivot_x, pivot_y, duration (from the .bin)
    (2) DECOMP-canonical world top-left, FLIP_NONE:
            world_topleft_x = entity_x + pivot_x
        (RSDK convention is positive-add: the RSDK Drawing.cpp DrawSprite
        path adds the pivot to entity_pos to get the top-left because the
        atlas builder stores pivots as the negative of the inside-canvas
        offset. See `tools/convert_anim_sprite.py` + Game.c:1620-1626 which
        composes `entity_x + pivot_x + (width>>1)` as the canvas centre.)
    (4) CURRENT (broken) Saturn formula at Game.c:1685-1691, which uses
        ASSET-LEVEL pivot/width (= the metadata cached at TitleAssets.c:
        524-533 = g_tsonic_frames[anim1_first_frame], i.e. finger frame 0).
        We extract this by computing the formula with frame-0's metadata
        regardless of which frame is being drawn.
    (5) CORRECT Saturn formula: same math but with the active frame's
        per-frame metadata from g_tsonic_frames[anim1_first + finger_local].

OUTPUT
    Console table + JSON to tools/audit_finger_pivot_report.json with
    per-frame x/y centroid coords for the body's settled keyframe 7 and
    each finger frame, plus the worst-case horizontal drift (broken vs
    correct) in pixels.

USAGE
    python tools/audit_finger_pivot.py
"""
from __future__ import annotations

import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

sys.path.insert(0, HERE)
from convert_ring_sprite import parse_spr  # noqa: E402


# Entity world position from docs/title_ground_truth.md (TitleSonic entity
# parsed out of Title/Scene1.bin). Mirrors Game.c:1620 (world_x=252, world_y=104).
ENTITY_X = 252
ENTITY_Y = 104

# Body keyframe-7 = atlas frame 48 (settled iconic pose), the body frame
# the finger overlay is composited against once the body walker latches.
# Matches s_body_kf_offsets[7] at Game.c:1615.
BODY_SETTLED_KF_ATLAS_INDEX = 48


def fmt_int(n: int, width: int = 5) -> str:
    return f"{n:>{width}d}"


def main() -> int:
    bin_path = os.path.join(ROOT, "extracted", "Data", "Sprites", "Title", "Sonic.bin")
    if not os.path.exists(bin_path):
        print(f"FATAL: missing {bin_path}", file=sys.stderr)
        return 2

    sheets, anims = parse_spr(bin_path)
    if len(anims) < 2:
        print(f"FATAL: expected >= 2 anims in {bin_path}, got {len(anims)}",
              file=sys.stderr)
        return 2

    body = anims[0]
    finger = anims[1]

    if len(body["frames"]) <= BODY_SETTLED_KF_ATLAS_INDEX:
        print(f"FATAL: body anim has {len(body['frames'])} frames, "
              f"need at least {BODY_SETTLED_KF_ATLAS_INDEX + 1}",
              file=sys.stderr)
        return 2

    body_frame = body["frames"][BODY_SETTLED_KF_ATLAS_INDEX]
    # parse_spr() returns tuples shaped: (sheet_id, sx, sy, w, h, px, py, dur)
    _bsh, _bsx, _bsy, bw, bh, bpx, bpy, _bdur = body_frame

    # Body settled-frame centroid (FLIP_NONE):
    #   world_topleft = entity + pivot
    #   centroid     = world_topleft + (w/2, h/2)
    body_cx_noflip = ENTITY_X + bpx + (bw >> 1)
    body_cy        = ENTITY_Y + bpy + (bh >> 1)
    # FLIP_X mirrors about entity_x: cx_flipped = 2*entity_x - cx_noflip
    body_cx_flip = 2 * ENTITY_X - body_cx_noflip

    # The broken Saturn formula uses FRAME-0 of the finger anim as the
    # "asset-level" cached pivot/width/height (per TitleAssets.c:524-533).
    finger_frame0 = finger["frames"][0]
    _f0sh, _f0sx, _f0sy, f0w, f0h, f0px, f0py, _f0dur = finger_frame0

    rows = []
    worst_drift_x = 0
    worst_drift_y = 0
    worst_frame = -1

    for fi, frame_tuple in enumerate(finger["frames"]):
        _sh, _sx, _sy, w, h, px, py, dur = frame_tuple

        # CORRECT formula (per-frame): canvas centre with FLIP_NONE
        correct_cx_noflip = ENTITY_X + px + (w >> 1)
        correct_cy        = ENTITY_Y + py + (h >> 1)
        # FLIP_X mirror about entity origin (matches Game.c:1632 + 1687).
        correct_cx_flip = 2 * ENTITY_X - correct_cx_noflip

        # BROKEN formula (asset-level, uses frame-0 pivot/width for all 12):
        broken_cx_noflip = ENTITY_X + f0px + (f0w >> 1)
        broken_cy        = ENTITY_Y + f0py + (f0h >> 1)
        broken_cx_flip   = 2 * ENTITY_X - broken_cx_noflip

        # Horizontal drift, absolute, in the FLIPPED domain (which is what
        # actually lands on screen, given the title build uses direction=1):
        drift_x = abs(correct_cx_flip - broken_cx_flip)
        drift_y = abs(correct_cy        - broken_cy)
        if drift_x > worst_drift_x:
            worst_drift_x = drift_x
            worst_frame = fi
        if drift_y > worst_drift_y:
            worst_drift_y = drift_y

        # Relative to the body's settled centroid (horizontal offset of
        # the finger centre from Sonic's iconic-pose centre):
        rel_correct = correct_cx_flip - body_cx_flip
        rel_broken  = broken_cx_flip  - body_cx_flip

        rows.append({
            "frame": fi,
            "w": w, "h": h,
            "pivot_x": px, "pivot_y": py,
            "duration": dur,
            "correct_cx_flip": correct_cx_flip,
            "broken_cx_flip":  broken_cx_flip,
            "correct_cy":      correct_cy,
            "broken_cy":       broken_cy,
            "drift_x_px":      drift_x,
            "drift_y_px":      drift_y,
            "rel_to_body_cx_correct": rel_correct,
            "rel_to_body_cx_broken":  rel_broken,
        })

    # ----- Console table -----
    print("Phase 1.27b: TitleSonic finger pivot audit "
          "(per CLAUDE.md §4.5.1 Audit 3)")
    print(f"Source:  {os.path.relpath(bin_path, ROOT)}")
    print(f"Entity:  x={ENTITY_X}, y={ENTITY_Y}  "
          f"(from docs/title_ground_truth.md)")
    print(f"Body settled (kf=7, atlas frame {BODY_SETTLED_KF_ATLAS_INDEX}): "
          f"w={bw} h={bh} px={bpx} py={bpy}")
    print(f"  centroid FLIP_NONE: ({body_cx_noflip}, {body_cy})")
    print(f"  centroid FLIP_X   : ({body_cx_flip}, {body_cy})")
    print(f"Finger frame 0 (cached as 'asset-level' at TitleAssets.c:524-533):")
    print(f"  w={f0w} h={f0h} px={f0px} py={f0py}")
    print()
    print("Per-finger-frame metadata + composite math:")
    hdr = (f"{'fr':>3} {'w':>4} {'h':>4} {'px':>5} {'py':>5} {'dur':>4} "
           f"| {'correct_cx':>11} {'broken_cx':>10} {'dx':>5} "
           f"| {'correct_cy':>11} {'broken_cy':>10} {'dy':>5} "
           f"| {'rel(C)':>7} {'rel(B)':>7}")
    print(hdr)
    print("-" * len(hdr))
    for r in rows:
        print(
            f"{r['frame']:>3} {r['w']:>4} {r['h']:>4} "
            f"{r['pivot_x']:>5} {r['pivot_y']:>5} {r['duration']:>4} "
            f"| {r['correct_cx_flip']:>11} {r['broken_cx_flip']:>10} "
            f"{r['drift_x_px']:>5} "
            f"| {r['correct_cy']:>11} {r['broken_cy']:>10} "
            f"{r['drift_y_px']:>5} "
            f"| {r['rel_to_body_cx_correct']:>7} {r['rel_to_body_cx_broken']:>7}"
        )
    print()
    print(f"WORST horizontal drift:  {worst_drift_x} px  "
          f"(at finger frame {worst_frame})")
    print(f"WORST vertical drift:    {worst_drift_y} px")

    # Verdict — if all per-frame pivots are identical to frame-0, the bug
    # isn't pivot-related. Cite this verdict so the caller doesn't ship a
    # mock fix.
    all_identical = all(
        r["pivot_x"] == f0px and r["pivot_y"] == f0py and
        r["w"] == f0w and r["h"] == f0h
        for r in rows
    )
    if all_identical:
        print()
        print("VERDICT: all 12 finger frames have IDENTICAL pivot+size as "
              "frame 0. The asset-level lookup at Game.c:1683-1691 is "
              "mathematically equivalent to a per-frame lookup; the "
              "user-reported placement bug must originate ELSEWHERE.")
    else:
        print()
        print("VERDICT: per-frame pivot/size DIFFERS from frame 0 -> the "
              "asset-level lookup at Game.c:1683-1691 produces drift; the "
              "fix is to switch to per-frame metadata.")

    # ----- JSON sidecar -----
    out_path = os.path.join(HERE, "audit_finger_pivot_report.json")
    with open(out_path, "w") as f:
        json.dump({
            "entity": {"x": ENTITY_X, "y": ENTITY_Y},
            "body_settled": {
                "atlas_index": BODY_SETTLED_KF_ATLAS_INDEX,
                "w": bw, "h": bh, "pivot_x": bpx, "pivot_y": bpy,
                "cx_flip_none": body_cx_noflip,
                "cy":           body_cy,
                "cx_flip_x":    body_cx_flip,
            },
            "finger_frame0_cached": {
                "w": f0w, "h": f0h, "pivot_x": f0px, "pivot_y": f0py,
            },
            "per_finger_frame": rows,
            "worst_drift_x_px":     worst_drift_x,
            "worst_drift_x_frame":  worst_frame,
            "worst_drift_y_px":     worst_drift_y,
            "all_frames_identical_to_frame0": all_identical,
        }, f, indent=2)
    print()
    print(f"JSON report -> {os.path.relpath(out_path, ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
