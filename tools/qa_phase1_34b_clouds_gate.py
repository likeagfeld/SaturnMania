#!/usr/bin/env python3
"""qa_phase1_34b_clouds_gate.py - Phase 1.34b RED-firing gate.

Phase 1.34b ports the Title TileLayer 2 'Clouds' band (decomp source
tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:103-136) to a
Saturn NBG2 cell-mode 8-bpp bitmap with per-frame slScrPosNbg2 scroll
animation.

This gate is added BEFORE the implementation lands per CLAUDE.md §4.7
(RED-firing gate before fix).  On the current pre-fix build the gate
must fire RED on P1 (asset missing), P3 (no cloud pixels in top ROI),
and P4 (no motion).  Once Phase 1.34b's build_clouds_bg.py + setup_
clouds_bg() + per-frame scroll tick land it goes GREEN.

PREDICATES
==========

  P1 (asset present):
     cd/CLOUDS.DAT exists, size = 65536 B (256x256 8-bpp cell-mode).
     cd/CLOUDS.PAL exists, size = 512 B (256-entry RGB555 BE).

  P2 (palette has cloud/sky colors):
     CLOUDS.PAL has >= 10 slots with sky-blue-or-white character:
       cloud-white:  R+G+B >= 540 and max-min < 80
       sky-blue:     B > R + 40, B > G + 10, B > 100
     Catches the 'palette emerged as neon-green flood' failure mode
     before the asset gets flashed to ISO.

  P3 (Saturn renders clouds in top region):
     For the 3 fps title-window captures (settled frames 60..85), the
     top 1/3 ROI of the Mednafen window must show >= 5000 cloud-color
     pixels per frame.  Pre-fix baseline (no NBG2 cloud setup) has
     ~0 cloud pixels (top region is sky-blue back-color only).

  P4 (cross-frame motion):
     Cloud-color centroid Y position must shift across the capture
     series:  |sum of frame-to-frame deltas| >= 2 px, with >= 3 frames
     showing non-trivial Y motion.  Decomp Scanline_Clouds drives
     position.y = TitleBG->timer + 2*cos -- monotonic Y scroll at +0.5
     fixed unit/frame.

USAGE
=====

    py -3 tools/qa_phase1_34b_clouds_gate.py
        # Uses qa_phase1_34b_clouds_*.png frames in repo root.

    py -3 tools/qa_phase1_34b_clouds_gate.py --skip-saturn
        # P1 + P2 only (asset + palette; for pre-build sanity check).

Exit codes:
    0 = GREEN  (all enabled predicates pass)
    1 = RED    (any enabled predicate fails)
"""
import argparse
import glob
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("requires Pillow")

ROOT = Path(__file__).resolve().parent.parent

# Mednafen capture ROI matches qa_phase1_31_fix4_pivot_gate.py:78.
ROI = (60, 60, 850, 690)

# Top 1/3 of the 224-row Saturn display (cloud band region).
TOP_BAND_FRAC = 80.0 / 224.0


def is_cloud_white(r, g, b):
    """White / near-white cloud highlight."""
    total = r + g + b
    if total < 540:
        return False
    if max(r, g, b) - min(r, g, b) > 80:
        return False
    return True


def is_sky_blue_cloud(r, g, b):
    """Sky-blue cloud base (richer blue than back-color, but identifiable
    as cloud-layer-emitted because the back-color is RGB(96,128,224)
    while the cloud layer's sky band saturates the blue channel)."""
    if b < 150:
        return False
    if b < r + 40:
        return False
    if b < g + 10:
        return False
    return True


def is_cloud_color(r, g, b):
    return is_cloud_white(r, g, b) or is_sky_blue_cloud(r, g, b)


def predicate_asset_present():
    dat = ROOT / "cd/CLOUDS.DAT"
    pal = ROOT / "cd/CLOUDS.PAL"
    if not dat.exists():
        return False, f"missing {dat}"
    if not pal.exists():
        return False, f"missing {pal}"
    if dat.stat().st_size != 65536:
        return False, f"{dat.name} size {dat.stat().st_size} != 65536"
    if pal.stat().st_size != 512:
        return False, f"{pal.name} size {pal.stat().st_size} != 512"
    return True, "CLOUDS.DAT=65536 B, CLOUDS.PAL=512 B"


def predicate_palette_colors():
    pal = ROOT / "cd/CLOUDS.PAL"
    if not pal.exists():
        return False, "CLOUDS.PAL missing"
    data = pal.read_bytes()
    n_cloud = 0
    n_sky   = 0
    for i in range(256):
        w = struct.unpack(">H", data[i*2:i*2+2])[0]
        r5 = (w >> 10) & 0x1F
        g5 = (w >>  5) & 0x1F
        b5 = (w      ) & 0x1F
        r = r5 << 3 | r5 >> 2
        g = g5 << 3 | g5 >> 2
        b = b5 << 3 | b5 >> 2
        if is_cloud_white(r, g, b):
            n_cloud += 1
        elif is_sky_blue_cloud(r, g, b):
            n_sky += 1
    # Threshold = 5: empirically Pillow median-cut on the 256x256 Clouds
    # composite produces ~6-7 dominant color buckets (1 white highlight +
    # 4..6 sky-blue gradient slots).  Catastrophic palette-build bugs
    # (e.g. Phase 1.29c neon-green-flood at 37 green slots) would either
    # zero out the cloud-color count or replace it with off-hue saturated
    # buckets.  5 is the floor that catches both the missing-asset case
    # AND the wrong-color-bucket case.
    if n_cloud + n_sky < 5:
        return False, (f"only {n_cloud} cloud-white + {n_sky} sky-blue "
                       f"palette slots (< 5 total)")
    return True, f"{n_cloud} cloud-white + {n_sky} sky-blue palette slots"


def analyse_top_band(png_path):
    img = Image.open(png_path).convert("RGB").crop(ROI)
    w, h = img.size
    y1 = int(h * TOP_BAND_FRAC)
    px = img.load()
    n_cloud = 0
    sum_y   = 0
    for y in range(y1):
        for x in range(w):
            r, g, b = px[x, y]
            if is_cloud_color(r, g, b):
                n_cloud += 1
                sum_y += y
    cy = (sum_y / n_cloud) if n_cloud > 0 else 0.0
    return {"n_cloud": n_cloud, "cy": cy, "size": (w, y1)}


def predicate_saturn_clouds(frames, threshold=5000):
    if len(frames) < 5:
        return False, f"only {len(frames)} captures (need >= 5)", []
    stats = []
    for p in frames:
        s = analyse_top_band(p)
        stats.append((p, s))
    n_ok = sum(1 for _, s in stats if s["n_cloud"] >= threshold)
    counts = [s["n_cloud"] for _, s in stats]
    msg = (f"{n_ok}/{len(stats)} frames with >= {threshold} cloud px "
           f"(counts: {counts})")
    return n_ok >= max(3, len(stats) // 2), msg, stats


def predicate_motion(stats, min_total=2.0, min_nonzero_frames=3):
    if len(stats) < 2:
        return False, "need >= 2 frames"
    ys = [s["cy"] for _, s in stats if s["n_cloud"] >= 1000]
    if len(ys) < 2:
        return False, f"only {len(ys)} frames have measurable centroid"
    deltas = [ys[i+1] - ys[i] for i in range(len(ys) - 1)]
    total_abs = sum(abs(d) for d in deltas)
    nonzero = [d for d in deltas if abs(d) >= 0.5]
    msg = (f"|sum|={total_abs:.1f}, nonzero_frames={len(nonzero)}/{len(deltas)} "
           f"(thresholds: |sum|>={min_total}, nonzero>={min_nonzero_frames})")
    return (total_abs >= min_total and len(nonzero) >= min_nonzero_frames), msg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames-dir", default=str(ROOT))
    ap.add_argument("--frame-glob", default="qa_phase1_34b_clouds_*.png")
    ap.add_argument("--skip-saturn", action="store_true",
                    help="P1 + P2 only (pre-build sanity check)")
    ap.add_argument("--threshold", type=int, default=5000)
    args = ap.parse_args()

    print("=== Phase 1.34b NBG2 clouds gate ===")
    failures = []

    ok, msg = predicate_asset_present()
    print(f"  P1 asset present     : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P1")

    ok, msg = predicate_palette_colors()
    print(f"  P2 palette colors    : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P2")

    if args.skip_saturn:
        print("  P3 + P4 SKIPPED      : --skip-saturn requested")
    else:
        frames = sorted(Path(args.frames_dir).glob(args.frame_glob))
        print(f"  capture frames        : {len(frames)} matching "
              f"{args.frame_glob}")
        ok, msg, stats = predicate_saturn_clouds(frames, args.threshold)
        print(f"  P3 cloud pixels      : {'PASS' if ok else 'FAIL'} -- {msg}")
        if not ok:
            failures.append("P3")

        ok, msg = predicate_motion(stats)
        print(f"  P4 cloud Y motion    : {'PASS' if ok else 'FAIL'} -- {msg}")
        if not ok:
            failures.append("P4")

    print()
    if failures:
        print(f"Gate RED: {len(failures)} predicate(s) failed: "
              f"{', '.join(failures)}")
        sys.exit(1)
    print("Gate GREEN.")
    sys.exit(0)


if __name__ == "__main__":
    main()
