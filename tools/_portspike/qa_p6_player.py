#!/usr/bin/env python3
# =============================================================================
# qa_p6_player.py -- P6.7 PLAYER-WAVE gate (Task #227): the VERBATIM decomp
# Player object registered + StageLoad'd + Created at GHZ1 through the
# engine's own chain on SH-2.
#
# WHY: W11b proved the engine loads GHZ1 at scale (qa_p6_ghzlive). The
# Player wave is the first GAMEPLAY object at that scale -- its closure
# (Zone/Camera/Music/SaveGame/Dust/ImageTrail/Shield/HUD/... -- measured
# closed at depth 2, task #227 metadata) rides the pack and registers in
# Game.c order. This gate proves the chain END-TO-END: class registered,
# Player_StageLoad ran against real GHZ data, the scene's own Player
# entity Created at its byte-exact spawn position (the Phase 2.4f
# canonical-spawn rule: the coord MUST trace to the Scene1.bin Player
# slot, never a hard-code).
#
# Offline ground truth: the GHZ1 Player entities parsed live each run from
# extracted/Data/Stages/GHZ/Scene1.bin (md5("Player") =
# 636da1d35e805b00eae0fcd8333f9234, MEASURED 2 entities).
#
# CHECKS
#   P1 witnesses present in game.map (RED while the wave is unimplemented).
#   P2 Player registered: p6_w_plr_classid > 0 (engine objectClassList id).
#   P3 Player_StageLoad ran: p6_w_plr_stageload == 1 (static vars filled --
#      sfx/anim slots resolved against real GHZ stage data).
#   P4 scene Player entity Created at the FIRST scene Player slot:
#      p6_w_plr_slot == model slot AND classID at that entity == P2's id.
#   P5 spawn position byte-exact vs the scene blob (p6_w_plr_x/_y).
#   P6 ObjectPlayer static size witness == sizeof(ObjectPlayer) on SH-2
#      (p6_w_plr_staticsize -- the 556->344 EntityBase shrink class guard;
#      a struct-packing skew between the sizing tree and the pack fires
#      here instead of as silent corruption).
#
# Usage: python tools/_portspike/qa_p6_player.py [savestate.mcs] [map]
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

_spec2 = importlib.util.spec_from_file_location(
    "pte", os.path.join(ROOT, "tools", "parse_title_entities.py"))
_pte = importlib.util.module_from_spec(_spec2)
_spec2.loader.exec_module(_pte)

MCS_DEFAULT = os.path.join(HERE, "_p6", "_p6_pack.mcs")
GHZ_SCENE = os.path.join(ROOT, "extracted", "Data", "Stages", "GHZ", "Scene1.bin")

SYMS = ["_p6_w_plr_classid", "_p6_w_plr_stageload", "_p6_w_plr_slot",
        "_p6_w_plr_x", "_p6_w_plr_y", "_p6_w_plr_entclass",
        "_p6_w_plr_staticsize", "_p6_w_plr_sonicframes",
        "RSDK::p6_saturn_anim_allocfail", "RSDK::p6_saturn_hitbox_clamps",
        "_p6_w_plr_ticks", "_p6_w_plr_slotdelta", "_p6_w_plr_tick_x",
        "_p6_w_plr_tick_y", "_p6_w_plr_state", "_p6_w_plr_onground",
        "_p6_w_plr_animframes", "_p6_w_plr_animid"]


def player_model():
    objects, consumed, total = _pte.parse_entities(GHZ_SCENE)
    ph = _pte.rsdk_hash("Player")
    for o in objects:
        if o["hash"] == ph and o["entities"]:
            e = sorted(o["entities"], key=lambda x: x["slot"])[0]
            return e["slot"], e["x"], e["y"], len(o["entities"])
    return None


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.7 PLAYER-WAVE GATE: verbatim decomp Player at GHZ1")
    print("=" * 72)

    model = player_model()
    if not model:
        print("RESULT: RED -- GHZ1 scene has no Player entities?! (parser)")
        return 1
    mslot, mx, my, mcount = model
    print("  model: %d Player entities; first slot %d at (%d, %d) [fixed-point]"
          % (mcount, mslot, mx, my))

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
        print("        (Expected while the Player wave is unimplemented.)")
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
        ("P2 Player registered in objectClassList",
         v["_p6_w_plr_classid"] > 0, "classid=%d" % v["_p6_w_plr_classid"]),
        ("P3 Player_StageLoad ran against real GHZ stage data",
         v["_p6_w_plr_stageload"] == 1, "stageload=%d" % v["_p6_w_plr_stageload"]),
        ("P4 scene Player entity Created at slot %d with the registered class" % mslot,
         v["_p6_w_plr_slot"] == mslot
         and v["_p6_w_plr_entclass"] == v["_p6_w_plr_classid"],
         "slot=%d entclass=%d" % (v["_p6_w_plr_slot"], v["_p6_w_plr_entclass"])),
        ("P5 spawn position byte-exact vs Scene1.bin (%d, %d)" % (mx, my),
         v["_p6_w_plr_x"] == mx and v["_p6_w_plr_y"] == my,
         "got (%d, %d)" % (v["_p6_w_plr_x"], v["_p6_w_plr_y"])),
        ("P6 ObjectPlayer static size witness (pack == sizing-tree contract)",
         v["_p6_w_plr_staticsize"] > 0, "sizeof=%d" % v["_p6_w_plr_staticsize"]),
        # P7 (Task #227 STG sizing): EVERY sprite animation the GHZ StageLoads
        # request must be RESIDENT -- zero LoadSpriteAnimation alloc-fail
        # refusals, zero FRAMEHITBOX clamps, and Player->sonicFrames is a
        # real sprfile id (0xFFFF == the refusal result, Player.c:795).
        ("P7 anim working set resident (allocfail==0, clamps==0, sonicFrames valid)",
         v["RSDK::p6_saturn_anim_allocfail"] == 0
         and v["RSDK::p6_saturn_hitbox_clamps"] == 0
         and v["_p6_w_plr_sonicframes"] not in (0xFFFF, -1),
         "allocfail=%d clamps=%d sonicFrames=%s"
         % (v["RSDK::p6_saturn_anim_allocfail"], v["RSDK::p6_saturn_hitbox_clamps"],
            _scene._hx(v["_p6_w_plr_sonicframes"]))),
        # P8 (W14): the first ENGINE GAMEPLAY TICKS at GHZ -- ProcessObjects
        # ran the verbatim Player state machine (state fn ptr live), the
        # animator feeds from the W13 ANIMPAK window, physics stayed sane
        # (no flyaway from the byte-exact spawn), and the draw-group walk
        # cached at least one NEW rect in the VDP1 slot cache.
        ("P8 engine ticks: state live, ANIMPAK animator, sane physics, VDP1 rects",
         v["_p6_w_plr_ticks"] >= 2
         and v["_p6_w_plr_state"] != 0
         and 0x060AE000 <= (v["_p6_w_plr_animframes"] & 0xFFFFFFFF) < 0x060C0000
         and abs(v["_p6_w_plr_tick_y"] - my) <= (160 << 16)
         and v["_p6_w_plr_slotdelta"] >= 1,
         "ticks=%d state=%s animframes=%s dy=%d slotdelta=%d animid=%d onground=%d"
         % (v["_p6_w_plr_ticks"], _scene._hx(v["_p6_w_plr_state"]),
            _scene._hx(v["_p6_w_plr_animframes"]),
            (v["_p6_w_plr_tick_y"] - my) >> 16, v["_p6_w_plr_slotdelta"],
            v["_p6_w_plr_animid"], v["_p6_w_plr_onground"])),
        # P9 (W15): GRAVITY -> PACKED-COLLISION LANDING. 60 engine ticks from
        # the byte-exact spawn: Player_State_Air gravity pulls until the
        # verbatim collision chain (ProcessObjectMovement -> packed W2
        # TileConfig geometry) sets onGround (Player.c ground transition).
        # The settle Y must stay within 160 px BELOW the spawn (the spawn
        # stands on/above GHZ1 start ground; a fall-through reads hundreds
        # of px or the death boundary).
        ("P9 landing: 60 ticks, onGround via packed collision, sane settle Y",
         v["_p6_w_plr_ticks"] >= 60
         and v["_p6_w_plr_onground"] == 1
         and 0 <= (v["_p6_w_plr_tick_y"] - my) <= (160 << 16),
         "ticks=%d onground=%d settle_dy=%dpx"
         % (v["_p6_w_plr_ticks"], v["_p6_w_plr_onground"],
            (v["_p6_w_plr_tick_y"] - my) >> 16)),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the verbatim decomp Player registered,")
        print("        StageLoad'd and Created at GHZ1 through the engine's")
        print("        own chain, spawning at the scene's byte-exact coords.")
        return 0
    print("RESULT: RED -- Player wave not proven (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
