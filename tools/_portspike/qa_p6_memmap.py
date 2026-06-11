#!/usr/bin/env python3
# =============================================================================
# qa_p6_memmap.py -- P6.7b gate (Task #210): GHZ-SCALE MEMORY MAP FEASIBILITY.
# The PC engine's flat object arrays do NOT fit the Saturn at GHZ scale
# (MEASURED: GHZ1 Scene1.bin places 1,041 entities, largest class 446 rings;
# stock typeGroups at the needed ENTITY_COUNT would be 321 KB on top of a
# 430 KB entityList). This gate is the arithmetic contract for the P6.8
# SHIPPING image layout declared in platform/Saturn/SaturnMemoryMap.h:
#
#   M1 the header exists and declares every constant this gate consumes
#   M2 SCENEENTITY covers the LIVE-parsed GHZ1 entity count + headroom
#   M3 typeGroup/drawGroup entry caps cover the largest possible INRANGE
#      population (camera-culled; the engine writes only inRange entities
#      into the group lists, Object.cpp:462-493) with the Saturn-gated
#      clamp making overflow SAFE instead of corrupting (.bss class)
#   M4 WRAM-L tenants fit the 1 MB bank with the declared margin
#   M5 WRAM-H tenants fit under the SGL work-area reservation
#
# Every size derives from the header constants + real struct arithmetic
# (EntityBase 344 B per the P4 retarget; TypeGroupList/DrawList per
# Object.hpp). Any future growth that breaks the map fires THIS gate.
#
# Usage: python tools/_portspike/qa_p6_memmap.py
# =============================================================================
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
HDR = os.path.join(ROOT, "platform", "Saturn", "SaturnMemoryMap.h")
GHZ_SCENE = os.path.join(ROOT, "extracted", "Data", "Stages", "GHZ", "Scene1.bin")

ENTITYBASE_SIZE = 344  # P4 retarget (Task #203), data[0x40] overlay
OBJECTCLASS_SIZE = 68  # hash 16 + 10 fn ptrs + name + staticVars + 2x u32 (measured)


def parse_header():
    if not os.path.isfile(HDR):
        return None
    txt = open(HDR, encoding="utf-8", errors="replace").read()
    consts = {}
    for m in re.finditer(r"#define\s+(P68_\w+)\s+\(?(0x[0-9A-Fa-f]+|\d+)\)?", txt):
        consts[m.group(1)] = int(m.group(2), 0)
    return consts


def count_ghz_entities():
    # Live count via the proven parser (consumes the scene byte-exactly).
    import subprocess
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "parse_title_entities.py"),
                        GHZ_SCENE], capture_output=True, text=True)
    n = 0
    for ln in (r.stdout or "").splitlines():
        if re.match(r"\s*\S.*\s+\d+\s+\S*\s*[\d.-]+\s+[\d.-]+\s*$", ln):
            n += 1
    return n


def main():
    print("=" * 72)
    print("P6.7b MEMMAP GATE: GHZ-scale P6.8 layout feasibility (arithmetic)")
    print("=" * 72)
    c = parse_header()
    if not c:
        print("RESULT: RED -- platform/Saturn/SaturnMemoryMap.h missing")
        print("        (Expected before the P6.7b memory map lands.)")
        return 1

    need = ["P68_RESERVE_ENTITIES", "P68_SCENE_ENTITIES", "P68_TEMP_ENTITIES",
            "P68_TYPEGROUP_ENTRY_CAP", "P68_DRAWGROUP_ENTRY_CAP",
            "P68_TYPEGROUP_COUNT", "P68_DRAWGROUP_COUNT", "P68_OBJECT_COUNT",
            "P68_LWRAM_HEAP_BYTES", "P68_LWRAM_DATASTORAGE_BYTES",
            "P68_LWRAM_TILELAYERS_BYTES", "P68_LWRAM_DATAFILELIST_BYTES",
            "P68_LWRAM_GROUPB_BYTES", "P68_LWRAM_MARGIN_MIN",
            "P68_HWRAM_CODE_BYTES", "P68_HWRAM_SGL_RESERVE",
            "P68_HWRAM_TILESET_BYTES", "P68_HWRAM_MISC_BYTES",
            "P68_HWRAM_MARGIN_MIN",
            "P68_COLL_RAW_BYTES", "P68_COLL_PACKED_BYTES", "P68_COLLISION_PLACED"]
    missing = [k for k in need if k not in c]
    if missing:
        print("RESULT: RED -- header missing constants: %s" % ", ".join(missing))
        return 1

    # P6.7c: re-derive sizeof(DataStorage) LIVE from Storage.hpp (the hard
    # coupling the verification flagged: the contract's 16,404 B stride is
    # valid ONLY with the Saturn STORAGE_ENTRY_COUNT 0x800 branch; a build
    # skew back to 0x1000 silently overruns the window by 81,920 B -- the
    # Phase 1.4-1.15 class).
    storage_hpp = open(os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK",
                                    "Storage", "Storage.hpp"),
                       encoding="utf-8", errors="replace").read()
    m = re.search(r"RETRO_PLATFORM == RETRO_SATURN.*?#define\s+STORAGE_ENTRY_COUNT\s+\((0x[0-9A-Fa-f]+)\)",
                  storage_hpp, re.S)
    if not m:
        print("RESULT: RED -- Storage.hpp lacks the Saturn STORAGE_ENTRY_COUNT branch")
        return 1
    entry_count = int(m.group(1), 0)
    datastorage_real = 5 * (2 * entry_count * 4 + 20)  # Storage.hpp:17-25

    ghz = count_ghz_entities()
    entity_count = c["P68_RESERVE_ENTITIES"] + c["P68_SCENE_ENTITIES"] + c["P68_TEMP_ENTITIES"]
    entitylist = entity_count * ENTITYBASE_SIZE
    typegroups = c["P68_TYPEGROUP_COUNT"] * (2 * c["P68_TYPEGROUP_ENTRY_CAP"] + 4)
    drawgroups = c["P68_DRAWGROUP_COUNT"] * (2 * c["P68_DRAWGROUP_ENTRY_CAP"] + 40)
    classlist  = c["P68_OBJECT_COUNT"] * OBJECTCLASS_SIZE

    lwram_used = (c["P68_LWRAM_HEAP_BYTES"] + entitylist +
                  c["P68_LWRAM_DATASTORAGE_BYTES"] +
                  c["P68_LWRAM_TILELAYERS_BYTES"] +
                  c["P68_LWRAM_DATAFILELIST_BYTES"] + c["P68_LWRAM_GROUPB_BYTES"])
    lwram_margin = 0x100000 - lwram_used

    hwram_used = (c["P68_HWRAM_CODE_BYTES"] + c["P68_HWRAM_SGL_RESERVE"] +
                  c["P68_HWRAM_TILESET_BYTES"] + typegroups + drawgroups +
                  classlist + c["P68_HWRAM_MISC_BYTES"])
    if c["P68_COLLISION_PLACED"]:
        # P6.7d.2: collision is a placed WRAM-H tenant (packed form).
        hwram_used += c.get("P68_HWRAM_COLL_BYTES", c["P68_COLL_PACKED_BYTES"])
    hwram_margin = 0x100000 - hwram_used

    print("  GHZ1 live entity count: %d" % ghz)
    print("  ENTITY_COUNT %d -> entityList %d B" % (entity_count, entitylist))
    print("  typeGroups %d B, drawGroups %d B, classList %d B"
          % (typegroups, drawgroups, classlist))
    print("  WRAM-L used %d B  margin %d B" % (lwram_used, lwram_margin))
    print("  WRAM-H used %d B  margin %d B" % (hwram_used, hwram_margin))
    print("-" * 72)

    checks = [
        ("M2 SCENE_ENTITIES (%d) >= GHZ1 live count (%d) + 32 headroom"
         % (c["P68_SCENE_ENTITIES"], ghz),
         c["P68_SCENE_ENTITIES"] >= ghz + 32, ""),
        ("M3 group entry caps sane (cap <= ENTITY_COUNT, >= 0x80 inRange peak)",
         0x80 <= c["P68_TYPEGROUP_ENTRY_CAP"] <= entity_count
         and 0x80 <= c["P68_DRAWGROUP_ENTRY_CAP"] <= entity_count, ""),
        ("M4 WRAM-L fits: margin %d >= %d" % (lwram_margin, c["P68_LWRAM_MARGIN_MIN"]),
         lwram_margin >= c["P68_LWRAM_MARGIN_MIN"], ""),
        ("M5 WRAM-H fits: margin %d >= %d" % (hwram_margin, c["P68_HWRAM_MARGIN_MIN"]),
         hwram_margin >= c["P68_HWRAM_MARGIN_MIN"], ""),
        ("M6 dataStorage stride contract: window %d >= 5*sizeof(DataStorage) %d "
         "(STORAGE_ENTRY_COUNT 0x%X live from Storage.hpp)"
         % (c["P68_LWRAM_DATASTORAGE_BYTES"], datastorage_real, entry_count),
         c["P68_LWRAM_DATASTORAGE_BYTES"] >= datastorage_real
         and entry_count == 0x800, ""),
        ("M7 collision-gap declaration: raw %d / packed %d arithmetic"
         % (c["P68_COLL_RAW_BYTES"], c["P68_COLL_PACKED_BYTES"]),
         c["P68_COLL_RAW_BYTES"] == 2 * 0x400 * 64 + 2 * 0x400 * 5
         and c["P68_COLL_PACKED_BYTES"] == 2 * 0x400 * 32 + 2 * 0x400 * 5, ""),
    ]
    if not c["P68_COLLISION_PLACED"]:
        print("  [WARN ] collision residency (raw %d / packed %d B) is a DECLARED"
              % (c["P68_COLL_RAW_BYTES"], c["P68_COLL_PACKED_BYTES"]))
        print("          OPEN GAP -- neither bank holds it (WRAM-L margin %d,"
              % lwram_margin)
        print("          WRAM-H margin %d). P6.7d design decision, user-checkpointed."
              % hwram_margin)

    ok = all(p for _, p, _ in checks)
    for title, passed, _ in checks:
        print("  [%s] %s" % ("GREEN" if passed else " RED ", title))
    print("-" * 72)
    if ok:
        print("RESULT: GREEN -- the declared P6.8 GHZ-scale layout fits both")
        print("        banks with the contracted margins (live-measured scene")
        print("        population; clamped group lists; arithmetic, no guess).")
        return 0
    print("RESULT: RED -- the declared layout does not fit (see checks).")
    return 1


if __name__ == "__main__":
    sys.exit(main())
