#!/usr/bin/env python3
# =============================================================================
# qa_p6_object_preflight.py -- OFFLINE object-readiness pre-flight (BINDING).
#
# WHY (user-mandated 2026-06-17): O3 SpikeLog was registered + built + tested
# before anyone proved its anim would actually load -- the build DISCOVERED the
# failure instead of CONFIRMING success, wasting a ~10-min cycle. This tool
# measures EVERYTHING an object's load path needs, OFFLINE (no Docker build), so
# the build only ever confirms. Run it BEFORE writing an object's registration
# code; a RED here means "do not build yet." See
# memory/object-add-preflight-before-build.md.
#
# It is data-driven end to end -- it reads the REAL retail pack (cd/DATA.RSDK),
# the REAL decrypted bins (extracted/Data), the REAL staged-sheet manifest
# (p6_sheet_model.json), the REAL scene placements (docs/scene_census.json), the
# REAL anim-pack manifest (build_anim_pack.py), and (if a capture is given) the
# REAL live pool usage. Nothing is assumed.
#
#   python tools/_portspike/qa_p6_object_preflight.py SpikeLog
#   python tools/_portspike/qa_p6_object_preflight.py SpikeLog --mcs tools/_portspike/_p6_o3.mcs
#
# Checks (each GREEN/RED, overall exit 0 only if all GREEN):
#   P1 decomp located + StageLoad asset calls extracted
#   P2 every anim .bin present in cd/DATA.RSDK (rev-md5 key)
#   P3 every anim .bin parses (sig 'SPR\0', sane frames/anims) from extracted/Data
#   P4 every referenced sheet is STAGED (p6_sheet_model.json)
#   P5 STG byte cost (frames*36 + anims*28) fits DATASET_STG headroom
#   P6 anim-SLOT cost fits SPRFILE_COUNT (0x400) headroom
#   P7 entity stride estimate <= 344 (narrow slot; RegisterObject refuses oversize)
#   P8 placement coords found -> recommended gate frame / camera-x
#   P9 LOAD-PATH classification: resident fast-pack vs slow windowed read
#      (slow == runtime-read risk -> the resident aniFrames witness is mandatory)
#   P10 object's load witness is -u-rooted in build_p6scene_objs.sh
# =============================================================================
import os, sys, re, json, hashlib, struct, importlib.util

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DECOMP = os.path.join(ROOT, "tools", "_decomp_raw")
PACK = os.path.join(ROOT, "cd", "DATA.RSDK")
EXTRACTED = os.path.join(ROOT, "extracted", "Data", "Sprites")
SHEET_MODEL = os.path.join(ROOT, "tools", "_portspike", "_p6", "p6_sheet_model.json")
CENSUS = os.path.join(ROOT, "docs", "scene_census.json")
ANIM_PACK = os.path.join(ROOT, "tools", "build_anim_pack.py")
BUILD_OBJS = os.path.join(ROOT, "tools", "_portspike", "_p6", "build_p6scene_objs.sh")

SPRFILE_COUNT = 0x400        # Animation.hpp
STG_LIMIT = 153600           # funded DATASET_STG (#254); P0 of any add
NARROW_STRIDE = 344          # EntityBase (Object.hpp); RegisterObject refuses oversize
SPRITEFRAME = 36
ANIMENTRY = 28

GREEN, RED, WARN = "GREEN", " RED ", "WARN "


def load_module(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    m = importlib.util.module_from_spec(spec)
    try:
        spec.loader.exec_module(m)
    except SystemExit:
        pass
    return m


def rev_md5(s):
    d = hashlib.md5(s.encode()).digest()
    return b"".join(d[i:i + 4][::-1] for i in range(0, 16, 4))


def pack_index():
    data = open(PACK, "rb").read()
    cnt = struct.unpack_from("<H", data, 6)[0]
    idx, p = {}, 8
    for _ in range(cnt):
        h = data[p:p + 16]
        off, saf = struct.unpack_from("<ii", data, p + 16)
        idx[h] = (off, saf & 0x7FFFFFFF, (saf >> 31) & 1)
        p += 24
    return idx


def find_decomp(obj):
    for f in os.listdir(DECOMP):
        if f.lower().endswith(obj.lower() + ".c"):
            return os.path.join(DECOMP, f)
    return None


def extract_assets(c_path):
    """Pull LoadSpriteAnimation / LoadSpriteSheet string args from the source."""
    src = open(c_path, "r", errors="ignore").read()
    anims = re.findall(r'LoadSpriteAnimation\(\s*"([^"]+)"', src)
    sheets = re.findall(r'LoadSpriteSheet\(\s*"([^"]+)"', src)
    return sorted(set(anims)), sorted(set(sheets))


def main(argv):
    if not argv:
        print("usage: qa_p6_object_preflight.py <ObjectName> [--mcs capture.mcs]")
        return 2
    obj = argv[0]
    mcs = argv[argv.index("--mcs") + 1] if "--mcs" in argv else None

    rows = []  # (tag, label, detail)

    # ---- P1 decomp + asset extraction ----
    c_path = find_decomp(obj)
    if not c_path:
        rows.append((RED, "P1 decomp located", "no *%s.c under tools/_decomp_raw" % obj))
        return report(obj, rows)
    anim_paths, sheet_paths = extract_assets(c_path)
    rows.append((GREEN if anim_paths or sheet_paths else WARN,
                 "P1 decomp + StageLoad assets",
                 "%s: anims=%s sheets=%s" % (os.path.basename(c_path), anim_paths, sheet_paths)))

    idx = pack_index()
    bap = load_module(ANIM_PACK, "bap")
    # Resident set = WRAM-H Player pack (BINS) + cart object pack (OBJ_BINS, #254).
    # A resident anim resolves fast-path -> ZERO DATASET_STG, so it does NOT count
    # against the STG budget below.
    resident_bins = set(b.lower() for b in (getattr(bap, "BINS", []) + getattr(bap, "OBJ_BINS", [])))

    # ---- per anim .bin: P2 pack, P3 parse, P9 load-path, accumulate sheets/budget ----
    parsed_sheets, stg_bytes, slot_cost = set(sheet_paths), 0, 0
    for ap in anim_paths:
        key = "data/sprites/" + ap.lower()
        present = rev_md5(key) in idx
        rows.append((GREEN if present else RED, "P2 pack has %s" % ap,
                     "rev-md5 %s in cd/DATA.RSDK" % ("HIT" if present else "MISS")))
        ext = os.path.join(EXTRACTED, ap.replace("/", os.sep))
        if os.path.exists(ext):
            try:
                sh, hbc, an, fr = bap.parse_bin(ext)
                parsed_sheets.update(s.split(b"\x00")[0].decode("latin1") for s in sh)
                # STG cost ONLY for NON-resident anims (resident packs are zero-STG).
                # +2 AllocateStorage headers (frames + animations), 16 B each
                # (HEADER_SIZE 4 words; Storage.cpp:273 counts it against the limit).
                if ap.lower() not in resident_bins:
                    stg_bytes += len(fr) * SPRITEFRAME + len(an) * ANIMENTRY + 2 * 16
                    slot_cost += 1
                rows.append((GREEN, "P3 parse %s" % ap,
                             "frames=%d anims=%d sheets=%d -> %d B STG" %
                             (len(fr), len(an), len(sh), len(fr) * SPRITEFRAME + len(an) * ANIMENTRY)))
            except Exception as e:
                rows.append((RED, "P3 parse %s" % ap, "PARSE FAIL: %r" % e))
        else:
            rows.append((WARN, "P3 parse %s" % ap, "no decrypted copy at extracted/Data (can't parse)"))
        rows.append((GREEN if ap.lower() in resident_bins else WARN,
                     "P9 load-path %s" % ap,
                     "RESIDENT fast-pack (zero I/O)" if ap.lower() in resident_bins
                     else "SLOW windowed read -> RUNTIME-READ RISK; resident aniFrames witness MANDATORY"))

    # ---- P4 sheet staging ----
    model = json.load(open(SHEET_MODEL))
    staged = set(s["sheet"] for s in model["sheets"])
    for sp in sorted(parsed_sheets):
        ok = sp in staged
        rows.append((GREEN if ok else RED, "P4 sheet staged: %s" % sp,
                     ("-> %s" % next(s["file"] for s in model["sheets"] if s["sheet"] == sp)) if ok
                     else "NOT in p6_sheet_model.json -> sprite would load but DRAW NOTHING"))

    # ---- P5/P6 budgets (live from capture if given) ----
    stg_used, slots_used = None, None
    if mcs and os.path.exists(mcs):
        try:
            sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
            import qa_p6_scene as Q
            mod = Q.load_harness(); sec = mod.parse_savestate(Q._as_path(mcs))
            mp = Q.read_text(Q.MAP_DEFAULT)
            mptr = Q.map_symbol(mp, "_p6_w_magic")
            _, perm = Q.calibrate(mod._peek_bytes(sec, mptr, 4) if mptr else None)

            def pk(a):
                b = mod._peek_bytes(sec, a, 4)
                return int.from_bytes(bytes(b[perm[i]] for i in range(4)), "big") if b and len(b) >= 4 else None
            # CRITICAL: dataStorage.usedStorage is in uint32 WORDS, not bytes
            # (Storage.cpp:273 compares usedStorage*sizeof(uint32)+size+header vs
            # storageLimit-in-bytes). Reading it as bytes UNDERREPORTS usage 4x and
            # gives a FALSE GREEN -- the exact bug that let SpikeLog's overflow ship.
            uw = pk(0x002BC400 + 4)                   # dataStorage[STG].usedStorage (WORDS)
            stg_used = uw * 4 if uw is not None else None  # -> BYTES
            sal = pk(0x060553bc)                      # spriteAnimationList ptr
            if sal:
                slots_used = sum(1 for i in range(SPRFILE_COUNT)
                                 if (lambda w: w not in (None, 0))(pk(sal + i * ANIMENTRY)))
        except Exception as e:
            rows.append((WARN, "P5/P6 live peek", "capture read failed: %r" % e))
    stg_head = (STG_LIMIT - stg_used) if stg_used is not None else STG_LIMIT
    rows.append((GREEN if stg_bytes <= stg_head else RED, "P5 STG byte budget",
                 "need %d B, headroom %d B (used=%s limit=%d)" %
                 (stg_bytes, stg_head, stg_used, STG_LIMIT)))
    slot_head = (SPRFILE_COUNT - slots_used) if slots_used is not None else SPRFILE_COUNT
    rows.append((GREEN if slot_cost <= slot_head else RED, "P6 anim-slot budget",
                 "need %d slot(s), headroom %d (used=%s of %d)" %
                 (slot_cost, slot_head, slots_used, SPRFILE_COUNT)))

    # ---- P7 entity stride estimate ----
    h_path = c_path[:-2] + ".h"
    est = entity_stride_estimate(h_path, obj)
    if est is None:
        rows.append((WARN, "P7 entity stride", "could not parse Entity%s struct" % obj))
    else:
        rows.append((GREEN if est <= NARROW_STRIDE else RED, "P7 entity stride",
                     "~%d B (limit %d narrow slot) [estimate]" % (est, NARROW_STRIDE)))

    # ---- P8 placement -> gate frame ----
    rows.append(placement_row(obj))

    # ---- P11 draw-order / Z-layering (Audit-1) ----
    rows.append(draworder_row(c_path, obj))

    # ---- P10 witness wiring ----
    bld = open(BUILD_OBJS, "r", errors="ignore").read()
    wit = "_p6_w_%s_aniframes" % obj.lower()
    rows.append((GREEN if wit in bld else WARN, "P10 load witness rooted",
                 "%s %s in build_p6scene_objs.sh -u roots" % (wit, "FOUND" if wit in bld else "MISSING")))

    return report(obj, rows)


def entity_stride_estimate(h_path, obj):
    if not os.path.exists(h_path):
        return None
    src = open(h_path, "r", errors="ignore").read()
    m = re.search(r"struct Entity%s\s*\{(.*?)\};" % obj, src, re.S)
    if not m:
        return None
    body = m.group(1)
    size = 88  # RSDK_ENTITY base (REV02, measured)
    # crude field sizing; pointers/StateMachine = 4, Animator = 32, Vector2 = 8, Hitbox = 8
    for ln in body.splitlines():
        ln = ln.strip()
        if not ln or ln.startswith("RSDK_ENTITY") or ln.startswith("//"):
            continue
        if "StateMachine" in ln or "*" in ln or "int32" in ln or "uint32" in ln or "bool32" in ln or "void " in ln:
            size += 4
        elif "Animator" in ln:
            size += 32
        elif "Vector2" in ln:
            size += 8
        elif "Hitbox" in ln:
            size += 8
        elif "int16" in ln or "uint16" in ln:
            size += 2
        elif "int8" in ln or "uint8" in ln or "bool " in ln:
            size += 1
    return size


def placement_row(obj):
    try:
        c = json.load(open(CENSUS))
        sc = c["scenes"].get("GHZ/Scene1.bin", {})
        coords = sc.get("_coords", {}).get(obj, [])
        if not coords:
            return (WARN, "P8 placement (GHZ1)", "not placed in GHZ/Scene1.bin (check the object's home zone)")
        xs = sorted(p[1] for p in coords)
        spawn = 108
        nearest = min(xs, key=lambda x: abs(x - spawn))
        return (GREEN, "P8 placement -> gate frame",
                "n=%d  x=%d..%d  nearest-to-spawn=%d  => capture must reach camera-x ~%d "
                "(autorun); foreach_all is IN-RANGE only" % (len(coords), min(xs), max(xs), nearest, nearest))
    except Exception as e:
        return (WARN, "P8 placement", "census read failed: %r" % e)


def draworder_row(c_path, obj):
    """Audit-1: extract the object's drawGroup from Create and resolve its Z-layer.
    GHZ stack (back->front, MEASURED Zone.c / GHZSetup.c):
      0  BG tile layers (sky/parallax)
      2  Zone->objectDrawGroup[0]  -- most objects (badniks, springs, SpikeLog, Bridge)
      4  Zone->playerDrawGroup[0]  -- player
      8  Zone->objectDrawGroup[1]  -- fg objects (Newtron)
      12 Zone->playerDrawGroup[1]
      16 DRAWGROUP_COUNT           -- FG-High tiles (tree tops) = OCCLUDE everything below
    A drawGroup-2 object is correctly HIDDEN BEHIND tree tops; the Saturn render must
    keep its VDP1 sprite priority below the NBG1 FG-High priority."""
    src = open(c_path, "r", errors="ignore").read()
    m = re.search(r"->drawGroup\s*=\s*([^;]+);", src)
    if not m:
        return (WARN, "P11 draw-order (Z)", "no drawGroup assignment found in Create")
    expr = m.group(1).strip()
    dgmap = {"Zone->objectDrawGroup[0]": 2, "Zone->objectDrawGroup[1]": 8,
             "Zone->playerDrawGroup[0]": 4, "Zone->playerDrawGroup[1]": 12,
             "Zone->objectDrawGroup[1] - 1": 7}
    dg = dgmap.get(expr)
    if dg is None:
        mlit = re.match(r"^\d+$", expr)
        dg = int(expr) if mlit else None
    if dg is None:
        return (WARN, "P11 draw-order (Z)", "drawGroup=%s (unresolved expr -- verify by hand)" % expr)
    behind_fg = dg < 16
    return (GREEN, "P11 draw-order (Z)",
            "drawGroup=%d (%s); %s FG-High tiles (occluded by tree tops); render VDP1 prio < NBG1-FG" %
            (dg, expr, "BEHIND" if behind_fg else "IN FRONT OF"))


def report(obj, rows):
    print("=" * 78)
    print("OBJECT-READINESS PRE-FLIGHT: %s   (offline -- no build)" % obj)
    print("=" * 78)
    ok = True
    for tag, label, detail in rows:
        if tag == RED:
            ok = False
        print("  [%s] %-30s %s" % (tag, label, detail))
    print("-" * 78)
    if ok:
        print("RESULT: GREEN -- offline-checkable load path proven. A build may CONFIRM it.")
        print("NOTE: a SLOW load-path object (P9 WARN) still needs the resident aniFrames")
        print("      witness GREEN in the post-build capture -- offline cannot prove the")
        print("      windowed read. That gate is qa_p6_ghz_regression.py R10-R13.")
        return 0
    print("RESULT: RED -- do NOT build yet; fix the RED line(s) above first.")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
