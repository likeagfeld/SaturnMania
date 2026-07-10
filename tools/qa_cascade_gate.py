#!/usr/bin/env python3
# qa_cascade_gate.py -- #325 stage-1 vblank-quantization cascade gate (RED->GREEN).
#
# Contract: the settled chain-GHZ leg (post AIZ->GHZCutscene->GHZ handoff) must run
# at >= 20.0 median fps = the 3-vblank tier (frame <= 3 vblanks after the fgbg
# slave offload). RED on the 186898e/2becc6e baseline (measured 15.0 fps = 4-vblank
# quantization, p6_w_perf_full_frt ~37k FRT = ~44 ms master compute).
#
# LIVE measurement (binding memory live-memory-mandatory-for-temporal): reads the
# running headless RetroArch (qa_live.ps1 -NoMonitor) via qa_trace.Reader.
# fps = 60 * d(p6_w_cont_frames) / d(p6_perf_vbl_count) per interval, dcont==0
# intervals excluded (load/stall). Requires folder=="GHZ" before sampling starts
# (waits up to --wait secs for the chain to land).
#
# Usage: python tools/qa_cascade_gate.py [--secs 30] [--wait 240] [--floor 20.0]
# Exit 0 = GREEN, 1 = RED, 2 = harness error (no live target / never reached GHZ).
import argparse, importlib.util, statistics, sys, time
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument("--secs", type=float, default=30.0, help="sampling window at settled GHZ")
ap.add_argument("--wait", type=float, default=240.0, help="max secs to wait for folder==GHZ")
ap.add_argument("--floor", type=float, default=20.0, help="median fps floor (3-vblank tier)")
args = ap.parse_args()

H = Path("tools")
spec = importlib.util.spec_from_file_location("qa_trace", H / "qa_trace.py")
qt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(qt)

mt = Path("game.map").read_text(errors="replace")
rd = qt.Reader(True, None, "127.0.0.1", 55355)
csf = rd.sym(mt, "RSDK::currentSceneFolder")

def folder():
    try:
        return rd.mem.read_saturn(csf, 16).split(b"\0")[0].decode(errors="replace")
    except Exception:
        return None

def u32(name):
    a = rd.sym(mt, name)
    if a is None:
        return None
    return rd.r32(a)

# 1. wait for the chain to land at GHZ (live cont_frames must ALSO advance --
#    a dead RA reads a frozen folder string; the liveness check discriminates).
t0 = time.time()
seen = None
while time.time() - t0 < args.wait:
    f = folder()
    seen = f
    if f == "GHZ":
        c0 = u32("p6_w_cont_frames")
        time.sleep(1.0)
        c1 = u32("p6_w_cont_frames")
        if c0 is not None and c1 is not None and c1 > c0:
            break
    time.sleep(2.0)
else:
    print("HARNESS-ERROR: never reached live GHZ (last folder=%r) in %.0fs" % (seen, args.wait))
    sys.exit(2)

# 2. sample the settled leg
fpss, frozen, last = [], 0, None
full_frts, objs, fgbgs, draws = [], [], [], []
t0 = time.time()
while time.time() - t0 < args.secs:
    cont = u32("p6_w_cont_frames")
    vbl = u32("p6_perf_vbl_count")
    if last and cont is not None and vbl is not None and vbl > last[1]:
        dc, dv = cont - last[0], vbl - last[1]
        if dc == 0:
            frozen += 1
        else:
            fpss.append(60.0 * dc / dv)
            for lst, nm in ((full_frts, "p6_w_perf_full_frt"), (objs, "p6_w_perf_cyc_obj"),
                            (fgbgs, "p6_w_perf_cyc_fgbg"), (draws, "p6_w_perf_cyc_draw")):
                v = u32(nm)
                if v:
                    lst.append(v)
    if cont is not None and vbl is not None:
        last = (cont, vbl)
    time.sleep(0.7)

if folder() != "GHZ":
    print("HARNESS-ERROR: leg changed away from GHZ mid-sample (folder=%r)" % folder())
    sys.exit(2)
if not fpss:
    print("HARNESS-ERROR: no moving intervals (frozen=%d) -- RA dead or leg stalled" % frozen)
    sys.exit(2)

med = statistics.median(fpss)
def m(lst):
    return int(statistics.median(lst)) if lst else -1
print("settled GHZ: n=%d frozen=%d fps median=%.1f min=%.1f max=%.1f  "
      "full_frt=%d cyc_obj=%d cyc_fgbg=%d cyc_draw=%d (FRT ticks, ~1.19us)"
      % (len(fpss), frozen, med, min(fpss), max(fpss),
         m(full_frts), m(objs), m(fgbgs), m(draws)))
ok = med >= args.floor
print("%s CASCADE-1: settled chain-GHZ median fps %.1f %s %.1f (3-vblank tier)"
      % ("GREEN" if ok else "RED  ", med, ">=" if ok else "<", args.floor))
sys.exit(0 if ok else 1)
