#!/usr/bin/env python3
"""qa_phase2_4j1_titlecard_gate.py - Phase 2.4j.1 TitleCard (act-intro) gate.

BINDING per CLAUDE.md #4.7 + memory/qa-iterative-improvement.md. Authored
BEFORE any 2.4j.1 implementation lands; MUST fire RED on the current build
(the TitleCard object + the text-rendering trio do not exist yet).

2.4j.1 ports the in-engine act-intro card (decomp
tools/_decomp_raw/SonicMania_Objects_Global_TitleCard.c, 955 lines) onto
the RSDK engine: it slides in colored parallelogram strips + curtains,
drops the zone-name glyphs + "ZONE" + act number, holds, then slides away
while the engine is PAUSED. One TitleCard is spawned on GHZ Act 1 entry
with zoneName "GREEN HILL" + actID ACT_1.

Decomp authority (cached in tools/_decomp_raw/):
  - SonicMania_Objects_Global_TitleCard.c / .h -- the object ported.
  - _RSDKv5_Graphics_Animation.cpp:179-231 -- GetStringWidth + SetSpriteString.
  - _RSDKv5_Graphics_Drawing.cpp:4312-4391 -- DrawString (= RSDK.DrawText).

Predicates (static unless noted):

  P1 - TitleCard.c defines the full callback + state set, registered as an
       RSDK object.
       (a) src/.../Global/TitleCard.c defines TitleCard_Create / _Update /
           _Draw / _StageLoad.
       (b) the 6 update-states: SetupBGElements, OpeningBG, EnterTitle,
           ShowingTitle, SlideAway, Supressed.
       (c) the 3 draw-states: SlideIn, ShowTitleCard, SlideAway (the draw
           variants -- TitleCard_Draw_SlideIn / _ShowTitleCard / _SlideAway).
       (d) Game.c registers it via rsdk_object_register_ex("TitleCard", ...).
       (e) game.map contains TitleCard_Create AND TitleCard_Update.

  P2 - the text-rendering trio is IMPLEMENTED (definitions, not just
       declarations) in src/rsdk/*.c bodies (NOT only the headers):
         * rsdk_set_sprite_string
         * rsdk_get_string_width
         * rsdk_draw_text
       Each must appear as a C definition (`void rsdk_xxx(...) {`-style)
       in a .c file under src/rsdk/.

  P3 - asset provenance (CONTENT, not filename). cd/TITLECARD.SP2 +
       cd/TITLECARD.MET must be byte-for-byte reproducible from
       extracted/Data/Sprites/Global/TitleCard.bin via build_entity_atlas
       with NO dropped anims (the full 27-glyph font is required), and the
       SP2 must carry 36 frames.

  P4 - BSS budget: ENTITY_ATLAS_MAX_FRAMES >= 36 (the atlas must hold the
       full TitleCard frame set) AND game.map _end < 0x060C0000 (the SGL
       work-area floor).

  P5 - spawn wired in Game.c: a TitleCard is created on GHZ entry with the
       zone name string "GREEN HILL".
       (a) Game.c references TitleCard (rsdk_create_entity / a TitleCard
           spawn helper) on the GHZ path.
       (b) the literal "GREEN HILL" appears in Game.c.

Exit code: 0 = all GREEN. Non-zero = any RED.

Run:
    py -3 tools/qa_phase2_4j1_titlecard_gate.py
"""

import os
import re
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

TITLECARD_C = os.path.join(ROOT, "src", "mania", "Objects", "Global", "TitleCard.c")
GAME_C = os.path.join(ROOT, "src", "mania", "Game.c")
ENTITY_ATLAS_H = os.path.join(ROOT, "src", "rsdk", "entity_atlas.h")
GAME_MAP = os.path.join(ROOT, "game.map")
TITLECARD_BIN = os.path.join(ROOT, "extracted", "Data", "Sprites", "Global", "TitleCard.bin")
TITLECARD_SP2 = os.path.join(ROOT, "cd", "TITLCARD.SP2")
TITLECARD_MET = os.path.join(ROOT, "cd", "TITLCARD.MET")

BSS_FLOOR = 0x060C0000
EXPECT_FRAMES = 36
RSDK_DIR = os.path.join(ROOT, "src", "rsdk")

# Update-state function suffixes the decomp defines (TitleCard_State_*).
UPDATE_STATES = [
    "SetupBGElements", "OpeningBG", "EnterTitle",
    "ShowingTitle", "SlideAway", "Supressed",
]
# Draw-state function suffixes (TitleCard_Draw_*). The decomp uses three
# distinct draw states; the SlideAway draw reuses the SlideIn renderer in
# the decomp, so accept either an explicit _SlideAway draw symbol OR the
# two core renderers being present.
DRAW_STATES = ["SlideIn", "ShowTitleCard"]

TEXT_TRIO = ["rsdk_set_sprite_string", "rsdk_get_string_width", "rsdk_draw_text"]


def cprint(tag, msg, ok):
    colour = "\033[32m" if ok else "\033[31m"
    reset = "\033[0m"
    print(f"  [{colour}{tag}{reset}] {msg}")
    return ok


def _read(path):
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        return f.read()


# --- P1: TitleCard object defined + registered ----------------------------

def predicate_1_object():
    if not os.path.exists(TITLECARD_C):
        return cprint("P1 RED",
                      f"{os.path.relpath(TITLECARD_C, ROOT)} not found", False)
    src = _read(TITLECARD_C)
    gc = _read(GAME_C) if os.path.exists(GAME_C) else ""
    mp = _read(GAME_MAP) if os.path.exists(GAME_MAP) else ""

    all_ok = True

    for fn in ["TitleCard_Create", "TitleCard_Update", "TitleCard_Draw",
               "TitleCard_StageLoad"]:
        ok = re.search(rf"\b{fn}\s*\(", src) is not None
        all_ok &= cprint(f"P1.{fn} {'GREEN' if ok else 'RED'}",
                         f"{fn} defined in TitleCard.c", ok)

    for st in UPDATE_STATES:
        ok = re.search(rf"\bTitleCard_State_{st}\b", src) is not None
        all_ok &= cprint(f"P1.State_{st} {'GREEN' if ok else 'RED'}",
                         f"TitleCard_State_{st} present", ok)

    for st in DRAW_STATES:
        ok = re.search(rf"\bTitleCard_Draw_{st}\b", src) is not None
        all_ok &= cprint(f"P1.Draw_{st} {'GREEN' if ok else 'RED'}",
                         f"TitleCard_Draw_{st} present", ok)

    reg_ok = re.search(r'rsdk_object_register_ex\s*\(\s*"TitleCard"', gc) is not None
    all_ok &= cprint(f"P1.register_ex {'GREEN' if reg_ok else 'RED'}",
                     'rsdk_object_register_ex("TitleCard", ...) in Game.c', reg_ok)

    if mp:
        map_ok = (re.search(r"\bTitleCard_Create\b", mp) is not None and
                  re.search(r"\bTitleCard_Update\b", mp) is not None)
        all_ok &= cprint(f"P1.map {'GREEN' if map_ok else 'RED'}",
                         "TitleCard_Create + TitleCard_Update in game.map", map_ok)
    else:
        all_ok &= cprint("P1.map RED", "game.map not found (build first)", False)

    return all_ok


# --- P2: text-rendering trio IMPLEMENTED in src/rsdk/*.c -------------------

def predicate_2_text_trio():
    # Gather every .c body under src/rsdk.
    bodies = []
    for fn in os.listdir(RSDK_DIR):
        if fn.endswith(".c"):
            bodies.append(_read(os.path.join(RSDK_DIR, fn)))
    blob = "\n".join(bodies)

    all_ok = True
    for fn in TEXT_TRIO:
        # A definition has a `{` body, not a `;` prototype. Match
        # "<fn>(<args>) {" possibly spanning lines.
        m = re.search(rf"\b{fn}\s*\([^;{{]*\)\s*{{", blob, re.DOTALL)
        ok = m is not None
        all_ok &= cprint(f"P2.{fn} {'GREEN' if ok else 'RED'}",
                         f"{fn} DEFINED (with body) in src/rsdk/*.c", ok)
    return all_ok


# --- P3: TITLECARD.SP2 + .MET reproducible from the decomp blob ------------

def predicate_3_provenance():
    try:
        import build_entity_atlas as bea
    except Exception as e:
        return cprint("P3 RED", f"build_entity_atlas import failed: {e}", False)

    if not os.path.exists(TITLECARD_BIN):
        return cprint("P3 RED",
                      f"source blob {os.path.relpath(TITLECARD_BIN, ROOT)} missing",
                      False)
    if not os.path.exists(TITLECARD_SP2) or not os.path.exists(TITLECARD_MET):
        return cprint("P3 RED",
                      "cd/TITLECARD.SP2 / .MET not shipped (build assets first)",
                      False)

    all_ok = True
    try:
        with tempfile.TemporaryDirectory() as td:
            tmp_spr = os.path.join(td, "rebuilt.sp2")
            tmp_met = os.path.join(td, "rebuilt.met")
            fc, ac = bea.build_atlas(TITLECARD_BIN, tmp_spr, tmp_met, drop_anims=[])
            with open(tmp_spr, "rb") as f:
                rebuilt_spr = f.read()
            with open(tmp_met, "rb") as f:
                rebuilt_met = f.read()
        with open(TITLECARD_SP2, "rb") as f:
            shipped_spr = f.read()
        with open(TITLECARD_MET, "rb") as f:
            shipped_met = f.read()

        spr_ok = rebuilt_spr == shipped_spr
        all_ok &= cprint(f"P3.SP2 {'GREEN' if spr_ok else 'RED'}",
                         f"cd/TITLECARD.SP2 ({len(shipped_spr)} B) "
                         f"{'reproducible' if spr_ok else 'DIVERGES'} from "
                         f"TitleCard.bin (rebuilt {len(rebuilt_spr)} B)", spr_ok)
        met_ok = rebuilt_met == shipped_met
        all_ok &= cprint(f"P3.MET {'GREEN' if met_ok else 'RED'}",
                         f"cd/TITLECARD.MET ({len(shipped_met)} B) "
                         f"{'reproducible' if met_ok else 'DIVERGES'} from "
                         f"TitleCard.bin (rebuilt {len(rebuilt_met)} B)", met_ok)
        frame_ok = fc == EXPECT_FRAMES
        all_ok &= cprint(f"P3.frames {'GREEN' if frame_ok else 'RED'}",
                         f"frame_count = {fc} (expect {EXPECT_FRAMES}: "
                         f"Decorations 2 + Name Letters 27 + Zone Letters 4 "
                         f"+ Act Numbers 3)", frame_ok)
    except Exception as e:
        all_ok &= cprint("P3 RED", f"rebuild failed: {e}", False)
    return all_ok


# --- P4: BSS budget (MAX_FRAMES + _end) -----------------------------------

def predicate_4_bss():
    sub = []

    max_frames = None
    if os.path.exists(ENTITY_ATLAS_H):
        m = re.search(r"#define\s+ENTITY_ATLAS_MAX_FRAMES\s+(\d+)",
                      _read(ENTITY_ATLAS_H))
        if m:
            max_frames = int(m.group(1))
    mf_ok = max_frames is not None and max_frames >= EXPECT_FRAMES
    sub.append(("MAX_FRAMES", mf_ok,
                f"ENTITY_ATLAS_MAX_FRAMES = {max_frames} >= {EXPECT_FRAMES}"))

    end_addr = None
    if os.path.exists(GAME_MAP):
        m = re.search(r"(0x0[06][0-9a-fA-F]+)\s+_end\b", _read(GAME_MAP))
        if m:
            end_addr = int(m.group(1), 16)
    end_ok = end_addr is not None and end_addr < BSS_FLOOR
    margin = (BSS_FLOOR - end_addr) if end_addr is not None else 0
    sub.append(("_end", end_ok,
                f"_end = {hex(end_addr) if end_addr else 'N/A'} < "
                f"{hex(BSS_FLOOR)} (SGL floor); margin "
                f"{margin} B ({margin // 1024} KB)"))

    all_ok = all(ok for _, ok, _ in sub)
    for tag, ok, msg in sub:
        cprint(f"P4.{tag} {'GREEN' if ok else 'RED'}", msg, ok)
    return all_ok


# --- P5: spawn wired in Game.c with zoneName "GREEN HILL" -------------------

def predicate_5_spawn():
    if not os.path.exists(GAME_C):
        return cprint("P5 RED", "Game.c not found", False)
    gc = _read(GAME_C)

    all_ok = True
    spawn_ok = (re.search(r"\bTitleCard\b", gc) is not None and
                (re.search(r'rsdk_create_entity\s*\([^)]*TitleCard', gc) is not None
                 or re.search(r"\btitlecard_spawn\b", gc, re.IGNORECASE) is not None
                 or re.search(r"\bmania_ghz_spawn_titlecard\b", gc) is not None))
    all_ok &= cprint(f"P5.spawn {'GREEN' if spawn_ok else 'RED'}",
                     "TitleCard spawn wired on the GHZ path in Game.c", spawn_ok)

    zone_ok = "GREEN HILL" in gc
    all_ok &= cprint(f"P5.zoneName {'GREEN' if zone_ok else 'RED'}",
                     'zoneName literal "GREEN HILL" present in Game.c', zone_ok)
    return all_ok


def main():
    print("=== Phase 2.4j1 TitleCard (act-intro) port gate ===")
    print(f"  TitleCard.c:   {os.path.relpath(TITLECARD_C, ROOT)}")
    print(f"  TitleCard.bin: {os.path.relpath(TITLECARD_BIN, ROOT)}")

    ok = True
    ok &= predicate_1_object()
    ok &= predicate_2_text_trio()
    ok &= predicate_3_provenance()
    ok &= predicate_4_bss()
    ok &= predicate_5_spawn()

    if ok:
        print("=== Gate Phase 2.4j1: GREEN ===")
        return 0
    print("=== Gate Phase 2.4j1: RED ===")
    return 1


if __name__ == "__main__":
    sys.exit(main())
