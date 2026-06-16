#!/usr/bin/env python3
# =============================================================================
# qa_p6_ghz2.py -- Task #237 GHZ2 LOAD gate (P6_GHZ2_BOOT build).
#
# WHAT IT PROVES
#   When the engine loads GHZ Act 2 (continuously, via the P6_GHZ2_BOOT vehicle
#   that routes through the SAME scene-generic p6_scene_load_and_arm the real
#   signpost transition uses), GHZ2 loads CORRECTLY: its band store mounts, the
#   FG Low + FG High layers serve solid GHZ2 collision at the spawn column (NOT
#   the 0xFFFF "fall-through" empty), ~450 scene entities spawn, and the player
#   spawns at the GHZ2 start. The witnesses are STICKY (latched once at the GHZ2
#   load) so they survive the player's subsequent drop+autorun + transition.
#
# MEASURED (P6_GHZ2_BOOT, 2026-06-16): ghz2_loaded=1, xtile_lo=0x2001 (solid),
#   xtile_hi=0x7413 (platform), entcount=453, spawn=(256,1358)px.
#
# RUN: build with env P6_GHZ2_BOOT=1, capture (any SaveFrame past the GHZ2 load
#   -- the latch is sticky), then:
#       python qa_p6_ghz2.py <state.mcs> game.map
#   RED if the witnesses are absent (not a P6_GHZ2_BOOT build) or GHZ2 did not
#   load cleanly (empty collision / no entities / wrong spawn). This is the
#   GHZ2-LOAD half; the GHZ2 continuous-PLAY half (no transition-away) is #237's
#   next step.
# =============================================================================
import os
import sys
import pathlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as _scene  # noqa: E402

MCS_DEFAULT = os.path.join(HERE, "p6_s0.mcs")

# Offline-known GHZ2 spawn-column ground tiles (p6_io_main.cpp:3110-3114).
GHZ2_FG_LOW_SOLID = 0x2001
GHZ2_FG_HIGH_PLAT = 0x7413
GHZ2_SPAWN_X_PX = 256

SYMS = ["_p6_w_ghz2_loaded", "_p6_w_ghz2_xtile_lo", "_p6_w_ghz2_xtile_hi",
        "_p6_w_ghz2_entcount", "_p6_w_ghz2_plrx", "_p6_w_ghz2_plry"]


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("GHZ2 LOAD (Task #237): collision + entities + spawn at GHZ Act 2 load")
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
    if any(s in missing for s in SYMS):
        print("  [ RED ] M1 GHZ2 latch witnesses present (build with P6_GHZ2_BOOT=1)")
        for s in missing:
            print("          MISSING %s" % s)
        print("-" * 72)
        print("RESULT: RED -- GHZ2 latch instrumentation absent (M1).")
        return 1
    print("  [GREEN] M1 GHZ2 latch witnesses present in the link map")

    if not os.path.isfile(mcs):
        print("RESULT: RED -- savestate missing (%s)" % mcs)
        return 1

    mod = _scene.load_harness()
    sections = mod.parse_savestate(pathlib.Path(mcs))
    raw_magic = mod._peek_bytes(sections, syms[_scene.SYM_MAGIC], 4)
    label, perm = _scene.calibrate(raw_magic)
    if perm is None:
        print("RESULT: RED -- magic mis-decode")
        return 1

    def pk(s):
        return _scene.peek_u32(mod, sections, syms[s], perm, signed=True)

    loaded = pk("_p6_w_ghz2_loaded")
    lo = pk("_p6_w_ghz2_xtile_lo") & 0xFFFF
    hi = pk("_p6_w_ghz2_xtile_hi") & 0xFFFF
    ent = pk("_p6_w_ghz2_entcount")
    px = pk("_p6_w_ghz2_plrx") >> 16
    py = pk("_p6_w_ghz2_plry") >> 16

    print("          loaded=%d  xtile_lo=0x%04X  xtile_hi=0x%04X  entcount=%d  "
          "spawn=(%d,%d)px" % (loaded, lo, hi, ent, px, py))
    print("-" * 72)

    checks = [
        ("M2 GHZ2 load was latched (ghz2_loaded==1)", loaded == 1,
         "loaded=%d" % loaded),
        ("M3 FG Low collision SOLID at spawn (0x%04X, not 0xFFFF empty)"
         % GHZ2_FG_LOW_SOLID, lo == GHZ2_FG_LOW_SOLID, "xtile_lo=0x%04X" % lo),
        ("M4 FG High platform present (0x%04X)" % GHZ2_FG_HIGH_PLAT,
         hi == GHZ2_FG_HIGH_PLAT, "xtile_hi=0x%04X" % hi),
        ("M5 GHZ2 scene entities spawned (entcount > 400)", ent > 400,
         "entcount=%d" % ent),
        ("M6 player at GHZ2 spawn x (%d px)" % GHZ2_SPAWN_X_PX,
         px == GHZ2_SPAWN_X_PX, "spawn x=%d" % px),
    ]

    ok = True
    for name, passed, detail in checks:
        print("  [%s] %s -- %s" % ("GREEN" if passed else " RED ", name, detail))
        ok = ok and passed

    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- GHZ Act 2 LOADS correctly (collision solid, "
              "%d entities, spawn (%d,%d)). PLAY-continuity is #237's next step."
              % (ent, px, py))
        return 0
    print("RESULT: RED -- GHZ2 did not load cleanly (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
