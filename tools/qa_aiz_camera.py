#!/usr/bin/env python3
"""qa_aiz_camera.py -- RED/GREEN gate for the #316 AIZ pan-back camera-follow fix.

Measures, from an AIZ-intro savestate (or live), the three quantities that define
the bug + the fix:
  * beat      = p6_w_aiz_cutscene_state (the live CutsceneSeq stateID; 0=EnterAIZ,
                1=EnterAIZJungle, 2=EnterHeavies, 3=P2FlyIn, 4+=EnterClaw...).
  * cam_state = SLOT_CAMERA1 (class 6) state ptr @ entity+88. Categorised vs the
                map symbol Camera_State_FollowXY: None(0) | FollowXY | other.
  * delta     = |camera.x - player1.x| px (the INV-camera quantity).

RED (the bug): during the run-right beats (cutscene_state 1..3) the camera is stuck
in None and does NOT track the player -> delta huge (measured 9686 on _aizwork.mcs).
GREEN (the fix, AIZTornadoPath_State_SetPlayerCamera restore mirrored by the census
nudge in p6_ovl_ghz.c): cam_state==FollowXY and delta<=CAM_TRACK_MAX.

Usage:
  python tools/qa_aiz_camera.py --state STATE.mcs
  python tools/qa_aiz_camera.py --live
Exit 0 = GREEN, 1 = RED, 2 = not an AIZ run-right state / read error.
"""
from __future__ import annotations

import argparse
import importlib.util
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_spec = importlib.util.spec_from_file_location("qa_trace", _HERE / "qa_trace.py")
qa_trace = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(qa_trace)  # type: ignore[union-attr]

CLASS_PLAYER = 8
CLASS_CAMERA = 6
OFF_STATE = 88          # EntityCamera.state @ RSDK_ENTITY base +88 (Object.hpp)
CAM_TRACK_MAX = 200     # px: the camera must frame the player during the run-right


def main(argv=None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--live", action="store_true")
    p.add_argument("--state")
    p.add_argument("--map", default=str(_HERE.parent / "game.map"))
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=55355)
    p.add_argument("--slots", type=int, default=400)
    a = p.parse_args(argv)
    if not a.live and not a.state:
        p.error("pass --live or --state STATE.mcs")

    map_text = Path(a.map).read_text(errors="replace")
    try:
        rd = qa_trace.Reader(a.live, a.state, a.host, a.port)
        s = qa_trace.sample(rd, map_text, a.slots)
    except Exception as e:  # noqa: BLE001
        sys.stderr.write(f"qa_aiz_camera: {e}\n"); return 2

    # beat: the AIZ cutscene stateID witness
    beat_sym = rd.sym(map_text, "p6_w_aiz_cutscene_state")
    beat = rd.r32(beat_sym) if beat_sym else None
    if beat is not None and beat > 0x7FFFFFFF:
        beat -= 0x100000000  # signed (it starts at -1)

    followxy = rd.sym(map_text, "Camera_State_FollowXY")
    players = [e for e in s["entities"] if e["classID"] == CLASS_PLAYER]
    cams = [e for e in s["entities"] if e["classID"] == CLASS_CAMERA]
    if not players or not cams:
        sys.stderr.write(f"qa_aiz_camera: not an active AIZ state (players={len(players)} cams={len(cams)})\n")
        return 2

    p0, c0 = players[0], cams[0]
    # camera->state ptr: re-walk to the camera's base (sample doesn't keep it)
    sa = rd.sym(map_text, "RSDK::objectEntityList")
    pool = rd.r32(sa) if sa else None
    if pool is None or not (0x00200000 <= pool < 0x00300000):
        pool = qa_trace.POOL_BASE_FALLBACK
    cam_state = rd.r32(pool + c0["slot"] * qa_trace.STRIDE + OFF_STATE)
    if cam_state == 0:
        cat = "None"
    elif followxy is not None and cam_state == followxy:
        cat = "FollowXY"
    else:
        cat = f"other(0x{cam_state:08X})"

    delta = abs(p0["x"] - c0["x"]) if (p0["x"] is not None and c0["x"] is not None) else None

    print(f"qa_aiz_camera: beat(cutscene_state)={beat}  cam_state={cat}  "
          f"cam.x={c0['x']} player.x={p0['x']} delta={delta}")
    print(f"  Camera_State_FollowXY=0x{followxy:08X}" if followxy else "  (FollowXY symbol not in map)")

    # The gate only judges the run-right beats (1..3) where the camera MUST follow.
    if beat is None or not (1 <= beat <= 3):
        print(f"qa_aiz_camera: -> N/A (beat={beat} not in the run-right window 1..3; capture deeper/shallower)")
        return 2

    ok = (cat == "FollowXY") and (delta is not None and delta <= CAM_TRACK_MAX)
    print(f"qa_aiz_camera: -> {'GREEN (camera follows the player)' if ok else 'RED (camera stuck -- pan-back)'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
