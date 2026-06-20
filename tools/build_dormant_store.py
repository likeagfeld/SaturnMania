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
# ENDIANNESS (measured -- the I3b 2a load read magic 0x5036444D = 'P6DM' byte-swapped): the STORE
# STRUCTURE fields are BIG-ENDIAN (read NATIVELY by the big-endian SH-2 runtime -- magic/slot_count/
# obj_idx/pos/index). The raw VAR-BYTES stay little-endian Scene.bin bytes: the materialize replays
# them through the engine's ReadInt16/32 which already byte-swaps the PC-format scene file.
#   <TAG>DORM.BIN layout v2 (big-endian structure fields; the raw var-bytes stay LE Scene.bin):
#     header(20): 'P6DM' u32 | ver u16=2 | slot_count u16 | obj_count u16 | pad u16 | slot_idx_off u32 | recs_off u32
#     object data (from off 20, sequential):  per obj  obj-hash[16] | nvars u8 | nvars*(var-hash[16] | type u8)
#     slot_index[slot_count] u32 (at slot_idx_off):  entity-record offset rel to recs (0xFFFFFFFF = empty slot)
#     entity records (at recs_off):  obj_idx u16 | nvarbytes u16 | pos_x i32 | pos_y i32 | var_bytes[nvarbytes]
#   MATERIALIZE: slot_index[L] -> record -> obj_idx -> object data -> resolve obj-hash->classID (cache per obj)
#   + serialize() rebuilds editableVarList (var-name MD5 -> offset) -> match each stored var-hash -> offset ->
#   replay var_bytes (in scene order, per type) at those offsets -> Create. The var-hashes (v2 add) are why the
#   offline store is materialize-complete -- the offset is a runtime C++ struct layout the tool cannot know.
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
    for _vh, at in attribs:                       # attribs = [(var_hash16, type)]
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

    objects = []          # (hash16, var_count, [(var_hash16, type)])
    entities = []         # (slot, obj_idx, x, y, var_bytes)
    over = 0
    for oi in range(r.u8()):                      # objectCount
        nhash = r.take(16)
        var_count = r.u8()
        attribs = []
        for _ in range(max(0, var_count - 1)):
            vh = r.take(16); vt = r.u8()           # var-name MD5 hash + RSDK var type
            attribs.append((vh, vt))               # KEEP the hash -- the materialize re-resolves
        objects.append((nhash, var_count, attribs))  # the entity offset via serialize()+hash-match
        for _ in range(r.u16()):                  # entityCount
            slot = r.u16(); x = r.i32(); y = r.i32()
            vb = _var_bytes(r, attribs)
            if slot >= SCENEENTITY_COUNT:
                over += 1; continue               # temp/drop region -- not scene-streamed
            entities.append((slot, oi, x, y, vb))
    return objects, entities, over


def emit(objects, entities):
    slot_count = (max((e[0] for e in entities), default=-1) + 1)
    H = 20  # header: magic I + ver H + slot_count H + obj_count H + pad H + slot_idx_off I + recs_off I
    # OBJECT DATA (variable-length, sequential from offset H): per obj {obj-hash16, nvars u8,
    # nvars*(var-hash16, type u8)}. Hashes are byte-wise-compared (HASH_MATCH_MD5 = memcmp) so NO
    # alignment padding. The materialize scans this once -> obj_idx->offset map, then per object:
    # serialize() rebuilds editableVarList (var-name MD5 -> offset); match each stored var-hash ->
    # offset; replay the entity's var-bytes (in order) at those offsets; Create.
    odata = bytearray()
    for nhash, vc, attribs in objects:
        odata += nhash + struct.pack(">B", len(attribs) & 0xFF)
        for vh, vt in attribs:
            odata += vh + struct.pack(">B", vt & 0xFF)
    # ENTITY RECORDS (12-B header each: obj_idx u16, nvarbytes u16, pos_x i32, pos_y i32) + var bytes
    recs = bytearray()
    index = [0xFFFFFFFF] * slot_count
    for slot, oi, x, y, vb in entities:
        index[slot] = len(recs)
        recs += struct.pack(">HHii", oi, len(vb), x, y) + vb
    slot_idx_off = H + len(odata)
    recs_off = slot_idx_off + slot_count * 4
    hdr = struct.pack(">IHHHHII", 0x4D443650, 2, slot_count, len(objects), 0, slot_idx_off, recs_off)
    idx = b"".join(struct.pack(">I", o) for o in index)
    return bytes(hdr) + bytes(odata) + bytes(idx) + bytes(recs), slot_count


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
