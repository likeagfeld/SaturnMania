#!/usr/bin/env python3
"""qa_jo_sprite_budget_gate.py - jo VDP1 sprite-table overflow gate (#189).

MEASURED root cause (this gate's RED capture, gameplay.mcs):
__jo_sprite_id reaches 407 at GHZ gameplay, but JO_MAX_SPRITE = 255
(jo-engine/jo_engine/jo/conf.h:67). The "Too many sprites" guard in
__internal_jo_sprite_add (sprites.c:189) is #ifdef JO_DEBUG only, so the
release build silently writes __jo_sprite_def[255..407] and
__jo_sprite_pic[255..407] PAST their fixed [255] arrays:

  __jo_sprite_def @ 0x...4904, +255*8 = exactly g_titlecard's address
    -> def[] overflow CLOBBERS the titlecard struct (garbage zone text).
  __jo_sprite_pic @ 0x...410c, +255*8 = __jo_sprite_def base
    -> pic[] overflow CLOBBERS def[] of earlier sprites -> their VDP1
       VRAM addresses (texture->adr) go wrong -> HUD/glyph garbage.

Why #188 exposed it: pre-#188 the GHZ load bailed on the FG.CEL pool
failure before loading the HUD (30) + titlecard (36) atlases, so the
count stayed lower. The #188 FG.CEL->LWRAM fix let the load proceed,
adding 66 more jo sprites on top of the entity atlases (275 frames),
tipping __jo_sprite_id past 255 -> the def[]/pic[] overflow above.

Per-atlas resident frame_total at capture (sum of ready atlases = 341,
+player + misc = 407):
  ring 40, itembox 40, titlecard 36, spikelog 32, buzz 32, hud 30,
  motobug 29, chopper 24, newtron 23, crabmeat 22, signpost 12,
  batbrain 11, platform 7, spikes 2, bridge 1, spring 0.

Predicate
---------
RUNTIME (default; needs the gameplay savestate gameplay.mcs):

  T1  Peek __jo_sprite_id from the savestate. GREEN requires
      __jo_sprite_id <= 254 (every jo sprite index in-bounds for the
      [255] def/pic arrays -> no table overflow, no titlecard/HUD
      clobber). RED on the current build: __jo_sprite_id == 407.

Exit codes:
    0 = GREEN (sprite table in bounds; no HUD/titlecard clobber)
    1 = RED   (sprite table overflowed JO_MAX_SPRITE)
    2 = harness error / runtime state not captured
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
STATE = os.path.join(HERE, "_hud_states", "gameplay.mcs")
JO_MAX_SPRITE = 255


def _map_text():
    with open(GAME_MAP, "r", errors="ignore") as f:
        return f.read()


def _sym_addr(txt, sym):
    # static LTO-privatised symbols appear as name.lto_priv.NNN
    m = re.search(r"0x([0-9a-fA-F]{8,})\s+_?" + re.escape(sym) +
                  r"(?:\.lto_priv\.\d+)?(?![A-Za-z0-9_])", txt)
    return int(m.group(1), 16) if m else None


def check_t1(state_path):
    if not os.path.exists(state_path):
        sys.stderr.write(
            "T1 HARNESS GAP: gameplay savestate not captured.\n"
            f"  Expected: {state_path}\n")
        return None, "T1 SKIPPED (state not captured)"
    if not os.path.exists(GAME_MAP):
        return False, "T1 FAIL: game.map not found (build first)"
    txt = _map_text()
    a_sid = _sym_addr(txt, "__jo_sprite_id")
    if a_sid is None:
        return False, "T1 FAIL: __jo_sprite_id not in game.map"

    import mcs_extract
    from pathlib import Path
    sections = mcs_extract.parse_savestate(Path(state_path))
    b = mcs_extract._peek_bytes(sections, a_sid, 4)
    # WRAM-H pair-swap (project gate convention).
    sid = (b[1] << 24) | (b[0] << 16) | (b[3] << 8) | b[2]
    if sid >= 0x80000000:
        sid -= 0x100000000

    print(f"      T1 diag: __jo_sprite_id={sid}  JO_MAX_SPRITE={JO_MAX_SPRITE}")
    if sid > JO_MAX_SPRITE - 1:
        return False, (f"T1 FAIL: __jo_sprite_id={sid} exceeds "
                       f"JO_MAX_SPRITE-1={JO_MAX_SPRITE - 1} -- "
                       f"def[]/pic[] table overflow corrupts titlecard+HUD")
    return True, (f"T1 OK (__jo_sprite_id={sid} <= {JO_MAX_SPRITE - 1}; "
                  f"sprite table in bounds)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--state", default=STATE)
    args = ap.parse_args()

    print("JO VDP1 SPRITE-TABLE OVERFLOW GATE (#189)")
    print("-" * 60)
    ok, msg = check_t1(args.state)
    if ok is None:
        print("  SKIP " + msg)
        print("-" * 60)
        print("HARNESS GAP: runtime state not captured; T1 unverified.")
        sys.exit(2)
    print(("  PASS " if ok else "  FAIL ") + msg)
    print("-" * 60)
    if ok:
        print("GREEN: jo sprite table in bounds (no HUD/titlecard clobber)")
        sys.exit(0)
    print("RED: jo sprite table overflowed JO_MAX_SPRITE")
    sys.exit(1)


if __name__ == "__main__":
    main()
