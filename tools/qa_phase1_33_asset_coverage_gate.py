#!/usr/bin/env python3
"""
qa_phase1_33_asset_coverage_gate.py - Asset-coverage gate for Title scene.

Predicate: for every asset path referenced by the Title-scene decomp files
in tools/_decomp_raw/SonicMania_Objects_Title_*.c (plus globals Music.c +
Localization.c when their StageLoad runs on the Title scene), the file MUST
exist in extracted/Data/<path> with size > 0.

Methodology (Phase 1.33, BINDING):
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
  3. Print PASS / FAIL with the missing-list.

Run: python tools/qa_phase1_33_asset_coverage_gate.py
Exit code: 0 GREEN; 1 RED (with missing paths in stdout).
"""
import os
import re
import struct
import sys


REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DECOMP_DIR = os.path.join(REPO_ROOT, "tools", "_decomp_raw")
EXTRACTED = os.path.join(REPO_ROOT, "extracted")

# Title-scene direct files + globals that have StageLoad effects on Title.
TITLE_DECOMP_FILES = [
    "SonicMania_Objects_Title_TitleSetup.c",
    "SonicMania_Objects_Title_TitleLogo.c",
    "SonicMania_Objects_Title_TitleSonic.c",
    "SonicMania_Objects_Title_TitleBG.c",
    "SonicMania_Objects_Title_Title3DSprite.c",
    "SonicMania_Objects_Global_Music.c",
    "SonicMania_Objects_Global_Localization.c",
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

# Plus-only assets that are skipped on the retail Mania build (the user's
# Data.rsdk is REV01, no Plus DLC). Track them as INFO not FAIL.
PLUS_ONLY = {
    "Data/Sprites/Title/PlusLogo.bin",
    "Data/Sprites/Title/PlusLogo.gif",
    "Data/SoundFX/Stage/Plus.wav",
}

# Files that the scene-anchor default-set probes for but which are genuinely
# absent from the retail Data.rsdk (verified via direct hash lookup against
# the datapack header). The Title scene is a UI scene with no playable
# collision -- the engine handles a missing TileConfig.bin gracefully by
# treating every tile as non-solid.
EXPECTED_ABSENT = {
    "Data/Stages/Title/TileConfig.bin",
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
    for fname in TITLE_DECOMP_FILES:
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
                        citations.append((fname, lineno, m.group(0), resolved))
                        paths.add(resolved)
    return citations, paths


def derive_sheet_paths(sprite_paths):
    """For every Data/Sprites/X.bin path that exists, parse the .bin and
    return the set of additional GIF sheet paths it references."""
    extra = set()
    for p in sprite_paths:
        if not p.endswith(".bin"):
            continue
        abspath = os.path.join(EXTRACTED, p)
        if not os.path.isfile(abspath):
            # Can't inspect a missing .bin; the .bin itself will be reported
            # missing separately.
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

    # Also include the Title scene's own scene anchor files (these are always
    # extracted via the per-folder block in build_filelist.py, but a gate
    # should be explicit).
    paths.update([
        "Data/Stages/Title/Scene1.bin",
        "Data/Stages/Title/StageConfig.bin",
        "Data/Stages/Title/TileConfig.bin",
        "Data/Stages/Title/16x16Tiles.gif",
    ])

    sprite_paths = {p for p in paths if p.startswith("Data/Sprites/")
                    and p.endswith(".bin")}
    extra_sheets = derive_sheet_paths(sprite_paths)
    paths.update(extra_sheets)

    present, missing = check_present(paths)

    # Split missing into FAIL (retail) vs INFO (Plus-only DLC or
    # documented-absent scene anchors).
    fail = [m for m in missing if m not in PLUS_ONLY
            and m not in EXPECTED_ABSENT]
    info = [m for m in missing if m in PLUS_ONLY or m in EXPECTED_ABSENT]

    print(f"Title-scene asset coverage gate (Phase 1.33)")
    print(f"  Decomp files scanned: {len(TITLE_DECOMP_FILES)}")
    print(f"  RSDK call sites:      {len(citations)}")
    print(f"  Unique asset paths:   {len(paths)}")
    print(f"  Present (retail):     {len(present)}")
    print(f"  Plus-DLC missing INFO:{len(info)}")
    print(f"  Retail missing FAIL:  {len(fail)}")

    if info:
        print("\nPlus-DLC assets absent from REV01 retail Data.rsdk "
              "(informational, NOT a failure):")
        for p in info:
            print(f"  INFO: {p}")

    if fail:
        print("\nRETAIL ASSETS MISSING (Phase 1.33 gate FAIL):")
        for p in fail:
            # Find any citation that references this path.
            for fname, lineno, callstr, resolved in citations:
                if resolved == p:
                    print(f"  MISS: {p}   <-  {fname}:{lineno}  {callstr}")
                    break
            else:
                print(f"  MISS: {p}   <-  derived sheet or scene anchor")
        return 1

    print("\nGATE GREEN: every Title-scene retail asset is present.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
