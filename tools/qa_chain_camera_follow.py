#!/usr/bin/env python3
"""qa_chain_camera_follow.py - user punch list v2 item 7: the camera must track
the player in the post-handoff playable Green Hill Zone leg of the full chain.

MEASURED RED baseline (2026-07-03, _v8_land_right.mcs, 12 s held Right):
Sonic ran to x=777 but screens[0].position=(52,780) -- X pinned at 52 = the
cutscene camera's final position (212) minus center (160). ROOT (savestate +
decomp): Zone->setATLBounds==1 (Zone+320) makes Camera_Create skip its state +
bounds init (Camera.c:72-86); the decomp clears the flag and re-wires
player->camera / camera->target / Camera_State_FollowXY inside
TitleCard_State_ShowTitleCard (TitleCard.c:504-514) -- and the Saturn engine
build has NO TitleCard, so no camera ever re-arms after the GHZCutscene->GHZ
handoff. Camera slot 60: state=NULL target=NULL bounds=0.

GREEN = in a held-Right landing savestate:
  1. player1.position.x (px) > 500 (input reached the game and Sonic ran), AND
  2. screens[0].position.x > 150 (left the 52 pin), AND
  3. the camera frames the player: player_px - screen_x within [0, 320].

Usage:
    python tools/qa_chain_camera_follow.py STATE.mcs [--map game.map]

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

ENTITY_WIDE_SIZE = 556


def map_symbol(map_text: str, name: str) -> int:
    m = re.search(r"0x([0-9a-fA-F]{16})\s+" + re.escape(name) + r"\s*$", map_text, re.M)
    if not m:
        raise KeyError(name)
    return int(m.group(1), 16)


def swap32(v: int) -> int:
    return ((v & 0xFF00FF00) >> 8) | ((v & 0x00FF00FF) << 8)


def main(argv=None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("state")
    p.add_argument("--map", default=str(_HERE.parent / "game.map"))
    a = p.parse_args(argv)

    map_text = Path(a.map).read_text(errors="replace")
    try:
        sym_pool = map_symbol(map_text, "RSDK::objectEntityList")
        # Link-API pointer variable holding &RSDK::screens[0] (the array itself
        # has no map symbol; the pointer was validated against live data).
        sym_screeninfo_ptr = map_symbol(map_text, "ScreenInfo")
    except KeyError as e:
        sys.stderr.write(f"qa_chain_camera_follow: symbol missing from map: {e}\n")
        return 2

    sections = mcs_extract.parse_savestate(Path(a.state))

    def peek32(addr: int) -> int:
        raw = mcs_extract._peek_bytes(sections, addr, 4)
        if raw is None:
            raise ValueError(f"address 0x{addr:08X} not in any savestate region")
        return swap32(int.from_bytes(raw, "big"))

    pool = peek32(sym_pool)
    player_px = peek32(pool) >> 16  # slot 0 position.x Q16.16 -> px
    screens = peek32(sym_screeninfo_ptr)
    # Saturn ScreenInfo: frameBuffer[1] (2B) + pad -> position.x at +4
    # (validated: size=(320,224) center=(160,112) at +12..+24).
    screen_x = peek32(screens + 4)

    ran = player_px > 500
    unpinned = screen_x > 150
    framed = 0 <= (player_px - screen_x) <= 320
    ok = ran and unpinned and framed
    print(f"  player1.x = {player_px} px ({'ran' if ran else 'DID NOT RUN'})")
    print(f"  screen.x  = {screen_x} px ({'unpinned' if unpinned else 'PINNED (52-class)'})")
    print(f"  framing   = player-screen delta {player_px - screen_x} "
          f"({'in-view' if framed else 'OUT OF VIEW'})")
    verdict = "GREEN" if ok else "RED"
    print(f"qa_chain_camera_follow: -> {verdict}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
