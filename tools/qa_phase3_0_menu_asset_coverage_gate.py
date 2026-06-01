#!/usr/bin/env python3
"""
qa_phase3_0_menu_asset_coverage_gate.py - Asset-coverage gate for Menu / Logos scenes.

Predicate: for every asset path referenced by the Menu/Logos-scene decomp
files in tools/_decomp_raw/SonicMania_Objects_Menu_*.c (LogoSetup + MenuSetup
+ all 51 UI* + menu-mode .c files) AND every trackFile attribute on Music
entities in Menu/Scene1.bin, the file MUST exist in extracted/Data/<path>
with size > 0.

Methodology (Phase 3.0-prep, second application of the Phase 1.33 BINDING
methodology -- mirror of tools/qa_phase1_33_asset_coverage_gate.py):

  1. Grep each cached decomp .c file for:
       RSDK.LoadSpriteAnimation("X")  -> Data/Sprites/X
       RSDK.LoadSpriteSheet("X")      -> Data/Sprites/X
       RSDK.LoadStringList(&_, "X", N) -> Data/Strings/X
       RSDK.LoadVideo("X", ...)       -> Data/Video/X
       RSDK.PlayStream("X", ...)      -> Data/Music/X
       RSDK.GetSfx("X")               -> Data/SoundFX/X
       Music_SetMusicTrack("X", ...)  -> Data/Music/X
  2. For every sprite .bin discovered, parse its sheets list and demand
     every referenced sheet GIF exists in extracted/ too.
  3. Parse Menu/Scene1.bin (via parse_title_entities.parse_entities) and
     enumerate every Music entity's trackFile attribute -> demand
     Data/Music/<trackFile> exists.
  4. Print PASS / FAIL with the missing-list.

Run: python tools/qa_phase3_0_menu_asset_coverage_gate.py
Exit code: 0 GREEN; 1 RED (with missing paths in stdout).
"""
import os
import re
import struct
import sys


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DECOMP_DIR = os.path.join(REPO_ROOT, "tools", "_decomp_raw")
EXTRACTED = os.path.join(REPO_ROOT, "extracted")
MENU_SCENE1 = os.path.join(EXTRACTED, "Data", "Stages", "Menu", "Scene1.bin")
LOGOS_SCENE1 = os.path.join(EXTRACTED, "Data", "Stages", "Logos", "Scene1.bin")

# Menu / Logos directory decomp file list. Discovered via
# `gh api repos/RSDKModding/Sonic-Mania-Decompilation/contents/SonicMania/Objects/Menu`
# on 2026-05-28. The pre-cached LogoSetup, MenuSetup, and PauseMenu remain.
MENU_DECOMP_FILES = [
    "SonicMania_Objects_Menu_LogoSetup.c",
    "SonicMania_Objects_Menu_MenuSetup.c",
    "SonicMania_Objects_Menu_CompetitionMenu.c",
    "SonicMania_Objects_Menu_DAControl.c",
    "SonicMania_Objects_Menu_DASetup.c",
    "SonicMania_Objects_Menu_DemoMenu.c",
    "SonicMania_Objects_Menu_E3MenuSetup.c",
    "SonicMania_Objects_Menu_ExtrasMenu.c",
    "SonicMania_Objects_Menu_LevelSelect.c",
    "SonicMania_Objects_Menu_MainMenu.c",
    "SonicMania_Objects_Menu_ManiaModeMenu.c",
    "SonicMania_Objects_Menu_MenuParam.c",
    "SonicMania_Objects_Menu_OptionsMenu.c",
    "SonicMania_Objects_Menu_ThanksSetup.c",
    "SonicMania_Objects_Menu_TimeAttackMenu.c",
    "SonicMania_Objects_Menu_UIBackground.c",
    "SonicMania_Objects_Menu_UIButton.c",
    "SonicMania_Objects_Menu_UIButtonLabel.c",
    "SonicMania_Objects_Menu_UIButtonPrompt.c",
    "SonicMania_Objects_Menu_UICarousel.c",
    "SonicMania_Objects_Menu_UICharButton.c",
    "SonicMania_Objects_Menu_UIChoice.c",
    "SonicMania_Objects_Menu_UIControl.c",
    "SonicMania_Objects_Menu_UICreditsText.c",
    "SonicMania_Objects_Menu_UIDialog.c",
    "SonicMania_Objects_Menu_UIDiorama.c",
    "SonicMania_Objects_Menu_UIHeading.c",
    "SonicMania_Objects_Menu_UIInfoLabel.c",
    "SonicMania_Objects_Menu_UIKeyBinder.c",
    "SonicMania_Objects_Menu_UILeaderboard.c",
    "SonicMania_Objects_Menu_UIMedallionPanel.c",
    "SonicMania_Objects_Menu_UIModeButton.c",
    "SonicMania_Objects_Menu_UIOptionPanel.c",
    "SonicMania_Objects_Menu_UIPicture.c",
    "SonicMania_Objects_Menu_UIPopover.c",
    "SonicMania_Objects_Menu_UIRankButton.c",
    "SonicMania_Objects_Menu_UIReplayCarousel.c",
    "SonicMania_Objects_Menu_UIResPicker.c",
    "SonicMania_Objects_Menu_UISaveSlot.c",
    "SonicMania_Objects_Menu_UIShifter.c",
    "SonicMania_Objects_Menu_UISlider.c",
    "SonicMania_Objects_Menu_UISubHeading.c",
    "SonicMania_Objects_Menu_UITABanner.c",
    "SonicMania_Objects_Menu_UITAZoneModule.c",
    "SonicMania_Objects_Menu_UIText.c",
    "SonicMania_Objects_Menu_UITransition.c",
    "SonicMania_Objects_Menu_UIUsernamePopup.c",
    "SonicMania_Objects_Menu_UIVideo.c",
    "SonicMania_Objects_Menu_UIVsCharSelector.c",
    "SonicMania_Objects_Menu_UIVsResults.c",
    "SonicMania_Objects_Menu_UIVsRoundPicker.c",
    "SonicMania_Objects_Menu_UIVsScoreboard.c",
    "SonicMania_Objects_Menu_UIVsZoneButton.c",
    "SonicMania_Objects_Menu_UIWaitSpinner.c",
    "SonicMania_Objects_Menu_UIWidgets.c",
    "SonicMania_Objects_Menu_UIWinSize.c",
]

# Patterns mapping call site -> Data/ subdirectory.
PATTERNS = [
    (r'RSDK\.LoadSpriteAnimation\s*\(\s*"([^"]+)"', "Data/Sprites/{}"),
    (r'RSDK\.LoadSpriteSheet\s*\(\s*"([^"]+)"',     "Data/Sprites/{}"),
    (r'RSDK\.LoadStringList\s*\(\s*&[^,]+,\s*"([^"]+)"', "Data/Strings/{}"),
    (r'RSDK\.LoadVideo\s*\(\s*"([^"]+)"',           "Data/Video/{}"),
    (r'RSDK\.PlayStream\s*\(\s*"([^"]+)"',          "Data/Music/{}"),
    (r'RSDK\.GetSfx\s*\(\s*"([^"]+)"',              "Data/SoundFX/{}"),
    (r'Music_SetMusicTrack\s*\(\s*"([^"]+)"',       "Data/Music/{}"),
]

# Plus-DLC + REGION_JP-only assets that are skipped on the retail Saturn build
# (REV01, NTSC-U/EU). Track them as INFO not FAIL.
PLUS_ONLY = {
    "Data/Sprites/Players/Mighty.bin",   # UIDiorama.c:109 (Plus DLC)
    "Data/Sprites/Players/Ray.bin",      # UIDiorama.c:110 (Plus DLC)
    "Data/Image/CESA.png",               # LogoSetup.c:87 (Plus + JP region)
    "Data/Image/CESA.tga",               # LogoSetup.c:89 (JP region)
    # The two below were verified absent from the REV01 retail Data.rsdk
    # via direct hash lookup (both lowercase and uppercase, raw and
    # wordswapped MD5 -- no match in 1677 hashes). UIDiorama is the
    # Save Select character-preview-with-cutscene-stage widget; its
    # full diorama and AIZ Encore boss capsule were a Plus DLC addition.
    "Data/Sprites/UI/Diorama.bin",
    "Data/Sprites/AIZ/SchrodingersCapsule.bin",
}

# Files that the scene-anchor default-set probes for but which are genuinely
# absent from the retail Data.rsdk (verified via direct hash lookup against
# the datapack header). The Menu + Logos scenes are UI-only with no playable
# collision -- TileConfig.bin is absent for both.
EXPECTED_ABSENT = {
    # TitleConfig is in the Title-scene gate's EXPECTED_ABSENT; Menu/Logos
    # share the property -- UI-only, no tile collision.
}

# Editor-only references whose surrounding function is *_EditorLoad or
# *_EditorDraw and not part of the shipping port.
EDITOR_ONLY_PATHS = {
    "Data/Sprites/Editor/EditorIcons.bin",
}


def parse_spr_sheets(path):
    """Parse RSDKv5 SPR header and return its sheet list (GIF relative paths
    under Data/Sprites/)."""
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"SPR\x00":
        return []
    p = 4
    _frame_count = struct.unpack_from("<I", d, p)[0]
    p += 4
    sheet_count = d[p]
    p += 1
    sheets = []
    for _ in range(sheet_count):
        n = d[p]
        p += 1
        s = d[p:p + n].decode("latin-1").rstrip("\x00")
        p += n
        sheets.append(s)
    return sheets


def collect_references():
    """Return (citations, paths). citations: list of (decomp_file, line_no,
    call_string, resolved_data_path). paths: set of resolved Data/ paths.

    Tracks #if/#endif depth of GAME_INCLUDE_EDITOR blocks so editor-only
    asset references (EditorIcons.bin etc.) are excluded -- the Saturn port
    does not ship the editor."""
    citations = []
    paths = set()
    for fname in MENU_DECOMP_FILES:
        fpath = os.path.join(DECOMP_DIR, fname)
        if not os.path.isfile(fpath):
            print(f"WARNING: missing decomp cache {fname}", file=sys.stderr)
            continue
        with open(fpath, "r", encoding="latin-1") as f:
            in_editor = 0  # nesting depth of GAME_INCLUDE_EDITOR
            for lineno, line in enumerate(f, 1):
                stripped = line.strip()
                if re.match(r'#if\s+GAME_INCLUDE_EDITOR\b', stripped):
                    in_editor += 1
                    continue
                if in_editor > 0:
                    if stripped.startswith("#if"):
                        in_editor += 1
                    elif stripped.startswith("#endif"):
                        in_editor -= 1
                    continue
                for pat, tmpl in PATTERNS:
                    for m in re.finditer(pat, line):
                        arg = m.group(1)
                        resolved = tmpl.format(arg)
                        if resolved in EDITOR_ONLY_PATHS:
                            continue
                        citations.append((fname, lineno, m.group(0), resolved))
                        paths.add(resolved)
    return citations, paths


def collect_scene_music_tracks(scene_path):
    """Parse a Scene1.bin and return list of (slot, trackFile) for Music
    entities. Uses parse_title_entities.parse_entities()."""
    if not os.path.isfile(scene_path):
        return []
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    try:
        from parse_title_entities import parse_entities
    except ImportError:
        return []
    try:
        objects, _, _ = parse_entities(scene_path)
    except Exception as e:
        print(f"WARNING: parse_entities({scene_path}) failed: {e}",
              file=sys.stderr)
        return []
    tracks = []
    for obj in objects:
        if obj["name"] != "Music":
            continue
        # Find the index of the trackFile attribute.
        track_idx = None
        for i, (aname, _atype) in enumerate(obj["attribs"]):
            if aname == "trackFile":
                track_idx = i
                break
        if track_idx is None:
            continue
        for ent in obj["entities"]:
            try:
                _aname, _label, val = ent["attrs"][track_idx]
                if isinstance(val, str) and val:
                    tracks.append((ent["slot"], val))
            except (IndexError, KeyError):
                continue
    return tracks


def derive_sheet_paths(sprite_paths):
    """For every Data/Sprites/X.bin path that exists, parse the .bin and
    return the set of additional GIF sheet paths it references."""
    extra = set()
    for p in sprite_paths:
        if not p.endswith(".bin"):
            continue
        abspath = os.path.join(EXTRACTED, p)
        if not os.path.isfile(abspath):
            continue
        try:
            sheets = parse_spr_sheets(abspath)
        except Exception:
            continue
        for s in sheets:
            extra.add(f"Data/Sprites/{s}")
    return extra


def check_present(paths):
    present, missing = [], []
    for p in sorted(paths):
        abspath = os.path.join(EXTRACTED, p)
        if os.path.isfile(abspath) and os.path.getsize(abspath) > 0:
            present.append((p, os.path.getsize(abspath)))
        else:
            missing.append(p)
    return present, missing


def main():
    citations, paths = collect_references()

    # Also include the Menu + Logos scene anchor files.
    paths.update([
        "Data/Stages/Menu/Scene1.bin",
        "Data/Stages/Menu/StageConfig.bin",
        "Data/Stages/Menu/16x16Tiles.gif",
        "Data/Stages/Logos/Scene1.bin",
        "Data/Stages/Logos/StageConfig.bin",
        "Data/Stages/Logos/16x16Tiles.gif",
    ])

    # Parse Menu/Scene1.bin Music entities for trackFile attrs.
    menu_tracks = collect_scene_music_tracks(MENU_SCENE1)
    logos_tracks = collect_scene_music_tracks(LOGOS_SCENE1)
    track_citations = []
    for slot, trackfile in menu_tracks:
        resolved = f"Data/Music/{trackfile}"
        paths.add(resolved)
        track_citations.append(("Menu/Scene1.bin", slot, "trackFile attr",
                                resolved))
    for slot, trackfile in logos_tracks:
        resolved = f"Data/Music/{trackfile}"
        paths.add(resolved)
        track_citations.append(("Logos/Scene1.bin", slot, "trackFile attr",
                                resolved))

    sprite_paths = {p for p in paths if p.startswith("Data/Sprites/")
                    and p.endswith(".bin")}
    extra_sheets = derive_sheet_paths(sprite_paths)
    paths.update(extra_sheets)

    present, missing = check_present(paths)

    fail = [m for m in missing if m not in PLUS_ONLY
            and m not in EXPECTED_ABSENT]
    info = [m for m in missing if m in PLUS_ONLY or m in EXPECTED_ABSENT]

    print(f"Menu/Logos-scene asset coverage gate (Phase 3.0-prep)")
    print(f"  Decomp files scanned:     {len(MENU_DECOMP_FILES)}")
    print(f"  RSDK call sites:          {len(citations)}")
    print(f"  Scene1.bin trackFile attrs: {len(track_citations)}")
    print(f"  Unique asset paths:       {len(paths)}")
    print(f"  Present (retail):         {len(present)}")
    print(f"  Plus-DLC / region INFO:   {len(info)}")
    print(f"  Retail missing FAIL:      {len(fail)}")

    if info:
        print("\nPlus-DLC / region-conditional assets absent (informational, "
              "NOT a failure):")
        for p in info:
            print(f"  INFO: {p}")

    if fail:
        print("\nRETAIL ASSETS MISSING (Phase 3.0-prep gate FAIL):")
        all_cits = citations + track_citations
        for p in fail:
            for fname, lineno, callstr, resolved in all_cits:
                if resolved == p:
                    print(f"  MISS: {p}   <-  {fname}:{lineno}  {callstr}")
                    break
            else:
                print(f"  MISS: {p}   <-  derived sheet or scene anchor")
        return 1

    print("\nGATE GREEN: every Menu/Logos-scene retail asset is present.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
