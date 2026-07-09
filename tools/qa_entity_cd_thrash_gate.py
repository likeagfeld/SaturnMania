#!/usr/bin/env python3
"""qa_entity_cd_thrash_gate.py - FR-2 entity-residency CD-thrash perf gate.

WHY THIS EXISTS
---------------
The user reported (2026-06-04): "everything gets incredibly slow like frame by
frame rendering slow once the title card renders." Measured from a clean GHZ
capture (tools/_repro192_deep.mcs): the FR-2 entity residency issues ~2.4 CD
file-reads PER FRAME during gameplay. Root cause (entity_atlas.c:218-252):

  * _blob_get streams the WHOLE atlas SP2 blob (all frames, 40-94 KB each),
    not the one displayed frame.
  * The pool is 192 KB but reserves ENTITY_MAX_BLOB (96 KB) as flush headroom,
    so it WHOLESALE-FLUSHES once resident blobs exceed ~96 KB usable
    (entity_atlas.c:224 `s_epool_head + ENTITY_MAX_BLOB > ENTITY_POOL_SIZE`).
  * The distinct on-screen atlas working set (6+ atlases) is 2-3x that 96 KB,
    so the pool flushes 2-3x PER TICK -> every frame re-streams blobs from CD.

At ~100 ms / emulated-CD seek, re-streaming a couple of blobs per frame is a
multi-frame stall every frame -> the single-digit-fps "frame by frame slow".

This is the SAME FR-2 design that causes the #192 crash (the uncapped per-tick
VDP1 re-upload overflows 512 KB VDP1 VRAM -> jo_texture_definition.adr
truncation -> master SH-2 derail). Slowness and crash are two faces of one
design; this gate measures the slowness face deterministically.

PREDICATE
---------
Game.c / entity_atlas.c keep two never-reset cumulative counters:
  g_er_cd_reads    - count of jo_fs_read_file_ptr CD streams in _blob_get
  g_er_sprite_adds - count of jo_sprite_add VDP1 uploads in _er_ensure
and the gameplay latch g_gp_draw_ticks (gameplay draw passes).

A CORRECT persistent-residency design streams each distinct atlas from CD
exactly ONCE (warm-up), then plateaus: total CD reads ~= the number of
distinct atlases ever displayed (<= 16, the entity atlas table size). So
reads-per-tick collapses toward 0 as the capture deepens.

  reads_per_tick = g_er_cd_reads / g_gp_draw_ticks

  RED   (FAIL): reads_per_tick > MAX_READS_PER_TICK  -> continuous re-stream
                (the pool is thrashing every frame -> the user's slowdown).
  GREEN (PASS): reads_per_tick <= MAX_READS_PER_TICK -> CD reads plateaued
                (each atlas streamed ~once; residency persists across frames).

The capture must have at least MIN_TICKS gameplay draw passes so the one-time
warm-up reads (<= 16) are amortized below the threshold. This is NOT the #192
crash-depth requirement (1800 ticks) -- a shallow GHZ capture suffices to
expose per-frame thrash.

Exit codes:
    0 = GREEN (CD reads plateaued; no per-frame thrash)
    1 = RED   (CD reads grow per-frame; pool thrashing -> slowdown)
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

# A correct design plateaus at <= ~16 one-time reads (entity atlas table size).
# Over a >=100-tick capture that is <= 0.16 reads/tick; the thrashing build is
# 1.57 reads/tick (271 reads / 173 ticks). 0.25 sits cleanly between, with a
# 6x margin below the current RED and a >1.5x margin above the GREEN ceiling.
MAX_READS_PER_TICK = 0.25
# Enough gameplay frames to amortize the <=16 warm-up reads below threshold.
# (Distinct from the 1800-tick #192 crash-depth requirement: per-frame thrash
# shows up immediately, so a shallow GHZ capture is sufficient here.)
MIN_TICKS = 100


def _map_text():
    with open(GAME_MAP, "r", errors="ignore") as f:
        return f.read()


def _sym_addr(txt, sym):
    m = re.search(r"0x([0-9a-fA-F]{8,})\s+_?" + re.escape(sym) +
                  r"(?:\.lto_priv\.\d+)?(?![A-Za-z0-9_])", txt)
    return int(m.group(1), 16) if m else None


def _peek_u32(sections, addr):
    import mcs_extract
    b = mcs_extract._peek_bytes(sections, addr, 4)
    # WRAM-H pair-swap (project gate convention; see qa_jo_sprite_peak_gate).
    return ((b[1] << 24) | (b[0] << 16) | (b[3] << 8) | b[2]) & 0xFFFFFFFF


def main():
    ap = argparse.ArgumentParser(
        description="FR-2 entity-residency CD-thrash perf gate")
    ap.add_argument("--state", required=True,
                    help="GHZ gameplay savestate (.mcs/.mc0) to inspect")
    args = ap.parse_args()

    print("FR-2 ENTITY-RESIDENCY CD-THRASH PERF GATE")
    print("-" * 62)

    if not os.path.exists(args.state):
        print(f"  HARNESS GAP: state not found: {args.state}")
        sys.exit(2)
    if not os.path.exists(GAME_MAP):
        print("  HARNESS GAP: game.map not found (build first)")
        sys.exit(2)

    txt = _map_text()
    a_cd   = _sym_addr(txt, "g_er_cd_reads")
    a_add  = _sym_addr(txt, "g_er_sprite_adds")
    a_tick = _sym_addr(txt, "g_gp_draw_ticks")
    missing = [n for n, a in (("g_er_cd_reads", a_cd),
                              ("g_er_sprite_adds", a_add),
                              ("g_gp_draw_ticks", a_tick)) if a is None]
    if missing:
        print("  HARNESS GAP: symbols not in game.map: " + ", ".join(missing))
        print("  (rebuild with the FR-2 residency instrumentation)")
        sys.exit(2)

    import mcs_extract
    from pathlib import Path
    sections = mcs_extract.parse_savestate(Path(args.state))
    cd_reads    = _peek_u32(sections, a_cd)
    sprite_adds = _peek_u32(sections, a_add)
    ticks       = _peek_u32(sections, a_tick)

    rpt = (cd_reads / ticks) if ticks else float("inf")
    apt = (sprite_adds / ticks) if ticks else float("inf")

    print(f"  g_er_cd_reads    = {cd_reads}")
    print(f"  g_er_sprite_adds = {sprite_adds}")
    print(f"  g_gp_draw_ticks  = {ticks}  (~{ticks/60.0:.1f}s of gameplay)")
    print(f"  CD reads / tick  = {rpt:.3f}   (threshold {MAX_READS_PER_TICK})")
    print(f"  VDP1 adds / tick = {apt:.1f}   (informational; rebuild DMA cost)")
    print("-" * 62)

    if ticks < MIN_TICKS:
        print(f"  CAPTURE TOO SHALLOW: {ticks} draw ticks < {MIN_TICKS}. "
              f"Hold Right in GHZ gameplay long enough to accumulate "
              f"{MIN_TICKS}+ frames before F5 so warm-up reads amortize.")
        sys.exit(2)

    if rpt > MAX_READS_PER_TICK:
        print(f"  RED: {rpt:.3f} CD reads/tick > {MAX_READS_PER_TICK} -- the "
              f"192 KB MRU pool is thrashing (re-streaming atlas blobs from CD "
              f"every frame).")
        print(f"       At ~100 ms/seek this is a multi-frame stall every frame "
              f"-> the reported 'frame by frame slow'. entity_atlas.c:224 "
              f"wholesale-flush + 96 KB effective pool + whole-atlas blobs.")
        sys.exit(1)

    print(f"  GREEN: {rpt:.3f} CD reads/tick <= {MAX_READS_PER_TICK} -- CD "
          f"reads plateaued. Each distinct atlas streamed ~once; residency "
          f"persists across frames (no per-frame pool flush).")
    sys.exit(0)


if __name__ == "__main__":
    main()
