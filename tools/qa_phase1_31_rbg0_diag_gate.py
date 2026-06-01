#!/usr/bin/env python3
"""qa_phase1_31_rbg0_diag_gate.py - Phase 1.31 Fix #3 RED-firing diagnostic gate.

Validates that RBG0's CRAM routing actually contains the asset's palette
data, NOT just black + printf font (which produces the observed pure-green
flood because the asset's pixel bytes index into mostly-empty CRAM region
PLUS the back-screen sky-blue shows through the SINGLE-over-mode area).

Per ST-058-R2 §RBG + §11 (Color RAM):
  - In 256-color cell mode (R0CHCN = 1 in CHCTLB), RBG0 pixel byte V
    looks up CRAM slot (R0CAOS * 256 + V).
  - R0CAOS is the lower 3 bits of CRAOFB at 0x05F800E6.

Per jo-engine/jo_engine/vdp2_malloc.c:60:
  __jo_cram = ((jo_color *)JO_VDP2_CRAM) + 256 + 1
  // The first palette is reserved
So jo_create_palette_from() places the FIRST user palette into CRAM
slots [257..512]. That's bank 1 + 1 slot.

This gate peeks the actual register + CRAM contents of a captured
Mednafen state and asserts:

  P1 BGON.R0ON           = 1        (RBG0 enabled)
  P2 PRIR.R0PRIN         > 0        (RBG0 visible)
  P3 CHCTLB.R0CHCN       = 1        (RBG0 in 256-color cell mode)
  P4 Back-screen color   != neon green (not the source of the flood)
  P5 RBG0's actual CRAM read region (CRAM[R0CAOS*256 .. R0CAOS*256+255])
     contains at least 32 nonzero slots
     (i.e. the asset palette IS loaded into the region RBG0 reads from)

P5 is the predicate that exposes the bug.  With CRAOFB.R0CAOS=0 and
jo's __jo_cram starting at slot 257, the asset palette is in slots
[257..512] but RBG0 reads from [0..255] which contains only the
printf font palette (~15 nonzero slots).  That's why the screen
floods pure colors derived from CRAM[0..255] instead of showing
the Sonic-Mania backdrop.

Usage:
    py -3 tools/qa_phase1_31_rbg0_diag_gate.py samples/qa_phase1_31_fix3_diag.mcs

Exit codes:
    0 = GREEN (all P1-P5 pass)
    1 = RED   (any predicate fails)
"""
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MCS_DEFAULT = ROOT / "samples" / "qa_phase1_31_fix3_diag.mcs"


def peek16(mcs_path: Path, addr: int) -> int:
    """Call mcs_extract.py --peek16 and return the value."""
    out = subprocess.check_output(
        ["py", "-3", str(ROOT / "tools" / "mcs_extract.py"),
         str(mcs_path), "--peek16", hex(addr)],
        text=True,
    ).strip()
    # Format: "peek16 0xADDR = 0xVALUE"
    return int(out.split("= ")[-1], 16)


def dump_cram(mcs_path: Path, out_bin: Path):
    subprocess.check_call(
        ["py", "-3", str(ROOT / "tools" / "mcs_extract.py"),
         str(mcs_path), "--cram", str(out_bin)],
        stdout=subprocess.DEVNULL,
    )


def rgb555_to_rgb888(w: int):
    r5 = w & 0x1F
    g5 = (w >> 5) & 0x1F
    b5 = (w >> 10) & 0x1F
    return ((r5 << 3) | (r5 >> 2),
            (g5 << 3) | (g5 >> 2),
            (b5 << 3) | (b5 >> 2))


def is_neon_green(rgb):
    r, g, b = rgb
    return g >= 200 and r < 64 and b < 64


def main():
    mcs_path = Path(sys.argv[1]) if len(sys.argv) > 1 else MCS_DEFAULT
    if not mcs_path.exists():
        print(f"RED: mcs file not found: {mcs_path}")
        sys.exit(1)

    print(f"qa_phase1_31_rbg0_diag_gate: inspecting {mcs_path}")
    print()

    # --- Peek registers
    bgon    = peek16(mcs_path, 0x05F80020)
    prir    = peek16(mcs_path, 0x05F800FC)
    chctlb  = peek16(mcs_path, 0x05F8002A)
    craofb  = peek16(mcs_path, 0x05F800E6)
    bktau   = peek16(mcs_path, 0x05F800AC)
    bktal   = peek16(mcs_path, 0x05F800AE)
    bkclmd  = (bktau >> 15) & 1
    bkta_words = ((bktau & 0x7) << 16) | bktal
    bk_byte_addr = 0x05E00000 + bkta_words * 2
    bk_word = peek16(mcs_path, bk_byte_addr)

    r0on    = (bgon  >> 4) & 1
    r0prin  = prir & 0x7
    r0chcn  = (chctlb >> 12) & 0x7
    r0caos  = craofb & 0x7

    print("Register snapshot:")
    print(f"  BGON     @ 0x05F80020 = 0x{bgon:04x}   R0ON   = {r0on}")
    print(f"  PRIR     @ 0x05F800FC = 0x{prir:04x}   R0PRIN = {r0prin}")
    print(f"  CHCTLB   @ 0x05F8002A = 0x{chctlb:04x}   R0CHCN = {r0chcn}  (1=256-color)")
    print(f"  CRAOFB   @ 0x05F800E6 = 0x{craofb:04x}   R0CAOS = {r0caos}  (CRAM bank for RBG0)")
    print(f"  BKTAU/L  @ 0x05F800AC/AE = 0x{bktau:04x}/0x{bktal:04x}  BKCLMD={bkclmd}")
    print(f"  Back-color word @ 0x{bk_byte_addr:08x} = 0x{bk_word:04x} -> RGB={rgb555_to_rgb888(bk_word)}")
    print()

    # --- Dump CRAM
    cram_bin = ROOT / "samples" / "qa_phase1_31_fix3_cram.bin"
    cram_bin.parent.mkdir(parents=True, exist_ok=True)
    dump_cram(mcs_path, cram_bin)
    data = cram_bin.read_bytes()
    slots = [struct.unpack("<H", data[i:i+2])[0] for i in range(0, len(data), 2)]

    rbg0_bank_base = r0caos * 256
    rbg0_region = slots[rbg0_bank_base:rbg0_bank_base + 256]
    nonzero_in_region = sum(1 for w in rbg0_region if (w & 0x7FFF) != 0)
    print(f"RBG0 actual CRAM read region: CRAM[{rbg0_bank_base}..{rbg0_bank_base+255}]")
    print(f"  nonzero slots in region: {nonzero_in_region}/256")
    jo_user_base = 257
    nonzero_in_jo_user = sum(1 for w in slots[jo_user_base:jo_user_base+256]
                             if (w & 0x7FFF) != 0)
    print(f"jo's first-user-palette region: CRAM[{jo_user_base}..{jo_user_base+255}]")
    print(f"  nonzero slots in jo user region: {nonzero_in_jo_user}/256")
    print()

    # --- Predicates
    failures = []

    if r0on != 1:
        failures.append(f"P1 RED: BGON.R0ON = {r0on}, expected 1 (RBG0 not enabled)")
    else:
        print("  P1 GREEN: BGON.R0ON = 1 (RBG0 enabled)")

    # Phase 1.31 Fix #4 REVISED (Task #106, 2026-05-27): RBG0 is now
    # INTENTIONALLY hidden via slPriorityRbg0(0).  P2's original
    # premise (RBG0 must be visible) was inverted by the strategic
    # pivot.  The remaining predicates (P1 R0ON, P3 R0CHCN, P4 back-
    # color, P5 CRAM routing) still hold as defensive contracts: if
    # we ever re-enable RBG0 we want the routing infrastructure intact.
    # P2 now allows R0PRIN=0 (Sub-fix A) AND R0PRIN>0 (legacy / future
    # re-enable).
    if r0prin == 0:
        print(f"  P2 GREEN: PRIR.R0PRIN = 0 (RBG0 hidden by Sub-fix A)")
    else:
        print(f"  P2 GREEN: PRIR.R0PRIN = {r0prin} (RBG0 visible)")

    if r0chcn != 1:
        failures.append(f"P3 RED: CHCTLB.R0CHCN = {r0chcn}, expected 1 (256-color)")
    else:
        print("  P3 GREEN: CHCTLB.R0CHCN = 1 (256-color cell mode)")

    bk_rgb = rgb555_to_rgb888(bk_word)
    if is_neon_green(bk_rgb):
        failures.append(f"P4 RED: back-color = {bk_rgb} is neon green")
    else:
        print(f"  P4 GREEN: back-color RGB={bk_rgb} (not neon green)")

    if nonzero_in_region < 32:
        failures.append(
            f"P5 RED: RBG0 reads CRAM[{rbg0_bank_base}..{rbg0_bank_base+255}] "
            f"which contains only {nonzero_in_region} nonzero slots "
            f"(threshold: 32). The asset palette is NOT in RBG0's read "
            f"region; jo's __jo_cram base at slot {jo_user_base} contains "
            f"{nonzero_in_jo_user} nonzero slots which is where the asset "
            f"palette actually got loaded. Fix: align CRAOFB.R0CAOS so "
            f"that R0CAOS * 256 == {jo_user_base - 1} = 256 -> R0CAOS=1; "
            f"OR move the asset palette into the region RBG0 reads."
        )
    else:
        print(f"  P5 GREEN: RBG0 read region has {nonzero_in_region} nonzero slots (>= 32)")

    print()
    if failures:
        print("=" * 64)
        print("RED: gate failed")
        print("=" * 64)
        for f in failures:
            print(f)
        sys.exit(1)
    else:
        print("GREEN: all predicates pass")
        sys.exit(0)


if __name__ == "__main__":
    main()
