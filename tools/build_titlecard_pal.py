#!/usr/bin/env python3
"""build_titlecard_pal.py -- emit cd/DISPCARD.BIN, the Global/Display.gif 256-color
palette used by the TitleCard zone-name GLYPHS (GL1, frontend_full_chain_parity
scene-6 row).

The TitleCard glyphs (Name Letters / Zone Letters / Act Numbers, all frames of
Global/TitleCard.bin) live on the Global/Display.gif sheet (the HUD sheet). On
Saturn those frames draw from the already-staged cd/DISPLAY.SHT as VDP1 8bpp
sprites; a VDP1 8bpp sprite reads CRAM[colno + pixel] (ST-013-R3 sec 6.4 +
ST-058-R2 sec 10.1, Type-3 full-11-bit DC, SPCAOS=0). The glyph pixels index the
Display GCT (MEASURED indices {1,16,18,32..41,255}), so the Display GCT must sit
in the CRAM block the glyph blits select.

At the GHZ landing the card routes glyphs to CRAM block 2 (colno=512) -- the
first of the 5 GHZCutscene-Heavy blocks, which are UNUSED at the landing (the
Heavies exit in the GHZCutscene). p6_vdp2_titlecard_pal_upload copies this blob
straight to CRAM[512..767].

Byte format MIRRORS cd/PLRPAL.BIN exactly (build_player_atlas.py:363): 256 entries,
each a big-endian u16 = 0x8000 | (b5<<10)|(g5<<5)|r5 (BGR555 + transparency bit).
The SH-2 reads it native big-endian; entry 0 is the VDP1 transparent slot.

Usage: python tools/build_titlecard_pal.py
Writes: cd/DISPCARD.BIN (512 bytes).
"""
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "extracted", "Data", "Sprites", "Global", "Display.gif")
OUT = os.path.join(ROOT, "cd", "DISPCARD.BIN")


def rgb888_to_bgr555(r, g, b):
    r5, g5, b5 = r >> 3, g >> 3, b >> 3
    return 0x8000 | (b5 << 10) | (g5 << 5) | r5


def main():
    try:
        from PIL import Image
    except Exception:
        sys.stderr.write("build_titlecard_pal: Pillow (PIL) required\n")
        return 2
    if not os.path.exists(SRC):
        sys.stderr.write(f"build_titlecard_pal: missing {SRC}\n")
        return 2
    im = Image.open(SRC)
    if im.mode != "P":
        sys.stderr.write(f"build_titlecard_pal: expected palettized GIF, got mode {im.mode}\n")
        return 2
    pal = im.getpalette()  # flat [r,g,b, r,g,b, ...], up to 256 triples
    ncol = len(pal) // 3
    out = bytearray()
    for i in range(256):
        if i == 0:
            # RSDK index 0 = the transparent color (magenta 0xFF00FF in the GCT).
            # A VDP1 8bpp sprite skips pixel 0, so this CRAM slot is normally never
            # sampled -- BUT the glyph is staged into a fixed 64x64 slot whose
            # PADDING is index 0, and (MEASURED _shots/222629) the pad reads as its
            # CRAM colour. The player palette (PLRPAL.BIN) sets index 0 to a dark
            # 0x83C0 for exactly this reason; the source magenta made the glyph pad
            # bright-magenta. Force index 0 = transparent-black (0x8000) so the pad
            # is inconspicuous, matching the codebase's transparent-slot convention.
            out += struct.pack(">H", 0x8000)
            continue
        if i < ncol:
            r, g, b = pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]
        else:
            r = g = b = 0
        out += struct.pack(">H", rgb888_to_bgr555(r, g, b))
    with open(OUT, "wb") as f:
        f.write(out)
    print("DISPCARD.BIN  %6d B  (Display.gif GCT, 256 BGR555 -> CRAM[512..767], block 2)"
          % len(out))
    # Report the glyph-relevant indices so a regression is visible in the log.
    for i in (1, 16, 18, 32, 33, 34, 35, 37, 38, 39, 41, 255):
        if i < ncol:
            r, g, b = pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2]
            print("  idx %3d = (%3d,%3d,%3d)  BGR555=0x%04X" % (i, r, g, b, rgb888_to_bgr555(r, g, b)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
