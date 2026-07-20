#!/usr/bin/env python3
"""qa_vdp1_evicts.py - VDP1 slot-cache thrash gate (R3-residual class).

The user-visible symptom class ([[user-symptom-map-v2-2026-07-17]] R3 residual):
ring/digit/checkpoint-ball sprites FLASH-ALTERNATE and the sky shows transient
pink washes. ROOT (MEASURED 2026-07-17 live): VDP1 slot-cache LRU thrash --
p6_w_vdp1_evicts ~6.10/frame at settled GHZ with dl_cmds_max=43 sprites
competing for the slot pool; ~6 textures re-stage EVERY frame so a jo slot
alternates digit/ball/ring content frame-to-frame.

GATE: at settled GHZ (water/corkscrew witnesses healthy), sample
p6_w_vdp1_evicts and cont_frames over a window; evicts-per-frame must be
<= THRESH (0.5). RED on the thrashing build BEFORE the re-bucket fix;
GREEN after. Live-only (RA netmem via _gl_boot.ps1 -- headless cannot
advance the chain, [[headless-ra-cant-advance-chain-no-audio-clock]]).

Usage:
    python tools/qa_vdp1_evicts.py [--map game.map] [--window 15] [--thresh 0.5]

Exit 0 = GREEN, 1 = RED (thrash), 2 = harness error / not settled.
"""
from __future__ import annotations

import argparse
import importlib.util
import re
import sys
import time
from pathlib import Path

_HERE = Path(__file__).resolve().parent


def _load(mod):
    spec = importlib.util.spec_from_file_location(mod, _HERE / (mod + ".py"))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)  # type: ignore[union-attr]
    return m


def map_symbol(mt: str, name: str) -> int:
    for cand in (f"RSDK::{name}", name):
        m = re.search(r"0x([0-9a-fA-F]{16})\s+" + re.escape(cand) + r"\s*$", mt, re.M)
        if m:
            return int(m.group(1), 16)
    raise KeyError(name)


def main(argv=None) -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--map", default=str(_HERE.parent / "game.map"))
    p.add_argument("--window", type=float, default=15.0, help="sample seconds")
    p.add_argument("--thresh", type=float, default=0.5, help="max evicts/frame")
    p.add_argument("--settle-wait", type=float, default=420.0,
                   help="max seconds to wait for settled GHZ")
    a = p.parse_args(argv)

    mt = Path(a.map).read_text(errors="replace")
    try:
        sym_ev = map_symbol(mt, "p6_w_vdp1_evicts")
        sym_cont = map_symbol(mt, "p6_w_cont_frames")
        sym_pool = map_symbol(mt, "objectEntityList")
        sym_cork = map_symbol(mt, "p6_w_corkscrew_classid")
    except KeyError as e:
        sys.stderr.write(f"qa_vdp1_evicts: symbol missing from map: {e}\n")
        return 2

    qn = _load("qa_netmem")
    c = qn.RetroMem(timeout=1.5)

    def r32(addr):
        return int.from_bytes(c.read_saturn(addr, 4), "big")

    def s32(v):
        return v - 0x100000000 if v >= 0x80000000 else v

    # settle gate: pool healthy + corkscrew registered (GHZ arm complete)
    t0 = time.time()
    while time.time() - t0 < a.settle_wait:
        try:
            pool = r32(sym_pool)
            cork = s32(r32(sym_cork))
        except Exception:
            time.sleep(3)
            continue
        if 0x00200000 <= pool < 0x00300000 and cork == 53:
            break
        time.sleep(3)
    else:
        sys.stderr.write("qa_vdp1_evicts: never reached settled GHZ (boot _gl_boot.ps1?)\n")
        return 2

    ev0, cf0 = r32(sym_ev), s32(r32(sym_cont))
    time.sleep(a.window)
    ev1, cf1 = r32(sym_ev), s32(r32(sym_cont))
    frames = cf1 - cf0
    evicts = ev1 - ev0
    if frames <= 0:
        sys.stderr.write(f"qa_vdp1_evicts: cont_frames did not advance ({cf0}->{cf1}) -- frozen core?\n")
        return 2
    per = evicts / frames
    ok = per <= a.thresh
    print(f"qa_vdp1_evicts: {evicts} evicts / {frames} frames over {a.window:.0f}s "
          f"= {per:.2f}/frame (thresh {a.thresh}) -> {'GREEN' if ok else 'RED (slot-cache thrash)'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
