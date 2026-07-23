#!/usr/bin/env python3
"""qa_ghz_lifeface.py -- RED-first gate for the GHZ HUD lives-count Sonic-FACE
sprite rendering as a pixelated garbage square (task #326 follow-up).

MEASURED ROOT-CAUSE HYPOTHESIS UNDER TEST (data-driven, decomp/code-cited):
  The lives-face DrawSprite (HUD.c:299, lifeIconAnimator anim 12 frame 0 =
  Global/Display.gif rect 111,112,18,17) routes to the Saturn VDP1 content-size
  pool bucket idx1 (64x80) because its width 18 > the tiny bucket 16 (p6_vdp1.c
  p6_bucket_for + P6_BUCK). The digits (9x14) route to the tiny bucket idx0
  (16x20), which is why they render CLEAN while the face garbles. If the
  per-frame COUNT of distinct bucket-1 rects at a busy landed-GHZ frame EXCEEDS
  the bucket's slot count P6_BK1 (18), the LRU pool evicts + restages a slot
  MID-FRAME after an earlier draw command in the same VDP1 list already
  referenced that slot's jo_id -- so at flush VDP1 reads the NEW rect's pixels
  and CMDSIZE for the earlier command (p6_vdp1.c:300-321 witness comment): the
  striped/garbage-square garble. The FRD pattern + restage pack are PROVEN
  byte-correct offline (build_frame_dir S1/S2 + the direct FRD->crop diff), so
  the corruption is this intra-frame slot reuse, not the content fetch.

WHAT THIS GATE READS (live WRAM witnesses, addrs resolved from the FRESH
game.map -- the "regen from current map" rule; VDP1 VRAM is not in netmem):
  p6_w_buck0_fmax .. p6_w_buck4_fmax  -- running MAX per-frame miss (restage)
                                          count per bucket (p6_vdp1.c perf_reset)
  vs the compiled slot counts P6_BK0..P6_BK2 + wide + catch-all.

VERDICT:
  RED  if any bucket's fmax  >  its slot count  (intra-frame reuse -> garble)
  GREEN if every bucket's fmax <= its slot count.

This gate fires RED on the pre-fix build (bucket 1 over-subscribed) and GREEN
after the fix (bucket 1 sized to cover the face + labels working set, OR the
face routed so it no longer collides). The final visual verdict is the USER's
framebuffer confirmation of a clean blue Sonic head bottom-left.

Usage:
  python tools/qa_ghz_lifeface.py --map game.map [--host 127.0.0.1] [--port 55355]
Run AFTER _gl_boot has advanced the chain to a landed GHZ frame (single RA
instance, port 55355). Exit 0 = GREEN, 1 = RED, 2 = harness error.
"""
from __future__ import annotations

import argparse
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))

# Compiled bucket slot counts (p6_vdp1.c, P6_FRONTEND_MENU + P6_GHZCUT_BOOT flavor):
#   idx0 TINY 16x20  = P6_BK0 26
#   idx1 64x80       = P6_BK1 18   <- the lives-face bucket
#   idx2 WIDE 176x56 = P6_BKW 13
#   idx3 160x160     = P6_BK2 7
#   idx4 catch-all   = P6_VDP1_NSLOTS 4
# Keep in sync with p6_vdp1.c if the counts change.
SLOTS = [26, 18, 13, 7, 4]
NAMES = ["tiny16x20", "64x80(FACE)", "wide176x56", "160x160", "catchall248x160"]


def main() -> int:
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
        print("HARNESS: no %s" % a.map)
        return 2
    mt = open(a.map, errors="replace").read()
    rd = qt.Reader(True, None, a.host, a.port)

    def sym(n):
        return rd.sym(mt, n)

    fmax_syms = ["p6_w_buck0_fmax", "p6_w_buck1_fmax", "p6_w_buck2_fmax",
                 "p6_w_buck3_fmax", "p6_w_buck4_fmax"]
    fmax = []
    for s in fmax_syms:
        adr = sym(s)
        if adr is None:
            print("HARNESS: %s ABSENT from %s (wrong/old map?)" % (s, a.map))
            return 2
        v = rd.r32(adr)
        fmax.append(v)
    if any(v is None for v in fmax):
        print("HARNESS: live read None (RA dead? boot via _gl_boot.ps1, single "
              "RA on port %d, chain advanced to landed GHZ)" % a.port)
        return 2

    ok = True
    print("bucket per-frame-demand (fmax) vs slot count:")
    for i in range(5):
        over = fmax[i] > SLOTS[i]
        print("  idx%d %-16s fmax=%-4d slots=%-3d %s"
              % (i, NAMES[i], fmax[i], SLOTS[i], "  <-- OVER (garble)" if over else ""))
        if over:
            ok = False

    if not ok:
        print("RED: >=1 bucket over-subscribed -- intra-frame slot reuse garbles "
              "an already-drawn sprite (the lives-face square).")
        return 1
    print("GREEN: every bucket fmax <= its slot count (no intra-frame reuse).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
