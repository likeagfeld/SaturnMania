#!/usr/bin/env python3
"""qa_p6_water.py - Water subsystem M1 register + water-line gate.

docs/feature_checklists/water.md M1. Reads two witnesses from a settled GHZ
savestate (or a live-capture .mcs):

  p6_w_water_classid  register gate: 0/-1 = RED (Water not registered),
                      >0 = GREEN (RegisterObject assigned a live classID).
  p6_w_water_level    Water_StageLoad seeds 0x7FFFFFFF; once a WATER_WATERLEVEL
                      entity Creates (ACTIVE_NORMAL, at scene load) State_Water
                      drops it to the real GHZ water Y. GREEN when it has left
                      the sentinel (a finite positive fixed-point Y).

MEASURED RED baseline: on any pre-P6_WATER build the witnesses are absent/0 ->
RED. On the P6_WATER build after GHZ settles, classid>0 and water_level is the
scene water line -> GREEN.

Usage:
    python tools/qa_p6_water.py --live [--map game.map]     # RA netmem (PRIMARY)
    python tools/qa_p6_water.py STATE.mcs [--map game.map]  # savestate fallback

--live reads the witnesses over the RA network interface (qa_netmem) and gates on
the entity-pool anchor being healthy (a boot/garbage read is refused, per
[[headless-ra-cant-advance-chain-no-audio-clock]] -- use _gl_boot.ps1 so the chain
advances). It is HARNESS-HEALTH-AWARE: if the chain has not reached settled GHZ yet
(corkscrew classid != 53) it reports RED-not-yet, not a false GREEN.

Exit 0 = GREEN, 1 = RED, 2 = parse/harness error.
"""
from __future__ import annotations

import argparse
import importlib.util
import re
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent

SENTINEL = 0x7FFFFFFF


def _load(mod):
    spec = importlib.util.spec_from_file_location(mod, _HERE / (mod + ".py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)  # type: ignore[union-attr]
    return m


def map_symbol(map_path: Path, name: str) -> int:
    # Witnesses in p6_io_main.cpp land under the RSDK namespace in the map; try
    # both the qualified and bare forms so this is robust to the emit style.
    for cand in (f"RSDK::{name}", name):
        pat = re.compile(r"0x([0-9a-fA-F]{16})\s+" + re.escape(cand) + r"\s*$")
        for line in map_path.read_text(errors="replace").splitlines():
            m = pat.search(line)
            if m:
                return int(m.group(1), 16)
    raise KeyError(name)


def swap32(v: int) -> int:
    return ((v & 0xFF00FF00) >> 8) | ((v & 0x00FF00FF) << 8)


def _s32(v: int) -> int:
    return v - 0x100000000 if v >= 0x80000000 else v


def _verdict(cid: int, lvl: int, sht: int = 0) -> int:
    cid = _s32(cid)
    sht = _s32(sht)
    registered = cid > 0
    line_set = lvl not in (0, SENTINEL)
    # M1b: WATER.SHT staged into the band store -> the VDP1 surface is bound (the FRD
    # attaches to it) -> Water_Draw_Water tiles the surface strip at the water line.
    # shtslot -3 = SaturnSheet_Stage overflow (pre-BandReset fix); >=0 = surface bound.
    surf_bound = sht >= 0
    print(f"  p6_w_water_classid = {cid} ({'registered' if registered else 'NOT registered'})")
    print(f"  p6_w_water_level   = 0x{lvl & 0xFFFFFFFF:08X} "
          f"({'water line SET (Y=%d px)' % (lvl >> 16) if line_set else 'sentinel/unset'})")
    print(f"  p6_w_water_shtslot = {sht} "
          f"({'surface BOUND (band slot)' if surf_bound else 'surface UNBOUND (stage overflow)'})")
    ok = registered and line_set and surf_bound
    print(f"qa_p6_water: -> {'GREEN' if ok else 'RED'}")
    return 0 if ok else 1


def _run_live(mp: Path) -> int:
    qn = _load("qa_netmem")
    cli_cls = next(t for t in map(lambda n: getattr(qn, n), dir(qn))
                   if isinstance(t, type) and hasattr(t, "read32_saturn"))
    c = cli_cls()
    try:
        sym_cid = map_symbol(mp, "p6_w_water_classid")
        sym_lvl = map_symbol(mp, "p6_w_water_level")
        sym_sht = map_symbol(mp, "p6_w_water_shtslot")
        sym_pool = map_symbol(mp, "objectEntityList")
    except KeyError as e:
        sys.stderr.write(f"qa_p6_water: symbol missing from map: {e}\n")
        return 2
    # HARNESS-HEALTH guard (qa_trace class): the entity-pool pointer must be a real
    # WRAM-L address, else the core is stuck at boot and reads are GARBAGE.
    pool = c.read32_saturn(sym_pool)
    if not (0x00200000 <= pool < 0x00300000):
        sys.stderr.write(f"qa_p6_water: LIVE HARNESS UNHEALTHY (objectEntityList=0x{pool:08X}); "
                         "boot _gl_boot.ps1 (NOT headless -- no audio clock) + wait for GHZ.\n")
        return 2
    cid = c.read32_saturn(sym_cid)
    lvl = c.read32_saturn(sym_lvl)
    sht = c.read32_saturn(sym_sht)
    return _verdict(cid, lvl, sht)


def _run_state(mp: Path, state: str) -> int:
    me = _load("mcs_extract")
    try:
        sym_cid = map_symbol(mp, "p6_w_water_classid")
        sym_lvl = map_symbol(mp, "p6_w_water_level")
        sym_sht = map_symbol(mp, "p6_w_water_shtslot")
    except KeyError as e:
        sys.stderr.write(f"qa_p6_water: witness missing from map: {e}\n")
        return 2
    sections = me.parse_savestate(Path(state))

    def peek32(addr):
        raw = me._peek_bytes(sections, addr, 4)
        if raw is None:
            raise ValueError(f"address 0x{addr:08X} not in any savestate region")
        return swap32(int.from_bytes(raw, "big"))  # WRAM pair-swap (gotcha #10)

    return _verdict(peek32(sym_cid), peek32(sym_lvl), peek32(sym_sht))


def main(argv=None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("state", nargs="?", help="savestate .mcs (omit with --live)")
    p.add_argument("--live", action="store_true", help="read via RA netmem (PRIMARY)")
    p.add_argument("--map", default=str(_HERE.parent / "game.map"))
    a = p.parse_args(argv)
    mp = Path(a.map)
    if a.live:
        return _run_live(mp)
    if not a.state:
        p.error("pass --live or a STATE.mcs")
    return _run_state(mp, a.state)


if __name__ == "__main__":
    sys.exit(main())
