#!/usr/bin/env python3
"""qa_hud_present_gate.py - HUD-present PARITY gate (#186).

The user reported the in-game HUD "completely disappeared ... you have broken
that over several of the last iterations". The Score/Time/Rings/Life overlay
(HUD_Draw, decomp HUD.c) renders nothing in GHZ gameplay.

This gate makes the regression measurable and discriminates the root cause
among the candidates that accumulated as entities and the FR-1 player atlas
were added (the HUD is drawn LAST in mania_ghz_draw_only, so it is the first
casualty of any sprite-list / sprite-id pressure):

  C1  SGL sprite-list / draw-order: hud_draw runs and issues blits, but they
      never reach the screen (culled, overflowed, or behind another layer).
  C2  player rewind wipes HUD ids: player_atlas_play() does
      jo_sprite_free_from(s_player_base); if s_player_base <= the HUD's
      sprite ids, the rewind frees the HUD sprites every anim change.
  C3  HUD atlas never became ready (load/upload failed).

Predicates
----------
STATIC (default; no emulator):

  H1  cd/HUD.SP2 (magic SPR2) and cd/HUD.MET (magic MET1) exist.
  H2  The diag mirror symbols g_hud_diag_ready / _base / _sid0 / _blits and
      player_atlas_base are present in game.map (so the runtime peek works).

RUNTIME (--runtime; needs a gameplay savestate captured with
tools/qa_savestate.ps1 -PressStartAt ... written to
tools/_hud_states/gameplay.mcs):

  R1  Peek the four diag mirrors from the gameplay state and require the HUD
      to be actually drawn:
        ready == 1            (atlas loaded)            else -> C3
        sid0  >= 0            (frames uploaded)         else -> C3
        base  >  sid0         (player rewind safe)      else -> C2
        blits >= MIN_BLITS    (draw reached jo)         else -> C1
      The capture must be in gameplay (s_ts_state == 6 = TS_GHZ_ACTIVE),
      else the state is rejected (not a false RED).

Exit codes:
    0 = GREEN (HUD present)
    1 = RED   (HUD absent; the printed diag values name the candidate)
    2 = harness error / runtime state not captured
"""
from __future__ import annotations
import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)

SPR2_MAGIC = b"SPR2"
MET1_MAGIC = b"MET1"

HUD_SP2 = os.path.join(REPO, "cd", "HUD.SP2")
HUD_MET = os.path.join(REPO, "cd", "HUD.MET")
GAME_MAP = os.path.join(REPO, "game.map")
STATE = os.path.join(HERE, "_hud_states", "gameplay.mcs")

# player_atlas_base() is a one-line getter; GCC 8.2 LTO inlines it at its
# single call site in hud_draw, so it has no standalone symbol. Its value
# still flows into g_hud_diag_base (the mirror R1 actually peeks), so we do
# NOT require the function symbol here.
DIAG_SYMS = ["g_hud_diag_ready", "g_hud_diag_base", "g_hud_diag_sid0",
             "g_hud_diag_blits"]

# HUD_Draw issues: Score label + 6 score digits + Time label + 2 colons +
# m + ss + cc digits + Rings label + ring digits + Life icon. Even the
# minimum (single-digit rings, zero score) is well over 10 blits.
MIN_BLITS = 10


def _read_magic(path, magic):
    try:
        with open(path, "rb") as f:
            return f.read(4) == magic
    except OSError:
        return False


def check_h1():
    if not _read_magic(HUD_SP2, SPR2_MAGIC):
        return False, "H1 FAIL: cd/HUD.SP2 missing or wrong magic"
    if not _read_magic(HUD_MET, MET1_MAGIC):
        return False, "H1 FAIL: cd/HUD.MET missing or wrong magic"
    return True, "H1 OK (HUD.SP2 + HUD.MET present)"


def _map_text():
    with open(GAME_MAP, "r", errors="ignore") as f:
        return f.read()


def _has_symbol(txt, sym):
    return re.search(r"(?<![A-Za-z0-9_])_?" + re.escape(sym) +
                     r"(?![A-Za-z0-9_])", txt) is not None


def check_h2():
    if not os.path.exists(GAME_MAP):
        return False, "H2 FAIL: game.map not found (build first)"
    txt = _map_text()
    missing = [s for s in DIAG_SYMS if not _has_symbol(txt, s)]
    if missing:
        return False, f"H2 FAIL: symbols absent from game.map: {missing}"
    return True, f"H2 OK ({len(DIAG_SYMS)} diag symbols present)"


def _sym_addr(txt, sym):
    m = re.search(r"0x([0-9a-fA-F]{8,})\s+_?" + re.escape(sym) +
                  r"(?![A-Za-z0-9_])", txt)
    return int(m.group(1), 16) if m else None


def check_r1():
    if not os.path.exists(STATE):
        sys.stderr.write(
            "R1 HARNESS GAP: gameplay savestate not captured.\n"
            "  Capture it with:\n"
            "    pwsh tools/qa_savestate.ps1 -Cue game.cue -PressStartAt 30 "
            "-SaveFrame 44 -Out tools/_hud_states/gameplay.mcs\n"
            "  then re-run with --runtime.\n")
        return None, "R1 SKIPPED (gameplay state not captured)"

    import mcs_extract  # noqa: E402
    from pathlib import Path

    txt = _map_text()
    a_ready = _sym_addr(txt, "g_hud_diag_ready")
    a_base = _sym_addr(txt, "g_hud_diag_base")
    a_sid0 = _sym_addr(txt, "g_hud_diag_sid0")
    a_blits = _sym_addr(txt, "g_hud_diag_blits")
    a_ts = _sym_addr(txt, "s_ts_state")
    a_lf = _sym_addr(txt, "g_hud_diag_loadfail")
    a_fid = _sym_addr(txt, "g_hud_diag_fid")
    a_pool = _sym_addr(txt, "g_hud_diag_poolok")
    if None in (a_ready, a_base, a_sid0, a_blits):
        return False, "R1 FAIL: diag symbol addresses not in game.map"

    sections = mcs_extract.parse_savestate(Path(STATE))

    # Work RAM-H (0x06xxxxxx) is serialised byte-pair-swapped by Mednafen.
    def s32(addr):
        b = mcs_extract._peek_bytes(sections, addr, 4)
        v = (b[1] << 24) | (b[0] << 16) | (b[3] << 8) | b[2]
        return struct.unpack(">i", struct.pack(">I", v))[0]

    def u32(addr):
        b = mcs_extract._peek_bytes(sections, addr, 4)
        return (b[1] << 24) | (b[0] << 16) | (b[3] << 8) | b[2]

    ready = s32(a_ready)
    base = s32(a_base)
    sid0 = s32(a_sid0)
    blits = s32(a_blits)
    ts = u32(a_ts) if a_ts is not None else -1
    loadfail = s32(a_lf) if a_lf is not None else -99
    fid = s32(a_fid) if a_fid is not None else -99
    poolok = s32(a_pool) if a_pool is not None else -99

    lf_name = {0: "OK", 1: "SP2-read-NULL/pool", 2: "SP2-magic",
               3: "0-frames-uploaded/VDP1", 4: "MET-read-NULL/pool",
               5: "MET-magic", 9: "null-args", -2: "in-progress",
               -3: "hud_load-never-ran"}.get(loadfail, "?")
    print(f"      R1 diag: ready={ready} base={base} sid0={sid0} "
          f"blits={blits} ts={ts} loadfail={loadfail}({lf_name})")
    fid_name = ("not-registered(GFS dir table)" if fid < 0 else
                f"registered id={fid}")
    pool_name = {1: "pool-OK (malloc succeeded)", 0: "POOL-EXHAUSTED",
                 -1: "skipped (fid<0)"}.get(poolok, "?")
    print(f"      R1 split: fid={fid}({fid_name}) "
          f"poolok={poolok}({pool_name})")

    if a_ts is not None and ts != 6:
        return False, (f"R1 REJECT: state not in gameplay (s_ts_state={ts}, "
                       f"need 6=TS_GHZ_ACTIVE); recapture deeper")

    if ready != 1:
        return False, (f"R1 FAIL [C3]: HUD atlas not ready (ready={ready}); "
                       f"hud_load/entity_atlas_load failed")
    if sid0 < 0:
        return False, (f"R1 FAIL [C3]: HUD frames not uploaded (sid0={sid0})")
    if base <= sid0:
        return False, (f"R1 FAIL [C2]: player base {base} <= HUD sid0 {sid0}; "
                       f"player_atlas rewind frees the HUD sprites")
    if blits < MIN_BLITS:
        return False, (f"R1 FAIL [C1]: hud_draw issued only {blits} blits "
                       f"(need >= {MIN_BLITS}); draw path culled/overflowed")
    return True, (f"R1 OK (ready=1, sid0={sid0}, base={base}>sid0, "
                  f"blits={blits} HUD drawn in gameplay)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--runtime", action="store_true",
                    help="also peek the gameplay savestate diag mirrors")
    args = ap.parse_args()

    print("HUD-PRESENT GATE (#186)")
    print("-" * 60)
    results = []
    for fn in (check_h1, check_h2):
        ok, msg = fn()
        results.append(ok)
        print(("  PASS " if ok else "  FAIL ") + msg)

    if args.runtime:
        ok, msg = check_r1()
        if ok is None:
            print("  SKIP " + msg)
            print("-" * 60)
            print("HARNESS GAP: runtime state not captured; static verdict "
                  "stands but R1 unverified.")
            sys.exit(2)
        results.append(ok)
        print(("  PASS " if ok else "  FAIL ") + msg)

    print("-" * 60)
    if all(results):
        print("GREEN: HUD present")
        sys.exit(0)
    print("RED: HUD absent / not provably drawn")
    sys.exit(1)


if __name__ == "__main__":
    main()
