#!/usr/bin/env python3
# =============================================================================
# qa_p6_occupancy.py -- mass-port I3-S1: runtime ENTITY-POOL OCCUPANCY census.
#
# Measures, from a captured savestate (NO firmware change, NO rebuild -- the
# Mednafen savestate harness is the project's PRIMARY diagnostic, CLAUDE.md 8.5),
# how many entity slots are MATERIALIZED (would need a full EntityBase in the I3
# shrunk pool) at the capture instant. This validates the I3 shrink's core
# assumption -- that a ~256-slot tiered pool holds the live set -- against the
# REAL GHZ1 runtime instead of the offline 680x496 proxy (qa_camera_local_pool).
#
# An entity occupies a full slot iff classID != 0 AND inRange != 0:
#   - ACTIVE_ALWAYS(1)/ACTIVE_NORMAL(2)  -> inRange forced true (RESIDENT set).
#   - ACTIVE_BOUNDS(4..7)                -> inRange true only when camera-near.
# (ProcessObjects sets entity->inRange every frame, Object.cpp:488-560.) So the
# peak of |{classID && inRange}| over GHZ1 play = the slot count the shrunk pool
# must hold. This tool reports it for one capture; sample several frames (player
# positions) to bracket the peak.
#
# Pool layout (Object.hpp dual-stride): base = objectEntityList (read live from
# the pointer); slots [0,RESERVE) WIDE 556, [RESERVE,TEMP_START) NARROW 344,
# [TEMP_START,COUNT) WIDE 556. EntityBase fields (REV02, validated vs the prior
# classID@54/onGround@72 anchors): group u16 @52, classID u16 @54, inRange
# bool32 @56, active u8 @76.
#
#   python tools/_portspike/qa_p6_occupancy.py <savestate.mcs> [map]
# =============================================================================
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qa_p6_scene as Q

# Object.hpp dual-stride constants (RETRO_SATURN).
RESERVE        = 0x40          # 64
SCENEENTITY    = 0x440         # 1088
TEMP           = 0x40          # 64
ENTITY_COUNT   = RESERVE + SCENEENTITY + TEMP   # 1216
TEMP_START     = ENTITY_COUNT - TEMP            # 1152
WIDE           = 556
NARROW         = 344           # sizeof(EntityBase) on Saturn (Task #203)

POOL_DEFAULT   = 0x00243000    # P6_LW_ENTITYLIST (fallback; we prefer the live ptr)
PLAYER_CLASSID = 8             # SLOT_PLAYER1 anchor (typeGroups[Player]=8)

# ACTIVE_* enum (RSDKv5): NEVER0 ALWAYS1 NORMAL2 PAUSED3 BOUNDS4 XB5 YB6 RB7 DIS0xFF
RESIDENT_ACTIVE = (1, 2)       # ACTIVE_ALWAYS / ACTIVE_NORMAL -> always inRange


def slot_base(pool, slot):
    if slot < RESERVE:
        return pool + slot * WIDE
    if slot < TEMP_START:
        return pool + RESERVE * WIDE + (slot - RESERVE) * NARROW
    return pool + RESERVE * WIDE + SCENEENTITY * NARROW + (slot - TEMP_START) * WIDE


def main(argv):
    pos = [a for a in argv if not a.startswith("--")]
    if not pos:
        print("usage: qa_p6_occupancy.py <savestate.mcs> [map]"); return 2
    mcs = Q._as_path(pos[0])
    mp  = Q._as_path(pos[1]) if len(pos) > 1 else Q.MAP_DEFAULT
    if not os.path.isfile(mcs):
        print("RED: savestate missing (%s)" % mcs); return 1

    mod      = Q.load_harness()
    map_text = Q.read_text(mp)
    sections = mod.parse_savestate(Q._as_path(mcs))

    ma  = Q.map_symbol(map_text, "_p6_w_magic")
    raw = mod._peek_bytes(sections, ma, 4) if ma else None
    label, perm = Q.calibrate(raw)
    if perm is None:
        print("RED: magic uncalibrated (image not loaded / wrong bank)"); return 1

    # Live pool base from the objectEntityList pointer; fall back to the #define.
    pool = POOL_DEFAULT
    pa = Q.map_symbol(map_text, "objectEntityList")
    if pa:
        v = Q.peek_u32(mod, sections, pa, perm, signed=False)
        if v and 0x00200000 <= v < 0x00300000:
            pool = v

    def classid(slot):
        return Q.peek_u32(mod, sections, slot_base(pool, slot) + 52, perm, signed=False) & 0xFFFF
    def inrange(slot):
        return Q.peek_u32(mod, sections, slot_base(pool, slot) + 56, perm, signed=False)
    def active(slot):
        return (Q.peek_u32(mod, sections, slot_base(pool, slot) + 76, perm, signed=False) >> 24) & 0xFF

    # --- anchor self-validation: slot 0 must be the live Player (classID 8) ------
    s0 = classid(0)
    anchor_ok = (s0 == PLAYER_CLASSID)

    cont = None
    ca = Q.map_symbol(map_text, "_p6_w_cont_frames")
    if ca:
        cont = Q.peek_u32(mod, sections, ca, perm, signed=True)

    live = materialized = resident = 0
    maxslot = -1
    histo = {}
    for s in range(ENTITY_COUNT):
        cid = classid(s)
        if cid == 0:
            continue
        live += 1
        if s > maxslot:
            maxslot = s
        if inrange(s):
            materialized += 1
            histo[cid] = histo.get(cid, 0) + 1
            if active(s) in RESIDENT_ACTIVE:
                resident += 1

    print("=" * 68)
    print("I3-S1 ENTITY-POOL OCCUPANCY CENSUS (runtime, from savestate)")
    print("=" * 68)
    print("  mcs            = %s" % os.path.basename(mcs))
    print("  perm           = %s   pool base = 0x%08X" % (label, pool))
    print("  cont_frames    = %s" % Q._dv(cont))
    print("  anchor slot0   = classID %d  (%s, expect Player=8)"
          % (s0, "OK" if anchor_ok else "MISMATCH -> offsets/byte-order wrong"))
    if not anchor_ok:
        print("RED: slot-0 anchor failed; the parse is not trustworthy.")
        return 1
    print("-" * 68)
    print("  live   (classID!=0)            = %4d   (placed+spawned, <= ENTITY_COUNT %d)"
          % (live, ENTITY_COUNT))
    print("  MATERIALIZED (classID&&inRange)= %4d   <== the I3 shrunk-pool slot demand"
          % materialized)
    print("    of which resident (ALWAYS/NORMAL) = %d" % resident)
    print("    of which camera-near (BOUNDS)     = %d" % (materialized - resident))
    print("  max occupied slot              = %4d" % maxslot)
    print("-" * 68)
    top = sorted(histo.items(), key=lambda kv: -kv[1])[:12]
    print("  materialized classID histogram (top 12): %s"
          % ", ".join("c%d:%d" % (c, n) for c, n in top))
    print("-" * 68)
    print("  SIZING: plan I3 pool ~256 tiered slots (qa_camera_local_pool offline ~101).")
    print("  This capture's materialized peak = %d (%.0f%% of a 256 pool)."
          % (materialized, 100.0 * materialized / 256))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
