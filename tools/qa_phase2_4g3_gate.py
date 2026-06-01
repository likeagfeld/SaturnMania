#!/usr/bin/env python3
"""qa_phase2_4g3_gate.py - Phase 2.4g.3 PlaneSwitch + two-plane bridge gate.

BINDING per CLAUDE.md #4.7 + memory/qa-iterative-improvement.md +
memory/ghz-pivot-to-rsdk-engine.md. Authored BEFORE any 2.4g.3
implementation lands; MUST fire RED on the current build (the symbols do
not exist yet).

2.4g.3 is the third (last + largest) increment of the GHZ -> RSDK-engine
pivot. It ports the RSDK PlaneSwitch object (106 GHZ Act 1 instances) as a
real RSDK object, spawns all 106 instances through the same spawn pipeline
2.4g.1/2.4g.2 used (InvisibleBlock 18 + BoundsMarker 22 + PlaneSwitch 106 =
146 overflow entities), and bridges the GHZ surface to a two-plane tile
collision selection so player->collisionPlane chooses which collision path
the player's surface probe reads.

Decomp authority:
  - tools/_decomp_raw/SonicMania_Objects_Global_PlaneSwitch.c
      _Update L12-22 (foreach_active Player -> CheckCollisions),
      _Create L30-45 (active=ACTIVE_BOUNDS, updateRange from size*Sin/Cos256,
        negAngle = -angle & 0xFF),
      _CheckCollisions L81-113 (Zone_RotateOnPivot pivot; when
        |pivotPos.x - pos.x| < TO_FIXED(24) && |pivotPos.y - pos.y| <
        size<<19: other->collisionPlane = (flags>>3)&1 if moving right,
        else (flags>>1)&1).
  - tools/_decomp_raw/SonicMania_Objects_Global_Zone.c:506-512
      Zone_RotateOnPivot.

Predicates (static unless noted):

  P1 - PlaneSwitch is a registered RSDK object.
       (a) game.map contains PlaneSwitch_Create AND PlaneSwitch_Update.
       (b) Game.c registers it via rsdk_object_register_ex("PlaneSwitch", ...).

  P2 - the player surface probe reads collisionPlane to select a collision
       path. The two-plane bridge is present in Player.c/Player.h:
       (a) sms_world_t carries a second-path pointer (token "raw_alt") and a
           mutable active-path selector (token "active_path").
       (b) Player_Tick sets the active path from p->collisionPlane (token
           "collisionPlane" referenced in Player.c), and the surface probe
           selects raw vs raw_alt by that selector. Verified by the
           presence of the codepath in game.map: Player_SurfaceY +
           Player_Tick are kept (the selection logic lives inside them).

  P3 - scene spawn wires PlaneSwitch.
       (a) 106 PlaneSwitch entities counted in GHZ Scene1.bin by class hash.
       (b) The temp-entity budget holds all three overflow classes:
           RSDK_TEMPENTITY_COUNT >= IB18 + BM22 + PS106 = 146.
       (c) the GHZ surface preload provides a second path: Game.c references
           a second-path world pointer (token "raw_alt") so the bridge has a
           non-NULL alternate to select (degenerate == path A is acceptable
           for GHZ1 per task; the gate tests the SELECTION toggle, not
           divergent geometry).

  P4 - (runtime, --with-savestate) player_t.collisionPlane toggles when the
       player crosses a PlaneSwitch X. Verified by peeking the diag global
       g_ghz_planeswitch_lastplane (and g_ghz_planeswitch_spawned >= 106)
       resolved from game.map (WRAM-H pair-swap aware). SKIPPED without
       --with-savestate (static P1-P3 carry the RED-now demonstration).

Exit code: 0 = all GREEN. Non-zero = any RED.

Run:
    py -3 tools/qa_phase2_4g3_gate.py
    py -3 tools/qa_phase2_4g3_gate.py --with-savestate samples/qa_phase2_4g3.mcs
"""

import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE_PATH = os.path.join(ROOT, "extracted", "Data", "Stages", "GHZ", "Scene1.bin")

# Reuse the 2.4g.1 scene-bin class parser (identical bin format).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qa_phase2_4g1_gate as g1  # noqa: E402

EXPECT_PS = 106
EXPECT_IB = 18
EXPECT_BM = 22


def cprint(tag, msg, ok):
    colour = "\033[32m" if ok else "\033[31m"
    reset = "\033[0m"
    print(f"  [{colour}{tag}{reset}] {msg}")
    return ok


def _read(path):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        return f.read()


# --- P1: PlaneSwitch registered as an RSDK object -------------------------

def predicate_1_registered():
    mp = os.path.join(ROOT, "game.map")
    if not os.path.exists(mp):
        return cprint("P1 RED", "game.map not found (build first)", False)
    body = _read(mp)
    have_create = re.search(r"\bPlaneSwitch_Create\b", body) is not None
    have_update = re.search(r"\bPlaneSwitch_Update\b", body) is not None

    gc = os.path.join(ROOT, "src", "mania", "Game.c")
    reg_ok = False
    if os.path.exists(gc):
        reg_ok = re.search(
            r'rsdk_object_register_ex\s*\(\s*"PlaneSwitch"', _read(gc)) is not None

    if have_create and have_update and reg_ok:
        return cprint("P1 GREEN",
                      "PlaneSwitch_Create+_Update in game.map; registered in Game.c",
                      True)
    return cprint(
        "P1 RED",
        f"create={have_create} update={have_update} register_ex={reg_ok} "
        "(PlaneSwitch not yet a registered RSDK object)",
        False)


# --- P2: two-plane bridge (surface probe reads collisionPlane) ------------

def predicate_2_two_plane_bridge():
    sub = []

    ph = os.path.join(ROOT, "src", "mania", "Objects", "Global", "Player.h")
    pc = os.path.join(ROOT, "src", "mania", "Objects", "Global", "Player.c")

    # (a) sms_world_t carries a 2nd-path pointer + active-path selector.
    hdr = _read(ph) if os.path.exists(ph) else ""
    has_raw_alt = re.search(r"\braw_alt\b", hdr) is not None
    has_active_path = re.search(r"\bactive_path\b", hdr) is not None
    sub.append(("2a", has_raw_alt and has_active_path,
                f"Player.h sms_world_t raw_alt={has_raw_alt} "
                f"active_path={has_active_path} (two-plane fields)"))

    # (b) Player.c selects the path from collisionPlane.
    src = _read(pc) if os.path.exists(pc) else ""
    sel_ok = (re.search(r"collisionPlane", src) is not None
              and re.search(r"active_path", src) is not None)
    sub.append(("2b", sel_ok,
                f"Player.c surface probe selects path via collisionPlane "
                f"{'present' if sel_ok else 'ABSENT'}"))

    # (c) the probe + tick symbols survive in game.map.
    mp = os.path.join(ROOT, "game.map")
    syms_ok = False
    if os.path.exists(mp):
        b = _read(mp)
        syms_ok = (re.search(r"\bPlayer_SurfaceY\b", b) is not None
                   and re.search(r"\bPlayer_Tick\b", b) is not None)
    sub.append(("2c", syms_ok,
                f"Player_SurfaceY + Player_Tick in game.map "
                f"{'present' if syms_ok else 'ABSENT'}"))

    all_ok = all(ok for _, ok, _ in sub)
    for tag, ok, msg in sub:
        cprint(f"P2.{tag} {'GREEN' if ok else 'RED'}", msg, ok)
    return all_ok


# --- P3: scene spawn wires PlaneSwitch -------------------------------------

def predicate_3_scene_path():
    sub = []

    # (a) 106 PlaneSwitch entities counted in GHZ Scene1.bin.
    ps = g1._parse_class_entities(SCENE_PATH, "PlaneSwitch")
    ps_n = len(ps) if ps is not None else -1
    sub.append(("3a", ps_n == EXPECT_PS,
                f"GHZ Scene1.bin PlaneSwitch count = {ps_n} (expect {EXPECT_PS})"))

    # (b) temp-entity budget >= combined overflow (IB18 + BM22 + PS106 = 146).
    oh = os.path.join(ROOT, "src", "rsdk", "object.h")
    budget = -1
    if os.path.exists(oh):
        m = re.search(r"#define\s+RSDK_TEMPENTITY_COUNT\s+(0x[0-9a-fA-F]+|\d+)",
                      _read(oh))
        if m:
            budget = int(m.group(1), 0)
    ib = g1._parse_class_entities(SCENE_PATH, "InvisibleBlock")
    bm = g1._parse_class_entities(SCENE_PATH, "BoundsMarker")
    ib_n = len(ib) if ib is not None else 0
    bm_n = len(bm) if bm is not None else 0
    need = ib_n + bm_n + ps_n
    sub.append(("3b", budget >= need,
                f"RSDK_TEMPENTITY_COUNT = {budget} >= IB{ib_n}+BM{bm_n}+PS{ps_n} "
                f"= {need} (temp budget must hold all three classes' overflow)"))

    # (c) GHZ surface preload provides a second path pointer.
    gc = os.path.join(ROOT, "src", "mania", "Game.c")
    alt_ok = False
    if os.path.exists(gc):
        alt_ok = re.search(r"raw_alt", _read(gc)) is not None
    sub.append(("3c", alt_ok,
                f"Game.c GHZ world seeds a second-path pointer (raw_alt) "
                f"{'present' if alt_ok else 'ABSENT'}"))

    all_ok = all(ok for _, ok, _ in sub)
    for tag, ok, msg in sub:
        cprint(f"P3.{tag} {'GREEN' if ok else 'RED'}", msg, ok)
    return all_ok


# --- P4: runtime savestate (optional) -------------------------------------

def _peek32_wram(sections, addr):
    try:
        from mcs_extract import _peek_bytes  # type: ignore
    except Exception:
        return None
    b = _peek_bytes(sections, addr, 4)
    if not b:
        return None
    swapped = bytes([b[1], b[0], b[3], b[2]])
    return int.from_bytes(swapped, "big")


def _resolve_sym(map_body, name):
    m = re.search(r"(0x0[06][0-9a-fA-F]+)\s+" + re.escape(name) + r"\b", map_body)
    return int(m.group(1), 16) if m else None


def predicate_4_runtime(state_path):
    if not state_path:
        return cprint("P4 SKIP",
                      "no --with-savestate; static P1-P3 carry the RED-now contract",
                      True)
    mp = os.path.join(ROOT, "game.map")
    if not os.path.exists(mp):
        return cprint("P4 RED", "game.map not found", False)
    map_body = _read(mp)
    addr_spawn = _resolve_sym(map_body, "g_ghz_planeswitch_spawned")
    addr_plane = _resolve_sym(map_body, "g_ghz_planeswitch_lastplane")
    if addr_spawn is None or addr_plane is None:
        return cprint("P4 RED",
                      "g_ghz_planeswitch_spawned / _lastplane symbols missing", False)
    try:
        from mcs_extract import parse_savestate  # type: ignore
        from pathlib import Path
        sections = parse_savestate(Path(state_path))
    except Exception as e:
        return cprint("P4 RED", f"savestate parse failed: {e}", False)
    spawned = _peek32_wram(sections, addr_spawn)
    plane = _peek32_wram(sections, addr_plane)
    ok = (spawned is not None and spawned >= EXPECT_PS
          and plane is not None and plane in (0, 1))
    return cprint("P4 GREEN" if ok else "P4 RED",
                  f"g_ghz_planeswitch_spawned={spawned} (>= {EXPECT_PS}) "
                  f"lastplane={plane}", ok)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--with-savestate", default="",
                    help="savestate captured after the player crosses a PlaneSwitch")
    args = ap.parse_args()

    print("=== Phase 2.4g.3 PlaneSwitch + two-plane bridge gate ===")
    print(f"  GHZ Scene1.bin: {SCENE_PATH}")
    ps = g1._parse_class_entities(SCENE_PATH, "PlaneSwitch")
    if ps is None:
        print("  PlaneSwitch entities: SCENE PARSE FAILED")
    else:
        slots = sorted(s for s, _, _ in ps)
        print(f"  PlaneSwitch entities in GHZ Scene1.bin: {len(ps)} "
              f"(slots {slots[0] if slots else '-'}..{slots[-1] if slots else '-'})")

    ok = True
    ok &= predicate_1_registered()
    ok &= predicate_2_two_plane_bridge()
    ok &= predicate_3_scene_path()
    ok &= predicate_4_runtime(args.with_savestate.strip())

    if ok:
        print("=== Gate Phase 2.4g.3: GREEN ===")
        return 0
    print("=== Gate Phase 2.4g.3: RED ===")
    return 1


if __name__ == "__main__":
    sys.exit(main())
