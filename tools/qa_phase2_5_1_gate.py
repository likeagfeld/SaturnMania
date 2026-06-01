#!/usr/bin/env python3
"""qa_phase2_5_1_gate.py - Phase 2.5.1 Player state-machine refactor + Roll gate.

BINDING per CLAUDE.md #4.7 + memory/qa-iterative-improvement.md. Authored
BEFORE any 2.5.1 implementation lands; MUST fire RED on the current build
(the state enum, the `state` field, the three Roll functions, the roll-init
constants, and the switch dispatch all do not exist yet).

2.5.1 is the first increment of the Phase 2.5 "Player completion" plan. It
(a) replaces the binary `if (p->onGround)` dispatch in Player_Tick with a
decomp-style `switch (p->state)` selector (Ground/Air behaviour byte-
identical), and (b) ports the Roll moveset: Player_Action_Roll,
Player_State_Roll, Player_HandleRollDeceleration, and the State_Ground
roll-init branch.

Decomp authority (tools/_decomp_raw/SonicMania_Objects_Global_Player.c):
  - Player_Action_Roll            L3330-3340 (state = Player_State_Roll).
  - Player_HandleRollDeceleration L3466-3565 (rollingDeceleration on
       reverse-press, rollingFriction toward 0, slope force
       0x1400 with-slope / 0x5000 against-slope, |groundVel| cap 0x120000,
       reversal -> groundVel=0 + state=Player_State_Ground).
  - Player_State_Roll             L3932-3958 (HandleRollDeceleration +
       Gravity_False + jump cancel).
  - Player_State_Ground roll-init L3849-3854 (groundVel && |groundVel| >=
       minRollVel(0x8800) && !left && !right && down -> Action_Roll).
  - Player_UpdatePhysicsState     L2792,2795 (rollingFriction = table[5],
       rollingDeceleration = 0x2000).

Predicates:

  P1 (static) - the decomp-style state selector exists.
       (a) Player.h declares a `player_state_t` enum that includes
           PLAYER_STATE_ROLL (and the Ground/Air members).
       (b) player_t carries a `state` field of that enum type.
       (c) Player.c dispatches via `switch` on the state field (not the
           old binary `if (p->onGround)` selector).

  P2 (static) - the Roll port is present.
       (a) Player.c defines Player_Action_Roll, Player_State_Roll,
           Player_HandleRollDeceleration.
       (b) the roll-init condition is wired in State_Ground: the
           minRollVel literal 0x8800 AND a `down && !left && !right`
           (any spelling) guard.
       (c) the rolling-deceleration constants are present: 0x1400 (with-
           slope), 0x5000 (against-slope), 0x120000 (cap), and a
           rollingDeceleration field (= 0x2000 per UpdatePhysicsState).
       (d) Player_Tick survives in game.map (the switch dispatch lives
           inside it; `used` keeps the symbol against LTO inlining).

  P3 (runtime, --with-savestate) - at speed, DOWN enters Roll.
       Peeks the `used` diag globals g_player_diag_state (uint8) and
       g_player_diag_gsp (int32, Q16.16) resolved from game.map (WRAM-H
       pair-swap aware). The savestate must be captured with the player
       moving at >= minRollVel with DOWN held: asserts
       g_player_diag_state == PLAYER_STATE_ROLL (2) AND
       |g_player_diag_gsp| >= 0x8000 (still rolling, decel not yet
       parked). SKIPPED without --with-savestate (static P1-P2 carry the
       RED-now demonstration, mirroring qa_phase2_4g3_gate P4).

Exit code: 0 = all GREEN. Non-zero = any RED.

Run:
    py -3 tools/qa_phase2_5_1_gate.py
    py -3 tools/qa_phase2_5_1_gate.py --with-savestate samples/qa_phase2_5_1.mcs
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAYER_H = os.path.join(ROOT, "src", "mania", "Objects", "Global", "Player.h")
PLAYER_C = os.path.join(ROOT, "src", "mania", "Objects", "Global", "Player.c")
GAME_MAP = os.path.join(ROOT, "game.map")

# PLAYER_STATE_ROLL enum value the port assigns (Ground=0, Air=1, Roll=2).
PLAYER_STATE_ROLL = 2

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


# --- P1: decomp-style state selector --------------------------------------

def predicate_1_state_selector():
    hdr = _read(PLAYER_H)
    src = _read(PLAYER_C)
    sub = []

    # (a) player_state_t enum with PLAYER_STATE_ROLL.
    enum_ok = (re.search(r"\bplayer_state_t\b", hdr) is not None
               and re.search(r"\bPLAYER_STATE_ROLL\b", hdr) is not None
               and re.search(r"\bPLAYER_STATE_GROUND\b", hdr) is not None
               and re.search(r"\bPLAYER_STATE_AIR\b", hdr) is not None)
    sub.append(("1a", enum_ok,
                f"Player.h player_state_t enum w/ GROUND+AIR+ROLL "
                f"{'present' if enum_ok else 'ABSENT'}"))

    # (b) player_t.state field of that enum type.
    field_ok = re.search(r"player_state_t\s+state\s*;", hdr) is not None
    sub.append(("1b", field_ok,
                f"player_t carries `player_state_t state;` field "
                f"{'present' if field_ok else 'ABSENT'}"))

    # (c) Player.c dispatches via switch on state (not the old binary if).
    switch_ok = re.search(r"switch\s*\(\s*p->state\s*\)", src) is not None
    sub.append(("1c", switch_ok,
                f"Player_Tick uses `switch (p->state)` dispatch "
                f"{'present' if switch_ok else 'ABSENT'}"))

    all_ok = all(ok for _, ok, _ in sub)
    for tag, ok, msg in sub:
        cprint(f"P1.{tag} {'GREEN' if ok else 'RED'}", msg, ok)
    return all_ok


# --- P2: Roll port present -------------------------------------------------

def predicate_2_roll_port():
    src = _read(PLAYER_C)
    sub = []

    # (a) the three roll routines are defined.
    fns = ["Player_Action_Roll", "Player_State_Roll",
           "Player_HandleRollDeceleration"]
    fns_present = {fn: re.search(r"\b" + fn + r"\s*\(", src) is not None
                   for fn in fns}
    fns_ok = all(fns_present.values())
    sub.append(("2a", fns_ok,
                "Player.c roll routines: " +
                ", ".join(f"{k}={'Y' if v else 'N'}"
                          for k, v in fns_present.items())))

    # (b) roll-init condition in State_Ground.
    has_minroll = re.search(r"0x8800", src) is not None
    # down held, left+right released (any spelling of the guard).
    has_guard = (re.search(r"!\s*left", src) is not None
                 and re.search(r"!\s*right", src) is not None
                 and re.search(r"\bdown\b", src) is not None)
    init_ok = has_minroll and has_guard
    sub.append(("2b", init_ok,
                f"roll-init minRollVel(0x8800)={has_minroll} "
                f"down&!left&!right guard={has_guard}"))

    # (c) rolling-deceleration constants + rollingDeceleration field.
    has_1400 = re.search(r"0x1400", src) is not None
    has_5000 = re.search(r"0x5000", src) is not None
    has_cap = re.search(r"0x120000", src) is not None
    has_rolldecel = re.search(r"\brollingDeceleration\b", src) is not None
    const_ok = has_1400 and has_5000 and has_cap and has_rolldecel
    sub.append(("2c", const_ok,
                f"rolling decel consts 0x1400={has_1400} 0x5000={has_5000} "
                f"cap0x120000={has_cap} rollingDeceleration={has_rolldecel}"))

    # (d) Player_Tick survives in game.map.
    mp = _read(GAME_MAP)
    sym_ok = re.search(r"\bPlayer_Tick\b", mp) is not None if mp else False
    sub.append(("2d", sym_ok,
                f"Player_Tick in game.map "
                f"{'present' if sym_ok else ('ABSENT' if mp else 'no game.map')}"))

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
    # WRAM-H byte: the mcs WorkRAM-H chunk is 16-bit pair-swapped (see
    # memory/ghz-fg-tmp-buffer-off-by-4-bytes.md). A single byte at `addr`
    # maps to its sibling within the swapped 16-bit halfword.
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


def predicate_3_runtime(state_path):
    if not state_path:
        return cprint("P3 SKIP",
                      "no --with-savestate; static P1-P2 carry the RED-now contract",
                      True)
    mp = _read(GAME_MAP)
    if not mp:
        return cprint("P3 RED", "game.map not found (build first)", False)
    addr_state = _resolve_sym(mp, "g_player_diag_state")
    addr_gsp = _resolve_sym(mp, "g_player_diag_gsp")
    if addr_state is None or addr_gsp is None:
        return cprint("P3 RED",
                      f"diag symbols missing (state={addr_state} gsp={addr_gsp}); "
                      "add `used` g_player_diag_state/_gsp in Game.c", False)
    try:
        from mcs_extract import parse_savestate  # type: ignore
        from pathlib import Path
        sections = parse_savestate(Path(state_path))
    except Exception as e:
        return cprint("P3 RED", f"savestate parse failed: {e}", False)

    state = _u8(sections, addr_state)
    gsp = _i32(sections, addr_gsp)
    rolling = (state == PLAYER_STATE_ROLL) and gsp is not None and abs(gsp) >= 0x8000
    return cprint("P3 GREEN" if rolling else "P3 RED",
                  f"g_player_diag_state={state} (expect ROLL={PLAYER_STATE_ROLL}) "
                  f"g_player_diag_gsp={hex(gsp & 0xFFFFFFFF) if gsp is not None else None} "
                  f"(|gsp| >= 0x8000)", rolling)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--with-savestate", default="",
                    help="savestate captured with the player rolling (DOWN at speed)")
    args = ap.parse_args()

    print("=== Phase 2.5.1 Player state-machine + Roll gate ===")
    print(f"  Player.h: {PLAYER_H}")
    print(f"  Player.c: {PLAYER_C}")

    ok = True
    ok &= predicate_1_state_selector()
    ok &= predicate_2_roll_port()
    ok &= predicate_3_runtime(args.with_savestate.strip())

    if ok:
        print("=== Gate Phase 2.5.1: GREEN ===")
        return 0
    print("=== Gate Phase 2.5.1: RED ===")
    return 1


if __name__ == "__main__":
    sys.exit(main())
