#!/usr/bin/env python3
"""qa_title_island_map.py - user punch list v2 item 1: the title island RBG0
pattern-name map must be PRESENT in VDP2 bank B0 during the title leg.

MEASURED RED baseline (2026-07-03, chain build, _v9_title.mcs t=55 AND
_v9_title_early.mcs t=48): p6_w_title_island_armed=1 (the arm RAN) yet the
RBG0 map region B0 0x25E40000..+0x4000 is ALL ZERO (0/64 sampled lines)
=> the island renders black. The arm's one-shot direct CPU write either never
landed or was wiped within one vblank (p6_w_fg_dma=3: a stale FG-page DMA into
the SAME B0 address is the prime suspect -- P6_VDP2_MAP == P6_RBG0_MAP ==
0x25E40000, p6_vdp2.c:35/:697). CRAM healthy, A0 cells healthy, A1 RPT/coeff
healthy, B1 clouds healthy -- ONLY the B0 map is missing.

GREEN = in a title-leg savestate (folder=="Title"):
  1. p6_w_title_island_armed == 1, AND
  2. >= 4 of 64 sampled 16-byte lines in B0 0x25E40000..+0x4000 are nonzero.

Usage:
    python tools/qa_title_island_map.py STATE.mcs [--map game.map]

Exit 0 = GREEN, 1 = RED, 2 = parse error.
"""
from __future__ import annotations

import argparse
import importlib.util
import re
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("mcs_extract", _HERE / "mcs_extract.py")
mcs_extract = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mcs_extract)  # type: ignore[union-attr]

RBG0_MAP = 0x25E40000
# The landmass sits in page-0 rows 8..23 (the build shifts the head there, see
# p6_island_build_map_to's +16 source shift). Row y (page 0) = u16 [y*64 .. y*64+63];
# rows 8..23 => u16 512..1536 => byte 1024..3072. Sample EVERY word in that band --
# the earlier 0x100-step sampling hit only columns 0-3 of each row (all blank, the
# head is at columns 8-23) => a false RED despite a fully-present map (MEASURED
# _v12_title.mcs: 301/1024 landmass PNDs 0xC000/0x0028 in this exact band).
LANDMASS_LO = 0x25E40000 + 512 * 2   # byte 1024
LANDMASS_HI = 0x25E40000 + 1536 * 2  # byte 3072
MIN_NONZERO_WORDS = 50


def swap32(v: int) -> int:
    return ((v & 0xFF00FF00) >> 8) | ((v & 0x00FF00FF) << 8)


def main(argv=None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("state")
    p.add_argument("--map", default=str(_HERE.parent / "game.map"))
    a = p.parse_args(argv)

    map_text = Path(a.map).read_text(errors="replace")
    m = re.search(r"0x([0-9a-fA-F]{16})\s+p6_w_title_island_armed\s*$", map_text, re.M)
    if not m:
        sys.stderr.write("qa_title_island_map: p6_w_title_island_armed missing from map\n")
        return 2
    sym_armed = int(m.group(1), 16)

    sections = mcs_extract.parse_savestate(Path(a.state))
    raw = mcs_extract._peek_bytes(sections, sym_armed, 4)
    armed = swap32(int.from_bytes(raw, "big")) if raw else 0

    nz = total = 0
    addr = LANDMASS_LO
    while addr < LANDMASS_HI:
        w = mcs_extract._peek_bytes(sections, addr, 2)
        addr += 2
        if w is None:
            continue
        total += 1
        if int.from_bytes(w, "big"):
            nz += 1
    ok = armed == 1 and nz >= MIN_NONZERO_WORDS
    print(f"  island_armed = {armed}")
    print(f"  B0 landmass band = {nz}/{total} words nonzero (min {MIN_NONZERO_WORDS})")
    verdict = "GREEN" if ok else "RED"
    print(f"qa_title_island_map: -> {verdict}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
