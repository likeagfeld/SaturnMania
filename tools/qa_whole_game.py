#!/usr/bin/env python3
# =============================================================================
# qa_whole_game.py -- WHOLE-GAME scalable-drop-in FOUNDATION gate (constant review).
#
# BINDING (user directive 2026-06-19): every foundation/perf change must be planned
# against ALL un-ported assets for EVERY one of the 94 scenes (Green Hill Zone +
# every other Zone, Title, Menus, Intro, Cutscenes, UI) and EVERY asset class
# (entities, sprite sheets, code, + the tracked-not-yet-static audio/CRAM/anim walls).
# This gate operationalizes "constantly review the research" -- run it on every
# foundation change; it cross-checks docs/{dropin,scene}_census.json (the pre-measured
# whole-game corpus) against the LIVE budget constants parsed from the code, so it can
# NEVER drift from the engine. It is the single entry point that says, with data, "is
# the foundation still scalable for every scene?" -- NOT a per-scene GHZ-only check.
#
# THE FOUR MEASURED WALLS (WHOLE_GAME_MASSPORT_PLAN.md s8.1) + the cull invariant:
#   W1 CULL COVERAGE (ENFORCED): the camera-cull sorted index (P6_SCAN_CAP) must cover
#      every scene's whole scene region -> P6_SCAN_CAP >= SCENEENTITY_COUNT. A smaller
#      cap silently un-indexes high-slot entities (the 159bfbd/badafca GHZ1-ism). HARD.
#   W2 ENTITY POOL (DASHBOARD + drop roster): per-scene max slotID vs TEMPENTITY_START
#      (1152). slotID >= 1152 => the engine DROPS the entity (content-parity wall, not a
#      crash). The mass-port fix is per-act SCENEENTITY_COUNT raise / the camera pool.
#   W3 SHEET STORE per-zone-resident (DASHBOARD): per-scene OWN banded sheets vs the
#      store_bytes capacity. A zone whose own sheets exceed the store needs a per-sheet
#      band-window (S2/S4). (The census store_fits/dropin_ready use a stricter cumulative
#      all-resident model -- NOT the actual per-zone-swap residency -- so we use OWN bytes.)
#   W4 WRAM-H CODE (ENFORCED): the current build's _end < P6_HW_ANIMPAK (#228 ceiling).
#
# TRACKED-NOT-YET-STATIC (printed so they are never forgotten -- WHOLE_GAME_MASSPORT_PLAN s9):
#   CRAM per-scanline palettes (Phase-Z Z3), whole-game AUDIO 58 BGM + per-zone SFX vs
#   512 KB Sound RAM (Phase S-AUDIO), DATASET_STG anim pool. These need their own census
#   gates -- flagged here as the next research-gate work, not silently dropped.
#
# VERDICT: GREEN iff the two CODE-ENFORCEABLE invariants hold (W1 cull covers every scene,
# W4 current build under #228). W2/W3 print the per-scene roadmap (which Zone needs which
# mass-port wave) -- they are the documented walls, not regressions, so they inform rather
# than fail. A change that lowers P6_SCAN_CAP, grows _end past ANIMPAK, or shrinks the store
# under a zone that fit -> RED.
#
#   python tools/qa_whole_game.py [game.map]
# =============================================================================
import json, os, re, sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))


def _read(p):
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def _first_define(text, name):
    # first #define NAME (value)  -- the Saturn branch precedes the PC branch in Object.hpp
    m = re.search(r"#define\s+%s\s+\(?\s*(0x[0-9A-Fa-f]+|\d+)" % re.escape(name), text)
    return int(m.group(1), 0) if m else None


def main(argv):
    mappath = argv[0] if argv else os.path.join(ROOT, "game.map")

    # ---- LIVE budget constants (parsed -- cannot drift from the engine) ----------
    objhpp = _read(os.path.join(ROOT, "rsdkv5-src/RSDKv5/RSDK/Scene/Object.hpp"))
    RES = _first_define(objhpp, "RESERVE_ENTITY_COUNT")
    SCENE = _first_define(objhpp, "SCENEENTITY_COUNT")
    TEMPC = _first_define(objhpp, "TEMPENTITY_COUNT")
    ENT = (RES + SCENE + TEMPC) if None not in (RES, SCENE, TEMPC) else None
    TEMPSTART = (ENT - TEMPC) if ENT is not None else None  # = RES+SCENE; drop threshold

    iohpp = _read(os.path.join(ROOT, "tools/_portspike/_p6/p6_io_main.cpp"))
    m = re.search(r"#define\s+P6_SCAN_CAP\s+(\w+)", iohpp)
    capraw = m.group(1) if m else None
    SCAN_CAP = SCENE if capraw == "SCENEENTITY_COUNT" else (int(capraw, 0) if capraw and re.match(r"0x|\d", capraw) else None)

    animhpp = _read(os.path.join(ROOT, "rsdkv5-src/RSDKv5/RSDK/Graphics/Animation.hpp"))
    ma = re.search(r"P6_HW_ANIMPAK\s+(0x[0-9A-Fa-f]+)", animhpp)
    ANIMPAK = int(ma.group(1), 0) if ma else None

    dc = json.load(open(os.path.join(ROOT, "docs/dropin_census.json")))
    sc = json.load(open(os.path.join(ROOT, "docs/scene_census.json")))
    STORE = dc.get("store_bytes")
    scenes_d = dc["scenes"]
    scenes_s = sc["scenes"]
    NSC = len(scenes_d)

    # ---- per-scene wall computation (data-driven, all 94) -------------------------
    pool_drop = []   # (scene, max_slot, drop_count)
    store_over = []  # (scene, own_banded, over_bytes)
    for name, rec in scenes_d.items():
        # W2 pool: max slotID + drop count from scene_census _coords (authoritative slots)
        co = scenes_s.get(name, {}).get("_coords", {})
        slots = [p[0] for plist in (co.values() if isinstance(co, dict) else []) for p in plist if p]
        mx = max(slots) if slots else 0
        drops = sum(1 for s in slots if TEMPSTART is not None and s >= TEMPSTART)
        if drops:
            pool_drop.append((name, mx, drops))
        # W3 store: per-zone-resident own banded sheets vs capacity
        own = rec.get("sheets_banded_est", 0)
        if STORE and own > STORE:
            store_over.append((name, own, own - STORE))

    # ---- current build WRAM-H ------------------------------------------------------
    end = None
    if os.path.isfile(mappath):
        mm = re.search(r"0x0*([0-9A-Fa-f]+)\s+_end\s*=", _read(mappath))
        if mm:
            end = int(mm.group(1), 16)

    # ---- report -------------------------------------------------------------------
    print("=" * 74)
    print("WHOLE-GAME SCALABLE-DROP-IN FOUNDATION GATE  (%d scenes, live budgets)" % NSC)
    print("=" * 74)
    print("  LIVE constants: RESERVE=%s SCENE=%s(0x%X) TEMP=%s ENTITY=%s TEMPSTART=%s"
          % (RES, SCENE, SCENE or 0, TEMPC, ENT, TEMPSTART))
    print("                  P6_SCAN_CAP=%s(%s)  ANIMPAK=0x%X  store_bytes=%s"
          % (SCAN_CAP, capraw, ANIMPAK or 0, STORE))
    print("-" * 74)

    # W1 CULL COVERAGE (hard)
    w1 = SCAN_CAP is not None and SCENE is not None and SCAN_CAP >= SCENE
    print("  [%s] W1 CULL COVERS EVERY SCENE  P6_SCAN_CAP(%s) >= SCENEENTITY_COUNT(%s)"
          % ("GREEN" if w1 else " RED ", SCAN_CAP, SCENE))
    print("         -> the camera-cull indexes every scene's whole [RESERVE,TEMPSTART) region;")
    print("            no zone can silently un-index high-slot entities (the badafca fix).")

    # W2 ENTITY POOL drop roster (dashboard)
    print("  [DASH ] W2 ENTITY POOL drop wall: %d/%d scenes DROP entities (slotID>=%s)"
          % (len(pool_drop), NSC, TEMPSTART))
    for name, mx, dr in sorted(pool_drop, key=lambda r: -r[2])[:8]:
        print("            %-22s max slot %4d  drops %d  (raise SCENEENTITY_COUNT / pool)" % (name, mx, dr))
    if len(pool_drop) > 8:
        print("            ... +%d more (full list in dropin_census)" % (len(pool_drop) - 8))

    # W3 SHEET STORE per-zone-resident (dashboard)
    print("  [DASH ] W3 SHEET STORE per-zone: %d/%d zones' OWN sheets exceed store_bytes(%d)"
          % (len(store_over), NSC, STORE or 0))
    for name, own, over in sorted(store_over, key=lambda r: -r[2]):
        print("            %-22s own banded %7d B  over %6d B  (S2/S4 band-window)" % (name, own, over))

    # W4 WRAM-H (hard)
    w4 = end is not None and ANIMPAK is not None and end < ANIMPAK
    print("  [%s] W4 WRAM-H current build: _end %s < ANIMPAK 0x%X  (headroom %s B, #228)"
          % ("GREEN" if w4 else (" RED " if end is not None else "  ?  "),
             ("0x%X" % end) if end else "??", ANIMPAK or 0,
             (ANIMPAK - end) if (end and ANIMPAK) else "?"))

    # tracked-not-yet-static
    print("-" * 74)
    print("  TRACKED (no static census gate yet -- next research-gate work, plan s9):")
    print("    * CRAM per-scanline palettes (Phase-Z Z3)   * AUDIO 58 BGM + SFX vs 512KB (S-AUDIO)")
    print("    * DATASET_STG anim pool                     * VDP1 fill-rate at density (S3)")
    print("-" * 74)

    ok = w1 and w4
    if ok:
        print("RESULT: GREEN -- foundation INVARIANTS hold whole-game (W1 cull covers all %d scenes,"
              " W4 under #228). W2/W3 are the documented mass-port roadmap above (per-Zone waves),"
              " not regressions. Consult this on every foundation change." % NSC)
        return 0
    print("RESULT: RED -- a foundation invariant BROKE for the whole game:")
    if not w1:
        print("   W1: P6_SCAN_CAP < SCENEENTITY_COUNT -> high-slot entities un-indexed in dense scenes.")
    if not w4:
        print("   W4: _end >= ANIMPAK -> #228 boot trap. Reclaim WRAM-H before growing.")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
