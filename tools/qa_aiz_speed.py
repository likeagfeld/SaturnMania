#!/usr/bin/env python3
"""qa_aiz_speed.py -- #302 AIZ-intro GAME-SPEED + per-frame CD-I/O gate.

THE BUG CLASS (measured 2026-07-16, live chain build): at the AIZ intro the
game runs ~5 tick/s (9% realtime) because ~94% of every frame is CD-I/O wait:
  p6_w_gfs_io_vbl delta = 16.86 vblanks per rendered frame
  p6_w_gfs_fills        = 1.43 per rendered frame (each fill ~= a real CD seek)
This is the 606fbfb bug class (banded/unstaged asset falls through to GFS per
frame) surfacing at the Menu->AIZ seam.

THE GATE (both axes must pass):
  PRIMARY   p6_w_tick_frames advance per EMULATED second at AIZ >= --min (50).
            VBLANK-ANCHORED (binding 2026-07-16, commit 666634b pattern): speed
            = d(tick_frames)/d(p6_w_perf_vblanks)*60. The original wall-clock
            divisor lied whenever RetroArch ran off realtime (base retroarch.cfg
            fastforward_ratio=0.0 = unlimited); the vblank anchor is
            throttle-independent on any emulator at any pacing.
  SECONDARY p6_w_gfs_io_vbl delta per rendered frame (cont_frames) <= --max-io (1.0)

ATTRIBUTION TABLE (#302): when the build carries the p6_w_aiz_vbl_* witnesses
(cumulative per-section vblank sums, p6_io_main.cpp), the gate prints the
vbl/frame breakdown -- objtick / fgpresent / bgstream / vdp1emit / other-in-
frame / jo-body(slSynch+VDP1 wait) -- so the render wall is attributed, not
guessed.

MEASUREMENT DISCIPLINE (mirrors qa_chain_speed.py, commit 446ff0d): MINIMAL
UDP reads -- endpoints only over a >= 8 s window. Heavy read bursts stall the
emulator and fake a low tick reading (MEASURED).

This is a LIVE memory-read gate (RetroArch UDP 55355). Addresses re-resolved
from game.map each run (they shift per build). The chain reaches AIZ ~81 s
after boot.

Usage:
  python tools/qa_aiz_speed.py [--min 50] [--max-io 1.0] [--window 8]
                               [--boot-timeout 300] [--no-launch]
Exit 0 = GREEN, 1 = RED (slow / CD-bound), 2 = harness error.
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
    ap.add_argument("--min", type=float, default=50.0,
                    help="min p6_w_tick_frames advance/emulated-sec (vblank-anchored) for GREEN")
    ap.add_argument("--max-io", type=float, default=1.0,
                    help="max p6_w_gfs_io_vbl delta per rendered frame")
    ap.add_argument("--max-seam-fills", type=int, default=15,
                    help="max GFS fills during the Menu->AIZ seam storm "
                         "(RED baseline 23 on HEAD c4ad000)")
    ap.add_argument("--window", type=float, default=8.0)
    ap.add_argument("--boot-timeout", type=float, default=300.0)
    ap.add_argument("--map", default=str(_ROOT / "game.map"))
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=55355)
    ap.add_argument("--no-launch", action="store_true")
    args = ap.parse_args()

    map_text = Path(args.map).read_text(errors="replace")
    proc = None
    if not args.no_launch:
        boot = _HERE / "_gl_boot.ps1"
        print("[qa_aiz_speed] launching GL RetroArch via", boot)
        proc = subprocess.Popen(
            ["pwsh", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(boot)],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        rd = qa_trace.Reader(True, None, args.host, args.port)

        def sym(n):
            return rd.sym(map_text, n)

        a_csf  = sym("RSDK::currentSceneFolder")
        a_cont = sym("p6_w_cont_frames")
        a_tick = sym("p6_w_tick_frames")
        a_iov  = sym("p6_w_gfs_io_vbl")
        a_fill = sym("p6_w_gfs_fills")
        a_sht  = sym("p6_w_sht_fetches")
        a_vbl  = sym("p6_w_perf_vblanks")
        if a_tick is None or a_csf is None or a_cont is None or a_iov is None:
            print("[HARNESS] missing symbol(s): tick=%s csf=%s cont=%s io_vbl=%s"
                  % (a_tick, a_csf, a_cont, a_iov), file=sys.stderr)
            return 2
        if a_vbl is None:
            print("[HARNESS] p6_w_perf_vblanks symbol absent -- cannot measure "
                  "throttle-independent speed", file=sys.stderr)
            return 2
        # #302 attribution witnesses (optional -- older maps lack them).
        attrib = {n: sym("p6_w_aiz_vbl_" + n)
                  for n in ("objtick", "fgpresent", "bgstream", "vdp1emit", "framesum")}

        def folder():
            try:
                return rd.mem.read_saturn(a_csf, 16).split(b"\0")[0].decode(errors="replace")
            except Exception:
                return None

        # Wait for AIZ. One folder read per second = negligible load.
        # If we just launched, give the relaunch time to kill the previous RA
        # (its UDP responder answers with the OLD scene until then).
        if proc is not None:
            time.sleep(15.0)
        t0 = time.time()
        reached = False
        while time.time() - t0 < args.boot_timeout:
            f = folder()
            if f == "AIZ":
                reached = True
                break
            if f == "GHZ" and args.no_launch:
                print("[HARNESS] scene already past AIZ (folder=GHZ) -- reboot needed",
                      file=sys.stderr)
                return 2
            time.sleep(1.0)
        if not reached:
            print("[HARNESS] never reached AIZ (folder=%s) in %.0fs"
                  % (folder(), args.boot_timeout), file=sys.stderr)
            return 2
        # ---- Phase 1: the Menu->AIZ seam CD storm (tick frozen, GFS fills).
        # Light polling (3 reads / 0.5 s) until the game starts TICKING (load
        # done) -- measures the storm my fix targets: fills + io_vbl + wall.
        # RED baseline (HEAD c4ad000, tools/_aiz_cdlog.txt): 23 fills,
        # io_vbl +285 (4.75 s CD), ~8.3 s wall.
        seam_f0 = rd.r32(a_fill) if a_fill else None
        seam_io0 = rd.r32(a_iov)
        seam_t0 = time.time()
        tick_prev = rd.r32(a_tick)
        while time.time() - seam_t0 < 90.0:
            time.sleep(0.5)
            tk = rd.r32(a_tick)
            if tk is not None and tick_prev is not None and tk > tick_prev + 5:
                break  # ticking resumed = seam load over
            tick_prev = tk if tk is not None else tick_prev
        seam_wall = time.time() - seam_t0
        seam_f1 = rd.r32(a_fill) if a_fill else None
        seam_io1 = rd.r32(a_iov)
        seam_fills = (seam_f1 - seam_f0) if (seam_f0 is not None and seam_f1 is not None) else None
        seam_io = (seam_io1 - seam_io0) if (seam_io0 is not None and seam_io1 is not None) else None

        time.sleep(2.0)  # let the intro settle into the play phase
        if folder() != "AIZ":
            print("[HARNESS] left AIZ during settle (folder=%s)" % folder(), file=sys.stderr)
            return 2

        # ---- Phase 2: play-phase window. Endpoint reads ONLY (minimal-read
        # discipline -- heavy UDP bursts stall the emulator and fake a low tick).
        tk0 = rd.r32(a_tick); c0 = rd.r32(a_cont); io0 = rd.r32(a_iov)
        v0 = rd.r32(a_vbl)
        fl0 = rd.r32(a_fill) if a_fill else None
        sh0 = rd.r32(a_sht) if a_sht else None
        at0 = {n: (rd.r32(a) if a else None) for n, a in attrib.items()}
        w0 = time.time()
        time.sleep(args.window)
        tk1 = rd.r32(a_tick); c1 = rd.r32(a_cont); io1 = rd.r32(a_iov)
        v1 = rd.r32(a_vbl)
        fl1 = rd.r32(a_fill) if a_fill else None
        sh1 = rd.r32(a_sht) if a_sht else None
        at1 = {n: (rd.r32(a) if a else None) for n, a in attrib.items()}
        w1 = time.time()
        f_end = folder()

        wall = w1 - w0
        dt  = (tk1 - tk0) if (tk0 is not None and tk1 is not None) else None
        dc  = (c1 - c0) if (c0 is not None and c1 is not None) else None
        dio = (io1 - io0) if (io0 is not None and io1 is not None) else None
        dv  = (v1 - v0) if (v0 is not None and v1 is not None) else None
        # PRIMARY: ticks per EMULATED second (vblank-anchored, throttle-free).
        tick_ps = (dt * 60.0 / dv) if (dt is not None and dv and dv > 0) else None
        cont_ps = (dc * 60.0 / dv) if (dc is not None and dv and dv > 0) else None
        throttle = (dv / 60.0 / wall) if (dv is not None and wall > 0) else None
        if throttle is not None and (throttle < 0.5 or throttle > 1.5):
            print("[note] emulator pacing = %.2fx realtime (wall-based numbers "
                  "would lie by that factor; vblank anchor corrects for it)" % throttle)
        io_pf   = (dio / dc) if (dio is not None and dc and dc > 0) else None
        fill_pf = ((fl1 - fl0) / dc) if (fl0 is not None and fl1 is not None
                                         and dc and dc > 0) else None
        sht_pf  = ((sh1 - sh0) / dc) if (sh0 is not None and sh1 is not None
                                         and dc and dc > 0) else None
        speed_pct = (tick_ps / 60.0 * 100.0) if tick_ps is not None else None

        print("=" * 70)
        print("AIZ INTRO GAME-SPEED + CD-I/O gate (%.1fs wall window, folder end=%s)"
              % (wall, f_end))
        print("  [seam] Menu->AIZ storm: fills=%s io_vbl=%s (%.2fs CD) wall=%.1fs  (max-fills %d)"
              % (seam_fills, seam_io,
                 (seam_io / 60.0) if seam_io is not None else -1, seam_wall,
                 args.max_seam_fills))
        print("  p6_w_tick_frames /emulated-sec = %s  (min %.0f, target 60) [PRIMARY]"
              % (("%.1f" % tick_ps) if tick_ps is not None else "?", args.min))
        print("  game speed                  = %s%% of realtime"
              % (("%.0f" % speed_pct) if speed_pct is not None else "?"))
        print("  gfs_io_vbl / rendered frame = %s  (max %.1f) [SECONDARY]"
              % (("%.2f" % io_pf) if io_pf is not None else "?", args.max_io))
        print("  gfs_fills / rendered frame  = %s"
              % (("%.2f" % fill_pf) if fill_pf is not None else "?"))
        print("  sht_fetches / rendered frame= %s"
              % (("%.2f" % sht_pf) if sht_pf is not None else "?"))
        print("  render fps (cont/emulated-sec) = %s"
              % (("%.1f" % cont_ps) if cont_ps is not None else "?"))
        # ---- #302 attribution table (vbl/frame per frontend-frame section) ----
        d_at = {n: ((at1[n] - at0[n]) if (at0[n] is not None and at1[n] is not None)
                    else None) for n in attrib}
        if dc and dc > 0 and dv is not None and all(v is not None for v in d_at.values()):
            per = {n: d_at[n] / dc for n in d_at}
            total_pf = dv / dc
            sections = per["objtick"] + per["fgpresent"] + per["bgstream"] + per["vdp1emit"]
            other_in = per["framesum"] - sections
            jo_body  = total_pf - per["framesum"]
            print("  -- #302 AIZ frame attribution (vbl/frame; total %.2f = %.1f fps) --"
                  % (total_pf, 60.0 / total_pf if total_pf > 0 else 0))
            print("     objtick   (ProcessObjects x catch-up) = %6.2f" % per["objtick"])
            print("     fgpresent (FG-Low NBG1 present)       = %6.2f" % per["fgpresent"])
            print("     bgstream  (3x AIZ BG stream + frame)  = %6.2f" % per["bgstream"])
            print("     vdp1emit  (DrawLists + direct list)   = %6.2f" % per["vdp1emit"])
            print("     other-in-frame (input/arm/witness)    = %6.2f" % other_in)
            print("     jo-body   (slSynch + VDP1/vbl wait)   = %6.2f" % jo_body)
        else:
            print("  -- #302 attribution witnesses absent on this build (older map) --")
        print("=" * 70)

        if f_end != "AIZ":
            print("[HARNESS] scene changed mid-window (folder=%s) -- rerun" % f_end,
                  file=sys.stderr)
            return 2
        if tick_ps is None:
            print("RED: could not read tick_frames")
            return 1
        cd_red = []
        if io_pf is not None and io_pf > args.max_io:
            cd_red.append("CD-I/O wait %.2f vbl/frame > max %.1f (per-frame GFS reads)"
                          % (io_pf, args.max_io))
        if seam_fills is not None and seam_fills > args.max_seam_fills:
            cd_red.append("Menu->AIZ seam storm %d fills > max %d"
                          % (seam_fills, args.max_seam_fills))
        tick_red = tick_ps < args.min
        for r in cd_red:
            print("RED:", r)
        if tick_red:
            print("RED: game logic %.1f Hz < min %.0f (%.0f%% realtime)"
                  % (tick_ps, args.min, speed_pct))
        if cd_red:
            return 1
        if tick_red:
            # CD axes GREEN but the play phase is still slow (render/draw-bound,
            # NOT CD -- measured io_vbl flat during play). Distinct exit code so
            # the caller reports the remaining bottleneck honestly.
            print("CD-GREEN/TICK-RED: seam fills=%s, play CD-I/O %.2f vbl/frame -- "
                  "remaining slowness is the render wall, not CD"
                  % (seam_fills, io_pf if io_pf is not None else -1))
            return 4
        print("GREEN: AIZ at %.1f tick/s (%.0f%% realtime), CD-I/O %.2f vbl/frame, "
              "seam fills=%s" % (tick_ps, speed_pct,
                                 io_pf if io_pf is not None else -1, seam_fills))
        return 0
    finally:
        if proc is not None:
            # leave RA running for follow-up probes; the caller owns teardown
            pass


if __name__ == "__main__":
    sys.exit(main())
