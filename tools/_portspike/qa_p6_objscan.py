#!/usr/bin/env python3
# =============================================================================
# qa_p6_objscan.py -- Task #245 Lever 2 (loop3) don't-regress gate.
#
# WHAT IT PROVES
#   The engine ProcessObjects lateUpdate pass (loop3) no longer does a full
#   ENTITY_COUNT (~1216) RSDK_ENTITY_AT scan. It is driven off the in-range
#   lists instead: lateUpdate over THIS frame's s_p6_inrange (~12), and the
#   onScreen clear over LAST frame's in-range (s_p6_inrange_prev) -- which is
#   parity-exact with the stock clear-all because onScreen is only ever set
#   (during Draw) for entities that were in a draw group == in range.
#
# MEASURED (in-motion, 2026-06-15):
#   RED  (full-scan loop3, commit 2096806): p6_w_objsec_loop3 = 4.63 ms.
#   GREEN(list-driven loop3)              : p6_w_objsec_loop3 < 1.0 ms.
#
# THE INVARIANT (don't-regress): loop3 stays under 1 ms (the full-scan was
# 4.63 ms; the list walk of ~12 + ~12 + a copy is ~0.1 ms). Behavioural parity
# is the full diag sweep (player/continuous/entdraw) -- onScreen + lateUpdate
# drive the render path, so a broken clear/dispatch shows there.
#
# RED-first: RED on the full-scan build (loop3 >= 1 ms), GREEN on the
# list-driven build. Run against the shipping engine game.map (standalone,
# like qa_p6_perf / qa_p6_residentsheets -- NOT verify_done, which builds the
# hand-port flavor).
#
# Usage: python qa_p6_objscan.py [state.mcs] [game.map]
# =============================================================================
import os
import sys
import pathlib

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as _scene  # noqa: E402

MCS_DEFAULT = os.path.join(HERE, "p6_s0.mcs")
LOOP3_MS_MAX = 1.0

SYMS = ["_p6_w_objsec_loop3", "_p6_w_perf_cks", "_p6_w_cont_frames"]


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("OBJ SCAN (Task #245 Lever 2): ProcessObjects loop3 off the in-range list")
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
    if "_p6_w_objsec_loop3" in missing:
        print("  [ RED ] M1 loop3 witness present in the link map")
        print("          MISSING _p6_w_objsec_loop3 (build without P6_PERF_OBJPROF)")
        print("-" * 72)
        print("RESULT: RED -- loop3 instrumentation absent (M1).")
        return 1
    print("  [GREEN] M1 loop3 witness present in the link map")

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

    l3 = _scene.peek_u32(mod, sections, syms["_p6_w_objsec_loop3"], perm, signed=True)
    cks = _scene.peek_u32(mod, sections, syms["_p6_w_perf_cks"], perm, signed=True)
    frames = _scene.peek_u32(mod, sections, syms["_p6_w_cont_frames"], perm, signed=True)
    # us/tick = (8 << (2*cks)) / clock_MHz ; NTSC 320-mode = 26.8 MHz
    us_tick = (8 << (2 * (cks if cks in (0, 1, 2, 3) else 1))) / 26.8
    l3_ms = l3 * us_tick / 1000.0

    print("          cont_frames=%d  loop3=%d ticks (cks=%d) -> %.2f ms"
          % (frames, l3, cks, l3_ms))
    print("-" * 72)

    checks = []
    checks.append(("M2 capture in continuous GHZ (cont_frames>0)",
                   frames > 0, "cont_frames=%d" % frames))
    checks.append(("M3 loop3 list-driven (< %.1f ms; full-scan was 4.63 ms)"
                   % LOOP3_MS_MAX, 0 < l3_ms < LOOP3_MS_MAX, "loop3=%.2f ms" % l3_ms))

    ok = True
    for name, passed, detail in checks:
        print("  [%s] %s -- %s" % ("GREEN" if passed else " RED ", name, detail))
        ok = ok and passed

    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- loop3 no longer scans the full entity table "
              "(~4.5 ms/frame returned).")
        return 0
    print("RESULT: RED -- loop3 still doing a full ENTITY_COUNT scan.")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
