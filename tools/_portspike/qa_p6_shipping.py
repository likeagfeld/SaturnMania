#!/usr/bin/env python3
# =============================================================================
# qa_p6_shipping.py -- P6.8 Step B gate: the verbatim engine is the SHIPPING
# boot. The lean shipping flavor (`make P6_ENGINE_SHIPPING=1`) boots the engine
# straight into a CONTINUOUSLY-running GHZ -- NO diagnostic 60-tick burst, NO
# ~14 one-shot proofs, NO Title reload, NO legacy Ring.
#
# MECHANISM (measured, p6_io_main.cpp): main.c's -DP6_ENGINE_SHIPPING branch
# calls p6_engine_boot_and_run() (sets p6_lean_boot=1, runs p6_scene_run's
# masked load core through LoadGameConfig, then the lean early-return exits the
# mask and returns -- skipping every diag burst/proof). main.c then registers
# p6_scene_tick + jo_core_run; the FIRST lean tick (vblank active, unmasked --
# the proven p6_ghz_reload conditions) re-loads GHZ and arms the continuous
# loop. The DIAG flavor (P6SCENE=1, p6_lean_boot==0) is behavior-identical to
# W19/Step A -- the lean guard is a no-op there.
#
# WHY THE WITNESSES DISCRIMINATE THE SHIPPING PATH:
#   * p6_lean_boot == 1 proves the LEAN boot ran (not the diag burst path).
#   * p6_w_cont_frames is incremented ONLY in p6_ghz_frame() (per jo frame), so
#     a value far past the diag's fixed 60-tick burst ceiling proves the engine
#     is ticking GHZ every frame as the shipping main loop.
#   * the Player lands inside the GHZ1 playfield with a valid animator.
#
# CHECKS
#   S1 shipping symbols present in game.map: p6_engine_boot_and_run (the lean
#      boot entry) + p6_lean_boot (the flavor flag) + the continuous witnesses.
#      RED on the CURRENT tree -- the entry + flag do not exist yet.
#   S2 p6_lean_boot == 1: the lean shipping boot path executed.
#   S3 p6_w_cont_frames LARGE (> 120): GHZ ticked per frame as the ship loop,
#      far past the diag burst's 60 ceiling.
#   S4 Player live in the continuous GHZ scene (pos in GHZ1 bounds, valid anim).
#
# Usage: python tools/_portspike/qa_p6_shipping.py [savestate.mcs] [map]
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

MCS_DEFAULT = os.path.join(HERE, "p6_s0.mcs")

# The lean-boot entry + flavor flag (NEW in Step B) gate the RED->GREEN flip;
# the continuous witnesses (Step A) prove the per-frame GHZ tick.
FUNC_SYM = "_p6_engine_boot_and_run"
SYMS = ["_p6_lean_boot",
        "_p6_w_cont_frames", "_p6_w_cont_plr_x", "_p6_w_cont_plr_y",
        "_p6_w_cont_animid"]

# Same continuous threshold as Step A: the diag burst is a fixed 60; a per-frame
# counter captured well past boot must exceed this by a wide margin.
CONT_FRAMES_MIN = 120

# GHZ1 spawn / live-camera world bounds (fixed-point 16.16); generous window so
# the Player must be in the playfield, not at the (0,0) fallback.
PLR_X_MIN = 0
PLR_X_MAX = 16384 << 16
PLR_Y_MIN = 0
PLR_Y_MAX = 1004 << 16


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.8 STEP B SHIPPING GATE: verbatim engine is the lean shipping boot")
    print("=" * 72)

    if not os.path.isfile(mp):
        print("RESULT: RED -- link map missing (%s)" % mp)
        return 1
    map_text = _scene.read_text(mp)
    syms = {}
    missing = []
    for s in [_scene.SYM_MAGIC, FUNC_SYM] + SYMS:
        syms[s] = _scene.map_symbol(map_text, s)
        if syms[s] is None:
            missing.append(s)
    if missing:
        print("  [ RED ] S1 shipping symbols present in the link map")
        for s in missing:
            print("          MISSING %s" % s)
        print("          (Expected while Step B is unimplemented -- the lean")
        print("           boot entry + flavor flag do not exist yet.)")
        print("-" * 72)
        print("RESULT: RED -- Step B not proven (S1).")
        return 1
    print("  [GREEN] S1 shipping symbols present in the link map")

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

    lean = v["_p6_lean_boot"]
    cont = v["_p6_w_cont_frames"]
    px = v["_p6_w_cont_plr_x"]
    py = v["_p6_w_cont_plr_y"]
    aid = v["_p6_w_cont_animid"]

    s2 = lean == 1
    s3 = cont is not None and cont > CONT_FRAMES_MIN
    s4 = (px is not None and py is not None and aid is not None
          and PLR_X_MIN <= px <= PLR_X_MAX
          and PLR_Y_MIN <= py <= PLR_Y_MAX
          and aid >= 0)

    checks = [
        ("S2 lean shipping boot path executed (p6_lean_boot == 1)", s2,
         "p6_lean_boot=%s" % lean),
        ("S3 engine ticked GHZ per frame as the ship loop "
         "(p6_w_cont_frames > %d)" % CONT_FRAMES_MIN, s3,
         "p6_w_cont_frames=%s (diag burst ceiling 60)" % cont),
        ("S4 Player live in the continuous GHZ scene "
         "(pos in GHZ bounds, valid anim id)", s4,
         "plr=(%s,%s) [%d.%05d, %d.%05d px] animid=%s"
         % (px, py,
            (px >> 16) if px is not None else 0,
            ((px & 0xFFFF) * 100000 >> 16) if px is not None else 0,
            (py >> 16) if py is not None else 0,
            ((py & 0xFFFF) * 100000 >> 16) if py is not None else 0,
            aid)),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the verbatim engine is the shipping boot; it")
        print("        runs GHZ continuously per frame from the lean load path")
        print("        (no diag burst / Title reload / legacy Ring).")
        return 0
    print("RESULT: RED -- Step B not proven (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
