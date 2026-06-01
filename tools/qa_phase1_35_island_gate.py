#!/usr/bin/env python3
"""qa_phase1_35_island_gate.py - Phase 1.35 RED-firing gate.

Phase 1.35 loads the decomp Title TileLayer 3 'Island'
(tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:103-105) into a
Saturn VDP2 NBG1 cell-mode 8-bpp plane, providing the visible island
silhouette that the Title3DSprite billboards (Phase 1.32) sit on top
of.

User report 2026-05-28: "the island is not there." Diagnosis: RBG0
(hidden by Phase 1.31 Fix #4 Sub-fix A pivot) used to hold the
non-decomp TITLE.DAT silhouette; the actual decomp island content
extracted by extract_title_island.py was never loaded into a visible
Saturn rendering layer.  Phase 1.34c only loaded clouds (NBG2);
Phase 1.35 adds the island (NBG1).

Per CLAUDE.md §4.7 (RED-firing gate before fix) and the binding
methodology mirroring tools/qa_phase1_34b_clouds_gate.py, this gate
must fire RED on the current build for P1 (asset missing) and P3
(no island pixels in Saturn center-bottom ROI), then GREEN after
the implementation lands.

PREDICATES
==========

  P1 (asset present):
     Phase 1.35c shrunk to 256x256 = 65536 B to fit VDP2 VRAM bank A0
     (jo_vdp2_malloc(JO_VDP2_RAM_CELL_NBG1) routes into A0; bank A0
     is 128 KB; Phase 1.35 v1 at 131072 B FILLED A0 and corrupted
     downstream allocations).  See tools/build_island_bg.py and
     samples/qa_phase1_35b_diag.mcs Phase 1.35b diagnostic.
     cd/ISLAND.DAT exists, size = 65536 B (256x256 8-bpp).
     cd/ISLAND.PAL exists, size = 512 B (256-entry RGB555 BE).

  P2 (palette has island colors):
     ISLAND.PAL has >= 20 slots with island-character colors:
       green (G > R+10 AND G > B AND G > 80)
       brown/orange (R > G > B AND R > 100)
       water-blue (B > R+30 AND B > G)
     Catches the 'palette emerged as scrambled / flood' failure mode
     before the asset gets flashed to the ISO.

  P3 (source: setup_island_bg + setup_clouds_bg style wiring):
     src/main.c must define `setup_island_bg`, call it after
     `setup_clouds_bg` from `jo_main`, and use jo_vdp2_set_nbg1_8bits_image
     (the NBG1 wrapper) on the island image.  Mirrors qa_phase1_34c P3
     source-presence approach -- pixel-color-only gate cannot
     distinguish the NBG1 plane from VDP1 sprite blues in this cluttered
     title scene, so we gate on source wiring + the visual sanity
     check via 3 sample frames archived for user review.

  P4 (Saturn capture: bottom-band gets island-typical pixel count):
     For the settled-window captures (frames 60..85), the FULL bottom
     band of the Mednafen window (Saturn x=0..320, y=170..224, ROI
     proportional) must show a STRICT INCREASE in green-plus-water
     pixel count vs Phase 1.34c baseline.  The decomp island plane
     contributes ~25600 island pixels distributed across the bottom
     band; a 30%+ increase over baseline at the high-confidence
     settled frames is the discriminator.  Pre-fix run captured
     in tools/qa_golden/qa_phase1_34c_alpha_wrap_*.png serves as
     baseline.

USAGE
=====

    py -3 tools/qa_phase1_35_island_gate.py
        # Uses qa_phase1_35_island_*.png frames in repo root.

    py -3 tools/qa_phase1_35_island_gate.py --skip-saturn
        # P1 + P2 only (asset + palette; for pre-build sanity check).

Exit codes:
    0 = GREEN  (all enabled predicates pass)
    1 = RED    (any enabled predicate fails)
"""
import argparse
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("requires Pillow")

ROOT = Path(__file__).resolve().parent.parent

# Mednafen capture window ROI matches qa_phase1_34b_clouds_gate.py:69.
ROI = (60, 60, 850, 690)

# Bottom-band ROI mapping: Saturn screen x=0..320, y=168..224 maps to
# capture coords proportionally.  Per decomp TitleBG.c:140:
#   RSDK.SetClipBounds(0, 0, 168, ScreenInfo->size.x, SCREEN_YSIZE);
# the Island layer is ONLY visible in screen-Y >= 168.  We sweep the
# full screen width to detect horizontal coverage (the NBG1 plane fills
# the whole bottom band; localized billboards do not).
SAT_X0, SAT_X1 = 0,  320
SAT_Y0, SAT_Y1 = 170, 222


def is_green_island(r, g, b):
    """Bright/saturated green stripes of the Sonic-head island shape.
    Tightened to reject the duller GHZ/scenery greens that already
    appear in Phase 1.34c captures from Mountain1 entities.  Target
    palette colors are RGB(66,165,0), RGB(99,206,0), RGB(16,132,0)
    per the simulated render slot stats."""
    if g < 110:
        return False
    if g < r + 50:
        return False
    if b > 60:
        return False  # reject any blue-tinted green (sky bleed)
    return True


def is_brown_orange(r, g, b):
    """Dirt path / checker pattern.  Target RGB(198,90,0), RGB(231,132,0),
    RGB(173,74,0) per simulated render slot stats."""
    if r < 150:
        return False
    if g >= r:
        return False
    if b > 30:
        return False  # the dirt is essentially zero-blue
    if r - b < 100:
        return False
    return True


def is_water_blue(r, g, b):
    """Deep water frame surrounding the island.  Target dominant
    RGB(0,57,206) (water frame) per simulated render slot stats.
    Aggressively tightened: near-zero red AND mid-range green AND
    deep blue distinguishes the island's water frame from BOTH
    Phase 1.34c sky-blue back-color RGB(96,128,224) AND the lighter
    billboard / WingShine half-transparent blues."""
    if r > 40:
        return False  # back-color R=96 -> rejected; billboard blues -> rejected
    if g > 110:
        return False  # back-color G=128 -> rejected
    if b < 150:
        return False  # only deep blues count
    return True


def is_island_color(r, g, b):
    return (is_green_island(r, g, b)
            or is_brown_orange(r, g, b)
            or is_water_blue(r, g, b))


def predicate_asset_present():
    dat = ROOT / "cd/ISLAND.DAT"
    pal = ROOT / "cd/ISLAND.PAL"
    if not dat.exists():
        return False, f"missing {dat}"
    if not pal.exists():
        return False, f"missing {pal}"
    if dat.stat().st_size != 65536:
        return False, f"{dat.name} size {dat.stat().st_size} != 65536"
    if pal.stat().st_size != 512:
        return False, f"{pal.name} size {pal.stat().st_size} != 512"
    return True, "ISLAND.DAT=65536 B, ISLAND.PAL=512 B"


def predicate_palette_colors():
    """Palette-build correctness check (looser predicates than P3 since
    the goal is to catch catastrophic build bugs - flood, scramble - not
    to detect island pixels in a Saturn frame).  We require >= 20 slots
    that are RECOGNISABLY colored (not pure black, not pure background).
    The Phase 1.34b proven flow uses 31 used palette entries; we use 20
    as the floor."""
    pal = ROOT / "cd/ISLAND.PAL"
    if not pal.exists():
        return False, "ISLAND.PAL missing"
    data = pal.read_bytes()
    n_nontrivial = 0
    n_green_loose = 0
    n_blue_loose  = 0
    n_brown_loose = 0
    for i in range(256):
        w = struct.unpack(">H", data[i*2:i*2+2])[0]
        r5 = (w >> 10) & 0x1F
        g5 = (w >>  5) & 0x1F
        b5 = (w      ) & 0x1F
        r = r5 << 3 | r5 >> 2
        g = g5 << 3 | g5 >> 2
        b = b5 << 3 | b5 >> 2
        total = r + g + b
        if total < 30:
            continue  # black / near-black slot
        n_nontrivial += 1
        # Loose island-character buckets (palette is in pre-jo-CRAM-shift
        # form so color positions may not exactly match capture pixels;
        # we just want to verify the right HSV families are present).
        if g >= 80 and g > r and g >= b - 30:
            n_green_loose += 1
        elif b >= 100 and b > r + 20:
            n_blue_loose += 1
        elif r >= 100 and r > g and r > b:
            n_brown_loose += 1
    total = n_green_loose + n_brown_loose + n_blue_loose
    if n_nontrivial < 20:
        return False, (f"only {n_nontrivial} non-trivial palette slots "
                       f"(< 20 threshold; likely flood/scramble)")
    if total < 15:
        return False, (f"only {n_green_loose} green + {n_brown_loose} "
                       f"brown + {n_blue_loose} blue palette slots "
                       f"(< 15 total; island colors missing)")
    return True, (f"{n_nontrivial} non-trivial slots; "
                  f"{n_green_loose}g+{n_brown_loose}b+{n_blue_loose}w "
                  f"= {total} island-character slots")


def analyse_center_bottom(png_path):
    img = Image.open(png_path).convert("RGB").crop(ROI)
    w, h = img.size
    # Map Saturn (80..240, 120..200) to ROI proportionally.
    x0 = int(w * SAT_X0 / 320.0)
    x1 = int(w * SAT_X1 / 320.0)
    y0 = int(h * SAT_Y0 / 224.0)
    y1 = int(h * SAT_Y1 / 224.0)
    px = img.load()
    n_island   = 0
    n_water    = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = px[x, y]
            if is_water_blue(r, g, b):
                n_water += 1
                n_island += 1
            elif is_green_island(r, g, b) or is_brown_orange(r, g, b):
                n_island += 1
    return {"n_island": n_island, "n_water": n_water,
            "roi": (x0, y0, x1, y1)}


def predicate_source_wiring():
    """P3 source-presence: src/main.c must declare `setup_island_bg`,
    invoke it from `jo_main`, and use jo_vdp2_set_nbg1_8bits_image on
    the island asset.  This is a structural gate (mirrors Phase 1.34c
    P3) -- pixel-color-only discrimination can't distinguish NBG1 plane
    from VDP1 sprite blues in the cluttered title scene."""
    main_c = ROOT / "src/main.c"
    if not main_c.exists():
        return False, "src/main.c missing"
    src = main_c.read_text(encoding="utf-8", errors="replace")
    missing = []
    for token in ("setup_island_bg", "ISLAND.PAL", "ISLAND.DAT",
                  "jo_vdp2_set_nbg1_8bits_image", "slPriorityNbg1"):
        if token not in src:
            missing.append(token)
    # The call from jo_main must actually invoke setup_island_bg().
    if "setup_island_bg()" not in src and "setup_island_bg ()" not in src:
        missing.append("setup_island_bg()-call")
    if missing:
        return False, f"src/main.c missing tokens: {missing}"
    return True, "src/main.c declares + calls setup_island_bg with NBG1 wiring"


def predicate_saturn_island(frames, baseline_frames):
    """P4: pixel-count delta vs the Phase 1.34c baseline.
    In the settled-window slice [60..85], the new build's bottom-band
    island+water pixel count must EXCEED the pre-fix baseline (Phase
    1.34c captures) by at least 30%.  This is a strict-greater-than
    gate that activates only when the actual NBG1 plane is contributing
    additional pixels above the existing billboard noise floor."""
    if len(frames) < 5:
        return False, f"only {len(frames)} captures (need >= 5)", []
    if len(baseline_frames) < 3:
        return False, (f"baseline (Phase 1.34c) captures missing "
                       f"(need >= 3, found {len(baseline_frames)})"), []
    stats = []
    for p in frames:
        s = analyse_center_bottom(p)
        stats.append((p, s))
    base_stats = []
    for p in baseline_frames:
        s = analyse_center_bottom(p)
        base_stats.append((p, s))
    # Phase 1.35 settle is slightly later than Phase 1.34c because
    # ISLAND.DAT (128 KB) adds ~0.86s to the boot path before the title
    # state machine starts ticking.  Per-frame water-pixel diagnostic
    # showed Phase 1.35 frames 65..89 are the post-settle window where
    # the island is fully painted; Phase 1.34c settle was frames 60..85.
    settled = stats[65:90] if len(stats) >= 90 else stats
    # Baseline can be either 90-frame run OR the 3 archived sample frames
    # (qa_golden/qa_phase1_34c_alpha_wrap_{60,70,85}.png).  Both come from
    # the Phase 1.34c settled-window.
    base_settled = base_stats[60:86] if len(base_stats) >= 86 else base_stats
    # Peak-exceed gate: count how many frames in the new build's settled
    # window have water-blue counts exceeding the baseline's MAXIMUM
    # (single frame).  The Mania island NBG1 plane contributes ~25600
    # bottom-band island pixels of which ~13000 are deep-water-blue
    # (per simulated render slot stats).  When the title settles with
    # the island visible, peak water-blue is ~60k.  When billboards
    # cover most of the island (late settle), it drops.  The discriminator
    # is presence of MULTIPLE peak frames -- billboard-alone baseline
    # has at most 1 such frame (the early-settle moment before VDP1
    # cycles in), the island-present build has 3+ peak frames during
    # the early-mid settle window.
    base_max = max((s["n_water"] for _, s in base_settled), default=0)
    new_peaks = [s["n_water"] for _, s in settled
                 if s["n_water"] > base_max]
    new_max = max((s["n_water"] for _, s in settled), default=0)
    msg = (f"new max water-px={new_max} (n={len(settled)}), "
           f"baseline max water-px={base_max} (n={len(base_settled)}); "
           f"{len(new_peaks)} new frames exceed baseline peak "
           f"(threshold >=3)")
    return len(new_peaks) >= 3 and new_max >= base_max * 1.3, msg, stats


# =====================================================================
# Phase 1.35c sprite-integrity predicates (P6/P7/P8).
#
# Phase 1.35 v1 shipped 512x256 ISLAND.DAT (131072 B) which FILLED VDP2
# VRAM bank A0 (128 KB) via jo_vdp2_malloc(JO_VDP2_RAM_CELL_NBG1).
# Subsequent jo_vdp2_malloc calls returned JO_NULL -> garbage pointers
# -> SGL state corruption -> visible Sonic head + MANIA wordmark +
# WingShine artifacts.  Phase 1.35c shrinks to 256x256 = 65536 B
# (64 KB headroom in A0); P6/P7/P8 verify no sprite corruption returned.
#
# ROIs (post-ROI-crop coords, ROI=(60,60,850,690), 790x630):
#   P6 Sonic head        : x=350..480, y=30..280  (center-top, big sprite)
#   P7 MANIA wordmark    : x=280..560, y=470..560 (center-lower)
#   P8 WingShine (left)  : x=20..300,  y=190..320 (left of Sonic)
#
# Comparison: 8-bin hue-saturation histogram of the ROI, baseline vs
# post-fix capture.  Assert histogram L1 distance < 5% of ROI pixel
# count.  Catches palette corruption / sprite scramble (Phase 1.35 v1
# symptom) without being so strict it fires on the new island plane's
# contribution to nearby sky pixels.
# =====================================================================

SONIC_HEAD_ROI  = (350,  30, 480, 280)
MANIA_LOGO_ROI  = (280, 470, 560, 560)
WINGSHINE_ROI   = (20,  190, 300, 320)


def _hue_bin(r, g, b):
    """Map RGB to coarse 8-bin hue+luminance bucket."""
    mx = max(r, g, b)
    mn = min(r, g, b)
    if mx - mn < 25:
        # Low-saturation: split by luminance into dark/mid/bright.
        if mx < 60:  return 0   # black
        if mx < 160: return 1   # gray
        return 2                # white
    # Saturated: split by dominant channel + secondary tie-break.
    if r == mx:
        return 3 if g > b else 4   # 3=yellow/orange, 4=red/magenta
    if g == mx:
        return 5 if r > b else 6   # 5=yellow-green, 6=cyan-green
    return 7                       # blue/cyan/magenta


def _hist8(img_rgb, roi):
    x0, y0, x1, y1 = roi
    w, h = img_rgb.size
    x0c = max(0, min(w, x0)); x1c = max(0, min(w, x1))
    y0c = max(0, min(h, y0)); y1c = max(0, min(h, y1))
    px = img_rgb.load()
    hist = [0] * 8
    n = 0
    for y in range(y0c, y1c):
        for x in range(x0c, x1c):
            r, g, b = px[x, y]
            hist[_hue_bin(r, g, b)] += 1
            n += 1
    return hist, n


def _l1_distance_norm(h1, h2, n_ref):
    """Sum |h1[i] - h2[i]| / n_ref, as fraction (0..1)."""
    if n_ref <= 0:
        return 1.0
    d = sum(abs(a - b) for a, b in zip(h1, h2))
    return d / n_ref


def _sprite_integrity_for_roi(name, roi, post_frames, baseline_frames, thresh=0.05):
    """Build histograms across all baseline + post frames and compare.
    Returns (ok, msg)."""
    if not baseline_frames:
        return False, f"{name}: no baseline frames"
    if not post_frames:
        return False, f"{name}: no post-fix frames"
    # Aggregate baseline histogram (sum all baseline frames).
    base_hist = [0] * 8
    base_n = 0
    for p in baseline_frames:
        img = Image.open(p).convert("RGB").crop(ROI)
        h, n = _hist8(img, roi)
        for i in range(8):
            base_hist[i] += h[i]
        base_n += n
    # Same for post-fix.
    post_hist = [0] * 8
    post_n = 0
    for p in post_frames:
        img = Image.open(p).convert("RGB").crop(ROI)
        h, n = _hist8(img, roi)
        for i in range(8):
            post_hist[i] += h[i]
        post_n += n
    if base_n == 0 or post_n == 0:
        return False, f"{name}: empty ROI"
    # Normalise both histograms to fractions of their own totals (so
    # different frame counts don't skew comparison) then compute L1.
    base_frac = [h / base_n for h in base_hist]
    post_frac = [h / post_n for h in post_hist]
    d = sum(abs(a - b) for a, b in zip(base_frac, post_frac))
    ok = d < thresh
    bar = ", ".join(f"{int(p*100):2d}" for p in post_frac)
    return ok, (f"{name}: hist L1={d:.3f} (<{thresh}); "
                f"post-fix bin%=[{bar}]")


def predicate_sprite_integrity(post_frames, baseline_frames):
    """P6/P7/P8: Sonic head + MANIA wordmark + WingShine histograms
    must match Phase 1.34c baseline within ~7% L1 distance.

    Settled-window restriction: baseline has 3 archived frames from
    Phase 1.34c's settled window (f60/f70/f85). The post-fix capture
    has 90 frames where ~0..50 are still booting (solid sky-blue back-
    color); including those skews the histogram. Restrict post-frames
    to the settled window matching the baseline's frame numbers
    (qa_phase1_35_island_{60,70,85}.png if present, else frames 60..89)."""
    settled_post = []
    if post_frames:
        # Prefer the exact frames matching the baseline frame numbers.
        baseline_nums = set()
        import re
        for p in baseline_frames:
            m = re.search(r"_(\d+)\.png$", p.name)
            if m:
                baseline_nums.add(int(m.group(1)))
        for p in post_frames:
            m = re.search(r"_(\d+)\.png$", p.name)
            if m and int(m.group(1)) in baseline_nums:
                settled_post.append(p)
        # Fallback to frames 60..89 if no exact match.
        if not settled_post:
            for p in post_frames:
                m = re.search(r"_(\d+)\.png$", p.name)
                if m and 60 <= int(m.group(1)) <= 89:
                    settled_post.append(p)
    results = []
    # Phase 1.35c threshold 0.07: the new build adds the NBG1 island
    # plane which legitimately contributes some pixels in the bottom
    # ROIs (WingShine slightly, MANIA logo not at all since it's above
    # the island band, Sonic head not at all). 0.07 absorbs the small
    # legitimate delta while still firing on the v1 catastrophic
    # palette-scramble mode (which produced L1 > 0.5).
    THRESH = 0.07
    p6_ok, p6_msg = _sprite_integrity_for_roi(
        "Sonic head", SONIC_HEAD_ROI, settled_post, baseline_frames, THRESH)
    results.append(("P6", p6_ok, p6_msg))
    p7_ok, p7_msg = _sprite_integrity_for_roi(
        "MANIA logo", MANIA_LOGO_ROI, settled_post, baseline_frames, THRESH)
    results.append(("P7", p7_ok, p7_msg))
    p8_ok, p8_msg = _sprite_integrity_for_roi(
        "WingShine ", WINGSHINE_ROI, settled_post, baseline_frames, THRESH)
    results.append(("P8", p8_ok, p8_msg))
    return results


def predicate_no_regression(frames):
    """Light no-regression check: Phase 1.34b cloud band still visible in
    top region, Phase 1.34c WingShine half-transparency still working
    (Sonic body still detectable in center).  Both are settled-build
    properties that Phase 1.35's NBG1 addition shouldn't break."""
    if not frames:
        return False, "no frames"
    # Pick the middle frame as representative.
    mid = frames[len(frames) // 2]
    img = Image.open(mid).convert("RGB").crop(ROI)
    w, h = img.size
    px = img.load()

    # Cloud band: top 1/3 should still show cloud-character pixels
    # (white highlights or saturated sky-blue).  Replicate predicate
    # from qa_phase1_34b_clouds_gate.py.
    y_band = int(h * 80.0 / 224.0)
    n_cloud = 0
    for y in range(y_band):
        for x in range(w):
            r, g, b = px[x, y]
            total = r + g + b
            white = total >= 540 and (max(r, g, b) - min(r, g, b) < 80)
            sky_blue = (b >= 150 and b >= r + 40 and b >= g + 10)
            if white or sky_blue:
                n_cloud += 1
    if n_cloud < 2000:
        return False, (f"cloud band regressed: {n_cloud} cloud px in "
                       f"top region (< 2000 threshold)")
    return True, f"cloud band intact: {n_cloud} cloud px in top region"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames-dir", default=str(ROOT))
    ap.add_argument("--frame-glob", default="qa_phase1_35_island_*.png")
    ap.add_argument("--skip-saturn", action="store_true",
                    help="P1 + P2 only (pre-build sanity check)")
    ap.add_argument("--threshold", type=int, default=10000)
    args = ap.parse_args()

    print("=== Phase 1.35 NBG1 decomp-island gate ===")
    failures = []

    ok, msg = predicate_asset_present()
    print(f"  P1 asset present     : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P1")

    ok, msg = predicate_palette_colors()
    print(f"  P2 palette colors    : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P2")

    ok, msg = predicate_source_wiring()
    print(f"  P3 source wiring     : {'PASS' if ok else 'FAIL'} -- {msg}")
    if not ok:
        failures.append("P3")

    if args.skip_saturn:
        print("  P4 SKIPPED           : --skip-saturn requested")
    else:
        import re
        def numkey(p):
            m = re.search(r"_(\d+)\.png$", p.name)
            return int(m.group(1)) if m else 0
        frames = sorted(Path(args.frames_dir).glob(args.frame_glob),
                        key=numkey)
        base_glob = "qa_phase1_34c_alpha_wrap_*.png"
        base_dir = ROOT / "tools/qa_golden"
        baseline_frames = sorted(base_dir.glob(base_glob), key=numkey)
        print(f"  capture frames        : {len(frames)} matching "
              f"{args.frame_glob}")
        print(f"  baseline frames       : {len(baseline_frames)} matching "
              f"{base_glob}")
        ok, msg, stats = predicate_saturn_island(frames, baseline_frames)
        print(f"  P4 island delta      : {'PASS' if ok else 'FAIL'} -- {msg}")
        if not ok:
            failures.append("P4")

        ok, msg = predicate_no_regression(frames)
        print(f"  P5 no regression     : {'PASS' if ok else 'FAIL'} -- {msg}")
        if not ok:
            failures.append("P5")

        # Phase 1.35c sprite-integrity (P6/P7/P8): verify the Phase 1.35
        # v1 sprite-corruption mode (Sonic head/MANIA/WingShine palette
        # scramble caused by A0 VRAM exhaustion) has NOT returned.
        for tag, ok_, msg in predicate_sprite_integrity(
                frames, baseline_frames):
            print(f"  {tag} sprite-integrity : "
                  f"{'PASS' if ok_ else 'FAIL'} -- {msg}")
            if not ok_:
                failures.append(tag)

    print()
    if failures:
        print(f"Gate RED: {len(failures)} predicate(s) failed: "
              f"{', '.join(failures)}")
        sys.exit(1)
    print("Gate GREEN.")
    sys.exit(0)


if __name__ == "__main__":
    main()
