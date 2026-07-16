#!/usr/bin/env python3
"""qa_chain_speed.py -- #243/#315 PRIMARY GAME-SPEED gate (programmatic, no pixels).

THE METRIC: p6_w_tick_frames (the 60 Hz game-logic clock) advance per WALL-second
at chain playable GHZ. This is GAME SPEED, not render fps. The user's #1 complaint
is genuine SLOW-MOTION ("slow even at 60fps display"): logic ticks < 60/wall-sec.

BUDGET (measured, perf-chain-ghz-frontend-scan-not-sh2.md): the frontend runs
logic in a catch-up loop clamped to P6_TICK_CAP=4 (p6_io_main.cpp:8712). At ~6fps
render (~8 vbl/frame) the loop caps at 4 ticks/frame -> 6*4 = 24 Hz logic = 40%
speed. The ONLY correct-speed fix is cutting RENDER cost so elapsed-vbl/frame drops
to ~2, letting the cap-4 catch-up reach the full 60 Hz elapsed-vblank count.

RED baseline (pre-fix, MEASURED 2026-07-16): tick_frames ~24/wall-sec (40% speed).
GREEN target: tick_frames >= 55/wall-sec (>= ~92% realtime).

Secondary report (NOT the pass/fail axis): p6_w_cont_frames/wall-sec = render fps.

This is a LIVE memory-read gate (RetroArch UDP). No savestate, no screenshot.
Addresses re-resolved from game.map each run (they shift per build).

Usage:
  python tools/qa_chain_speed.py [--min 55] [--window 6] [--boot-timeout 360]
                                 [--no-launch]
Exit 0 = GREEN, 1 = RED (too slow), 2 = harness error.
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


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--min", type=float, default=55.0,
                    help="min p6_w_tick_frames advance/wall-sec for GREEN")
    ap.add_argument("--window", type=float, default=6.0)
    ap.add_argument("--boot-timeout", type=float, default=360.0)
    ap.add_argument("--map", default=str(_ROOT / "game.map"))
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=55355)
    ap.add_argument("--no-launch", action="store_true")
    args = ap.parse_args()

    map_text = Path(args.map).read_text(errors="replace")
    proc = None
    if not args.no_launch:
        boot = _HERE / "_gl_boot.ps1"
        print("[qa_chain_speed] launching GL RetroArch via", boot)
        proc = subprocess.Popen(
            ["pwsh", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(boot)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        rd = qa_trace.Reader(True, None, args.host, args.port)

        def sym(n):
            return rd.sym(map_text, n)

        a_csf = sym("RSDK::currentSceneFolder")
        a_cont = sym("p6_w_cont_frames")
        a_tick = sym("p6_w_tick_frames")
        if a_tick is None:
            print("[HARNESS] p6_w_tick_frames symbol absent (build lacks P6_TICK_CATCHUP?)",
                  file=sys.stderr)
            return 2
        if a_csf is None or a_cont is None:
            print("[HARNESS] missing currentSceneFolder/cont_frames symbol", file=sys.stderr)
            return 2

        def folder():
            try:
                return rd.mem.read_saturn(a_csf, 16).split(b"\0")[0].decode(errors="replace")
            except Exception:
                return None

        t0 = time.time()
        last = None
        reached = False
        while time.time() - t0 < args.boot_timeout:
            f = folder()
            cont = rd.r32(a_cont)
            if f == "GHZ" and cont is not None:
                if last is not None and cont > last + 3:
                    reached = True
                    break
                last = cont
            time.sleep(1.0)
        if not reached:
            print("[HARNESS] never reached playable GHZ (folder=%s cont=%s) in %.0fs"
                  % (folder(), rd.r32(a_cont), args.boot_timeout), file=sys.stderr)
            return 2
        time.sleep(3.0)  # let the landing settle
        if folder() != "GHZ":
            print("[HARNESS] left GHZ during settle (folder=%s)" % folder(), file=sys.stderr)
            return 2

        tk0 = rd.r32(a_tick)
        c0 = rd.r32(a_cont)
        w0 = time.time()
        time.sleep(args.window)
        tk1 = rd.r32(a_tick)
        c1 = rd.r32(a_cont)
        w1 = time.time()

        dt = (tk1 - tk0) if (tk0 is not None and tk1 is not None) else None
        dc = (c1 - c0) if (c0 is not None and c1 is not None) else None
        wall = w1 - w0
        tick_ps = (dt / wall) if dt is not None else None
        cont_ps = (dc / wall) if dc is not None else None
        speed_pct = (tick_ps / 60.0 * 100.0) if tick_ps is not None else None

        print("=" * 66)
        print("CHAIN GHZ GAME-SPEED gate (%.1fs wall window)" % wall)
        print("  p6_w_tick_frames /wall-sec = %s   (min %.0f, target 60)  [PRIMARY]"
              % (("%.1f" % tick_ps) if tick_ps is not None else "?", args.min))
        print("  game speed                 = %s%% of realtime"
              % (("%.0f" % speed_pct) if speed_pct is not None else "?"))
        print("  p6_w_cont_frames /wall-sec = %s   (render fps, secondary)"
              % (("%.1f" % cont_ps) if cont_ps is not None else "?"))
        print("=" * 66)

        if tick_ps is None:
            print("RED: could not read tick_frames")
            return 1
        if tick_ps >= args.min:
            print("GREEN: game logic at >= %.0f Hz (%.0f%% realtime)"
                  % (args.min, speed_pct))
            return 0
        print("RED: game logic %.1f Hz < min %.0f (%.0f%% realtime = slow-motion)"
              % (tick_ps, args.min, speed_pct))
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
