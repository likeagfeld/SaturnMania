#!/usr/bin/env python3
"""qa_phase2_4plat_gate.py - Phase 2.4-PLAT GHZ platforming entity ports gate.

BINDING per CLAUDE.md #4.7 + memory/qa-iterative-improvement.md. Authored
BEFORE any 2.4-PLAT implementation lands; MUST fire RED on the current
build (the symbols do not exist yet).

2.4-PLAT ports five GHZ Act 1 platforming entities as real RSDK objects on
the same spawn pipeline as 2.4g/2.4h (overflow via compaction cursor):

  - CollapsingPlatform (15 instances) -- trigger; collapses under the
        player.  IN-GAME INVISIBLE (decomp Update sets visible=false; the
        crumbling surface is the FG tilemap, no per-entity sprite).
  - Bridge             (13 instances) -- VISIBLE; planks with sine
        depression drawn from GHZ/Bridge.bin via the entity_atlas.
  - ForceSpin          (13 instances) -- INVISIBLE trigger; forces the
        player into a tube-roll state.
  - BreakableWall      (23 instances) -- trigger; breaks when the player
        spins into it. IN-GAME INVISIBLE (decomp visible=debugActive; the
        wall surface is the FG tilemap, no per-entity sprite).
  - SpinBooster        (4 instances)  -- INVISIBLE trigger; rolls + boosts
        the player along a direction.

Decomp authority (cached in tools/_decomp_raw/):
  - SonicMania_Objects_Common_CollapsingPlatform.c -- _Create (data==NULL =>
        scene platform; visible toggled debug-only in _Update), _StageLoad.
  - SonicMania_Objects_GHZ_Bridge.c -- _Draw (sine-curve plank depression
        via RSDK.DrawSprite from GHZ/Bridge.bin), _HandleCollisions
        (Player_CheckCollisionTouch), _Create (visible=true, drawGroup[0]).
  - SonicMania_Objects_Common_ForceSpin.c -- _Create (visible=false),
        _Update (Zone_RotateOnPivot bbox -> ForceSpin_SetPlayerState =>
        ANI_JUMP + Player_State_TubeRoll/TubeAirRoll + groundVel write).
  - SonicMania_Objects_Common_BreakableWall.c -- _CheckBreak_Wall
        (Player_CheckCollisionBox solid + canBreak gate
        abs(groundVel)>=0x48000 && onGround && animationID==ANI_JUMP ->
        BreakableWall_Break + destroyEntity), _Create (data==NULL =>
        invisible scene wall, drawGroup[1]).
  - SonicMania_Objects_Common_SpinBooster.c -- _Create (visible=false),
        _Update (Zone_RotateOnPivot + activePlayers bitmask ->
        HandleForceRoll -> ApplyRollVelocity boostPower write).

Predicates (static unless noted):

  P1 - each class is a registered RSDK object.
       (a) game.map contains <Class>_Create AND <Class>_Update for all 5.
       (b) Game.c registers each via rsdk_object_register_ex("<Class>", ...).

  P2 - each class resolves player overlap through a canonical collision
       surface in its .c body:
         * BreakableWall  -> Player_CheckCollisionBox (solid + break gate).
         * Bridge         -> Player_CheckCollisionTouch (plank step-on).
         * CollapsingPlatform -> Player_CheckCollisionBox OR
                            Player_CheckCollisionTouch (trigger).
         * ForceSpin / SpinBooster -> trigger bbox test; accept either a
                            collision token OR the Zone_RotateOnPivot bbox
                            trigger marker present in the .c.

  P3 - scene spawn wires all five + slot/BSS budget holds.
       (a) GHZ Scene1.bin counts: CollapsingPlatform=15, Bridge=13,
           ForceSpin=13, BreakableWall=23, SpinBooster=4.
       (b) RSDK_TEMPENTITY_COUNT >= the combined overflow count of every
           overflow class spawned in the GHZ path (2.4g + 2.4h + 2.4-PLAT),
           i.e. slots >= RSDK_SCENEENTITY_COUNT.
       (c) BSS budget: game.map _end < 0x060C0000 (SGL work-area floor).

  P4 - (runtime, --with-savestate) >= 1 of each class is populated in an
       RSDK slot. Verified by peeking the diag globals
       g_ghz_<class>_spawned resolved from game.map (WRAM-H pair-swap
       aware). SKIPPED without --with-savestate.

  P5 - asset provenance (CONTENT, not filename). Only Bridge is in-game
       visible -> only cd/BRIDGE.SP2 must be byte-for-byte reproducible
       from extracted/Data/Sprites/GHZ/Bridge.bin via build_entity_atlas.
       The 4 invisible classes ship NO atlas (decomp-assets-only is
       trivially satisfied -- no synthesized pixels exist).

Exit code: 0 = all GREEN. Non-zero = any RED.

Run:
    py -3 tools/qa_phase2_4plat_gate.py
    py -3 tools/qa_phase2_4plat_gate.py --with-savestate samples/qa_phase2_4plat.mcs
"""

import argparse
import os
import re
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCENE_PATH = os.path.join(ROOT, "extracted", "Data", "Stages", "GHZ", "Scene1.bin")

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qa_phase2_4g1_gate as g1  # noqa: E402

# Expected GHZ Act 1 instance counts (verified against Scene1.bin below).
EXPECT = {
    "CollapsingPlatform": 15,
    "Bridge": 13,
    "ForceSpin": 13,
    "BreakableWall": 23,
    "SpinBooster": 4,
}

# Every overflow class spawned in the GHZ path: 2.4g (InvisibleBlock /
# BoundsMarker / PlaneSwitch) + 2.4h (Chopper / Crabmeat / Batbrain) +
# 2.4-PLAT (the five below). The temp budget must hold the SUM of their
# overflow (slot >= SCENEENTITY_COUNT) instance counts.
OVERFLOW_CLASSES = [
    "InvisibleBlock", "BoundsMarker", "PlaneSwitch",
    "Chopper", "Crabmeat", "Batbrain",
    "CollapsingPlatform", "Bridge", "ForceSpin", "BreakableWall", "SpinBooster",
]

# Source .c per class (Common vs GHZ category) for the P2 collision check.
SRC = {
    "CollapsingPlatform": os.path.join(ROOT, "src", "mania", "Objects", "Common", "CollapsingPlatform.c"),
    "Bridge":             os.path.join(ROOT, "src", "mania", "Objects", "GHZ", "Bridge.c"),
    "ForceSpin":          os.path.join(ROOT, "src", "mania", "Objects", "Common", "ForceSpin.c"),
    "BreakableWall":      os.path.join(ROOT, "src", "mania", "Objects", "Common", "BreakableWall.c"),
    "SpinBooster":        os.path.join(ROOT, "src", "mania", "Objects", "Common", "SpinBooster.c"),
}

# P2: per-class accepted collision-surface tokens (any one suffices).
COLLISION_TOKENS = {
    "BreakableWall":      ["Player_CheckCollisionBox"],
    "Bridge":             ["Player_CheckCollisionTouch", "Player_CheckCollisionBox"],
    "CollapsingPlatform": ["Player_CheckCollisionBox", "Player_CheckCollisionTouch"],
    "ForceSpin":          ["Player_CheckCollisionBox", "Player_CheckCollisionTouch",
                           "Zone_RotateOnPivot"],
    "SpinBooster":        ["Player_CheckCollisionBox", "Player_CheckCollisionTouch",
                           "Zone_RotateOnPivot"],
}

# Source blob + shipped SP2 for the provenance check (Bridge only; the
# other 4 classes are in-game invisible and ship no atlas).
ASSETS = {
    "Bridge": ("extracted/Data/Sprites/GHZ/Bridge.bin", "cd/BRIDGE.SP2"),
}

BSS_FLOOR = 0x060C0000
SCENEENTITY_COUNT = 0x50


def cprint(tag, msg, ok):
    colour = "\033[32m" if ok else "\033[31m"
    reset = "\033[0m"
    print(f"  [{colour}{tag}{reset}] {msg}")
    return ok


def _read(path):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        return f.read()


def _count(cls):
    e = g1._parse_class_entities(SCENE_PATH, cls)
    return len(e) if e is not None else -1


def _overflow_count(cls):
    e = g1._parse_class_entities(SCENE_PATH, cls)
    if e is None:
        return None
    return sum(1 for s, _, _ in e if s >= SCENEENTITY_COUNT)


# --- P1: each class registered as an RSDK object --------------------------

def predicate_1_registered():
    mp = os.path.join(ROOT, "game.map")
    if not os.path.exists(mp):
        return cprint("P1 RED", "game.map not found (build first)", False)
    body = _read(mp)
    gc = os.path.join(ROOT, "src", "mania", "Game.c")
    gc_body = _read(gc) if os.path.exists(gc) else ""

    all_ok = True
    for cls in EXPECT:
        have_create = re.search(rf"\b{cls}_Create\b", body) is not None
        have_update = re.search(rf"\b{cls}_Update\b", body) is not None
        reg_ok = re.search(rf'rsdk_object_register_ex\s*\(\s*"{cls}"', gc_body) is not None
        ok = have_create and have_update and reg_ok
        all_ok &= cprint(f"P1.{cls} {'GREEN' if ok else 'RED'}",
                         f"create={have_create} update={have_update} "
                         f"register_ex={reg_ok}", ok)
    return all_ok


# --- P2: each class resolves overlap via a canonical collision surface ----

def predicate_2_collision():
    all_ok = True
    for cls, path in SRC.items():
        if not os.path.exists(path):
            all_ok &= cprint(f"P2.{cls} RED",
                             f"{os.path.relpath(path, ROOT)} not found", False)
            continue
        src = _read(path)
        toks = COLLISION_TOKENS[cls]
        hit = next((t for t in toks if re.search(re.escape(t), src)), None)
        ok = hit is not None
        all_ok &= cprint(f"P2.{cls} {'GREEN' if ok else 'RED'}",
                         f"collision surface {hit if ok else 'ABSENT'} "
                         f"(accept {'|'.join(toks)}) in {cls}.c", ok)
    return all_ok


# --- P3: scene spawn + slot/BSS budget ------------------------------------

def predicate_3_scene_budget():
    sub = []

    for cls, exp in EXPECT.items():
        n = _count(cls)
        sub.append((f"3a.{cls}", n == exp,
                    f"GHZ Scene1.bin {cls} count = {n} (expect {exp})"))

    oh = os.path.join(ROOT, "src", "rsdk", "object.h")
    budget = -1
    if os.path.exists(oh):
        m = re.search(r"#define\s+RSDK_TEMPENTITY_COUNT\s+(0x[0-9a-fA-F]+|\d+)",
                      _read(oh))
        if m:
            budget = int(m.group(1), 0)
    need = 0
    parts = []
    for cls in OVERFLOW_CLASSES:
        oc = _overflow_count(cls)
        oc = 0 if oc is None else oc
        need += oc
        parts.append(f"{cls}={oc}")
    sub.append(("3b", budget >= need,
                f"RSDK_TEMPENTITY_COUNT = {budget} >= overflow sum "
                f"({' + '.join(parts)}) = {need}"))

    mp = os.path.join(ROOT, "game.map")
    end_addr = None
    if os.path.exists(mp):
        m = re.search(r"(0x0[06][0-9a-fA-F]+)\s+_end\b", _read(mp))
        if m:
            end_addr = int(m.group(1), 16)
    end_ok = end_addr is not None and end_addr < BSS_FLOOR
    sub.append(("3c", end_ok,
                f"_end = {hex(end_addr) if end_addr else 'N/A'} < "
                f"{hex(BSS_FLOOR)} (SGL work-area floor)"))

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
                      "no --with-savestate; static P1-P3+P5 carry the RED-now contract",
                      True)
    mp = os.path.join(ROOT, "game.map")
    if not os.path.exists(mp):
        return cprint("P4 RED", "game.map not found", False)
    map_body = _read(mp)
    try:
        from mcs_extract import parse_savestate  # type: ignore
        from pathlib import Path
        sections = parse_savestate(Path(state_path))
    except Exception as e:
        return cprint("P4 RED", f"savestate parse failed: {e}", False)

    all_ok = True
    for cls in EXPECT:
        sym = f"g_ghz_{cls.lower()}_spawned"
        addr = _resolve_sym(map_body, sym)
        if addr is None:
            all_ok &= cprint(f"P4.{cls} RED", f"{sym} symbol missing", False)
            continue
        spawned = _peek32_wram(sections, addr)
        ok = spawned is not None and spawned >= 1
        all_ok &= cprint(f"P4.{cls} {'GREEN' if ok else 'RED'}",
                         f"{sym} = {spawned} (>= 1)", ok)
    return all_ok


# --- P5: asset provenance (content-reproducible from decomp blob) ----------

def predicate_5_provenance():
    try:
        import build_entity_atlas as bea  # noqa: E402
    except Exception as e:
        return cprint("P5 RED", f"build_entity_atlas import failed: {e}", False)

    all_ok = True
    for cls, (bin_rel, spr_rel) in ASSETS.items():
        bin_full = os.path.join(ROOT, bin_rel)
        spr_full = os.path.join(ROOT, spr_rel)
        if not os.path.exists(spr_full):
            all_ok &= cprint(f"P5.{cls} RED",
                             f"{spr_rel} not shipped (build assets first)", False)
            continue
        if not os.path.exists(bin_full):
            all_ok &= cprint(f"P5.{cls} RED",
                             f"source blob {bin_rel} missing", False)
            continue
        try:
            with tempfile.TemporaryDirectory() as td:
                tmp_spr = os.path.join(td, "rebuilt.sp2")
                tmp_met = os.path.join(td, "rebuilt.met")
                bea.build_atlas(bin_full, tmp_spr, tmp_met, drop_anims=[])
                with open(tmp_spr, "rb") as f:
                    rebuilt = f.read()
            with open(spr_full, "rb") as f:
                shipped = f.read()
            ok = rebuilt == shipped
            all_ok &= cprint(f"P5.{cls} {'GREEN' if ok else 'RED'}",
                             f"{spr_rel} ({len(shipped)} B) "
                             f"{'reproducible from' if ok else 'DIVERGES from'} "
                             f"{bin_rel} (rebuilt {len(rebuilt)} B)", ok)
        except Exception as e:
            all_ok &= cprint(f"P5.{cls} RED", f"rebuild failed: {e}", False)
    return all_ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--with-savestate", default="",
                    help="savestate captured in GHZ Act 1 with PLAT entities spawned")
    args = ap.parse_args()

    print("=== Phase 2.4-PLAT GHZ platforming entity ports gate ===")
    print(f"  GHZ Scene1.bin: {SCENE_PATH}")
    for cls in EXPECT:
        e = g1._parse_class_entities(SCENE_PATH, cls)
        if e is None:
            print(f"  {cls}: SCENE PARSE FAILED")
        else:
            slots = sorted(s for s, _, _ in e)
            print(f"  {cls}: {len(e)} instances "
                  f"(slots {slots[0] if slots else '-'}..{slots[-1] if slots else '-'})")

    ok = True
    ok &= predicate_1_registered()
    ok &= predicate_2_collision()
    ok &= predicate_3_scene_budget()
    ok &= predicate_5_provenance()
    ok &= predicate_4_runtime(args.with_savestate.strip())

    if ok:
        print("=== Gate Phase 2.4-PLAT: GREEN ===")
        return 0
    print("=== Gate Phase 2.4-PLAT: RED ===")
    return 1


if __name__ == "__main__":
    sys.exit(main())
