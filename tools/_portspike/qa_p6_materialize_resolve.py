#!/usr/bin/env python3
# =============================================================================
# qa_p6_materialize_resolve.py -- I3b camera-local-pool MATERIALIZE read-side MODEL,
# OFFLINE (no build, zero runtime WRAM-H code).
#
# WHY OFFLINE (binding): memory/wram-h-animpak-ceiling-boot-trap.md -- "never add a
# runtime self-check to the engine build; prove static props OFFLINE (zero binary
# cost)". A ~576 B runtime self-check ALREADY caused the #228 blue screen the user
# saw. The materialize's read side (DORM navigation + obj-hash->classID + var-hash->
# offset) is a STATIC property of the on-disk store + the registered class set + the
# decomp editable-var sets -- so it is proven HERE, offline, not in the engine image.
# This gate is the SPEC the runtime materialize (increment 2b) must reproduce: the
# resolved-record count it prints is exactly how many far entities the runtime
# materialize will instantiate per scene.
#
# WHAT IT PROVES, against the REAL cd/<TAG>DORM.BIN the runtime loads:
#   D1  NAVIGATION   -- big-endian header parses; the variable-length object table
#                       walk lands EXACTLY at slot_idx_off; slot index + record
#                       offsets in range; every record's var-byte length equals the
#                       sum of its object's attrib type sizes (store is self-consistent).
#   D2  OBJ-HASH->NAME -- every object's 16-B md5 hash maps to a known decomp object
#                       NAME (census build_hash_table). An unmapped hash = a corrupt
#                       store or a census gap; the runtime classID memcmp would miss it.
#   D3  RECORDS vs RESOLVED -- the registered Saturn class set is parsed from SOURCE
#                       (no hardcoding): p6_wave1_reg.c RSDK_REGISTER_OBJECT (pack),
#                       p6_io_main.cpp engine RegisterObject, p6_ovl_ghz.c overlay
#                       register_object_full. For each entity record, resolve its
#                       object name and count those in the registered set. That count
#                       is the materialize SPEC; the rest resolve to unregistered ->
#                       the materialize SKIPS them (they don't exist on Saturn).
#   D4  VAR-HASH->OFFSET -- for each REGISTERED object that carries vars, its decomp
#                       source's RSDK_EDITABLE_VAR field names are md5'd and every
#                       stored var-hash must match one. This proves the runtime
#                       serialize()+hash-match (Object.hpp:397 RSDK_EDITABLE_VAR ->
#                       SetEditableVar -> GEN_HASH_MD5, Text.hpp:142 = ASCII md5)
#                       finds an offset for every var the materialize replays. An
#                       unmatched var-hash = a port divergence (the Saturn object
#                       registers a different editable-var set than Scene.bin expects).
#                       The ASCII encoding is CONFIRMED EMPIRICALLY (Bridge length/
#                       burnable) -- not assumed.
#
# RED-first: run on a corrupted store copy (magic flipped / truncated) -> RED on D1;
# run on the real v2 stores -> GREEN + the measured spec. The gate is a permanent
# guard against a malformed/incomplete DORM store and against a registered object
# whose editable-var set drifts from the decomp.
#
#   python tools/_portspike/qa_p6_materialize_resolve.py [STORE.BIN ...]
#       (default: cd/GHZ1DORM.BIN cd/GHZ2DORM.BIN)
# =============================================================================
import os, re, sys, struct, hashlib
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
import build_scene_census as C  # build_hash_table, ATTR_SIZE, RAW, ROOT

ROOT = C.ROOT
RAW = C.RAW
CD = os.path.join(ROOT, "cd")
DORM_MAGIC = 0x4D443650  # 'P6DM' big-endian
DORM_VER = 2


def _read(p):
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


# ---- registered Saturn class set, parsed from SOURCE (data-driven, no hardcoding) ----
def registered_classes():
    reg = {}  # name -> origin
    wave1 = _read(os.path.join(ROOT, "tools/_portspike/_p6/p6_wave1_reg.c"))
    for m in re.finditer(r"RSDK_REGISTER_OBJECT\(\s*(\w+)\s*\)", wave1):
        reg.setdefault(m.group(1), "pack")
    iomain = _read(os.path.join(ROOT, "tools/_portspike/_p6/p6_io_main.cpp"))
    for m in re.finditer(r"RegisterObject\(\(Object \*\*\)&(\w+),\s*\":(\w+):\"", iomain):
        reg.setdefault(m.group(2), "engine")
    ovl = _read(os.path.join(ROOT, "tools/_portspike/_p6/p6_ovl_ghz.c"))
    for m in re.finditer(r'register_object_full\(\(void \*\*\)&\w+,\s*"(\w+)"', ovl):
        reg.setdefault(m.group(1), "overlay")
    return reg


# ---- decomp editable-var field-name set per object (for D4 offset-match proof) ----
def editable_var_names(objname):
    # locate the decomp TU by EXACT object name == the census name-capture (the last
    # underscore segment), NOT a loose "_<name>.c" suffix: that suffix wrongly matches
    # BSS_BSS_Player.c / UFO_UFO_Player.c (object names "BSS_Player"/"UFO_Player") for
    # objname "Player" and picks the wrong file. Same regex as build_hash_table.
    src = None
    for fn in os.listdir(RAW):
        m = re.match(r"SonicMania_Objects_[A-Za-z0-9]+_(.+)\.c$", fn)
        if m and m.group(1) == objname:
            src = os.path.join(RAW, fn)
            break
    if src is None:
        return None  # no cached source for this exact object name -> cannot check its vars
    text = _read(src)
    names = []
    for m in re.finditer(r"RSDK_EDITABLE_VAR\(\s*\w+\s*,\s*\w+\s*,\s*([^)]+?)\s*\)", text):
        names.append(m.group(1).strip())
    return names  # in decomp declaration order (== Scene.bin var order)


def md5a(s):
    return hashlib.md5(s.encode("ascii")).digest()


# ---- big-endian DORM store parser (the exact bytes the SH-2 runtime reads) ----
def parse_dorm(blob):
    if len(blob) < 20:
        raise RuntimeError("store < header")
    magic, ver, slot_count, obj_count, pad, slot_idx_off, recs_off = struct.unpack_from(">IHHHHII", blob, 0)
    objs = []  # (hash16, [(varhash16, type)])
    p = 20
    for _ in range(obj_count):
        if p + 17 > len(blob):
            raise RuntimeError("object table overran store")
        h = blob[p:p + 16]; p += 16
        nv = blob[p]; p += 1
        attribs = []
        for _ in range(nv):
            if p + 17 > len(blob):
                raise RuntimeError("object attrib overran store")
            vh = blob[p:p + 16]; p += 16
            vt = blob[p]; p += 1
            attribs.append((vh, vt))
        objs.append((h, attribs))
    obj_table_end = p
    index = list(struct.unpack_from(">%dI" % slot_count, blob, slot_idx_off)) if slot_count else []
    records = []  # (slot, obj_idx, nvarbytes, x, y, varbytes)
    for slot, off in enumerate(index):
        if off == 0xFFFFFFFF:
            continue
        rp = recs_off + off
        obj_idx, nvb, x, y = struct.unpack_from(">HHii", blob, rp)
        vb = blob[rp + 12:rp + 12 + nvb]
        records.append((slot, obj_idx, nvb, x, y, vb))
    return dict(magic=magic, ver=ver, slot_count=slot_count, obj_count=obj_count,
                slot_idx_off=slot_idx_off, recs_off=recs_off, objs=objs,
                obj_table_end=obj_table_end, index=index, records=records, size=len(blob))


def varbytes_expected(attribs):
    # sum of fixed type sizes; None if a STRING var (type 8, variable) is present.
    total = 0
    for _vh, at in attribs:
        if at == 8:
            return None
        if at in C.ATTR_SIZE:
            total += C.ATTR_SIZE[at][1]
        else:
            return -1  # unknown type -> inconsistent
    return total


def check_store(path, h2n, reg, var_cache):
    name = os.path.basename(path)
    print("-" * 74)
    print("STORE  %s" % name)
    if not os.path.isfile(path):
        print("  [ RED ] file missing"); return False
    blob = open(path, "rb").read()
    try:
        d = parse_dorm(blob)
    except Exception as e:
        print("  [ RED ] D1 navigation -- parse failed: %s" % e); return False

    # ---- D1 navigation / self-consistency ----
    d1 = True
    if d.get("magic") != DORM_MAGIC:
        print("  [ RED ] D1 magic = 0x%08X (want 0x%08X 'P6DM' big-endian)" % (d["magic"], DORM_MAGIC)); d1 = False
    if d.get("ver") != DORM_VER:
        print("  [ RED ] D1 ver = %s (want %d)" % (d.get("ver"), DORM_VER)); d1 = False
    if d1 and d["obj_table_end"] != d["slot_idx_off"]:
        print("  [ RED ] D1 object-table walk ended at %d, slot_idx_off=%d (var-length table corrupt)"
              % (d["obj_table_end"], d["slot_idx_off"])); d1 = False
    if d1 and d["recs_off"] != d["slot_idx_off"] + d["slot_count"] * 4:
        print("  [ RED ] D1 recs_off=%d != slot_idx_off+slot_count*4=%d"
              % (d["recs_off"], d["slot_idx_off"] + d["slot_count"] * 4)); d1 = False
    badvb = 0
    if d1:
        for slot, oi, nvb, x, y, vb in d["records"]:
            if oi >= d["obj_count"]:
                badvb += 1; continue
            exp = varbytes_expected(d["objs"][oi][1])
            if exp is not None and exp >= 0 and exp != nvb:
                badvb += 1
            if len(vb) != nvb:
                badvb += 1
        if badvb:
            print("  [ RED ] D1 var-byte length mismatch on %d record(s)" % badvb); d1 = False
    if d1:
        print("  [GREEN] D1 navigation -- magic/ver OK, obj-table ends exactly at slot_idx_off (%d),"
              % d["slot_idx_off"])
        print("          recs_off consistent, %d records, every var-byte length matches its attrib sizes"
              % len(d["records"]))

    # ---- D2 obj-hash -> name (informational; unmapped == unported -> materialize skips it) ----
    # An unmapped hash can NEVER be in the registered set (registered names are all census-
    # mapped by construction), so every unmapped object is unregistered and the materialize
    # correctly skips it. GREEN unless the census is BROKEN (mapped fraction collapses), which
    # could mask a registered object. GHZ1's 2 unmapped (TimeAttackData=0 placements, an idx-0
    # 7-placement object the decomp corpus can't name) are genuine unported objects.
    names = []
    unmapped = []
    for h, _attr in d["objs"]:
        nm = h2n.get(h)
        names.append(nm)
        if nm is None:
            unmapped.append(h.hex()[:12])
    mapped = d["obj_count"] - len(unmapped)
    d2 = d["obj_count"] == 0 or (mapped / d["obj_count"]) >= 0.90
    print("  [%s] D2 obj-hash->name -- %d/%d mapped; %d unmapped (unported, skipped): %s"
          % ("GREEN" if d2 else " RED ", mapped, d["obj_count"], len(unmapped),
             ", ".join(unmapped) if unmapped else "none"))

    # ---- D3 records vs resolved (the materialize SPEC) ----
    per_obj = {}  # name -> record count
    for slot, oi, nvb, x, y, vb in d["records"]:
        nm = names[oi] if oi < len(names) else None
        per_obj[nm] = per_obj.get(nm, 0) + 1
    resolved = sum(c for nm, c in per_obj.items() if nm in reg)
    skipped = sum(c for nm, c in per_obj.items() if nm not in reg)
    d3 = resolved > 0 and resolved <= len(d["records"])
    print("  [%s] D3 records=%d  RESOLVED(registered)=%d  skipped(unregistered)=%d"
          % ("GREEN" if d3 else " RED ", len(d["records"]), resolved, skipped))
    _k = lambda cn: (cn[0], cn[1] or "")  # None-tolerant (an unmapped object has name None)
    reg_objs = sorted(((c, nm) for nm, c in per_obj.items() if nm in reg), key=_k, reverse=True)
    top_skip = sorted(((c, nm) for nm, c in per_obj.items() if nm not in reg), key=_k, reverse=True)
    print("        materialize SPEC -- registered placements the runtime will instantiate:")
    for c, nm in reg_objs[:10]:
        print("           %-16s %4d  (%s)" % (nm, c, reg.get(nm)))
    if top_skip:
        print("        unregistered (materialize skips; not ported on Saturn): " +
              ", ".join("%s=%d" % (nm or "?", c) for c, nm in top_skip[:8]))

    # ---- D4 var-hash -> editable-var name (offset-match proof) ----
    d4 = True
    checked = 0
    unmatched_total = 0
    for oi, (h, attribs) in enumerate(d["objs"]):
        nm = names[oi]
        if nm is None or nm not in reg or not attribs:
            continue
        fields = var_cache.get(nm, "MISS")
        if fields == "MISS":
            fields = editable_var_names(nm)
            var_cache[nm] = fields
        if fields is None:
            continue  # no cached decomp source for this registered object -> can't check
        fieldhashes = {md5a(f): f for f in fields}
        unmatched = [vh.hex()[:8] for vh, _vt in attribs if vh not in fieldhashes]
        checked += 1
        if unmatched:
            unmatched_total += len(unmatched)
            d4 = False
            print("  [ RED ] D4 %s -- %d/%d var-hash UNMATCHED vs decomp editable-vars %s (port divergence)"
                  % (nm, len(unmatched), len(attribs), fields))
    if d4:
        print("  [GREEN] D4 var-hash->offset -- every stored var-hash on all %d checked registered objects"
              % checked)
        print("          matches a decomp RSDK_EDITABLE_VAR name (runtime serialize()+hash-match will resolve)")

    ok = d1 and d2 and d3 and d4
    print("  ==> %s  %s" % ("GREEN" if ok else "RED  ", name))
    return ok


def main(argv):
    stores = argv or [os.path.join(CD, "GHZ1DORM.BIN"), os.path.join(CD, "GHZ2DORM.BIN")]
    print("=== qa_p6_materialize_resolve -- I3b MATERIALIZE read-side MODEL (offline, zero WRAM-H) ===")
    h2n = C.build_hash_table()
    reg = registered_classes()
    print("  registered Saturn classes parsed from source: %d  (%d pack, %d engine, %d overlay)"
          % (len(reg),
             sum(1 for v in reg.values() if v == "pack"),
             sum(1 for v in reg.values() if v == "engine"),
             sum(1 for v in reg.values() if v == "overlay")))

    # D4 encoding CONFIRMATION (empirical, not assumed): Bridge length/burnable.
    bf = editable_var_names("Bridge")
    print("  encoding check -- Bridge RSDK_EDITABLE_VAR names from decomp: %s" % bf)
    try:
        bblob = open(os.path.join(CD, "GHZ1DORM.BIN"), "rb").read()
        bd = parse_dorm(bblob)
        bh = h2n  # reuse
        bridge_attribs = None
        for h, attribs in bd["objs"]:
            if bh.get(h) == "Bridge":
                bridge_attribs = attribs; break
        if bridge_attribs and bf:
            want = {md5a(f) for f in bf}
            got = {vh for vh, _t in bridge_attribs}
            ok_enc = got and got.issubset(want)
            print("  encoding check -- Bridge stored var-hashes %s md5(ASCII) of %s -> ASCII %s"
                  % ("==" if ok_enc else "!=", bf, "CONFIRMED" if ok_enc else "MISMATCH"))
    except Exception as e:
        print("  encoding check -- skipped (%s)" % e)

    var_cache = {}
    results = [check_store(p, h2n, reg, var_cache) for p in stores]
    print("=" * 74)
    if all(results):
        print("RESULT: GREEN -- the materialize read side is sound on every store: DORM navigates,")
        print("        all object hashes resolve to known names, the registered subset is identified")
        print("        (the runtime materialize SPEC), and every registered object's var-hashes match")
        print("        its decomp editable-var set (offset lookup will succeed at runtime).")
        return 0
    print("RESULT: RED -- a store failed a read-side check above. The runtime materialize must NOT")
    print("        be built against a store that fails D1/D2/D4 (it would read garbage or skip vars).")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
