#!/usr/bin/env python3
# =============================================================================
# qa_ghz_badnik_vis.py -- GHZ badnik/item VISIBILITY gate (RED-first, live).
#
# ROOT CAUSE (MEASURED 2026-07-21, live netmem, chain build parked at GHZ):
#   The chain VDP1 bind table (P6_VDP1_NSHEETS, p6_vdp1.c) is sized ZERO-MARGIN
#   to the GHZ-chain sheet demand. GHZOBJ (GHZ/Objects.gif -- badniks + bridges +
#   GHZ content objects, engine surface 11) binds LAST and overflows the cap ->
#   p6_vdp1_sheet_bind_banded returns -1 -> p6_vdp1HandleBySurface[11] = -1 ->
#   EVERY draw of that surface drops (p6_io_main.cpp:2571 hh<0 -> ++dropbysheet).
#   Live proof this gate encodes: p6_w_dropbysheet[11]=3943 (all other sheets 0),
#   p6_w_ghzobj_surf_handle=-1, bind_demand(39) > bind_count(33) -> 6 failed binds.
#
# GATE (live, RetroArch netmem; the game must be AT a GHZ gameplay leg):
#   RED  if ANY p6_w_dropbysheet[i] > DROP_TOL   (a loaded sheet is unbound+drawn)
#   RED  if p6_w_ghzobj_surf_handle < 0          (GHZOBJ surface never bound)
#   GREEN otherwise (every drawn sheet has a bound VDP1 handle).
#
# The fix (raise P6_VDP1_NSHEETS so GHZOBJ binds) turns this GREEN. Per the
# qa-iterative-improvement rule this gate is added BEFORE the fix and must be
# watched RED->GREEN.
#
# Usage:
#   python tools/qa_ghz_badnik_vis.py            # live, default map/host/port
#   python tools/qa_ghz_badnik_vis.py --map game.map
# =============================================================================
import argparse
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))

DROP_TOL = 0  # any drawn-but-unbound sheet is a bug


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", default=os.path.join(ROOT, "game.map"))
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=55355)
    a = ap.parse_args()

    spec = importlib.util.spec_from_file_location(
        "qa_trace", os.path.join(HERE, "qa_trace.py"))
    qt = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(qt)

    if not os.path.exists(a.map):
        print("RED: no %s" % a.map)
        return 1
    mt = open(a.map, errors="replace").read()
    rd = qt.Reader(True, None, a.host, a.port)

    def sym(n):
        return rd.sym(mt, n)

    base = sym("p6_w_dropbysheet")
    hsym = sym("p6_w_ghzobj_surf_handle")
    dsym = sym("p6_w_bind_demand")
    csym = sym("p6_w_bind_count")
    if base is None:
        print("RED: p6_w_dropbysheet ABSENT from %s (wrong/old map?)" % a.map)
        return 1

    drops = [rd.r32(base + 4 * i) for i in range(16)]
    if any(v is None for v in drops):
        print("RED: live read returned None (RA dead? boot via _gl_boot.ps1, "
              "single RA instance on port %d)" % a.port)
        return 1

    handle = rd.r32(hsym) if hsym is not None else None
    demand = rd.r32(dsym) if dsym is not None else None
    count = rd.r32(csym) if csym is not None else None
    # int32 sign fix
    def s32(v):
        return v - 0x100000000 if v is not None and v >= 0x80000000 else v
    handle = s32(handle)

    ok = True
    nonzero = [(i, v) for i, v in enumerate(drops) if v > DROP_TOL]
    if nonzero:
        ok = False
        print("RED: unbound-surface drops (sheetID -> count):")
        for i, v in nonzero:
            print("       sheetID %2d : %d drops%s"
                  % (i, v, "   <- GHZOBJ (badniks+bridges+GHZ objs)"
                     if i == 11 else ""))
    else:
        print("GREEN: p6_w_dropbysheet all zero (every drawn sheet bound)")

    if handle is not None and handle < 0:
        ok = False
        print("RED: p6_w_ghzobj_surf_handle = %d (GHZOBJ VDP1 handle UNBOUND)"
              % handle)
    elif handle is not None:
        print("     p6_w_ghzobj_surf_handle = %d (bound)" % handle)

    if demand is not None and count is not None:
        fails = demand - count
        print("     bind_demand=%d bind_count=%d -> %d failed bind(s)%s"
              % (demand, count, fails, "" if fails == 0 else "  (RED class)"))

    print("qa_ghz_badnik_vis: %s" % ("GREEN" if ok else "RED"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
