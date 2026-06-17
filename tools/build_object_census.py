#!/usr/bin/env python3
# =============================================================================
# build_object_census.py -- COMPREHENSIVE drop-in-readiness census of the whole
# Sonic Mania decomp object corpus, for the engine-shipping port.
#
# WHY (user directive 2026-06-17): stop measuring objects reactively one bug at
# a time. Pre-measure EVERYTHING so any object is drop-in ready: its draw-kind,
# StageLoad residency, dependency closure, and -- critically -- which RSDK APIs
# it needs that are STUBBED on Saturn (the DrawTile-class no-ops that make a
# registered object still render wrong).
#
# Parses every tools/_decomp_raw/SonicMania_Objects_*.c and cross-references:
#   - the currently-registered set        (tools/_portspike/_p6/p6_wave1_reg.c)
#   - the currently-staged sprite sheets  (tools/build_sheet_bands.py SHEETS)
#   - the Saturn draw-stub set            (tools/_portspike/_p6/p6_stubs.cpp)
#   - every zone's StageConfig objects    (docs/scene_objects.json, 42 zones)
#
# Emits docs/object_census.json (machine-readable manifest) and prints a summary
# + the GHZ transitive closure. Re-run after any registration/stage/stub change.
#
# Deterministic static analysis (regex over the verbatim decomp) -- no agent
# interpretation. Cite: p6_stubs.cpp:203-224 (draw stubs), Sprite.cpp (sheet
# hash), Bridge port #181 (the present-but-invisible lesson this generalizes).
# =============================================================================
import json
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
RAW = os.path.join(ROOT, "tools", "_decomp_raw")
REG_C = os.path.join(ROOT, "tools", "_portspike", "_p6", "p6_wave1_reg.c")
SHEETS_PY = os.path.join(ROOT, "tools", "build_sheet_bands.py")
STUBS_CPP = os.path.join(ROOT, "tools", "_portspike", "_p6", "p6_stubs.cpp")
SCENES_JSON = os.path.join(ROOT, "docs", "scene_objects.json")
SPRITES = os.path.join(ROOT, "extracted", "Data", "Sprites")
P6_MAIN = os.path.join(ROOT, "tools", "_portspike", "_p6", "p6_main.cpp")
OUT_JSON = os.path.join(ROOT, "docs", "object_census.json")

# Resolve a SpriteAnimation .bin to the sheet .gif(s) it references, so an
# object's LoadSpriteAnimation("X.bin") residency counts the real sheet (the
# badniks load via .bin, not a direct LoadSpriteSheet). Format (Animation.cpp:
# 126-166): u32 sig 'SPR\0', u32 frameCount, u8 sheetCount, then sheetCount
# u8-length-prefixed name strings. Cache so a shared .bin parses once.
_BIN_CACHE = {}
def bin_sheets(rel):
    rel = rel.lstrip("/")
    if rel in _BIN_CACHE:
        return _BIN_CACHE[rel]
    p = os.path.join(SPRITES, rel.replace("/", os.sep))
    out = []
    try:
        b = open(p, "rb").read()
        if len(b) >= 9 and b[0:4] == b"SPR\x00":
            sc = b[8]; off = 9
            for _ in range(sc):
                ln = b[off]; off += 1
                out.append(b[off:off + ln].decode("latin1").rstrip("\x00")); off += ln
    except OSError:
        out = None  # .bin not extracted (editor-only / absent)
    _BIN_CACHE[rel] = out
    return out

# The Saturn DRAW-stub set: registered objects that rely on these for their
# visuals will still render wrong/absent. From p6_stubs.cpp:203-224 (no-op
# bodies). DrawSprite (206) is EXCLUDED -- it is the real backend in the pack
# (#if !defined(P6_SCENE_TEST)). Each maps to the visual it silently drops.
DRAW_STUBS = {
    "DrawTile": "falling/dynamic tile debris not drawn (p6_stubs.cpp:209)",
    "DrawAniTile": "animated tiles not drawn (p6_stubs.cpp:210)",
    "DrawString": "text (HUD/dialogue/cards) not drawn (p6_stubs.cpp:211)",
    "DrawDeformedSprite": "deform FX (water ripple/heat haze) not drawn (p6_stubs.cpp:208)",
    "FillScreen": "full-screen flash/fill not drawn (p6_stubs.cpp:204)",
    "SwapDrawListEntries": "manual draw-order swap is a no-op (p6_stubs.cpp:203)",
    "LoadVideo": "Cinepak/FMV not loaded (p6_stubs.cpp:224)",
}

# Sheets already staged into the SaturnSheet band store (build_sheet_bands.py).
def load_staged_sheets():
    txt = open(SHEETS_PY, encoding="utf-8", errors="replace").read()
    # entries like ("GHZ/Objects.gif", "GHZOBJ.SHT")
    return set(m.group(1) for m in re.finditer(r'\(\s*"([^"]+\.gif)"\s*,\s*"[^"]+\.SHT"\s*\)', txt))

def load_registered():
    txt = open(REG_C, encoding="utf-8", errors="replace").read()
    reg = set(re.findall(r'RSDK_REGISTER_OBJECT\(\s*(\w+)\s*\)', txt))
    # Objects registered via the overlay's own RegisterObject(...,"Name",...) call
    # (Ring is overlay-registered at p6_main.cpp:131, not in p6_wave1_reg.c).
    try:
        m = open(P6_MAIN, encoding="utf-8", errors="replace").read()
        reg |= set(re.findall(r'RegisterObject\([^,]+,\s*"(\w+)"', m))
    except OSError:
        pass
    return reg

def load_zone_objects():
    d = json.load(open(SCENES_JSON, encoding="utf-8"))
    zones = {}
    for z, v in d.items():
        objs = (v.get("stage_config") or {}).get("objects") if isinstance(v, dict) else None
        if isinstance(objs, list):
            zones[z] = objs
    return zones

# Animator member -> the StageLoad sheet/anim it loads. We capture the .bin path
# the object's StageLoad passes to LoadSpriteAnimation; the sheet .gif is named
# inside the .bin (not statically visible here), so we record the .bin and let
# the cross-ref note "anim .bin" residency separately from a direct .gif sheet.
RE_LOADANIM = re.compile(r'LoadSpriteAnimation\(\s*"([^"]+)"')
RE_LOADSHEET = re.compile(r'LoadSpriteSheet\(\s*"([^"]+)"')
RE_GETSFX = re.compile(r'GetSfx\(\s*"([^"]+)"')
RE_CREATE = re.compile(r'CREATE_ENTITY\(\s*(\w+)')
RE_GETENT = re.compile(r'RSDK_GET_ENTITY\([^,]+,\s*(\w+)\s*\)')
RE_RSDKAPI = re.compile(r'\bRSDK\.(\w+)\s*\(')
RE_DRAWANY = re.compile(r'\bRSDK\.(DrawSprite|DrawTile|DrawAniTile|DrawString|DrawDeformedSprite|'
                        r'DrawLine|DrawRect|DrawCircle|DrawCircleOutline|DrawBlendedQuad|DrawFace|DrawAniTiles)\s*\(')

def draw_function_empty(txt, name):
    # <Name>_Draw(void) {}  or with only whitespace/comments between braces
    m = re.search(re.escape(name) + r'_Draw\(void\)\s*\{(.*?)\}', txt, re.DOTALL)
    if not m:
        return None  # no Draw function found
    body = re.sub(r'//.*', '', m.group(1))
    body = re.sub(r'/\*.*?\*/', '', body, flags=re.DOTALL)
    return body.strip() == ""

def classify(path):
    fn = os.path.basename(path)
    m = re.match(r'SonicMania_Objects_([A-Za-z0-9]+)_(.+)\.c$', fn)
    if not m:
        return None
    cat, name = m.group(1), m.group(2)
    txt = open(path, encoding="utf-8", errors="replace").read()

    # Strip the editor block so EditorDraw/EditorLoad don't pollute residency/draw.
    txt_play = re.split(r'#if\s+GAME_INCLUDE_EDITOR', txt)[0]

    is_object = bool(re.search(r'\bvoid\s+' + re.escape(name) + r'_(Update|Create|Draw)\b', txt))
    draws = sorted(set(re.findall(RE_DRAWANY, txt_play)))
    draw_empty = draw_function_empty(txt_play, name)
    debug_only = bool(re.search(r'visible\s*=\s*DebugMode->debugActive', txt_play))

    anims = sorted(set(RE_LOADANIM.findall(txt_play)))
    sheets = sorted(set(RE_LOADSHEET.findall(txt_play)))
    # Resolve each anim .bin -> its sheet .gif(s) so residency counts the real
    # sheet (badniks load via .bin). missing_bins = referenced but not extracted.
    anim_sheets, missing_bins = set(), []
    for a in anims:
        rs = bin_sheets(a)
        if rs is None:
            missing_bins.append(a)
        else:
            anim_sheets.update(rs)
    all_sheets = sorted(set(sheets) | anim_sheets)
    sfx = sorted(set(RE_GETSFX.findall(txt_play)))
    deps = sorted(set(RE_CREATE.findall(txt_play)) | set(RE_GETENT.findall(txt_play)))
    apis = sorted(set(RE_RSDKAPI.findall(txt_play)))
    stub_hits = {a: DRAW_STUBS[a] for a in apis if a in DRAW_STUBS}

    # draw_kind summary
    if draw_empty and not draws:
        kind = "invisible"
    elif debug_only and all(d in ("DrawLine", "DrawRect", "DrawSprite") for d in draws):
        kind = "debug-only"
    elif "DrawTile" in draws or "DrawAniTiles" in draws or "DrawAniTile" in draws:
        kind = "tile-draw"
    elif "DrawSprite" in draws or "DrawString" in draws:
        kind = "sprite-draw"
    elif draws:
        kind = "primitive-draw"
    else:
        kind = "no-draw" if not is_object else "unknown"

    return name, {
        "category": cat, "name": name, "is_object": is_object,
        "draw_kind": kind, "draw_apis": draws, "debug_only_visible": debug_only,
        "load_anims": anims, "load_sheets": sheets, "load_sfx": sfx,
        "anim_sheets": sorted(anim_sheets), "all_sheets": all_sheets,
        "missing_bins": missing_bins,
        "spawn_or_ref_deps": [d for d in deps if d != name],
        "rsdk_apis": apis,
        "stub_gated_visuals": stub_hits,
        "rsdk_api_count": len(apis),
    }

def main():
    staged = load_staged_sheets()
    registered = load_registered()
    zones = load_zone_objects()

    objects = {}
    for fn in sorted(os.listdir(RAW)):
        if fn.startswith("SonicMania_Objects_") and fn.endswith(".c"):
            r = classify(os.path.join(RAW, fn))
            if r:
                objects[r[0]] = r[1]

    # Cross-reference per object.
    for name, o in objects.items():
        o["registered"] = name in registered
        o["sheets_unstaged"] = [s for s in o["all_sheets"] if s not in staged]
        o["deps_unregistered"] = [d for d in o["spawn_or_ref_deps"]
                                  if d in objects and not (d in registered)]

    # Transitive closure helper: all objects needed to make `seed` set work.
    def closure(seed):
        seen, stack = set(), list(seed)
        while stack:
            n = stack.pop()
            if n in seen or n not in objects:
                continue
            seen.add(n)
            for d in objects[n]["spawn_or_ref_deps"]:
                if d in objects and d not in seen:
                    stack.append(d)
        return seen

    # Per-zone rollup: closure of the StageConfig objects + what's missing.
    zone_rollup = {}
    for z, objs in zones.items():
        cl = closure([o for o in objs if o in objects])
        need_reg = sorted(n for n in cl if not objects[n]["registered"])
        stub_hit = {n: objects[n]["stub_gated_visuals"] for n in cl if objects[n]["stub_gated_visuals"]}
        sheets_need = sorted(set(s for n in cl for s in objects[n]["sheets_unstaged"]))
        anims_need = sorted(set(a for n in cl for a in objects[n]["load_anims"]))
        zone_rollup[z] = {
            "stageconfig_count": len(objs),
            "closure_count": len(cl),
            "need_register": need_reg,
            "need_register_count": len(need_reg),
            "sheets_to_stage": sheets_need,
            "anim_bins_loaded": anims_need,
            "stub_gated_objects": stub_hit,
        }

    manifest = {
        "_comment": "GENERATED by build_object_census.py -- drop-in-readiness census",
        "draw_stub_set": DRAW_STUBS,
        "staged_sheets": sorted(staged),
        "registered_objects": sorted(registered),
        "object_count": len(objects),
        "objects": objects,
        "zones": zone_rollup,
    }
    json.dump(manifest, open(OUT_JSON, "w", encoding="utf-8"), indent=1)

    # ---- stdout summary --------------------------------------------------
    print("=" * 74)
    print("OBJECT CENSUS -- %d decomp objects, %d registered, %d sheets staged"
          % (len(objects), len(registered), len(staged)))
    print("=" * 74)
    kinds = {}
    for o in objects.values():
        kinds[o["draw_kind"]] = kinds.get(o["draw_kind"], 0) + 1
    print("draw-kind histogram:", ", ".join("%s=%d" % kv for kv in sorted(kinds.items())))
    nstub = sum(1 for o in objects.values() if o["stub_gated_visuals"])
    print("objects whose visuals hit a Saturn draw-stub:", nstub)
    print()
    gz = zone_rollup.get("GHZ", {})
    print("GHZ closure: %d objects from %d StageConfig entries; %d still need registering"
          % (gz.get("closure_count", 0), gz.get("stageconfig_count", 0), gz.get("need_register_count", 0)))
    print("  GHZ need_register:", ", ".join(gz.get("need_register", [])) or "(none)")
    print("  GHZ sheets_to_stage:", ", ".join(gz.get("sheets_to_stage", [])) or "(none -- all reuse staged sheets)")
    print("  GHZ stub-gated objects:")
    for n, hits in sorted(gz.get("stub_gated_objects", {}).items()):
        print("     %-20s %s" % (n, "; ".join(hits.values())))
    print()
    print("manifest -> %s (%d zones rolled up)" % (os.path.relpath(OUT_JSON, ROOT), len(zone_rollup)))
    return 0

if __name__ == "__main__":
    sys.exit(main())
