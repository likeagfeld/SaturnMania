#!/usr/bin/env python3
# =============================================================================
# qa_p6_scansplit.py -- Dual-SH2 PARTIAL SCAN-SPLIT gate (#246/#243, locked-60).
#
# The 30->48.92fps work proved GHZ is MASTER-COMPUTE-bound (VDP1 idle), and the
# one remaining lever to a locked 60 is the loop1 inRange CLASSIFY scan (~5.8ms,
# IRREDUCIBLE by caching on this HW -- both WRAM banks full, cart slower). The
# scan-split runs HALF of it on the SLAVE SH-2: at ProcessObjects frame-start
# (after the camera update, before loop1) the master forks the slave to classify
# the upper scene region [P6_SCAN_MID, TEMPENTITY_START) into each entity's
# inRange (cache-through), while the master classifies+updates [0, P6_SCAN_MID)
# in parallel and JOINs (slCashPurge) before it reaches P6_SCAN_MID; loop1 then
# trusts the slave's inRange for the upper region (skips the bounds switch).
#
# PARITY: proven 0-divergence for GHZ1 from the decomp (no registered object
# repositions a foreign scene-region entity; the camera is fixed for the frame)
# -- memory ghz-scan-split-parity-audit. This gate proves the split RAN and did
# not hang; the qa_p6_* diag sweep is the behavioural parity RED gate, and
# qa_p6_perf.py M7/M8 is the compute-full delta (the fps payoff).
#
# RED  (current build / -e P6_SPLIT unset): _p6_w_scan_slave_n absent from the
#       map -> the split is not compiled in.
# GREEN (-e P6_SPLIT=1, GHZ settled): slave_n == TEMPENTITY_START - P6_SCAN_MID
#       (the slave classified the whole upper region) AND slave_pop > 0 (it saw
#       populated entities) AND cont_frames large (the second mid-frame fork-join
#       did NOT hang -- a stuck slave freezes at jo_core_wait_for_slave -> 0).
#
# Usage: python tools/_portspike/qa_p6_scansplit.py [savestate.mcs] [map]
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

# Must match Object.hpp + the P6_SCAN_MID default in Object.cpp.
RESERVE_ENTITY_COUNT = 0x40
TEMPENTITY_START = 0x480          # ENTITY_COUNT(0x4C0) - TEMPENTITY_COUNT(0x40)
P6_SCAN_MID = 608                 # default split point (scene region [64,1152))
EXPECT_SLAVE_N = TEMPENTITY_START - P6_SCAN_MID  # slots the slave classifies

# #228 layout ceiling: _end MUST stay below the fixed ANIMPAK load address
# (Animation.hpp P6_HW_ANIMPAK). The split adds .text/.bss; if _end overruns this,
# the GHZ anim-pack load corrupts .bss -> a GFS-callback fault during load (master
# PC traps 0x06000956, cont_frames=0). This is the same boot trap as
# qa_p6_ghz_regression R0; assert it HERE too so the scan-split gate catches a
# layout overflow from the MAP, before a capture is ever spent.
ANIMPAK_CEILING = 0x060B6600

SYMS = ["_p6_w_scan_slave_n", "_p6_w_scan_slave_pop", "_p6_w_cont_frames"]


def _end_addr(map_text):
    import re
    m = re.search(r"0x0*([0-9a-fA-F]+)\s+_end\s*=\s*\.", map_text)
    return int(m.group(1), 16) if m else None


def main(argv):
    mcs = _scene._as_path(argv[1]) if len(argv) > 1 else MCS_DEFAULT
    mp = _scene._as_path(argv[2]) if len(argv) > 2 else _scene.MAP_DEFAULT

    print("=" * 72)
    print("DUAL-SH2 SCAN-SPLIT: slave classifies upper scene region [%d, %d)"
          % (P6_SCAN_MID, TEMPENTITY_START))
    print("=" * 72)

    if not os.path.isfile(mp):
        print("RESULT: RED -- link map missing (%s)" % mp)
        return 1
    map_text = _scene.read_text(mp)

    end = _end_addr(map_text)
    if end is None:
        print("  [ RED ] S0 _end present in link map")
        print("RESULT: RED -- cannot read _end.")
        return 1
    if end >= ANIMPAK_CEILING:
        print("  [ RED ] S0 _end < ANIMPAK ceiling 0x%06X (#228 boot trap)" % ANIMPAK_CEILING)
        print("          _end = 0x%06X -- OVER by %d B. The GHZ anim-pack load will"
              % (end, end - ANIMPAK_CEILING))
        print("          corrupt .bss -> GFS fault during load (master traps 0x06000956,")
        print("          cont_frames=0). Reclaim .text/.bss before capturing.")
        print("-" * 72)
        print("RESULT: RED -- layout overflow; do not capture.")
        return 1
    print("  [GREEN] S0 _end 0x%06X < ANIMPAK ceiling 0x%06X (%d B headroom)"
          % (end, ANIMPAK_CEILING, ANIMPAK_CEILING - end))

    syms = {}
    missing = []
    for s in [_scene.SYM_MAGIC] + SYMS:
        syms[s] = _scene.map_symbol(map_text, s)
        if syms[s] is None:
            missing.append(s)
    if missing:
        print("  [ RED ] scan-split witness symbols present in the link map")
        for s in missing:
            print("          MISSING %s" % s)
        print("          (Expected on the current tree / a -e P6_SPLIT unset build.)")
        print("-" * 72)
        print("RESULT: RED -- scan-split not compiled in.")
        return 1
    print("  [GREEN] scan-split witness symbols present in the link map")

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

    slave_n = _scene.peek_u32(mod, sections, syms["_p6_w_scan_slave_n"], perm, signed=True)
    slave_pop = _scene.peek_u32(mod, sections, syms["_p6_w_scan_slave_pop"], perm, signed=True)
    frames = _scene.peek_u32(mod, sections, syms["_p6_w_cont_frames"], perm, signed=True)

    scene_span = TEMPENTITY_START - RESERVE_ENTITY_COUNT
    classified = slave_n is not None and 0 < slave_n <= scene_span
    populated = slave_pop is not None and slave_pop > 0
    nohang = frames is not None and frames > 100

    checks = [
        ("S1 slave classified a valid upper scene sub-range (0 < slave_n <= %d)" % scene_span,
         classified, "slave_n=%s (== TEMPENTITY_START - P6_SCAN_MID)" % slave_n),
        ("S2 slave saw populated scene entities (slave_pop > 0)", populated,
         "slave_pop=%s" % slave_pop),
        ("S3 mid-frame fork-join did NOT hang (cont_frames > 100)", nohang,
         "cont_frames=%s" % frames),
    ]
    ok = all(c for _, c, _ in checks)
    for title, passed, detail in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
        print("          %s" % detail)
    print("-" * 72)
    if ok:
        print("  Slave classified %d of %d scene slots (%d populated). The master"
              % (slave_n, EXPECT_SLAVE_N, slave_pop))
        print("  offloaded that classify; run qa_p6_perf.py for the compute-full delta")
        print("  and the diag sweep for behavioural parity.")
        print("RESULT: GREEN -- scan-split active + coherent (no hang).")
        return 0
    if frames is not None and frames <= 100:
        print("  NOTE: cont_frames small with the split wired -> the second mid-frame")
        print("        fork-join likely HUNG. Check p6_scan_kick/join + slave bring-up.")
    print("RESULT: RED -- scan-split not proven active (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
