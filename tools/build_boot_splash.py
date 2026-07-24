#!/usr/bin/env python3
"""build_boot_splash.py v2 -- build cd/BOOTSPL.BIN, the ANIMATED boot/loading
splash (user feature 2026-07-24: replace the static AI-warning image with a
light looping animation from "Sonic Mania Saturn Loading Screen - Animated
1.mp4" -- Sonic running in a spinning wheel + LOADING... text).

MEASURED design inputs (this tool's authoring session, 145 frames @24fps,
scaled 320x224):
  - dominant loop period = 6 frames (mean inter-frame diff 12.2, the minimum
    across periods 2..29) -> ONE 6-frame cycle captures the wheel+run loop.
  - consecutive-frame strong-change bbox over one loop = x[6..301] y[25..156]
    -> region 296x132 (39,072 B @8bpp). The glow shimmer outside it is
    dropped (static from the background frame).
  - playback: the Saturn side advances one region frame per animation tick
    (ticks come from the front-end frame loop and the masked-load GFS window
    refills -- the load phase runs with interrupts MASKED, so no vblank ISR;
    see p6_vdp2.c p6_bootsplash_anim_tick).

Output cd/BOOTSPL.BIN (BSP2):
  0x000  'BSP2' magic
  0x004  u16 BE: rx, ry, rw, rh, nframes, reserved   (region + frame count)
  0x010  512 B : 256 x u16 BE Saturn BGR555 palette (entry 0 = 0x8000 unused;
                 pixel code 0 is the VDP2 transparent code, indices are 1..255)
  0x210  71,680 B : background frame, 224 rows x 320 cols 8bpp
  then   nframes x (rw*rh) B : region frames (row-major), same palette
Total with 6 frames of 296x132: 306,624 B (~2 s at 1x CD, one contiguous read;
first paint uses only header+palette+bg = fast).

The legacy loader path (palette-first, no magic) is superseded; p6_vdp2.c
falls back to a static show if the magic is absent."""
import os
import struct
import sys

from PIL import Image

FRAMES_DIR = os.path.join("_loadanim")   # ffmpeg-extracted 320x224 frames
OUT = os.path.join("cd", "BOOTSPL.BIN")
W, H = 320, 224
RX, RY, RW, RH = 6, 24, 296, 132         # ry 25 -> 24 (even, covers bbox)
LOOP_BASE, NFRAMES = 48, 6               # measured settled 6-frame cycle


def main():
    frames_dir = sys.argv[1] if len(sys.argv) > 1 else FRAMES_DIR
    out = sys.argv[2] if len(sys.argv) > 2 else OUT
    names = sorted(f for f in os.listdir(frames_dir) if f.endswith(".png"))
    bg = Image.open(os.path.join(frames_dir, names[LOOP_BASE])).convert("RGB")
    assert bg.size == (W, H), bg.size
    crops = [
        Image.open(os.path.join(frames_dir, names[LOOP_BASE + i]))
        .convert("RGB").crop((RX, RY, RX + RW, RY + RH))
        for i in range(NFRAMES)
    ]
    # GLOBAL palette: quantize one mosaic (bg + all crops) so every frame
    # shares the single CRAM bank-0 palette.
    mosaic = Image.new("RGB", (W, H + NFRAMES * RH))
    mosaic.paste(bg, (0, 0))
    for i, c in enumerate(crops):
        mosaic.paste(c, (0, H + i * RH))
    q = mosaic.quantize(colors=255, method=Image.MEDIANCUT, dither=Image.FLOYDSTEINBERG)
    pal = q.getpalette()[: 255 * 3]
    qb = q.tobytes()
    bg_px = bytes(p + 1 for p in qb[: W * H])
    frame_px = []
    for i in range(NFRAMES):
        rows = bytearray()
        for y in range(RH):
            off = (H + i * RH + y) * W
            rows += bytes(p + 1 for p in qb[off: off + RW])
        assert len(rows) == RW * RH
        frame_px.append(bytes(rows))

    words = [0x8000]
    for i in range(255):
        r, g, b = pal[i * 3: i * 3 + 3]
        words.append(0x8000 | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3))
    blob = b"BSP2" + struct.pack(">6H", RX, RY, RW, RH, NFRAMES, 0)
    blob += struct.pack(">256H", *words) + bg_px + b"".join(frame_px)
    with open(out, "wb") as f:
        f.write(blob)
    print("wrote %s: %d bytes (bg %d + %d frames x %d region %dx%d @(%d,%d))"
          % (out, len(blob), W * H, NFRAMES, RW * RH, RW, RH, RX, RY))


if __name__ == "__main__":
    main()
