#!/usr/bin/env python3
# =============================================================================
# qa_p6_lifecycle_index.py -- I3b 2b STREAMING lifecycle-bitfield bounds proof (BINDING, static).
#
# WHY (2026-06-20, adversarial audit of the committed streaming 1b1111c): the per-frame stream's
# lifecycle (destroyed) bitfield P6_STREAM_LIFE is indexed by the LOGICAL slot inside the loop
#   for (L = R; L < R + SCN; ++L) ... life[<idx> >> 3] |= (1 << (<idx> & 7))
# but the array is sized (SCENEENTITY_COUNT+7)/8 bytes and the compact init zeros exactly that many.
# If <idx> is the bare logical slot L (which carries the +RESERVE offset), the top RESERVE logical
# slots [SCN, R+SCN) index PAST the array -- an OOB read of UN-ZEROED cart RAM. For GHZ1 that hits
# the 17 highest-slotID scene entities (slotID 1024-1040 = ALL PlaneSwitches, incl. the x=3352/3512
# path-switches the player reaches first): a garbage 1-bit there flags the PlaneSwitch "destroyed"
# so the stream NEVER materializes it -> the path-switch silently fails (player takes the wrong
# layer at a loop). It is emulator/HW-state dependent (Mednafen happened to read 0), so NO runtime
# capture reliably catches it -- this STATIC bounds proof is the deterministic gate.
#
# The index MUST subtract R: life[(L - R) >> 3] -> the index lands in [0, SCN), exactly the zeroed
# range. (p6_scan_near is indexed by bare L and is correctly sized (ENTITY_COUNT+7)/8 = in-bounds;
# the bug was copying that index pattern but sizing life for SCENEENTITY_COUNT instead.)
#
#   S1 constants parsed     R (RESERVE_ENTITY_COUNT) + SCN (SCENEENTITY_COUNT) from live Object.hpp
#   S2 index base detected  every life[...] access subtracts R (index-by-(L-R)), none uses bare L
#   S3 in-bounds            max byte index < (SCN+7)/8 (the array size)
#
# RED on the committed bug (index-by-L: max byte (R+SCN-1)>>3 = 143 >= 136). GREEN after the (L-R)
# fix (max byte (SCN-1)>>3 = 135 < 136). Zero binary cost; run on any change to the stream's
# lifecycle indexing or the pool geometry (a wider SCN keeps this honest too).
#
#   python tools/_portspike/qa_p6_lifecycle_index.py
# =============================================================================
import os, re, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OBJHPP = os.path.join(ROOT, "rsdkv5-src/RSDKv5/RSDK/Scene/Object.hpp")
OVL    = os.path.join(ROOT, "tools/_portspike/_p6/p6_ovl_ghz.c")


def _read(p):
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def _first_define(text, name):
    # the Saturn branch is FIRST in Object.hpp (the RETRO_SATURN #if precedes the stock sizes);
    # the overlay's p6_eng_pool_geom returns exactly these defines at runtime.
    m = re.search(r"#define\s+%s\s+\(?\s*(0x[0-9A-Fa-f]+|\d+)" % re.escape(name), text)
    return int(m.group(1), 0) if m else None


def main():
    objhpp = _read(OBJHPP)
    ovl    = _read(OVL)
    R   = _first_define(objhpp, "RESERVE_ENTITY_COUNT")
    SCN = _first_define(objhpp, "SCENEENTITY_COUNT")

    print("=== qa_p6_lifecycle_index (streaming lifecycle-bitfield bounds, static) ===")
    print("  RESERVE_ENTITY_COUNT R = %s" % R)
    print("  SCENEENTITY_COUNT  SCN = %s" % SCN)
    s1 = R is not None and SCN is not None
    if not s1:
        print("  [ RED ] S1 constants parsed (R=%s SCN=%s)" % (R, SCN))
        print("RESULT: RED -- could not parse the live pool constants.")
        return 1

    # Every lifecycle access: capture the index expression inside life[ ... >> 3 ]. Exclude the
    # init zero-loop (life[q]). A bare-L index (no '- R') carries the +R offset = OOB-prone.
    accesses = re.findall(r"life\[\s*(.*?)\s*>>\s*3\s*\]", ovl)
    accesses = [a for a in accesses if a.strip() != "q"]
    if not accesses:
        print("  [ RED ] S2 no life[...>>3] lifecycle accesses found (parser stale?)")
        return 1
    # An access subtracts the RESERVE offset if it contains "- R" (the local R), for ANY index var
    # (the retire/skip loops use L, the backtrack-proof recorder uses tl) -- all land in [0,SCN). A BARE
    # index (no "- R") carries the +R offset and overflows. "- RESERVE_X" won't false-match (R\b needs a
    # word boundary, and RESERVE_X has E after R).
    subtracts_R = [bool(re.search(r"-\s*R\b", a)) for a in accesses]
    bare_L      = [(not sub) for sub in subtracts_R]
    offset = R if any(bare_L) else 0   # any bare-L access => the +R offset is in play
    array_bytes = (SCN + 7) // 8
    max_byte = (offset + SCN - 1) >> 3

    print("-" * 70)
    print("  lifecycle index expressions: %s" % accesses)
    print("  index base = %s   (offset added to the [0,SCN) slot = %d)"
          % ("bare L (+RESERVE)" if offset else "(L - R)", offset))
    print("  array size = (SCN+7)/8 = %d bytes  -> valid byte indices [0, %d]" % (array_bytes, array_bytes - 1))
    print("  worst access: L=%d -> life[%d]" % (offset + SCN - 1, max_byte))
    print("-" * 70)

    s2 = not any(bare_L)
    s3 = max_byte < array_bytes
    print("  [%s] S1 constants parsed" % "GREEN")
    print("  [%s] S2 index subtracts RESERVE (no bare-L access)" % ("GREEN" if s2 else " RED "))
    print("  [%s] S3 max byte index (%d) < array bytes (%d)" % ("GREEN" if s3 else " RED ", max_byte, array_bytes))
    if s2 and s3:
        print("RESULT: GREEN -- the lifecycle bitfield is indexed in-bounds over [0,SCN); the top "
              "%d logical slots no longer read un-zeroed OOB cart garbage." % R)
        return 0
    print("RESULT: RED -- life[L>>3] with L in [R,R+SCN) overflows the (SCN+7)/8-byte array by "
          "%d bytes; the top %d scene slots (GHZ1: 17 PlaneSwitches, slotID 1024-1040) read "
          "un-zeroed cart RAM -> a stray destroyed-bit makes them invisible. Index by (L - R)."
          % (max_byte - array_bytes + 1, R))
    return 1


if __name__ == "__main__":
    sys.exit(main())
