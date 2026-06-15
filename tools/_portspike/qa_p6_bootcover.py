#!/usr/bin/env python3
# Boot/load cover gate: a load-phase frame must be a CLEAN solid back-color, not
# the red/green VDP2 static (NBG1 displaying half-written VRAM). Metric = distinct
# color count: a solid screen has a handful of colors (back-color + window chrome);
# the static has thousands. GREEN if distinct colors <= threshold for the frame.
#
# Usage: qa_p6_bootcover.py <load_phase.png> [max_distinct_colors]
#   RED  (current build): load frame is garbage static -> thousands of colors.
#   GREEN (after fix):     load frame is a solid cover  -> few colors.
import sys

try:
    from PIL import Image
except ImportError:
    print("RED: Pillow (PIL) not available -- cannot measure the frame.")
    sys.exit(2)


def main(argv):
    if len(argv) < 2:
        print("usage: qa_p6_bootcover.py <png> [max_distinct=64]")
        return 2
    path = argv[1]
    max_distinct = int(argv[2]) if len(argv) > 2 else 64
    im = Image.open(path).convert("RGB")
    # Crop out the window title bar (top ~16px of the mednafen window chrome) so
    # we measure the emulated framebuffer, not the OS window decoration.
    w, h = im.size
    im = im.crop((2, 18, w - 2, h - 2))
    colors = im.getcolors(maxcolors=1 << 24)  # list of (count, (r,g,b))
    distinct = len(colors) if colors else (1 << 24)
    # dominant-color coverage: a clean cover is mostly one color.
    total = im.size[0] * im.size[1]
    top = max(c for c, _ in colors) if colors else 0
    dom_frac = top / total if total else 0.0
    print("-" * 56)
    print("  frame              : %s (%dx%d measured)" % (path, im.size[0], im.size[1]))
    print("  distinct colors    : %d  (threshold <= %d)" % (distinct, max_distinct))
    print("  dominant coverage  : %.1f%%" % (dom_frac * 100.0))
    print("-" * 56)
    clean = distinct <= max_distinct and dom_frac >= 0.80
    if clean:
        print("RESULT: GREEN -- load frame is a clean solid cover (no VDP2 static).")
        return 0
    print("RESULT: RED -- load frame is garbage static (%d colors, dominant %.1f%%)."
          % (distinct, dom_frac * 100.0))
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
