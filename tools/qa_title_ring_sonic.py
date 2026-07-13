#!/usr/bin/env python3
"""qa_title_ring_sonic.py -- VISUAL fidelity gate for the Title ring emblem.

Measures the ring-emblem INTERIOR (the hole in the gold ring) of a settled-
title screenshot. On PC Sonic Mania the ring hole is filled by the TitleSonic
head + gloved finger-wave (peach muzzle, red/blue body, white gloves). On the
broken Saturn build the hole is a solid-black void (the emblem's own opaque
black interior disc, drawn on top of / instead of the head).

Per the sega-saturn-developer skill gotcha #4: a VISUAL bug's gate MUST measure
the RENDERED PIXEL, never a code proxy (visible flag / handle bound). The prior
"fix" went GREEN on witness-only proxies while the head stayed invisible.

RED  (bug present): ring hole nearly all-black  -> Sonic missing.
GREEN (fixed):       ring hole has Sonic's head  -> non-black pixels + color
                     diversity above thresholds.

Ground truth (docs/title_ground_truth.md + parse_title_entities.py):
  EMBLEM  world(256,108) slot 5  (opaque black interior disc, drawGroup 4)
  TitleSonic world(252,104) slot 8 (head, drawGroup 4, drawn AFTER emblem)
  camera_x = 96 -> Sonic screen origin (156,104), frame48 pivot(-50,-91),
  size 110x120 -> covers screen x[106..216] y[13..133]; the ring hole
  (screen ~x[146..178] y[50..82]) is inside that bbox.

Usage:  python tools/qa_title_ring_sonic.py <settled_title.png>
Exit 0 = GREEN (Sonic present), 1 = RED (Sonic missing), 2 = usage error.
"""
import sys

try:
    from PIL import Image
    import numpy as np
except Exception as e:  # pragma: no cover
    print(f"qa_title_ring_sonic: needs Pillow+numpy ({e})", file=sys.stderr)
    sys.exit(2)

# --- thresholds (measured on the RED build game-260712-221551.png) -----------
# RED baseline: mean luma 4.68, non-black 4.8%, 7 unique colors.
# A rendered Sonic head fills the hole with peach/red/blue/white -> expect
# >= 30% non-black pixels and >= 20 distinct colors. Set the pass bar between.
MIN_NONBLACK_PCT = 20.0   # % of hole pixels with luma > 40
MIN_UNIQUE_COLORS = 16    # distinct RGB tuples in the hole
LUMA_BLACK = 40

# ring-hole bbox in 320x224 SCREEN space (scaled to the actual image W/H)
HOLE_X0, HOLE_X1 = 146, 178
HOLE_Y0, HOLE_Y1 = 50, 82


def measure(path):
    im = Image.open(path).convert("RGB")
    W, H = im.size
    a = np.array(im)
    sx, sy = W / 320.0, H / 224.0
    x0, x1 = int(HOLE_X0 * sx), int(HOLE_X1 * sx)
    y0, y1 = int(HOLE_Y0 * sy), int(HOLE_Y1 * sy)
    crop = a[y0:y1, x0:x1]
    luma = 0.299 * crop[:, :, 0] + 0.587 * crop[:, :, 1] + 0.114 * crop[:, :, 2]
    area = crop.shape[0] * crop.shape[1]
    nonblack = int(np.count_nonzero(luma > LUMA_BLACK))
    pct = 100.0 * nonblack / area if area else 0.0
    uniq = len(set(map(tuple, crop.reshape(-1, 3))))
    return dict(bbox=(x0, x1, y0, y1), area=area, mean_luma=float(luma.mean()),
                nonblack=nonblack, pct=pct, uniq=uniq, size=(W, H))


def main():
    if len(sys.argv) != 2:
        print("usage: qa_title_ring_sonic.py <settled_title.png>", file=sys.stderr)
        return 2
    m = measure(sys.argv[1])
    print(f"qa_title_ring_sonic: img={m['size']} hole-bbox={m['bbox']} "
          f"area={m['area']}")
    print(f"  mean_luma={m['mean_luma']:.2f} non-black={m['nonblack']} "
          f"({m['pct']:.1f}%) unique_colors={m['uniq']}")
    ok = m["pct"] >= MIN_NONBLACK_PCT and m["uniq"] >= MIN_UNIQUE_COLORS
    if ok:
        print(f"  GREEN: Sonic head present in ring hole "
              f"(>= {MIN_NONBLACK_PCT}% non-black, >= {MIN_UNIQUE_COLORS} colors)")
        return 0
    print(f"  RED: ring hole is a black void -- TitleSonic head MISSING "
          f"(need >= {MIN_NONBLACK_PCT}% non-black AND >= {MIN_UNIQUE_COLORS} colors)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
