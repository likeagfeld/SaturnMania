#!/usr/bin/env python3
"""qa_phase2_4g2_gate.py - Phase 2.4g.2 Zone-bounds + BoundsMarker gate.

BINDING per CLAUDE.md #4.7 + memory/qa-iterative-improvement.md +
memory/ghz-pivot-to-rsdk-engine.md. Authored BEFORE any 2.4g.2
implementation lands; MUST fire RED on the current build (the symbols do
not exist yet).

2.4g.2 is the second increment of the GHZ -> RSDK-engine pivot. It ports
the RSDK Zone object's camera-bounds + death-boundary globals (NOT the
whole Zone state machine) plus BoundsMarker.c, and spawns BoundsMarker's
22 GHZ Act 1 instances into RSDK slots through the same spawn pipeline
that 2.4g.1 used for InvisibleBlock (18 instances).

Decomp authority:
  - tools/_decomp_raw/SonicMania_Objects_Global_Zone.c:221-235
      (default bounds init: cameraBoundsL/R/T/B[s], playerBoundsL/R/T/B[s],
       deathBoundary[s]) + Zone.h:65-73 (the bounds field set).
  - tools/_decomp_raw/SonicMania_Objects_Global_BoundsMarker.c
      _Create L28-47 (active=ACTIVE_XBOUNDS, width default 48<<15,
        ApplyBounds setPos=true), _Update L12-20 (ApplyBounds per frame),
        _ApplyBounds L51-98 (switch type -> writes Zone->cameraBoundsB/T
        + playerBoundsB/T + deathBoundary when |marker.x - player.x| <
        marker.width).

Predicates (static unless noted):

  P1 - BoundsMarker is a registered RSDK object.
       (a) game.map contains BoundsMarker_Create AND BoundsMarker_Update.
       (b) Game.c registers it via rsdk_object_register_ex("BoundsMarker", ...).

  P2 - Zone camera-bounds globals exist (in game.map):
       g_zone_cameraBoundsB, g_zone_cameraBoundsT, g_zone_cameraBoundsL,
       g_zone_cameraBoundsR, g_zone_deathBoundary.

  P3 - scene spawn wires BoundsMarker.
       (a) 22 BoundsMarker entities counted in GHZ Scene1.bin by class hash.
       (b) The spawn pipeline (scene.c) reaches BoundsMarker: since the
           compaction path is class-agnostic, all that is required is that
           the combined overflow count (InvisibleBlock 18 + BoundsMarker 22
           = 40) fits the temp-entity budget. We assert scene.c's
           RSDK_TEMPENTITY_COUNT >= 40 so neither class is silently dropped.
       (c) the GHZ camera clamp reads the Zone bounds: scene_ghz.c
           ghz_set_camera references g_zone_cameraBounds* (token present).

  P4 - (runtime, --with-savestate) the Zone cameraBoundsB or cameraBoundsT
       global is nonzero/changed after GHZ load (BoundsMarker_Create's
       ApplyBounds wrote it). Verified by peeking g_zone_cameraBoundsB[0]
       resolved from game.map (WRAM-H pair-swap aware). SKIPPED without
       --with-savestate (static P1-P3 carry the RED-now demonstration).

Exit code: 0 = all GREEN. Non-zero = any RED.

Run:
    py -3 tools/qa_phase2_4g2_gate.py
    py -3 tools/qa_phase2_4g2_gate.py --with-savestate samples/qa_phase2_4g2.mcs
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


def cprint(tag, msg, ok):
    colour = "\033[32m" if ok else "\033[31m"
    reset = "\033[0m"
    print(f"  [{colour}{tag}{reset}] {msg}")
    return ok


def _read(path):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        return f.read()


# --- P1: BoundsMarker registered as an RSDK object ------------------------

def predicate_1_registered():
    mp = os.path.join(ROOT, "game.map")
    if not os.path.exists(mp):
        return cprint("P1 RED", "game.map not found (build first)", False)
    body = _read(mp)
    have_create = re.search(r"\bBoundsMarker_Create\b", body) is not None
    have_update = re.search(r"\bBoundsMarker_Update\b", body) is not None

    gc = os.path.join(ROOT, "src", "mania", "Game.c")
    reg_ok = False
    if os.path.exists(gc):
        reg_ok = re.search(
            r'rsdk_object_register_ex\s*\(\s*"BoundsMarker"', _read(gc)) is not None

    if have_create and have_update and reg_ok:
        return cprint("P1 GREEN",
                      "BoundsMarker_Create+_Update in game.map; registered in Game.c",
                      True)
    return cprint(
        "P1 RED",
        f"create={have_create} update={have_update} register_ex={reg_ok} "
        "(BoundsMarker not yet a registered RSDK object)",
        False)


# --- P2: Zone camera-bounds globals ---------------------------------------

def predicate_2_zone_globals():
    mp = os.path.join(ROOT, "game.map")
    if not os.path.exists(mp):
        return cprint("P2 RED", "game.map not found", False)
    body = _read(mp)
    syms = ["g_zone_cameraBoundsB", "g_zone_cameraBoundsT",
            "g_zone_cameraBoundsL", "g_zone_cameraBoundsR",
            "g_zone_deathBoundary"]
    present = {s: (re.search(r"\b" + s + r"\b", body) is not None) for s in syms}
    if all(present.values()):
        return cprint("P2 GREEN",
                      "Zone camera-bounds + deathBoundary globals in game.map", True)
    missing = [s for s, ok in present.items() if not ok]
    return cprint("P2 RED", f"missing Zone globals: {', '.join(missing)}", False)


# --- P3: scene spawn wires BoundsMarker -----------------------------------

def predicate_3_scene_path():
    sub = []

    # (a) 22 BoundsMarker entities counted in GHZ Scene1.bin.
    bm = g1._parse_class_entities(SCENE_PATH, "BoundsMarker")
    bm_n = len(bm) if bm is not None else -1
    sub.append(("3a", bm_n == 22,
                f"GHZ Scene1.bin BoundsMarker count = {bm_n} (expect 22)"))

    # (b) temp-entity budget >= combined overflow (IB 18 + BM 22 = 40).
    oh = os.path.join(ROOT, "src", "rsdk", "object.h")
    budget = -1
    if os.path.exists(oh):
        m = re.search(r"#define\s+RSDK_TEMPENTITY_COUNT\s+(0x[0-9a-fA-F]+|\d+)",
                      _read(oh))
        if m:
            budget = int(m.group(1), 0)
    ib = g1._parse_class_entities(SCENE_PATH, "InvisibleBlock")
    ib_n = len(ib) if ib is not None else 0
    need = ib_n + bm_n
    sub.append(("3b", budget >= need,
                f"RSDK_TEMPENTITY_COUNT = {budget} >= IB{ib_n}+BM{bm_n} = {need} "
                f"(temp budget must hold both classes' overflow slots)"))

    # (c) GHZ camera clamp reads Zone bounds.
    sg = os.path.join(ROOT, "src", "rsdk", "scene_ghz.c")
    clamp_ok = False
    if os.path.exists(sg):
        clamp_ok = re.search(r"g_zone_cameraBounds", _read(sg)) is not None
    sub.append(("3c", clamp_ok,
                f"scene_ghz.c camera clamp reads g_zone_cameraBounds "
                f"{'present' if clamp_ok else 'ABSENT'}"))

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
    # g_zone_cameraBoundsB is an int32[PLAYER_COUNT]; index [0] is the base.
    addr_b = _resolve_sym(map_body, "g_zone_cameraBoundsB")
    addr_t = _resolve_sym(map_body, "g_zone_cameraBoundsT")
    if addr_b is None:
        return cprint("P4 RED", "g_zone_cameraBoundsB symbol missing", False)
    try:
        from mcs_extract import parse_savestate  # type: ignore
        from pathlib import Path
        sections = parse_savestate(Path(state_path))
    except Exception as e:
        return cprint("P4 RED", f"savestate parse failed: {e}", False)
    bb = _peek32_wram(sections, addr_b)
    bt = _peek32_wram(sections, addr_t) if addr_t else None
    # After GHZ load, Zone_StageLoad seeds cameraBoundsB[0] = world height
    # (layerSize.y), and any crossed BoundsMarker overrides it. A nonzero
    # value proves the bounds globals are populated by the GHZ-load path.
    ok = (bb is not None and bb != 0)
    return cprint("P4 GREEN" if ok else "P4 RED",
                  f"g_zone_cameraBoundsB[0]={bb} cameraBoundsT[0]={bt}", ok)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--with-savestate", default="",
                    help="savestate captured after GHZ load (.mcs/.mc0)")
    args = ap.parse_args()

    print("=== Phase 2.4g.2 Zone-bounds + BoundsMarker gate ===")
    print(f"  GHZ Scene1.bin: {SCENE_PATH}")
    bm = g1._parse_class_entities(SCENE_PATH, "BoundsMarker")
    if bm is None:
        print("  BoundsMarker entities: SCENE PARSE FAILED")
    else:
        slots = sorted(s for s, _, _ in bm)
        print(f"  BoundsMarker entities in GHZ Scene1.bin: {len(bm)} "
              f"(slots {slots[0] if slots else '-'}..{slots[-1] if slots else '-'})")

    ok = True
    ok &= predicate_1_registered()
    ok &= predicate_2_zone_globals()
    ok &= predicate_3_scene_path()
    ok &= predicate_4_runtime(args.with_savestate.strip())

    if ok:
        print("=== Gate Phase 2.4g.2: GREEN ===")
        return 0
    print("=== Gate Phase 2.4g.2: RED ===")
    return 1


if __name__ == "__main__":
    sys.exit(main())
