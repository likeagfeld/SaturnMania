#!/usr/bin/env python3
# =============================================================================
# qa_p6_slave.py -- Dual-SH2 phase STEP 1 (#246/#243): slave-CPU LIVENESS proof.
#
# Before moving any real per-frame work (the FG present) to the slave SH-2, we
# must PROVE the slave actually runs in THIS jo+SGL+pack build and that the
# master<->slave coherency handoff works. jo gives a fork-join for free
# (jo_core_add_slave_callback -> core.c:615 kicks slSlaveFunc, :626 runs the
# master callback p6_ghz_frame in parallel, :628 joins via jo_core_wait_for_slave
# which slCashPurge()es the master cache). We register a TRIVIAL slave callback
# (p6_slave_probe) that increments a witness p6_w_slave_ticks via the CACHE-
# THROUGH alias (addr | 0x20000000) so the write reaches WRAM immediately (the
# slave's cached write would otherwise be invisible to the savestate + master).
#
# RED  (current build): _p6_w_slave_ticks absent from the map -> not instrumented.
# GREEN (slave live)   : ticks > 0 (the slave executed the callback every frame
#                        AND the cache-through write reached WRAM) AND
#                        cont_frames > 0 (the jo fork-join did NOT hang the loop --
#                        a dead/stuck slave would freeze at jo_core_wait_for_slave).
#
# This de-risks slSlaveFunc + coherency before the FG-present offload. If RED with
# cont_frames==0, the slave dispatch HUNG (SGL slave not started) -> investigate
# slInitSystem slave bring-up, NOT a witness bug.
#
# Usage: python tools/_portspike/qa_p6_slave.py [savestate.mcs] [map]
# =============================================================================
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location(
    "qa_p6_scene", os.path.join(HERE, "qa_p6_scene.py"))
_scene = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_scene)

MCS_DEFAULT = os.path.join(HERE, "p6_perf.mcs")

SYMS = ["_p6_w_slave_ticks", "_p6_w_cont_frames"]


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("DUAL-SH2 STEP 1: slave-CPU liveness + coherency proof")
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
        print("  [ RED ] slave-witness symbols present in the link map")
        for s in missing:
            print("          MISSING %s" % s)
        print("          (Expected before the slave callback is wired.)")
        print("-" * 72)
        print("RESULT: RED -- slave instrumentation not present.")
        return 1
    print("  [GREEN] slave-witness symbols present in the link map")

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
    ticks = _scene.peek_u32(mod, sections, syms["_p6_w_slave_ticks"], perm, signed=True)
    frames = _scene.peek_u32(mod, sections, syms["_p6_w_cont_frames"], perm, signed=True)

    live = ticks is not None and ticks > 0
    nohang = frames is not None and frames > 0
    checks = [
        ("S1 slave executed the callback (ticks > 0, coherency OK)", live,
         "slave_ticks=%s" % ticks),
        ("S2 fork-join did NOT hang the loop (cont_frames > 0)", nohang,
         "cont_frames=%s" % frames),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        ratio = (float(ticks) / frames) if frames else 0.0
        print("  slave ran ~%.2f ticks/continuous-frame (1.0 == one slave call per"
              " master frame)." % ratio)
        print("RESULT: GREEN -- slave SH-2 is live + coherent; safe to offload the")
        print("        FG present (STEP A) onto it.")
        return 0
    if frames is not None and frames == 0:
        print("  NOTE: cont_frames==0 with the slave wired -> the fork-join likely")
        print("        HUNG (SGL slave not started). Investigate slave bring-up.")
    print("RESULT: RED -- slave not proven live (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
