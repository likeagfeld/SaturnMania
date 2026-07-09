#!/usr/bin/env python3
"""qa_chain_player_pose.py - F-LAND-POSE gate: players animate from the GHZ
player pack (not the leftover cutscene pack) in the chain's playable landing.

MEASURED RED baseline (2026-07-03, _r1_land.mcs t=300): both players sit in the
real Player_State_Ground (seam parity OK) but animator.frames points INTO
P6_HW_OBJANIMPAK (HBHOBJ.PAK, the cutscene Heavies/warp pack: 0x22767bb4 /
0x22768334) with animationID=1 (flight) because the chain flavor never mounts
GHZANIM.PAK (p6_w_apk_bytes=0; the WRAM-H window would clobber .bss, #228).
SetSpriteAnimation(aniFrames=-1, ...) no-ops, so the flying pose sticks.

GREEN = p6_w_apk_bytes > 0 AND both players' animator.frames fall inside the
front-end player-pack cart window [0x22744000, +0x11000).

Usage:
    python tools/qa_chain_player_pose.py STATE.mcs [--map game.map]

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

PACK_BASE = 0x225E0000  # FINAL CARVE base (below the SaturnLayout region 0x22600000+)
PACK_CAP = 0x00011000
FRAMES_ARENA_OFF = 0x77C  # member 0 (Sonic.bin) frames start; animators must point >= here
ENTITY_WIDE_SIZE = 556
OFF_STATE = 88
OFF_FRAMES = 104
OFF_ANIMID = 112


def map_symbol(map_path: Path, name: str) -> int:
    pat = re.compile(r"0x([0-9a-fA-F]{16})\s+" + re.escape(name) + r"\s*$")
    for line in map_path.read_text(errors="replace").splitlines():
        m = pat.search(line)
        if m:
            return int(m.group(1), 16)
    raise KeyError(name)


def swap32(v: int) -> int:
    return ((v & 0xFF00FF00) >> 8) | ((v & 0x00FF00FF) << 8)


def peek32(sections, addr: int) -> int:
    raw = mcs_extract._peek_bytes(sections, addr, 4)
    if raw is None:
        raise ValueError(f"address 0x{addr:08X} not in any savestate region")
    # mcs stores WRAM byte-pair-swapped (gotcha #10); big-endian unpack then swap.
    return swap32(int.from_bytes(raw, "big"))


def main(argv=None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("state")
    p.add_argument("--map", default=str(_HERE.parent / "game.map"))
    a = p.parse_args(argv)

    mp = Path(a.map)
    try:
        sym_pool_ptr = map_symbol(mp, "RSDK::objectEntityList")
        sym_apk = map_symbol(mp, "RSDK::p6_w_apk_bytes")
    except KeyError as e:
        sys.stderr.write(f"qa_chain_player_pose: symbol missing from map: {e}\n")
        return 2

    sections = mcs_extract.parse_savestate(Path(a.state))

    apk = peek32(sections, sym_apk)
    pool = peek32(sections, sym_pool_ptr)
    ok = apk > 0
    print(f"  p6_w_apk_bytes = {apk} ({'mounted' if apk else 'NOT MOUNTED'})")
    print(f"  entity pool    = 0x{pool:08X}")
    for slot, who in ((0, "Sonic"), (1, "Tails")):
        base = pool + slot * ENTITY_WIDE_SIZE
        state = peek32(sections, base + OFF_STATE)
        frames = peek32(sections, base + OFF_FRAMES)
        animid = peek32(sections, base + OFF_ANIMID) >> 16
        # F-LAND-SONIC hardening: in-window is NOT enough (frames[-1] poison sat
        # inside the window) -- require the pointer INSIDE the frames arena.
        in_pack = PACK_BASE + FRAMES_ARENA_OFF <= frames < PACK_BASE + PACK_CAP
        ok = ok and in_pack
        print(f"  {who}: state=0x{state:08X} frames=0x{frames:08X} "
              f"({'FRAMES ARENA' if in_pack else 'BAD/POISONED'}) animID.hi16=0x{animid:04X}")
    verdict = "GREEN" if ok else "RED"
    print(f"qa_chain_player_pose: -> {verdict}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
