#!/usr/bin/env python3
"""qa_p6_fg_settile.py -- FG-tile-mutation piece 1 gate (BreakableWall/
CollapsingPlatform keystone).

MEASURED PROBLEM (2026-07-12): on the Saturn P6 engine the camera-STREAMED FG
layer has layout==NULL (it sources from the SaturnLayout band store, not a WRAM
array), so RSDK.SetTile(-1) -- the core of BreakableWall (remove broken tiles)
and CollapsingPlatform (via BreakableWall) -- was silently DROPPED
(SaturnLayerSetTile bumped p6_saturn_settile_drops). The tiles never became
empty, so a broken wall/floor stayed solid (Collision.cpp reads the same store
via RSDK_LAYER_TILE) AND stayed drawn.

FIX (piece 1, data-side): SaturnLayout_SetTile mutates the cached window(s) +
the resident store so GetTile reflects the write. Because Collision.cpp reads
tiles via the same store, this ALONE makes a broken wall/floor passable. (The
VDP2 static map is a separate cache -> the removed tile stays DRAWN until a map
rebuild; that visual punch is a later piece.)

GATE: at GHZ scene load the engine runs a self-restoring round trip on a
streamed FG tile -- GetTile(orig) -> SetTile(orig^0x0ABC) -> GetTile(seen) ->
SetTile(orig). It writes:
  p6_w_lay_settile_ok  int32  -1 not run; 0 RED (drop, seen!=sentinel); 1 GREEN
  p6_w_lay_settile_rt  int32  (orig<<16)|seen  forensics

RED baseline = the build BEFORE SaturnLayout_SetTile existed (SetTile dropped ->
seen==orig != sentinel -> ok=0). GREEN = ok==1.

Reads live via RetroArch (UDP 55355) -- boot the GHZ/chain build with
tools/qa_live.ps1 first. The witness is set once at GHZ load and latches, so the
poll just waits for it to leave -1.

Usage:
  python tools/qa_p6_fg_settile.py [--map game.map] [--secs 200]
Exit 0 = GREEN, 1 = RED, 2 = harness/parse error.
"""
from __future__ import annotations

import argparse
import importlib.util
import re
import sys
import time
from pathlib import Path

_HERE = Path(__file__).resolve().parent


def _load(mod, fn):
    spec = importlib.util.spec_from_file_location(mod, _HERE / fn)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)  # type: ignore[union-attr]
    return m


def map_symbol(map_text: str, name: str) -> int:
    m = re.search(r"0x([0-9a-fA-F]{16})\s+" + re.escape(name) + r"\s*$", map_text, re.M)
    if not m:
        raise KeyError(name)
    return int(m.group(1), 16)


def main(argv=None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--map", default=str(_HERE.parent / "game.map"))
    p.add_argument("--secs", type=float, default=200.0)
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=55355)
    a = p.parse_args(argv)

    qa_netmem = _load("qa_netmem", "qa_netmem.py")
    try:
        mt = Path(a.map).read_text(errors="replace")
        sym_ok = map_symbol(mt, "p6_w_lay_settile_ok")
        sym_rt = map_symbol(mt, "p6_w_lay_settile_rt")
        sym_folder = map_symbol(mt, "RSDK::currentSceneFolder")
    except KeyError as e:
        sys.stderr.write(f"qa_p6_fg_settile: symbol missing from map: {e}\n")
        return 2
    except FileNotFoundError:
        sys.stderr.write(f"qa_p6_fg_settile: map not found: {a.map}\n")
        return 2

    mem = qa_netmem.RetroMem(a.host, a.port, timeout=2.0)

    def r32(addr: int):
        v = mem.read32_saturn(addr)
        return v - 0x100000000 if v >= 0x80000000 else v

    def folder() -> str:
        try:
            return mem.read_saturn(sym_folder, 16).split(b"\0", 1)[0].decode("latin1", "replace")
        except Exception:
            return ""

    t0 = time.time()
    last = 0.0
    ok = -1
    while time.time() - t0 < a.secs:
        try:
            ok = r32(sym_ok)
        except Exception:
            ok = -1
        if ok in (0, 1):
            break
        now = time.time()
        if now - last > 3.0:
            print(f"  [{now - t0:5.1f}s] folder={folder()!r} settile_ok={ok} (waiting for GHZ load)")
            last = now
        time.sleep(0.1)

    if ok not in (0, 1):
        sys.stderr.write("qa_p6_fg_settile: witness never left -1 within "
                         f"{a.secs:.0f}s (did the build reach GHZ load? is RA running?)\n")
        return 2
    try:
        rt = r32(sym_rt) & 0xFFFFFFFF
    except Exception:
        rt = 0
    orig, seen = (rt >> 16) & 0xFFFF, rt & 0xFFFF
    print(f"  settile round-trip: orig=0x{orig:04X} seen=0x{seen:04X} "
          f"sentinel=0x{(orig ^ 0x0ABC) & 0xFFFF:04X}")
    print(f"  p6_w_lay_settile_ok = {ok}  "
          f"({'GREEN: SetTile reflected in GetTile' if ok == 1 else 'RED: SetTile dropped (streamed FG)'})")
    print(f"qa_p6_fg_settile: -> {'GREEN' if ok == 1 else 'RED'}")
    return 0 if ok == 1 else 1


if __name__ == "__main__":
    sys.exit(main())
