#!/usr/bin/env python3
"""qa_ghz_frametime_gate.py - GHZ gameplay frame-time / slow-motion gate (#192).

WHY THIS EXISTS
---------------
Task #192's "player-only resident" change fixed the jo sprite-table overflow
(qa_jo_sprite_peak_gate.py: peak 407 -> 77, far under 255). But the user then
reported the OPPOSITE failure: not a crash, "crazy slow motion" 45-60s into
GHZ gameplay. A savestate confirmed s_ts_state == TS_GHZ_ACTIVE (in gameplay,
not stuck in titlecard teardown) yet only g_gp_draw_ticks == 161 draw passes
had run over a long wall-clock -> the frame loop collapsed to a fraction of
60 Hz. peak == 77 rules OUT the #189 overflow as the cause; this is a per-tick
COST problem the FR-2 plan explicitly flagged:
  "Per-tick rebuild cost: ~40-60 jo_sprite_add/tick (each a small DMA)...
   Need to verify frame-time stays in budget (measure via savestate tick
   counter, not 'looks smooth')."

There are exactly two candidate costs per gameplay draw pass:
  (A) CD thrash. The 192 KB entity MRU pool (entity_atlas.c ENTITY_POOL_SIZE)
      flushes when s_epool_head + ENTITY_MAX_BLOB(96 KB) > 192 KB -- i.e. once
      the resident blob set passes ~96 KB, EVERY new distinct atlas flushes
      the whole pool and re-CD-streams. SPRING.SP2 alone is 94 KB. If the
      on-screen distinct-atlas working set exceeds ~96 KB, _blob_get re-reads
      from CD every tick. One GFS seek+read ~= 100 ms ~= 6 frames of stall.
      This is the dominant slow-motion suspect.
  (B) VDP1 upload DMA. entity_residency_begin_frame frees the dynamic block
      every tick, so _er_ensure re-jo_sprite_add's ~77 on-screen frames each
      tick. ~77 small DMAs/tick is real cost but does not, alone, produce a
      10x slowdown.

Two behavior-neutral never-reset counters in entity_atlas.c separate them:
  g_er_cd_reads    - jo_fs_read_file_ptr CD streams in _blob_get
  g_er_sprite_adds - jo_sprite_add VDP1 uploads in _er_ensure
Divided by the gameplay-draw-pass delta they give the per-tick rate of each.

MEASUREMENT PROTOCOL (two captures isolate steady state from warm-up):
  1. Build the instrumented ISO; boot it; press Start.
  2. PLAY into the slow section (~45-60s).
  3. Press F5 -> save state A.
  4. Keep playing ~10 real seconds (count them), press F5 -> save state B.
  5. Run:
       python tools/qa_ghz_frametime_gate.py \
              --state-a A.mcs --state-b B.mcs --wall-seconds 10

Predicate
---------
  fps           = (draw_ticks_B - draw_ticks_A) / wall_seconds
  cd_per_tick   = (cd_reads_B   - cd_reads_A)   / (draw_ticks_B - draw_ticks_A)
  add_per_tick  = (sprite_adds_B- sprite_adds_A)/ (draw_ticks_B - draw_ticks_A)

  RED (slow-motion present, cause attributed) when:
     fps < FPS_FLOOR (50)                  -- the frame loop is collapsed, OR
     cd_per_tick >= CD_THRASH (1.0)        -- sustained >=1 CD read per tick.
  GREEN when fps >= FPS_FLOOR AND cd_per_tick < CD_THRASH.

Single-state fallback (--state-a only): no fps, warm-up-inclusive rates only;
RED if cd_reads/draw_ticks >= CD_THRASH, else INCONCLUSIVE (exit 2).

Exit codes:
    0 = GREEN (frame time in budget, no CD thrash)
    1 = RED   (slow motion measured / CD thrash measured)
    2 = harness error / capture too shallow / inconclusive
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
MIN_TICKS = 1500            # ~25s of draw passes; below this the capture did
                           # not reach the reported slow section.
FPS_FLOOR = 50.0           # healthy GHZ runs ~60; below 50 is the regression.
CD_THRASH = 1.0            # sustained >=1 CD blob read per draw pass == thrash.

SYMS = ("g_gp_draw_ticks", "g_er_cd_reads", "g_er_sprite_adds")


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
    # WRAM-H pair-swap (project gate convention; qa_jo_sprite_peak_gate).
    return ((b[1] << 24) | (b[0] << 16) | (b[3] << 8) | b[2]) & 0xFFFFFFFF


def _read_state(path, addrs):
    import mcs_extract
    from pathlib import Path
    sections = mcs_extract.parse_savestate(Path(path))
    return {n: _peek_u32(sections, a) for n, a in addrs.items()}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--state-a", required=True,
                    help="first gameplay savestate (.mcs/.mc0)")
    ap.add_argument("--state-b", default=None,
                    help="second savestate, ~wall-seconds later (recommended)")
    ap.add_argument("--wall-seconds", type=float, default=0.0,
                    help="real seconds elapsed between the two F5 saves")
    args = ap.parse_args()

    print("GHZ GAMEPLAY FRAME-TIME / SLOW-MOTION GATE (#192)")
    print("-" * 64)

    if not os.path.exists(GAME_MAP):
        print("  HARNESS GAP: game.map not found (build first)")
        sys.exit(2)
    for p in (args.state_a, args.state_b):
        if p and not os.path.exists(p):
            print(f"  HARNESS GAP: state not found: {p}")
            sys.exit(2)

    txt = _map_text()
    addrs = {n: _sym_addr(txt, n) for n in SYMS}
    missing = [n for n, a in addrs.items() if a is None]
    if missing:
        print("  HARNESS GAP: symbols not in game.map: " + ", ".join(missing))
        print("  (rebuild with the #192 frame-time counters in entity_atlas.c)")
        sys.exit(2)

    a = _read_state(args.state_a, addrs)
    print(f"  state A: draw_ticks={a['g_gp_draw_ticks']} "
          f"cd_reads={a['g_er_cd_reads']} "
          f"sprite_adds={a['g_er_sprite_adds']}")

    if args.state_b:
        b = _read_state(args.state_b, addrs)
        print(f"  state B: draw_ticks={b['g_gp_draw_ticks']} "
              f"cd_reads={b['g_er_cd_reads']} "
              f"sprite_adds={b['g_er_sprite_adds']}")
        dt = b["g_gp_draw_ticks"] - a["g_gp_draw_ticks"]
        dcd = b["g_er_cd_reads"] - a["g_er_cd_reads"]
        dadd = b["g_er_sprite_adds"] - a["g_er_sprite_adds"]
        print("-" * 64)
        if b["g_gp_draw_ticks"] < MIN_TICKS:
            print(f"  CAPTURE TOO SHALLOW: draw_ticks {b['g_gp_draw_ticks']} "
                  f"< {MIN_TICKS}. Play deeper (the slowdown is at 45-60s).")
            sys.exit(2)
        if dt <= 0:
            print(f"  HARNESS GAP: draw_ticks did not advance A->B (dt={dt}). "
                  f"Save B AFTER A with gameplay running between them.")
            sys.exit(2)
        cd_pt = dcd / dt
        add_pt = dadd / dt
        print(f"  steady-state (B-A, warm-up excluded):")
        print(f"    cd_reads/tick    = {cd_pt:.3f}")
        print(f"    sprite_adds/tick = {add_pt:.2f}")
        fps = None
        if args.wall_seconds > 0:
            fps = dt / args.wall_seconds
            print(f"    draw passes A->B = {dt} over {args.wall_seconds:.1f}s"
                  f"  -> {fps:.1f} fps (target ~60)")
        print("-" * 64)
        # Attribution.
        if cd_pt >= CD_THRASH:
            print(f"  CAUSE = CD THRASH: {cd_pt:.2f} blob reads/tick. The "
                  f"192 KB entity MRU pool is re-streaming atlas SP2 blobs "
                  f"from CD every tick (working set > ~96 KB). ~100 ms/seek "
                  f"-> the slow motion. Fix targets the pool, not the DMA.")
        elif add_pt >= 40.0:
            print(f"  CAUSE = REBUILD DMA: {add_pt:.0f} jo_sprite_add/tick "
                  f"with cd_reads/tick {cd_pt:.2f} (no thrash). The per-tick "
                  f"free+re-add of every on-screen frame is the cost. Fix "
                  f"persists VDP1 sprites across ticks instead of rebuilding.")
        red = (fps is not None and fps < FPS_FLOOR) or cd_pt >= CD_THRASH
        if red:
            why = []
            if fps is not None and fps < FPS_FLOOR:
                why.append(f"fps {fps:.1f} < {FPS_FLOOR:.0f}")
            if cd_pt >= CD_THRASH:
                why.append(f"cd_reads/tick {cd_pt:.2f} >= {CD_THRASH:.1f}")
            print(f"  RED: slow motion measured ({'; '.join(why)}).")
            sys.exit(1)
        print(f"  GREEN: frame time in budget "
              f"(fps {fps if fps else 'n/a'}, cd/tick {cd_pt:.2f}).")
        sys.exit(0)

    # Single-state fallback (warm-up-inclusive).
    print("-" * 64)
    dt = a["g_gp_draw_ticks"]
    if dt < MIN_TICKS:
        print(f"  CAPTURE TOO SHALLOW: draw_ticks {dt} < {MIN_TICKS}.")
        sys.exit(2)
    cd_pt = a["g_er_cd_reads"] / dt
    add_pt = a["g_er_sprite_adds"] / dt
    print(f"  warm-up-inclusive (single state):")
    print(f"    cd_reads/tick    = {cd_pt:.3f}")
    print(f"    sprite_adds/tick = {add_pt:.2f}")
    print("-" * 64)
    if cd_pt >= CD_THRASH:
        print(f"  RED: CD thrash measured (cd_reads/tick {cd_pt:.2f} >= "
              f"{CD_THRASH:.1f}). Pool re-streams blobs every tick.")
        sys.exit(1)
    print(f"  INCONCLUSIVE: no CD thrash in a single warm-up-inclusive "
          f"capture, but single-state cannot measure fps. Capture a second "
          f"state ~10s later with --state-b/--wall-seconds to measure fps "
          f"and exclude warm-up.")
    sys.exit(2)


if __name__ == "__main__":
    main()
