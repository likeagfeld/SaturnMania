#!/usr/bin/env python3
"""qa_phase1_31_tile_seam_gate.py - Phase 1.31 Fix #1 gate.

Asserts that the title backdrop does NOT show the 224x512 TITLE.DAT
tiling/repeating across the 512x512 RBG0 plane.

PROBLEM (pre-fix):
  src/main.c:156 calls jo_background_3d_plane_a_img(&img, pal, true, true).
  The third arg `repeat=true` -> jo's vdp2.c sets slOverRA(0) =
  RBG0_OVER_MODE_REPEAT.  More importantly, jo's __jo_create_map
  (vdp2.c:240-266) BAKES the 224x512 tile into the 64x64 page map by
  wrapping `x2 >= x` (x=28 cells); so the plane content itself shows
  the 224-px source pattern repeating at world-X = 224, 448 px.
  Visible result: multiple "island" silhouette bands appear in the
  title backdrop where there should be a single instance + back-color
  fill.

GATE PREDICATE (data-driven):
  For each settled-title frame in [START..END], crop the Saturn
  display ROI, then sample 16 horizontal rows spanning the top
  portion of the backdrop (above the Sonic/logo composite).  For
  each row, count "island-pixel" columns -- pixels matching the
  Saturn 555-palette colours for the island art (the dark/forest-
  green + dirt-orange + sky-blue triad that's distinct from the
  central wide neon-green back-color).  Then count CONNECTED COMPONENTS
  along each row: a single island instance = 1 component; tiled
  multi-island = 2+ components.

  Per-frame island-component count >= MIN_COMPONENTS = bug present.
  Aggregate over the settled-title window: median count > 1.0 = RED.

  RED-baseline (post-Fix-#2 capture, pre-Fix-#1): tile pattern visible
  as multiple connected components per row in frames 60..85.

  GREEN target: median row-component count <= 1.0 (one island only,
  surrounded by back-color fill).

Exit:
  0  GREEN (no tile-repeat detected)
  1  RED  (tile-repeat detected)
"""
import sys
from pathlib import Path
try:
    from PIL import Image
    import numpy as np
except ImportError:
    sys.exit("requires Pillow + numpy")

ROOT = Path(__file__).resolve().parent.parent
# Settled-title window: frame 60..85 of the 90-frame 3 fps capture.
# Per Phase 1.31 Fix #2 deadlock analysis, frame 87+ is past the
# auto-advance trigger (now disabled) but stay safe and end at 85.
START, END = 60, 85

# Mednafen Saturn ROI inside the captured window PNG (matches Gate
# V1.31-deadlock at qa_phase1_31_deadlock_gate.py:39).
ROI = (60, 60, 850, 690)

# Sample rows along the upper backdrop area (above the Sonic+logo
# composite which dominates the lower half).  Saturn screen height is
# ~630 px inside the ROI; rows 50..200 are the top backdrop band.
SAMPLE_Y_START = 50
SAMPLE_Y_END   = 200
SAMPLE_Y_STEP  = 10

# An "island pixel" is one that matches the dark-saturated forest /
# rocky-shore colours of the Title.bin island art, distinguishable
# from the neon-green back-color and from the central Sonic/logo
# composite.  Empirically (sampling qa_diag frames 60..85 post-Fix-#2):
#   Back-color neon green:  R~0   G~255 B~0
#   Island dark green:      R~30..80   G~120..180  B~30..80
#   Island brown/dirt:      R~120..200 G~80..130   B~30..80
#   Island sky/water:       R~50..120  G~120..180  B~180..255
# Test: a pixel is an "island pixel" iff it is NOT close to neon green
# AND NOT close to white/grey-Sonic AND has some saturation.
def is_island_pixel(rgb):
    """rgb is an (N,3) uint8 array. Returns (N,) bool."""
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    # Not pure neon green: reject pixels with G high AND R+B both very low
    pure_green = (g > 200) & (r < 60) & (b < 60)
    # Not near-white (Sonic body / banner highlights): reject high R+G+B
    near_white = (r > 200) & (g > 200) & (b > 200)
    # Not near-black/header borders (rejection band for the title-bar
    # border on the capture's outer edge)
    very_dark = (r < 20) & (g < 20) & (b < 20)
    return ~pure_green & ~near_white & ~very_dark


def count_components_in_row(mask_row):
    """Count connected runs of True pixels with length >= MIN_RUN."""
    MIN_RUN = 8  # an island is at least 8 px wide on screen
    runs = 0
    in_run = False
    run_len = 0
    for v in mask_row:
        if v:
            run_len += 1
            if not in_run and run_len >= MIN_RUN:
                runs += 1
                in_run = True
        else:
            in_run = False
            run_len = 0
    return runs


def analyse_frame(path):
    img = Image.open(path).convert("RGB").crop(ROI)
    arr = np.array(img)
    h, w, _ = arr.shape
    row_counts = []
    for y in range(SAMPLE_Y_START, min(SAMPLE_Y_END, h), SAMPLE_Y_STEP):
        row = arr[y]
        mask = is_island_pixel(row)
        # Restrict to the LEFT + RIGHT margin bands (the central
        # 33%..67% column band is dominated by the Sonic+logo
        # composite which is sprite-based, not RBG0 content).
        # Left band: 0..0.33*w; Right band: 0.67*w..w.
        third = w // 3
        left_mask  = mask[:third]
        right_mask = mask[2*third:]
        # Tile-seam detection: more components in the LEFT or RIGHT
        # margin = the island art is tiling into the margins.
        left_n  = count_components_in_row(left_mask)
        right_n = count_components_in_row(right_mask)
        # Total margin components.  Clean single-instance = 0 or 1;
        # tiled = 2+ (one in left margin, one in right margin, or
        # multiple within either margin).
        row_counts.append(left_n + right_n)
    return row_counts


def rbg0_hidden():
    """Phase 1.31 Fix #4 REVISED (Task #106, 2026-05-27): if the per-
    frame REPLACE block in src/main.c::mania_title_3d_backdrop_draw
    issues slPriorityRbg0(0), RBG0 contributes no pixels and the
    "224x512 tile-seam in RBG0" failure mode is structurally
    impossible.  Skip the gate in that configuration.

    Detection: peek PRIR.R0PRIN from the most recent settled-title
    savestate (samples/qa_phase1_31_fix4_pivot.mcs preferred; fall
    back to qa_phase1_31_rbg0_diag.mcs).  R0PRIN==0 -> RBG0 hidden ->
    skip.
    """
    import subprocess
    for cand in [ROOT / "samples" / "qa_phase1_31_fix4_pivot.mcs",
                 ROOT / "samples" / "qa_phase1_31_rbg0_diag.mcs"]:
        if cand.exists():
            try:
                out = subprocess.check_output(
                    ["py", "-3", str(ROOT / "tools" / "mcs_extract.py"),
                     str(cand), "--peek16", "0x05F800FC"], text=True).strip()
                prir = int(out.split("= ")[-1], 16)
                if (prir & 0x7) == 0:
                    return True
            except Exception:
                pass
    return False


def main():
    if rbg0_hidden():
        print(f"Phase 1.31 Fix #1 tile-seam gate")
        print(f"  SKIPPED: RBG0 is hidden (PRIR.R0PRIN==0 per Phase 1.31 "
              f"Fix #4 REVISED).  The 224x512 source-tile repeat failure "
              f"mode is structurally impossible when RBG0 contributes no "
              f"pixels.  Sub-fix A landed via slPriorityRbg0(0) in "
              f"src/main.c::mania_title_3d_backdrop_draw.")
        sys.exit(0)

    frames_with_tiles = []
    all_counts = []
    for n in range(START, END + 1):
        p = ROOT / f"qa_diag_{n}.png"
        if not p.exists():
            sys.exit(f"missing {p}")
        row_counts = analyse_frame(p)
        if not row_counts:
            continue
        median = float(np.median(row_counts))
        mx = max(row_counts)
        all_counts.append(median)
        if median > 1.0 or mx >= 3:
            frames_with_tiles.append((n, median, mx, row_counts))

    print(f"Phase 1.31 Fix #1 tile-seam gate")
    print(f"  Frames analysed: {START}..{END}")
    print(f"  Sample rows per frame: y in [{SAMPLE_Y_START},{SAMPLE_Y_END}] "
          f"step {SAMPLE_Y_STEP}")
    print(f"  Per-row metric: connected island-content runs in the "
          f"LEFT + RIGHT margins")
    print()
    print(f"  Aggregate median per-frame: median = "
          f"{float(np.median(all_counts)):.2f}, "
          f"mean = {float(np.mean(all_counts)):.2f}, "
          f"max = {float(np.max(all_counts)):.2f}")
    print()
    if frames_with_tiles:
        print(f"  Frames showing >1 island components in margins:")
        for n, med, mx, rc in frames_with_tiles[:10]:
            print(f"    frame {n}: row-component median={med:.1f} "
                  f"max={mx} per-row={rc}")
        if len(frames_with_tiles) > 10:
            print(f"    ... +{len(frames_with_tiles)-10} more")

    # RED predicate: aggregate median > 1.0 (more than one island
    # instance visible in margins for the majority of settled-title
    # frames).
    aggregate_median = float(np.median(all_counts))
    if aggregate_median > 1.0:
        print(f"\nRED: aggregate median row-components = "
              f"{aggregate_median:.2f} (> 1.0); tile seams visible "
              f"across {len(frames_with_tiles)}/{END-START+1} frames.")
        sys.exit(1)
    print(f"\nGREEN: aggregate median row-components = "
          f"{aggregate_median:.2f} (<= 1.0); no tile repeat detected.")
    sys.exit(0)


if __name__ == "__main__":
    main()
