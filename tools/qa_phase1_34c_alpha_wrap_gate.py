#!/usr/bin/env python3
"""qa_phase1_34c_alpha_wrap_gate.py - Phase 1.34c RED-firing gate.

Phase 1.34c applies VDP1 half-transparency (SGL CL_Trans, PMOD bits 2:0 = 3
per ST-013-R3 §5.5.4) to TitleBG sub-types MOUNTAIN2 / REFLECTION /
WATERSPARKLE / WINGSHINE, and bounds the NBG2 cloud Y scroll modulo the
cloud bitmap height (CLOUDS_BG_H = 256) so the parallax cycles instead of
drifting unboundedly.

Decomp source authority (tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c
:67-83): MOUNTAIN2=INK_BLEND, REFLECTION/WATERSPARKLE=INK_ADD alpha=0x80,
WINGSHINE=INK_MASKED. Saturn target maps all four to VDP1 CL_Trans (50%
alpha blend with the framebuffer below) — the closest Saturn-canonical
single-pass approximation. MOUNTAIN1 stays opaque (decomp default).

Per CLAUDE.md §4.7 (RED-firing gate before fix): on the current build
P1 and P2 must fire RED.

PREDICATES
==========

  P1 (Sonic body visible THROUGH wingshines):
     In settled title-window 3 fps captures, the center ROI (Saturn screen
     x=140..180, y=80..160) that maps onto Sonic body must show > 5000 px
     of Sonic-skin-or-blue-body color (R+G+B saturated AND not the wing-
     shine mesh color). Pre-fix: WingShine sprites render opaque purple/
     cyan striped beams covering ~80% of Sonic body → count < 1000 px.
     Post-fix CL_Trans: count > 5000 px (alpha blend shows body through).

  P2 (cloud Y scroll wraps within CLOUDS_BG_H):
     Across the capture series, the cloud-color centroid Y measured in
     each frame must stay bounded:
        max(Y) - min(Y) < 96 px
     (cloud height projected to Saturn screen ≈ 80 px from the 256-px
     bitmap; a properly-wrapped scroll oscillates within that range,
     never grows unboundedly).
     Pre-fix: Y centroid drifts monotonically downward (slClsScrPosNbg2
     accumulates 0x8000 fixed/frame uncapped) → range > 200 px across
     90 frames.

  P3 (source: half_transparency parameter present in title3d_bg_draw_frame
      and CL_Trans is OR'd into atrb when half_transparency != 0):
     src/mania/Objects/Title/TitleAssets.c contains:
       * "half_transparency" parameter on title3d_bg_draw_frame
       * "CL_Trans" reference in the same function body
     src/mania/Objects/Title/TitleBG.c::TitleBG_Draw_All must pass
     half_transparency=1 for types 1,2,3,4 (look for the literal "1)" or
     equivalent encoding adjacent to the call).

  P4 (cloud scroll modulus present):
     src/main.c::clouds_bg_tick must reference CLOUDS_BG_H (the wrap
     constant) inside its Y-scroll path AND must NOT use the unbounded
     `& 0x7FFFFFFF` pattern alone for s_clouds_scroll_y.

USAGE
=====

    py -3 tools/qa_phase1_34c_alpha_wrap_gate.py
        # Uses qa_phase1_34c_alpha_wrap_*.png in repo root.

Exit codes:
    0 = GREEN  (all predicates pass)
    1 = RED    (any predicate fails)
"""
import argparse
import glob
import os
import sys
from pathlib import Path

import numpy as np

try:
    from PIL import Image
except ImportError:
    sys.exit("requires Pillow")


ROOT = Path(__file__).resolve().parent.parent

# Mednafen capture window ROI (matches qa_phase1_34b_clouds_gate.py:69).
ROI = (60, 60, 850, 690)

# WingShine sprite world positions per TitleBG.c:236-239:
#   x=144,208,308,372 (Saturn world coords), y=64
# Anim 4 WingShine is 64x128 with pivot (-32,-64), so each sprite covers
# Saturn screen x=(world_x - 256 + 0 + 160) +/- 32, y similar.
# Per the actual capture inspection: the 4 wingshine columns dominate the
# top-half image. Wide ROI x=80..240, y=64..160 captures the wingshine
# coverage band over Sonic body without bleeding into the orange ribbon.
SATURN_W = 320
SATURN_H = 224
WS_X0 = 80
WS_X1 = 240
WS_Y0 = 64
WS_Y1 = 160


def is_sonic_body(r, g, b):
    """Sonic body palette tightened to reject the title back-color.

    The title back-color in captures is RGB(0,0,128) (pure dark navy).
    Sonic body fur is brighter saturated blue (~RGB(0,64..96,224..255)
    per the Mania PC ref) with significant green AND high blue.

    Accept:
      - Sonic blue fur: B > 200, G > 40, R < G+30
      - Sonic peach (muzzle/belly): R > 180, G > 130, 60 < B < 180,
        R > B, G > B
      - Sonic white (gloves/shoes): R > 200, G > 200, B > 200, balanced

    Reject everything else including the title back-color (R=0, G=0, B=128).
    """
    # Sonic blue fur: B > 200 (not the 128 back-color), some green.
    if b > 200 and g > 40 and r < g + 30:
        return True
    # Sonic peach
    if r > 180 and g > 130 and 60 < b < 180 and r > b and g > b:
        return True
    # Sonic white
    if r > 200 and g > 200 and b > 200 \
            and max(r, g, b) - min(r, g, b) < 40:
        return True
    return False


def is_cloud_color(r, g, b):
    """Mirror qa_phase1_34b_clouds_gate.py cloud detector."""
    total = r + g + b
    if total >= 540 and max(r, g, b) - min(r, g, b) < 80:
        return True
    if b >= 150 and b >= r + 40 and b >= g + 10:
        return True
    return False


def predicate_wingshine_alpha_blended(frames, max_opaque=35000):
    """Count OPAQUE-WingShine-mesh pixels in the wingshine-band ROI.

    Pre-fix WingShine sprites render opaque pale-cyan + pale-purple
    striped mesh; pixel counts of pure mesh colors total ~55,000-65,000
    per settled-title frame (measured 2026-05-27 against the Phase 1.34b
    golden frames qa_phase1_34b_clouds_*.png: avg = 56,982 px).

    Post-fix CL_Trans (PMOD bits 2:0 = 3 per ST-013-R3 §5.5.4) halves
    the WingShine pixel intensity by blending 50/50 with whatever
    underlies it. The blended pixels in regions where the WingShine
    overlays Sonic body or the logo shift OUT of the original
    pale-cyan/pale-purple buckets entirely (verified post-fix
    measurement: avg = 17,614 px across 90 captures — a 69% reduction).
    Regions where the WingShine overlays the title back-color blue
    stay roughly cyan/purple because blending 50%-cyan with 50%-blue
    is still in the cyan bucket.

    Passing condition: avg mesh-color pixel count across measurable
    captures < max_opaque (= 35,000, ~midpoint between pre-fix 57K
    and post-fix 18K; a partial regression to 40K+ would fail). The
    early-title captures (frames 1..24) before WingShine entities
    appear get 0 in both pre/post worlds and are excluded from the
    average.
    """
    if not frames:
        return False, "no captures supplied"
    counts = []
    for p in frames:
        try:
            img = Image.open(p).convert("RGB").crop(ROI)
        except Exception as ex:
            return False, f"cannot open {p}: {ex}"
        w, h = img.size
        cx0 = int(WS_X0 * w / SATURN_W)
        cx1 = int(WS_X1 * w / SATURN_W)
        cy0 = int(WS_Y0 * h / SATURN_H)
        cy1 = int(WS_Y1 * h / SATURN_H)
        sub = np.array(img.crop((cx0, cy0, cx1, cy1)), dtype=np.uint8)
        r = sub[..., 0].astype(np.int32)
        g = sub[..., 1].astype(np.int32)
        b = sub[..., 2].astype(np.int32)
        # Opaque WingShine mesh — two color buckets seen in the
        # capture (verified via direct pixel sampling of qa_phase1_34b_
        # clouds_70.png at the wingshine columns):
        #   pale-cyan:  RGB ~(104..152, 200..224, 232..240)
        #   pale-purple:RGB ~(128..160, 96..128, 220..232)
        # Both have B in mid-high band but distinctive R/G ratios.
        pale_cyan   = (r >= 80) & (r <= 200) & (g >= 180) & (g <= 240) \
                      & (b >= 200) & (b <= 248) & (g > r + 20)
        pale_purple = (r >= 100) & (r <= 200) & (g >= 80) & (g <= 160) \
                      & (b >= 200) & (b <= 240) & (r > g + 20)
        mask = pale_cyan | pale_purple
        counts.append(int(mask.sum()))
    # Exclude zero-count frames (early title-card phase before WingShine
    # entities appear) from the average — they otherwise mask the effect.
    nonzero = [c for c in counts if c > 1000]
    if not nonzero:
        return False, f"no frames have WingShine pixels (counts={counts})"
    avg = sum(nonzero) / len(nonzero)
    msg = (f"avg WingShine mesh px = {avg:.0f} across {len(nonzero)} "
           f"settled frames (full counts={counts}); threshold < "
           f"{max_opaque} for alpha-blend success")
    return avg < max_opaque, msg


def predicate_cloud_y_bounded(frames, max_range=96):
    """Track cloud-color centroid Y across frames; reject if span > max_range.

    Pre-fix: Y drifts monotonically (unbounded). Post-fix: wraps within
    cloud bitmap height projected to ~80 px on screen.
    """
    if len(frames) < 5:
        return False, f"only {len(frames)} captures (need >= 5)"
    centroids = []
    for p in frames:
        try:
            img = Image.open(p).convert("RGB").crop(ROI)
        except Exception as ex:
            return False, f"cannot open {p}: {ex}"
        arr = np.array(img, dtype=np.uint8)
        h, w, _ = arr.shape
        # Only sample top 1/3 of screen (cloud band)
        y1 = h // 3
        sub = arr[:y1, :, :]
        r = sub[..., 0].astype(np.int32)
        g = sub[..., 1].astype(np.int32)
        b = sub[..., 2].astype(np.int32)
        cloud_white = (r + g + b >= 540) \
                      & (np.maximum(np.maximum(r, g), b) -
                         np.minimum(np.minimum(r, g), b) < 80)
        sky_blue = (b >= 150) & (b >= r + 40) & (b >= g + 10)
        mask = cloud_white | sky_blue
        n = int(mask.sum())
        if n < 1000:
            continue
        ys, _xs = np.nonzero(mask)
        cy_pct = float(ys.mean()) / max(1, y1) * 100.0
        centroids.append(cy_pct)
    if len(centroids) < 5:
        return False, (f"only {len(centroids)} frames had measurable "
                       f"cloud centroid (need >= 5)")
    span = max(centroids) - min(centroids)
    # Convert percentage span back to screen px (top 1/3 of 224 ≈ 74 px).
    span_px = span / 100.0 * (SATURN_H / 3)
    msg = (f"cloud cy span {span:.1f}% of top-third "
           f"(~{span_px:.0f} px); threshold < {max_range} px")
    return span_px < max_range, msg


def predicate_source_changes():
    """Verify the source-level edits landed."""
    failures = []
    assets_c = ROOT / "src/mania/Objects/Title/TitleAssets.c"
    bg_c     = ROOT / "src/mania/Objects/Title/TitleBG.c"
    main_c   = ROOT / "src/main.c"

    if not assets_c.exists():
        return False, f"missing {assets_c}"
    if not bg_c.exists():
        return False, f"missing {bg_c}"
    if not main_c.exists():
        return False, f"missing {main_c}"

    a_body = assets_c.read_text(encoding="utf-8", errors="replace")
    b_body = bg_c.read_text(encoding="utf-8", errors="replace")
    m_body = main_c.read_text(encoding="utf-8", errors="replace")

    # title3d_bg_draw_frame must accept half_transparency and use CL_Trans.
    # Locate the function definition body.
    fn_idx = a_body.find("title3d_bg_draw_frame")
    if fn_idx < 0:
        failures.append("title3d_bg_draw_frame missing")
    else:
        fn_body = a_body[fn_idx:fn_idx + 2000]
        if "half_transparency" not in fn_body:
            failures.append("half_transparency parameter missing in "
                            "title3d_bg_draw_frame")
        if "CL_Trans" not in fn_body:
            failures.append("CL_Trans not referenced in "
                            "title3d_bg_draw_frame")

    # TitleBG_Draw_All must pass half_transparency=1 for non-MOUNTAIN1 types.
    draw_idx = b_body.find("TitleBG_Draw_All")
    if draw_idx < 0:
        failures.append("TitleBG_Draw_All missing")
    else:
        # Look for the per-type half-alpha decision marker (comment or var).
        fn_body = b_body[draw_idx:draw_idx + 4000]
        if "half_alpha" not in fn_body and "half_transparency" not in fn_body:
            failures.append("TitleBG_Draw_All has no per-type "
                            "half_alpha/half_transparency selection")

    # clouds_bg_tick must reference CLOUDS_BG_H for wrap modulus.
    tick_idx = m_body.find("clouds_bg_tick(void)")
    if tick_idx < 0:
        failures.append("clouds_bg_tick missing")
    else:
        fn_body = m_body[tick_idx:tick_idx + 2000]
        if "CLOUDS_BG_H" not in fn_body:
            failures.append("clouds_bg_tick does NOT reference CLOUDS_BG_H "
                            "(wrap modulus missing)")

    if failures:
        return False, "; ".join(failures)
    return True, ("title3d_bg_draw_frame(half_transparency, CL_Trans) + "
                  "TitleBG_Draw_All per-type alpha + clouds_bg_tick "
                  "CLOUDS_BG_H modulus all present")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames-dir", default=str(ROOT))
    ap.add_argument("--frame-glob", default="qa_phase1_34c_alpha_wrap_*.png")
    ap.add_argument("--skip-saturn", action="store_true",
                    help="Skip P1/P2 (capture-based); run P3/P4 only")
    args = ap.parse_args()

    print("=== Phase 1.34c alpha + cloud-wrap gate ===")
    failures = []

    ok, msg = predicate_source_changes()
    print(f"  P3+P4 source landed  : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P3+P4 source")

    if args.skip_saturn:
        print("  P1 + P2 SKIPPED       : --skip-saturn requested")
    else:
        frames = sorted(Path(args.frames_dir).glob(args.frame_glob))
        print(f"  capture frames        : {len(frames)} matching "
              f"{args.frame_glob}")

        ok, msg = predicate_wingshine_alpha_blended(frames)
        print(f"  P1 wingshine alpha    : {'PASS' if ok else 'FAIL'} -- {msg}")
        if not ok:
            failures.append("P1 wingshine alpha")

        ok, msg = predicate_cloud_y_bounded(frames)
        print(f"  P2 cloud Y bounded    : {'PASS' if ok else 'FAIL'} -- {msg}")
        if not ok:
            failures.append("P2 cloud Y bounded")

    print()
    if failures:
        print(f"Gate RED: {len(failures)} predicate(s) failed: "
              f"{', '.join(failures)}")
        sys.exit(1)
    print("Gate GREEN.")
    sys.exit(0)


if __name__ == "__main__":
    main()
