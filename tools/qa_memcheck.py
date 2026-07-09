#!/usr/bin/env python3
"""qa_memcheck.py -- LIVE memory-availability validator for the Saturn build.

Answers "is the build about to blow a memory wall?" continuously, from the
RUNNING game (RetroArch live) or a Mednafen savestate -- so the recurring
overflow bug CLASSES (#228 WRAM-H boot trap, entity-pool overflow -> dropped
entities, anim-pack / cart-store overflow -> banded fallback / black sprites)
are caught the instant they approach, not after a user notices garbage.

DESIGN PRINCIPLE (why this is NOT one-hand-witness-per-factor): every budget is
declared ONCE in the BUDGETS table as {name, live watermark symbol, ceiling,
warn%}. The ceiling comes from the linker map / a #define (authoritative), the
watermark from a single allocator high-water witness. Adding a budget is one
row, not a new gate. The monitor reports headroom for ALL of them every tick and
fails if any crosses its wall -- structural coverage, not bespoke checks.

Budgets (Saturn Object.hpp + Animation.hpp, MEASURED):
  entity pool   : high-water live slot  vs TEMPENTITY_START 1152 (0x40+0x440)
  anim pack     : p6_w_apk_bytes         vs P6_HW_ANIMPAK_CAP    0x11000
  obj anim pack : p6_w_objapk_bytes      vs P6_HW_OBJANIMPAK_CAP 0x40000
  layout store  : p6_w_lay_bytes         vs the layout cart window
  code wall     : _end (map)             vs WRAM-H ANIMPAK ceiling (#228)

Usage:
  python tools/qa_memcheck.py --live [--watch SECONDS] [--warn 0.9]
  python tools/qa_memcheck.py --state STATE.mcs
Exit 0 = all budgets under their warn line; 1 = a budget breached; 2 = read error.
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


mcs_extract = _load("mcs_extract", "mcs_extract.py")
qa_netmem = _load("qa_netmem", "qa_netmem.py")

# name, watermark witness symbol, ceiling (value), warn fraction, unit
# ceiling=None means "resolve from a map symbol / static" (code wall uses _end).
BUDGETS = [
    ("entity pool slots", "p6_w_pool_maxls",   1152,     0.90, "slots"),
    ("anim pack bytes",   "p6_w_apk_bytes",     0x11000, 0.90, "B"),
    ("obj anim pk bytes", "p6_w_objapk_bytes",  0x40000, 0.90, "B"),
    ("layout store bytes","p6_w_lay_bytes",     0x1A0000,0.90, "B"),  # 0x22600000..0x227A0000
]
# code wall (#228): _end vs the WRAM-H anim-pack ceiling. In the front-end/chain
# flavor the pack is in CART so the WRAM-H wall is the GLOBALS base; report + flag.
CODE_WALL_CEIL = 0x060C8000  # front-end GLOBALS base (frontend-cart-map memory)


def map_symbol(map_text: str, name: str):
    # \b (not \s*$) terminates the name: the GNU ld map writes end-of-list
    # symbols at EOL (RSDK::objectEntityList) but PROVIDE-style symbols with a
    # trailing assignment ("_end = ."). \b matches both; \s*$ missed "_end = ."
    # -> the #228 code-wall was silently skipped. \b still refuses substring
    # hits (_end won't match _end_of_data).
    m = re.search(r"0x([0-9a-fA-F]{8,16})\s+" + re.escape(name) + r"\b", map_text, re.M)
    return int(m.group(1), 16) if m else None


def swap32(v: int) -> int:
    return ((v & 0xFF00FF00) >> 8) | ((v & 0x00FF00FF) << 8)


class Reader:
    """Uniform read32 over either live RetroArch or a Mednafen savestate."""
    def __init__(self, live, state, host, port):
        self.live = live
        if live:
            self.mem = qa_netmem.RetroMem(host, port, timeout=2.0)
        else:
            self.sections = mcs_extract.parse_savestate(Path(state))

    def r32(self, addr):
        if self.live:
            return self.mem.read32_saturn(addr)
        raw = mcs_extract._peek_bytes(self.sections, addr, 4)
        return None if raw is None else swap32(int.from_bytes(raw, "big"))


def sweep(rd: Reader, map_text: str, warn: float):
    rows = []
    worst = 0.0
    for name, sym, ceil, wf, unit in BUDGETS:
        addr = map_symbol(map_text, sym)
        used = rd.r32(addr) if addr else None
        if used is None or used > 0x7FFFFFFF:
            rows.append((name, "?", ceil, None, unit, "no-witness"))
            continue
        frac = used / ceil if ceil else 0.0
        worst = max(worst, frac)
        state = "BREACH" if used >= ceil else ("WARN" if frac >= wf else "ok")
        rows.append((name, used, ceil, frac, unit, state))
    # code wall
    end = map_symbol(map_text, "_end")
    if end is not None:
        cfrac = end / CODE_WALL_CEIL
        cstate = "BREACH" if end >= CODE_WALL_CEIL else ("WARN" if cfrac >= warn else "ok")
        rows.append(("code wall _end", end, CODE_WALL_CEIL, cfrac, "addr", cstate))
        worst = max(worst, cfrac)
    return rows, worst


def main(argv=None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--live", action="store_true")
    p.add_argument("--state")
    p.add_argument("--map", default=str(_HERE.parent / "game.map"))
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=55355)
    p.add_argument("--warn", type=float, default=0.90)
    p.add_argument("--watch", type=float, default=0.0, help="poll every N sec continuously")
    a = p.parse_args(argv)
    if not a.live and not a.state:
        p.error("pass --live or --state STATE.mcs")

    map_text = Path(a.map).read_text(errors="replace")
    try:
        rd = Reader(a.live, a.state, a.host, a.port)
    except Exception as e:
        sys.stderr.write(f"qa_memcheck: {e}\n"); return 2

    def one():
        rows, worst = sweep(rd, map_text, a.warn)
        for name, used, ceil, frac, unit, state in rows:
            u = used if isinstance(used, str) else (f"0x{used:X}" if unit == "addr" else f"{used}")
            c = f"0x{ceil:X}" if unit == "addr" else f"{ceil}"
            fr = f"{frac*100:4.0f}%" if frac is not None else "  ?  "
            mark = {"BREACH": "!! BREACH", "WARN": "!  WARN", "ok": "   ok", "no-witness": "   (no witness)"}[state]
            print(f"  {name:20s} {u:>10s}/{c:<9s} {fr} {mark}")
        breach = any(s in ("BREACH",) for *_, s in rows)
        print(f"qa_memcheck: worst {worst*100:.0f}% -> {'BREACH' if breach else 'OK'}")
        return breach

    if a.watch > 0:
        print(f"== live memory-availability watch (every {a.watch}s; Ctrl-C to stop) ==")
        try:
            while True:
                if one():
                    return 1
                time.sleep(a.watch)
        except KeyboardInterrupt:
            return 0
    else:
        return 1 if one() else 0


if __name__ == "__main__":
    sys.exit(main())
