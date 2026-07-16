#!/usr/bin/env python3
"""qa_chain_draw.py -- #328 chain-GHZ sprite-BLIT draw-wall gate (RED->GREEN).

MEASURED ROOT CAUSE (2026-07-13, live-memory harness -- skill v2.8.0 sec.0 +
CLAUDE.md sec.8.5): the full-experience CHAIN build (P6_FRONTEND_CHAIN +
P6_GHZCUT_BOOT + P6_FRAMEDIR) renders playable GHZ at ~7 fps while PLAIN GHZ
renders the SAME scene at ~59 fps. NOT VDP1-fill-bound (p6_w_perf_v1_busyvbl
~0.7%). The wall is a per-frame CPU cost in the sprite BLIT path: 4 store slots
banded-FetchRect (miniz inflate + row repack) EVERY frame -- slot 11 Sonic1,
slot 14 Tails1, slot 17 Items(rings), slot 18 Display(HUD) -- with frdSlot=-1
despite their pre-cut frame directories being staged (frd_active=9, frd_misses=0,
frd_lookups>0). The FRD attach ran before the GHZ scene arm re-bound those
surfaces, so the per-frame-drawn player/HUD/ring sheets stayed on the banded
per-draw inflate path.

FIX: re-run p6_frd_attach_bound() AFTER p6_scene_load_and_arm() at the GHZ
landing so every bound surface with a staged FRD routes its slot-cache miss
through the pre-cut pattern (one aligned linear copy) instead of the banded
miniz inflate.

This gate boots the chain, auto-advances Logos->Title->Menu->AIZ->GHZCutscene->
playable GHZ, then at the GHZ landing asserts (measured over a window):
  (1) p6_w_sht_fetches delta/frame <= FETCH_FLOOR   (RED ~5.9 -> GREEN <=1.0)
  (2) p6_w_draw_blit_v OR p6_w_perf_vbl_draw draw-window vblanks low
  (3) fps >= FPS_FLOOR                               (RED ~7 -> GREEN materially up)

MEASUREMENT RULE (binding, perf-chain-ghz memory): for the chain draw path use
the VBLANK sub-brackets (p6_w_draw_blit_v / p6_w_perf_vbl_draw), NEVER the
wrap-corrupted p6_w_perf_cyc_* FRT ms.

Usage:
  python tools/qa_chain_draw.py [--fps-floor 20] [--fetch-floor 1.0]
                                [--boot-timeout 360] [--no-launch]
Exit 0 = GREEN, 1 = RED, 2 = harness error.
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
    ap.add_argument("--fps-floor", type=float, default=20.0)
    ap.add_argument("--fetch-floor", type=float, default=1.0,
                    help="max banded FetchRect inflates/frame for GREEN")
    ap.add_argument("--blit-floor", type=float, default=4.0,
                    help="max p6_w_draw_blit_v delta over the window for GREEN")
    ap.add_argument("--boot-timeout", type=float, default=360.0)
    ap.add_argument("--fps-window", type=float, default=5.0)
    ap.add_argument("--map", default=str(_ROOT / "game.map"))
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=55355)
    ap.add_argument("--no-launch", action="store_true")
    args = ap.parse_args()

    map_text = Path(args.map).read_text(errors="replace")
    proc = None
    if not args.no_launch:
        boot = _HERE / "_gl_boot.ps1"
        print("[qa_chain_draw] launching GL RetroArch via", boot)
        proc = subprocess.Popen(
            ["pwsh", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(boot)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        rd = qa_trace.Reader(True, None, args.host, args.port)

        def sym(n):
            return rd.sym(map_text, n)

        a_csf = sym("RSDK::currentSceneFolder")
        a_cont = sym("p6_w_cont_frames")
        a_fetch = sym("p6_w_sht_fetches")
        a_blit = sym("p6_w_draw_blit_v")
        a_vbld = sym("p6_w_perf_vbl_draw")
        a_v1busy = sym("p6_w_perf_v1_busyvbl")
        a_frda = sym("p6_w_frd_active")
        for nm, av in [("currentSceneFolder", a_csf), ("cont_frames", a_cont),
                       ("sht_fetches", a_fetch)]:
            if av is None:
                print("[HARNESS] missing symbol:", nm, file=sys.stderr)
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
        time.sleep(3.0)
        if folder() != "GHZ":
            print("[HARNESS] left GHZ during settle (folder=%s)" % folder(), file=sys.stderr)
            return 2

        f0 = rd.r32(a_fetch)
        b0 = rd.r32(a_blit) if a_blit else None
        c0 = rd.r32(a_cont)
        w0 = time.time()
        time.sleep(args.fps_window)
        f1 = rd.r32(a_fetch)
        b1 = rd.r32(a_blit) if a_blit else None
        c1 = rd.r32(a_cont)
        w1 = time.time()

        frames = (c1 - c0) if (c0 is not None and c1 is not None) else 0
        fps = frames / (w1 - w0) if frames else 0.0
        fetch_pf = ((f1 - f0) / frames) if (frames > 0 and f0 is not None and f1 is not None) else None
        blit_d = (b1 - b0) if (b0 is not None and b1 is not None) else None
        v1busy = rd.r32(a_v1busy) if a_v1busy else None
        frda = rd.r32(a_frda) if a_frda else None

        print("=" * 66)
        print("CHAIN GHZ draw-wall gate (%.1fs window, %d frames)" % (args.fps_window, frames))
        print("  fps                       = %.2f   (floor %.1f)" % (fps, args.fps_floor))
        print("  p6_w_sht_fetches /frame   = %s   (floor %.2f)" % (
            ("%.2f" % fetch_pf) if fetch_pf is not None else "?", args.fetch_floor))
        print("  p6_w_draw_blit_v  delta   = %s   (floor %.1f)" % (blit_d, args.blit_floor))
        print("  p6_w_perf_v1_busyvbl      = %s   (VDP1 idle check)" % v1busy)
        print("  p6_w_frd_active           = %s" % frda)
        print("=" * 66)

        reasons = []
        if fetch_pf is None or fetch_pf > args.fetch_floor:
            reasons.append("sht_fetches %.2f/frame > floor %.2f"
                           % (fetch_pf if fetch_pf is not None else -1, args.fetch_floor))
        if fps < args.fps_floor:
            reasons.append("fps %.1f < floor %.1f" % (fps, args.fps_floor))
        if blit_d is not None and blit_d > args.blit_floor:
            reasons.append("draw_blit_v delta %s > floor %.1f" % (blit_d, args.blit_floor))
        if not reasons:
            print("GREEN: banded fetches eliminated, fps up, draw window low")
            return 0
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
