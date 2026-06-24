#!/usr/bin/env python3
# =============================================================================
# qa_montage.py -- tile a numbered PNG burst into ONE contact-sheet image so the
# whole boot->title arc can be seen in a single Read (the user's "take multiple
# screenshots a second for the WHOLE load so you can see everything" requirement).
#
# The qa_boot.ps1 burst writes <base>_001.png .. <base>_NNN.png. This montages
# them in capture order into a labelled grid (frame index + a 1-line per-frame
# luminance/colour readout under each thumb) and saves one PNG.
#
# Usage:
#   python tools/_portspike/qa_montage.py "_arc_shot_*.png" [out.png] [cols]
#   python tools/_portspike/qa_montage.py "_arc_shot_*.png" _arc_montage.png 10
# =============================================================================
import glob
import os
import sys

from PIL import Image, ImageDraw

import numpy as np


def _idx(p):
    s = os.path.basename(p).rsplit("_", 1)[-1].split(".")[0]
    return int(s) if s.isdigit() else 0


def _readout(a):
    """One-line per-frame classification to label the thumb: mean luma + a coarse
    tag (BLACK / WHITE / BLUE / CONTENT) so the intro phases are scannable."""
    r, g, b = a[..., 0].mean(), a[..., 1].mean(), a[..., 2].mean()
    luma = 0.299 * r + 0.587 * g + 0.114 * b
    if luma < 24:
        tag = "BLACK"
    elif luma > 200:
        tag = "WHITE"
    elif b > r + 25 and b > g + 12:
        tag = "BLUE"
    else:
        tag = "CONTENT"
    return luma, tag


def main(argv):
    if not argv:
        print("usage: qa_montage.py <glob> [out.png] [cols]")
        return 2
    pat = argv[0]
    out = argv[1] if len(argv) > 1 else "_montage.png"
    cols = int(argv[2]) if len(argv) > 2 else 10
    root = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    pngs = sorted(glob.glob(os.path.join(root, pat)), key=_idx)
    if not pngs:
        print("RESULT: no PNGs match %s" % pat)
        return 1

    tw, th, lab = 200, 150, 16  # thumb w/h + label-strip height
    rows = (len(pngs) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * tw, rows * (th + lab)), (20, 20, 20))
    draw = ImageDraw.Draw(sheet)
    print("montage: %d frames, %dx%d grid" % (len(pngs), cols, rows))
    for i, p in enumerate(pngs):
        im = Image.open(p).convert("RGB")
        a = np.asarray(im).astype(np.float32)
        luma, tag = _readout(a)
        im = im.resize((tw, th))
        cx, cy = (i % cols) * tw, (i // cols) * (th + lab)
        sheet.paste(im, (cx, cy + lab))
        draw.rectangle([cx, cy, cx + tw - 1, cy + lab - 1], fill=(0, 0, 0))
        draw.text((cx + 2, cy + 3), "%03d %s L%3d" % (_idx(p), tag, int(luma)),
                  fill=(255, 255, 0))
        print("  %03d  %-7s luma=%5.1f  %s" % (_idx(p), tag, luma, os.path.basename(p)))
    outp = os.path.join(root, out)
    sheet.save(outp)
    print("RESULT: wrote %s (%dx%d)" % (outp, sheet.width, sheet.height))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
