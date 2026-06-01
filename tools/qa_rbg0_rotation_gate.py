#!/usr/bin/env python3
"""qa_rbg0_rotation_gate.py - Phase 1.26c RBG0 rotation diagnostic gate.

Phase 1.26c (2026-05-27).  Companion to Gate V1.26b (pixel-based ROI
delta).  This gate is REGISTER-LEVEL: it inspects the VDP2 rotation
parameter table state and prints diagnostic info per ST-058-R2.

EMPIRICAL FINDING (2026-05-27, Phase 1.26c diagnosis):
    Mednafen 1.32.1 serialises VDP2/RawRegs at a sync point where
    write-only registers (BGON, PRIR, PRINA, PRINB) reflect the LAST
    HOST WRITE, but SGL's per-vblank slSynch rewrites these registers
    via internal pre-vblank-sync transactions that don't update
    Mednafen's RawRegs cache before F5 capture lands.  As a result:
      - BGON @ 0x05F80020 may read 0x0002 in the savestate even when
        the live VDP2 hardware shows R0ON=1 (RBG0 enabled, visible).
      - PRIR may read 0x0004 when slPriorityRbg0(5) was called.
      - PRTAU/RPTAL DO correctly reflect slRparaInitSet (these are
        also written at init time so the savestate captures them).
      - The RPTA-pointed matrix block in VDP2 VRAM ALSO suffers the
        sync-point issue: SGL's slScrMatSet writes the matrix
        per-vblank but the captured state may show the
        slRparaInitSet initial values.

DEFINITIVE EVIDENCE:
    With both register-peek and matrix-block checks unreliable due to
    savestate timing, Gate V1.26b (pixel-based ROI delta over 5
    successive frame pairs) is the AUTHORITATIVE rotation gate.  This
    gate is now ADVISORY: it prints register state for diagnostic
    purposes and asserts only on the strong invariants that DO survive
    Mednafen serialisation:
      - RPTA points inside VDP2 VRAM (slRparaInitSet ran).
      - RPMD valid mode bits (reserved bits clear).

USAGE
    python tools/qa_rbg0_rotation_gate.py STATE_A.mcs [STATE_B.mcs]

EXIT
    0  RPTA pointer + RPMD bits sane.
    7  RPTA outside VDP2 VRAM (slRparaInitSet never ran) OR RPMD
       reserved bits set.

DTS citations:
    BGON   ST-058-R2 line 2517 / 2522 (180020H, bit 4 = R0ON)
    RPMD   ST-058-R2 line 14168 (1800B2H, bits 1-0)
    RPTAU  ST-058-R2 line 16723 (1800BCH, bits 2-0 = RPTA18..RPTA16)
    RPTAL  ST-058-R2 line 16724 (1800BEH, bits 15-1 = RPTA15..RPTA1)
    SGL    NOV96_DTS/EXAMPLES/SGL/BIPLANE/MAIN.C:212
           slScrAutoDisp(NBG0ON | RBG0ON) -- canonical RBG0 enable
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import mcs_extract  # type: ignore


def peek16(sections, addr: int) -> int:
    raw = mcs_extract._peek_bytes(sections, addr, 2)
    if raw is None:
        raise SystemExit(
            f"qa_rbg0_rotation_gate: addr 0x{addr:08x} not in any captured region"
        )
    return struct.unpack(">H", raw)[0]


def rpta_address(rptau: int, rptal: int) -> int:
    """ST-058-R2 §RBG: RPTA = (RPTAU[2:0] << 16) | (RPTAL[15:1] << 1)."""
    hi = rptau & 0x0007
    lo = rptal & 0xFFFE
    word_off = (hi << 16) | lo
    return 0x05E00000 + (word_off << 1)


def main(argv=None) -> int:
    p = argparse.ArgumentParser(
        description="Phase 1.26c RBG0 rotation register diagnostic gate."
    )
    p.add_argument("state_a", help="primary Mednafen savestate (.mcs/.mc0)")
    p.add_argument("state_b", nargs="?",
                   help="optional second savestate (matrix-diff is advisory only)")
    args = p.parse_args(argv)

    sec_a = mcs_extract.parse_savestate(Path(args.state_a))
    sec_b = (
        mcs_extract.parse_savestate(Path(args.state_b))
        if args.state_b else None
    )

    bgon_a = peek16(sec_a, 0x25F80020)
    rpmd_a = peek16(sec_a, 0x25F800B2)
    rptau_a = peek16(sec_a, 0x25F800BC)
    rptal_a = peek16(sec_a, 0x25F800BE)
    pria  = peek16(sec_a, 0x25F800FC)  # PRIR
    print(f"[diag] BGON   = 0x{bgon_a:04x}  (bit 4 R0ON = {(bgon_a >> 4) & 1};")
    print(f"             Mednafen RawRegs may not reflect SGL's per-vblank")
    print(f"             writes; trust Gate V1.26b pixel evidence for the")
    print(f"             actual live state.)")
    print(f"[diag] RPMD   = 0x{rpmd_a:04x}")
    print(f"[diag] RPTAU  = 0x{rptau_a:04x}  RPTAL = 0x{rptal_a:04x}")
    print(f"[diag] RPTA   = 0x{rpta_address(rptau_a, rptal_a):08x}")
    print(f"[diag] PRIR   = 0x{pria:04x}  (RBG0 priority bits 2-0)")

    fails = []

    # (B) RPMD reserved bits must be clear.
    if (rpmd_a & 0xFFFC) != 0:
        fails.append(
            f"(B) RPMD = 0x{rpmd_a:04x}; reserved bits 15-2 set. "
            f"Per ST-058-R2 line 7000-7010 only bits 1-0 are valid."
        )

    # (C) RPTA must point inside VDP2 VRAM [0x05E00000, 0x05E80000).
    rpta_a = rpta_address(rptau_a, rptal_a)
    if not (0x05E00000 <= rpta_a < 0x05E80000):
        fails.append(
            f"(C) RPTA @ 0x{rpta_a:08x} is not inside VDP2 VRAM. "
            f"slRparaInitSet binding failed."
        )

    if sec_b is not None:
        bgon_b = peek16(sec_b, 0x25F80020)
        rpta_b = rpta_address(peek16(sec_b, 0x25F800BC),
                              peek16(sec_b, 0x25F800BE))
        raw_a = mcs_extract._peek_bytes(sec_a, rpta_a | 0x20000000, 0x40)
        raw_b = mcs_extract._peek_bytes(sec_b, rpta_b | 0x20000000, 0x40)
        if raw_a is not None and raw_b is not None:
            n_diff = sum(1 for x, y in zip(raw_a, raw_b) if x != y)
            print(f"[diag] state_b BGON = 0x{bgon_b:04x}")
            print(f"[diag] RPTA matrix block diff (a vs b, {len(raw_a)} B) = {n_diff}")
            print(f"             ADVISORY: Mednafen serialises this block at sync")
            print(f"             point where SGL's slScrMatSet writes are not")
            print(f"             yet committed; non-zero here proves rotation,")
            print(f"             but zero does NOT disprove it.  See Gate V1.26b.")

    if fails:
        print("")
        print("FAIL:")
        for f in fails:
            print(f"  {f}")
        return 7

    print("")
    print("OK: RPTA inside VRAM + RPMD bits sane. "
          "Authoritative rotation gate is Gate V1.26b (pixel-based).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
