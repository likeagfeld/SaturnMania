#!/usr/bin/env python3
"""qa_title_deepframe.py - #313 title deep-frame sprite-pipeline gate.

The title FG (logo/Sonic/press-start, ~10 VDP1 sprite commands) must still be
in the VDP1 command table at a DEEP capture (settled title idles for minutes).

MEASURED RED baseline (2026-07-02, build game.elf Jul-2): frame-60 capture has
10 sprite commands; frames 120/240/360/500 have ZERO (preamble + immediate
END) while p6_w_vdp1_landed keeps incrementing ~15/frame -- the emits stop
reaching SGL's SpriteEntry (SGL system byte 0x060FFC73 stays 0; the CommandBuf
mailbox base 0x060F9D38 holds only slSynch's own op=4 command all frame).

Usage:
    python tools/qa_title_deepframe.py STATE.mcs [--min-cmds 5]

Exit 0 = GREEN (>= min sprite commands), 1 = RED, 2 = parse error.
"""
from __future__ import annotations

import argparse
import importlib.util
import struct
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("mcs_extract", _HERE / "mcs_extract.py")
mcs_extract = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mcs_extract)  # type: ignore[union-attr]


def count_sprite_cmds(vram: bytes) -> int:
    """Walk the VDP1 command table (LE 16-bit words, 0x20-byte commands).

    comm = ctrl & 0xF; stop at CTRL bit15 (END). Commands 0-4 (normal/scaled/
    distorted sprites + flat polygons) past the 3-command preamble count as
    draw commands. #316 F1: FOLLOWS JP=assign links (CMDCTRL bits 14-12 ==
    001, CMDLINK = VRAM addr>>3) -- the direct command list lives at 0x2000/
    0x2800 reached via the trampoline at 0x60, so a sequential-only walk would
    read the OLD (dead SGL) chain and miss every real command.
    """
    off = 0
    cmds = 0
    hops = 0
    while 0 <= off < len(vram) - 0x20 and hops < 256:
        hops += 1
        ctrl = struct.unpack("<H", vram[off:off + 2])[0]
        if ctrl & 0x8000:
            break
        comm = ctrl & 0xF
        if comm <= 4 and off >= 0x60:
            cmds += 1
        jp = (ctrl >> 12) & 0x7
        if jp == 1:  # jump-assign via CMDLINK
            link = struct.unpack("<H", vram[off + 2:off + 4])[0]
            off = link * 8
        else:
            off += 0x20
    return cmds


def main(argv=None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("state")
    p.add_argument("--min-cmds", type=int, default=5)
    a = p.parse_args(argv)

    sections = mcs_extract.parse_savestate(Path(a.state))
    if "VDP1" not in sections or "VRAM" not in sections["VDP1"]:
        sys.stderr.write("qa_title_deepframe: VDP1/VRAM not in state\n")
        return 2
    off, sz = sections["VDP1"]["VRAM"]
    buf = sections["__buf_bytes__"]
    vram = buf[off:off + sz]

    n = count_sprite_cmds(vram)
    verdict = "GREEN" if n >= a.min_cmds else "RED"
    print(f"qa_title_deepframe: sprite_cmds={n} (min {a.min_cmds}) -> {verdict}")
    return 0 if n >= a.min_cmds else 1


if __name__ == "__main__":
    sys.exit(main())
