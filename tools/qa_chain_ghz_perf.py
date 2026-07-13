#!/usr/bin/env python3
"""qa_chain_ghz_perf.py -- #328 chain-GHZ ProcessObjects far-cull perf gate.

MEASURED PROBLEM (parent, 2026-07-13): the full-experience CHAIN build
(P6_FRONTEND_CHAIN + P6_GHZCUT_BOOT + P6_FRAMEDIR) renders playable GHZ at
~15 fps while the PLAIN GHZ build renders the SAME scene at ~59 fps. Root
cause: the ProcessObjects far-slot cull (Object.cpp #298 branch, gated on
g_p6_fe_ghz_cull) is NOT arming on the chain's AIZ->GHZCutscene->GHZ handoff
load path -> every one of the 1088 scene slots is walked through the slow
wide-pool RSDK_ENTITY_AT remap (~30ms) instead of the ~8ms bit-test skip the
plain build uses.

This gate boots the chain build in GL RetroArch (headless-ok), auto-advances
Logos->Title->Menu->AIZ->GHZCutscene->playable GHZ, then at the GHZ landing
asserts:
  (1) g_p6_fe_ghz_cull == 1   (the far-cull is ARMED)
  (2) fps >= FLOOR            (derived from p6_w_cont_frames delta / wall)

RED now (~15 fps, cull==0). GREEN target: cull==1 AND fps >= 45.

The live-memory harness is the PRIMARY diagnostic for this compute/memory
question (skill v2.8.0 sec.0 + CLAUDE.md sec.8.5). Reads witnesses via the
game.map symbols over RetroArch UDP (port 55355).

Usage:
  python tools/qa_chain_ghz_perf.py [--floor 45] [--boot-timeout 180]
                                    [--fps-window 5.0] [--no-launch]
Exit 0 = GREEN (cull armed AND fps>=floor). Exit 1 = RED. Exit 2 = harness.
"""
from __future__ import annotations

import argparse
import importlib.util
import subprocess
import sys
import time
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_ROOT = _HERE.parent


def _load(mod, fn):
    spec = importlib.util.spec_from_file_location(mod, _HERE / fn)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)  # type: ignore[union-attr]
    return m


qa_trace = _load("qa_trace", "qa_trace.py")


def read_map(path: Path) -> str:
    return path.read_text(errors="replace")


def sym(rd, map_text, name):
    return rd.sym(map_text, name)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--floor", type=float, default=45.0, help="fps floor for GREEN")
    ap.add_argument("--boot-timeout", type=float, default=200.0,
                    help="max wall seconds to reach folder=='GHZ' with frames advancing")
    ap.add_argument("--fps-window", type=float, default=5.0)
    ap.add_argument("--map", default=str(_ROOT / "game.map"))
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=55355)
    ap.add_argument("--no-launch", action="store_true",
                    help="assume RetroArch already booted on game.cue")
    args = ap.parse_args()

    map_text = read_map(Path(args.map))

    proc = None
    if not args.no_launch:
        boot = _HERE / "_gl_boot.ps1"
        print("[qa_chain_ghz_perf] launching GL RetroArch via", boot)
        proc = subprocess.Popen(
            ["pwsh", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(boot)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    try:
        rd = qa_trace.Reader(True, None, args.host, args.port)

        # symbol addresses (all __attribute__((used)) globals in the chain map)
        a_cull = sym(rd, map_text, "g_p6_fe_ghz_cull")
        a_scan_n = sym(rd, map_text, "p6_scan_n")
        a_maxslot = sym(rd, map_text, "p6_scan_idx_maxslot")
        a_cyc_obj = sym(rd, map_text, "p6_w_perf_cyc_obj")
        a_cont = sym(rd, map_text, "p6_w_cont_frames")
        a_csf = sym(rd, map_text, "RSDK::currentSceneFolder")
        for nm, av in [("g_p6_fe_ghz_cull", a_cull), ("p6_scan_n", a_scan_n),
                       ("p6_w_perf_cyc_obj", a_cyc_obj), ("p6_w_cont_frames", a_cont),
                       ("RSDK::currentSceneFolder", a_csf)]:
            if av is None:
                print("[HARNESS] missing symbol in map:", nm, file=sys.stderr)
                return 2

        def folder():
            try:
                return rd.mem.read_saturn(a_csf, 16).split(b"\0")[0].decode(errors="replace")
            except Exception:
                return None

        # ---- wait for playable GHZ (folder=="GHZ" AND cont_frames advancing) ----
        t_start = time.time()
        last_cont = None
        reached = False
        while time.time() - t_start < args.boot_timeout:
            f = folder()
            cont = rd.r32(a_cont)
            if f == "GHZ" and cont is not None:
                if last_cont is not None and cont > last_cont + 3:
                    reached = True
                    break
                last_cont = cont
            time.sleep(1.0)
        if not reached:
            print("[HARNESS] never reached playable GHZ (folder='%s' cont=%s) within %.0fs"
                  % (folder(), rd.r32(a_cont), args.boot_timeout), file=sys.stderr)
            return 2

        # give the landing a moment to settle past the seam, then re-confirm GHZ
        time.sleep(3.0)
        if folder() != "GHZ":
            print("[HARNESS] left GHZ during settle (folder='%s')" % folder(), file=sys.stderr)
            return 2

        # ---- measure fps over the window + read the cull/index witnesses ----
        c0 = rd.r32(a_cont)
        w0 = time.time()
        time.sleep(args.fps_window)
        c1 = rd.r32(a_cont)
        w1 = time.time()
        fps = (c1 - c0) / (w1 - w0) if (c1 is not None and c0 is not None) else 0.0

        cull = rd.r32(a_cull)
        scan_n = rd.r32(a_scan_n)
        maxslot = rd.r32(a_maxslot) if a_maxslot is not None else None
        cyc_obj = rd.r32(a_cyc_obj)
        cyc_ms = (cyc_obj or 0) * 1.194 / 1000.0

        print("=" * 64)
        print("CHAIN GHZ LANDING witnesses:")
        print("  folder          = GHZ")
        print("  g_p6_fe_ghz_cull= %s  (1 == far-cull ARMED)" % cull)
        print("  p6_scan_n       = %s  (sorted-index entry count; must be >0)" % scan_n)
        print("  p6_scan_idx_maxslot = %s" % maxslot)
        print("  p6_w_perf_cyc_obj   = %s ticks = %.2f ms" % (cyc_obj, cyc_ms))
        print("  fps (cont delta over %.1fs) = %.2f" % (args.fps_window, fps))
        print("=" * 64)

        green = (cull == 1) and (fps >= args.floor)
        if green:
            print("GREEN: cull armed AND fps %.1f >= floor %.1f" % (fps, args.floor))
            return 0
        reasons = []
        if cull != 1:
            reasons.append("cull NOT armed (g_p6_fe_ghz_cull=%s)" % cull)
        if fps < args.floor:
            reasons.append("fps %.1f < floor %.1f" % (fps, args.floor))
        print("RED: " + "; ".join(reasons))
        return 1
    finally:
        if proc is not None:
            try:
                subprocess.run(["taskkill", "/F", "/IM", "retroarch.exe"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            except Exception:
                pass


if __name__ == "__main__":
    sys.exit(main())
