#!/usr/bin/env python3
"""qa_sync_floor.py -- #331 frame sync-floor attribution gate (Step 1).

THE QUESTION (memory aiz-cd-fix-ghz-regression-bisect, d73b052 attribution
table): AIZ fly-in runs 6.66 vbl/frame of which only 3.67 vbl is INSIDE
p6_frontend_frame -- ~3.0 vbl/frame is OUTSIDE the frame fn (the jo core loop /
slSynch alignment). Chain GHZ: 4.00 vbl/frame with ~1-2 vbl of work. This gate
splits the outside-frame region PRECISELY using the #331 witnesses:

  p6_w_jo_vbl_sum      (p6_io_main.cpp)  cumulative outside-frame vblanks
                                         (prev frame END -> frame START)
  p6_w_sync_vbl_sum    (p6_perf.c, incremented by the jo core.c bracket)
                                         cumulative vblanks INSIDE slSynch
  p6_w_sync_n          slSynch calls (one per jo loop iteration -- verifies
                                         exactly ONE sync wait per frame)
  p6_w_sync_edsr_busy  frames where VDP1 EDSR.CEF==0 at slSynch ENTRY
                                         (the sync wait includes VDP1 still
                                          rasterizing = DRAW-BOUND floor)
  p6_w_aiz_vbl_framesum cumulative vblanks INSIDE p6_frontend_frame

Split per rendered frame (all vblank-anchored, throttle-independent):
  total   = d(p6_w_perf_vblanks) / d(cont_frames)
  inside  = d(framesum) / d(cont_frames)
  outside = d(jo_vbl_sum) / d(cont_frames)      (== total - inside)
  sync    = d(sync_vbl_sum) / d(cont_frames)    (inside slSynch)
  jo-else = outside - sync                      (loop body minus slSynch)
  edsr_busy% = d(sync_edsr_busy) / d(sync_n)    (slSynch waits on VDP1 draw)

RED/GREEN: RED when outside > --max-outside (default 1.25 vbl/frame) at the
measured scene. Fires RED on the current build (measured ~3.0 at AIZ) BEFORE
any fix; a landed cut must turn it GREEN. Doc basis: ST-238-R1 p.188 (slSynch
waits to the event-processing unit time = JO_FRAMERATE=1 vblank nominal + the
data-transfer/screen-switch step); ST-013-R3 EDSR.CEF.

LIVE RetroArch UDP read (qa_trace Reader), map-resolved per build. Mednafen
two-capture remains the independent arbiter for the same counters.

Usage:
  py -3 tools/qa_sync_floor.py --scene aiz [--window 8] [--max-outside 1.25]
                               [--boot-timeout 360] [--no-launch] [--report-only]
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

SCENE_FOLDER = {"aiz": "AIZ", "ghz": "GHZ", "ghzcut": "GHZCutscene"}


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--scene", choices=sorted(SCENE_FOLDER), default="aiz")
    ap.add_argument("--window", type=float, default=8.0)
    ap.add_argument("--max-outside", type=float, default=1.25,
                    help="GREEN when outside-frame vbl/frame <= this")
    ap.add_argument("--boot-timeout", type=float, default=360.0)
    ap.add_argument("--map", default=str(_ROOT / "game.map"))
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=55355)
    ap.add_argument("--no-launch", action="store_true")
    ap.add_argument("--report-only", action="store_true",
                    help="always exit 0 after printing the split (attribution runs)")
    args = ap.parse_args()

    want_folder = SCENE_FOLDER[args.scene]
    map_text = Path(args.map).read_text(errors="replace")
    proc = None
    if not args.no_launch:
        boot = _HERE / "_gl_boot.ps1"
        print("[qa_sync_floor] launching GL RetroArch via", boot)
        proc = subprocess.Popen(
            ["pwsh", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(boot)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        rd = qa_trace.Reader(True, None, args.host, args.port)

        def sym(n):
            return rd.sym(map_text, n)

        names = ["RSDK::currentSceneFolder", "p6_w_cont_frames", "p6_w_perf_vblanks",
                 "p6_w_aiz_vbl_framesum", "p6_w_jo_vbl_sum", "p6_w_sync_vbl_sum",
                 "p6_w_sync_vbl_max", "p6_w_sync_n", "p6_w_sync_edsr_busy",
                 "p6_w_tick_frames"]
        addr = {n: sym(n) for n in names}
        missing = [n for n, a in addr.items() if a is None]
        if missing:
            print("[HARNESS] missing map symbols: %s (build lacks the #331 "
                  "witnesses / not the chain flavor?)" % ", ".join(missing),
                  file=sys.stderr)
            return 2

        def folder():
            try:
                return rd.mem.read_saturn(addr["RSDK::currentSceneFolder"],
                                          16).split(b"\0")[0].decode(errors="replace")
            except Exception:
                return None

        t0 = time.time()
        last = None
        reached = False
        while time.time() - t0 < args.boot_timeout:
            f = folder()
            cont = rd.r32(addr["p6_w_cont_frames"])
            if f == want_folder and cont is not None:
                if last is not None and cont > last + 3:
                    reached = True
                    break
                last = cont
            time.sleep(1.0)
        if not reached:
            print("[HARNESS] never reached %s (folder=%s cont=%s) in %.0fs"
                  % (want_folder, folder(), rd.r32(addr["p6_w_cont_frames"]),
                     args.boot_timeout), file=sys.stderr)
            return 2
        time.sleep(2.0)
        if folder() != want_folder:
            print("[HARNESS] left %s during settle (folder=%s)"
                  % (want_folder, folder()), file=sys.stderr)
            return 2

        def snap():
            return {n: rd.r32(addr[n]) for n in names[1:]}

        s0 = snap()
        time.sleep(args.window)
        if folder() != want_folder:
            print("[HARNESS] scene changed mid-window (folder=%s) -- window invalid"
                  % folder(), file=sys.stderr)
            return 2
        s1 = snap()
        d = {}
        for n in names[1:]:
            if s0[n] is None or s1[n] is None:
                print("[HARNESS] unreadable witness %s" % n, file=sys.stderr)
                return 2
            d[n] = s1[n] - s0[n]

        dc = d["p6_w_cont_frames"]
        dn = d["p6_w_sync_n"]
        dv = d["p6_w_perf_vblanks"]
        if dc <= 0 or dv <= 0:
            print("[HARNESS] frame/vblank counters frozen (dc=%s dv=%s) -- "
                  "harness or build unhealthy" % (dc, dv), file=sys.stderr)
            return 2

        total = dv / dc
        inside = d["p6_w_aiz_vbl_framesum"] / dc
        outside = d["p6_w_jo_vbl_sum"] / dc
        sync = d["p6_w_sync_vbl_sum"] / dc
        jo_else = outside - sync
        busy_pct = (100.0 * d["p6_w_sync_edsr_busy"] / dn) if dn > 0 else float("nan")
        syncs_per_frame = dn / dc
        fps = 60.0 / total
        tick_ps = d["p6_w_tick_frames"] * 60.0 / dv

        print("=" * 70)
        print("SYNC-FLOOR ATTRIBUTION  scene=%s  window=%.1fs  frames=%d  vbl=%d"
              % (want_folder, args.window, dc, dv))
        print("  render fps                    = %6.2f  (%.2f vbl/frame)" % (fps, total))
        print("  game speed (tick/emu-s)       = %6.1f  (%.0f%% realtime)"
              % (tick_ps, tick_ps / 60.0 * 100.0))
        print("  INSIDE p6_frontend_frame      = %6.2f vbl/frame" % inside)
        print("  OUTSIDE (jo body incl. sync)  = %6.2f vbl/frame" % outside)
        print("    of which slSynch            = %6.2f vbl/frame" % sync)
        print("    of which jo-else            = %6.2f vbl/frame" % jo_else)
        print("  slSynch calls per frame       = %6.2f  (expect 1.00)" % syncs_per_frame)
        print("  slSynch worst single span     = %6d vblanks (cumulative max)"
              % (s1["p6_w_sync_vbl_max"] or -1))
        print("  VDP1 busy (CEF=0) at slSynch  = %6.1f%% of syncs  "
              "(draw-bound share)" % busy_pct)
        print("=" * 70)

        if args.report_only:
            return 0
        if outside <= args.max_outside:
            print("GREEN: outside-frame %.2f <= %.2f vbl/frame"
                  % (outside, args.max_outside))
            return 0
        print("RED: outside-frame %.2f > %.2f vbl/frame (sync=%.2f jo-else=%.2f "
              "edsr_busy=%.0f%%)" % (outside, args.max_outside, sync, jo_else, busy_pct))
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
