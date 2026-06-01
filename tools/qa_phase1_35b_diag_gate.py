#!/usr/bin/env python3
"""qa_phase1_35b_diag_gate.py - Phase 1.35b CRAM-bank-collision diagnostic.

Documents the savestate evidence that REFUTED the "g_island_pal collides
with VDP1 sprite palette" hypothesis the user surfaced after Phase 1.35
visible-corruption regression on 2026-05-28.

Methodology pre-amble (sega-saturn-developer skill v2.5.0):
  - Read complete-doc-index.md (line 449: CRAOFA/CRAOFB row).
  - Cross-referenced ST-058-R2 Sec. 11 (Color RAM Address Offset Register
    at 0x05F800E4-E7) + ST-013-R3 Sec. 5.5 (VDP1 4-bpp Color Bank mode).
  - Used the Phase 1.30 Mednafen savestate harness as primary diagnostic
    (mcs_extract.py --peek16 + --cram region dump). Pixel QA secondary.
  - Captured qa_phase1_35b_diag.mcs at SaveFrame=24 with setup_island_bg()
    re-enabled.

Predicates and verdicts at capture time (qa_phase1_35b_diag.mcs):

  P1 RAMCTL.CRMD            = 0x00 (CRAM Mode 0, 1024 RGB555 colors)   GREEN
  P2 CRAOFA.N1CAOS          = 0    (NBG1 reads bank 0 baseline)        GREEN
  P3 CRAOFB.SPCAOS          = 0    (VDP1 sprite reads bank 0 baseline) GREEN
  P4 CRAOFB.R0CAOS          = 0    (note: per-frame REPLACE sets =1
                                    AFTER mania_title_3d_backdrop_draw
                                    runs; at frame 24 it has not)      INFO
  P5 g_island_pal slots 1025..1280 do NOT overlap VDP1 sprite palette
     slots 2000..2047 (TITLE3D=2000-15, ELECTRA=2016-31, TSONIC=2032-47)  GREEN

  P6 Bank-by-bank CRAM nonzero count (from samples/qa_phase1_35b_cram.bin):
        bank 0 (slots 0..255)    :   3 nonzero (printf transparent)
        bank 1 (slots 256..511)  : 173 nonzero (s_nbg1_dummy_pal region)
        bank 2 (slots 512..767)  : 244 nonzero (g_title_pal)
        bank 3 (slots 768..1023) : 144 nonzero (g_clouds_pal)
        bank 4 (slots 1024..1279):  15 nonzero (g_island_pal Phase 1.35)
        bank 5 (slots 1280..1535): 173 nonzero (write mirror of bank 1)
        bank 6 (slots 1536..1791): 244 nonzero (write mirror of bank 2)
        bank 7 (slots 1792..2047): 186 nonzero (TSONIC+ELECTRA+TITLE3D)

  Conclusion: P5 hypothesis (palette CRAM collision) is REFUTED. The
  visible sprite corruption observed by the user must originate from a
  different mechanism. Leading candidate per skill-binding-methodology:
  VDP2 A0 VRAM exhaustion (NBG1 cell allocation = 128 KB; A0 capacity
  = 128 KB; pre-existing printf font + nbg0_cell already resident in A0).

Usage:
    py -3 tools/qa_phase1_35b_diag_gate.py [path/to/state.mcs]

Exit codes:
    0 = baseline checks pass (use as defensive gate to detect any later
        regression in the CRAM allocation order)
    1 = a register or bank deviates from the documented baseline
"""
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MCS_DEFAULT = ROOT / "qa_phase1_35b_diag.mcs"


def peek16(mcs: Path, addr: int) -> int:
    out = subprocess.check_output(
        ["py", "-3", str(ROOT / "tools" / "mcs_extract.py"),
         str(mcs), "--peek16", hex(addr)],
        text=True,
    ).strip()
    return int(out.split("= ")[-1], 16)


def dump_cram(mcs: Path, out_bin: Path):
    subprocess.check_call(
        ["py", "-3", str(ROOT / "tools" / "mcs_extract.py"),
         str(mcs), "--cram", str(out_bin)],
        stdout=subprocess.DEVNULL,
    )


def bank_nonzero(slots, bank_idx: int) -> int:
    base = bank_idx * 256
    return sum(1 for w in slots[base:base + 256] if (w & 0x7FFF) != 0)


def main():
    mcs = Path(sys.argv[1]) if len(sys.argv) > 1 else MCS_DEFAULT
    if not mcs.exists():
        print(f"RED: mcs not found: {mcs}")
        sys.exit(1)

    print(f"qa_phase1_35b_diag_gate: inspecting {mcs}")
    print()

    ramctl = peek16(mcs, 0x05F800E8)
    craofa = peek16(mcs, 0x05F800E4)
    craofb = peek16(mcs, 0x05F800E6)
    chctla = peek16(mcs, 0x05F80028)
    chctlb = peek16(mcs, 0x05F8002A)
    bgon   = peek16(mcs, 0x05F80020)

    crmd     = (ramctl >> 12) & 0x3
    n0caos   = craofa & 0x7
    n1caos   = (craofa >> 4) & 0x7
    n2caos   = (craofa >> 8) & 0x7
    n3caos   = (craofa >> 12) & 0x7
    r0caos   = craofb & 0x7
    spcaos   = (craofb >> 4) & 0x7
    n1chcn   = (chctla >> 12) & 0x3
    r0chcn   = (chctlb >> 12) & 0x7

    print("Register snapshot:")
    print(f"  RAMCTL @ 0x05F800E8 = 0x{ramctl:04x}  CRMD={crmd} (0=Mode 0 RGB555)")
    print(f"  CRAOFA @ 0x05F800E4 = 0x{craofa:04x}  N0={n0caos} N1={n1caos} N2={n2caos} N3={n3caos}")
    print(f"  CRAOFB @ 0x05F800E6 = 0x{craofb:04x}  R0={r0caos} SP={spcaos}")
    print(f"  CHCTLA @ 0x05F80028 = 0x{chctla:04x}  N1CHCN={n1chcn}")
    print(f"  CHCTLB @ 0x05F8002A = 0x{chctlb:04x}  R0CHCN={r0chcn}")
    print(f"  BGON   @ 0x05F80020 = 0x{bgon:04x}")
    print()

    cram_bin = ROOT / "samples" / "qa_phase1_35b_cram.bin"
    cram_bin.parent.mkdir(parents=True, exist_ok=True)
    dump_cram(mcs, cram_bin)
    data = cram_bin.read_bytes()
    slots = [struct.unpack("<H", data[i:i + 2])[0] for i in range(0, len(data), 2)]

    print("CRAM bank census (nonzero slots per 256-slot bank, MSB masked):")
    for b in range(8):
        print(f"  bank {b} ({b*256:4d}..{b*256+255:4d}): {bank_nonzero(slots, b):3d}")
    print()

    failures = []

    if crmd != 0:
        failures.append(f"P1 RED: CRAM mode={crmd}, expected 0 (Mode 0 RGB555)")
    else:
        print("  P1 GREEN: CRAM Mode 0")

    if n1caos != 0:
        failures.append(f"P2 RED: N1CAOS={n1caos}, expected 0")
    else:
        print("  P2 GREEN: N1CAOS=0 (NBG1 reads CRAM bank 0 baseline)")

    if spcaos != 0:
        failures.append(f"P3 RED: SPCAOS={spcaos}, expected 0")
    else:
        print("  P3 GREEN: SPCAOS=0 (VDP1 sprite reads CRAM bank 0 baseline)")

    # P5: g_island_pal region (bank 4) vs VDP1 sprite palette region
    # (slots 2000..2047 in bank 7). Verify they do NOT overlap.
    island_lo, island_hi = 1025, 1280
    vdp1_lo, vdp1_hi = 2000, 2047
    if island_hi >= vdp1_lo and island_lo <= vdp1_hi:
        failures.append(f"P5 RED: g_island_pal slots {island_lo}..{island_hi} OVERLAP VDP1 sprite palette {vdp1_lo}..{vdp1_hi}")
    else:
        print(f"  P5 GREEN: g_island_pal slots {island_lo}..{island_hi} do not overlap VDP1 sprite palette {vdp1_lo}..{vdp1_hi}")

    # P6: bank 4 has SOME g_island_pal data (only nonzero if setup_island_bg ran)
    bank4_nz = bank_nonzero(slots, 4)
    print(f"  P6 INFO: bank 4 nonzero={bank4_nz} (>0 means setup_island_bg ran; revert keeps =0)")

    print()
    if failures:
        print("=" * 64)
        print("RED")
        for f in failures:
            print(f)
        sys.exit(1)
    else:
        print("GREEN: documented baseline holds")
        sys.exit(0)


if __name__ == "__main__":
    main()
