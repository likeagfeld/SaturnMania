#!/usr/bin/env python3
"""qa_ghz_state_probe.py - one-shot GHZ gameplay savestate health + load probe.

Point this at any F5 capture from a GHZ gameplay session. It reports, from a
single state, everything needed to triage the #192 crash WITHOUT relying on
the post-mortem crash capture (which is unreadable -- WRAM-H is stomped):

  * WRAM-H health      -- distinct byte count + top non-zero halfword fill
                          (crash signature = uniform non-zero fill).
  * master SH-2 PC     -- in .text [0x06000000,0x06100000) or derailed.
  * s_ts_state         -- decoded; TS_GHZ_ACTIVE(6) == in gameplay.
  * __jo_sprite_id     -- the live jo VDP1 sprite count. The release build
                          has NO bounds check (sprites.c guard is JO_DEBUG-
                          only); > 255 means the #189-class overflow is
                          clobbering WRAM-H structs THIS tick.
  * g_gp_draw_ticks    -- gameplay draw-pass counter (depth of capture).
  * g_gp_sprite_peak   -- high-water jo sprite count seen.
  * g_er_cd_reads      -- cumulative _blob_get CD streams (slow-motion cost).
  * g_er_sprite_adds   -- cumulative _er_ensure jo_sprite_add VDP1 uploads.

Run it on each capture in a sequence; the LAST intact state before the crash
is the one that names the writer (overflow vs CD thrash vs DMA).

Exit codes:
    0 = state intact AND __jo_sprite_id <= 255 (no overflow this tick)
    1 = state intact BUT __jo_sprite_id > 255 (#189 overflow caught live)
    2 = state crashed/unreadable OR symbols missing
"""
from __future__ import annotations
import argparse
import os
import re
import sys
from collections import Counter
from pathlib import Path

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
GAME_MAP = os.path.join(REPO, "game.map")

JO_MAX_SPRITE = 255
PC_LO, PC_HI = 0x06000000, 0x06100000
TS_NAMES = {0: "FADE_IN", 1: "FLASH", 2: "WAIT_FOR_SONIC", 3: "PRESS_REVEAL",
            4: "WAIT_FOR_ENTER", 5: "TRANSITION_TO_GHZ", 6: "GHZ_ACTIVE"}

SYMS = ("__jo_sprite_id", "s_ts_state", "g_gp_draw_ticks", "g_gp_sprite_peak",
        "g_gp_sprite_last", "g_er_cd_reads", "g_er_sprite_adds")


def _sym_addr(txt, sym):
    m = re.search(r"0x0*([0-9a-fA-F]{6,})\s+_?" + re.escape(sym) +
                  r"(?:\.lto_priv\.\d+)?(?![A-Za-z0-9_])", txt)
    return int(m.group(1), 16) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("state", help="GHZ gameplay savestate (.mc0/.mcs)")
    args = ap.parse_args()
    import mcs_extract

    print("GHZ GAMEPLAY STATE PROBE (#192)")
    print("-" * 60)
    if not os.path.exists(args.state):
        print(f"  HARNESS GAP: state not found: {args.state}"); sys.exit(2)
    if not os.path.exists(GAME_MAP):
        print("  HARNESS GAP: game.map not found (build first)"); sys.exit(2)

    txt = open(GAME_MAP, errors="ignore").read()
    addrs = {n: _sym_addr(txt, n) for n in SYMS}

    s = mcs_extract.parse_savestate(Path(args.state))
    wh = mcs_extract._read_region(s, "MAIN", "WorkRAMH")
    distinct = len(set(wh))
    hw = Counter()
    n = len(wh) & ~1
    for i in range(0, n, 2):
        if wh[i] or wh[i + 1]:
            hw[(wh[i], wh[i + 1])] += 1
    (thb, tcnt) = hw.most_common(1)[0] if hw else ((0, 0), 0)
    top_hw = (thb[1] << 8) | thb[0]
    frac = tcnt / (n // 2)
    rm = mcs_extract._sh2_regs(s, "master")
    pc = rm.get("PC") if rm else None

    crashed = (distinct < 32) or (frac >= 0.50) or \
              (pc is not None and not (PC_LO <= pc < PC_HI))
    pc_s = f"0x{pc:08x}" if pc is not None else "n/a"
    print(f"  WRAM-H distinct={distinct}  top-nonzero-halfword="
          f"0x{top_hw:04x}@{frac*100:.1f}%  master PC={pc_s}")
    if crashed:
        print("-" * 60)
        print("  CRASHED/UNREADABLE: this state is post-stomp. Capture an "
              "EARLIER one (F5 every ~5s; the last intact one names the "
              "writer).")
        sys.exit(2)

    def peek(addr):
        b = mcs_extract._peek_bytes(s, addr, 4)
        return ((b[1] << 24) | (b[0] << 16) | (b[3] << 8) | b[2]) & 0xFFFFFFFF

    vals = {}
    for k, a in addrs.items():
        vals[k] = peek(a) if a is not None else None

    ts = vals["__jo_sprite_id"]  # placeholder to avoid KeyError ordering
    tsv = vals["s_ts_state"]
    print(f"  s_ts_state      = {tsv} "
          f"({TS_NAMES.get(tsv, '?') if tsv is not None else 'n/a'})"
          f"{'   <- in gameplay' if tsv == 6 else '   <- NOT gameplay yet'}")
    sid = vals["__jo_sprite_id"]
    print(f"  __jo_sprite_id  = {sid}  (ceiling {JO_MAX_SPRITE})"
          f"{'   *** OVERFLOW ***' if sid is not None and sid > JO_MAX_SPRITE else ''}")
    print(f"  g_gp_draw_ticks = {vals['g_gp_draw_ticks']}")
    print(f"  g_gp_sprite_peak= {vals['g_gp_sprite_peak']}   "
          f"last={vals['g_gp_sprite_last']}")
    print(f"  g_er_cd_reads   = {vals['g_er_cd_reads']}")
    print(f"  g_er_sprite_adds= {vals['g_er_sprite_adds']}")
    print("-" * 60)
    if sid is not None and sid > JO_MAX_SPRITE:
        print(f"  RED: __jo_sprite_id {sid} > {JO_MAX_SPRITE} -- #189 overflow "
              f"clobbering WRAM-H THIS tick. The lazy residency is still "
              f"exceeding the jo table.")
        sys.exit(1)
    print(f"  OK: state intact, __jo_sprite_id {sid} <= {JO_MAX_SPRITE}. Use "
          f"g_er_cd_reads/g_gp_draw_ticks for the slow-motion rate; pair two "
          f"states for the frame-time gate.")
    sys.exit(0)


if __name__ == "__main__":
    main()
