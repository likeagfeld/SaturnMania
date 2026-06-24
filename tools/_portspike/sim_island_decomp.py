#!/usr/bin/env python3
# =============================================================================
# sim_island_decomp.py -- GROUND-TRUTH simulator of the Mania title rotating
# island (TitleBG_Scanline_Island, decomp authoritative) AND the Saturn RBG0
# screen formula, so the on-screen result can be proven BEFORE a hardware build.
#
# Renders the 320x240 title band at a sweep of TitleBG->angle values for:
#   (A) the DECOMP scanline formula           (position/deform per scanline)
#   (B) the SATURN RBG0 HW formula            (kx*(A*Sx+B*Sy)+Mx ; the p6_vdp2.c
#                                              implementation) with a given
#                                              (CTR_TEXEL, My_swing) param pair.
# A and B at the decomp-exact params (512, 0xA000) MUST match -- that proves the
# Saturn formula is a faithful translation and the only open variable is params.
#
# Output: a montage PNG per mode (one column per angle) -> Read it to SEE the
# island. This is the asset-inspection + decomp-simulation the methodology
# requires (CLAUDE.md 4.3) and which prior island sessions SKIPPED.
#
#   python tools/_portspike/sim_island_decomp.py
# =============================================================================
import math
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import render_scene as rs  # reuse the verified Scene1.bin / tile / palette parse

TITLE_DIR = os.path.join(ROOT, "extracted", "Data", "Stages", "Title")
SCREEN_W, SCREEN_H = 320, 240
ISLAND_LINE0 = 168          # decomp SetClipBounds(0,0,168,...) -> band y[168,240)
CENTER_X = 160              # ScreenInfo->center.x
SKY = np.array([0, 96, 224], np.uint8)   # P6_TITLE_SKY_COL 0xF180 backdrop


# ---- RSDK trig (Math.cpp): Sin1024(a)=sin(a*2pi/1024)*1024, 1.0==1024 --------
def _tbl(n):
    return [int(round(math.sin((i / (n / 2.0)) * math.pi) * n)) for i in range(n)]
_SIN1024 = _tbl(1024)
def sin1024(a): return _SIN1024[a & 0x3FF]
def cos1024(a): return _SIN1024[(a + 256) & 0x3FF]


def asr(v, n):
    """Arithmetic shift right matching C >> on a two's-complement int."""
    return v >> n if v >= 0 else -((-v) >> n)
    # NOTE: C/GCC arithmetic >> floors; Python >> also floors. For exact C
    # parity on negatives use floor (Python >>). We use floor below directly.


def build_island_rgba():
    """1024x1024 RGBA of TileLayer 3; empty (0xFFFF) tiles -> alpha 0."""
    gif = os.path.join(TITLE_DIR, "16x16Tiles.gif")
    tiles = rs.load_tiles(gif)
    banks, masks = rs.read_stage_palette(os.path.join(TITLE_DIR, "StageConfig.bin"))
    pal = rs.build_palette(gif, banks, masks, 0)
    layers = rs.parse_scene(os.path.join(TITLE_DIR, "Scene1.bin"))
    isl = layers[3]
    xs, ys, layout = isl["xs"], isl["ys"], isl["layout"]
    rgba = np.zeros((ys * 16, xs * 16, 4), np.uint8)
    for ty in range(ys):
        for tx in range(xs):
            e = int(layout[ty, tx])
            if e >= 0xFFFF:
                continue
            idx = e & 0x3FF
            if idx >= tiles.shape[0]:
                continue
            t = tiles[idx]
            if e & 0x400: t = t[:, ::-1]
            if e & 0x800: t = t[::-1, :]
            block = pal[t]
            rgba[ty*16:ty*16+16, tx*16:tx*16+16, :3] = block
            rgba[ty*16:ty*16+16, tx*16:tx*16+16, 3] = 255
    return rgba


def sample(tex, tx, ty):
    """Bilinear-free nearest sample with wrap (RSDK tile-layer wrap by size)."""
    H, W = tex.shape[0], tex.shape[1]
    txi = int(tx) % W
    tyi = int(ty) % H
    px = tex[tyi, txi]
    if px[3] == 0:
        return None
    return px[:3]


def render_decomp(tex, angle):
    """(A) DECOMP TitleBG_Scanline_Island, verbatim 16.16 fixed."""
    out = np.zeros((SCREEN_H, SCREEN_W, 3), np.uint8)
    out[:, :] = SKY
    sine   = sin1024((-angle) & 0x3FF) >> 2
    cosine = cos1024((-angle) & 0x3FF) >> 2
    for i in range(16, 88):
        line = ISLAND_LINE0 + (i - 16)
        idv = 0xA00000 // (8 * i)
        sin = sine * idv
        cos = cosine * idv
        deform_y = sin >> 7
        deform_x = (-cos) >> 7
        pos_y = cos - CENTER_X * deform_y - 0xA000 * cosine + 0x2000000
        pos_x = sin - CENTER_X * deform_x - 0xA000 * sine + 0x2000000
        for x in range(SCREEN_W):
            tX = (pos_x + x * deform_x) >> 16
            tY = (pos_y + x * deform_y) >> 16
            px = sample(tex, tX, tY)
            if px is not None:
                out[line, x] = px
    return out


def render_saturn(tex, angle, ctr_texel, my_swing):
    """(B) SATURN RBG0 HW formula as implemented in p6_vdp2.c:
       X = kx*(A*Sx + B*Sy) + Mx ;  Sx = Xst + Hcnt, Sy = Yst   (Mode 0, kx=ky)
       A=-cos, B=-sin, D=sin, E=-cos ; kx=2*id ; cos=cosine<<8, sin=sine<<8
       Mx = ctr_texel<<16 - 0xA000*sine ; My = 0x2000000 - my_swing*cosine
       Xst=-160<<16, Yst=-128<<16 (texel start)."""
    out = np.zeros((SCREEN_H, SCREEN_W, 3), np.uint8)
    out[:, :] = SKY
    sine   = sin1024((-angle) & 0x3FF) >> 2
    cosine = cos1024((-angle) & 0x3FF) >> 2
    cosF = cosine << 8      # 16.16 true cos
    sinF = sine << 8
    A = -cosF; B = -sinF; D = sinF; E = -cosF
    Xst_t = -160; Yst_t = -128                      # texel start (Xst>>16)
    Mx = (ctr_texel << 16) - 0xA000 * sine
    My = 0x2000000 - my_swing * cosine
    for i in range(16, 88):
        line = ISLAND_LINE0 + (i - 16)
        idv = 0xA00000 // (8 * i)
        kx = 2 * idv
        if kx > 0x007FFFFF: kx = 0x007FFFFF
        if kx < 0x00001000: kx = 0x00001000
        # Sy = Yst (constant across the line); Sx = Xst + Hcnt
        # X = (kx*(A*Sx + B*Sy))>>...   do products the way the HW/witness does:
        #   inner = A*(Xst_t) + B*(Yst_t) + A*Hcnt   (matrix*texel, A/B are 16.16)
        #   X = ((kx * inner) >> 16) + Mx            (kx 16.16)
        for x in range(SCREEN_W):
            innerX = A * Xst_t + B * Yst_t + A * x
            innerY = D * Xst_t + E * Yst_t + D * x
            X = ((kx * innerX) >> 16) + Mx
            Y = ((kx * innerY) >> 16) + My
            tX = X >> 16
            tY = Y >> 16
            px = sample(tex, tX, tY)
            if px is not None:
                out[line, x] = px
    return out


def montage(frames, labels, path, scale=1):
    pad = 4
    h = SCREEN_H
    w = SCREEN_W
    cols = len(frames)
    sheet = np.full((h + 16, (w + pad) * cols, 3), 30, np.uint8)
    from PIL import ImageDraw
    img = Image.fromarray(sheet)
    drw = ImageDraw.Draw(img)
    arr = np.array(img)
    for c, (f, lab) in enumerate(zip(frames, labels)):
        x0 = c * (w + pad)
        arr[16:16+h, x0:x0+w] = f
    img = Image.fromarray(arr)
    drw = ImageDraw.Draw(img)
    for c, lab in enumerate(labels):
        drw.text((c * (w + pad) + 2, 2), lab, fill=(255, 255, 0))
    if scale != 1:
        img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    img.save(path)
    print("wrote", path)


def main():
    tex = build_island_rgba()
    # measure landmass bbox (non-transparent)
    ys, xs = np.where(tex[:, :, 3] > 0)
    print("island texture occupied bbox: x[%d..%d] y[%d..%d]  center=(%d,%d)"
          % (xs.min(), xs.max(), ys.min(), ys.max(),
             (xs.min()+xs.max())//2, (ys.min()+ys.max())//2))
    angles = [0, 128, 256, 384, 512, 640, 768, 896]
    labs = ["a%d" % a for a in angles]

    dec = [render_decomp(tex, a) for a in angles]
    montage(dec, labs, os.path.join(ROOT, "_sim_island_decomp.png"), scale=1)

    sat_exact = [render_saturn(tex, a, 512, 0xA000) for a in angles]
    montage(sat_exact, labs, os.path.join(ROOT, "_sim_island_saturn_exact.png"), scale=1)

    sat_agent = [render_saturn(tex, a, 448, 0x5000) for a in angles]
    montage(sat_agent, labs, os.path.join(ROOT, "_sim_island_saturn_agent.png"), scale=1)

    # quantitative: decomp vs saturn-exact mean abs diff in the band
    band = slice(ISLAND_LINE0, SCREEN_H)
    for a, dfr, sfr in zip(angles, dec, sat_exact):
        d = float(np.mean(np.abs(dfr[band].astype(int) - sfr[band].astype(int))))
        print("angle %3d  decomp-vs-saturnExact band mean|diff| = %.2f" % (a, d))


if __name__ == "__main__":
    main()
