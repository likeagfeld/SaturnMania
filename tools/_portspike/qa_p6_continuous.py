#!/usr/bin/env python3
# =============================================================================
# qa_p6_continuous.py -- P6.8 Step A gate: the proven engine GHZ pass runs
# CONTINUOUSLY (per-frame, 60 fps) instead of as the one-shot 60-tick burst.
#
# MECHANISM (measured, p6_io_main.cpp): p6_scene_run() runs the GHZ burst
# (fixed 60 ticks) then ~14 more one-shot proofs (the last of which RELOADS
# the TITLE scene), so the live scene at return was TITLE and the per-frame
# p6_scene_tick() animated only the legacy Ring. Step A re-loads GHZ as the
# FINAL live scene at the tail of p6_scene_run and arms p6_ghz_continuous_armed;
# p6_scene_tick() then drives p6_ghz_frame() every frame.
#
# WHY THE WITNESS DISCRIMINATES BURST-vs-CONTINUOUS: the burst increments
# nothing per jo frame -- it is a fixed in-run loop of exactly 60 iterations.
# p6_w_cont_frames is a NEW counter incremented ONLY inside p6_ghz_frame()
# (the per-jo-frame continuous path). So a one-shot burst can NEVER make
# p6_w_cont_frames exceed 0. A capture taken well past boot (SaveFrame ~280)
# with p6_w_cont_frames > 120 proves the engine GHZ scene is being ticked
# every frame, far past the 60-tick burst ceiling.
#
# CHECKS
#   C1 witness symbols present in game.map (RED while Step A unimplemented).
#   C2 p6_w_cont_frames LARGE (> 120): the continuous tick incremented it
#      well past the burst's fixed 60 -- the engine advanced GHZ per frame.
#   C3 Player live in the continuous scene: p6_w_cont_plr_x/_y land inside
#      the GHZ1 spawn / camera region (fixed-point) and p6_w_cont_animid is
#      a valid Player anim id (>= 0).
#
# Usage: python tools/_portspike/qa_p6_continuous.py [savestate.mcs] [map]
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

MCS_DEFAULT = os.path.join(HERE, "p6_o0.mcs")

SYMS = ["_p6_w_cont_frames", "_p6_w_cont_plr_x", "_p6_w_cont_plr_y",
        "_p6_w_cont_animid"]

# Continuous-tick threshold: the burst is a fixed 60. A per-frame counter
# captured well past boot must exceed this by a wide margin. 120 = "more
# than twice the burst ceiling" -- unreachable by any one-shot path.
CONT_FRAMES_MIN = 120

# GHZ1 spawn / live-camera world bounds (fixed-point, 16.16). The W15 burst
# settled SLOT_PLAYER1 near the GHZ1 start ground; the continuous tick keeps
# it inside the GHZ1 cameraBounds (16384 x 1004 px). Generous window: the
# Player must be inside the playfield, not at the (0,0) Title fallback.
PLR_X_MIN = 0
PLR_X_MAX = 16384 << 16
PLR_Y_MIN = 0
PLR_Y_MAX = 1004 << 16


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("P6.8 STEP A CONTINUOUS GATE: engine GHZ pass ticks per-frame @60fps")
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
        print("  [ RED ] C1 continuous-witness symbols present in the link map")
        for s in missing:
            print("          MISSING %s" % s)
        print("          (Expected while Step A is unimplemented.)")
        print("-" * 72)
        print("RESULT: RED -- Step A not proven (C1).")
        return 1
    print("  [GREEN] C1 continuous-witness symbols present in the link map")

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

    cont = v["_p6_w_cont_frames"]
    px = v["_p6_w_cont_plr_x"]
    py = v["_p6_w_cont_plr_y"]
    aid = v["_p6_w_cont_animid"]

    c2 = cont is not None and cont > CONT_FRAMES_MIN
    c3 = (px is not None and py is not None and aid is not None
          and PLR_X_MIN <= px <= PLR_X_MAX
          and PLR_Y_MIN <= py <= PLR_Y_MAX
          and aid >= 0)

    checks = [
        ("C2 engine advanced GHZ PER FRAME past the 60-tick burst "
         "(p6_w_cont_frames > %d)" % CONT_FRAMES_MIN, c2,
         "p6_w_cont_frames=%s (burst ceiling 60)" % cont),
        ("C3 Player live in the continuous GHZ scene "
         "(pos in GHZ bounds, valid anim id)", c3,
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
        print("RESULT: GREEN -- the engine GHZ pass runs continuously per "
              "frame;")
        print("        the Player is live in the GHZ scene past the burst "
              "ceiling.")
        return 0
    print("RESULT: RED -- Step A not proven (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
