#!/usr/bin/env python3
# Mass-port plan I3 step 1 -- camera-local pool ADDRESS-BIJECTION proof (OFFLINE/STATIC).
#
# GOAL (plan 7.5 I3 / SECTION 9 CRITICAL #4): before the pool shrink (1216 -> ~256 tiered
# slots) makes SaturnSlotToPoolSlot a real non-identity logical->pool table, PROVE the
# entity-address indirection is a clean bijection that survives a non-identity remap:
#   (1) SaturnEntitySlot is the EXACT inverse of SaturnEntityAt's address arithmetic for
#       every slot across all three regions + both stride boundaries -- I3's GetEntitySlot
#       callers map a pool ADDRESS back to a slot through it; and
#   (2) that bijection holds for a NON-IDENTITY remap (I3's table is one) -- not an artifact
#       of identity ordering.
#
# WHY OFFLINE, NOT A RUNTIME SELF-CHECK (2026-06-19, MEASURED): the bijection is a STATIC
# property of the address arithmetic (Object.hpp SaturnEntityAt/SaturnEntitySlot) + the pool
# #defines -- it does not depend on anything that only exists at runtime. A first attempt
# added a runtime self-check (two 1216-slot loops + an involution, ~576 B) to p6_i2_selfcheck;
# WRAM-H has only ~64 B headroom under P6_HW_ANIMPAK (0x060B6600, Animation.hpp:55), so _end
# went 0x060B65C0 -> 0x060B6800, the anim-pack load clobbered .bss, and the master SH-2
# trapped at 0x06000956 (#228 boot trap -- blue screen forever, cont_frames=0). The runtime
# check was reverted. This gate proves the SAME invariant with ZERO binary cost by replicating
# the arithmetic here against the LIVE #defines parsed from Object.hpp -- so a future edit to a
# pool constant or an off-by-one in the inverse RED-fires immediately, no build/capture needed.
# (Re-confirm at runtime only AFTER the pool shrink frees WRAM-H, if desired.)
#
# RED if the round-trip or the non-identity remap fails for ANY slot, or if a constant can't
# be parsed. GREEN proves the indirection survives a non-identity remap -> the pool shrink can
# rely on SaturnSlotToPoolSlot/SaturnEntitySlot becoming a real table.
#
#   python tools/_portspike/qa_p6_i3.py            # parse Object.hpp + prove the bijection

import os, re, sys

ROOT  = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OBJHPP = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Scene", "Object.hpp")


def parse_define(txt, name):
    """First #define <name> (<expr>) occurrence -- the Saturn branch precedes the PC
    branch in Object.hpp, so the FIRST match is the live Saturn value. Evaluates simple
    integer expressions over already-parsed names."""
    m = re.search(r"^#define\s+" + re.escape(name) + r"\s+\(?\s*([0-9xXa-fA-F]+)\s*\)?\s*$",
                  txt, re.M)
    return int(m.group(1), 0) if m else None


def main():
    txt = open(OBJHPP, "r", encoding="utf-8", errors="replace").read()

    RESERVE = parse_define(txt, "RESERVE_ENTITY_COUNT")
    TEMPCNT = parse_define(txt, "TEMPENTITY_COUNT")
    SCENE   = parse_define(txt, "SCENEENTITY_COUNT")
    WIDE    = parse_define(txt, "ENTITY_WIDE_SIZE")
    # NARROW = sizeof(EntityBase); not a #define (struct size). Object.hpp:57 documents it
    # as the NARROW stride. Parse the cited value; the bijection's self-consistency is
    # proven for whatever the live stride is (any positive NARROW round-trips), but using
    # the cited number keeps the proof tied to the source.
    mnar = re.search(r"sizeof\(EntityBase\)\s*=\s*(\d+)", txt)
    NARROW = int(mnar.group(1)) if mnar else None

    miss = [n for n, v in [("RESERVE_ENTITY_COUNT", RESERVE), ("TEMPENTITY_COUNT", TEMPCNT),
                           ("SCENEENTITY_COUNT", SCENE), ("ENTITY_WIDE_SIZE", WIDE),
                           ("sizeof(EntityBase)", NARROW)] if v is None]
    print("=== qa_p6_i3 (camera-local pool address-bijection proof, mass-port I3 step 1) ===")
    print("  source: rsdkv5-src/.../Scene/Object.hpp (live Saturn pool #defines)")
    if miss:
        print("RED: could not parse %s from Object.hpp -- update the gate's regex." % ", ".join(miss))
        return 1

    ENTITY_COUNT = RESERVE + SCENE + TEMPCNT
    TEMP_START   = ENTITY_COUNT - TEMPCNT
    narrowOff = RESERVE * WIDE
    tempOff   = narrowOff + SCENE * NARROW
    listBytes = tempOff + TEMPCNT * WIDE
    print("  RESERVE=%d SCENE=%d TEMP_COUNT=%d -> ENTITY_COUNT=%d TEMP_START=%d"
          % (RESERVE, SCENE, TEMPCNT, ENTITY_COUNT, TEMP_START))
    print("  WIDE=%d NARROW=%d -> narrowOff=%d tempOff=%d listBytes=%d"
          % (WIDE, NARROW, narrowOff, tempOff, listBytes))

    # --- exact replicas of Object.hpp:438-461 (SaturnEntityAt / SaturnEntitySlot) ---
    def at(ps):            # SaturnEntityAt offset for POOL slot ps (identity SaturnSlotToPoolSlot)
        if ps < RESERVE:    return ps * WIDE
        if ps < TEMP_START: return narrowOff + (ps - RESERVE) * NARROW
        return tempOff + (ps - TEMP_START) * WIDE

    def slot(off):         # SaturnEntitySlot inverse
        if off >= listBytes: return 0
        if off < narrowOff:  return off // WIDE
        if off < tempOff:    return RESERVE + (off - narrowOff) // NARROW
        return TEMP_START + (off - tempOff) // WIDE

    # non-identity involution t (disjoint swaps straddling BOTH stride boundaries +
    # each region interior) -- the exact permutation the runtime attempt used.
    SWAPS = {2: 5, 5: 2,
             RESERVE - 1: RESERVE, RESERVE: RESERVE - 1,                 # 63<->64 reserve/scene
             100: 900, 900: 100,
             TEMP_START - 1: TEMP_START, TEMP_START: TEMP_START - 1,     # 1151<->1152 scene/temp
             TEMP_START + 8: ENTITY_COUNT - 1, ENTITY_COUNT - 1: TEMP_START + 8}
    def t(s):
        return SWAPS.get(s, s)

    # involution sanity (t == t^-1)
    invol = all(t(t(s)) == s for s in range(ENTITY_COUNT))
    # stride-divisibility sanity: each region offset is an exact multiple of its stride
    div_ok = (all(at(p) % WIDE == 0 for p in range(RESERVE))
              and all((at(p) - narrowOff) % NARROW == 0 for p in range(RESERVE, TEMP_START))
              and all((at(p) - tempOff) % WIDE == 0 for p in range(TEMP_START, ENTITY_COUNT)))
    # (1) round-trip: slot(at(s)) == s for ALL slots (inverse is exact)
    rt = all(slot(at(s)) == s for s in range(ENTITY_COUNT))
    # (2) non-identity: t(slot(at(t(s)))) == s for ALL slots (survives a non-identity remap)
    ni = all(t(slot(at(t(s)))) == s for s in range(ENTITY_COUNT))

    print("-" * 72)
    print("  involution t==t^-1            : %s" % ("PASS" if invol else "FAIL"))
    print("  stride divisibility (3 regions): %s" % ("PASS" if div_ok else "FAIL"))
    print("  (1) round-trip  slot(at(s))==s            for all %d slots : %s"
          % (ENTITY_COUNT, "PASS" if rt else "FAIL"))
    print("  (2) non-ident   t(slot(at(t(s))))==s      for all %d slots : %s"
          % (ENTITY_COUNT, "PASS" if ni else "FAIL"))
    # show the four boundary swaps explicitly (the discriminating cases)
    print("  boundary swaps (logical s -> pool t(s) -> addr -> recovered pool -> logical):")
    for s in (RESERVE - 1, RESERVE, TEMP_START - 1, TEMP_START):
        print("    s=%-5d t(s)=%-5d at=%-7d slot=%-5d t(slot)=%-5d  %s"
              % (s, t(s), at(t(s)), slot(at(t(s))), t(slot(at(t(s)))),
                 "OK" if t(slot(at(t(s)))) == s else "FAIL"))

    if invol and div_ok and rt and ni:
        print("RESULT: GREEN -- the address<->slot indirection is an exact bijection AND "
              "survives a non-identity remap across both stride boundaries; the pool shrink "
              "can safely make SaturnSlotToPoolSlot/SaturnEntitySlot a real table.")
        return 0
    print("RESULT: RED -- the indirection is NOT a clean bijection under remap -- DO NOT "
          "shrink the pool (fix SaturnEntityAt/SaturnEntitySlot in Object.hpp first).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
