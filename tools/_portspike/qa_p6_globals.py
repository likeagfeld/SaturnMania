#!/usr/bin/env python3
# =============================================================================
# qa_p6_globals.py -- P6.7 wave-1 gate (Task #210): REAL GAME GLOBALS + THE
# GameAPI LINK LAYER. The first wave of verbatim Global TUs (Localization,
# LogHelpers, Options -- SonicMania_Game.c registration order :427/:429/:517)
# compiles against the REAL Game.h/GameLink.h surface and registers through
# the engine's own function table, with GlobalVariables living at the fixed
# WRAM-H window the P6.7d.2 SGL re-contract freed.
#
# Layout facts (gen_globals_map.py, self-tests S1-S6 GREEN 2026-06-11):
#   - verbatim sizeof(GlobalVariables) pre-Plus = 268,148 B; the
#     SATURN_GLOBALS_RETARGET arms shrink it to 56,180 B.
#   - BOTH sides build RETRO_REVISION=2 (build_p6scene_objs.sh CORE_DEFS
#     beats RetroEngine.hpp:227's #ifndef default; the game TUs use the
#     same census knob): LoadGameConfig's REV02 seed loop
#     (RetroEngine.cpp:1190-1197) writes the GameConfig var seeds through
#     globalVarsPtr[offset+v], with PC-layout offsets remapped to the
#     shrunk layout by the RETRO_SATURN arm (generated
#     SaturnGlobalsMap.inc). The seam memsets the window first
#     (AllocateStorage clearMemory=true mirror). S6 proves the seeds'
#     nonzero payload == the v5U InitCB writes, so the two seeding
#     mechanisms agree for 1.03.
#
# CHECKS
#   G1 (offline) function-table ABI: GameLink.h RSDKFunctionTable member
#      list (game config: REV02=1, REV0U=0, Plus=0 -- REVISION 2) is
#      slot-for-slot compatible with what the engine BINDS at each
#      FunctionTable_ enum index (Link.hpp:94-324 order, Link.cpp
#      ADD_RSDK_FUNCTION bindings) under the SAME config -- the memcpy fill
#      in p6_wave1_reg.c is sound iff this holds (the W9 class, applied to
#      the dispatch table). Measured layers (2026-06-11, byte-identical to
#      upstream master on both sides):
#      (a) most slots: game member == engine enum name or bound-function
#          name (upstream renamed internals on both axes);
#      (b) NAME_VARIANTS: verified same-slot rename pairs (e.g. game
#          DrawText == engine DrawString slot);
#      (c) SWAPPED_SLOTS: the v5U original-collision 8 (Find*Position vs
#          *Collision) are ORDER-SWAPPED between upstream GameLink.h and
#          the upstream engine enum under REV0U. At REVISION=2 both blocks
#          compile OUT (vacuous), but the no-caller scan still runs every
#          gate pass so any future REV0U flip or new caller fires RED.
#   G2 witnesses present in game.map (RED while unimplemented).
#   G3 RegisterGlobalVariables seam saw the game's sizeof: _p6_w_glb_size ==
#      56,180 (proves the wave TUs compiled with the retarget arms).
#   G4 _p6_w_glb_ptr == P6_GLOBALS_WINDOW (0x060C8000).
#   G5 globals->saveSlotID == 255 (NO_SAVE_SLOT) peeked at its SATURN-layout
#      address inside the window -- proves the REV02 seed loop wrote through
#      the RETRO_SATURN offset-remap arm.
#   G6 globals->presenceID == -1 (the other nonzero seed).
#   G7 globals->gameMode == 0 (the seam's AllocateStorage-mirror memset
#      reached offset 0).
#   G8 Localization ran its verbatim StageLoad chain: _p6_w_w1_locale ==
#      (loaded << 8) | language == 0x0100 (loaded=true, LANGUAGE_EN=0 from
#      SKU::curSKU via the pre-Plus sku_language compat arm).
#   G9 classCount == 6 (DefaultObject, DevOutput, overlay Ring,
#      Localization, LogHelpers, Options).
#
# Usage: python tools/_portspike/qa_p6_globals.py [savestate.mcs] [map]
# =============================================================================
import importlib.util
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

MCS_DEFAULT = os.path.join(HERE, "_p6", "_p6_pack.mcs")
MODEL = os.path.join(HERE, "_p6", "p6_globals_model.json")
GAMELINK = os.path.join(ROOT, "tools", "_decomp_raw", "SonicMania_GameLink.h")
LINKHPP = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Core",
                       "Link.hpp")
LINKCPP = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Core",
                       "Link.cpp")

P6_GLOBALS_WINDOW = 0x060C8000
SAT_SIZEOF_EXPECT = 56180
EXP_CLASSCOUNT = 26  # DefaultObject, DevOutput, overlay Ring + the 23
#                      Player-wave registrations (p6_wave1_reg.c, Game.c line
#                      order); the step-B dual-stride pool (ENTITY_WIDE_SIZE
#                      556) admits Player/GameOver/ImageTrail (Task #227).
EXP_LOCALE = 0x0100  # loaded=1, language=LANGUAGE_EN(0)

SYMS = ["_p6_w_glb_size", "_p6_w_glb_ptr", "_p6_w_w1_locale",
        "_p6_w_obj_classcount"]

# Active macro config for BOTH files: RETRO_REVISION=2 everywhere (engine:
# build_p6scene_objs.sh CORE_DEFS -DRETRO_REVISION=2 beats the #ifndef
# default; game TUs: the same census knob + -DGAME_VERSION=3 -> pre-Plus);
# mod loader OFF on both sides.
CONFIG = {"RETRO_REV02": True, "RETRO_REV0U": False, "MANIA_USE_PLUS": False,
          "RETRO_USE_MOD_LOADER": False, "RETRO_VER_EGS": False,
          "MANIA_USE_EGS": False}


def eval_cond(expr):
    e = expr.strip()
    neg = False
    if e.startswith("!"):
        neg, e = True, e[1:].strip()
    if e not in CONFIG:
        raise SystemExit("G1 parser: unhandled condition %r" % expr)
    v = CONFIG[e]
    return (not v) if neg else v


def active_lines(text, start_marker, end_marker):
    """Yield preprocessor-active lines between two markers (exclusive)."""
    body = text.split(start_marker, 1)[1].split(end_marker, 1)[0]
    stack = []
    for line in body.splitlines():
        s = line.strip()
        if s.startswith("#if "):
            stack.append([eval_cond(s[4:]), False])
        elif s.startswith("#ifdef") or s.startswith("#ifndef"):
            raise SystemExit("G1 parser: unhandled %r" % s)
        elif s.startswith("#else"):
            stack[-1][0] = not (stack[-1][0] or stack[-1][1])
            stack[-1][1] = True
        elif s.startswith("#endif"):
            stack.pop()
        elif all(f[0] for f in stack):
            yield line


def game_table_members():
    text = open(GAMELINK, encoding="latin1").read()
    # struct body: the "// Function Table" typedef ending at the named tag
    lines = list(active_lines(text, "// Function Table\ntypedef struct {",
                              "} RSDKFunctionTable;"))
    members = []
    # One member per ';'-terminated declaration; the OUTER member name is the
    # FIRST (*name) in the declaration (later matches are function-pointer
    # PARAMETERS like RegisterGlobalVariables' initCB).
    for decl in "\n".join(lines).split(";"):
        m = re.search(r"\(\s*\*\s*(\w+)\s*\)\s*\(", decl)
        if m:
            members.append(m.group(1))
    return members


def engine_enum_entries():
    text = open(LINKHPP, encoding="latin1").read()
    lines = list(active_lines(text, "enum FunctionTableIDs {",
                              "FunctionTable_Count,"))
    entries = []
    for line in lines:
        m = re.match(r"\s*FunctionTable_(\w+)\s*,", line)
        if m:
            entries.append(m.group(1))
    return entries


def engine_bindings():
    """enum-name -> bound engine function name, from Link.cpp's
    ADD_RSDK_FUNCTION(FunctionTable_X, Func) calls (array layout is enum
    order regardless of ADD call order)."""
    text = open(LINKCPP, encoding="latin1").read()
    return dict(re.findall(
        r"ADD_RSDK_FUNCTION\(FunctionTable_(\w+),\s*([\w:]+)\)", text))


# (game member name, engine bound-function name): upstream renamed the
# engine internals; same slot semantics, verified pairwise against the
# Link.cpp bindings + GameLink.h signatures 2026-06-11.
NAME_VARIANTS = {
    ("DrawText", "DrawString"),
    ("DrawAniTiles", "DrawAniTile"),
    ("DrawDynamicAniTiles", "DrawDynamicAniTile"),
    ("AddModelTo3DScene", "AddModelToScene"),
    ("AddMeshFrameTo3DScene", "AddMeshFrameToScene"),
    ("CheckObjectCollisionTouchBox", "CheckObjectCollisionTouch"),
    ("CheckObjectCollisionTouchCircle", "CheckObjectCollisionCircle"),
    ("IsSfxPlaying", "SfxPlaying"),
    ("AddVarEnumValue", "AddEnumVariable"),
    ("ClearViewableVariables", "ClearDebugValues"),
}

# The v5U original-collision 8: ORDER-SWAPPED between upstream GameLink.h
# (Find* first) and the upstream engine enum (*Collision first) -- verified
# byte-identical to upstream master on BOTH sides 2026-06-11. Latent
# upstream: NO Mania TU calls any of them. SWAP_SCAN below re-measures
# that on every gate run.
SWAPPED_SLOTS = {
    ("FindFloorPosition", "FloorCollision"),
    ("FindLWallPosition", "LWallCollision"),
    ("FindRoofPosition", "RoofCollision"),
    ("FindRWallPosition", "RWallCollision"),
    ("FloorCollision", "FindFloorPosition"),
    ("LWallCollision", "FindLWallPosition"),
    ("RoofCollision", "FindRoofPosition"),
    ("RWallCollision", "FindRWallPosition"),
}
SWAP_SCAN_NAMES = sorted({a for a, _ in SWAPPED_SLOTS})


def scan_swapped_callers():
    """Return decomp TUs that call any swapped slot via RSDK. -- must stay
    empty for the memcpy fill to remain sound."""
    pat = re.compile(r"RSDK\.(%s)\b" % "|".join(SWAP_SCAN_NAMES))
    hits = []
    raw = os.path.join(ROOT, "tools", "_decomp_raw")
    for fn in os.listdir(raw):
        if fn.endswith(".c"):
            if pat.search(open(os.path.join(raw, fn),
                               encoding="latin1").read()):
                hits.append(fn)
    return hits


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.7 WAVE-1 GATE: real game globals + GameAPI link layer")
    print("=" * 72)

    model = json.load(open(MODEL))
    by_name = {s["field"]: s for s in model["seeds"]}
    save_off = by_name["saveSlotID"]["sat_off"] * 4
    pres_off = by_name["presenceID"]["sat_off"] * 4
    print("  model: sat sizeof %d; saveSlotID @win+0x%X, presenceID @win+0x%X"
          % (model["sat_sizeof"], save_off, pres_off))
    if model["sat_sizeof"] != SAT_SIZEOF_EXPECT:
        print("RESULT: RED -- model sizeof %d != %d (regenerate "
              "gen_globals_map.py)" % (model["sat_sizeof"],
                                       SAT_SIZEOF_EXPECT))
        return 1

    # ---- G1: offline dispatch-table ABI equivalence -------------------------
    game = game_table_members()
    eng = engine_enum_entries()
    bind = engine_bindings()
    problems = []
    if len(game) != len(eng):
        problems.append("length mismatch: game %d vs engine %d"
                        % (len(game), len(eng)))
    swapped_seen = 0
    for i, (gm, en) in enumerate(zip(game, eng)):
        bound = bind.get(en, en)
        # Slot identity holds when the game member names the ENUM slot or
        # the engine function bound there (upstream renamed internals on
        # both axes), or is a verified variant pair.
        if gm == en or gm == bound or (gm, bound) in NAME_VARIANTS:
            continue
        if (gm, bound) in SWAPPED_SLOTS:
            swapped_seen += 1
            continue
        problems.append("slot %d: game=%s vs engine-bound=%s (enum %s)"
                        % (i, gm, bound, en))
    callers = scan_swapped_callers()
    if callers:
        problems.append("swapped collision slots now have CALLERS: %s"
                        % ", ".join(callers))
    g1 = not problems
    print("  [%s] G1 table ABI: %d slots; %d name-variants whitelisted; "
          "%d swapped-uncalled (0 at REV02)"
          % ("GREEN" if g1 else " RED ", len(eng),
             len(NAME_VARIANTS), swapped_seen))
    if not g1:
        for p in problems:
            print("        %s" % p)
        print("RESULT: RED -- memcpy table fill is unsound; fix before "
              "any link-layer build.")
        return 1

    # ---- G2: witnesses in the map -------------------------------------------
    if not os.path.isfile(mp):
        print("RESULT: RED -- link map missing (%s)" % mp)
        return 1
    map_text = _scene.read_text(mp)
    syms = {}
    missing = []
    for s in [_scene.SYM_MAGIC] + SYMS:
        syms[s] = _scene.map_symbol(map_text, s)
        if syms[s] is None:
            missing.append(s)
    if missing:
        print("RESULT: RED -- witness symbol(s) absent from the map:")
        for s in missing:
            print("        %s" % s)
        print("        (Expected while wave-1 is unimplemented.)")
        return 1
    if not os.path.isfile(mcs):
        print("RESULT: RED -- savestate missing (%s)" % mcs)
        return 1

    import pathlib
    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))
    raw_magic = mod._peek_bytes(sections, syms[_scene.SYM_MAGIC], 4)
    label, perm = _scene.calibrate(raw_magic)
    if perm is None:
        print("RESULT: RED -- magic mis-decode")
        return 1
    v = {s: _scene.peek_u32(mod, sections, syms[s], perm, signed=False)
         for s in SYMS}
    save_v = _scene.peek_u32(mod, sections, P6_GLOBALS_WINDOW + save_off,
                             perm, signed=False)
    pres_v = _scene.peek_u32(mod, sections, P6_GLOBALS_WINDOW + pres_off,
                             perm, signed=False)
    mode_v = _scene.peek_u32(mod, sections, P6_GLOBALS_WINDOW, perm,
                             signed=False)

    checks = [
        ("G3 seam saw retargeted sizeof (%d)" % SAT_SIZEOF_EXPECT,
         v["_p6_w_glb_size"] == SAT_SIZEOF_EXPECT,
         "got %d" % v["_p6_w_glb_size"]),
        ("G4 globals at the fixed window 0x%08X" % P6_GLOBALS_WINDOW,
         v["_p6_w_glb_ptr"] == P6_GLOBALS_WINDOW,
         "got 0x%08X" % v["_p6_w_glb_ptr"]),
        ("G5 remapped seed wrote saveSlotID = 255 (NO_SAVE_SLOT) at "
         "win+0x%X" % save_off, save_v == 255, "got %d" % save_v),
        ("G6 remapped seed wrote presenceID = -1 at win+0x%X" % pres_off,
         pres_v == 0xFFFFFFFF, "got 0x%08X" % pres_v),
        ("G7 seam memset (AllocateStorage mirror) reached offset 0 "
         "(gameMode == 0)", mode_v == 0, "got 0x%08X" % mode_v),
        ("G8 Localization StageLoad chain: (loaded<<8)|language == 0x%04X"
         % EXP_LOCALE, v["_p6_w_w1_locale"] == EXP_LOCALE,
         "got 0x%04X" % v["_p6_w_w1_locale"]),
        ("G9 classCount == %d" % EXP_CLASSCOUNT,
         v["_p6_w_obj_classcount"] == EXP_CLASSCOUNT,
         "got %d" % v["_p6_w_obj_classcount"]),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the verbatim Localization/LogHelpers/Options")
        print("        TUs run against the real GameLink surface; globals")
        print("        live at the fixed window; the engine drove the")
        print("        verbatim InitCB; the dispatch-table memcpy ABI holds.")
        return 0
    print("RESULT: RED -- wave-1 not proven (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
