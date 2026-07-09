#!/usr/bin/env python3
"""qa_chain_landing_sprites.py - F1-R1 landing-leg player-sprite continuity gate.

MEASURED RED baseline (2026-07-03, F1 build _f1__*.png): in the post-handoff
playable Green Hill Zone leg, the players render on only ~70% of 1 Hz shots.
Mechanism (savestate-proven): SGL's still-alive slave sprite pipeline re-transfers
its empty plan (preamble + END at 0x60) each slSynch, stomping the direct-list
trampoline until the next vblank re-patch. Fix = frame-top re-patch in
p6_frontend_frame (runs right after slSynch).

Detector: RED SHOE pixels (r>170, g<80, b<80) inside the fixed player rect
(window 250,430 - 400,525). The landing camera is static post-handoff and the
idle players sit in that rect; nothing else in the crop is red (cliff is dark
brown, flowers purple, water blue, grass green). Validated against the viewed
montage _f1_montage.png ground truth (17/24 present).

SCOPE (2026-07-03 hardening): this gate measures RENDER CONTINUITY only (a
player rendered on >= min-ratio of shots). It does NOT prove both characters:
the shoe term can pass on Tails alone, and every color-mass detector tried for
Sonic-body identification failed validation (sonic-blue matched sky,
tails-orange matched cliffs, diff-vs-ref matched water animation). Per-character
presence is proven STRUCTURALLY by tools/qa_chain_player_pose.py (both players'
animator.frames inside the GHZANIM.PAK frames arena in a landing savestate) +
a personally viewed keyframe. Run both; never re-add a color identity term
without montage validation.

Usage:
    python tools/qa_chain_landing_sprites.py PREFIX START END [--min-ratio 0.9] [--per-shot]

Exit 0 = GREEN (present-ratio >= min-ratio), 1 = RED, 2 = no frames.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from PIL import Image

CROP = (250, 430, 400, 525)


def red_shoes(path: Path) -> int:
    im = Image.open(path).convert("RGB").crop(CROP)
    return sum(1 for r, g, b in im.getdata() if r > 170 and g < 80 and b < 80)


def main(argv=None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("prefix")
    p.add_argument("start", type=int)
    p.add_argument("end", type=int)
    p.add_argument("--min-ratio", type=float, default=0.9)
    p.add_argument("--min-red", type=int, default=6)
    p.add_argument("--per-shot", action="store_true")
    a = p.parse_args(argv)

    present = absent = 0
    for i in range(a.start, a.end + 1):
        f = Path(f"{a.prefix}_{i}.png")
        if not f.exists():
            continue
        n = red_shoes(f)
        ok = n >= a.min_red
        present += ok
        absent += not ok
        if a.per_shot:
            print(f"  {f.name}: red={n} {'P' if ok else '-'}")
    total = present + absent
    if not total:
        sys.stderr.write("qa_chain_landing_sprites: no frames matched\n")
        return 2
    ratio = present / total
    # Continuity only; per-character identity comes from qa_chain_player_pose
    # (savestate arena check) -- see SCOPE note in the module docstring.
    ok_all = ratio >= a.min_ratio
    verdict = "GREEN" if ok_all else "RED"
    print(f"qa_chain_landing_sprites: shoes {present}/{total} ratio={ratio:.3f} "
          f"(min {a.min_ratio}) -> {verdict}")
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
