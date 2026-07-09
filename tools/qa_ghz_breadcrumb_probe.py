#!/usr/bin/env python3
"""qa_ghz_breadcrumb_probe.py - decode the #192 WRAM-L breadcrumb ring.

Point this at ANY GHZ-gameplay savestate (.mc0/.mcs). It reads the never-
reset breadcrumb ring that src/rsdk/breadcrumb.{c,h} maintains in the 8 KB
WRAM-L slack at 0x002FE000 -- a region measured INTACT through the #192
hard crash (which stomps WRAM-H to a uniform 0x03EF fill). The ring records
the last N GHZ hot-path DMAs (dest+len+source-sample) plus, each V-blank,
the live stack pointer and two WRAM-H probe halfwords.

WHAT IT TELLS YOU
-----------------
On a HEALTHY capture: the ring shows the V-blank DMA (tag 1) repeating with
dest in VDP2 VRAM (0x05E.....) and len 0x2000, a healthy SP (0x060Fxxxx),
and WRAM-H probes != 0x03EF. That PROVES the harness records correctly.

On a CRASH capture: the newest records name the writer --
  * any record whose dest is in WRAM-H [0x06000000,0x06100000)  -> a DMA
    aimed a large fill into Work-RAM-H (the direct stomp writer), OR
  * any record whose val == 0x03EF                              -> the DMA
    source itself is the 0x03EF fill, OR
  * wramh_lo / wramh_hi == 0x03EF  -> the stomp had already reached that
    WRAM-H probe by that V-blank (timestamps the crash to last_tick), OR
  * last_sp outside WRAM-H [0x06000000,0x06100000)  -> the master stack had
    already wandered into A-bus garbage (the stack-smash mechanism).

WRAM-L byte order: mcs_extract returns the raw Mednafen chunk bytes; the
Saturn (big-endian) u32 reconstructs via the documented WorkRAM pair-swap
(b1<<24)|(b0<<16)|(b3<<8)|b2 (task #136). This probe AUTO-DETECTS the order
via the 32-bit magic, so it is robust whether or not the swap applies.

Exit codes:
    0 = ring decoded, NO crash signature in the breadcrumbs (healthy /
        harness proven, or simply no writer caught in this capture)
    1 = crash signature present in the breadcrumbs (WRAM-H dest, 0x03EF
        source/probe, or wandered SP) -- the writer is named above
    2 = ring not initialised (capture predates the build, or GHZ never
        loaded) OR harness gap (WorkRAML missing from the state)
"""
from __future__ import annotations
import argparse
import os
import sys
from pathlib import Path

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

BC_ADDR    = 0x002FE000
WRAML_BASE = 0x00200000
MAGIC      = 0x43425344
SLOTS      = 256
HDR_U32    = 8
REC_U32    = 4
WRAMH_LO   = 0x06000000
WRAMH_HI   = 0x06100000
STOMP_HW   = 0x03EF

TAGS = {
    1: "GHZ_VBLANK_DMA  (scene_ghz.c ghz_fg_vblank slDMAXCopy)",
    2: "GHZ_CELL_DMA    (scene_ghz.c setup slDMACopy cell bank)",
    3: "GHZ_PAGE_DMA    (scene_ghz.c setup slDMAXCopy page push)",
}


def _rd32_pairswap(b, o):
    return ((b[o + 1] << 24) | (b[o + 0] << 16) |
            (b[o + 3] << 8) | b[o + 2]) & 0xFFFFFFFF


def _rd32_be(b, o):
    return ((b[o + 0] << 24) | (b[o + 1] << 16) |
            (b[o + 2] << 8) | b[o + 3]) & 0xFFFFFFFF


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("state", help="GHZ gameplay savestate (.mc0/.mcs)")
    args = ap.parse_args()
    import mcs_extract

    print("GHZ WRAM-L BREADCRUMB PROBE (#192)")
    print("-" * 64)
    if not os.path.exists(args.state):
        print(f"  HARNESS GAP: state not found: {args.state}")
        sys.exit(2)

    s = mcs_extract.parse_savestate(Path(args.state))
    wl = mcs_extract._read_region(s, "MAIN", "WorkRAML")
    if wl is None:
        print("  HARNESS GAP: WorkRAML region not in state")
        sys.exit(2)

    base = BC_ADDR - WRAML_BASE
    if base + (HDR_U32 + SLOTS * REC_U32) * 4 > len(wl):
        print("  HARNESS GAP: WorkRAML too short for the ring")
        sys.exit(2)

    # Auto-detect the decoder via the magic.
    decoder = None
    for name, fn in (("pairswap", _rd32_pairswap), ("bigendian", _rd32_be)):
        if fn(wl, base) == MAGIC:
            decoder, decname = fn, name
            break
    if decoder is None:
        ps = _rd32_pairswap(wl, base)
        be = _rd32_be(wl, base)
        print(f"  ring NOT initialised @0x{BC_ADDR:08x}")
        print(f"    pairswap=0x{ps:08x}  bigendian=0x{be:08x}  "
              f"expected=0x{MAGIC:08x}")
        print("  -> this capture predates the breadcrumb build, or GHZ "
              "never loaded.")
        print("     Build the current ISO, play GHZ, F5, and re-run.")
        sys.exit(2)

    def H(i):
        return decoder(wl, base + i * 4)

    magic, head, count, last_sp, last_tick, wramh_lo, wramh_hi, _flags = \
        (H(i) for i in range(8))

    rec_base = base + HDR_U32 * 4

    def REC(slot):
        o = rec_base + slot * REC_U32 * 4
        return (decoder(wl, o), decoder(wl, o + 4),
                decoder(wl, o + 8), decoder(wl, o + 12))

    print(f"  decoder         : {decname} (magic 0x{magic:08x} OK)")
    print(f"  records written : {count}  (head={head}, ring={SLOTS})")
    mark_sp_ran = (last_sp != 0)
    sp_bad = mark_sp_ran and not (WRAMH_LO <= last_sp < WRAMH_HI)
    if not mark_sp_ran:
        sp_note = "   (mark_sp never ran -- per-frame vblank path not yet hit)"
    elif sp_bad:
        sp_note = "   *** WANDERED out of WRAM-H (stack-smash) ***"
    else:
        sp_note = "   (in WRAM-H stack)"
    print(f"  last_sp         : 0x{last_sp:08x}{sp_note}")
    print(f"  last_tick       : {last_tick}")
    lo_bad = (wramh_lo & 0xFFFF) == STOMP_HW
    hi_bad = (wramh_hi & 0xFFFF) == STOMP_HW
    print(f"  wramh_lo @0x0600F000 = 0x{wramh_lo & 0xFFFF:04x}"
          f"{'   *** 0x03EF STOMP reached low .text ***' if lo_bad else ''}")
    print(f"  wramh_hi @0x060FF000 = 0x{wramh_hi & 0xFFFF:04x}"
          f"{'   *** 0x03EF STOMP reached near-stack ***' if hi_bad else ''}")
    print("-" * 64)

    if count == 0:
        print("  ring armed but EMPTY -- no GHZ DMA recorded yet. Capture a "
              "DEEPER state (play further into GHZ).")
        sys.exit(2)

    nshown = min(count, SLOTS, 24)
    print(f"  last {nshown} records (newest first):")
    wramh_dest_hit = False
    stomp_src_hit = False
    vblank_rec_seen = False
    for k in range(nshown):
        slot = (head - 1 - k) % SLOTS
        tag, dest, length, val = REC(slot)
        if tag == 1:
            vblank_rec_seen = True
        flags = []
        if WRAMH_LO <= dest < WRAMH_HI:
            flags.append("DEST-IN-WRAM-H")
            wramh_dest_hit = True
        if (val & 0xFFFF) == STOMP_HW or val == 0x03EF03EF:
            flags.append("SRC=0x03EF")
            stomp_src_hit = True
        tagname = TAGS.get(tag, f"tag {tag}")
        fs = ("   <<< " + " ".join(flags)) if flags else ""
        print(f"    [{slot:3d}] {tagname}")
        print(f"          dest=0x{dest:08x} len=0x{length:08x} "
              f"src_hw=0x{val & 0xFFFF:04x}{fs}")
    print("-" * 64)

    if (not mark_sp_ran and not vblank_rec_seen
            and not (lo_bad or hi_bad or wramh_dest_hit or stomp_src_hit)):
        print("  INCOMPLETE: only one-time GHZ setup DMAs recorded; the "
              "per-frame")
        print("  ghz_fg_vblank breadcrumb never fired (mark_sp never ran, no "
              "tag-1 record).")
        print("  This capture is too shallow -- it landed before GHZ "
              "gameplay or in a")
        print("  static no-input moment. Capture a DEEPER state during GHZ "
              "gameplay WITH")
        print("  player movement (hold Right), then re-run. A real crash "
              "DOES record")
        print("  tag-1 records, so this exit-2 path cannot mask one.")
        sys.exit(2)

    red = wramh_dest_hit or stomp_src_hit or lo_bad or hi_bad or sp_bad
    if red:
        why = []
        if wramh_dest_hit:
            why.append("a DMA targets WRAM-H (direct fill writer)")
        if stomp_src_hit:
            why.append("a DMA source IS the 0x03EF fill")
        if lo_bad or hi_bad:
            why.append(f"0x03EF stomp present at last_tick={last_tick}")
        if sp_bad:
            why.append(f"master stack wandered to 0x{last_sp:08x}")
        print("  RED: crash signature in the breadcrumbs -- " +
              "; ".join(why))
        sys.exit(1)

    print("  GREEN: ring decoded, no WRAM-H-dest / 0x03EF / wandered-SP "
          "signature in the last records.")
    print("         (On a HEALTHY capture this is the harness-works proof: "
          "tag 1 DMA into VDP2 VRAM, len 0x2000, SP in WRAM-H.)")
    sys.exit(0)


if __name__ == "__main__":
    main()
