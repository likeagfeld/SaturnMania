#!/usr/bin/env python3
# =============================================================================
# qa_entity_slots.py -- RED-first gate for the dense-act entity-slot DROP wall.
#
# Resolves WHOLE_GAME_MASSPORT_PLAN.md s7.3 (2026-06-17): "does
# scene_census.entity_total == live scene-slot occupancy, or does it count
# placed objects the engine filters out of slots?"  MEASURED ANSWER, from the
# engine's own LoadScene (rsdkv5-src/RSDKv5/RSDK/Scene/Scene.cpp:620-646):
#
#   Each scene entity is placed by its EXPLICIT slotID read from Scene.bin
#   (line 622), NOT a sequential counter. The Saturn branch:
#     slotID <  SCENEENTITY_COUNT (0x440=1088)          -> scene region (RSDK_ENTITY_AT)
#     SCENEENTITY_COUNT <= slotID < +TEMPSCENE_COUNT     -> 64-slot temp buffer (SPILL)
#     slotID >= SCENEENTITY_COUNT+TEMPSCENE_COUNT (1152) -> DROPPED (sunk into the last
#                                                           temp slot, overwritten) and
#                                                           WITNESSED p6_saturn_tempentity_skips
#   objectEntityList is a FIXED ENTITY_COUNT=0x4C0=1216 allocation regardless of
#   scene density (Object.hpp:43-45). So a dense act does NOT overflow WRAM /
#   crash -- it SILENTLY DROPS the high-slotID entities. The failure mode is
#   CONTENT PARITY (missing objects), the threshold is slotID>=1152, and it is
#   already instrumented. (This CORRECTS the second-synthesis "G-2 hard WRAM-L
#   slot-table overflow / load-time crisis" framing -- see plan s7.1/s7.3.)
#
# scene_census.entity_total is a COUNT (build_scene_census.py:127, sum of
# per-type counts); the per-entity slotIDs live in scene_census _coords. Drops
# are governed by MAX slotID, so this gate computes the real max slotID + drop /
# spill counts per scene from _coords (authoritative, uncapped) -- never the
# count. Mania packs slotIDs densely (Object.hpp note: GHZ1 = 1041 entities, max
# slot 1040), so for dense acts max(slotID) ~= entity_total-1, but we MEASURE it
# rather than assume.
#
# Thresholds are PARSED FROM Object.hpp at runtime so this gate cannot drift from
# the engine.
#
# CONTRACT:
#   * SHIPPING scope (default GHZ/Scene1.bin + GHZ/Scene2.bin): RED if any shipped
#     act would silently DROP entities (max slotID >= drop threshold). Mirrors the
#     engine's own gate expectation p6_saturn_tempentity_skips == 0.
#   * WHOLE-GAME roster: prints every DROP-class scene + its drop count -- the
#     documented parity wall each dense zone's port must close (raise
#     SCENEENTITY_COUNT for that act, camera-stream entities, or accept the drop
#     for non-critical far objects). A zone entering shipping scope flips this gate
#     RED until its dense-act fix lands (per-zone RED-first, plan s2.1).
#
# Usage:
#   python tools/qa_entity_slots.py                 # default GHZ shipping scope
#   python tools/qa_entity_slots.py GHZ/Scene1.bin OOZ/Scene1.bin   # custom scope
# =============================================================================
import json
import os
import re
import sys

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
CENSUS = os.path.join(ROOT, "docs", "scene_census.json")
OBJECT_HPP = os.path.join(ROOT, "rsdkv5-src", "RSDKv5", "RSDK", "Scene", "Object.hpp")

DEFAULT_SHIP_SCOPE = ["GHZ/Scene1.bin", "GHZ/Scene2.bin"]


def parse_int(tok):
    tok = tok.strip()
    return int(tok, 16) if tok.lower().startswith("0x") else int(tok)


def object_hpp_constants():
    """Parse SCENEENTITY_COUNT / TEMPENTITY_COUNT / RESERVE_ENTITY_COUNT from the
    engine header so the gate's thresholds track the engine, never a hardcode.
    Note Scene.cpp:552 hardcodes the Saturn temp-list cap as enum
    TEMPSCENE_COUNT = 0x40 (== TEMPENTITY_COUNT here); both are 0x40."""
    txt = open(OBJECT_HPP, "r", encoding="utf-8", errors="replace").read()
    out = {}
    for name in ("RESERVE_ENTITY_COUNT", "SCENEENTITY_COUNT", "TEMPENTITY_COUNT"):
        m = re.search(r"#define\s+" + name + r"\s+\(?\s*(0x[0-9A-Fa-f]+|\d+)", txt)
        if not m:
            raise RuntimeError("could not parse %s from Object.hpp" % name)
        out[name] = parse_int(m.group(1))
    return out


def main():
    scope = sys.argv[1:] if len(sys.argv) > 1 else DEFAULT_SHIP_SCOPE

    c = object_hpp_constants()
    SCENE = c["SCENEENTITY_COUNT"]                 # 0x440 = 1088 scene slots
    TEMP = c["TEMPENTITY_COUNT"]                    # 0x40  = 64 (Scene.cpp TEMPSCENE_COUNT)
    SPILL_AT = SCENE                                # 1088: slotID>=this spills to temp buffer
    DROP_AT = SCENE + TEMP                          # 1152: slotID>=this is DROPPED + witnessed

    census = json.load(open(CENSUS, "r", encoding="utf-8"))
    scenes = census.get("scenes", {})

    rows = []  # (key, n_ent, max_slot, n_spill, n_drop)
    for key, s in sorted(scenes.items()):
        coords = s.get("_coords")
        if coords is None:
            continue
        slots = [slot for lst in coords.values() for (slot, _x, _y) in lst]
        if not slots:
            rows.append((key, 0, -1, 0, 0))
            continue
        max_slot = max(slots)
        n_drop = sum(1 for s2 in slots if s2 >= DROP_AT)
        n_spill = sum(1 for s2 in slots if SPILL_AT <= s2 < DROP_AT)
        rows.append((key, len(slots), max_slot, n_spill, n_drop))

    safe = [r for r in rows if r[2] < SPILL_AT]
    spill = [r for r in rows if SPILL_AT <= r[2] < DROP_AT]
    drop = [r for r in rows if r[2] >= DROP_AT]

    print("=== qa_entity_slots :: dense-act entity-slot DROP wall ===")
    print("Object.hpp: RESERVE=0x%X SCENEENTITY=0x%X(%d) TEMP=0x%X(%d)"
          % (c["RESERVE_ENTITY_COUNT"], SCENE, SCENE, TEMP, TEMP))
    print("spill threshold (temp buffer) slotID >= %d ; DROP threshold slotID >= %d"
          % (SPILL_AT, DROP_AT))
    print("scenes parsed=%d  SAFE(max<%d)=%d  SPILL(<%d)=%d  DROP(>=%d)=%d"
          % (len(rows), SPILL_AT, len(safe), DROP_AT, len(spill), DROP_AT, len(drop)))

    print("\n-- DROP-class scenes (silently lose entities at slotID >= %d) --" % DROP_AT)
    if drop:
        print("  %-22s %7s %8s %6s %6s" % ("scene", "n_ent", "max_slot", "spill", "DROP"))
        for key, n_ent, max_slot, n_spill, n_drop in sorted(drop, key=lambda r: -r[4]):
            print("  %-22s %7d %8d %6d %6d" % (key, n_ent, max_slot, n_spill, n_drop))
    else:
        print("  (none)")

    if spill:
        print("\n-- SPILL-class scenes (use the 64-slot temp buffer, 0 drops) --")
        for key, n_ent, max_slot, n_spill, n_drop in sorted(spill, key=lambda r: -r[2]):
            print("  %-22s n_ent=%d max_slot=%d spill=%d" % (key, n_ent, max_slot, n_spill))

    # ---- SHIPPING-scope assertion (RED-first contract) ----
    print("\n-- shipping scope: %s --" % ", ".join(scope))
    by_key = {r[0]: r for r in rows}
    failed = []
    for k in scope:
        r = by_key.get(k)
        if r is None:
            print("  MISSING from census: %s" % k)
            failed.append((k, "missing"))
            continue
        _, n_ent, max_slot, n_spill, n_drop = r
        verdict = "DROP" if n_drop else ("SPILL" if n_spill else "SAFE")
        print("  %-22s n_ent=%d max_slot=%d spill=%d drop=%d -> %s"
              % (k, n_ent, max_slot, n_spill, n_drop, verdict))
        if n_drop:
            failed.append((k, "drops=%d" % n_drop))

    if failed:
        print("\nRED: shipped act(s) drop entities -> %s" % failed)
        print("Close per plan 2.1: raise SCENEENTITY_COUNT for the act (WRAM cost),"
              " camera-stream entities, or accept the drop for non-critical far objects.")
        return 1
    print("\nGREEN: every shipped act loads all placed entities (0 drops).")
    print("(%d whole-game DROP-class scenes remain the documented per-zone wall.)" % len(drop))
    return 0


if __name__ == "__main__":
    sys.exit(main())
