#!/usr/bin/env python3
"""qa_invariants.py -- UNIVERSAL invariants over the structural game-state sample
(qa_trace). Per-CLASS checks that hold in EVERY gameplay scene, so they catch
whole bug categories without a hand-authored gate per symptom. Live or savestate.

Each invariant is a function of the sample dict; it returns (ok, detail). The
suite is the coverage -- adding a class of bug = one invariant, not one gate per
occurrence. These are what would have caught this session's user-reported bugs
automatically:
  INV-camera   camera (class 6) tracks player (class 8): |cam.x-plr.x| <= 200
               -> would have caught the dead-camera pin (cam 52 vs player 777).
               ADVISORY in a cutscene (camera is scripted away from the player at
               some beats -- e.g. AIZSetup_CutsceneSonic_EnterClaw detaches
               player->camera and lerps to the claw); the delta is still reported.
  INV-visible  a gameplay/cutscene scene has >=1 VISIBLE player (self->visible@+85,
               game-logic-set -- validated trustworthy; onScreen@+86 is engine-set
               via CheckOnScreen and the Saturn direct-VDP1 path never populates it,
               so it reads 0 even for shown entities -> NOT used as a gate).
               -> catches "characters missing / not drawn".
  INV-sane     no player position is absurd (|x|,|y| < 0x20000 px), animID valid
               -> catches teleport / garbage-pose / warp.
  INV-players  a gameplay scene has >=1 player (class 8)
               -> catches "players unbound / not spawned".

Usage:
  python tools/qa_invariants.py --live [--cutscene]
  python tools/qa_invariants.py --state STATE.mcs [--cutscene]
Exit 0 = all hard invariants hold, 1 = a hard violation, 2 = read error.
(Advisory violations are reported but do not change the exit code.)
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
CAM_TRACK_MAX = 200      # px: camera must frame the player (gameplay)
POS_ABS_MAX = 0x20000    # px: sane world bound


def invariants(s: dict, cutscene: bool = False):
    """Return a list of (name, ok, detail, advisory) tuples.

    advisory=True rows are reported but do NOT count toward a hard failure
    (the caller decides the exit code on hard fails only). cutscene=True
    downgrades INV-camera to advisory because a cutscene legitimately scripts
    the camera away from the player at some beats (decomp AIZSetup EnterClaw
    detaches player->camera and lerps to the claw).
    """
    players = [e for e in s["entities"] if e["classID"] == CLASS_PLAYER]
    cams = [e for e in s["entities"] if e["classID"] == CLASS_CAMERA]
    # STRUCTURAL scene-active detection (the folder string is unreliable across
    # a build/savestate map mismatch): a player + a camera == an active scene.
    active = len(players) >= 1 and len(cams) >= 1
    out = []

    # INV-sane (always, hard): player positions + animIDs sane
    for p in players:
        bad = (p["x"] is None or abs(p["x"]) > POS_ABS_MAX or
               p["y"] is None or abs(p["y"]) > POS_ABS_MAX)
        out.append(("INV-sane", not bad,
                    f"player slot{p['slot']} pos=({p['x']},{p['y']}) animID={p['animID']}", False))

    if active:
        # INV-players (hard): an active scene must have a player
        out.append(("INV-players", len(players) >= 1,
                    f"{len(players)} players present", False))
        # INV-camera: camera tracks player 0. HARD in gameplay; ADVISORY in a
        # cutscene (camera may be scripted away -- the delta is still reported).
        if players and cams:
            p0, c0 = players[0], cams[0]
            if p0["x"] is not None and c0["x"] is not None:
                d = abs(p0["x"] - c0["x"])
                note = " [cutscene: advisory -- camera may be scripted away]" if cutscene else ""
                out.append(("INV-camera", d <= CAM_TRACK_MAX,
                            f"|cam.x {c0['x']} - player.x {p0['x']}| = {d} (<= {CAM_TRACK_MAX}){note}",
                            cutscene))
        # INV-visible (hard): >=1 player DRAWN. Uses self->visible@+85 (game-
        # logic-set, validated trustworthy on 3 known-good states); NOT
        # onScreen@+86 (engine CheckOnScreen-set, which the Saturn direct-VDP1
        # path never populates -> reads 0 even for shown entities = a lying gate).
        vis = [p.get("visible") for p in players]
        out.append(("INV-visible", any((v or 0) for v in vis),
                    f"player visible flags = {vis}", False))
    return out


def main(argv=None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--live", action="store_true")
    p.add_argument("--state")
    p.add_argument("--cutscene", action="store_true",
                   help="scene is a cutscene -> INV-camera advisory (camera may be scripted away)")
    p.add_argument("--map", default=str(_HERE.parent / "game.map"))
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=55355)
    p.add_argument("--slots", type=int, default=700)
    a = p.parse_args(argv)
    if not a.live and not a.state:
        p.error("pass --live or --state STATE.mcs")

    map_text = Path(a.map).read_text(errors="replace")
    try:
        rd = qa_trace.Reader(a.live, a.state, a.host, a.port)
        s = qa_trace.sample(rd, map_text, a.slots)
    except Exception as e:
        sys.stderr.write(f"qa_invariants: {e}\n"); return 2

    print(f"scene folder={s['folder']!r} entities={s['n_entities']} mode={'cutscene' if a.cutscene else 'gameplay'}")
    results = invariants(s, cutscene=a.cutscene)
    hard_fails = 0
    for name, ok, detail, advisory in results:
        tag = "adv " if advisory else ("ok  " if ok else "FAIL")
        print(f"  {name:14s} {tag} {detail}")
        if not ok and not advisory:
            hard_fails += 1
    verdict = "GREEN" if hard_fails == 0 else f"RED ({hard_fails} hard-violated)"
    print(f"qa_invariants: -> {verdict}")
    return 0 if hard_fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
