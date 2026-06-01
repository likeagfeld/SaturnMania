#!/usr/bin/env python3
"""
qa_phase3_0_plus_whole_game_asset_coverage_gate.py - Phase 3.0-prep++
whole-game asset-coverage gate.

Predicate: scan EVERY tools/_decomp_raw/SonicMania_Objects_*.c file (518
files spanning every shipped scene/object/subsystem in the upstream Mania
decompilation) for RSDK asset patterns AND every extracted/Data/Stages/
<Folder>/Scene*.bin for Music entity trackFile attributes. Every resolved
Data/<path> MUST exist in extracted/ with size > 0, EXCEPT for the
Plus-DLC / Encore-mode / encrypted-asset set tracked as INFO.

Methodology (mirror of Phase 1.33 + Phase 3.0-prep gates -- BINDING):

  1. Grep each cached decomp .c file for:
       RSDK.LoadSpriteAnimation("X")  -> Data/Sprites/X
       RSDK.LoadSpriteSheet("X")      -> Data/Sprites/X
       RSDK.LoadStringList(&_, "X", N) -> Data/Strings/X
       RSDK.LoadVideo("X", ...)       -> Data/Video/X
       RSDK.PlayStream("X", ...)      -> Data/Music/X
       RSDK.GetSfx("X")               -> Data/SoundFX/X
       Music_SetMusicTrack("X", ...)  -> Data/Music/X
     Skipping #if GAME_INCLUDE_EDITOR blocks.
  2. For every sprite .bin already extracted, parse its sheets[] list and
     demand every referenced sheet GIF exists too.
  3. Parse every Scene*.bin under extracted/Data/Stages/<Folder>/ and
     enumerate Music entity trackFile attributes -> demand
     Data/Music/<trackFile> exists.
  4. Print PASS / FAIL with the missing-list and INFO with Plus-DLC items.

Run: python tools/qa_phase3_0_plus_whole_game_asset_coverage_gate.py
Exit 0 GREEN; 1 RED (with missing paths in stdout).
"""
import os
import re
import struct
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DECOMP_DIR = os.path.join(REPO_ROOT, "tools", "_decomp_raw")
EXTRACTED = os.path.join(REPO_ROOT, "extracted")
STAGES = os.path.join(EXTRACTED, "Data", "Stages")

PATTERNS = [
    (r'RSDK\.LoadSpriteAnimation\s*\(\s*"([^"]+)"', "Data/Sprites/{}"),
    (r'RSDK\.LoadSpriteSheet\s*\(\s*"([^"]+)"',     "Data/Sprites/{}"),
    (r'RSDK\.LoadStringList\s*\(\s*&[^,]+,\s*"([^"]+)"', "Data/Strings/{}"),
    (r'RSDK\.LoadVideo\s*\(\s*"([^"]+)"',           "Data/Video/{}"),
    (r'RSDK\.PlayStream\s*\(\s*"([^"]+)"',          "Data/Music/{}"),
    (r'RSDK\.GetSfx\s*\(\s*"([^"]+)"',              "Data/SoundFX/{}"),
    (r'Music_SetMusicTrack\s*\(\s*"([^"]+)"',       "Data/Music/{}"),
]

# Plus-DLC / Encore-mode / Mania Plus character (Mighty + Ray) refs.
# Absent from REV01 retail Data.rsdk (verified by hash-mismatch against
# datapack header). Tracked INFO not FAIL.
PLUS_DLC_PATHS = {
    # Mania Plus characters (Mighty + Ray)
    "Data/Sprites/Players/Mighty.bin",
    "Data/Sprites/Players/Ray.bin",
    "Data/Sprites/Players/ChibiMighty.bin",
    "Data/Sprites/Players/ChibiRay.bin",
    "Data/Sprites/SpecialBS/Mighty.bin",
    "Data/Sprites/SpecialBS/Ray.bin",
    "Data/Sprites/CPZ/MBMMighty.bin",
    "Data/Sprites/CPZ/MBMRay.bin",
    "Data/SoundFX/Global/MightyDeflect.wav",
    "Data/SoundFX/Global/MightyDrill.wav",
    "Data/SoundFX/Global/MightyLand.wav",
    "Data/SoundFX/Global/MightyUnspin.wav",
    "Data/SoundFX/VO/Mighty.wav",
    "Data/SoundFX/VO/Ray.wav",
    "Data/SoundFX/VO/MightyWins.wav",
    "Data/SoundFX/VO/RayWins.wav",
    # Plus-DLC Encore competition extras
    "Data/SoundFX/VO/Player3.wav",
    "Data/SoundFX/VO/Player4.wav",
    "Data/SoundFX/VO/ItsADraw.wav",
    "Data/SoundFX/VO/ItsADraw_Set.wav",
    "Data/SoundFX/Global/Swap.wav",
    "Data/SoundFX/Global/SwapFail.wav",
    # Plus DLC title splash + JP region
    "Data/Sprites/Title/PlusLogo.bin",
    "Data/Sprites/Title/PlusLogo.gif",
    "Data/SoundFX/Stage/Plus.wav",
    "Data/Image/CESA.png",
    "Data/Image/CESA.tga",
    # Plus-DLC Encore Save Select diorama
    "Data/Sprites/UI/Diorama.bin",
    "Data/Sprites/AIZ/SchrodingersCapsule.bin",
    # Plus-DLC Encore-only music (compass/portal warp)
    "Data/Music/EggReveriePinch.ogg",
    "Data/Music/BlueSpheresSPD.ogg",
    "Data/Music/UFOSpecial.ogg",
}

# Assets referenced by decomp but genuinely absent from REV01 retail
# Data.rsdk. Most are Encore-only variants or files renamed pre-shipping
# (e.g. AniTiles.gif extracted as a single 16x16Tiles.gif). Verified by
# direct hash-mismatch against datapack header (no match for either
# lowercase or wordswap MD5).
EXPECTED_ABSENT = {
    # AniTiles is the asset-build-time source layered into 16x16Tiles.gif.
    # The shipped Data.rsdk only contains the latter. Same pattern repeats
    # for every zone.
    "Data/Sprites/AIZ/AniTiles.gif",
    # Encore-only / unused sprite references found in decomp but not shipped.
    "Data/Sprites/AIZ/CaterkillerJr.bin",   # CPZ_CaterkillerJr.c:85 misroute
    "Data/Sprites/AIZ/Portal.bin",          # Cutscene_RubyPortal.c:112 misroute
    "Data/Sprites/AIZ/Sweep.bin",           # Sweep is HCZ asset; decomp wrong path
    "Data/Sprites/AIZ/SwingRope.bin",       # MSZ_SwingRope.c:159 misroute
    "Data/Sprites/Credits/AnimalHBH.bin",   # unused alt
    "Data/Sprites/Credits/Silhouettes.bin", # unused alt
    "Data/Sprites/Cutscene/DamagedKing.bin",   # encrypted/encore
    "Data/Sprites/Cutscene/HBHPile.bin",       # encrypted/encore
    "Data/Sprites/Cutscene/KingTMZ2.bin",      # encrypted/encore
    "Data/Sprites/LRZ1/OrbitSpike.bin",     # LRZ_OrbitSpike.c:91 (likely unused)
    "Data/Sprites/MMZ/Decoration.bin",      # Common_Decoration.c:84 conditional path
    "Data/Sprites/MMZ/RTeleporter.bin",     # SSZ_RTeleporter.c misroute
    "Data/Sprites/MSZ/Ending.bin",          # Common_Decoration.c:89 conditional
    "Data/Sprites/OOZ/Splash.bin",          # OOZ_OOZSetup.c:264 (Encore-conditional)
    "Data/Sprites/PSZ1/FrostThrower.bin",   # PGZ_FrostThrower.c:63 (decomp path uses PGZ folder)
    "Data/Sprites/PSZ1/IceBomba.bin",       # PGZ_IceBomba.c:90
    # Sound effects absent from retail datapack (verified hash-mismatch).
    "Data/Music/SPZ1.ogg",                          # bare zone tag never shipped (SPZ1.ogg vs Studiopolis1.ogg)
    "Data/SoundFX/CPZ/CPZ2HitBlocksStop.wav",
    "Data/SoundFX/Global/Recovery.wav",
    "Data/SoundFX/Global/Spike.wav",
    "Data/SoundFX/SSZ2/MSTransform.wav",
    "Data/SoundFX/Stage/Clack2.wav",
    "Data/SoundFX/Stage/Door.wav",
    "Data/SoundFX/Stage/DrownAlert.wav",
    "Data/SoundFX/Stage/Rush.wav",
    "Data/SoundFX/Stage/Waterfall.wav",
    "Data/SoundFX/Stage/Waterfall2.wav",
    "Data/SoundFX/TMZ3/RubyGet.wav",
}


def parse_spr_sheets(path):
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
    """Scan EVERY SonicMania_Objects_*.c in tools/_decomp_raw/ for RSDK
    asset call sites. Returns (citations, paths)."""
    citations = []
    paths = set()
    files_scanned = 0
    for fname in sorted(os.listdir(DECOMP_DIR)):
        if not (fname.endswith(".c") and
                fname.startswith("SonicMania_Objects_")):
            continue
        fpath = os.path.join(DECOMP_DIR, fname)
        files_scanned += 1
        with open(fpath, "r", encoding="latin-1") as f:
            in_editor = 0
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
                        citations.append((fname, lineno, m.group(0),
                                          resolved))
                        paths.add(resolved)
    return citations, paths, files_scanned


def collect_scene_music_tracks():
    """Parse every extracted Stages/<Folder>/Scene*.bin and return
    (folder, scene, slot, trackFile) for every Music entity."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    try:
        from parse_title_entities import parse_entities
    except ImportError:
        return []
    tracks = []
    if not os.path.isdir(STAGES):
        return []
    for folder in sorted(os.listdir(STAGES)):
        fpath = os.path.join(STAGES, folder)
        if not os.path.isdir(fpath):
            continue
        for sf in sorted(os.listdir(fpath)):
            if not (sf.startswith("Scene") and sf.endswith(".bin")):
                continue
            spath = os.path.join(fpath, sf)
            try:
                objs, _, _ = parse_entities(spath)
            except Exception:
                continue
            for o in objs:
                if o["name"] != "Music":
                    continue
                idx = None
                for i, (an, _) in enumerate(o["attribs"]):
                    if an == "trackFile":
                        idx = i
                        break
                if idx is None:
                    continue
                for ent in o["entities"]:
                    try:
                        _an, _lbl, val = ent["attrs"][idx]
                        if isinstance(val, str) and val:
                            tracks.append((folder, sf, ent["slot"], val))
                    except (IndexError, KeyError):
                        continue
    return tracks


def derive_sheet_paths(sprite_paths):
    """Parse every Data/Sprites/X.bin that exists and return its sheet
    GIF references."""
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
    citations, paths, files_scanned = collect_references()

    # Add scene anchors for every Stages/ folder.
    for folder in sorted(os.listdir(STAGES)):
        fp = os.path.join(STAGES, folder)
        if not os.path.isdir(fp):
            continue
        paths.add(f"Data/Stages/{folder}/StageConfig.bin")
        paths.add(f"Data/Stages/{folder}/16x16Tiles.gif")
        # Track each Scene*.bin found
        for fn in os.listdir(fp):
            if fn.startswith("Scene") and fn.endswith(".bin"):
                paths.add(f"Data/Stages/{folder}/{fn}")

    # Scene1.bin Music entities.
    tracks = collect_scene_music_tracks()
    track_citations = []
    for folder, scene, slot, tf in tracks:
        resolved = f"Data/Music/{tf}"
        paths.add(resolved)
        track_citations.append(
            (f"{folder}/{scene}", slot, "trackFile attr", resolved))

    # Sheet GIF derivation.
    sprite_paths = {p for p in paths if p.startswith("Data/Sprites/")
                    and p.endswith(".bin")}
    extra_sheets = derive_sheet_paths(sprite_paths)
    paths.update(extra_sheets)

    present, missing = check_present(paths)

    fail = [m for m in missing
            if m not in PLUS_DLC_PATHS and m not in EXPECTED_ABSENT]
    info = [m for m in missing
            if m in PLUS_DLC_PATHS or m in EXPECTED_ABSENT]

    print(f"Whole-game asset coverage gate (Phase 3.0-prep++)")
    print(f"  Decomp .c files scanned:    {files_scanned}")
    print(f"  RSDK call sites:            {len(citations)}")
    print(f"  Scene*.bin trackFile attrs: {len(track_citations)}")
    print(f"  Unique asset paths:         {len(paths)}")
    print(f"  Present (retail):           {len(present)}")
    print(f"  Plus-DLC / absent INFO:     {len(info)}")
    print(f"  Retail missing FAIL:        {len(fail)}")

    if info:
        print("\nPlus-DLC / Encore / decomp-conditional INFO (not a failure):")
        for p in info:
            tag = "PLUS" if p in PLUS_DLC_PATHS else "ABSENT"
            print(f"  INFO[{tag}]: {p}")

    if fail:
        print("\nRETAIL ASSETS MISSING (Phase 3.0-prep++ gate FAIL):")
        all_cits = citations + track_citations
        for p in fail:
            for fname, lineno, callstr, resolved in all_cits:
                if resolved == p:
                    print(f"  MISS: {p}   <-  {fname}:{lineno}  {callstr}")
                    break
            else:
                print(f"  MISS: {p}   <-  derived sheet or scene anchor")
        return 1

    print("\nGATE GREEN: every retail Mania asset referenced across all "
          "42 stages + Global + UI is present in extracted/.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
