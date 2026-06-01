#!/usr/bin/env python3
"""qa_phase2_5_4_gate.py - Phase 2.5.4 Player DropDash gate.

BINDING per CLAUDE.md #4.7 + memory/qa-iterative-improvement.md. Authored
BEFORE any 2.5.4 implementation lands; MUST fire RED on the current build
(the DROPDASH enum member, the jumpAbilityState/jumpHold fields, the
Player_State_DropDash + Player_JumpAbility_Sonic routines, the ramp-to-22
arm, and the landing-launch all do not exist yet).

2.5.4 is the fourth increment of the Phase 2.5 "Player completion" plan. It
ports the air DropDash ability on top of the 2.5.1-2.5.3 state machine:
  - Action_Jump arms the ability        -> jumpAbilityState = 1
  - in-air past jumpCap, jumpPress       -> JumpAbility_Sonic arms dropdash
                                            (SHIELD_NONE: jumpAbilityState = 2)
  - jumpHold while armed                  -> ++jumpAbilityState; at >= 22 ->
                                            state = Player_State_DropDash
  - landing in DropDash                   -> groundVel = vel(0x80000) +
                                            (groundVel>>2) cap 0xC0000;
                                            state = Player_State_Roll
  - jumpHold released mid-air             -> jumpAbilityState = 0, state = Air

Decomp authority (tools/_decomp_raw/SonicMania_Objects_Global_Player.c):
  - Player_JumpAbility_Sonic       L6114-6216 (dropdashDisabled =
        jumpAbilityState<=1; jumpAbilityState==1 + jumpPress + SHIELD_NONE ->
        jumpAbilityState = (~(medalMods&0xFF)>>3)&2 = 2; then !disabled &&
        jumpHold -> ++jumpAbilityState >= 22 -> state=DropDash, ANI_DROPDASH).
  - Player_State_DropDash          L4455-4543 (onGround: vel=0x80000 cap=
        0xC0000; facing right + velocity.x>=0 -> groundVel = vel +
        (groundVel>>2) cap +cap; facing left mirrors; pushing=0, state=Roll.
        airborne + jumpHold -> air friction/movement + anim ramp; else
        jumpAbilityState=0, state=Air).
  - Player_Action_Jump arm         L3325 (jumpAbilityState = 1).
  - Player_State_Air ability call  L3914-3917 (ANI_JUMP + velocity.y >=
        jumpCap -> StateMachine_Run(stateAbility)).

Saturn note: shields (2.5.7) + super (2.5.9) are NOT yet ported, so the
JumpAbility_Sonic shield/super branches are intentionally absent; the base
Sonic SHIELD_NONE dropdash-arm + ramp + launch are the 2.5.4 scope.

Predicates:

  P1 (static) - the DropDash state member + fields exist.
       (a) Player.h player_state_t enum gains PLAYER_STATE_DROPDASH.
       (b) player_t carries jumpAbilityState + jumpHold.

  P2 (static) - the DropDash port is present.
       (a) Player.c defines Player_State_DropDash AND Player_JumpAbility_Sonic.
       (b) the arm is wired: jumpAbilityState set to 1 (Action_Jump) and a
           state transition to PLAYER_STATE_DROPDASH.
       (c) the dropdash constants are present: 22 (ramp target), 0x80000
           (launch base vel), 0xC0000 (launch cap), and a `++` ramp on
           jumpAbilityState (>= 22 trigger).
       (d) switch (p->state) gains a PLAYER_STATE_DROPDASH case; Player_Tick
           survives in game.map.
       (e) launch -> Roll: a PLAYER_STATE_ROLL assignment lives inside the
           DropDash routine (onGround launch hands off to Roll).

  P3 (runtime, --with-savestate) - ramp then launch.
       Two-state capture. State A (jumpHold past 22 ticks in air):
       g_player_diag_state == PLAYER_STATE_DROPDASH. State B (just landed):
       g_player_diag_state == PLAYER_STATE_ROLL and |g_player_diag_gsp| >=
       0x80000. SKIPPED without --with-savestate (static P1-P2 carry the
       RED-now demonstration, mirroring qa_phase2_5_3_gate P3).

Exit code: 0 = all GREEN. Non-zero = any RED.

Run:
    py -3 tools/qa_phase2_5_4_gate.py
    py -3 tools/qa_phase2_5_4_gate.py --with-savestate-dropdash samples/dd.mcs \
                                      --with-savestate-launch samples/dd_land.mcs
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAYER_H = os.path.join(ROOT, "src", "mania", "Objects", "Global", "Player.h")
PLAYER_C = os.path.join(ROOT, "src", "mania", "Objects", "Global", "Player.c")
GAME_MAP = os.path.join(ROOT, "game.map")

# Enum values (Ground=0..LookUp=5, DropDash=6).
PLAYER_STATE_ROLL = 2
PLAYER_STATE_DROPDASH = 6

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


# --- P1: DropDash state member + fields -----------------------------------

def predicate_1_members_fields():
    hdr = _read(PLAYER_H)
    sub = []

    # (a) enum gains DROPDASH.
    dd_ok = re.search(r"\bPLAYER_STATE_DROPDASH\b", hdr) is not None
    sub.append(("1a", dd_ok,
                f"player_state_t enum DROPDASH={dd_ok}"))

    # (b) player_t carries jumpAbilityState + jumpHold.
    has_jas = re.search(r"\bjumpAbilityState\b", hdr) is not None
    has_jh = re.search(r"\bjumpHold\b", hdr) is not None
    sub.append(("1b", has_jas and has_jh,
                f"player_t fields jumpAbilityState={has_jas} jumpHold={has_jh}"))

    all_ok = all(ok for _, ok, _ in sub)
    for tag, ok, msg in sub:
        cprint(f"P1.{tag} {'GREEN' if ok else 'RED'}", msg, ok)
    return all_ok


# --- P2: DropDash port present --------------------------------------------

def predicate_2_port():
    src = _read(PLAYER_C)
    sub = []

    # (a) Player_State_DropDash + Player_JumpAbility_Sonic defined.
    dd_fn = re.search(r"\bPlayer_State_DropDash\s*\(", src) is not None
    ab_fn = re.search(r"\bPlayer_JumpAbility_Sonic\s*\(", src) is not None
    sub.append(("2a", dd_fn and ab_fn,
                f"Player.c DropDash={dd_fn} JumpAbility_Sonic={ab_fn}"))

    # (b) arm wired: jumpAbilityState = 1 AND a DROPDASH set-state.
    arm_ok = re.search(r"jumpAbilityState\s*=\s*1\b", src) is not None
    has_dd_state = re.search(r"PLAYER_STATE_DROPDASH", src) is not None
    sub.append(("2b", arm_ok and has_dd_state,
                f"arm jumpAbilityState=1={arm_ok} set-state DROPDASH={has_dd_state}"))

    # (c) consts: 22 ramp target, 0x80000 launch base, 0xC0000 cap, ++ ramp.
    has_22 = re.search(r"\b22\b", src) is not None
    has_base = re.search(r"0x80000\b", src) is not None
    has_cap = re.search(r"0xC0000\b", src, re.IGNORECASE) is not None
    has_ramp = (re.search(r"\+\+\s*p->jumpAbilityState\b", src) is not None or
                re.search(r"p->jumpAbilityState\s*\+\+", src) is not None or
                re.search(r"jumpAbilityState\s*\+=\s*1\b", src) is not None)
    const_ok = has_22 and has_base and has_cap and has_ramp
    sub.append(("2c", const_ok,
                f"ramp22={has_22} base0x80000={has_base} cap0xC0000={has_cap} "
                f"++ramp={has_ramp}"))

    # (d) switch gains DROPDASH case; Player_Tick in game.map.
    has_dd_case = re.search(
        r"case\s+PLAYER_STATE_DROPDASH\s*:", src) is not None
    mp = _read(GAME_MAP)
    sym_ok = re.search(r"\bPlayer_Tick\b", mp) is not None if mp else False
    dispatch_ok = has_dd_case and sym_ok
    sub.append(("2d", dispatch_ok,
                f"switch case DROPDASH={has_dd_case} "
                f"Player_Tick-in-map={sym_ok if mp else 'no game.map'}"))

    # (e) launch -> Roll inside the DropDash routine. Isolate the
    # Player_State_DropDash function body and require a PLAYER_STATE_ROLL
    # assignment within it.
    roll_ok = False
    m = re.search(r"\bPlayer_State_DropDash\s*\([^)]*\)\s*\{", src)
    if m:
        body = src[m.end():m.end() + 4000]
        # cut at the next top-level function (heuristic: a line beginning with
        # `static ` or a non-indented `void `/type at column 0 after a blank).
        cut = re.search(r"\n\}\s*\n", body)
        if cut:
            body = body[:cut.end()]
        roll_ok = re.search(r"PLAYER_STATE_ROLL", body) is not None
    sub.append(("2e", roll_ok,
                f"DropDash onGround launch -> state=ROLL={roll_ok}"))

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


def predicate_3_runtime(dropdash_path, launch_path):
    if not dropdash_path and not launch_path:
        return cprint("P3 SKIP",
                      "no --with-savestate-*; static P1-P2 carry the RED-now contract",
                      True)
    mp = _read(GAME_MAP)
    if not mp:
        return cprint("P3 RED", "game.map not found (build first)", False)
    addr_state = _resolve_sym(mp, "g_player_diag_state")
    addr_gsp = _resolve_sym(mp, "g_player_diag_gsp")
    if addr_state is None:
        return cprint("P3 RED",
                      "diag symbol g_player_diag_state missing; "
                      "add `used` g_player_diag_state/_gsp in Game.c", False)

    ok = True
    # State A: ramped into dropdash (state == DROPDASH).
    if dropdash_path:
        try:
            sec = _parse(dropdash_path)
        except Exception as e:
            return cprint("P3 RED", f"dropdash savestate parse failed: {e}", False)
        st = _u8(sec, addr_state)
        a_ok = (st == PLAYER_STATE_DROPDASH)
        ok &= cprint("P3a GREEN" if a_ok else "P3a RED",
                     f"dropdash state={st} (expect DROPDASH={PLAYER_STATE_DROPDASH})",
                     a_ok)
    # State B: launched -> Roll, |gsp| >= 0x80000.
    if launch_path:
        try:
            sec = _parse(launch_path)
        except Exception as e:
            return cprint("P3 RED", f"launch savestate parse failed: {e}", False)
        st = _u8(sec, addr_state)
        gsp = _i32(sec, addr_gsp) if addr_gsp is not None else None
        b_ok = (st == PLAYER_STATE_ROLL and gsp is not None and
                abs(gsp) >= 0x80000)
        ok &= cprint("P3b GREEN" if b_ok else "P3b RED",
                     f"launch state={st} (expect ROLL={PLAYER_STATE_ROLL}) "
                     f"gsp={gsp} (expect |gsp|>=0x80000)", b_ok)
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--with-savestate-dropdash", default="",
                    help="savestate captured while dropdashing (jumpHold past 22)")
    ap.add_argument("--with-savestate-launch", default="",
                    help="savestate captured just after landing (Roll launch)")
    args = ap.parse_args()

    print("=== Phase 2.5.4 Player DropDash gate ===")
    print(f"  Player.h: {PLAYER_H}")
    print(f"  Player.c: {PLAYER_C}")

    ok = True
    ok &= predicate_1_members_fields()
    ok &= predicate_2_port()
    ok &= predicate_3_runtime(args.with_savestate_dropdash.strip(),
                              args.with_savestate_launch.strip())

    if ok:
        print("=== Gate Phase 2.5.4: GREEN ===")
        return 0
    print("=== Gate Phase 2.5.4: RED ===")
    return 1


if __name__ == "__main__":
    sys.exit(main())
