#!/usr/bin/env python3
"""qa_phase2_5_2_gate.py - Phase 2.5.2 Player Crouch + Spindash gate.

BINDING per CLAUDE.md #4.7 + memory/qa-iterative-improvement.md. Authored
BEFORE any 2.5.2 implementation lands; MUST fire RED on the current build
(the CROUCH/SPINDASH enum members, the abilityTimer/spindashCharge/timer
fields, the Crouch + Spindash state functions, and the launch-velocity
constants all do not exist yet).

2.5.2 is the second increment of the Phase 2.5 "Player completion" plan. It
ports the at-rest Crouch + Spindash charge/release moveset on top of the
2.5.1 state machine:
  - at-rest DOWN on flat ground            -> state = Player_State_Crouch
  - jumpPress while crouched                -> Player_Action_Spindash
  - each jumpPress while spindashing        -> abilityTimer += 0x20000 (cap
                                               0x90000), spindashCharge++ (cap 12)
  - jump NOT pressed while spindashing      -> abilityTimer -= abilityTimer >> 5
  - release DOWN                            -> groundVel =
        (((uint32)abilityTimer >> 1) & 0x7FFF8000) + 0x80000, state = Roll

Decomp authority (tools/_decomp_raw/SonicMania_Objects_Global_Player.c):
  - Player_Action_Spindash         L3341-3355 (state = Player_State_Spindash,
        abilityTimer = 0, spindashCharge = 0, ANI_SPINDASH, sfxCharge).
  - Player_State_Crouch            L4082-4129 (down -> ANI_CROUCH, timer++ to
        60 then camera lookPos.y += 2 cap 96, jumpPress -> Action_Spindash;
        else speed=128, frameID==0||left||right -> state=Ground).
  - Player_State_Spindash          L4131-4187 (jumpPress -> abilityTimer +=
        0x20000 cap 0x90000, spindashCharge++ cap 12; else abilityTimer -=
        abilityTimer >> 5; !down -> vel = (((uint32)abilityTimer >> 1) &
        0x7FFF8000) + 0x80000, groundVel = dir ? -vel : vel, state = Roll).
  - Player_State_Ground crouch-init L3849-3868 (groundVel == 0 &&
        (angle<0x20||angle>0xE0) && !collisionMode: down -> ANI_CROUCH,
        timer=0, state = Crouch; minRollVel from Crouch = 0x11000).

Predicates:

  P1 (static) - the Crouch + Spindash state members + fields exist.
       (a) Player.h player_state_t enum gains PLAYER_STATE_CROUCH and
           PLAYER_STATE_SPINDASH (in addition to GROUND/AIR/ROLL).
       (b) player_t carries abilityTimer, spindashCharge, timer fields.

  P2 (static) - the Crouch + Spindash port is present.
       (a) Player.c defines Player_State_Crouch, Player_Action_Spindash,
           Player_State_Spindash.
       (b) State_Ground at-rest crouch-init is wired: a `down` -> Crouch
           transition that sets state to PLAYER_STATE_CROUCH, plus the
           Crouch minRollVel literal 0x11000.
       (c) the spindash charge/launch constants are present: 0x20000
           (per-tap charge), 0x90000 (charge cap), 0x7FFF8000 (launch
           mask), 0x80000 (launch base); and the spindashCharge cap 12.
       (d) switch (p->state) gains PLAYER_STATE_CROUCH and
           PLAYER_STATE_SPINDASH cases; Player_Tick survives in game.map.

  P3 (runtime, --with-savestate) - charge then release.
       Two-state capture. State A (mid-charge, jump-tapped while crouched):
       g_player_diag_state == PLAYER_STATE_SPINDASH and the charge diag
       g_player_diag_charge > 0. State B (DOWN released): g_player_diag_state
       == PLAYER_STATE_ROLL and |g_player_diag_gsp| >= 0x80000 (launched).
       SKIPPED without --with-savestate (static P1-P2 carry the RED-now
       demonstration, mirroring qa_phase2_5_1_gate P3).

Exit code: 0 = all GREEN. Non-zero = any RED.

Run:
    py -3 tools/qa_phase2_5_2_gate.py
    py -3 tools/qa_phase2_5_2_gate.py --with-savestate-charge samples/sd_charge.mcs \
                                      --with-savestate-launch samples/sd_launch.mcs
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAYER_H = os.path.join(ROOT, "src", "mania", "Objects", "Global", "Player.h")
PLAYER_C = os.path.join(ROOT, "src", "mania", "Objects", "Global", "Player.c")
GAME_MAP = os.path.join(ROOT, "game.map")

# Enum values the port assigns (Ground=0, Air=1, Roll=2, Crouch=3, Spindash=4).
PLAYER_STATE_ROLL = 2
PLAYER_STATE_CROUCH = 3
PLAYER_STATE_SPINDASH = 4

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


# --- P1: Crouch + Spindash state members + fields -------------------------

def predicate_1_members_fields():
    hdr = _read(PLAYER_H)
    sub = []

    # (a) enum gains CROUCH + SPINDASH.
    crouch_ok = re.search(r"\bPLAYER_STATE_CROUCH\b", hdr) is not None
    spindash_ok = re.search(r"\bPLAYER_STATE_SPINDASH\b", hdr) is not None
    enum_ok = crouch_ok and spindash_ok
    sub.append(("1a", enum_ok,
                f"player_state_t enum CROUCH={crouch_ok} SPINDASH={spindash_ok}"))

    # (b) player_t carries abilityTimer, spindashCharge, timer.
    has_ability = re.search(r"\babilityTimer\b\s*;", hdr) is not None
    has_charge = re.search(r"\bspindashCharge\b\s*;", hdr) is not None
    has_timer = re.search(r"\btimer\b\s*;", hdr) is not None
    field_ok = has_ability and has_charge and has_timer
    sub.append(("1b", field_ok,
                f"player_t fields abilityTimer={has_ability} "
                f"spindashCharge={has_charge} timer={has_timer}"))

    all_ok = all(ok for _, ok, _ in sub)
    for tag, ok, msg in sub:
        cprint(f"P1.{tag} {'GREEN' if ok else 'RED'}", msg, ok)
    return all_ok


# --- P2: Crouch + Spindash port present -----------------------------------

def predicate_2_port():
    src = _read(PLAYER_C)
    sub = []

    # (a) the three routines are defined.
    fns = ["Player_State_Crouch", "Player_Action_Spindash",
           "Player_State_Spindash"]
    fns_present = {fn: re.search(r"\b" + fn + r"\s*\(", src) is not None
                   for fn in fns}
    fns_ok = all(fns_present.values())
    sub.append(("2a", fns_ok,
                "Player.c crouch/spindash routines: " +
                ", ".join(f"{k}={'Y' if v else 'N'}"
                          for k, v in fns_present.items())))

    # (b) State_Ground at-rest crouch-init: down -> CROUCH + minRollVel 0x11000.
    has_crouch_state = re.search(r"PLAYER_STATE_CROUCH", src) is not None
    has_minroll11 = re.search(r"0x11000", src) is not None
    init_ok = has_crouch_state and has_minroll11
    sub.append(("2b", init_ok,
                f"crouch-init set-state CROUCH={has_crouch_state} "
                f"Crouch minRollVel(0x11000)={has_minroll11}"))

    # (c) spindash charge/launch constants + spindashCharge cap 12.
    has_20000 = re.search(r"0x20000", src) is not None
    has_90000 = re.search(r"0x90000", src) is not None
    has_mask = re.search(r"0x7FFF8000", src, re.IGNORECASE) is not None
    has_80000 = re.search(r"0x80000", src) is not None
    has_cap12 = re.search(r"\b12\b", src) is not None
    const_ok = has_20000 and has_90000 and has_mask and has_80000 and has_cap12
    sub.append(("2c", const_ok,
                f"charge 0x20000={has_20000} cap0x90000={has_90000} "
                f"mask0x7FFF8000={has_mask} base0x80000={has_80000} "
                f"chargeCap12={has_cap12}"))

    # (d) switch gains CROUCH + SPINDASH cases; Player_Tick in game.map.
    has_crouch_case = re.search(
        r"case\s+PLAYER_STATE_CROUCH\s*:", src) is not None
    has_spindash_case = re.search(
        r"case\s+PLAYER_STATE_SPINDASH\s*:", src) is not None
    mp = _read(GAME_MAP)
    sym_ok = re.search(r"\bPlayer_Tick\b", mp) is not None if mp else False
    dispatch_ok = has_crouch_case and has_spindash_case and sym_ok
    sub.append(("2d", dispatch_ok,
                f"switch case CROUCH={has_crouch_case} SPINDASH={has_spindash_case} "
                f"Player_Tick-in-map={sym_ok if mp else 'no game.map'}"))

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


def predicate_3_runtime(charge_path, launch_path):
    if not charge_path and not launch_path:
        return cprint("P3 SKIP",
                      "no --with-savestate-*; static P1-P2 carry the RED-now contract",
                      True)
    mp = _read(GAME_MAP)
    if not mp:
        return cprint("P3 RED", "game.map not found (build first)", False)
    addr_state = _resolve_sym(mp, "g_player_diag_state")
    addr_gsp = _resolve_sym(mp, "g_player_diag_gsp")
    addr_charge = _resolve_sym(mp, "g_player_diag_charge")
    if addr_state is None or addr_gsp is None:
        return cprint("P3 RED",
                      f"diag symbols missing (state={addr_state} gsp={addr_gsp}); "
                      "add `used` g_player_diag_state/_gsp/_charge in Game.c", False)

    ok = True
    # State A: mid-charge (spindash, charge > 0).
    if charge_path:
        try:
            sec = _parse(charge_path)
        except Exception as e:
            return cprint("P3 RED", f"charge savestate parse failed: {e}", False)
        st = _u8(sec, addr_state)
        ch = _i32(sec, addr_charge) if addr_charge is not None else None
        a_ok = (st == PLAYER_STATE_SPINDASH) and (ch is None or ch > 0)
        ok &= cprint("P3a GREEN" if a_ok else "P3a RED",
                     f"charge state={st} (expect SPINDASH={PLAYER_STATE_SPINDASH}) "
                     f"charge={ch}", a_ok)
    # State B: launched (roll, |gsp| >= 0x80000).
    if launch_path:
        try:
            sec = _parse(launch_path)
        except Exception as e:
            return cprint("P3 RED", f"launch savestate parse failed: {e}", False)
        st = _u8(sec, addr_state)
        gsp = _i32(sec, addr_gsp)
        b_ok = (st == PLAYER_STATE_ROLL) and gsp is not None and abs(gsp) >= 0x80000
        ok &= cprint("P3b GREEN" if b_ok else "P3b RED",
                     f"launch state={st} (expect ROLL={PLAYER_STATE_ROLL}) "
                     f"gsp={hex(gsp & 0xFFFFFFFF) if gsp is not None else None} "
                     f"(|gsp| >= 0x80000)", b_ok)
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--with-savestate-charge", default="",
                    help="savestate captured mid-spindash-charge (crouched, jump-tapped)")
    ap.add_argument("--with-savestate-launch", default="",
                    help="savestate captured just after DOWN release (launched into roll)")
    args = ap.parse_args()

    print("=== Phase 2.5.2 Player Crouch + Spindash gate ===")
    print(f"  Player.h: {PLAYER_H}")
    print(f"  Player.c: {PLAYER_C}")

    ok = True
    ok &= predicate_1_members_fields()
    ok &= predicate_2_port()
    ok &= predicate_3_runtime(args.with_savestate_charge.strip(),
                              args.with_savestate_launch.strip())

    if ok:
        print("=== Gate Phase 2.5.2: GREEN ===")
        return 0
    print("=== Gate Phase 2.5.2: RED ===")
    return 1


if __name__ == "__main__":
    sys.exit(main())
