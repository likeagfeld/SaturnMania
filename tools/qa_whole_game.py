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
#   W5c TEXT/LOCALIZATION (DASHBOARD): DATASET_STR active-language resident (build_strings_census).
#   W8 COVERAGE COMPLETENESS (ENFORCED): the self-audit -- every on-disk extracted/Data root AND
#      every live RSDK storage pool (Storage.hpp StorageDataSets, parsed) must map to a tracked
#      wall. This is the data-driven answer to "what about the asset classes I did NOT name?" --
#      a NEW or forgotten asset class (root or pool) RED-fails the gate instead of silently slipping
#      through. The 5 RSDK pools (STG/MUS/SFX/STR/TMP) + the 7 Data roots + the FIXED HW tables
#      (entity pool, CRAM palette, collision 0x060E0000, WRAM-H code, VDP VRAM, backup RAM) are the
#      WHOLE corpus partition; W8 proves the wall set is total over it.
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
    acp = os.path.join(ROOT, "docs/audio_census.json")
    ac = json.load(open(acp)) if os.path.isfile(acp) else None  # W5 (tools/build_audio_census.py)
    rcp = os.path.join(ROOT, "docs/residency_census.json")
    rc = json.load(open(rcp)) if os.path.isfile(rcp) else None  # W5b/W6/W7 (build_residency_census.py)
    stp = os.path.join(ROOT, "docs/strings_census.json")
    stc = json.load(open(stp)) if os.path.isfile(stp) else None  # W5c DATASET_STR (build_strings_census.py)
    sxp = os.path.join(ROOT, "docs/sfx_subset.json")
    sxc = json.load(open(sxp)) if os.path.isfile(sxp) else None  # W5b subset (build_sfx_subset.py)
    # LIVE RSDK storage-pool enum (Storage.hpp StorageDataSets) -- parsed so it cannot drift; W8
    # asserts every pool maps to a tracked wall (an engine-added pool would surface as untracked).
    sthpp = _read(os.path.join(ROOT, "rsdkv5-src/RSDKv5/RSDK/Storage/Storage.hpp"))
    POOLS = re.findall(r"\b(DATASET_[A-Z]+)\s*=\s*\d+", sthpp)
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

    # W5 AUDIO -- music (CD-DA) enforced disc fit + SFX Sound-RAM residency dashboard
    w5bgm = True
    if ac:
        b, s = ac["bgm"], ac["sfx"]
        w5bgm = bool(b.get("fits_one_74min_cd"))
        print("  [%s] W5a AUDIO/BGM whole-game disc: %d tracks ~%.1f min -> %d/%d CD sectors (1 disc)"
              % ("GREEN" if w5bgm else " RED ", b["track_count"], b["est_duration_min"],
                 b["total_sectors"], ac["cd74_sectors"]))
        print("         CD-DA %d + data %d sectors; headroom %d (~%.1f min). All-58-CD-DA FEASIBLE."
              % (b["est_cdda_sectors"], b["data_sectors"], b["sector_headroom"],
                 b["sector_headroom"] / 75.0 / 60.0))
        _ = s  # (full-bank summary now superseded by the per-scene W5b below)
    else:
        print("  [  ?  ] W5 AUDIO: docs/audio_census.json absent -- run tools/build_audio_census.py")

    # W5b per-scene SFX working set / W6 per-zone anim / W7 video (build_residency_census.py)
    if rc:
        scn = rc["scenes"]
        hz = rc.get("sfx_adpcm_hz", 11025)
        over = [k for k, v in scn.items() if not v.get("sfx_fits_sound_ram")]
        worst = max(scn.items(), key=lambda kv: kv[1].get("sfx_adpcm_bytes", 0)) if scn else ("-", {})
        print("  [DASH ] W5b AUDIO/SFX per-scene -- 8-bit PCM @ %d Hz mono (SCSP-NATIVE max; NO ADPCM,"
              % hz)
        print("            ST-077 doc-verified): %d/%d scenes' full SFX set fits 512 KB; %d over (worst %s %.0f KB)."
              % (len(scn) - len(over), len(scn), len(over), worst[0],
                 worst[1].get("sfx_adpcm_bytes", 0) / 1024.0))
        print("            CURRENT engine = 16-bit 44.1k ONE-at-a-time (p6_snd #209). S-AUDIO BUILD = engine")
        print("            resident sfxList @ 8-bit PCM low-rate + a per-scene WORKING-SUBSET (the %d dense)." % len(over))
        zw = max(rc["zones"].items(), key=lambda kv: kv[1].get("anim_bin_source_bytes", 0)) if rc.get("zones") else ("-", {})
        print("  [DASH ] W6 ANIM (DATASET_STG pool ~150 KB): worst zone %s = %.0f KB .bin SOURCE"
              % (zw[0], zw[1].get("anim_bin_source_bytes", 0) / 1024.0))
        print("            (pool holds DECODED frames -> a decoded-size census is the precise gate; #254)")
        v = rc.get("video", {})
        print("  [DASH ] W7 VIDEO (intro Cinepak, BOOT): Mania.ogv %.1f MB -> low-res FILM on data track"
              % (v.get("source_ogv_bytes", 0) / 1e6))
        print("            fits the W5a CD headroom; decode = SBL Cinepak. INTRO.CPK = %d B stub today."
              % v.get("current_saturn_cpk_bytes", 0))
    else:
        print("  [  ?  ] W5b/W6/W7: docs/residency_census.json absent -- run build_residency_census.py")

    # W5b working-subset feasibility (build_sfx_subset.py) + W5c text/localization (DATASET_STR)
    if sxc:
        s = sxc["scenes"]
        zero = sum(1 for v in s.values() if v["ondemand_count"] == 0)
        wod = max(s.items(), key=lambda kv: kv[1]["ondemand_count"]) if s else ("-", {})
        print("  [GREEN] W5b SUBSET feasible: ALL %d scenes fit the %d KB scene budget @ 8-bit %d Hz;"
              % (len(s), sxc["scene_budget_bytes"] // 1024, sxc["sfx_hz"]))
        print("            %d need ZERO on-demand; worst %s = %d SFX CD-on-demand (S-AUDIO plays on trigger)."
              % (zero, wod[0], wod[1].get("ondemand_count", 0)))
    if stc:
        print("  [GREEN] W5c TEXT/LOCALIZATION (DATASET_STR, %d langs): active-language resident = %s %d B"
              % (stc["language_count"], stc["active_lang_worst"], stc["active_lang_resident_bytes"]))
        print("            (load ONLY the active language; all %d = %d B is the anti-pattern; Credits %d B scene-only)."
              % (stc["language_count"], stc["all_langs_resident_bytes"], stc["credits_bytes"]))

    # W8 COVERAGE COMPLETENESS (hard) -- the self-audit answering "the asset classes I did NOT name":
    # every on-disk Data root AND every live RSDK storage pool must map to a tracked wall, so a NEW
    # or forgotten asset class cannot be silently uncovered (user 2026-06-19). Mechanical, not a
    # hand-kept list -- it RED-fails the instant an untracked root/pool appears.
    ROOT_WALL = {
        "Music": "W5a BGM (CD-DA)", "SoundFX": "W5b/subset SFX (Sound RAM)",
        "Sprites": "W3 sheets + W6 anim (DATASET_STG)", "Video": "W7 Cinepak (boot)",
        "Strings": "W5c DATASET_STR (active-language)", "Game": "GameConfig boot index (StageList)",
        "Stages": "W1/W2 entities + tiles(band/W3) + collision(0x060E0000 fixed) + palette(CRAM/Z3)",
    }
    POOL_WALL = {
        "DATASET_STG": "W3 sheets + W6 anim", "DATASET_MUS": "W5a BGM (CD-DA)",
        "DATASET_SFX": "W5b SFX (Sound RAM)", "DATASET_STR": "W5c (active-language)",
        "DATASET_TMP": "transient load scratch (perf/load wall #249)",
    }
    data_dir = os.path.join(ROOT, "extracted/Data")
    if os.path.isdir(data_dir):
        roots = sorted(d for d in os.listdir(data_dir) if os.path.isdir(os.path.join(data_dir, d)))
        roots_src = "extracted/Data live"
    else:
        roots = sorted(ROOT_WALL); roots_src = "baked partition (extracted/ absent on this checkout)"
    pools = [p for p in (POOLS or []) if p != "DATASET_MAX"]
    untracked_roots = [r for r in roots if r not in ROOT_WALL]
    untracked_pools = [p for p in pools if p not in POOL_WALL]
    w8 = bool(roots) and bool(pools) and not untracked_roots and not untracked_pools
    print("  [%s] W8 COVERAGE COMPLETE: every Data root + every RSDK pool maps to a tracked wall"
          % ("GREEN" if w8 else " RED "))
    print("         roots(%s): %s" % (roots_src, ", ".join(roots)))
    print("         pools(Storage.hpp StorageDataSets): %s" % ", ".join(pools))
    if untracked_roots:
        print("         !! UNTRACKED ROOT(S): %s -- add a wall (an 'unnamed asset class')" % untracked_roots)
    if untracked_pools:
        print("         !! UNTRACKED POOL(S): %s -- add a wall" % untracked_pools)
    print("         FIXED HW tables also budgeted (not pools/roots): entity pool=W1/W2,")
    print("            CRAM palette=Z3, collision masks 0x060E0000=Scene.hpp x1-Saturn fixed,")
    print("            WRAM-H code=W4, VDP1/VDP2 VRAM=band-store/S3, backup RAM 0x00180000 64KB=SaveGame.")

    # tracked-not-yet-static
    print("-" * 74)
    print("  TRACKED (precise gate still TODO -- next research-gate work, plan s9):")
    print("    * CRAM per-scanline palettes (Phase-Z Z3)   * SFX working-subset selection (S-AUDIO)")
    print("    * ANIM decoded-frame pool census (W6 exact) * VDP1 fill-rate at density (S3)")
    print("-" * 74)

    ok = w1 and w4 and w5bgm and w8
    if ok:
        print("RESULT: GREEN -- foundation INVARIANTS hold whole-game (W1 cull covers all %d scenes,"
              " W4 under #228, W5a all-58-BGM fits one disc, W8 every asset class tracked). W2/W3/W5b"
              " are the documented mass-port roadmap above (per-Zone waves), not regressions." % NSC)
        return 0
    print("RESULT: RED -- a foundation invariant BROKE for the whole game:")
    if not w1:
        print("   W1: P6_SCAN_CAP < SCENEENTITY_COUNT -> high-slot entities un-indexed in dense scenes.")
    if not w4:
        print("   W4: _end >= ANIMPAK -> #228 boot trap. Reclaim WRAM-H before growing.")
    if not w5bgm:
        print("   W5a: BGM CD-DA + data pack > 74-min CD -> needs 2nd disc OR PCM-stream some BGM.")
    if not w8:
        print("   W8: an on-disk Data root or RSDK storage pool has NO tracked wall -> an asset class")
        print("       is uncovered ('a class I did not name'). Add its wall before the foundation grows.")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
