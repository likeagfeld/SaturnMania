#!/usr/bin/env python3
# =============================================================================
# build_dormant_store.py -- OFFLINE dormant placement store for the I3b camera-local
# pool streaming shrink (increment 1, ZERO runtime WRAM-H code).
#
# WHY OFFLINE: the runtime-capture form (p6_dormant_* in p6_io_main/Scene.cpp) was
# MEASURED to add ~528 B WRAM-H -> _end 0x060B67C0 > ANIMPAK 0x060B6600 = #228 boot
# trap (only ~80 B headroom). A cart-OVERLAY capture is impossible because the overlay
# (OVLRING.BIN) loads AFTER LoadSceneAssets (p6_io_main.cpp:2677 vs Scene.cpp:535).
# So the CAPTURE is done here, offline, into a CD data file the runtime loads into cart
# (data, not code) -> zero WRAM-H. The per-frame MATERIALIZE (increment 2) reads this
# store from the cart overlay (overlay-resident by gameplay) to re-create far entities.
#
# STORE = the Scene.bin placement table RE-INDEXED for O(1) per-slot materialize. It is
# FORMAT-FAITHFUL (the raw placement data + an index), so it is NOT a speculative format:
# any materialize design reads placements from it. Parser reused VERBATIM from
# build_scene_census.py (Scene.bin walk proven against extract_ghz_spawn.py).
#
#   <TAG>DORM.BIN layout (little-endian, matches the engine's reader endianness):
#     'P6DM' u32 magic | version u16=1 | slot_count u16 | object_count u16 | pad u16
#     object table[object_count]:  hash[16] | var_count u8 | nvtypes u8 | var_types[nvtypes] (2-byte aligned)
#     slot_index[slot_count] u32:  byte offset of the entity record (0xFFFFFFFF = empty slot)
#     entity records (packed):     obj_idx u16 | nvarbytes u16 | pos_x i32 | pos_y i32 | var_bytes[nvarbytes]
#   The materialize: slot_index[L] -> record -> obj table -> resolve hash->classID (cached
#   per object at runtime) + replay var_bytes via the runtime varList (offsets) in order.
#
#   python tools/build_dormant_store.py [GHZ/Scene1.bin ...]   (default: GHZ1 + GHZ2)
# =============================================================================
import os, sys, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import build_scene_census as C  # reuse R, ATTR_SIZE, build_hash_table

ROOT = C.ROOT
STAGES = C.STAGES
CD = os.path.join(ROOT, "cd")
SCENEENTITY_COUNT = 0x440  # Object.hpp Saturn scene region (the logical-slot index ceiling)


def _var_bytes(r, attribs):
    # consume + RETURN the exact var-value bytes this entity wrote (all types; mirrors
    # Scene.cpp's var loop byte consumption -- STRING = u16 len + len*2 UTF16).
    start = r.p
    for at in attribs:
        if at == 8:
            n = r.u16(); r.take(n * 2)
        elif at in C.ATTR_SIZE:
            r.take(C.ATTR_SIZE[at][1])
        else:
            raise RuntimeError("attr type %d" % at)
    return r.d[start:r.p]


def parse_for_store(path, h2n):
    d = open(path, "rb").read()
    r = C.R(d)
    if d[:4] != b"SCN\x00":
        raise RuntimeError("not SCN")
    r.p = 4; r.take(0x10)
    nl = r.u8(); r.take(nl + 1)
    for _ in range(r.u8()):                       # layers
        r.u8(); r.s(); r.u8(); r.u8()
        r.u16(); r.u16(); r.u16(); r.u16()
        for _ in range(r.u16()):
            r.take(6)
        r.compressed(); r.compressed()

    objects = []          # (hash16, var_count, var_types[])
    entities = []         # (slot, obj_idx, x, y, var_bytes)
    over = 0
    for oi in range(r.u8()):                      # objectCount
        nhash = r.take(16)
        var_count = r.u8()
        attribs = []
        for _ in range(max(0, var_count - 1)):
            r.take(16); attribs.append(r.u8())    # attr-hash + type
        objects.append((nhash, var_count, attribs))
        for _ in range(r.u16()):                  # entityCount
            slot = r.u16(); x = r.i32(); y = r.i32()
            vb = _var_bytes(r, attribs)
            if slot >= SCENEENTITY_COUNT:
                over += 1; continue               # temp/drop region -- not scene-streamed
            entities.append((slot, oi, x, y, vb))
    return objects, entities, over


def emit(objects, entities):
    slot_count = (max((e[0] for e in entities), default=-1) + 1)
    # object table
    otab = bytearray()
    for nhash, vc, vts in objects:
        otab += nhash + struct.pack("<BB", vc & 0xFF, len(vts) & 0xFF) + bytes(vts)
        if len(otab) & 1:
            otab += b"\x00"
    # entity records (12-B header each: obj_idx u16, nvarbytes u16, pos_x i32, pos_y i32) + var bytes
    recs = bytearray()
    index = [0xFFFFFFFF] * slot_count
    for slot, oi, x, y, vb in entities:
        index[slot] = len(recs)
        recs += struct.pack("<HHii", oi, len(vb), x, y) + vb
    hdr = struct.pack("<IHHHH", 0x4D443650, 1, slot_count, len(objects), 0)  # 'P6DM'
    idx = b"".join(struct.pack("<I", o) for o in index)
    return bytes(hdr) + bytes(otab) + idx + bytes(recs), slot_count


def main(argv):
    scenes = argv or ["GHZ/Scene1.bin", "GHZ/Scene2.bin"]
    h2n = C.build_hash_table()
    print("=== build_dormant_store -- offline I3b placement store (zero runtime WRAM-H) ===")
    for sk in scenes:
        path = os.path.join(STAGES, sk)
        if not os.path.isfile(path):
            print("  %-20s MISSING" % sk); continue
        objects, entities, over = parse_for_store(path, h2n)
        blob, slot_count = emit(objects, entities)
        total_vb = sum(len(e[4]) for e in entities)
        tag = sk.split("/")[0].upper() + ("1" if sk.endswith("1.bin") else "2" if sk.endswith("2.bin") else "")
        out = os.path.join(CD, tag + "DORM.BIN")
        with open(out, "wb") as f:
            f.write(blob)
        print("  %-16s -> %-14s  %4d objs, %4d scene entities (slot<%d), %d temp/drop dropped"
              % (sk, os.path.basename(out), len(objects), len(entities), SCENEENTITY_COUNT, over))
        print("                     store %d B  (index %d slots, var-bytes %d B)  magic OK"
              % (len(blob), slot_count, total_vb))
    print("  NOTE: the runtime loads <TAG>DORM.BIN into cart as DATA; the increment-2 materialize")
    print("        resolves each record's object hash->classID + replays var_bytes via the live varList.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
