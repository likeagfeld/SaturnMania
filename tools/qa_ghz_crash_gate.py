#!/usr/bin/env python3
"""qa_ghz_crash_gate.py - GHZ hard-crash / WRAM-H stomp detector (#192).

WHY THIS EXISTS
---------------
A user GHZ-gameplay savestate captured "right when it crashed"
(.mednafen/mcs/game.4f2a5fa7...mc0, 2026-06-02 13:38) was measured to be
a HARD CRASH, distinct from the earlier "slow motion" report:

  WorkRAM-H (0x06000000, 1 MB): 99.99% filled with the halfword 0x03EF
      (byte pattern ef 03 ef 03). Only 4 distinct byte values remain.
  WorkRAM-L (0x00200000):        INTACT (256 distinct byte values, "MG2S"
      header present) -> the #192 LWRAM pools (entity 0x260000, player
      0x2D0000 / 0x2F4000) are NOT the writer.
  Master SH-2: PC = 0x00000003 (boot-ROM, derailed), SP corrupted to
      0x05374040 (A-bus garbage). Last real frame PR = ghz_fg_vblank+24,
      with live VRAM-DMA pointers (R5 = 0x25e40000 VDP2-VRAM cache-through,
      R13 = 0x25c77b80 VDP1-VRAM cache-through, R6 = 0x800 = GHZ_FG_PAGE_LEN
      * 2, the slDMAXCopy length). The master derailed in / just after the
      GHZ V-blank SCU-DMA.
  Slave SH-2: PC = 0x000003f3 (derailed). PR = SlaveControl+40 -- but the
      HEALTHY 13:20 state also sits at SlaveControl+42, so that is the
      slave's normal SGL dispatch loop, NOT the crash site.

The uniform single-halfword fill is the signature of a fixed-source fill /
runaway store, not a varied-data copy. The fill DESTROYED the jo sprite
table, the #192 frame-time counters, and both stacks -> the proximate
writer cannot be named from this post-mortem capture. This gate does NOT
diagnose the writer; it ENCODES THE CRASH CLASS so any future build that
reproduces the WRAM-H stomp is caught automatically (RED), and a healthy
in-gameplay capture passes (GREEN). It is the don't-regress anchor for the
#192 crash, per the project's RED-gate-before-fix rule.

PREDICATE
---------
A state is RED (crashed) when EITHER:
  (1) WorkRAM-H distinct-byte-value count < DISTINCT_FLOOR (32) -- the RAM
      has collapsed to a fill pattern, OR
  (2) WorkRAM-H most-common single halfword covers >= FILL_FRAC (0.50) of
      the region -- a uniform stomp, OR
  (3) master SH-2 PC is outside executable WRAM-H [0x06000000,0x06100000)
      -- the CPU derailed off its .text.
GREEN when none of the above trip.

Exit codes:
    0 = GREEN (no crash signature)
    1 = RED   (crash / WRAM-H stomp signature present)
    2 = harness error (state unreadable)
"""
from __future__ import annotations
import argparse
import os
import sys
from collections import Counter
from pathlib import Path

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

DISTINCT_FLOOR = 32     # healthy WRAM-H has 256 distinct byte values.
FILL_FRAC = 0.50        # >=50% one halfword == uniform stomp.
PC_LO = 0x06000000      # executable WRAM-H window.
PC_HI = 0x06100000


def _analyze(path):
    import mcs_extract
    s = mcs_extract.parse_savestate(Path(path))
    wh = mcs_extract._read_region(s, "MAIN", "WorkRAMH")
    if wh is None:
        return None
    distinct = len(set(wh))
    # most-common aligned NON-ZERO halfword fraction. 0x0000 is excluded:
    # benign BSS / unused RAM is dominated by zeros (healthy WRAM-H is ~60%
    # zero), so a zero majority is NOT a crash. A uniform *non-zero* fill is
    # the stomp signature (the measured crash is 0x03EF over ~100%).
    hw = Counter()
    n = len(wh) & ~1
    for i in range(0, n, 2):
        if wh[i] or wh[i + 1]:
            hw[(wh[i], wh[i + 1])] += 1
    if hw:
        top_hw, top_cnt = hw.most_common(1)[0]
    else:
        top_hw, top_cnt = (0, 0), 0
    frac = top_cnt / (n // 2)
    rm = mcs_extract._sh2_regs(s, "master")
    pc = rm["PC"] if rm and "PC" in rm else None
    return {
        "distinct": distinct,
        "top_hw": (top_hw[1] << 8) | top_hw[0],   # Saturn-endian halfword
        "frac": frac,
        "master_pc": pc,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("state", help="savestate (.mc0/.mcs) to inspect")
    args = ap.parse_args()

    print("GHZ HARD-CRASH / WRAM-H STOMP GATE (#192)")
    print("-" * 60)
    if not os.path.exists(args.state):
        print(f"  HARNESS GAP: state not found: {args.state}")
        sys.exit(2)

    m = _analyze(args.state)
    if m is None:
        print("  HARNESS GAP: WorkRAMH region not in state")
        sys.exit(2)

    pc_s = f"0x{m['master_pc']:08x}" if m["master_pc"] is not None else "n/a"
    print(f"  WRAM-H distinct bytes : {m['distinct']}  (healthy ~256)")
    print(f"  WRAM-H top halfword   : 0x{m['top_hw']:04x} "
          f"covering {m['frac']*100:.2f}%")
    print(f"  master SH-2 PC        : {pc_s}")
    print("-" * 60)

    reasons = []
    if m["distinct"] < DISTINCT_FLOOR:
        reasons.append(f"distinct bytes {m['distinct']} < {DISTINCT_FLOOR}")
    if m["frac"] >= FILL_FRAC:
        reasons.append(f"halfword 0x{m['top_hw']:04x} fills "
                       f"{m['frac']*100:.1f}% >= {FILL_FRAC*100:.0f}%")
    if m["master_pc"] is not None and not (PC_LO <= m["master_pc"] < PC_HI):
        reasons.append(f"master PC {pc_s} outside .text "
                       f"[0x{PC_LO:08x},0x{PC_HI:08x})")

    if reasons:
        print("  RED: crash signature present -- " + "; ".join(reasons))
        sys.exit(1)
    print("  GREEN: no crash signature (RAM intact, master PC in .text).")
    sys.exit(0)


if __name__ == "__main__":
    main()
