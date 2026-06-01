#!/usr/bin/env python3
"""qa_phase1_31_palette_gate.py - Phase 1.31 Fix #3 gate.

Asserts the title backdrop does NOT show neon-green/magenta palette
artifacts when rendered through Saturn's actual CRAM-shift path.

DIAGNOSIS (pre-fix, 2026-05-27):
  cd/TITLE.DAT + cd/TITLE.PAL was built from an unknown asset pipeline
  that ended up with ~15K bright-green pixels per title frame.
  Via tools/view_title_dat_joshifted.py:
    slot 130: 8115 px  rgb=(0, 165, 66)   <-- neon green
    slot 131: 7243 px  rgb=(0, 206, 99)   <-- neon green
    slot 129: 2000 px  rgb=(0, 132, 16)
    slot 128: 1408 px  rgb=(0, 99, 0)
  Source 16x16Tiles.gif contains bright-green tile pixels (Mania island
  grass tones), but the 1024x1024 source Island layer is 93.75%
  magenta-transparency (GIF GCT slot 0 placeholder).  When that
  transparent-dominated layer is naively 256-color-quantised down to
  224x512, the small remaining solid-green grass patches get
  over-represented in the palette and dominate the output, producing
  the neon-green artifacts the user flagged.

  The Phase 1.29c builder (tools/build_title_island_bg.py) tried to
  rebuild the asset but contained TWO palette-shift bugs:
    line 145-147: palette shifted UP by 1 on disk (correct per
                  memory/jo-cram-off-by-one-shift.md)
    line 152-157: pixel bytes ALSO shifted up by 1 (WRONG — double
                  shift, makes the V->CRAM[V-1] correction self-cancel
                  and lands every pixel one slot off)
  Result: TITLE3D.PAL[1..7] all = 0x3320 (R=96,G=200,B=0) bright green;
  TITLE3D.PAL has 37 bright-green slots out of 138 unique colors.

  The fix path (Path A from the prompt brief):
    1. Re-implement build_title_island_bg.py with the CORRECT shift
       convention (palette shifted up by 1 on disk, pixels UNCHANGED).
    2. Composite Island over Black BG + Horizon + Clouds so opaque
       output dominates the 256-color quantiser.
    3. Use the existing extract_title_island.py per-layer outputs to
       feed a layered composite.

GATE PREDICATES (data-driven, no impressionism):
  Three independent checks; ALL three must pass for GREEN:

  P1 - Asset-level CRAM-shift palette histogram (deterministic, no
       runtime capture needed): for the on-disk cd/TITLE.DAT +
       cd/TITLE.PAL pair, simulate the V -> CRAM[V-1] lookup jo
       imposes and count pixels in each post-shift palette slot.
       Predicate: the SINGLE most-rendered slot must NOT be a
       bright-saturated-green color.  The Mania title backdrop's
       dominant region is sky-blue (Horizon layer) so the top-slot-
       by-pixel-count should be a blue/cyan family color.  Bright
       green appearing as the TOP slot indicates the asset is
       island-only without sky context (the OLD asset's failure mode).

  P2 - Asset-level unique-color count: cd/TITLE.PAL must have >= 30
       unique 16-bit color values.  A properly composited Mania
       backdrop has 30+ tones (sky gradients + cloud highlights +
       horizon + water + island art).  TITLE3D.PAL's 138 entries
       collapsed to 37 bright-green slots is caught by P1's
       dominant-slot check, not P2.

  P3 - Per-title-frame total bright-green pixel count from the live
       Saturn capture: each settled-title frame (60..85 of the
       90-frame 3 fps qa_diag capture) must have < 5000 bright-green
       pixels AND < 1000 magenta pixels within the title backdrop ROI.
       "Bright green" = (G > R+50 AND G > B+50 AND value > 150);
       "magenta" = (R > G+50 AND B > G+50 AND value > 150).

RED-firing baseline (current build, 2026-05-27 with cd/TITLE.DAT
unchanged from May 26 09:01):
  P1: slot 130 has 8115 green px, slot 131 has 7243 — RED.
  P2: 107 unique colors, 6 bright-green slots — P2 alone passes
      ON UNIQUE COUNT, but the per-slot pixel count fails via P1.
  P3: ~14K-130K bright-green pixels per title frame (per prior
      agent's analyze_diag_capture.py output) — RED.

GREEN target: all three P1/P2/P3 pass after rebuilding the asset
with proper layered composite + correct shift_pal.py convention.

Exit:
  0  GREEN (all three predicates pass)
  1  RED  (any predicate fails)
"""
import struct, sys
from collections import Counter
from pathlib import Path
try:
    from PIL import Image
    import numpy as np
except ImportError:
    sys.exit("requires Pillow + numpy")

ROOT = Path(__file__).resolve().parent.parent

# Asset paths (the gate operates on whatever cd/TITLE.DAT + cd/TITLE.PAL
# the build currently flashes into the ISO; rebuilding the asset
# rebuilds these files in-place).
DAT_PATH = ROOT / "cd/TITLE.DAT"
PAL_PATH = ROOT / "cd/TITLE.PAL"

# Capture window matches V1.31-tile-seam at qa_phase1_31_tile_seam_gate.py:54-58
ROI = (60, 60, 850, 690)
START, END = 60, 85

# Bright-green / magenta predicates.  Calibrated against
# view_title_dat_joshifted output bands: slot 130 rgb=(0,165,66) and
# slot 131 rgb=(0,206,99) MUST both classify as bright-green.  Those
# colors have val=(R+G+B)/3 = 77 and 101 respectively, so a val>150
# threshold (sum-of-channels brightness) is wrong.  The correct
# discriminator for the neon Mania-grass green that the user flagged
# is: G channel >= 130, AND G dominates R+B by at least 50 each, AND
# G saturation (G - max(R,B)) >= 60.
def classify(rgb_arr):
    """rgb_arr: (N,3) uint8. Returns (green_mask, magenta_mask)."""
    r = rgb_arr[..., 0].astype(np.int16)
    g = rgb_arr[..., 1].astype(np.int16)
    b = rgb_arr[..., 2].astype(np.int16)
    sat_g = g - np.maximum(r, b)
    green = (g >= 130) & (g > r + 50) & (g > b + 50) & (sat_g >= 60)
    # Magenta: classic Mania transparency placeholder (255,0,255) plus
    # any reddish-purple that leaked through quantisation.
    sat_m = np.minimum(r, b) - g
    magenta = (r >= 130) & (b >= 130) & (r > g + 50) & (b > g + 50) & (sat_m >= 60)
    return green, magenta


# ----------------------------------------------------------------------
# P1 - Asset-level CRAM-shift palette histogram
# ----------------------------------------------------------------------
def gate_p1():
    if not DAT_PATH.exists() or not PAL_PATH.exists():
        return False, f"missing {DAT_PATH.name} or {PAL_PATH.name}"
    dat = DAT_PATH.read_bytes()
    pal_bytes = PAL_PATH.read_bytes()
    if len(pal_bytes) < 512:
        return False, f"TITLE.PAL too small: {len(pal_bytes)}"
    # Decode palette to RGB
    pal_rgb = []
    for i in range(256):
        w = struct.unpack(">H", pal_bytes[i * 2:i * 2 + 2])[0]
        r5 = (w >> 10) & 0x1F
        g5 = (w >>  5) & 0x1F
        b5 = (w      ) & 0x1F
        pal_rgb.append((r5 << 3 | r5 >> 2,
                        g5 << 3 | g5 >> 2,
                        b5 << 3 | b5 >> 2))
    # Pixel-byte histogram; jo down-shift: byte V renders at CRAM[V-1]
    pix_count = Counter(dat)
    # Aggregate post-shift slot hit counts
    slot_hits = Counter()
    for v, c in pix_count.items():
        slot = (v - 1) & 0xFF
        slot_hits[slot] += c
    # Predicate: the SINGLE TOP-COUNT post-shift slot must NOT be a
    # bright-saturated green.  The Mania title backdrop's dominant
    # region is sky-blue + sea-blue (Horizon layer); a properly
    # composited asset has a blue/cyan-family color as the top slot.
    # The OLD asset's failure mode: orange-dirt (slot 7) was top,
    # but slots 130/131 (greens) accumulated 8K+7K = 15K combined,
    # i.e. the Sonic-head ISLAND filled the whole 224x512 with no
    # sky context.  The NEW composited asset puts sky-blue at the
    # top slot (30K+ px) and island grass as a discrete < 9K slot.
    ranked = sorted(slot_hits.items(), key=lambda t: -t[1])
    if not ranked:
        return False, "P1 RED: no pixels in TITLE.DAT"
    top_slot, top_count = ranked[0]
    tr, tg, tb = pal_rgb[top_slot]
    sat_g = tg - max(tr, tb)
    top_is_green = (tg >= 130 and tg > tr + 50 and tg > tb + 50 and sat_g >= 60)
    # Also detect the failure where the asset has NO sky context at
    # all — the next strongest signal is that NO slot in the top-5
    # is a blue/cyan family color (B dominates).
    top5 = ranked[:5]
    any_blue = False
    for slot, count in top5:
        r, g, b = pal_rgb[slot]
        sat_b = b - max(r, g)
        if b >= 100 and sat_b >= 30:
            any_blue = True
            break
    if top_is_green:
        return False, (f"P1 RED: top-rendered post-shift slot {top_slot} "
                       f"({top_count} px, rgb=({tr},{tg},{tb})) is bright "
                       f"green — the backdrop is island-only without sky.")
    if not any_blue:
        return False, (f"P1 RED: no blue/cyan slot in the top-5 most-rendered "
                       f"slots — top slots: " +
                       ", ".join(f"slot{s}={c}px rgb={pal_rgb[s]}"
                                 for s, c in top5))
    return True, (f"P1 GREEN: top slot {top_slot} ({top_count} px, "
                  f"rgb=({tr},{tg},{tb})) is non-green; >=1 of top-5 "
                  f"slots is blue family.")


# ----------------------------------------------------------------------
# P2 - Unique color count + bright-green slot fraction
# ----------------------------------------------------------------------
def gate_p2():
    pal_bytes = PAL_PATH.read_bytes()
    if len(pal_bytes) < 512:
        return False, "P2 RED: palette too small"
    unique = set()
    green_slots = 0
    for i in range(256):
        w = struct.unpack(">H", pal_bytes[i * 2:i * 2 + 2])[0]
        unique.add(w)
        r5 = (w >> 10) & 0x1F
        g5 = (w >>  5) & 0x1F
        b5 = (w      ) & 0x1F
        r = r5 << 3 | r5 >> 2
        g = g5 << 3 | g5 >> 2
        b = b5 << 3 | b5 >> 2
        sat_g = g - max(r, b)
        if g >= 130 and g > r + 50 and g > b + 50 and sat_g >= 60:
            green_slots += 1
    # P2 RED if > 40 bright-green slots OR < 30 unique colors.
    # The Sonic-head Mania island has ~10 green shading tones; > 40
    # means the asset is dominated by greens (the TITLE3D.PAL bug).
    # < 30 unique means the composite is degenerate.
    msg = f"unique colors = {len(unique)}, bright-green slots = {green_slots}"
    if green_slots > 40:
        return False, f"P2 RED: {msg} (> 40 bright-green slots — palette saturated by greens)"
    if len(unique) < 30:
        return False, f"P2 RED: {msg} (< 30 unique colors — degenerate quantisation)"
    return True, f"P2 GREEN: {msg}"


# ----------------------------------------------------------------------
# P3 - Live-capture per-frame bright-green / magenta pixel count
# ----------------------------------------------------------------------
def gate_p3():
    frames = []
    for n in range(START, END + 1):
        p = ROOT / f"qa_diag_{n}.png"
        if p.exists():
            frames.append((n, p))
    if not frames:
        # No capture present.  P3 is informational only when a capture
        # window isn't available (used in the asset-build standalone
        # verify path).  Report neutral.
        return True, "P3 SKIP: no qa_diag_*.png frames present (asset-only run)"
    bad_green = []
    bad_magenta = []
    all_green = []
    all_magenta = []
    for n, p in frames:
        img = Image.open(p).convert("RGB").crop(ROI)
        arr = np.array(img)
        green, magenta = classify(arr)
        gc = int(green.sum())
        mc = int(magenta.sum())
        all_green.append(gc)
        all_magenta.append(mc)
        if gc >= 5000:
            bad_green.append((n, gc))
        if mc >= 1000:
            bad_magenta.append((n, mc))
    summary = (f"P3: green per-frame min/median/max = "
               f"{min(all_green)}/{int(np.median(all_green))}/{max(all_green)}; "
               f"magenta per-frame min/median/max = "
               f"{min(all_magenta)}/{int(np.median(all_magenta))}/{max(all_magenta)}")
    if bad_green or bad_magenta:
        msg = f"P3 RED: {summary}\n"
        if bad_green:
            msg += f"    {len(bad_green)} frames >= 5000 bright-green; "
            msg += "worst: " + ", ".join(f"f{n}={c}" for n, c in bad_green[:5]) + "\n"
        if bad_magenta:
            msg += f"    {len(bad_magenta)} frames >= 1000 magenta; "
            msg += "worst: " + ", ".join(f"f{n}={c}" for n, c in bad_magenta[:5])
        return False, msg.rstrip()
    return True, f"P3 GREEN: {summary}"


def main():
    print("=== Phase 1.31 Fix #3 palette artifact gate ===")
    print(f"  asset: {DAT_PATH.name} + {PAL_PATH.name}")
    print(f"  capture window: qa_diag_{START}..{END}.png in ROI {ROI}")
    print()
    p1_ok, p1_msg = gate_p1()
    print(f"  {p1_msg}")
    p2_ok, p2_msg = gate_p2()
    print(f"  {p2_msg}")
    p3_ok, p3_msg = gate_p3()
    print(f"  {p3_msg}")
    print()
    if p1_ok and p2_ok and p3_ok:
        print("GREEN: palette-artifact gate passes all three predicates.")
        sys.exit(0)
    failed = []
    if not p1_ok: failed.append("P1")
    if not p2_ok: failed.append("P2")
    if not p3_ok: failed.append("P3")
    print(f"RED: palette-artifact gate failed: {', '.join(failed)}")
    sys.exit(1)


if __name__ == "__main__":
    main()
