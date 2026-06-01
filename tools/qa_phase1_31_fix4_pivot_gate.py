#!/usr/bin/env python3
"""qa_phase1_31_fix4_pivot_gate.py - Phase 1.31 Fix #4 REVISED RED-firing gate.

The strategic pivot (2026-05-27, Task #106): replace the full-screen RBG0
rotating bitmap (which the user dislikes — Phase 1.27/1.32/1.32b billboards
alone form the visible island) with:
  SUB-FIX A: hide RBG0 entirely (slPriorityRbg0(0)).
  SUB-FIX B: add NBG2 cloud parallax for the top region (Title TileLayer 2
             Clouds, 256x256 source -> Saturn NBG2 8bpp cell-mode).

This gate fires RED on the current build (RBG0 visible, neon-green flood
dominant) and GREEN once both sub-fixes have landed.

PREDICATES
==========

  P1 (Sub-fix A) RBG0 hidden register:
     PRIR.R0PRIN  @ 0x05F800FC bits[2:0] == 0.
     RED on baseline (R0PRIN=5), GREEN after slPriorityRbg0(0).

  P2 (Sub-fix A) RBG0 flood eliminated from visible output:
     Settled-title window frames (60..85), top-1/3 ROI of the Saturn
     display:
       * Pre-fix: neon-green flood (RGB ~(0,255,0)) covers > 30% of
         top-1/3 ROI pixels.  Sky-blue back-color (RGB(96,128,224))
         covers < 5% of ROI.
       * Post-fix A: neon-green eliminated (< 5% of ROI) AND sky-blue
         back-color OR cloud-region pixels dominate (>= 50% of ROI).
     This catches both "RBG0 still visible" AND "billboards regressed"
     since sky-blue / cloud dominance only happens when both billboard
     pipeline survives AND RBG0 hidden.

  P3 (Sub-fix B) Cloud parallax animated in top-1/3:
     For at least 6 of the last 10 settled frames, the top-1/3 ROI
     must contain >= 800 cloud-puff pixels (white/light highlights).
     AND the centroid X of cloud-puff pixels must shift over the
     window: |sum-of-deltas| >= 4 px AND >= 3 frames with non-trivial
     motion.

     Sub-fix A ALONE (sky-blue back-color in top-1/3, no clouds) will
     PASS P1+P2 but FAIL P3 — which is correct behaviour for the
     intermediate Sub-fix-A-only landing.

USAGE
=====

    py -3 tools/qa_phase1_31_fix4_pivot_gate.py
        # Uses the most recent qa_diag_*.png frames in repo root and
        # the savestate at samples/qa_phase1_31_fix4_pivot.mcs.

    py -3 tools/qa_phase1_31_fix4_pivot_gate.py \\
        --mcs samples/qa_phase1_31_fix4a_revert_v2.mcs \\
        --frames-dir D:/sonicmaniasaturn/
        # Uses an existing baseline savestate + frame set.

    py -3 tools/qa_phase1_31_fix4_pivot_gate.py --skip-p3
        # Only checks P1+P2 (Sub-fix A only); skips cloud-band check.

Exit codes:
    0 = GREEN  (all enabled predicates pass)
    1 = RED    (any enabled predicate fails)
"""
import argparse
import struct
import subprocess
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("requires Pillow (pip install Pillow)")

ROOT = Path(__file__).resolve().parent.parent

# Mednafen window capture ROI (matches Phase 1.31 Fix #1 tile-seam gate
# at qa_phase1_31_tile_seam_gate.py:58).
ROI = (60, 60, 850, 690)

# Top-1/3 of the Saturn display ROI = cloud band.
# ROI height = 690-60 = 630 px representing 224 emulated lines.
# 80 emulated lines = 80/224 * 630 = ~225 ROI pixels.
TOP_BAND_ROI_FRAC = 80.0 / 224.0

# Settled-title window of the 90-frame 3 fps capture.  Phase 1.31 Fix #1
# uses 60..85.
SETTLED_FRAME_LO = 60
SETTLED_FRAME_HI = 85


def peek16(mcs_path: Path, addr: int) -> int:
    out = subprocess.check_output(
        ["py", "-3", str(ROOT / "tools" / "mcs_extract.py"),
         str(mcs_path), "--peek16", hex(addr)],
        text=True,
    ).strip()
    return int(out.split("= ")[-1], 16)


def is_neon_green_flood(r, g, b):
    """RBG0 K-table back-screen leak per Phase 1.31 Fix #3 retry: RGB
    ~(0, 255, 0)."""
    return g >= 200 and r < 80 and b < 80


def is_back_color_sky(r, g, b):
    """jo_core_init back-color RGB(96, 128, 224); tolerance band."""
    return (60 < r < 140 and 90 < g < 170 and 180 < b < 248)


def is_cloud_puff(r, g, b):
    """Light/white cloud highlights from Title/Clouds layer art.  In the
    255-clamped Saturn output these are R+G+B >= 540 with similar
    channels (white-ish or pale-grey-blue cloud edges)."""
    total = r + g + b
    if total < 540:
        return False
    # Reject saturated single-channel colors (e.g. neon green).
    if max(r, g, b) - min(r, g, b) > 80:
        return False
    return True


def analyse_top_band(png_path: Path):
    """Crop to Saturn display ROI, take top-1/3 band, count pixel
    classes + cloud-puff centroid X."""
    img = Image.open(png_path).convert("RGB").crop(ROI)
    w, h = img.size
    y1 = int(h * TOP_BAND_ROI_FRAC)
    px = img.load()
    n_green = 0
    n_back  = 0
    n_cloud = 0
    sum_cloud_x = 0
    for y in range(y1):
        for x in range(w):
            r, g, b = px[x, y]
            if is_neon_green_flood(r, g, b):
                n_green += 1
            elif is_back_color_sky(r, g, b):
                n_back += 1
            if is_cloud_puff(r, g, b):
                n_cloud += 1
                sum_cloud_x += x
    total = y1 * w
    centroid_x = (sum_cloud_x / n_cloud) if n_cloud > 0 else 0.0
    return {
        "total":      total,
        "n_green":    n_green,
        "n_back":     n_back,
        "n_cloud":    n_cloud,
        "centroid_x": centroid_x,
        "size":       (w, h),
        "y_band":     y1,
    }


def find_settled_frames(frames_dir: Path):
    out = []
    for n in range(SETTLED_FRAME_LO, SETTLED_FRAME_HI + 1):
        p = frames_dir / f"qa_diag_{n}.png"
        if p.exists():
            out.append((n, p))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mcs",
                    default=str(ROOT / "samples" / "qa_phase1_31_fix4_pivot.mcs"))
    ap.add_argument("--frames-dir", default=str(ROOT))
    ap.add_argument("--skip-p3", action="store_true",
                    help="Only check P1+P2 (Sub-fix A only)")
    args = ap.parse_args()

    failures = []

    # ===== P1: PRIR.R0PRIN register =====
    mcs_path = Path(args.mcs)
    if not mcs_path.exists():
        for fallback in [
            ROOT / "samples" / "qa_phase1_31_fix4a_revert_v2.mcs",
            ROOT / "samples" / "qa_phase1_31_fix3_post.mcs",
        ]:
            if fallback.exists():
                mcs_path = fallback
                print(f"  (using fallback savestate {fallback.name})")
                break

    if not mcs_path.exists():
        failures.append("P1 RED: no Mednafen savestate available")
    else:
        print(f"P1: peeking PRIR + BGON from {mcs_path.name}")
        prir = peek16(mcs_path, 0x05F800FC)
        bgon = peek16(mcs_path, 0x05F80020)
        r0prin = prir & 0x7
        r0on   = (bgon >> 4) & 1
        print(f"  PRIR  @ 0x05F800FC = 0x{prir:04x}   R0PRIN = {r0prin}")
        print(f"  BGON  @ 0x05F80020 = 0x{bgon:04x}   R0ON   = {r0on}")
        if r0prin == 0:
            print(f"  P1 GREEN: RBG0 priority = 0 (hidden)")
        else:
            failures.append(
                f"P1 RED: PRIR.R0PRIN = {r0prin}, expected 0 "
                f"(Sub-fix A: slPriorityRbg0(0) not applied)")

    # ===== P2 + P3: visual analysis of top-1/3 ROI =====
    frames_dir = Path(args.frames_dir)
    frames = find_settled_frames(frames_dir)
    print()
    if len(frames) < 10:
        failures.append(
            f"P2/P3 RED: only {len(frames)} settled frames in "
            f"{frames_dir} (need >= 10 from window {SETTLED_FRAME_LO}..{SETTLED_FRAME_HI})")
    else:
        print(f"P2/P3: inspecting {len(frames)} settled frames "
              f"(window {SETTLED_FRAME_LO}..{SETTLED_FRAME_HI})")
        stats = []
        for n, fp in frames:
            s = analyse_top_band(fp)
            stats.append((n, fp, s))
            pct_green = 100.0 * s['n_green'] / s['total']
            pct_back  = 100.0 * s['n_back']  / s['total']
            pct_cloud = 100.0 * s['n_cloud'] / s['total']
            print(f"  frame {n:2d}: ROI {s['size'][0]}x{s['y_band']} "
                  f"green={pct_green:5.1f}% back={pct_back:5.1f}% "
                  f"cloud={pct_cloud:4.1f}% centroid_x={s['centroid_x']:.1f}")

        # P2: settled frames must have neon-green << 5% AND
        # (back-color + cloud-band) >= 30% in top-1/3.
        # Use the last 16 frames (settled).
        window = stats[-16:]
        ok = 0
        for n, _, s in window:
            pct_green = 100.0 * s['n_green'] / s['total']
            pct_back  = 100.0 * s['n_back']  / s['total']
            pct_cloud = 100.0 * s['n_cloud'] / s['total']
            if pct_green < 5.0 and (pct_back + pct_cloud) >= 30.0:
                ok += 1
        if ok >= 10:
            print(f"  P2 GREEN: {ok}/{len(window)} settled frames have "
                  f"top-1/3 free of neon-green AND filled with "
                  f"back-color/cloud content (>= 10 required)")
        else:
            failures.append(
                f"P2 RED: only {ok}/{len(window)} settled frames have "
                f"top-1/3 ROI free of neon-green flood AND filled with "
                f"sky-blue back-color or cloud content (threshold: 10). "
                f"RBG0 still rendering the neon-green flood OR billboards "
                f"regressed.")

        # P3: cloud-puff motion in top-1/3 across the window.
        if args.skip_p3:
            print(f"  P3 SKIPPED (Sub-fix A only)")
        else:
            cloud_ok = sum(1 for _, _, s in window if s['n_cloud'] >= 800)
            if cloud_ok < 6:
                failures.append(
                    f"P3 RED: only {cloud_ok}/{len(window)} settled frames "
                    f"show >= 800 cloud-puff pixels (threshold: 6).  "
                    f"Sub-fix B (NBG2 cloud parallax) not applied or "
                    f"clouds invisible.")
            else:
                deltas = []
                prev = None
                for _, _, s in window:
                    if prev is not None and s['n_cloud'] > 0 and prev['n_cloud'] > 0:
                        d = s['centroid_x'] - prev['centroid_x']
                        deltas.append(d)
                    prev = s
                nonzero = [d for d in deltas if abs(d) >= 0.5]
                total_abs = sum(abs(d) for d in deltas) if deltas else 0.0
                print(f"  P3 motion: |sum|={total_abs:.1f}, "
                      f"nonzero_frames={len(nonzero)}/{len(deltas)}")
                if total_abs >= 4.0 and len(nonzero) >= 3:
                    print(f"  P3 GREEN: cloud parallax animates "
                          f"(|sum|={total_abs:.1f}, {len(nonzero)} moving frames)")
                else:
                    failures.append(
                        f"P3 RED: cloud centroid X motion |sum|={total_abs:.1f} "
                        f"(threshold 4.0) with {len(nonzero)} moving frames "
                        f"(threshold 3).  Cloud parallax tick not driving "
                        f"slScrPosNbg2 frame-over-frame.")

    print()
    if failures:
        print("=" * 64)
        print("RED: gate failed")
        print("=" * 64)
        for f in failures:
            print(f)
        sys.exit(1)
    else:
        print("=" * 64)
        print("GREEN: all predicates pass")
        print("=" * 64)
        sys.exit(0)


if __name__ == "__main__":
    main()
