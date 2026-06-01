#!/usr/bin/env python3
"""qa_phase2_3l_gate.py - Phase 2.3l RED/GREEN gate.

Verifies SKY.DAT load failure (bit 5 of g_ghz_load_error_code) is
resolved by the LWRAM bypass that mirrors the Phase 2.2c FG.TMP pattern.

Predicates:
  P1 - g_ghz_load_error_code == 0
       (no sub-asset load failed; specifically bit 5 = SKY.DAT cleared)
  P2 - WRAM-L at SKY.DAT base (0x00260000) first 16 bytes are non-zero
       (SKY.DAT actually landed; we don't know exact header bytes since
        cd/GHZ1SKY.DAT is a raw 176x512 8bpp blob with no magic header,
        but any successful load produces non-zero pixel data in the
        first scanline — sky gradient is never solid black).
  P3 - g_ghz_active_tick_counter > 0
       (mania_ghz_tick_and_draw running; TS_GHZ_ACTIVE reached)
  P4 - s_ts_state == 6 (TS_GHZ_ACTIVE)
  P5 - VDP2 BGON includes NBG1ON|NBG2ON|SPRON (bits 1, 2, 6)

On RED build (SKY.DAT load fails via jo_fs_read_file pool exhaustion):
  P1 RED: bit 5 fires (0x20)
  P2 RED: WRAM-L at 0x00260000 is zeros (LWRAM never written)
  P3 RED: tick counter == 0 (state machine stuck pre-TS_GHZ_ACTIVE)
  P4 RED: state stuck at TS_TRANSITION_TO_GHZ (5) or earlier
  P5 may be RED (NBG2 not enabled if SKY.DAT failed)

On GREEN build: all five predicates pass.

Symbol addresses (from game.map after Phase 2.3k-mid):
  g_ghz_load_error_code      = 0x060322F8
  g_ghz_active_tick_counter  = 0x060322F0
  s_ts_state                 = 0x060322F4

WRAM-L SKY.DAT base = 0x00260000 per scene_ghz.c GHZ_SKY_DAT_LWRAM_ADDR.
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

G_GHZ_LOAD_ERROR_CODE_ADDR = 0x060322B8
G_GHZ_ACTIVE_TICK_COUNTER_ADDR = 0x060322B0
S_TS_STATE_ADDR = 0x060322B4
SKY_DAT_LWRAM_ADDR = 0x00260000
VDP2_BGON_ADDR = 0x25F80020


def _peek32_wram(sections, addr):
    """Read 32-bit big-endian Saturn value from WRAM-H/L.

    Mednafen's WorkRAMH/WorkRAML chunks store bytes pair-swapped relative
    to SH-2 view. Each 16-bit word has MSB/LSB flipped. To recover SH-2-
    visible big-endian we un-swap each pair before interpreting.
    Mirrors qa_phase2_3k_main_gate._peek32_wram.
    """
    b = _peek_bytes(sections, addr, 4)
    if b is None or len(b) < 4:
        return None
    fixed = bytes([b[1], b[0], b[3], b[2]])
    return struct.unpack(">I", fixed)[0]


def _peek_bytes_wram(sections, addr, n):
    """Pair-swap WRAM-L/H bytes so they read in SH-2-visible order."""
    b = _peek_bytes(sections, addr, n)
    if b is None or len(b) < n:
        return None
    # Pair-swap each 16-bit word: [b0,b1,b2,b3,...] -> [b1,b0,b3,b2,...]
    out = bytearray(n)
    for i in range(0, n & ~1, 2):
        out[i]   = b[i+1]
        out[i+1] = b[i]
    if n & 1:
        out[n-1] = b[n-1]
    return bytes(out)


def _peek16_vdp2(sections, addr):
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
    sky_first16 = _peek_bytes_wram(sections, SKY_DAT_LWRAM_ADDR, 16)
    bgon = _peek16_vdp2(sections, VDP2_BGON_ADDR)

    if err is None or tick is None or ts_state is None or sky_first16 is None or bgon is None:
        print("qa_phase2_3l_gate: ERROR - one or more peeks returned None")
        print(f"  err={err} tick={tick} ts={ts_state} sky={sky_first16} bgon={bgon}")
        return 2

    sky_nonzero_bytes = sum(1 for b in sky_first16 if b != 0)

    p1 = (err == 0)
    # P2: at least 4 of the first 16 bytes non-zero. Even a sky-blue
    # gradient indexed-palette image has non-zero palette indices (sky
    # palette index 0 is rarely the first pixel; typical SKY.DAT first
    # scanline holds the top-row sky-color palette indices).
    p2 = (sky_nonzero_bytes >= 4)
    p3 = (tick > 0)
    p4 = (ts_state == 6)
    # NBG1ON=bit1 (0x02), NBG2ON=bit2 (0x04), SPRON=bit6 (0x40).
    bgon_mask = 0x02 | 0x04 | 0x40
    p5 = ((bgon & bgon_mask) == bgon_mask)

    def verdict(name, ok, detail):
        tag = "GREEN" if ok else "RED  "
        print(f"  [{tag}] {name}: {detail}")
        return ok

    print(f"qa_phase2_3l_gate: {args.state}")
    print(f"  raw values:")
    print(f"    g_ghz_load_error_code        = 0x{err:08X}")
    print(f"    g_ghz_active_tick_counter    = {tick}")
    print(f"    s_ts_state (decimal)         = {ts_state} (0=FADE_IN 1=FLASH 2=WAIT_SONIC 3=PRESS_REVEAL 4=WAIT_ENTER 5=TRANSITION_TO_GHZ 6=GHZ_ACTIVE)")
    print(f"    WRAM-L 0x00260000 first 16 B = {sky_first16.hex(' ')}")
    print(f"    WRAM-L 0x00260000 nonzero B  = {sky_nonzero_bytes}/16")
    print(f"    VDP2 BGON @ 0x25F80020       = 0x{bgon:04X}")
    print(f"  predicates:")
    all_ok = True
    all_ok &= verdict("P1 load_error_code==0", p1,
                      f"got 0x{err:08X} (bits: "
                      f"FG.TMP={(err>>0)&1} FG.PAL={(err>>1)&1} "
                      f"FG.CEL={(err>>2)&1} FG.PAT={(err>>3)&1} "
                      f"SKY.PAL={(err>>4)&1} SKY.DAT={(err>>5)&1})")
    all_ok &= verdict("P2 SKY.DAT LWRAM populated (>=4/16 nonzero)", p2,
                      f"got {sky_nonzero_bytes}/16 nonzero bytes")
    all_ok &= verdict("P3 active_tick_counter>0", p3,
                      f"got {tick}")
    all_ok &= verdict("P4 s_ts_state==TS_GHZ_ACTIVE(6)", p4,
                      f"got {ts_state}")
    all_ok &= verdict("P5 BGON has NBG1ON|NBG2ON|SPRON", p5,
                      f"got BGON=0x{bgon:04X}, masked=0x{bgon&bgon_mask:02X}, need 0x{bgon_mask:02X}")

    if all_ok:
        print("qa_phase2_3l_gate: PASS (GREEN)")
        return 0
    print("qa_phase2_3l_gate: FAIL (RED)")
    return 1


if __name__ == "__main__":
    sys.exit(main())
