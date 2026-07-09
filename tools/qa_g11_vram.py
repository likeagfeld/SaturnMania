#!/usr/bin/env python3
"""
qa_g11_vram.py -- #311 mech-4 VRAM-readback gate (the DMA-race symptom measure).

For each rect in the live g11 ring (the last 4 GHCOBJ slot-11 restages), find the
VDP1 sprite command(s) whose CMDSIZE equals the rect's content TEXDEF
((pw>>3)<<8 | h, pw = mult-8 padded width) and djb2 the pw*h texture bytes at
SRCA<<3 in VDP1 VRAM against the offline content-packed crop (identical bytes to
what p6_title_restage_content packed into s_stage). ST-238-R1: slDMACopy is
ASYNCHRONOUS ("terminates soon after DMA is initiated") -- if the next restage's
pack loop overwrote s_stage while a prior transfer was in flight, the slot's
VRAM holds a TORN mix and this gate fires RED. Clean VRAM for every locatable
ring rect = GREEN.

A slot may legitimately have been re-restaged to a DIFFERT same-size rect after
the ring recorded it (LRU churn) -- so a MISMATCH is only counted when NO
size-matched command in the list carries the expected texture. PNG previews of
every candidate texture are written to _g11vram_<i>_<n>.png for the mandatory
visual check.

Usage: python tools/qa_g11_vram.py STATE.mcs
"""
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image


def peek(mcs, addr):
    out = subprocess.run(["python", "tools/mcs_extract.py", mcs, "--peek", hex(addr)],
                         capture_output=True, text=True).stdout
    v = int(out.strip().split("=")[1], 16)
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


def pair_swap(b):
    a = bytearray(b)
    a[0::2], a[1::2] = b[1::2], b[0::2]
    return bytes(a)


def main(mcs):
    gif = np.asarray(Image.open("extracted/Data/Sprites/GHZCutscene/Objects.gif"))
    rect_base = sym_addr("p6_w_g11_rect")
    wh_base = sym_addr("p6_w_g11_wh")

    with tempfile.TemporaryDirectory() as td:
        vpath = Path(td) / "v1.bin"
        subprocess.run(["python", "tools/mcs_extract.py", mcs, "--vdp1-vram", str(vpath)],
                       capture_output=True, text=True)
        vram = vpath.read_bytes()
    print(f"vdp1 vram dump: {len(vram)} bytes")

    # Walk the frame's command list from 0 to the first END (CTRL bit15). Comm =
    # CTRL bits3:0 (0=normal,1=scaled,2=distorted -- jo_sprite_draw3D emits
    # SCALED, Comm=1, with the zoom-point in bits11:8, so mask ONLY the low
    # nibble). LITTLE-endian 16-bit words in the dump (established recipe).
    cmds = []
    for off in range(0, 0x40000, 0x20):
        ctrl, _link, _pmod, _colr, srca, size = struct.unpack_from("<6H", vram, off)
        if ctrl & 0x8000:
            break
        if (ctrl & 0xF) > 2:
            continue
        w = ((size >> 8) & 0x3F) * 8
        h = size & 0xFF
        if w == 0 or h == 0:
            continue
        cmds.append((off, srca, w, h))
    print(f"frame command list: {len(cmds)} sprite draws")

    red = 0
    for i in range(4):
        r = peek(mcs, rect_base + i * 4)
        wh = peek(mcs, wh_base + i * 4)
        if r == 0xFFFFFFF7:
            print(f"ring[{i}]: (unused)")
            continue
        sx, sy = (r >> 16) & 0xFFFF, r & 0xFFFF
        w, h = (wh >> 16) & 0xFFFF, wh & 0xFFFF
        pw = (w + 7) & ~7
        crop = gif[sy:sy + h, sx:sx + w]
        padded = np.zeros((h, pw), dtype=np.uint8)
        padded[:, :w] = crop
        exp = djb2(padded.tobytes())
        cands = [(off, srca) for (off, srca, cw, ch) in cmds if cw == pw and ch == h]
        verdict = "NOCMD"
        n = 0
        for off, srca in cands:
            tex = vram[srca * 8: srca * 8 + pw * h]
            if len(tex) < pw * h:
                continue
            hits = djb2(tex) == exp or djb2(pair_swap(tex)) == exp
            Image.fromarray(np.frombuffer(pair_swap(tex), dtype=np.uint8)
                            .reshape(h, pw) * 5).save(f"_g11vram_{i}_{n}.png")
            if hits:
                verdict = "MATCH"
                break
            verdict = "MISMATCH"
            n += 1
        if verdict == "MISMATCH":
            red += 1
        print(f"ring[{i}]: rect=({sx},{sy},{w},{h}) pw={pw} cmds_sized={len(cands)} {verdict}")
    if red:
        print(f"RESULT: RED ({red} ring rect(s) have size-matched VDP1 textures that"
              " match NO expected content -> torn VRAM, the async-slDMACopy race)")
        return 1
    print("RESULT: GREEN (every locatable ring rect's VRAM texture is byte-exact)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
