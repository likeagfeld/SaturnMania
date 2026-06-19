#!/usr/bin/env python3
# Mass-port plan I3 step 2 (WRAM-H funding) -- DE-INLINE SaturnEntityAt (RED-first).
#
# GOAL: the entity-address indirection SaturnEntityAt (Object.hpp) is `inline` and gets
# inlined at ~69 RSDK_ENTITY_AT sites -- MEASURED: 0 SaturnEntityAt symbols in game.map
# (fully inlined; the -fkeep-inline-functions out-of-line copy is gc'd as unreferenced),
# the dual-stride arithmetic (~35 B) duplicated ~69x ~= ~2.4 KB of WRAM-H. WRAM-H has only
# ~64 B headroom under P6_HW_ANIMPAK (0x060B6600), so the camera-local pool's per-access
# remap (SaturnSlotToPoolSlot -> table lookup, I3) has no room. Marking SaturnEntityAt
# `inline __attribute__((noinline))` forces the ~69 sites to CALL one COMDAT-folded copy
# -> that copy is now referenced (survives gc, appears in the map) and the inlined
# duplicates vanish, freeing ~2 KB of WRAM-H to fund the remap. (The cart-overlay choice
# covers the STREAMING code; this de-inline covers the PER-ACCESS remap.)
#
# RED on the current build: SaturnEntityAt is absent from the map (still inlined) AND
# headroom is the 64 B baseline. GREEN after the de-inline: SaturnEntityAt is a defined
# symbol (out-of-line, ONE copy) AND _end dropped so headroom grew >= 1 KB AND _end stays
# under the ANIMPAK clobber line. This is a PERMANENT gate: if a future change re-inlines
# SaturnEntityAt (re-consuming the ~2 KB), it RED-fires.
#
#   python tools/_portspike/qa_p6_deinline.py            # evaluate the current game.map

import os, re, sys

ROOT     = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
MAP      = os.path.join(ROOT, "game.map")
ANIM_HPP = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Graphics", "Animation.hpp")

HEADROOM_MIN = 0x400   # >= 1 KB freed proves the de-inline reclaimed meaningful WRAM-H
BASELINE_HEADROOM = 64 # pre-de-inline _end was 0x060B65C0 = 64 B under ANIMPAK


def sym_addr(text, name):
    m = re.search(r"^\s+0x([0-9a-fA-F]+)\s+" + re.escape(name) + r"\s*$", text, re.M)
    return int(m.group(1), 16) & 0xFFFFFFFF if m else None


def main():
    if not os.path.isfile(MAP):
        print("RED: game.map missing"); return 1
    text = open(MAP, errors="ignore").read()

    # the de-inlined out-of-line copy appears as a (possibly namespace-mangled) symbol.
    # GNU ld -Map prints the demangled-ish C++ name; match SaturnEntityAt either bare or
    # as a substring of a longer demangled line.
    has_sym = bool(re.search(r"\bSaturnEntityAt\b", text))
    end = sym_addr(text, "_end = .") if "_end = ." in text else None
    m_end = re.search(r"0x[0]*([0-9a-fA-F]+)\s+_end = \.", text)
    end = int(m_end.group(1), 16) & 0xFFFFFFFF if m_end else None

    animpak = None
    try:
        ma = re.search(r"#define\s+P6_HW_ANIMPAK\s+(0x[0-9a-fA-F]+)", open(ANIM_HPP, errors="ignore").read())
        animpak = int(ma.group(1), 16) & 0xFFFFFFFF if ma else None
    except OSError:
        pass

    headroom = (animpak - end) if (animpak is not None and end is not None) else None

    print("=== qa_p6_deinline (SaturnEntityAt out-of-line, I3 WRAM-H funding) ===")
    print("  SaturnEntityAt symbol in map : %s" % ("PRESENT (out-of-line)" if has_sym else "ABSENT (still inlined)"))
    print("  _end                         : %s" % ("0x%08X" % end if end is not None else "None"))
    print("  P6_HW_ANIMPAK                : %s" % ("0x%08X" % animpak if animpak is not None else "None"))
    print("  WRAM-H headroom under ANIMPAK : %s  (baseline %d B; want >= %d B)"
          % (("%d B" % headroom) if headroom is not None else "None", BASELINE_HEADROOM, HEADROOM_MIN))

    if end is not None and animpak is not None and end >= animpak:
        print("RESULT: RED -- BOOT TRAP: _end >= ANIMPAK (the de-inline must not push _end UP)."); return 1
    if not has_sym:
        print("RESULT: RED -- SaturnEntityAt is still inlined (no out-of-line symbol). The "
              "noinline de-inline has not landed; ~2 KB of WRAM-H is still spent on ~69 "
              "inlined copies."); return 1
    if headroom is None:
        print("RESULT: RED -- could not measure _end/ANIMPAK headroom."); return 1
    if headroom < HEADROOM_MIN:
        print("RESULT: RED -- SaturnEntityAt is out-of-line but headroom only %d B (< %d B): "
              "the de-inline freed less than expected (LTO re-inline? estimate off?). Investigate."
              % (headroom, HEADROOM_MIN)); return 1
    print("RESULT: GREEN -- SaturnEntityAt is a single out-of-line copy; WRAM-H headroom grew "
          "to %d B (from %d B), funding the I3 per-access remap." % (headroom, BASELINE_HEADROOM))
    return 0


if __name__ == "__main__":
    sys.exit(main())
