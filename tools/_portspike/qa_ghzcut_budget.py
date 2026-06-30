#!/usr/bin/env python3
# =============================================================================
# qa_ghzcut_budget.py -- OFFLINE memory-budget gate for the AIZ->GHZCutscene
# handoff (Task #309 architectural step). Per CLAUDE.md object-add-preflight:
# PROVE the whole load path offline BEFORE any speculative build.
#
# The AIZ intro's final beat (CutsceneSonic_LoadGHZ, AIZSetup.c:896) does
#   RSDK.SetScene("Cutscenes","Green Hill Zone"); RSDK.LoadScene();
# GameConfig resolves "Cutscenes"/"Green Hill Zone" -> folder GHZCutscene id 1
# -> extracted/Data/Stages/GHZCutscene/Scene1.bin (VERIFIED via parse_gameconfig).
#
# This gate measures, WITHOUT writing any engine artifact (it must NOT clobber
# the GHZ1 p6_layout_model.json / probes.inc that build_layout_bands.py emits):
#   B1  layout band store bytes vs the P6_LW_LAYOUTBANDS window (0xC800=51,200)
#   B2  widest raw band vs the inflate scratch (P6_LW_LAYSCRATCH 0x8000=32,768)
#   B3  the <tag>LAYT.BIN filename length vs the SGL GFS 8.3 limit (name<=8)
#   B4  Scene1 object table -> which classes are NEW (not already AIZ/GHZ ported)
#
# Exit 0 = every hard memory unknown fits; nonzero = a wall the plan must clear.
# =============================================================================
import importlib.util
import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def _load(modname, relpath):
    spec = importlib.util.spec_from_file_location(modname, os.path.join(ROOT, relpath))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


blb = _load("blb", "tools/build_layout_bands.py")
pte = _load("pte", "tools/parse_title_entities.py")

# Window / scratch constants -- mirror p6_io_main.cpp #defines (the LIVE source).
WINDOW = 0xC800        # P6_LW_LAYOUTBANDS window bytes
SCRATCH = 0x8000       # P6_LW_LAYSCRATCH inflate scratch bytes
GFS_NAME_MAX = 8       # SGL 8.3: "<tag>LAYT" name part must be <= 8 chars

SCENE = "extracted/Data/Stages/GHZCutscene/Scene1.bin"
FOLDER = "GHZCutscene"
SCENE_ID = "1"

# Classes the engine ALREADY has ported+registered (AIZ front-end flavor + GHZ),
# so they cost ZERO new code for the GHZCutscene port. Source of truth:
#  - AIZ intro work this session: CutsceneSeq, PhantomRuby, AIZKingClaw, AIZEggRobo
#  - GHZ gameplay pack: Platform, Player, Camera + the global subsystems.
ALREADY_PORTED = {
    "CutsceneSeq", "PhantomRuby", "AIZKingClaw", "AIZEggRobo", "Platform",
    "Player", "Camera", "Music", "SaveGame", "Localization", "APICallback",
    "Dust", "ImageTrail", "Ring", "Spikes", "Spring", "Bridge", "PlaneSwitch",
    "ItemBox", "SignPost", "TitleCard", "ActClear", "HUD", "Animals",
    "Explosion", "ScoreBonus", "BoundsMarker", "InvisibleBlock",
}

# Full StageConfig object load list for the GHZCutscene folder (from
# StageConfig.bin -> docs/scene_objects.json). Used to seed hash resolution.
STAGECFG_OBJECTS = [
    "GHZSetup", "Platform", "BGSwitch", "PhantomRuby", "FXRuby", "AIZKingClaw",
    "GHZCutsceneST", "GHZCutsceneK", "CutsceneSeq", "CutsceneHBH", "FXFade",
    "FXTrail", "GHZ2Outro", "DERobot", "Eggman",
]
# Likely globals declared (0-entity) so the slot tables pre-allocate.
EXTRA_GLOBALS = [
    "Player", "Camera", "Music", "SaveGame", "Localization", "APICallback",
    "Zone", "Dust", "ImageTrail", "Ring", "PauseMenu", "HUD", "TitleCard",
    "ActClear", "DebugMode", "Animals", "Explosion", "ScoreBonus",
]


def measure_bands(scene_path):
    layers = blb.parse_layers(scene_path)
    max_w = max((xs for _, xs, _, _ in layers), default=1)
    band_rows = max(1, min(blb.MAX_BAND_ROWS, blb.SCRATCH_CAP // (max_w * 2)))
    n_layers = len(layers)
    head = 8 + n_layers * 8
    band_counts = [(ys + band_rows - 1) // band_rows for _, _, ys, _ in layers]
    dir_bytes = sum(bc * 12 for bc in band_counts)
    total = head + dir_bytes
    max_raw = 0
    layer_info = []
    for (name, xs, ys, layout), bc in zip(layers, band_counts):
        zsum = 0
        for b in range(bc):
            raw = layout[b * band_rows * xs * 2:(b + 1) * band_rows * xs * 2]
            zsum += len(zlib.compress(raw, 9))
            max_raw = max(max_raw, len(raw))
        total += zsum
        layer_info.append((name, xs, ys, bc, zsum))
    return total, band_rows, max_raw, max_w, layer_info


def measure_objects(scene_path):
    # Seed the parser's hash table with the GHZCutscene class names so the
    # object table resolves by name instead of <unknown ...>.
    for n in STAGECFG_OBJECTS + EXTRA_GLOBALS:
        if n not in pte.KNOWN_NAMES:
            pte.KNOWN_NAMES.append(n)
    objects, consumed, total = pte.parse_entities(scene_path)
    return objects, consumed, total


def main():
    scene_path = os.path.join(ROOT, SCENE)
    raw_scene = os.path.getsize(scene_path)
    print("=== qa_ghzcut_budget: AIZ -> GHZCutscene/Scene1.bin handoff ===")
    print("scene: %s (%d B on disk)\n" % (SCENE, raw_scene))

    fails = []

    # ---- B1/B2 band store ----
    total, band_rows, max_raw, max_w, linfo = measure_bands(scene_path)
    print("[B1] layout band store: %d B  (window 0x%X = %d B)  %s" % (
        total, WINDOW, WINDOW,
        "FITS, %d B slack" % (WINDOW - total) if total <= WINDOW else "OVERFLOW"))
    if total > WINDOW:
        fails.append("B1 band store %d > window %d" % (total, WINDOW))
    print("[B2] widest raw band: %d B  (scratch 0x%X = %d B, band_rows=%d, maxW=%d)  %s" % (
        max_raw, SCRATCH, SCRATCH, band_rows, max_w,
        "FITS" if max_raw <= SCRATCH else "OVERFLOW"))
    if max_raw > SCRATCH:
        fails.append("B2 raw band %d > scratch %d" % (max_raw, SCRATCH))
    print("     layers:")
    for name, xs, ys, bc, zsum in linfo:
        print("       %-12s %4dx%-4d  %2d bands  %6d B z" % (name, xs, ys, bc, zsum))

    # ---- B3 filename length ----
    tag = (FOLDER[:7] + SCENE_ID)
    namepart = tag + "LAYT"
    fname = namepart + ".BIN"
    print("\n[B3] LAYT tag='%s'  name='%s' (%d chars; SGL 8.3 name<=%d)  %s" % (
        tag, namepart, len(namepart), GFS_NAME_MAX,
        "OK" if len(namepart) <= GFS_NAME_MAX else "TOO LONG -> needs a tag remap"))
    if len(namepart) > GFS_NAME_MAX:
        fails.append("B3 '%s.BIN' name part %d > %d chars (folder[:7]+id scheme)"
                     % (namepart, len(namepart), GFS_NAME_MAX))

    # ---- B4 object table ----
    objects, consumed, total_b = measure_objects(scene_path)
    print("\n[B4] Scene1 object table (consumed %d/%d B):" % (consumed, total_b))
    new_classes, ported_classes, unknown = [], [], []
    for o in objects:
        nm = o["name"]
        ne = len(o["entities"])
        if nm.startswith("<unknown"):
            unknown.append((nm, ne))
            tag2 = "UNKNOWN-HASH"
        elif nm in ALREADY_PORTED:
            ported_classes.append((nm, ne))
            tag2 = "ported"
        else:
            new_classes.append((nm, ne))
            tag2 = "*** NEW PORT ***"
        print("       %-16s %3d ent   %s" % (nm, ne, tag2))
    print("\n     NEW classes Scene1 instantiates (need porting): %s" %
          ([n for n, _ in new_classes] or "NONE"))
    print("     already-ported reused: %s" % [n for n, _ in ported_classes])
    if unknown:
        print("     UNRESOLVED hashes (class name not in seed list): %d" % len(unknown))

    print("\n=== VERDICT ===")
    if fails:
        for f in fails:
            print("  RED: " + f)
        print("RESULT: %d budget wall(s) -- see above." % len(fails))
        return 1
    print("  GREEN: all hard memory unknowns fit.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
