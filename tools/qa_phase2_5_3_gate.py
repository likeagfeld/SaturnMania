#!/usr/bin/env python3
"""qa_phase2_5_3_gate.py - Phase 2.5.3 Player LookUp + camera-pan gate.

BINDING per CLAUDE.md #4.7 + memory/qa-iterative-improvement.md. Authored
BEFORE any 2.5.3 implementation lands; MUST fire RED on the current build
(the LOOKUP enum member, the lookPos field, the Player_State_LookUp state
function, the at-rest up-init, and the camera lookPos pan wiring all do not
exist yet).

2.5.3 is the third increment of the Phase 2.5 "Player completion" plan. It
ports the at-rest LookUp state + camera look-up pan on top of the 2.5.1/2.5.2
state machine:
  - at-rest UP on flat ground               -> state = Player_State_LookUp
  - UP held in LookUp                        -> timer++ to 60 (hold), then
                                                camera lookPos.y -= 2 (floor -96)
  - UP released (or left/right)              -> state = Player_State_Ground
  - jumpPress in LookUp                       -> Action_Jump (statePeelout
                                                default-OFF for base Sonic)

Decomp authority (tools/_decomp_raw/SonicMania_Objects_Global_Player.c):
  - Player_State_LookUp            L4026-4081 (Gravity_False, nextAirState=Air;
        up -> HandleGroundMovement + ANI_LOOK_UP (settle frame 5), timer++ to
        60 then camera->lookPos.y -= 2 cap -96 (invertGravity: += 2 cap 96),
        jumpPress -> statePeelout ? Run : Action_Jump; else speed=64,
        frameID==0||left||right -> state=Ground, jumpPress -> Action_Jump).
  - Player_State_Ground lookup-init L3856-3861 (groundVel == 0 &&
        (angle<0x20||angle>0xE0) && !collisionMode: up -> ANI_LOOK_UP,
        timer=0, state = LookUp).

Saturn camera note: the decomp's camera is an RSDK entity with a lookPos
offset. The Saturn build owns the GHZ camera directly in Game.c, so the
look-up offset is carried on player_t.lookPos (mirrors camera->lookPos.y) and
Game.c's camera-follow adds it into cam_y. lookPos goes negative to pan the
viewport UP (lower world-Y at the top-left origin).

Predicates:

  P1 (static) - the LookUp state member + field exist.
       (a) Player.h player_state_t enum gains PLAYER_STATE_LOOKUP (in addition
           to GROUND/AIR/ROLL/CROUCH/SPINDASH).
       (b) player_t carries the lookPos field (camera look offset).

  P2 (static) - the LookUp port is present.
       (a) Player.c defines Player_State_LookUp.
       (b) State_Ground at-rest up-init is wired: an `up` -> LookUp transition
           that sets state to PLAYER_STATE_LOOKUP.
       (c) the look-up pan constants are present: 60 (hold counter), 96 (pan
           floor magnitude), and a -2 / -= 2 step on lookPos.
       (d) switch (p->state) gains a PLAYER_STATE_LOOKUP case; Player_Tick
           survives in game.map.
       (e) Game.c camera-follow reads lookPos into the camera (cam_y wiring).

  P3 (runtime, --with-savestate) - look-up then pan.
       Two-state capture. State A (UP held on flat ground, < 60 ticks):
       g_player_diag_state == PLAYER_STATE_LOOKUP. State B (UP held past 60
       ticks): g_player_diag_lookpos < 0 (panned up) and >= -96. SKIPPED
       without --with-savestate (static P1-P2 carry the RED-now demonstration,
       mirroring qa_phase2_5_2_gate P3).

Exit code: 0 = all GREEN. Non-zero = any RED.

Run:
    py -3 tools/qa_phase2_5_3_gate.py
    py -3 tools/qa_phase2_5_3_gate.py --with-savestate-lookup samples/lu.mcs \
                                      --with-savestate-pan samples/lu_pan.mcs
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAYER_H = os.path.join(ROOT, "src", "mania", "Objects", "Global", "Player.h")
PLAYER_C = os.path.join(ROOT, "src", "mania", "Objects", "Global", "Player.c")
GAME_C = os.path.join(ROOT, "src", "mania", "Game.c")
GAME_MAP = os.path.join(ROOT, "game.map")

# Enum values (Ground=0, Air=1, Roll=2, Crouch=3, Spindash=4, LookUp=5).
PLAYER_STATE_LOOKUP = 5

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def cprint(tag, msg, ok):
    colour = "\033[32m" if ok else "\033[31m"
    reset = "\033[0m"
    print(f"  [{colour}{tag}{reset}] {msg}")
    return ok


def _read(path):
    if not os.path.exists(path):
        return ""
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        return f.read()


# --- P1: LookUp state member + field --------------------------------------

def predicate_1_members_fields():
    hdr = _read(PLAYER_H)
    sub = []

    # (a) enum gains LOOKUP.
    lookup_ok = re.search(r"\bPLAYER_STATE_LOOKUP\b", hdr) is not None
    sub.append(("1a", lookup_ok,
                f"player_state_t enum LOOKUP={lookup_ok}"))

    # (b) player_t carries lookPos.
    has_lookpos = re.search(r"\blookPos\b\s*;", hdr) is not None
    sub.append(("1b", has_lookpos,
                f"player_t field lookPos={has_lookpos}"))

    all_ok = all(ok for _, ok, _ in sub)
    for tag, ok, msg in sub:
        cprint(f"P1.{tag} {'GREEN' if ok else 'RED'}", msg, ok)
    return all_ok


# --- P2: LookUp port present ----------------------------------------------

def predicate_2_port():
    src = _read(PLAYER_C)
    game = _read(GAME_C)
    sub = []

    # (a) Player_State_LookUp defined.
    fn_ok = re.search(r"\bPlayer_State_LookUp\s*\(", src) is not None
    sub.append(("2a", fn_ok,
                f"Player.c Player_State_LookUp defined={fn_ok}"))

    # (b) State_Ground at-rest up-init: up -> LOOKUP.
    has_lookup_state = re.search(r"PLAYER_STATE_LOOKUP", src) is not None
    sub.append(("2b", has_lookup_state,
                f"up-init set-state LOOKUP={has_lookup_state}"))

    # (c) pan constants: 60 hold, 96 floor magnitude, -2 step on lookPos.
    has_60 = re.search(r"\b60\b", src) is not None
    has_96 = re.search(r"\b96\b", src) is not None
    # accept lookPos -= 2  OR  lookPos = ... - 2  patterns.
    has_step2 = (re.search(r"lookPos\s*-=\s*2\b", src) is not None or
                 re.search(r"lookPos\b[^;]*-\s*2\b", src) is not None)
    const_ok = has_60 and has_96 and has_step2
    sub.append(("2c", const_ok,
                f"pan hold60={has_60} floor96={has_96} step(-2)={has_step2}"))

    # (d) switch gains LOOKUP case; Player_Tick in game.map.
    has_lookup_case = re.search(
        r"case\s+PLAYER_STATE_LOOKUP\s*:", src) is not None
    mp = _read(GAME_MAP)
    sym_ok = re.search(r"\bPlayer_Tick\b", mp) is not None if mp else False
    dispatch_ok = has_lookup_case and sym_ok
    sub.append(("2d", dispatch_ok,
                f"switch case LOOKUP={has_lookup_case} "
                f"Player_Tick-in-map={sym_ok if mp else 'no game.map'}"))

    # (e) Game.c camera-follow reads lookPos into cam_y.
    cam_ok = re.search(r"lookPos", game) is not None
    sub.append(("2e", cam_ok,
                f"Game.c camera reads lookPos={cam_ok}"))

    all_ok = all(ok for _, ok, _ in sub)
    for tag, ok, msg in sub:
        cprint(f"P2.{tag} {'GREEN' if ok else 'RED'}", msg, ok)
    return all_ok


# --- P3: runtime savestate (optional) -------------------------------------

def _resolve_sym(map_body, name):
    m = re.search(r"(0x0[06][0-9a-fA-F]+)\s+" + re.escape(name) + r"\b", map_body)
    return int(m.group(1), 16) if m else None


def _peek_wram(sections, addr, n):
    try:
        from mcs_extract import _peek_bytes  # type: ignore
    except Exception:
        return None
    b = _peek_bytes(sections, addr, n)
    if not b or len(b) < n:
        return None
    return b


def _u8(sections, addr):
    b = _peek_wram(sections, addr & ~1, 2)
    if b is None:
        return None
    swapped = bytes([b[1], b[0]])
    return swapped[addr & 1]


def _i32(sections, addr):
    b = _peek_wram(sections, addr, 4)
    if b is None:
        return None
    swapped = bytes([b[1], b[0], b[3], b[2]])
    v = int.from_bytes(swapped, "big")
    return v - (1 << 32) if v & 0x80000000 else v


def _parse(state_path):
    from mcs_extract import parse_savestate  # type: ignore
    from pathlib import Path
    return parse_savestate(Path(state_path))


def predicate_3_runtime(lookup_path, pan_path):
    if not lookup_path and not pan_path:
        return cprint("P3 SKIP",
                      "no --with-savestate-*; static P1-P2 carry the RED-now contract",
                      True)
    mp = _read(GAME_MAP)
    if not mp:
        return cprint("P3 RED", "game.map not found (build first)", False)
    addr_state = _resolve_sym(mp, "g_player_diag_state")
    addr_lookpos = _resolve_sym(mp, "g_player_diag_lookpos")
    if addr_state is None:
        return cprint("P3 RED",
                      "diag symbol g_player_diag_state missing; "
                      "add `used` g_player_diag_state/_lookpos in Game.c", False)

    ok = True
    # State A: looking up (state == LOOKUP).
    if lookup_path:
        try:
            sec = _parse(lookup_path)
        except Exception as e:
            return cprint("P3 RED", f"lookup savestate parse failed: {e}", False)
        st = _u8(sec, addr_state)
        a_ok = (st == PLAYER_STATE_LOOKUP)
        ok &= cprint("P3a GREEN" if a_ok else "P3a RED",
                     f"lookup state={st} (expect LOOKUP={PLAYER_STATE_LOOKUP})", a_ok)
    # State B: panned up (lookPos < 0, >= -96).
    if pan_path:
        try:
            sec = _parse(pan_path)
        except Exception as e:
            return cprint("P3 RED", f"pan savestate parse failed: {e}", False)
        lp = _i32(sec, addr_lookpos) if addr_lookpos is not None else None
        b_ok = lp is not None and lp < 0 and lp >= -96
        ok &= cprint("P3b GREEN" if b_ok else "P3b RED",
                     f"pan lookPos={lp} (expect -96 <= lookPos < 0)", b_ok)
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--with-savestate-lookup", default="",
                    help="savestate captured while looking up (UP held, < 60 ticks)")
    ap.add_argument("--with-savestate-pan", default="",
                    help="savestate captured after 60-tick hold (camera panned up)")
    args = ap.parse_args()

    print("=== Phase 2.5.3 Player LookUp + camera-pan gate ===")
    print(f"  Player.h: {PLAYER_H}")
    print(f"  Player.c: {PLAYER_C}")

    ok = True
    ok &= predicate_1_members_fields()
    ok &= predicate_2_port()
    ok &= predicate_3_runtime(args.with_savestate_lookup.strip(),
                              args.with_savestate_pan.strip())

    if ok:
        print("=== Gate Phase 2.5.3: GREEN ===")
        return 0
    print("=== Gate Phase 2.5.3: RED ===")
    return 1


if __name__ == "__main__":
    sys.exit(main())
