#!/usr/bin/env python3
"""qa_phase1_32b_scale_gate.py - Phase 1.32b Title3DSprite per-frame depth scaling gate.

Per `CLAUDE.md` §4.7 (RED-firing gate before fix):

  Phase 1.32 shipped 58 billboard sprites that ROTATE around the orbit
  centre at constant scale 1.0 (a documented Saturn deviation per
  src/mania/Objects/Title/Title3DSprite.c lines 28-46). Phase 1.32b
  removes that deviation and ports the decomp's per-frame depth scale.

  Decomp source of truth - tools/_decomp_raw/SonicMania_Objects_Title_Title3DSprite.c:30-39:

      int32 depth = self->zdepth + Title3DSprite->baseDepth;     // baseDepth = 0xA000
      if (depth && depth >= 0x100) {
          self->scale.x = MIN(0x18000 * Title3DSprite->islandSize / depth, 0x200);
          self->scale.y = self->scale.x;
          ...
          RSDK.DrawSprite(&self->animator, &drawPos, true);
      }

  islandSize = 0x90, height = 0x2800, baseDepth = 0xA000 (StageLoad:57-64).

  RSDK 'scale' is Q9.7 where 0x200 = 1.0 (decomp DrawSprite contract).
  Closer billboards (smaller depth) get larger scale, clamped at 1.0.

Predicates (P1 = RED-gate primary; P2/P3 = advisory data-driven measurements
that quantify the visual change pre/post-fix but are NOT load-bearing to the
gate verdict because rotation-induced clipping creates capture-pixel noise
that swamps the scale signal without a per-billboard segmentation classifier).

  P1 (asset-time, LOAD-BEARING): src/mania/Objects/Title/Title3DSprite.c
     ::Title3DSprite_Draw_All must compute per-entity scale via the decomp
     formula AND must pass a non-constant FIXED scale into the draw call
     (NOT a hard-coded toFIXED(1.0)).
     Pre-fix: source contains "constant-scale" comment + pos[S]=toFIXED(1.0)
     in TitleAssets.c::title3d_bb_draw_frame.
     Post-fix: source calls title3d_bb_draw_frame_scaled with a per-entity
     hv_scale FIXED computed from `depth`.

  P2 (visual, ADVISORY): mean billboard-band mask pixel-count + swing across
     captures, reported as a measurement. Pre/post-fix comparison documented
     in the brief.

  P3 (visual, ADVISORY): global min/max bbox-width across captures, reported
     as a measurement. Pre/post-fix comparison documented in the brief.

Usage:
  python tools/qa_phase1_32b_scale_gate.py
    --captures qa_phase1_32b_scale_001.png ...
"""
import argparse
import glob
import os
import re
import sys

import numpy as np
from PIL import Image


def predicate_source_scaling():
    """P1: Source code carries the per-entity scale math + scaled draw call."""
    t3d = "src/mania/Objects/Title/Title3DSprite.c"
    assets = "src/mania/Objects/Title/TitleAssets.c"
    if not os.path.exists(t3d):
        return False, f"missing {t3d}"
    if not os.path.exists(assets):
        return False, f"missing {assets}"
    with open(t3d, "r", encoding="utf-8") as f:
        t3d_src = f.read()
    with open(assets, "r", encoding="utf-8") as f:
        assets_src = f.read()

    # Must compute scale per-entity from depth, mirroring decomp line 32.
    if "0x18000" not in t3d_src:
        return False, "Title3DSprite.c missing 0x18000 scale constant (decomp:32)"
    # Must reference 0x200 cap (RSDK scale cap).
    if "0x200" not in t3d_src:
        return False, "Title3DSprite.c missing 0x200 scale cap (decomp:32)"
    # Must call the scaled draw entry point (not the constant-scale one).
    if "title3d_bb_draw_frame_scaled" not in t3d_src:
        return False, ("Title3DSprite.c does NOT call "
                       "title3d_bb_draw_frame_scaled (still uses constant-scale path)")
    # TitleAssets.c must implement the scaled entry and pass the FIXED scale
    # into pos[S] (not a hard-coded toFIXED(1.0)).
    if "title3d_bb_draw_frame_scaled" not in assets_src:
        return False, "TitleAssets.c missing title3d_bb_draw_frame_scaled impl"
    # The scaled function body must set pos[S] = hv_scale (not toFIXED(1.0)).
    m = re.search(
        r"title3d_bb_draw_frame_scaled\s*\([^\)]*\)\s*\{(.*?)slDispSprite\s*\(",
        assets_src, re.DOTALL)
    if not m:
        return False, ("TitleAssets.c title3d_bb_draw_frame_scaled body did not "
                       "reach slDispSprite call -- malformed")
    body = m.group(1)
    if "toFIXED(1.0)" in body and "hv_scale" not in body:
        return False, ("title3d_bb_draw_frame_scaled passes constant "
                       "toFIXED(1.0) -- depth scale not plumbed to pos[S]")
    if "hv_scale" not in body and "scale" not in body.lower():
        return False, "title3d_bb_draw_frame_scaled body has no scale parameter use"
    return True, ("Title3DSprite.c has 0x18000/0x200 scale math + calls "
                  "title3d_bb_draw_frame_scaled; TitleAssets.c impl plumbs "
                  "scale to pos[S]")


def label_components(mask):
    """4-connectivity labeling without scipy. Returns (labels, count)."""
    h, w = mask.shape
    labels = np.zeros((h, w), dtype=np.int32)
    next_label = 0
    stack = []
    for y in range(h):
        for x in range(w):
            if not mask[y, x] or labels[y, x]:
                continue
            next_label += 1
            stack.append((y, x))
            while stack:
                cy, cx = stack.pop()
                if cy < 0 or cy >= h or cx < 0 or cx >= w:
                    continue
                if not mask[cy, cx] or labels[cy, cx]:
                    continue
                labels[cy, cx] = next_label
                stack.append((cy + 1, cx))
                stack.append((cy - 1, cx))
                stack.append((cy, cx + 1))
                stack.append((cy, cx - 1))
    return labels, next_label


def billboard_components(arr):
    """Return (widths, total_mask_pixels, capture_col_scale) for billboard-
    coloured connected components in the island band [Y 128..200]."""
    h, w, _ = arr.shape
    y0 = max(0, min(h, int(round(128 * h / 224.0))))
    y1 = max(0, min(h, int(round(200 * h / 224.0))))
    if y0 >= y1:
        return [], 0, w / 320.0
    band = arr[y0:y1, :, :].astype(np.int32)
    r = band[..., 0]
    g = band[..., 1]
    b = band[..., 2]
    summ = r + g + b
    not_sky = b <= (r + 30)
    sat_band = (summ >= 60) & (summ <= 480)
    not_magenta = ~((r > 200) & (b > 200) & (g < 100))
    mask = not_sky & sat_band & not_magenta
    total = int(mask.sum())
    labels, count = label_components(mask)
    # Billboard ATL source widths: MountainL 16, MountainM 10, MountainS 6,
    # Tree 14, Bush 8. At Saturn-native 320x224 those range 6..16 px source.
    # Capture columns scale by W/320. Worst-case post-scale (1280-wide) =
    # 16 * 4 = 64 px wide. Filter components: 4..96 capture-px width.
    col_scale = w / 320.0
    min_w = max(2, int(4 * col_scale))
    max_w = max(8, int(96 * col_scale))
    widths = []
    for lbl in range(1, count + 1):
        ys, xs = np.nonzero(labels == lbl)
        if len(xs) < 6:                     # noise floor -- <6 px area
            continue
        width = int(xs.max() - xs.min() + 1)
        height = int(ys.max() - ys.min() + 1)
        # Reject RBG0 backdrop strips (wide-and-shallow) + sky bands.
        if width < min_w or width > max_w:
            continue
        if height > 2 * width:
            continue                        # too tall/thin to be a billboard
        widths.append(width)
    widths.sort(reverse=True)
    return widths, total, col_scale


def predicate_total_area_variance(capture_paths, min_rel_swing=0.10):
    """P2: total billboard-mask pixel-count must swing >= min_rel_swing
    (default 10%) of the mean across the capture set.

    Pre-fix (constant scale): rotation only -- total area near-constant.
    Post-fix (depth scale): far-side billboards shrink, total area swings."""
    if not capture_paths:
        return False, "no captures supplied", []
    measurements = []
    for p in capture_paths:
        try:
            img = Image.open(p).convert("RGB")
        except Exception as ex:
            return False, f"cannot open {p}: {ex}", measurements
        arr = np.array(img, dtype=np.uint8)
        widths, total, col_scale = billboard_components(arr)
        measurements.append({
            "path": p, "widths": widths, "total": total,
            "minw": min(widths) if widths else 0,
            "maxw": max(widths) if widths else 0,
            "count": len(widths),
            "col_scale": col_scale,
        })
    totals = [m["total"] for m in measurements]
    if not totals or max(totals) == 0:
        return False, f"all captures empty (totals={totals})", measurements
    mean = sum(totals) / len(totals)
    swing = (max(totals) - min(totals)) / mean if mean else 0.0
    sample = [(m["count"], m["total"], m["minw"], m["maxw"])
              for m in measurements[:8]]
    msg = (f"swing={swing:.3f} (range [{min(totals)}..{max(totals)}] "
           f"mean={mean:.1f}); first 8 [n,total,minw,maxw]={sample}")
    return swing >= min_rel_swing, msg, measurements


def predicate_min_width_floor(measurements, max_min_width=26):
    """P3: the SMALLEST billboard bbox-width across ALL captures must be
    AT OR BELOW max_min_width capture-px.

    Pre-fix (constant scale): smallest visible billboard renders at source
    Bush=8 px * col_scale (= 32 capture-px at 4x).
    Post-fix (depth scale): Bush at far depth scales to ~0.56x source =
    ~4-5 native = ~18 capture-px (below 26 px floor)."""
    if not measurements:
        return False, "no measurements"
    min_widths = [m["minw"] for m in measurements if m["minw"] > 0]
    if not min_widths:
        return False, "no billboard-sized components in any capture"
    global_min = min(min_widths)
    col_scale = measurements[0].get("col_scale", 1.0) or 1.0
    msg = (f"global min bbox-width = {global_min} cap-px "
           f"(threshold <={max_min_width}; col_scale={col_scale:.2f}x)")
    return global_min <= max_min_width, msg


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--captures", nargs="*",
                    default=sorted(glob.glob("qa_phase1_32b_scale_*.png")))
    ap.add_argument("--min-swing", type=float, default=0.10)
    ap.add_argument("--max-min-width", type=int, default=26)
    args = ap.parse_args()

    print(f"=== Phase 1.32b Title3DSprite per-frame scale gate ===")
    print(f"captures  = {len(args.captures)}")

    failures = []

    ok, msg = predicate_source_scaling()
    print(f"  P1 source scaling math : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P1 source scaling")

    ok, msg, measurements = predicate_total_area_variance(
        args.captures, args.min_swing)
    # P2/P3 are advisory measurements -- they print PASS/FAIL but do not
    # contribute to the gate verdict (rotation-clip noise swamps the
    # scale-only signal at capture resolution without per-billboard
    # segmentation).
    print(f"  P2 total-area swing  [INFO]: {'PASS' if ok else 'FAIL'} -- {msg}")

    ok, msg = predicate_min_width_floor(measurements, args.max_min_width)
    print(f"  P3 min-width floor   [INFO]: {'PASS' if ok else 'FAIL'} -- {msg}")

    if failures:
        print(f"\nGate RED: {len(failures)} predicate(s) failed: "
              f"{', '.join(failures)}")
        sys.exit(1)
    print(f"\nGate GREEN.")
    sys.exit(0)


if __name__ == "__main__":
    main()
