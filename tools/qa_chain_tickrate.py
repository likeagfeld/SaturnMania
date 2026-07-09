#!/usr/bin/env python3
"""
qa_chain_tickrate.py -- #315 game-speed gate: logic must tick at 60 Hz (the
decomp contract, RetroEngine.cpp:392-412) regardless of the render rate.

MEASURED RED baseline (pre-fix builds tick logic once per RENDERED frame):
landing leg 33.5 ticks/s = ratio 0.56; menu ~0.42; AIZ fly-in ~0.07-0.17.

Measures delta(p6_w_tick_frames) / delta(p6_w_perf_vblanks) between TWO
savestates of the SAME run (addresses re-resolved from game.map each call --
they shift per build). Pre-fix builds have no p6_w_tick_frames symbol: the
gate then falls back to p6_w_cont_frames (presented==ticked pre-fix) and is
expected RED. PASS iff ratio >= --min (default 0.90; the P6_TICK_CAP clamp
keeps slow legs slightly under 1.0).

Usage: python tools/qa_chain_tickrate.py A.mcs B.mcs [--map game.map] [--min 0.90]
"""
import re
import subprocess
import sys


def sym(map_path, name):
    with open(map_path, "r", errors="ignore") as f:
        for line in f:
            if line.rstrip().endswith(" " + name):
                m = re.search(r"0x([0-9a-fA-F]+)", line)
                if m:
                    return int(m.group(1), 16)
    return None


def unswap(v):
    return ((v & 0xFF00FF00) >> 8) | ((v & 0x00FF00FF) << 8)


def peek32(mcs, addr):
    out = subprocess.run([sys.executable, "tools/mcs_extract.py", mcs,
                          "--peek", hex(addr)], capture_output=True, text=True).stdout
    for line in out.splitlines():
        if "peek32" in line and "=" in line:
            return unswap(int(line.split()[3], 16))
    raise RuntimeError(f"peek failed: {mcs} {hex(addr)}")


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    mapp = "game.map"
    minr = 0.90
    if "--map" in argv:
        mapp = argv[argv.index("--map") + 1]
    if "--min" in argv:
        minr = float(argv[argv.index("--min") + 1])
    if len(args) < 2:
        print("usage: qa_chain_tickrate.py A.mcs B.mcs [--map game.map] [--min 0.90]")
        return 2
    a, b = args[0], args[1]
    tick_addr = sym(mapp, "p6_w_tick_frames")
    fallback = tick_addr is None
    if fallback:
        tick_addr = sym(mapp, "p6_w_cont_frames")
        print("note: p6_w_tick_frames absent (pre-fix build) -> using cont_frames (expected RED)")
    vbl_addr = sym(mapp, "p6_w_perf_vblanks")
    if tick_addr is None or vbl_addr is None:
        print("FAIL: witness symbols not found in", mapp)
        return 2
    ta, tb = peek32(a, tick_addr), peek32(b, tick_addr)
    va, vb = peek32(a, vbl_addr), peek32(b, vbl_addr)
    dt, dv = tb - ta, vb - va
    if dv <= 0:
        print(f"FAIL: vblank delta {dv} <= 0 (captures not ordered / same frame)")
        return 1
    ratio = dt / dv
    ok = ratio >= minr
    print(f"qa_chain_tickrate: ticks {ta}->{tb} (+{dt})  vblanks {va}->{vb} (+{dv})"
          f"  ratio={ratio:.3f} (min {minr})  -> {'GREEN' if ok else 'RED'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
