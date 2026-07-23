#!/usr/bin/env python3
"""build_boot_splash.py -- build cd/BOOTSPL.BIN, the boot-window splash bitmap.

User feature (2026-07-23): the engine boot/load window between the Sega splash
and the title shows jo_core_init's solid light-blue back screen (MEASURED
fullscreen-uniform RGB(96,128,224) for ~12+ s, _shots/_red_boot_1221*.png).
The Saturn side (p6_vdp2.c p6_vdp2_boot_splash_show, called from jo_main right
after jo_core_init) masks it with this fullscreen image on a VDP2 NBG0 8bpp
512x256 bitmap in VRAM bank A0.

Input : tools/refs/boot_splash/boot_splash_src.png (1024x1024 RGBA)
Output: cd/BOOTSPL.BIN, 72,192 bytes, big-endian, single file (ISO-root-count
        friendly; name <= 12 chars per the SGL GFS limit):

  offset 0x000, 512 B : 256 x u16 BIG-ENDIAN Saturn BGR555 palette entries,
                        each 0x8000 | b5<<10 | g5<<5 | r5 (MSB set = opaque
                        CRAM color). Entry 0 is 0x8000 (unused -- see below).
  offset 0x200, 71680 B: 224 rows x 320 cols of 8bpp pixel indices, row-major.

VDP2 pixel code 0 is TRANSPARENT on a paletted bitmap NBG (ST-058-R2
transparent-code-0), so the quantizer emits at most 255 colors and every pixel
index is shifted UP by 1 (indices 1..255); index 0 never appears in the pixel
data. The engine writes the palette words to CRAM bank 0 verbatim (they are
already BE BGR555) and the pixel rows into the 512-byte-stride bitmap.

Scaling: plain LANCZOS resize 1024x1024 -> 320x224 (fullscreen; the source is
square so the fit stretches ~1.43x horizontally in pixel space -- on the 4:3
output raster this is the intended fullscreen fill per the user request).
"""
import os
import struct
import sys

from PIL import Image

SRC = os.path.join("tools", "refs", "boot_splash", "boot_splash_src.png")
OUT = os.path.join("cd", "BOOTSPL.BIN")
W, H = 320, 224


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else SRC
    out = sys.argv[2] if len(sys.argv) > 2 else OUT
    im = Image.open(src).convert("RGB").resize((W, H), Image.LANCZOS)
    # 255 colors max: pixel index 0 is the VDP2 transparent code, keep it unused.
    q = im.quantize(colors=255, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG)
    pal = q.getpalette()[: 255 * 3]
    px = bytes(p + 1 for p in q.tobytes())  # shift 0..254 -> 1..255
    assert len(px) == W * H and min(px) >= 1

    words = [0x8000]  # entry 0: unused (transparent code), opaque black filler
    for i in range(255):
        r, g, b = pal[i * 3 : i * 3 + 3]
        words.append(0x8000 | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3))
    blob = struct.pack(">256H", *words) + px
    assert len(blob) == 0x200 + W * H  # 72,192

    with open(out, "wb") as f:
        f.write(blob)
    ncol = len(set(px))
    print("wrote %s: %d bytes, %d colors used (indices 1..255)" % (out, len(blob), ncol))


if __name__ == "__main__":
    main()
