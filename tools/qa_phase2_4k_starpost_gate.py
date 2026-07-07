#!/usr/bin/env python3
"""qa_phase2_4k_starpost_gate.py - Phase 2.4k StarPost port gate.

BINDING per CLAUDE.md #4.7 + memory/qa-iterative-improvement.md. Authored
BEFORE any 2.4k implementation lands; MUST fire RED on the current build
(StarPost.c/h do not exist yet).

2.4k ports the Global StarPost object as a real RSDK entity on the same
spawn pipeline as 2.4g/2.4h/2.4-PLAT.

Decomp authority:
  - tools/_decomp_raw/SonicMania_Objects_Global_StarPost.c
  - tools/_decomp_raw/SonicMania_Objects_Global_StarPost.h

Predicates (all static; no Mednafen/Docker needed in cloud):

  P1 - StarPost is a registered RSDK object.
       (a) src/mania/Objects/Global/StarPost.h + StarPost.c exist.
       (b) game.map contains StarPost_Create AND StarPost_Update (if map exists).
       (c) Game.c registers via rsdk_object_register_ex("StarPost", ...).

  P2 - StarPost source includes the Saturn-specific draw path.
       StarPost_draw_only is defined in StarPost.c AND called from
       mania_ghz_draw_only in Game.c.

  P3 - StarPost asset load is wired on the scene-load path.
       StarPost_load_assets is called from entities_load_assets in Entities.c.

  P4 - g_starpost_atlas extern declared in entity_atlas.h.

  P5 - scene.c carries fill_starpost_attributes for the id/vsRemove/direction
       multi-attribute fill (StarPost has StateMachine at +132, so the generic
       fill_first_attribute would write id to the wrong offset).

Exit code: 0 = all GREEN. Non-zero = any RED.

Run:
    py -3 tools/qa_phase2_4k_starpost_gate.py
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def cprint(tag, msg, ok):
    colour = "\033[32m" if ok else "\033[31m"
    reset = "\033[0m"
    print(f"  [{colour}{tag}{reset}] {msg}")
    return ok


def _read(path):
    try:
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            return f.read()
    except FileNotFoundError:
        return None


# --- P1: StarPost registered as RSDK object ----------------------------------

def predicate_1_registered():
    h_path = os.path.join(ROOT, "src", "mania", "Objects", "Global", "StarPost.h")
    c_path = os.path.join(ROOT, "src", "mania", "Objects", "Global", "StarPost.c")
    game_c = os.path.join(ROOT, "src", "mania", "Game.c")

    h_ok = os.path.exists(h_path)
    c_ok = os.path.exists(c_path)
    all_ok = True
    all_ok &= cprint("P1a GREEN" if h_ok else "P1a RED",
                     f"StarPost.h exists: {h_path}", h_ok)
    all_ok &= cprint("P1b GREEN" if c_ok else "P1b RED",
                     f"StarPost.c exists: {c_path}", c_ok)

    gc = _read(game_c)
    if gc is None:
        all_ok &= cprint("P1c RED", "Game.c not found", False)
        return all_ok

    reg_ok = re.search(r'rsdk_object_register_ex\s*\(\s*"StarPost"', gc) is not None
    all_ok &= cprint("P1c GREEN" if reg_ok else "P1c RED",
                     f"rsdk_object_register_ex(\"StarPost\") in Game.c: {reg_ok}", reg_ok)

    mp = os.path.join(ROOT, "game.map")
    if os.path.exists(mp):
        body = _read(mp)
        have_create = re.search(r"\bStarPost_Create\b", body) is not None
        have_update = re.search(r"\bStarPost_Update\b", body) is not None
        ok = have_create and have_update
        all_ok &= cprint("P1d GREEN" if ok else "P1d RED",
                         f"game.map StarPost_Create={have_create} StarPost_Update={have_update}",
                         ok)
    else:
        cprint("P1d SKIP", "game.map not found (build first to check map)", True)

    return all_ok


# --- P2: StarPost_draw_only wired in mania_ghz_draw_only --------------------

def predicate_2_draw():
    c_path = os.path.join(ROOT, "src", "mania", "Objects", "Global", "StarPost.c")
    game_c = os.path.join(ROOT, "src", "mania", "Game.c")

    all_ok = True
    sc = _read(c_path)
    if sc is None:
        all_ok &= cprint("P2a RED", "StarPost.c missing - cannot check draw_only definition", False)
    else:
        has_draw = re.search(r"\bStarPost_draw_only\s*\(", sc) is not None
        all_ok &= cprint("P2a GREEN" if has_draw else "P2a RED",
                         f"StarPost_draw_only defined in StarPost.c: {has_draw}", has_draw)

    gc = _read(game_c)
    if gc is None:
        all_ok &= cprint("P2b RED", "Game.c not found", False)
    else:
        has_call = re.search(r"\bStarPost_draw_only\s*\(", gc) is not None
        all_ok &= cprint("P2b GREEN" if has_call else "P2b RED",
                         f"StarPost_draw_only called in Game.c (mania_ghz_draw_only): {has_call}",
                         has_call)
    return all_ok


# --- P3: StarPost_load_assets wired in entities_load_assets -----------------

def predicate_3_load():
    ent_c = os.path.join(ROOT, "src", "mania", "Objects", "Common", "Entities.c")
    body = _read(ent_c)
    if body is None:
        return cprint("P3 RED", "Entities.c not found", False)

    has_load = re.search(r"\bStarPost_load_assets\s*\(", body) is not None
    return cprint("P3 GREEN" if has_load else "P3 RED",
                  f"StarPost_load_assets called in Entities.c: {has_load}", has_load)


# --- P4: g_starpost_atlas declared in entity_atlas.h ------------------------

def predicate_4_atlas():
    h = os.path.join(ROOT, "src", "rsdk", "entity_atlas.h")
    body = _read(h)
    if body is None:
        return cprint("P4 RED", "entity_atlas.h not found", False)

    has_extern = re.search(r"extern\s+entity_atlas_t\s+g_starpost_atlas", body) is not None
    return cprint("P4 GREEN" if has_extern else "P4 RED",
                  f"extern entity_atlas_t g_starpost_atlas in entity_atlas.h: {has_extern}",
                  has_extern)


# --- P5: fill_starpost_attributes in scene.c ---------------------------------

def predicate_5_scene():
    sc = os.path.join(ROOT, "src", "rsdk", "scene.c")
    body = _read(sc)
    if body is None:
        return cprint("P5 RED", "scene.c not found", False)

    has_fill = re.search(r"\bfill_starpost_attributes\b", body) is not None
    return cprint("P5 GREEN" if has_fill else "P5 RED",
                  f"fill_starpost_attributes present in scene.c: {has_fill}", has_fill)


def main():
    print("=== Gate V-2.4k: StarPost port (Phase 2.4k) ===")

    all_ok = True
    all_ok &= predicate_1_registered()
    all_ok &= predicate_2_draw()
    all_ok &= predicate_3_load()
    all_ok &= predicate_4_atlas()
    all_ok &= predicate_5_scene()

    print()
    if all_ok:
        print("  [GREEN] Gate V-2.4k PASSED -- StarPost port wired.")
    else:
        print("  [RED]   Gate V-2.4k FAILED -- see RED predicates above.")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
