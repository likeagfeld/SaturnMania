#!/usr/bin/env python3
"""qa_aiz_load.py -- task #322 RED->GREEN gate for the Menu->AIZ CD-seek stall.

Boots the front-end chain live (RetroArch + Beetle Saturn over UDP 55355), polls
RSDK::currentSceneFolder, and snapshots the shipping GFS witnesses
(p6_w_gfs_seeks_real, p6_w_gfs_io_vbl) at two moments:
  - MENU  : the last sample while folder != "AIZ" (Menu settled, pre-AIZ-load)
  - AIZ   : the first sample where folder == "AIZ" (AIZ scene loaded + armed)
The delta (AIZ - MENU) is the Menu->AIZ scene load's CD cost.

RED on the current build: seeks_real delta ~57, io_vbl delta ~780 (=13s @60Hz).
GREEN after the fix: seeks_real delta < SEEK_FLOOR and io_vbl delta < IOVBL_FLOOR.

This is a LIVE gate (temporal load question -> live is mandatory per
memory/live-memory-mandatory-for-temporal). Run RetroArch on game.cue first
(tools/_gl_boot.ps1); this script attaches and drives the poll.

Exit 0 = GREEN (pass). Exit 1 = RED (fail / above floor). Exit 2 = harness fault.
"""
import argparse
import importlib.util
import re
import sys
import time
from pathlib import Path

# Windows consoles default to cp1252; the folder string can carry non-decodable
# bytes during early boot. Make stdout tolerate them so a print never crashes the gate.
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(errors="backslashreplace")  # type: ignore[union-attr]
    except Exception:
        pass

_HERE = Path(__file__).resolve().parent


def _load(mod, fn):
    spec = importlib.util.spec_from_file_location(mod, _HERE / fn)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


qa_netmem = _load("qa_netmem", "qa_netmem.py")

# Floors (task #322). MEASURED live via P6_GFS_OPENTRACE, AIZ direct-boot (P6_AIZ_TEST):
#   BASELINE (pre-fix): 40 real GFS_Seeks, io_vbl 414 (~6.9 s CD) for the AIZ LoadScene.
#   FIXED (AIZANIM.PAK Global+decoration+Knux resident): 23 seeks, io_vbl 304 (~5.1 s).
# The remaining 23 = 5 engine-intrinsic (GameConfig x2/Scene1/StageConfig/TileConfig) +
# 4 SFX .wav + 2 Player .bin (Sonic/Tails, deliberately unpacked -- they overflow the
# 69,632 B P6_HW_ANIMPAK window) + ~9 uncatalogued GIF-sheet/SFX opens (LoadSpriteSheet,
# not anim-packable). None are cheaply removable without sheet-staging or a window
# re-carve. Floors sized to fire RED on the 40-seek baseline, GREEN on the 23-seek fix.
SEEK_FLOOR = 30     # GREEN if AIZ-load real-seeks delta < this (baseline 40 -> RED; fix 23 -> GREEN)
IOVBL_FLOOR = 360   # GREEN if io_vbl delta < this (~6s; baseline 414/~6.9s -> RED; fix 304/~5.1s -> GREEN)


def sym(map_text, name):
    m = re.search(r"0x0*([0-9a-fA-F]{8,16})\s+" + re.escape(name) + r"\s*$", map_text, re.M)
    return int(m.group(1), 16) if m else None


def read_folder(mem, addr):
    try:
        return mem.read_saturn(addr, 16).split(b"\0")[0].decode(errors="replace")
    except Exception:
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=55355)
    ap.add_argument("--map", default=str(_HERE.parent / "game.map"))
    ap.add_argument("--timeout", type=float, default=180.0, help="wall seconds to reach AIZ")
    ap.add_argument("--poll", type=float, default=1.0)
    a = ap.parse_args()

    map_text = Path(a.map).read_text(errors="replace")
    a_seeks = sym(map_text, "p6_w_gfs_seeks_real")
    a_iovbl = sym(map_text, "p6_w_gfs_io_vbl")
    a_folder = sym(map_text, "RSDK::currentSceneFolder")
    if not (a_seeks and a_iovbl and a_folder):
        sys.stderr.write(f"qa_aiz_load: missing symbols seeks={a_seeks} iovbl={a_iovbl} folder={a_folder}\n")
        return 2

    mem = qa_netmem.RetroMem(a.host, a.port, 2.0)

    # Works for BOTH entry paths:
    #  - AIZ direct-boot (P6_AIZ_TEST): pre-AIZ folder is '' (empty); the AIZ load's
    #    seeks are ~everything after boot -> pre snapshot is the last empty-folder sample.
    #  - Menu->AIZ (chain): pre snapshot is the settled Menu sample just before the hop.
    # After folder flips to AIZ, the LoadScene keeps seeking for ~15 s -> wait for the
    # seek counter to STOP climbing (settle) before taking the post snapshot.
    pre_seeks = pre_iovbl = None
    saw_pre = False
    t0 = time.time()
    while time.time() - t0 < a.timeout:
        folder = read_folder(mem, a_folder)
        seeks = mem.read32_saturn(a_seeks)
        iovbl = mem.read32_saturn(a_iovbl)
        print(f"t={time.time()-t0:6.1f}s folder={folder!r} seeks_real={seeks} io_vbl={iovbl}")
        if folder == "AIZ":
            if not saw_pre:
                # direct-boot poll landed after the flip: use 0 baseline (cold boot).
                pre_seeks = pre_iovbl = 0
            # Settle-wait: poll until seeks stops increasing for 3 consecutive reads.
            stable = 0
            last = seeks
            while stable < 3 and time.time() - t0 < a.timeout:
                time.sleep(2.0)
                s2 = mem.read32_saturn(a_seeks)
                stable = stable + 1 if s2 == last else 0
                last = s2
            fin_seeks = mem.read32_saturn(a_seeks)
            fin_iovbl = mem.read32_saturn(a_iovbl)
            d_seeks = fin_seeks - pre_seeks
            d_iovbl = fin_iovbl - pre_iovbl
            print(f"\nAIZ-load delta: seeks_real={d_seeks}  io_vbl={d_iovbl} (~{d_iovbl/60.0:.1f}s CD)")
            ok = (d_seeks < SEEK_FLOOR) and (d_iovbl < IOVBL_FLOOR)
            verdict = "GREEN" if ok else "RED"
            print(f"floors: seeks<{SEEK_FLOOR} io_vbl<{IOVBL_FLOOR}  =>  {verdict}")
            return 0 if ok else 1
        # Track the latest pre-AIZ snapshot (empty/menu/title just before the load).
        pre_seeks, pre_iovbl = seeks, iovbl
        saw_pre = True
        time.sleep(a.poll)

    sys.stderr.write("qa_aiz_load: timed out before folder==AIZ\n")
    return 2


if __name__ == "__main__":
    sys.exit(main())
