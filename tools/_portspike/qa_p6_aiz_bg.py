#!/usr/bin/env python3
# =============================================================================
# qa_p6_aiz_bg.py -- R2.1 RED gate: the 4-bpp AIZ Background plane on VDP2 NBG0.
#
# R2.0 produced cd/AIZBG.{CHR,RMP,BNK,CMP} (the lossless 4-bpp re-palettize of
# the 4 AIZ Background layers -> 3 custom 16-color banks; verified offline by
# tools/build_aiz_4bpp.py). R2.1 wires that data onto the unused NBG0 plane so
# the AIZ intro fly-in renders the jungle/sky instead of a black screen.
#
# This gate is SAVESTATE-PRIMARY (CLAUDE.md Sec 8.5 -- register/VRAM/CRAM
# questions use the Mednafen savestate harness first). It fires RED on the
# CURRENT build (no BG plane: B1 char base holds stale/zero data, CRAM[256..303]
# zero, NBG0 disabled) and GREEN once R2.1 lands. Three doc-grounded checks:
#
#   C1 (CHR uploaded): the engine copies cd/AIZBG.CHR verbatim to VDP2 bank B1
#      at 0x25E60000. The first compact tile (128 B, 4-bpp 2x2) must byte-match
#      AIZBG.CHR[0:128].
#   C2 (CRAM banks): the 3 custom 16-color banks live at CRAM[256..303]
#      (0x25F00200, PAL_BASE=16 -> clear of the 8-bpp FG's CRAM[0..255]). The
#      non-transparent slots (per AIZBG.CMP, slot 0 = transparent) must be
#      opaque (Saturn CRAM bit15 set) and non-zero.
#   C3 (NBG0 on): VDP2 BGON (0x25F80020) bit0 = NBG0 display-enable, set by
#      slScrAutoDisp(NBG0ON | NBG1ON | SPRON). Currently only NBG1 (bit1) is on.
#
# Doc anchors (all confirmed, no guess): 2-word PND palette = bits 22-16,
# charno unit = 0x20 B -> 4-bpp 16x16 = 4 units (qa_p6_vdp2.py:10-13, ST-058-R2
# Table 4.7); VDP2 VRAM bank B1 = 0x25E60000 / CRAM = 0x25F00000 (p6_vdp2.c:34-36);
# BGON screen-display-enable bit per scroll (ST-058-R2).
#
# Usage:
#   python tools/_portspike/qa_p6_aiz_bg.py [savestate.mcs]
# Capture (after the R2.1 build):
#   pwsh -File tools/qa_savestate.ps1 -Cue game.cue -SaveFrame 85 \
#        -Out tools/_portspike/_p6/_aiz_bg.mcs
# =============================================================================
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))

_spec = importlib.util.spec_from_file_location(
    "mcs_extract", os.path.join(ROOT, "tools", "mcs_extract.py"))
_mcs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_mcs)

CHR_PATH = os.path.join(ROOT, "cd", "AIZBG.CHR")
CMP_PATH = os.path.join(ROOT, "cd", "AIZBG.CMP")
MCS_DEFAULT = os.path.join(HERE, "_p6", "_aiz_bg.mcs")

VRAM_B1_CHR = 0x25E60000   # AIZBG.CHR upload target (free VDP2 bank B1)
CRAM_BG_BASE = 0x25F00200  # CRAM[256] = bank 16*16 (PAL_BASE=16), clear of FG[0..255]
VDP2_BGON = 0x25F80020     # screen display-enable; bit0=NBG0, bit1=NBG1


def _be16(b, i):
    return (b[i] << 8) | b[i + 1]


def main(argv=None):
    argv = argv if argv is not None else sys.argv[1:]
    mcs = argv[0] if argv else MCS_DEFAULT

    chr_data = open(CHR_PATH, "rb").read() if os.path.exists(CHR_PATH) else b""
    cmp_data = open(CMP_PATH, "rb").read() if os.path.exists(CMP_PATH) else b""
    if len(chr_data) < 128 or len(cmp_data) < 1:
        print("RED: cd/AIZBG.{CHR,CMP} missing -- run tools/build_aiz_4bpp.py first")
        return 1
    n_banks = cmp_data[0]
    banks = [list(cmp_data[1 + i * 16:1 + i * 16 + 16]) for i in range(n_banks)]

    if not os.path.exists(mcs):
        print(f"RED-ready (no savestate at {mcs}).")
        print("  Gate is in place; capture an AIZ savestate after the R2.1 build:")
        print("  pwsh -File tools/qa_savestate.ps1 -Cue game.cue -SaveFrame 85 "
              f"-Out {os.path.relpath(MCS_DEFAULT, ROOT)}")
        print(f"  Will verify: C1 CHR@0x{VRAM_B1_CHR:08X}, "
              f"C2 {sum(sum(1 for s in b[1:] if s) for b in banks)} CRAM slots "
              f"@0x{CRAM_BG_BASE:08X}, C3 NBG0 bit @0x{VDP2_BGON:08X}.")
        return 1

    sections = _mcs.parse_savestate(__import__("pathlib").Path(mcs))

    # ---- C1: CHR uploaded to B1 (first compact tile byte-exact) ----
    got = _mcs._peek_bytes(sections, VRAM_B1_CHR, 128)
    c1 = got is not None and bytes(got) == chr_data[:128]
    c1_nonzero = got is not None and any(got)
    print(f"C1 CHR@0x{VRAM_B1_CHR:08X}: "
          f"{'byte-match' if c1 else ('nonzero-but-mismatch' if c1_nonzero else 'zero/absent')} "
          f"-> {'GREEN' if c1 else 'RED'}")

    # ---- C2: 3 custom CRAM banks populated + opaque at CRAM[256..303] ----
    cram = _mcs._peek_bytes(sections, CRAM_BG_BASE, n_banks * 16 * 2)
    c2 = cram is not None
    bad = 0
    real = 0
    if cram is not None:
        for bi, bank in enumerate(banks):
            for s in range(16):
                if s == 0 or bank[s] == 0:        # slot 0 / pad = transparent
                    continue
                real += 1
                v = _be16(cram, (bi * 16 + s) * 2)
                if not (v & 0x8000) or (v & 0x7FFF) == 0:   # opaque + non-black
                    bad += 1
        c2 = (real > 0 and bad == 0)
    print(f"C2 CRAM@0x{CRAM_BG_BASE:08X}: {real} real slots, {bad} unset/transparent "
          f"-> {'GREEN' if c2 else 'RED'}")

    # ---- C3: NBG0 display-enable ----
    bgon = _mcs._peek_bytes(sections, VDP2_BGON, 2)
    bg = _be16(bgon, 0) if bgon is not None else 0
    c3 = bool(bg & 0x1)
    print(f"C3 BGON@0x{VDP2_BGON:08X}=0x{bg:04X}: NBG0(bit0)={'on' if c3 else 'OFF'}, "
          f"NBG1(bit1)={'on' if bg & 2 else 'off'} -> {'GREEN' if c3 else 'RED'}")

    ok = c1 and c2 and c3
    print(f"R2.1 AIZ-BG GATE: {'GREEN' if ok else 'RED'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
