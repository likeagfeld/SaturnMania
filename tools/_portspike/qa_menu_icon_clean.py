#!/usr/bin/env python3
# =============================================================================
# qa_menu_icon_clean.py -- M1b ICON-CLEANLINESS gate (RED-first, this session).
#
# The M1b qa_engine_menu_render.py gate is GREEN (tree built + UIModeButton live +
# VDP1 landed + non-black backdrop) yet the BROKEN build showed the MAINICON
# icon/shadow sprites as RED/BLUE/WHITE HORIZONTAL-STRIPED BANDS. That gate measures
# PROXIES (sprites landed, non-black mass) uncorrelated with the on-screen icon
# symptom (memory: engine-render-vs-handport-rootcauses #4 -- a rendering bug's gate
# is the rendered frame, with a quantitative pixel measure).
#
# MEASURED root cause (this session): the build flag P6_FRONTEND_MENU was NOT passed
# to the jo-make compile of p6_vdp1.c (the Makefile had no `ifeq ($(P6_FRONTEND_MENU))`
# block, and build_shipping.sh's `make` line did not forward it). So that TU compiled
# the TITLE bucket-count branch (b0/b1=6/6) instead of the MENU branch (b1=18). The
# UIModeButton rows draw ~10 MAINICON icon/shadow rects + ~4-6 mode-text rects PER FRAME,
# all 88-148px wide / h<=44 -> ALL route to bucket b1 (192x64). With only 6 b1 slots the
# per-frame working set (~16) overflowed every frame, so the LRU EVICTED +
# p6_title_restage_content RE-DMA'd a slot's jo_id + mutated its __jo_sprite_def CMDSIZE
# MID-FRAME, after an earlier same-id draw command was already queued -> VDP1 rasterised
# the FINAL (wrong) rect for the earlier icon draws = the striped garble (the TEXT, drawn
# LAST per row in UIModeButton_Draw, won the LRU and stayed clean). Fix: forward
# P6_FRONTEND_MENU to the jo compile + size b1=18 (> demand). MEASURED p6_w_vdp1_evicts
# 37,388 -> 0.
#
# THE PIXEL METRIC -- VERTICAL R<->B FLIP FRACTION (the striped-band fingerprint):
#   The garble is THIN (1-2px) alternating saturated-RED / saturated-BLUE rows. So a
#   saturated pixel's VERTICAL neighbour is frequently the OPPOSITE saturated colour.
#   A CLEAN Mania icon is legitimately colourful (blue Sonic head, red Knuckles, red
#   competition splat) BUT those are SOLID BLOBS -- a blue pixel's vertical neighbour is
#   blue or transparent, almost never red. So the discriminator is NOT "presence of
#   red+blue" (the original gate's flaw -- clean icons ARE red+blue) but the VERTICAL
#   STRIPING: count vertical-adjacent pixel pairs where BOTH are saturated R/B, and the
#   FRACTION of those that FLIP colour (R-over-B or B-over-R). MEASURED:
#       RED (striped) frames  : flipfrac 0.0219 .. 0.0390
#       GREEN (clean)  frames : flipfrac 0.0070 (every frame, 179/25545 -- deterministic)
#   Threshold 0.015 cleanly separates them. RED when flipfrac is high (striped); GREEN
#   when the icons are coherent colour blobs.
#
#   python tools/_portspike/qa_menu_icon_clean.py <frame.png>
#   python tools/_portspike/qa_menu_icon_clean.py            # uses the default RED cap
# =============================================================================
import os, sys

GOLD_RGB = (0xF0, 0xC8, 0x00)   # UIBackground main-menu backdrop (decomp 0xF0C800)

# MEASURED separation: RED >= 0.0219, GREEN = 0.0070. 0.015 is the midpoint margin.
FLIPFRAC_MAX_GREEN = 0.015
# Sanity floor: a frame with almost no saturated pixels (e.g. a still-loading blue
# splash, not the gold menu) is NOT a valid menu capture -- require a minimum of
# saturated vertical pairs so the fraction is meaningful (the clean menu has ~25k).
SATPAIRS_MIN = 4000


def is_gold(r, g, b):
    return abs(r - GOLD_RGB[0]) < 40 and abs(g - GOLD_RGB[1]) < 40 and b < 70


def cls(r, g, b):
    """Classify a pixel: 'R' saturated red, 'B' saturated blue, else '.'/'G'."""
    if is_gold(r, g, b):
        return '.'
    if r > 150 and g < 110 and b < 110:
        return 'R'
    if b > 140 and r < 130 and (b - r) > 40:
        return 'B'
    return '.'


def measure(png):
    from PIL import Image
    im = Image.open(png).convert("RGB")
    w, h = im.size
    px = im.load()
    flips = 0          # vertical-adjacent R<->B colour flips (the stripe fingerprint)
    satpairs = 0       # vertical-adjacent pairs where BOTH pixels are saturated R/B
    prev = [cls(*px[x, 0]) for x in range(w)]
    for y in range(1, h):
        cur = [cls(*px[x, y]) for x in range(w)]
        for x in range(w):
            a = prev[x]; b = cur[x]
            if a in 'RB' and b in 'RB':
                satpairs += 1
                if a != b:
                    flips += 1
        prev = cur
    frac = (flips / satpairs) if satpairs else 0.0
    return flips, satpairs, frac


def main(argv):
    png = argv[0] if argv else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), "..", "..", "_menuicon_RED_7.png")
    png = os.path.normpath(png)
    if not os.path.exists(png):
        print("FAIL: no PNG at %s" % png); return 2
    try:
        flips, satpairs, frac = measure(png)
    except Exception as e:
        print("FAIL: %s" % e); return 2
    print("=" * 64)
    print("M1b ICON-CLEAN -- MAINICON icons are coherent colour blobs, not striped")
    print("=" * 64)
    print("  PNG: %s" % png)
    print("  vertical-adjacent saturated R/B pairs = %d" % satpairs)
    print("  of those, R<->B colour FLIPS (the stripe fingerprint) = %d" % flips)
    print("  FLIP FRACTION = %.4f" % frac)
    if satpairs < SATPAIRS_MIN:
        print("-" * 64)
        print("RESULT: RED -- only %d saturated R/B pairs (< %d). Not a valid SETTLED "
              "menu capture (still loading / wrong frame). Capture later." % (satpairs, SATPAIRS_MIN))
        return 1
    ok = frac <= FLIPFRAC_MAX_GREEN
    print("  [%s] FLIP FRACTION %.4f <= %.4f (clean-blob ceiling; striped >= 0.0219)"
          % ("GREEN" if ok else " RED ", frac, FLIPFRAC_MAX_GREEN))
    print("-" * 64)
    if ok:
        print("RESULT: GREEN -- the MAINICON icons render as coherent colour blobs (no "
              "red/blue horizontal-band striping). OPEN the PNG to confirm each row's icon "
              "is a clean Mania sprite (character heads / stopwatch / VS splat) + white label.")
        return 0
    print("RESULT: RED -- the MAINICON icon region shows the striped red/blue/white "
          "horizontal-band garble (mid-frame VDP1 slot restage thrash: P6_FRONTEND_MENU "
          "not reaching p6_vdp1.c -> b1 = 6 slots << the ~16 per-frame demand).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
