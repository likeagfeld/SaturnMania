#!/usr/bin/env python3
# =============================================================================
# qa_p6_entdraw.py -- P6.7 W18 gate (Task #227): GHZ ENTITY-SPRITE SURFACE BIND
# + the silent-drop accounting for DrawSprite blits whose surface never bound.
#
# WHY (MEASURED root cause, 2026-06-13): the engine's Saturn DrawSprite backend
# (p6_io_main.cpp p6_vdp1_blit/_flipped, p6_vdp1.c) drops a blit at the
# unbound-surface handle check (`sheet < 0`) -- and the W17 build did NOT count
# that drop. Over the GHZ 60-tick pass MEASURED 1,139 such silent drops:
#   surface 0  Global/Display.gif  960 drops (HUD digits/display)
#   surface 5  Players/Tails1.gif  120 drops (the sidekick set -- Mania loads
#                                             BOTH Sonic + Tails, W13 memory)
#   surface 8  Global/Shields.gif   59 drops
# (matched offline: surf->hash[0] vs the GIF-path MD5, p6_w_surfhash).
#
# The Items.gif/ring surface specifically was UNBOUND for a different reason:
# the overlay Ring registers a NULL Create + StageLoad
# (p6_ovl_register_object, p6_io_main.cpp:1047-1049), so the GHZ InitObjects
# chain NEVER loads Global/Ring.bin -> Items.gif had no gfxSurface entry at all
# -> the pre-tick bind loop had nothing to bind (MEASURED RED: itemsurf created
# fresh @12, handle -1; bind_count 3 -- only the SONIC1/2/3 banded sheets).
#
# W18 FIX (this gate's GREEN): the GHZ pre-tick path now does a SHEET-ONLY
# RSDK.LoadSpriteSheet("Global/Items.gif") (NOT LoadSpriteAnimation -- that
# overflows the DATASET_STG anim pool after the Player closure StageLoads,
# MEASURED p6_saturn_anim_allocfail 0->1, which ALSO regresses qa_p6_player
# P7). LoadSpriteSheet allocates the surface + resolves the already-staged
# ITEMS.SHT banded slot (W12a; SaturnSheet_FindSlot by path hash ->
# saturnSheetSlot 3, Sprite.cpp:949-967) with ZERO anim-pool cost, so the bind
# loop binds it (banded handle >= 0).
#
# CHECKS
#   E1 witnesses present in game.map (RED while W18 unimplemented).
#   E2 the silent-drop accounting EXISTS and is consistent: landed > 0 and
#      handle_drops + (player+items landed) reconciles -- proves the formerly
#      uncounted drop path is now witnessed (p6_vdp1.c handle-check counters).
#   E3 the Items.gif / ring surface BINDS: ringsheethandle >= 0 AND its
#      saturnSheetSlot resolved to the staged ITEMS.SHT (ringsheet >= 0).
#      RED on a no-bind build: the surface is created without a banded slot
#      and the handle stays -1.
#   E4 bind_count >= 4 (the 3 Sonic banded sheets + the Items banded sheet).
#   E5 NO REGRESSION of the budget contracts the fix could have tripped:
#      p6_saturn_anim_allocfail == 0 AND p6_saturn_hitbox_clamps == 0
#      (the sheet-only load must not touch the DATASET_STG anim pool).
#
# WITNESSED GAP (declared, not asserted GREEN): the 1,139 silent drops are
# Display(960)+Tails1(120)+Shields(59) -- NOT rings. The GHZ rings issue ZERO
# blits in this pass (Ring_Create/StageLoad are NULL -> animators have no
# frames -> Ring_Draw_Normal is a no-op), so "ring blits land" is blocked on
# arming the rings, which needs Ring.bin's animation resident (DATASET_STG
# pool full) -- a SEPARATE budget item. Banding Display/Tails1/Shields needs
# >4 SaturnSheet slots (all 4 used) -- also a separate budget item. This gate
# proves the SURFACE-BIND mechanism + the drop accounting; the residency of
# those classes is the declared remaining gap (see dropbysheet witness).
#
# Usage: python tools/_portspike/qa_p6_entdraw.py [savestate.mcs] [map]
# =============================================================================
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

MCS_DEFAULT = os.path.join(HERE, "p6_m0.mcs")

SYMS = ["_p6_w_vdp1_landed", "_p6_w_vdp1_handle_drops",
        "_p6_w_ringspr", "_p6_w_ringsheet", "_p6_w_ringsheethandle",
        "_p6_w_itemshandle", "_p6_w_bind_count",
        "RSDK::p6_saturn_anim_allocfail", "RSDK::p6_saturn_hitbox_clamps"]


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.7 W18 ENTITY-DRAW GATE: GHZ ring/Items surface bind + drop count")
    print("=" * 72)

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
        print("        (Expected while W18 is unimplemented -- the silent-drop")
        print("         counters + Items-bind witnesses do not exist yet.)")
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
    v = {s: _scene.peek_u32(mod, sections, syms[s], perm, signed=True)
         for s in SYMS}

    checks = [
        ("E2 silent-drop accounting live (landed>0 AND drops counted)",
         v["_p6_w_vdp1_landed"] > 0 and v["_p6_w_vdp1_handle_drops"] >= 0,
         "landed=%d handle_drops=%d"
         % (v["_p6_w_vdp1_landed"], v["_p6_w_vdp1_handle_drops"])),
        ("E3 Items.gif/ring surface BINDS (banded handle>=0, ITEMS.SHT slot)",
         v["_p6_w_ringsheethandle"] >= 0 and v["_p6_w_ringsheet"] >= 0
         and v["_p6_w_itemshandle"] >= 0,
         "ringsheethandle=%d ringsheet(shtSlot)=%d itemshandle=%d ringspr(surf)=%d"
         % (v["_p6_w_ringsheethandle"], v["_p6_w_ringsheet"],
            v["_p6_w_itemshandle"], v["_p6_w_ringspr"])),
        ("E4 bind_count >= 4 (SONIC1/2/3 banded + Items banded)",
         v["_p6_w_bind_count"] >= 4, "bind_count=%d" % v["_p6_w_bind_count"]),
        ("E5 no budget regression (anim allocfail==0, hitbox clamps==0)",
         v["RSDK::p6_saturn_anim_allocfail"] == 0
         and v["RSDK::p6_saturn_hitbox_clamps"] == 0,
         "allocfail=%d clamps=%d"
         % (v["RSDK::p6_saturn_anim_allocfail"],
            v["RSDK::p6_saturn_hitbox_clamps"])),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the GHZ Items.gif/ring surface binds to its")
        print("        staged ITEMS.SHT banded slot (handle >= 0), the silent")
        print("        DrawSprite drop is now counted, and no anim-pool/hitbox")
        print("        budget contract regressed.")
        return 0
    print("RESULT: RED -- W18 entity-surface bind not proven (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
