#!/usr/bin/env python3
"""qa_phase2_3k_main_gate.py - Phase 2.3k main RED/GREEN gate.

Verifies the four post-fix predicates for the GHZ1FG.TMP load:

  P1 - g_ghz_load_error_code == 0      (no sub-asset load failed)
  P2 - WRAM-L first 4 bytes at FG.TMP base match the BE header
       xs=0x0400 ys=0x0080 (per Phase 2.3k iter-1 inspection +
       Phase 2.3k main hypothesis verification: cd/GHZ1FG.TMP first
       4 bytes are 04 00 00 80)
  P3 - g_ghz_active_tick_counter > 0   (mania_ghz_tick_and_draw ran;
                                        TS_GHZ_ACTIVE reached)
  P4 - VDP2 BGON bit 1 set (NBG1 enabled = ghz_setup_foreground
       reached the jo_vdp2_set_nbg1_8bits_image commit block)

On RED build (FG.TMP load fails): P1 = bit 0 fires (0x01), P2 = zeros
(WRAM-L empty), P3 may still be > 0 if state machine wrongly advances,
P4 will be 0 because ghz_setup_foreground returned false BEFORE the
NBG1 config block at scene_ghz.c:303.

On GREEN build: all four predicates pass.

Symbol addresses (from game.map after Phase 2.3k-mid):
  g_ghz_load_error_code      = 0x060322D8
  g_ghz_active_tick_counter  = 0x060322D0

WRAM-L FG.TMP base = 0x00210000 per scene_ghz.c:50.
VDP2 BGON = 0x25F80020 (uint16) per ST-058-R2.
"""

import argparse
import os
import struct
import sys
from pathlib import Path

THIS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, THIS)
from mcs_extract import parse_savestate, _peek_bytes  # type: ignore

G_GHZ_LOAD_ERROR_CODE_ADDR = 0x060322F8
G_GHZ_ACTIVE_TICK_COUNTER_ADDR = 0x060322F0
S_TS_STATE_ADDR = 0x060322F4
FG_TMP_LWRAM_ADDR = 0x00210000
VDP2_BGON_ADDR = 0x25F80020


def _peek32_wram(sections, addr):
    """Read 32-bit big-endian Saturn value from WRAM-H/L.

    Mednafen's WorkRAMH/WorkRAML chunks store bytes pair-swapped
    relative to SH-2 view (verified empirically vs the vector table
    at WRAM-H offset 0 + ASCII strings around 0x322FC). Each 16-bit
    word has MSB/LSB flipped. To recover SH-2-visible big-endian, we
    must un-swap each pair before interpreting.
    """
    b = _peek_bytes(sections, addr, 4)
    if b is None or len(b) < 4:
        return None
    # Pair-swap: [b0,b1,b2,b3] -> [b1,b0,b3,b2]
    fixed = bytes([b[1], b[0], b[3], b[2]])
    return struct.unpack(">I", fixed)[0]


def _peek16_wram(sections, addr):
    b = _peek_bytes(sections, addr, 2)
    if b is None or len(b) < 2:
        return None
    # Pair-swap single word.
    fixed = bytes([b[1], b[0]])
    return struct.unpack(">H", fixed)[0]


def _peek16_vdp2(sections, addr):
    """VDP2 RawRegs already swapped by _peek_bytes; use raw BE."""
    b = _peek_bytes(sections, addr, 2)
    if b is None or len(b) < 2:
        return None
    return struct.unpack(">H", b)[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("state", help="Mednafen .mc0/.mcs file")
    args = ap.parse_args()

    sections = parse_savestate(Path(args.state))

    err = _peek32_wram(sections, G_GHZ_LOAD_ERROR_CODE_ADDR)
    tick = _peek32_wram(sections, G_GHZ_ACTIVE_TICK_COUNTER_ADDR)
    ts_state = _peek32_wram(sections, S_TS_STATE_ADDR)
    fg_xs = _peek16_wram(sections, FG_TMP_LWRAM_ADDR)
    fg_ys = _peek16_wram(sections, FG_TMP_LWRAM_ADDR + 2)
    bgon = _peek16_vdp2(sections, VDP2_BGON_ADDR)

    if err is None or tick is None or ts_state is None or fg_xs is None or fg_ys is None or bgon is None:
        print("qa_phase2_3k_main_gate: ERROR - one or more peeks returned None")
        print(f"  err={err} tick={tick} ts={ts_state} fg_xs={fg_xs} fg_ys={fg_ys} bgon={bgon}")
        return 2

    p1 = (err == 0)
    p2 = (fg_xs == 0x0400 and fg_ys == 0x0080)
    p3 = (tick > 0)
    p4 = ((bgon & 0x0002) != 0)

    def verdict(name, ok, detail):
        tag = "GREEN" if ok else "RED  "
        print(f"  [{tag}] {name}: {detail}")
        return ok

    print(f"qa_phase2_3k_main_gate: {args.state}")
    print(f"  raw values:")
    print(f"    g_ghz_load_error_code        = 0x{err:08X}")
    print(f"    g_ghz_active_tick_counter    = {tick}")
    print(f"    s_ts_state (decimal)         = {ts_state} (0=FADE_IN 1=FLASH 2=WAIT_SONIC 3=PRESS_REVEAL 4=WAIT_ENTER 5=TRANSITION_TO_GHZ 6=GHZ_ACTIVE)")
    print(f"    WRAM-L 0x00210000 u16 (xs)   = 0x{fg_xs:04X}")
    print(f"    WRAM-L 0x00210002 u16 (ys)   = 0x{fg_ys:04X}")
    print(f"    VDP2 BGON @ 0x25F80020       = 0x{bgon:04X}")
    print(f"  predicates:")
    all_ok = True
    all_ok &= verdict("P1 load_error_code==0", p1,
                      f"got 0x{err:08X} (bits: "
                      f"FG.TMP={(err>>0)&1} FG.PAL={(err>>1)&1} "
                      f"FG.CEL={(err>>2)&1} FG.PAT={(err>>3)&1} "
                      f"SKY.PAL={(err>>4)&1} SKY.DAT={(err>>5)&1})")
    all_ok &= verdict("P2 FG.TMP header xs=0x0400 ys=0x0080", p2,
                      f"got xs=0x{fg_xs:04X} ys=0x{fg_ys:04X}")
    all_ok &= verdict("P3 active_tick_counter>0", p3,
                      f"got {tick}")
    all_ok &= verdict("P4 BGON NBG1ON (bit 1) set", p4,
                      f"got BGON=0x{bgon:04X}, NBG1ON={'YES' if p4 else 'NO'}")

    if all_ok:
        print("qa_phase2_3k_main_gate: PASS (GREEN)")
        return 0
    print("qa_phase2_3k_main_gate: FAIL (RED)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
