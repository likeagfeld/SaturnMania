#!/usr/bin/env python3
"""Phase 2.4j.1 runtime visual gate -- TitleCard polygon BG must render.

The TitleCard SHOWING-hold state draws a FULL-SCREEN yellow background rect
(src/mania/Objects/Global/TitleCard.c:667, TC_YELLOW = 0xF0C800) via the SGL
slPutPolygon emitter (src/rsdk/drawing.c rsdk_draw_rect). If the polygon path
back-face-culls the card (Single_Plane + degenerate {0,0,0} normal), NO card
pixels reach the screen and the GHZ gameplay scene shows through for the whole
title-card window.

This gate scans a set of boot captures (tc_*.png, captured -Wait 25 -Every 0.5
so frame i = 25 + (i-1)*0.5 s -- the GHZ act-intro window) and asserts that AT
LEAST ONE frame is predominantly the card yellow. On the current (culled) build
no frame is yellow-dominant -> RED. After the Dual_Plane fix the SHOWING hold
paints the screen yellow -> GREEN.

Usage:
  python tools/qa_phase2_4j1_titlecard_runtime_gate.py tc_*.png
  python tools/qa_phase2_4j1_titlecard_runtime_gate.py --glob "tc_*.png"

Exit 0 = GREEN (card yellow BG observed), exit 1 = RED (never observed).
"""
import sys
import glob as globmod

try:
    from PIL import Image
except ImportError:
    sys.stderr.write("PIL/Pillow required (pip install pillow)\n")
    sys.exit(2)

# TC_YELLOW = 0xF0C800 -> R=240 G=200 B=0. After RGB888->RGB555->display the
# channels round but stay "high R, high G, very low B". Match generously on the
# ratio rather than exact values so emulator/scaler rounding doesn't break it.
def is_card_yellow(r, g, b):
    return r >= 200 and g >= 150 and b <= 90 and (int(r) - int(b)) >= 130

# A frame counts as "card BG visible" when the yellow fraction dominates. The
# GHZ scene has small yellow accents (sun totems ~ a few %); the full-screen
# card BG covers the playfield. Threshold well above scene-accent noise.
DOMINANCE = 0.45  # >=45% of sampled pixels are card-yellow


def yellow_fraction(path):
    im = Image.open(path).convert("RGB")
    w, h = im.size
    # Sample on a grid to keep it fast and scaler-robust.
    step = max(1, min(w, h) // 120)
    px = im.load()
    total = 0
    hit = 0
    for y in range(0, h, step):
        for x in range(0, w, step):
            r, g, b = px[x, y]
            total += 1
            if is_card_yellow(r, g, b):
                hit += 1
    return (hit / total) if total else 0.0


def main():
    args = [a for a in sys.argv[1:]]
    files = []
    i = 0
    while i < len(args):
        if args[i] == "--glob":
            files.extend(sorted(globmod.glob(args[i + 1])))
            i += 2
        else:
            files.append(args[i])
            i += 1
    if not files:
        files = sorted(globmod.glob("tc_*.png"))
    if not files:
        sys.stderr.write("no capture frames supplied / found\n")
        sys.exit(2)

    best = 0.0
    best_file = None
    for f in files:
        try:
            frac = yellow_fraction(f)
        except Exception as e:  # noqa
            sys.stderr.write("skip %s: %s\n" % (f, e))
            continue
        if frac > best:
            best = frac
            best_file = f

    print("frames scanned : %d" % len(files))
    print("peak yellow    : %.3f  (%s)" % (best, best_file))
    print("threshold      : %.3f" % DOMINANCE)
    if best >= DOMINANCE:
        print("GREEN: TitleCard yellow BG rendered (polygon path emits on-screen)")
        sys.exit(0)
    print("RED: no yellow-dominant frame -- card polygons not rendering")
    sys.exit(1)


if __name__ == "__main__":
    main()
