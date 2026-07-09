#!/usr/bin/env python3
"""qa_jo_sprite_peak_gate.py - jo sprite-table PEAK high-water gate (#192).

WHY THIS EXISTS (and why qa_jo_sprite_budget_gate.py is insufficient):
entity_residency_begin_frame() rewinds __jo_sprite_id to the player top
EVERY tick (Game.c mania_ghz_draw_only). So the per-tick PEAK sprite count
is transient -- an instantaneous savestate peek of __jo_sprite_id reads
whatever the count happened to be at capture (it read 55 at an early frame
and looked GREEN). If a dense GHZ section 45-60s into gameplay briefly
pushes the count over JO_MAX_SPRITE (255), jo writes __jo_sprite_def[255..]
and __jo_sprite_pic[255..] PAST their fixed [255] arrays (the release build
has NO bounds check -- sprites.c:188 guard is #ifdef JO_DEBUG only),
clobbering the HUD/titlecard structs -> the user-reported "graphics crash
after the title card". The very next tick rewinds the count back down, so
the overflow is invisible to a later instantaneous peek.

Game.c keeps three never-reset globals updated once per gameplay draw pass:
  g_gp_sprite_peak  - MAX jo_get_last_sprite_id() ever observed (the latch)
  g_gp_sprite_last  - the most recent end-of-tick count (sanity)
  g_gp_draw_ticks   - count of gameplay draw passes (capture-depth proof)

This gate peeks all three from a post-crash savestate.

USAGE (the reliable repro path, since press-start injection is flaky):
  1. Build the instrumented ISO.
  2. Boot it, press Start, PLAY until the graphics crash (~45-60s).
  3. Press F5 in Mednafen to save a state, then run:
       python tools/qa_jo_sprite_peak_gate.py --state <that>.mcs

Predicate
---------
  GREEN: g_gp_sprite_peak <= 254 AND g_gp_draw_ticks large enough that the
         capture actually reached deep gameplay (>= MIN_TICKS). If the peak
         stays in-bounds across a deep capture that still crashed, the crash
         is NOT the sprite-table overflow -> redirect the investigation.
  RED:   g_gp_sprite_peak >= 255  -> table overflow IS the crash class.

Exit codes:
    0 = GREEN (peak in bounds)
    1 = RED   (peak overflowed JO_MAX_SPRITE)
    2 = harness error / state not captured / capture too shallow
"""
from __future__ import annotations
import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)

GAME_MAP = os.path.join(REPO, "game.map")
JO_MAX_SPRITE = 255
MIN_TICKS = 1800  # ~30s at 60Hz; below this the capture didn't reach the
                  # dense section where the overflow is reported to occur.


def _map_text():
    with open(GAME_MAP, "r", errors="ignore") as f:
        return f.read()


def _sym_addr(txt, sym):
    m = re.search(r"0x([0-9a-fA-F]{8,})\s+_?" + re.escape(sym) +
                  r"(?:\.lto_priv\.\d+)?(?![A-Za-z0-9_])", txt)
    return int(m.group(1), 16) if m else None


def _peek_i32(sections, addr):
    import mcs_extract
    b = mcs_extract._peek_bytes(sections, addr, 4)
    # WRAM-H pair-swap (project gate convention; see qa_jo_sprite_budget_gate).
    v = (b[1] << 24) | (b[0] << 16) | (b[3] << 8) | b[2]
    if v >= 0x80000000:
        v -= 0x100000000
    return v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--state", required=True,
                    help="post-crash savestate (.mcs/.mc0) to inspect")
    args = ap.parse_args()

    print("JO SPRITE-TABLE PEAK HIGH-WATER GATE (#192)")
    print("-" * 60)

    if not os.path.exists(args.state):
        print(f"  HARNESS GAP: state not found: {args.state}")
        sys.exit(2)
    if not os.path.exists(GAME_MAP):
        print("  HARNESS GAP: game.map not found (build first)")
        sys.exit(2)

    txt = _map_text()
    a_peak = _sym_addr(txt, "g_gp_sprite_peak")
    a_last = _sym_addr(txt, "g_gp_sprite_last")
    a_tick = _sym_addr(txt, "g_gp_draw_ticks")
    missing = [n for n, a in (("g_gp_sprite_peak", a_peak),
                              ("g_gp_sprite_last", a_last),
                              ("g_gp_draw_ticks", a_tick)) if a is None]
    if missing:
        print("  HARNESS GAP: symbols not in game.map: " + ", ".join(missing))
        print("  (rebuild with the #192 latch instrumentation in Game.c)")
        sys.exit(2)

    import mcs_extract
    from pathlib import Path
    sections = mcs_extract.parse_savestate(Path(args.state))
    peak = _peek_i32(sections, a_peak)
    last = _peek_i32(sections, a_last)
    ticks = _peek_i32(sections, a_tick) & 0xFFFFFFFF

    print(f"  g_gp_sprite_peak = {peak}")
    print(f"  g_gp_sprite_last = {last}")
    print(f"  g_gp_draw_ticks  = {ticks}  (~{ticks/60.0:.1f}s of gameplay)")
    print(f"  JO_MAX_SPRITE    = {JO_MAX_SPRITE}")
    print("-" * 60)

    if ticks < MIN_TICKS:
        print(f"  CAPTURE TOO SHALLOW: {ticks} draw ticks < {MIN_TICKS} "
              f"(~{MIN_TICKS/60.0:.0f}s). Play deeper into the level before "
              f"saving the state -- the crash is reported at 45-60s.")
        sys.exit(2)

    if peak >= JO_MAX_SPRITE:
        print(f"  RED: peak {peak} >= {JO_MAX_SPRITE} -- jo wrote "
              f"__jo_sprite_def/pic[{JO_MAX_SPRITE}..{peak}] past the [255] "
              f"arrays. THIS is the post-title-card graphics crash class.")
        sys.exit(1)

    print(f"  GREEN: peak {peak} <= {JO_MAX_SPRITE - 1} across a deep "
          f"({ticks/60.0:.0f}s) capture. The sprite table did NOT overflow "
          f"-- if the crash still occurred, it is a DIFFERENT class "
          f"(VDP1 VRAM / pool / scroll) -> redirect.")
    sys.exit(0)


if __name__ == "__main__":
    main()
