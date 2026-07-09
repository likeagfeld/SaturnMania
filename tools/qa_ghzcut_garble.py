#!/usr/bin/env python3
"""
qa_ghzcut_garble.py -- Task #311 RED gate: Heavy sprite garble in the
GHZCutscene direct-boot captures.

SIGNAL (measured 2026-07-02 on build 13 _skyra_*.png): the garble strips
contain magenta-family pixels (the sprite-sheet transparent filler 0xFC1F ~
(255,0,255)) that clean Green Hill Zone cutscene art NEVER shows. Counts on
the broken build: 417 / 8064 / 14079 / 3981 across settled frames 24/16/32/40
-- vs ~0 expected clean. ROOT CAUSE (measured): 11.5-28.1% of every Heavy's
packed-frame pixels index >=128, but HBHPAL.BIN loads only 128 colors per
256-entry CRAM block, so those pixels read junk CRAM. On PC the >=128 half is
CutsceneHBH_SetupPalettes' hardcoded tempPal tables written to stage indices
128-255 (decomp CutsceneHBH.c:195 + the colorSet tables at :84-163).

PASS: magenta-family count < THRESH in the given frame.
Usage: python tools/qa_ghzcut_garble.py FRAME.png [FRAME2.png ...]
Exit 0 iff ALL given frames pass.
"""
import sys
import numpy as np
from PIL import Image

THRESH = 100


def measure(path):
    im = np.asarray(Image.open(path).convert("RGB")).astype(int)
    r, g, b = im[..., 0], im[..., 1], im[..., 2]
    return int(((r > 200) & (b > 200) & (g < 90)).sum())


def main(argv):
    if not argv:
        print("usage: qa_ghzcut_garble.py FRAME.png [...]")
        return 2
    worst = -1
    ok = True
    for p in argv:
        n = measure(p)
        verdict = "PASS" if n < THRESH else "FAIL"
        if n >= THRESH:
            ok = False
        worst = max(worst, n)
        print(f"qa_ghzcut_garble: {p}  magenta-family px = {n}  (thresh {THRESH})  -> {verdict}")
    print(f"qa_ghzcut_garble: worst = {worst}  -> {'GREEN' if ok else 'RED'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
