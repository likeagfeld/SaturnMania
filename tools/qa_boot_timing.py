#!/usr/bin/env python3
# =============================================================================
# qa_boot_timing.py -- Task #322 step 1: MEASURE + GATE the BOOT-TO-TITLE stall.
#
# The user reports long loading times across the whole experience. The parent
# MEASURED the chain build's boot arc live at the correct emulator speed:
# ~12.5 s from RetroArch launch to the first Logos render (the #184 "12+s
# light-blue screen before the title"). This gate:
#   1. Launches GL RetroArch (observe cfg -- real 1x emulated speed, same as the
#      parent's measurement) on game.cue.
#   2. Polls RSDK::currentSceneFolder over UDP every ~0.25 s, recording the
#      wall-clock second at which it first becomes "Logos" (== first render).
#   3. Reads the p6_w_lt_* front-end load-timing witnesses (already compiled in
#      the P6_FRONTEND_LOGOS/CHAIN flavor, Task #271) LIVE and prints the
#      per-sub-step ms breakdown so the DOMINANT boot sub-step is localized.
#   4. Asserts wall-time-to-Logos < a floor. RED now (~12.5 s), GREEN after the
#      fix (floor set from the localized breakdown).
#
# This is a WALL-TIME gate (matches the parent's method) -- NOT a savestate
# snapshot (Mednafen F5 capture is broken; the witnesses are read live). The
# load-timing witnesses are zero-per-frame (they are set once during the masked
# load core + phase-2 and then never touched), so reading them costs nothing and
# cannot inflate .bss (they already exist in the shipping front-end map).
#
#   python tools/qa_boot_timing.py [--floor 7.0] [--timeout 40] [--no-boot]
#                                  [--map game.map] [--host 127.0.0.1] [--port 55355]
#
# Exit 0 = GREEN (folder reached "Logos" under the floor); 1 = RED (over floor or
# never reached); 2 = harness error (RA not up / witnesses absent).
# =============================================================================
from __future__ import annotations

import argparse
import importlib.util
import os
import re
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent

# Boot-phase WRAM garbage can decode to non-cp1252 bytes; never let a print crash
# the gate.
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(errors="backslashreplace")  # type: ignore[union-attr]
    except Exception:
        pass


def _load(mod, fn):
    spec = importlib.util.spec_from_file_location(mod, HERE / fn)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)  # type: ignore[union-attr]
    return m


qa_netmem = _load("qa_netmem", "qa_netmem.py")

VBL_MS = 1000.0 / 60.0  # one vblank = 16.667 ms (NTSC 320 non-interlace)

# Sub-step labels (index 1..10) matching qa_frontend_loadtime.py / the p6_lt_mark
# brackets in p6_io_main.cpp. Regime: 'M' masked core (interrupts off, vbl frozen);
# 'P' phase-2 (unmasked, vbl exact).
STEPS = [
    (1,  "M", "boot pre-load (memsets + InitStorage)"),
    (2,  "M", "chain loads (OVLRING/DORM/LAYT/ANIMPACK/GHZ Player sheets)"),
    (3,  "M", "512x512 sheets (LOGOS.SHT + TLOGO.SHT load+stage+resident)"),
    (4,  "M", "TSONIC.SHT (1024x1024, ~121 KB) load+stage"),
    (5,  "M", "LoadDataPack (DATA.RSDK 182 MB windowed walk)"),
    (6,  "M", "AudioDevice::Init + ScoreAdd + MenuBleep SFX"),
    (7,  "M", "LoadGameConfig (GameConfig.bin parse)"),
    (8,  "P", "LoadSceneFolder (Logos scene + TileConfig)"),
    (9,  "P", "LoadSceneAssets (sprite-sheet decode/stage)"),
    (10, "P", "InitObjects (entity Create/StageLoad) + arm_env VDP1 binds"),
]


def frt_khz(cks):
    if cks is None or cks < 0:
        cks = 1  # default observed = /32
    return 26800.0 / (8 << (2 * cks))  # kHz


def sym(map_text, name):
    m = re.search(r"0x([0-9a-fA-F]{16})\s+" + re.escape(name) + r"\s*$", map_text, re.M)
    return int(m.group(1), 16) if m else None


def s32(v):
    return v - 0x100000000 if (v is not None and v >= 0x80000000) else v


def read_folder(mem, csf_addr):
    if csf_addr is None:
        return None
    try:
        raw = mem.read_saturn(csf_addr, 16)
        s = raw.split(b"\0")[0]
        # Only treat as a real scene folder if it's printable ASCII (pre-boot the
        # WRAM holds garbage that decodes to control bytes).
        if s and all(0x20 <= b < 0x7F for b in s):
            return s.decode("ascii")
        return None
    except Exception:
        return None


def read_i32(mem, addr):
    if addr is None:
        return None
    try:
        return s32(mem.read32_saturn(addr))
    except Exception:
        return None


def read_arr(mem, base, n):
    return [read_i32(mem, (base + 4 * i) & 0xFFFFFFFF) if base else None for i in range(n)]


def launch_gl():
    """Launch GL RetroArch (observe cfg) at real 1x speed -- matches the parent's
    boot-timing measurement. Returns the launch monotonic timestamp."""
    RA = ROOT / "tools" / "retroarch" / "RetroArch-Win64"
    subprocess.run(["taskkill", "/F", "/IM", "retroarch.exe"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.0)
    t0 = time.monotonic()
    subprocess.Popen(
        [str(RA / "retroarch.exe"),
         "--config", str(RA / "retroarch.cfg"),
         "--appendconfig", str(RA / "observe_override.cfg"),
         "-L", str(RA / "cores" / "mednafen_saturn_libretro.dll"),
         str(ROOT / "game.cue")],
        cwd=str(RA))
    return t0


def print_breakdown(mem, map_text):
    """Read the p6_w_lt_* witnesses live and print the per-sub-step ms breakdown.
    Mirrors qa_frontend_loadtime.py's math but over the live UDP read path."""
    cks = read_i32(mem, sym(map_text, "p6_w_lt_cks"))
    khz = frt_khz(cks)
    base_vbl   = sym(map_text, "p6_w_lt_vbl")
    base_fills = sym(map_text, "p6_w_lt_fills")
    base_kb    = sym(map_text, "p6_w_lt_kb")
    base_frt   = sym(map_text, "p6_w_lt_frt")
    if not (base_vbl and base_fills and base_frt):
        print("  (load-timing witnesses absent from the map -- not a front-end flavor?)")
        return None

    vbl   = read_arr(mem, base_vbl,   11)
    fills = read_arr(mem, base_fills, 11)
    kb    = read_arr(mem, base_kb,    11)
    frt   = read_arr(mem, base_frt,   11)

    io_vbl    = read_i32(mem, sym(map_text, "p6_w_gfs_io_vbl"))
    ph2_fills = read_i32(mem, sym(map_text, "p6_w_lt_ph2_fills"))
    seeks     = read_i32(mem, sym(map_text, "p6_w_gfs_seeks_real"))
    tot_fills = read_i32(mem, sym(map_text, "p6_w_gfs_fills"))
    tot_kb    = read_i32(mem, sym(map_text, "p6_w_gfs_bytes"))
    tot_kb    = (tot_kb // 1024) if tot_kb else None

    ms_per_fill = None
    if io_vbl is not None and ph2_fills and ph2_fills > 0:
        ms_per_fill = (io_vbl * VBL_MS) / ph2_fills
    if not ms_per_fill or ms_per_fill <= 0:
        ms_per_fill = 83.0  # #251 measured emulated-CD per-fill latency fallback

    print("  FRT: CKS=%s -> %.1f kHz  |  GFS total fills=%s seeks_real=%s KB=%s"
          % (cks, khz, tot_fills, seeks, tot_kb))
    print("  phase-2 windowed I/O: io_vbl=%s (%.2f s) / ph2_fills=%s -> %.0f ms/fill"
          % (io_vbl, (io_vbl or 0) * VBL_MS / 1000.0, ph2_fills, ms_per_fill))
    print("  -> sizing masked I/O sub-steps at %.0f ms/fill" % ms_per_fill)
    print("  %-4s %-3s %-6s %-6s %-6s %-9s %s" % ("step", "reg", "vbl", "fills", "kb", "ms", "what"))
    rows = []
    total_ms = 0.0
    for idx, reg, lbl in STEPS:
        v, f, k, t = vbl[idx], fills[idx], kb[idx], frt[idx]
        if reg == "P":
            ms = (v or 0) * VBL_MS
        else:
            ms = (f or 0) * ms_per_fill if (f or 0) > 0 else (t or 0) / khz
        total_ms += ms
        rows.append((idx, reg, lbl, ms, f))
        print("  S%-3d %-3s %-6s %-6s %-6s %8.0f  %s"
              % (idx, reg, v, f, k, ms, lbl))
    print("  " + "-" * 60)
    print("  measured load core TOTAL = %.0f ms (%.2f s)" % (total_ms, total_ms / 1000.0))
    if total_ms > 0:
        dom = max(rows, key=lambda r: r[3])
        print("  DOMINANT: S%d (%s) = %.0f ms (%.0f%% of the measured load), fills=%s"
              % (dom[0], dom[2], dom[3], 100.0 * dom[3] / total_ms, dom[4]))
    return total_ms


def main(argv=None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--floor", type=float, default=7.0,
                   help="RED if wall-seconds-to-Logos >= this (RED baseline ~12.5)")
    p.add_argument("--timeout", type=float, default=40.0,
                   help="give up if Logos not reached within this many wall seconds")
    p.add_argument("--poll", type=float, default=0.25)
    p.add_argument("--no-boot", action="store_true",
                   help="don't launch RA; measure against an already-running core (t=0 now)")
    p.add_argument("--map", default=str(ROOT / "game.map"))
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=55355)
    a = p.parse_args(argv)

    if not os.path.isfile(a.map):
        print("qa_boot_timing: ERROR map missing (%s)" % a.map); return 2
    map_text = Path(a.map).read_text(errors="replace")
    csf = sym(map_text, "RSDK::currentSceneFolder")
    if csf is None:
        print("qa_boot_timing: ERROR currentSceneFolder not in map"); return 2

    print("=" * 74)
    print("BOOT-TO-TITLE TIMING GATE (Task #322 step 1)   floor=%.1fs" % a.floor)
    print("=" * 74)

    t0 = launch_gl() if not a.no_boot else time.monotonic()
    mem = qa_netmem.RetroMem(a.host, a.port, 1.0)

    reached = None
    last_folder = "<none>"
    deadline = t0 + a.timeout
    while time.monotonic() < deadline:
        f = read_folder(mem, csf)
        if f and f != last_folder:
            print("  t+%5.2fs  folder -> %r" % (time.monotonic() - t0, f))
            last_folder = f
        if f == "Logos":
            reached = time.monotonic() - t0
            break
        time.sleep(a.poll)

    print("-" * 74)
    if reached is None:
        print("  RESULT: folder never reached 'Logos' within %.0fs (last=%r)"
              % (a.timeout, last_folder))
        print("  [ RED ] boot did not render the first scene.")
        return 1

    print("  wall-time RA-launch -> first Logos render = %.2f s" % reached)
    print("-" * 74)
    # Give the load core a beat to finish populating the witnesses (folder flips
    # to Logos as phase-2 arms; the lt slots are already set by then, but be safe).
    time.sleep(0.5)
    print("LOAD SUB-STEP BREAKDOWN (live p6_w_lt_* witnesses):")
    print_breakdown(mem, map_text)
    print("=" * 74)
    if reached >= a.floor:
        print("  [ RED ] %.2f s >= floor %.1f s -- boot stall NOT fixed." % (reached, a.floor))
        return 1
    print("  [GREEN] %.2f s < floor %.1f s -- boot-to-title within budget." % (reached, a.floor))
    return 0


if __name__ == "__main__":
    sys.exit(main())
