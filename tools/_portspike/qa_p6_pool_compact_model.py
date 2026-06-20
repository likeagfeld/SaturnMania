#!/usr/bin/env python3
# =============================================================================
# qa_p6_pool_compact_model.py -- OFFLINE proof of the camera-local pool RELOCATION
# byte-copy plan (the I3b shrink's scariest mechanic). ZERO binary cost.
#
# WHY OFFLINE (the skill's "prove static properties offline" rule, like qa_p6_i3):
# the shrink relocates live entity DATA in the WRAM-L backing -- a single mis-ordered
# memcpy corrupts an entity's bytes (subtle breakage). The aliasing-safety of the
# in-place ascending copy is a STATIC property of the address arithmetic + the pool
# #defines; prove it HERE so an off-by-one or a constant change RED-fires instantly,
# with no 10-minute build+capture cycle. (Reconfirm at runtime via the R0-R16 + the
# compaction gate; this gate is the byte-plan's correctness proof, GREEN by
# construction like qa_p6_i3 -- the RED-first runtime gate is qa_p6_pool_compact.py.)
#
# THE PLAN BEING PROVEN (compaction = streaming with an all-near classification):
#   SRC layout (pre-compaction, identity sphys = SCENEENTITY_COUNT = SRC_SPHYS):
#     scene L in [R, R+SRC_SPHYS): src_byte = R*WIDE + (L-R)*NARROW
#     temp  L in [R+SRC_SPHYS, R+SRC_SPHYS+TEMP): src_byte = R*WIDE + SRC_SPHYS*NARROW + (L-(R+SRC_SPHYS))*WIDE
#   Compact: the populated scene slots L_0<L_1<...<L_{n-1} map L_r -> physical P = R+r
#            (dense, ascending). NEW sphys = n+1 (the last scene-physical slot R+n is
#            the reserved DUMMY -- classID=0 -- that every EMPTY logical slot remaps to,
#            so RSDK_ENTITY_AT(empty L) is always safe). Temp shifts 1:1 down to
#            physical [R+NEW, R+NEW+TEMP).
#   DST layout (post, sphys = NEW):
#     scene P in [R, R+NEW): dst_byte = R*WIDE + (P-R)*NARROW
#     temp  P in [R+NEW, R+NEW+TEMP): dst_byte = R*WIDE + NEW*NARROW + (P-(R+NEW))*WIDE
#
# ASSERTIONS (must hold for EVERY adversarial populated set, not just GHZ1's):
#   A1 scene ascending copy is clobber-free: dst(R+r) <= src(L_r) for all r
#      (so copying r = 0,1,2,... writes a LOWER address than it reads -> the source
#       L_r, which is >= R+r, has not yet been overwritten).
#   A2 temp src region lies entirely ABOVE the temp dst region (no scene/temp overlap),
#      so the temp 1:1 shift is safe regardless of order (scene-first still required for
#      A1's "scene src consumed" only matters if temp dst overlapped scene src -- A2
#      proves it does NOT).
#   A3 bijection: L_r -> R+r is injective into [R, R+n); the inverse inv[R+r]=L_r
#      round-trips; empty logical slots map to the single DUMMY R+n.
#   A4 dummy in-range: R+n < R+NEW (dummy is a valid scene-physical slot) and the
#      whole physical pool end (R+NEW+TEMP) fits the backing ENTITYLIST_SIZE_BYTES.
#
# RED if any assertion fails for any modelled set, or a constant can't be parsed.
#
#   python tools/_portspike/qa_p6_pool_compact_model.py
# =============================================================================
import os, re, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
OBJHPP = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Scene", "Object.hpp")


def parse_define(txt, name):
    m = re.search(r"^#define\s+" + re.escape(name) + r"\s+\(?\s*([0-9xXa-fA-F]+)\s*\)?\s*$", txt, re.M)
    return int(m.group(1), 0) if m else None


def model_compaction(R, SRC_SPHYS, TEMP, WIDE, NARROW, backing, populated):
    """Return (ok, detail) for one adversarial populated set (sorted ascending list
    of scene logical slots in [R, R+SRC_SPHYS))."""
    n = len(populated)
    NEW = n + 1  # +1 reserves the dummy at physical R+n
    # PRECONDITION (compaction-to-full domain): the +1 dummy must fit inside the scene
    # region, i.e. NEW <= SRC_SPHYS  <=>  n <= SRC_SPHYS-1. A completely-full scene
    # (n == SRC_SPHYS == 1088) has no slot to reserve -> that case is handled by
    # STREAMING (scene_phys=640, far->dormant), never by compaction-to-full. GHZ1's
    # 856 and every streaming near-set (<=640) satisfy this with huge margin.
    if NEW > SRC_SPHYS:
        return False, "PRECOND n=%d > SRC_SPHYS-1=%d (no dummy room; use streaming)" % (n, SRC_SPHYS - 1)
    # address arithmetic ------------------------------------------------------
    def src_scene(L): return R * WIDE + (L - R) * NARROW
    def src_temp(L):  return R * WIDE + SRC_SPHYS * NARROW + (L - (R + SRC_SPHYS)) * WIDE
    def dst_scene(P): return R * WIDE + (P - R) * NARROW
    def dst_temp(P):  return R * WIDE + NEW * NARROW + (P - (R + NEW)) * WIDE

    # The implementation copies PER-ENTITY in ascending physical order, SCENE-REGION
    # FIRST then temp (single forward pass over the NEW physical layout, memcpy per
    # entity). The proof models exactly that.
    # A1 scene per-entity ascending: each scene write ends at/before the next scene
    #    source begins -> never clobbers an unread scene source.
    for r, L in enumerate(populated):
        if dst_scene(R + r) > src_scene(L):
            return False, "A1 FAIL r=%d L=%d dst=%d > src=%d" % (r, L, dst_scene(R + r), src_scene(L))
        if r + 1 < n and dst_scene(R + r) + NARROW > src_scene(populated[r + 1]):
            return False, "A1 FAIL r=%d write-end %d > next src %d" % (r, dst_scene(R + r) + NARROW, src_scene(populated[r + 1]))
    # A2 scene writes never reach the temp source region (highest scene dst < temp src base)
    scene_dst_end = (dst_scene(R + n - 1) + NARROW) if n else (R * WIDE)
    temp_src_lo   = src_temp(R + SRC_SPHYS)
    if scene_dst_end > temp_src_lo:
        return False, "A2 FAIL scene_dst_end=%d > temp_src_lo=%d" % (scene_dst_end, temp_src_lo)
    # A2b temp per-entity ascending (1:1 shift down), scene already consumed -> temp
    #    writes into the now-stale upper scene region are safe; assert each temp write
    #    ends at/before the next temp source (no unread-temp-source clobber).
    for t in range(TEMP):
        Pd, Ls = R + NEW + t, R + SRC_SPHYS + t
        if dst_temp(Pd) > src_temp(Ls):
            return False, "A2b FAIL t=%d dst=%d > src=%d" % (t, dst_temp(Pd), src_temp(Ls))
        if t + 1 < TEMP and dst_temp(Pd) + WIDE > src_temp(R + SRC_SPHYS + t + 1):
            return False, "A2b FAIL t=%d write-end %d > next temp src %d" % (t, dst_temp(Pd) + WIDE, src_temp(R + SRC_SPHYS + t + 1))
    # A3 bijection
    fwd = {L: R + r for r, L in enumerate(populated)}
    inv = {R + r: L for r, L in enumerate(populated)}
    if len(set(fwd.values())) != n:
        return False, "A3 FAIL forward not injective"
    if any(inv[P] != L or fwd[L] != P for L, P in fwd.items()):
        return False, "A3 FAIL inverse does not round-trip"
    if any(P < R or P >= R + n for P in fwd.values()):
        return False, "A3 FAIL physical out of dense range [R,R+n)"
    # A4 dummy in-range + pool end fits the backing
    dummy = R + n
    if not (dummy < R + NEW):
        return False, "A4 FAIL dummy %d not < R+NEW %d" % (dummy, R + NEW)
    pool_end = dst_temp(R + NEW + TEMP - 1) + WIDE
    if pool_end > backing:
        return False, "A4 FAIL pool_end=%d > backing=%d" % (pool_end, backing)
    return True, "n=%d NEW=%d dummy=%d pool_end=%d/%d" % (n, NEW, dummy, pool_end, backing)


def main():
    txt = open(OBJHPP, "r", encoding="utf-8", errors="replace").read()
    R    = parse_define(txt, "RESERVE_ENTITY_COUNT")
    SCN  = parse_define(txt, "SCENEENTITY_COUNT")
    TEMP = parse_define(txt, "TEMPENTITY_COUNT")
    WIDE = parse_define(txt, "ENTITY_WIDE_SIZE")
    mnar = re.search(r"sizeof\(EntityBase\)\s*=\s*(\d+)", txt)
    NARROW = int(mnar.group(1)) if mnar else None

    print("=== qa_p6_pool_compact_model (OFFLINE relocation byte-plan proof, I3b 2b) ===")
    print("  source: rsdkv5-src/.../Scene/Object.hpp (live pool #defines)")
    miss = [k for k, v in [("RESERVE", R), ("SCENEENTITY_COUNT", SCN), ("TEMPENTITY_COUNT", TEMP),
                           ("ENTITY_WIDE_SIZE", WIDE), ("sizeof(EntityBase)", NARROW)] if v is None]
    if miss:
        print("RED: could not parse %s -- update the regex." % ", ".join(miss))
        return 1
    backing = R * WIDE + SCN * NARROW + TEMP * WIDE
    print("  R=%d SCENEENTITY_COUNT=%d TEMP=%d WIDE=%d NARROW=%d  backing=%d B"
          % (R, SCN, TEMP, WIDE, NARROW, backing))

    # adversarial populated sets over the scene region [R, R+SCN):
    #   - DENSEST: the first n slots (L_r = R+r) -> dst==src boundary (tightest A1)
    #   - HIGHEST: the last n slots (max downward shift -> stress the copy)
    #   - SPREAD : every other slot (holes interleaved, the real-world shape)
    #   - GHZ1 real n = 856 in all three shapes
    # Supported compaction-to-full domain: n in [0, SCN-1]. Test the streaming near-set
    # sizes (<=640), GHZ1's measured 856, and the ceiling SCN-1 (boundary NEW==SCN).
    cases = []
    for n in (1, 64, 640, 856, SCN - 1):  # GHZ1=856; SCN-1=1087 is the dummy-room ceiling
        if n < 1 or n > SCN - 1:
            continue
        cases.append(("densest", n, list(range(R, R + n))))
        cases.append(("highest", n, list(range(R + SCN - n, R + SCN))))
        spread = [R + i for i in range(0, SCN, 2)][:n]
        if len(spread) == n:
            cases.append(("spread", n, spread))

    print("-" * 72)
    allok = True
    shown = 0
    for tag, n, pop in cases:
        ok, detail = model_compaction(R, SCN, TEMP, WIDE, NARROW, backing, pop)
        allok = allok and ok
        if not ok or shown < 8 or n in (856, SCN - 1):
            print("  [%s] %-8s n=%-4d : %s" % ("GREEN" if ok else " RED ", tag, n, detail))
            shown += 1

    print("-" * 72)
    if allok:
        print("RESULT: GREEN -- the per-entity scene-first relocation is clobber-free (A1 scene,")
        print("        A2b temp), scene writes never reach temp src (A2), the remap is a")
        print("        bijection with a reserved dummy (A3/A4) for EVERY adversarial shape over")
        print("        the supported domain n<=%d (incl. GHZ1's 856 + every streaming near-set" % (SCN - 1))
        print("        <=640). The runtime p6_pool_compact byte-plan is proven safe.")
        return 0
    print("RESULT: RED -- a relocation assertion failed; DO NOT relocate (fix the plan).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
