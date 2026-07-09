#!/usr/bin/env python3
"""
qa_chain_menu_clean.py -- #314 punch-list 1 RED/GREEN gate: menu row-art garble
in the FULL-CHAIN build (P6_GHZCUT_BOOT + P6_FRONTEND_CHAIN).

MECHANISM (measured, the M1b class): the chain build compiles p6_vdp1.c with the
GHZCUT bucket table, which (pre-fix) had NO bucket for the menu's wide-flat
working set (~10 MainIcons rects 88-120w x 38-44h + 4-6 TextEN labels 86-176w
x 22 + the 176w Sound label = ~14-16 distinct rects/frame). They piled into the
160x160 bucket (8 slots) + the 1-slot catch-all -> intra-frame LRU reuse ->
p6_title_restage_content mutates a jo_id's __jo_sprite_def CMDSIZE after earlier
same-id draws were queued -> the torn-band garble on every row icon (BACK/
CONFIRM text + the hand cursor draw LAST and win the LRU -> stay clean).

GATE SIGNALS (calibrated 2026-07-02; RE-CALIBRATED same day with MEASURED clean-
menu data from the fixed build's _v__113/121/129.png):
  region = menu rows+preview band (x 3-93%, y 15-98% of the sub-chrome frame)
    saturated-RED fraction:  garbled _chain__46.png = 4.12%  | CLEAN menu
        (fixed build) = 1.01%; clean busy cutscene art = 0.66-0.85%
        -> threshold 2.0% (4x separation both ways)
    near-WHITE fraction: NOT discriminating for the MENU (garbled 9.73% vs
        clean 9.63% -- the menu's big white headline text confounds it).
        Reported informationally, NOT part of the verdict.
  left rows band (x 3-55%): YELLOW backdrop (240,200,0) integrity:
        garbled = 51.1% | clean menu = 73.0% | flat strip = 99.98%
        -> threshold 55%

PASS (clean) iff red<=2.0% AND yellow>=55% (both fire on the RED reference).
--expect garbled : RED-baseline proof (exit 0 iff the frame IS garbled).

Usage: python tools/qa_chain_menu_clean.py FRAME.png [...] [--expect garbled|clean]
Exit 0 iff ALL frames match the expectation.
"""
import sys
import numpy as np
from PIL import Image

RED_MAX_PCT    = 2.0
WHITE_MAX_PCT  = 5.0
YELLOW_MIN_PCT = 55.0


def measure(path):
    im = np.asarray(Image.open(path).convert("RGB")).astype(int)
    H, W = im.shape[:2]
    g = im[34:H - 8, 8:W - 8]          # strip OS window chrome
    gh, gw = g.shape[:2]
    band = g[int(gh * 0.15):int(gh * 0.98), int(gw * 0.03):int(gw * 0.93)]
    r, gr, b = band[..., 0], band[..., 1], band[..., 2]
    tot = band.shape[0] * band.shape[1]
    red = 100.0 * ((r > 180) & (gr < 70) & (b < 70)).sum() / tot
    white = 100.0 * ((r > 220) & (gr > 220) & (b > 220)).sum() / tot
    left = g[int(gh * 0.15):int(gh * 0.98), int(gw * 0.03):int(gw * 0.55)]
    lr, lg, lb = left[..., 0], left[..., 1], left[..., 2]
    yellow = 100.0 * ((lr > 190) & (lg > 140) & (lb < 90)).mean()
    return red, white, yellow


def main(argv):
    expect = "clean"
    if "--expect" in argv:
        i = argv.index("--expect")
        expect = argv[i + 1]
        del argv[i:i + 2]
    if not argv:
        print("usage: qa_chain_menu_clean.py FRAME.png [...] [--expect garbled|clean]")
        return 2
    ok = True
    for p in argv:
        red, white, yellow = measure(p)
        clean = (red <= RED_MAX_PCT) and (yellow >= YELLOW_MIN_PCT)
        verdict = "CLEAN" if clean else "GARBLED"
        want = (verdict == "CLEAN") if expect == "clean" else (verdict == "GARBLED")
        if not want:
            ok = False
        print(f"qa_chain_menu_clean: {p}  red={red:.2f}% (<= {RED_MAX_PCT})  "
              f"white={white:.2f}% (info only)  yellow={yellow:.1f}% (>= {YELLOW_MIN_PCT})"
              f"  -> {verdict}  [{'PASS' if want else 'FAIL'} vs expect={expect}]")
    print(f"qa_chain_menu_clean: {'GREEN' if ok else 'RED'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
