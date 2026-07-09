#!/usr/bin/env python3
"""
qa_g11_ring.py -- #311 mech-4: compare the live slot-11 (GHCOBJ) draw-time fetch
ring against offline djb2 of the SAME rects cropped from GHZCutscene/Objects.gif,
and (v3) the paired post-restage s_stage hash against the content-packed crop
(pw = mult-8 padded width, zero right-pad). Also reports the per-frame bucket
miss maxima (p6_w_buckN_fmax, armed in p6_frontend_frame since build 25) vs the
GHZCUT bucket slot counts -- fmax > slots == intra-frame slot reuse (M1b class).

Verdicts:
  fetch MISMATCH             -> the draw-time FetchRect corrupts (contention).
  fetch MATCH, stage MISMATCH-> the content pack loop corrupts.
  both MATCH + fmax>slots    -> intra-frame slot reuse (stale VDP1 cmds).
  both MATCH + fmax<=slots   -> tear is below (jo DMA / TEXDEF / VDP1 flush).

Usage: python tools/qa_g11_ring.py STATE.mcs
"""
import subprocess
import sys

import numpy as np
from PIL import Image

# GHZCUT bucket sizing (p6_vdp1.c P6_BK0/1/2 + b3 = P6_VDP1_NSLOTS). #312
# climax re-geometry (build 31): idx0 = the TINY 16x20 bucket, idx1 = 64x80.
BUCKETS = [("b0 16x20 tiny", 20), ("b1 64x80", 24), ("b2 160x160", 8), ("b3 248x160", 1)]


def peek(mcs, addr):
    out = subprocess.run(["python", "tools/mcs_extract.py", mcs, "--peek", hex(addr)],
                         capture_output=True, text=True).stdout
    v = int(out.strip().split("=")[1], 16)
    # WRAM-H 32-bit reads come back with bytes swapped within each 16-bit half
    return ((v & 0xFF00FF00) >> 8) | ((v & 0x00FF00FF) << 8)


def sym_addr(sym):
    out = subprocess.run(["grep", "-m1", f" {sym}$", "/d/sonicmaniasaturn/game.map"],
                         capture_output=True, text=True).stdout
    return int(out.split()[0], 16)


def djb2(b):
    h = 5381
    for x in b:
        h = ((h << 5) + h) ^ x
        h &= 0xFFFFFFFF
    return h


def main(mcs):
    gif = np.asarray(Image.open("extracted/Data/Sprites/GHZCutscene/Objects.gif"))
    rect_base = sym_addr("p6_w_g11_rect")
    wh_base = sym_addr("p6_w_g11_wh")
    hash_base = sym_addr("p6_w_g11_hash")
    stage_base = sym_addr("p6_w_g11_stage")
    n = peek(mcs, sym_addr("p6_w_g11_n"))
    print(f"total slot-11 fetches: {n}")
    ok = True
    for i in range(4):
        r = peek(mcs, rect_base + i * 4)
        wh = peek(mcs, wh_base + i * 4)
        hh = peek(mcs, hash_base + i * 4) & 0xFFFFFFFF
        sh = peek(mcs, stage_base + i * 4) & 0xFFFFFFFF
        if r == 0xFFFFFFF7:  # -9 init
            print(f"  ring[{i}]: (unused)")
            continue
        sx, sy = (r >> 16) & 0xFFFF, r & 0xFFFF
        w, h = (wh >> 16) & 0xFFFF, wh & 0xFFFF
        crop = gif[sy:sy + h, sx:sx + w]
        exp_f = djb2(crop.tobytes())
        pw = (w + 7) & ~7
        padded = np.zeros((h, pw), dtype=np.uint8)
        padded[:, :w] = crop
        exp_s = djb2(padded.tobytes())
        vf = "MATCH" if exp_f == hh else "MISMATCH"
        vs = "MATCH" if exp_s == sh else ("(unset)" if sh == 0xFFFFFFF7 else "MISMATCH")
        if exp_f != hh or (sh != 0xFFFFFFF7 and exp_s != sh):
            ok = False
        print(f"  ring[{i}]: rect=({sx},{sy},{w},{h}) fetch={hh:#010x}/{exp_f:#010x} {vf}"
              f"  stage={sh:#010x}/{exp_s:#010x} {vs}")
    print("per-frame bucket miss maxima (fmax vs slots):")
    reuse = False
    for bi, (label, slots) in enumerate(BUCKETS):
        fmax = peek(mcs, sym_addr(f"p6_w_buck{bi}_fmax"))
        flag = "INTRA-FRAME REUSE" if fmax > slots else "ok"
        if fmax > slots:
            reuse = True
        print(f"  {label}: fmax={fmax} slots={slots} {flag}")
    if not ok:
        print("RESULT: RED (draw-time fetch or stage pack corrupts)")
        return 1
    if reuse:
        print("RESULT: RED (content byte-exact but a bucket restages a slot"
              " more than once per frame -> stale VDP1 commands = the garble)")
        return 1
    print("RESULT: GREEN (fetch+stage byte-exact, no intra-frame reuse"
          " -> tear is below: jo DMA / TEXDEF / VDP1 flush)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
