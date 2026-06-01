#!/usr/bin/env python3
"""
build_filelist.py - Generate a comprehensive RSDKv5 datapack candidate filelist
for Sonic Mania's Data.rsdk.

The RSDK cipher requires the real path to decrypt a file, and stored hashes are
wordswap(MD5(lowercase(path))). To recover everything in the datapack we need
to enumerate every plausible path; rsdk_extract.py then hash-matches them.

METHODOLOGY (Phase 1.33+ established, Phase 3.0-prep confirmed transferable;
BINDING for every future scene port)
----------------------------------------------------------
When porting a new scene, do NOT add paths manually by guessing. Instead:
  1. Cache every <Scene>Setup.c / <SceneObject>.c file referenced by the
     scene's Scene1.bin (parse hashes via tools/parse_title_entities.py
     style scripts) under tools/_decomp_raw/. For UI/menu-style scenes
     where StageConfig.bin lists dozens of UI* classes, batch-fetch the
     full Objects/<Cat>/ directory listing first
     (`gh api repos/RSDKModding/Sonic-Mania-Decompilation/contents/SonicMania/Objects/<Cat>?ref=master`)
     and pull every .c file in one loop.
  2. For every cached .c file, grep for the following RSDK calls and
     collect their string arguments verbatim (relative to Data/):
       RSDK.LoadSpriteAnimation("X")  -> Data/Sprites/X
       RSDK.LoadSpriteSheet("X")      -> Data/Sprites/X
       RSDK.LoadStage(...)            -> Data/Stages/<folder>/
       RSDK.LoadStringList("X", N)    -> Data/Strings/X
       RSDK.LoadVideo("X", ...)       -> Data/Video/X
       RSDK.PlayStream("X", ...)      -> Data/Music/X
       RSDK.GetSfx("X")               -> Data/SoundFX/X
       Music_SetMusicTrack("X", ...)  -> Data/Music/X
       RSDK.SetScene("Cat", "Name")   -> next scene folder (cross-ref
                                        GameConfig category list)
  3. For each sprite .bin discovered, parse its sheets list (parse_spr in
     tools/parse_title_entities.py style) and add every sheet path too
     (Background.bin -> Title/BG.gif, etc.).
  4. For scenes that use Music entities (parse Scene1.bin entity list):
     extract the `trackFile` string attribute from each Music entity and
     add Data/Music/<trackFile> too. The MenuSetup_ChangeMenuTrack pattern
     uses trackID 0..3 keyed to per-mode Music entities (MainMenu.ogg /
     Competition.ogg / Results.ogg / SaveSelect.ogg in the Menu scene) --
     these are NOT in any decomp .c grep, only in Scene1.bin attrs.
  5. Add the full set to paths.add(...) in the per-scene block below with
     a comment citing the decomp file:line.
  6. After running this script + rsdk_extract.py, run the per-scene
     coverage gate (tools/qa_phase1_33_asset_coverage_gate.py for Title,
     tools/qa_phase3_0_menu_asset_coverage_gate.py for Menu/Logos, ...)
     to confirm every referenced asset is now present.

This methodology was reinforced 2026-05-27 (Phase 1.33, Title scene -- 15
newly-extracted assets including IntroTee/IntroHP music + 9 language
string lists) and reapplied 2026-05-28 (Phase 3.0-prep, Menu/Logos
scenes -- 54 Menu .c files batch-fetched, 42 retail asset paths
catalogued, 4 trackFile attrs unrecoverable from .c grep alone). The
Scene1.bin trackFile-attribute step in (4) was added in Phase 3.0-prep
after observing that MenuSetup.c calls Music_PlayTrack(trackID) where
trackID is a small int and the .ogg names are stored only in Scene1.bin
Music entity attributes.

Sources of candidates (all driven by what the user's own extracted
GameConfig.bin / per-stage StageConfig.bin actually reference):

  * GameConfig.bin -> objects[]   -> Data/Sprites/<folder>/<class>.bin
  * GameConfig.bin -> sfx[]       -> Data/SoundFX/<name>
  * Per-stage StageConfig.bin -> objects[] / sfx[] (same conventions)
  * Each stage folder's standard files (Scene*.bin / 16x16Tiles.gif /
    TileConfig.bin / StageConfig.bin)
  * Functional music name patterns (Stage / Boss / MiniBoss / Invincible /
    Speed / Drowning / ActClear / GameOver / Continue / Title / Menu / Bonus /
    Special / BlueSphere / 1up / Credits / Ending / Emerald / SuperTheme) AND
    per-folder patterns (<Folder>.ogg, <Folder>Act1.ogg, <Folder>Act2.ogg).

The script writes one path per line to the chosen output file. Paths are
emitted with forward slashes; rsdk_extract.py lowercases at hash time.

Usage:
  python tools/build_filelist.py \
      --extracted extracted \
      --out      tools/maniafilelist.txt
"""

import argparse
import os
import struct
import sys

# RSDKv5 reader -------------------------------------------------------------

class _R:
    """Tiny LE-bytewise reader (matches RSDKv5 RetroEngine.cpp::ReadString etc)."""
    def __init__(self, data):
        self.d = data
        self.p = 0
    def u8(self):
        v = self.d[self.p]; self.p += 1; return v
    def u16(self):
        v = struct.unpack_from("<H", self.d, self.p)[0]; self.p += 2; return v
    def u32(self):
        v = struct.unpack_from("<I", self.d, self.p)[0]; self.p += 4; return v
    def s(self):
        n = self.u8()
        v = self.d[self.p:self.p + n].decode("latin-1"); self.p += n
        return v

PALETTE_BANK_COUNT = 8

# Parsers -------------------------------------------------------------------

def parse_gameconfig(data, rev02):
    """Returns (objects[], sfx[], categories[{folder,id}])."""
    if data[:4] != b"CFG\x00":
        raise ValueError("not a GameConfig.bin")
    r = _R(data); r.p = 4
    title, subtitle, version = r.s(), r.s(), r.s()
    _active = r.u8(); _start = r.u16()
    obj_cnt = r.u8()
    objects = [r.s() for _ in range(obj_cnt)]
    # Palette banks.
    for _ in range(PALETTE_BANK_COUNT):
        rows = r.u16()
        for bit in range(16):
            if (rows >> bit) & 1:
                r.p += 16 * 3
    sfx_cnt = r.u8()
    sfx = []
    for _ in range(sfx_cnt):
        name = r.s()
        r.u8()                          # maxConcurrentPlays
        sfx.append(name)
    total = r.u16()
    cat_cnt = r.u8()
    cats = []
    for _ in range(cat_cnt):
        cname = r.s()
        sc_cnt = r.u8()
        for _ in range(sc_cnt):
            sname = r.s()
            folder = r.s()
            sid    = r.s()
            if rev02:
                r.u8()                  # filter byte
            cats.append({"folder": folder, "id": sid})
    return objects, sfx, cats


def parse_stageconfig(data):
    """Returns (objects[], sfx[]) — matches Scene.cpp LoadSceneAssets."""
    if data[:4] != b"CFG\x00":
        raise ValueError("not a StageConfig.bin")
    r = _R(data); r.p = 4
    r.u8()                               # useGlobalObjects
    obj_cnt = r.u8()
    objects = [r.s() for _ in range(obj_cnt)]
    for _ in range(PALETTE_BANK_COUNT):
        rows = r.u16()
        for bit in range(16):
            if (rows >> bit) & 1:
                r.p += 16 * 3
    sfx_cnt = r.u8()
    sfx = []
    for _ in range(sfx_cnt):
        name = r.s()
        r.u8()
        sfx.append(name)
    return objects, sfx

# Candidate generation ------------------------------------------------------

# Functional music slot names. These are role/state identifiers (what the
# engine uses a track for), enumerated so the hash matcher can prove which
# ones the user's datapack actually contains.
GENERIC_MUSIC_ROLES = [
    "Stage", "Boss", "MiniBoss", "MiniBoss2", "Invincible", "Speed",
    "Drowning", "Drown", "DrownWarn",
    "ActClear", "ActClear2", "Ending", "EndingShort",
    "GameOver", "Continue", "1up", "Bonus",
    "Title", "TitleScreen", "Menu", "SaveSelect", "Credits", "Special",
    "BlueSphere", "BlueSpheres", "BlueSphere1", "Bonus2",
    "SuperSonic", "SuperTheme", "Emerald", "Hidden",
    "Pause", "Logo", "Logos",
    "BossMini", "BossFinal", "MetalSonic", "EggReverie",
]

# Discovered (hash-confirmed against this datapack) named-zone music base
# words. Each gets `1.ogg` and `2.ogg` suffixes appended in the loop below.
NAMED_ZONE_MUSIC_BASES = [
    "GreenHill", "ChemicalPlant", "Studiopolis", "FlyingBattery",
    "StardustSpeedway", "Hydrocity", "MirageSaloon", "OilOcean",
    "LavaReef", "MetallicMadness", "TitanicMonarch", "BossEggman",
]

# Stage folders that contain music acts. Pattern variants are generated below.
PER_FOLDER_MUSIC_SUFFIXES = ["", "Act1", "Act2", "Act3", "Boss", "Mini", "End"]

# Known extension conventions for music in the RSDK datapack.
MUSIC_EXTS = ["ogg"]

# Standard per-folder asset filenames recognised by RetroEngine.cpp.
PER_FOLDER_FIXED = ["16x16Tiles.gif", "TileConfig.bin", "StageConfig.bin"]

# Sprite-data lives under Data/Sprites/<folder>/<class>.bin. Folders to probe.
DEFAULT_SPRITE_FOLDERS = [
    "Global", "Players", "Players2",
    # Stage folders (mirror what's already on disk in extracted/Data/Stages):
    "AIZ", "CPZ", "Credits", "DAGarden", "Ending", "ERZ", "FBZ", "GHZ",
    "GHZCutscene", "HCZ", "Logos", "LRZ1", "LRZ2", "LRZ3", "LSelect",
    "Menu", "MMZ", "MSZ", "MSZCutscene", "OOZ1", "OOZ2", "PSZ1", "PSZ2",
    "Puyo", "SpecialBS", "SPZ1", "SPZ2", "SSZ1", "SSZ2", "Thanks",
    "TimeTravel", "Title", "TMZ1", "TMZ2", "TMZ3",
    "UFO1", "UFO2", "UFO3", "UFO4", "UFO5", "UFO6", "UFO7",
    # Generic UI / HUD folders commonly present:
    "HUD", "UI",
    # Mania extras:
    "Bonus", "Special", "Pinball", "Continue", "GameOver",
    "Mania", "MSZCutscene",
]

# RSDK animation files live under Data/Sprites with several legal sub-paths.
# A class FooBar may have its animation under:
#   Data/Sprites/<folder>/<FooBar>.bin
#   Data/Sprites/<folder>/<lowercase>.bin
# Both are tried. The hash matcher silently drops misses.

def _candidate_sprite_paths(folder, class_name):
    yield f"Data/Sprites/{folder}/{class_name}.bin"
    # Some Mania assets store animations as lowercase or with sub-dirs.
    yield f"Data/Sprites/{folder}/{class_name.lower()}.bin"

def _candidate_sfx_paths(name):
    """SFX names in GameConfig already include the extension."""
    yield f"Data/SoundFX/{name}"
    # In case some references omit the extension.
    if "." not in name:
        for ext in ("wav", "ogg"):
            yield f"Data/SoundFX/{name}.{ext}"

def _candidate_music_paths(name, folders):
    for ext in MUSIC_EXTS:
        yield f"Data/Music/{name}.{ext}"
    for f in folders:
        for suffix in PER_FOLDER_MUSIC_SUFFIXES:
            for ext in MUSIC_EXTS:
                yield f"Data/Music/{f}{suffix}.{ext}"

# Driver --------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--extracted", default="extracted",
                    help="Path to the existing extracted/ directory (must "
                         "contain Data/Game/GameConfig.bin and "
                         "Data/Stages/*/StageConfig.bin).")
    ap.add_argument("--out", default="tools/maniafilelist.txt",
                    help="Output path for the candidate filelist.")
    args = ap.parse_args()

    extracted_root = os.path.abspath(args.extracted)
    gameconfig_path = os.path.join(extracted_root, "Data", "Game", "GameConfig.bin")
    if not os.path.isfile(gameconfig_path):
        sys.exit(f"missing {gameconfig_path}")

    with open(gameconfig_path, "rb") as f:
        gc_data = f.read()

    gc_objects, gc_sfx, gc_categories = None, None, None
    for rev02 in (False, True):
        try:
            o, s, c = parse_gameconfig(gc_data, rev02)
            if c and all(x["folder"].isprintable() and x["folder"] for x in c):
                gc_objects, gc_sfx, gc_categories = o, s, c
                print(f"[gameconfig] parsed as {'REV02' if rev02 else 'REV01'}")
                break
        except Exception:
            continue
    if gc_objects is None:
        sys.exit("could not parse GameConfig with either revision")
    print(f"[gameconfig] objects={len(gc_objects)} sfx={len(gc_sfx)} "
          f"scene-entries={len(gc_categories)}")

    # Discover all stage folders on disk + GameConfig folders.
    folders = set()
    for c in gc_categories:
        folders.add(c["folder"])
    stages_root = os.path.join(extracted_root, "Data", "Stages")
    if os.path.isdir(stages_root):
        for d in os.listdir(stages_root):
            if os.path.isdir(os.path.join(stages_root, d)):
                folders.add(d)

    # Per-folder fixed files + Scene1..Scene36 (Mania bonus stages go that high).
    per_scene_ids = list(range(1, 41))
    paths = set()
    for f in folders:
        for fixed in PER_FOLDER_FIXED:
            paths.add(f"Data/Stages/{f}/{fixed}")
        for sid in per_scene_ids:
            paths.add(f"Data/Stages/{f}/Scene{sid}.bin")
    # GameConfig also enumerates exact scene IDs (alphanumeric). Add those.
    for c in gc_categories:
        paths.add(f"Data/Stages/{c['folder']}/Scene{c['id']}.bin")

    # Per-stage StageConfig sweep -> object + sfx names.
    all_object_names = set(gc_objects)
    all_sfx_names    = set(gc_sfx)
    sc_count = 0
    if os.path.isdir(stages_root):
        for d in sorted(os.listdir(stages_root)):
            sc = os.path.join(stages_root, d, "StageConfig.bin")
            if not os.path.isfile(sc):
                continue
            try:
                with open(sc, "rb") as f:
                    sc_data = f.read()
                obj, sfx = parse_stageconfig(sc_data)
                all_object_names.update(obj)
                all_sfx_names.update(sfx)
                sc_count += 1
            except Exception as e:
                print(f"  [warn] {sc}: {e}", file=sys.stderr)
    print(f"[stageconfigs] parsed {sc_count}; total unique objects="
          f"{len(all_object_names)}, sfx={len(all_sfx_names)}")

    # Sprite candidates: each class against every plausible folder.
    sprite_folders = sorted(set(DEFAULT_SPRITE_FOLDERS) | folders | {"Global", "Players"})
    for cls in all_object_names:
        for folder in sprite_folders:
            for p in _candidate_sprite_paths(folder, cls):
                paths.add(p)
    # Per-sprite-folder atlas conventions (Objects.gif, Items.gif, etc.).
    for folder in sprite_folders:
        for base in ("Objects", "Objects2", "Objects3", "Items", "Items2",
                     "HUD", "Title", "TitleSprites"):
            paths.add(f"Data/Sprites/{folder}/{base}.gif")

    # SFX candidates.
    for name in all_sfx_names:
        for p in _candidate_sfx_paths(name):
            paths.add(p)

    # Music candidates: roles + per-folder act patterns.
    for role in GENERIC_MUSIC_ROLES:
        for p in _candidate_music_paths(role, folders):
            paths.add(p)
    # Folder roots themselves (e.g. AIZ.ogg, CPZ1.ogg, …).
    for f in sorted(folders):
        for suffix in PER_FOLDER_MUSIC_SUFFIXES:
            for ext in MUSIC_EXTS:
                paths.add(f"Data/Music/{f}{suffix}.{ext}")
    # Named-zone music bases (hash-confirmed): each gets 1.ogg + 2.ogg variants
    # plus a few generic suffixes for boss-style alternates.
    for base in NAMED_ZONE_MUSIC_BASES:
        for suffix in ("", "1", "2", "3", "Mini", "Boss"):
            for ext in MUSIC_EXTS:
                paths.add(f"Data/Music/{base}{suffix}.{ext}")

    # Video / cinematic candidates (only a handful in any RSDK game).
    for nm in ("Mania", "Title", "Intro", "Opening", "Ending", "Outro",
               "Logo", "Logos", "Cutscene"):
        for ext in ("ogv", "mp4", "webm"):
            paths.add(f"Data/Video/{nm}.{ext}")
            paths.add(f"Data/Movies/{nm}.{ext}")

    # Always-present global anchors (Mania puts these here).
    paths.add("Data/Game/GameConfig.bin")
    paths.add("Data/Sprites/Global/HUD.bin")
    paths.add("Data/Sprites/Global/Ring.bin")
    paths.add("Data/Sprites/Global/Items.gif")
    paths.add("Data/Sprites/Global/Objects.gif")
    paths.add("Data/Sprites/Global/Objects2.gif")
    paths.add("Data/Sprites/Global/Objects3.gif")
    paths.add("Data/Sprites/Global/PauseMenu.bin")
    paths.add("Data/Sprites/Global/Springs.bin")
    paths.add("Data/Sprites/Global/EggPrison.bin")
    paths.add("Data/Sprites/GHZ/Objects2.gif")
    paths.add("Data/Sprites/Title/Logo.bin")
    paths.add("Data/Sprites/Title/Logo.gif")
    paths.add("Data/Sprites/Title/DemoMenu.bin")
    paths.add("Data/Sprites/Title/DemoMenu.gif")
    # Phase 1.32: Title3DSprite + TitleBG entities source frames (Mountains,
    # Trees, Bushes, Reflection, WaterSparkle, WingShine) per
    # tools/_decomp_raw/SonicMania_Objects_Title_Title3DSprite.c:59 +
    # SonicMania_Objects_Title_TitleBG.c:89.
    paths.add("Data/Sprites/Title/Background.bin")
    paths.add("Data/Sprites/Title/BG.gif")

    # ---------------------------------------------------------------------
    # Phase 1.33 (2026-05-27) Title-scene comprehensive asset audit.
    # Sourced systematically from tools/_decomp_raw/SonicMania_Objects_Title_*.c
    # plus globals (Music, Localization) referenced from the scene.
    # See docs/title_scene_asset_audit.md for the per-line citation table.
    # ---------------------------------------------------------------------
    # TitleSetup.c:83 LoadSpriteAnimation("Title/Electricity.bin")
    paths.add("Data/Sprites/Title/Electricity.bin")
    paths.add("Data/Sprites/Title/Electricity1.gif")  # sheet 0 from Electricity.bin
    paths.add("Data/Sprites/Title/Electricity2.gif")  # sheet 1 from Electricity.bin
    # TitleSetup.c:85-87 GetSfx("Global/*.wav")
    paths.add("Data/SoundFX/Global/MenuBleep.wav")
    paths.add("Data/SoundFX/Global/MenuAccept.wav")
    paths.add("Data/SoundFX/Global/Ring.wav")
    # TitleSetup.c:371,376 PlayStream("Intro*.ogg") + LoadVideo("Mania.ogv")
    paths.add("Data/Music/IntroTee.ogg")
    paths.add("Data/Music/IntroHP.ogg")
    paths.add("Data/Video/Mania.ogv")
    # TitleSetup.c:331 SetScene("Presentation", "Menu") - the FIRST arg is
    # the GameConfig CATEGORY name (not a folder); the SECOND arg is the
    # scene-name in that category, which resolves via GameConfig to the
    # actual stage folder. For "Presentation" -> "Menu" the resolved folder
    # is "Menu" (already covered by the per-folder fixed block above).
    # TitleLogo.c:145 LoadSpriteAnimation("Title/Logo.bin") -- already above.
    # TitleLogo.c:149 LoadSpriteAnimation("Title/PlusLogo.bin") (Plus DLC)
    paths.add("Data/Sprites/Title/PlusLogo.bin")
    paths.add("Data/Sprites/Title/PlusLogo.gif")
    # TitleLogo.c:151 GetSfx("Stage/Plus.wav") (Plus DLC)
    paths.add("Data/SoundFX/Stage/Plus.wav")
    # TitleSonic.c:53 LoadSpriteAnimation("Title/Sonic.bin") -- already covered.
    paths.add("Data/Sprites/Title/Sonic.bin")
    paths.add("Data/Sprites/Title/Sonic.gif")
    # Title3DSprite.c:59 LoadSpriteAnimation("Title/Background.bin") -- already.
    # Title/Scene1.bin Music entity trackFile attr: "TitleScreen.ogg".
    paths.add("Data/Music/TitleScreen.ogg")
    # Music.c StageLoad (Global SCOPE_STAGE) tracks set on Title load
    # (Music.c:51-63 Music_SetMusicTrack literals).
    paths.add("Data/Music/Invincible.ogg")
    paths.add("Data/Music/Sneakers.ogg")
    paths.add("Data/Music/BossMini.ogg")
    paths.add("Data/Music/BossHBH.ogg")
    paths.add("Data/Music/BossEggman1.ogg")
    paths.add("Data/Music/BossEggman2.ogg")
    paths.add("Data/Music/ActClear.ogg")
    paths.add("Data/Music/Drowning.ogg")
    paths.add("Data/Music/GameOver.ogg")
    paths.add("Data/Music/Super.ogg")
    paths.add("Data/Music/HBHMischief.ogg")
    paths.add("Data/Music/1up.ogg")
    # Localization.c:42-83 LoadStringList("StringsXX.txt"). 9 locales.
    paths.add("Data/Strings/StringsEN.txt")
    paths.add("Data/Strings/StringsFR.txt")
    paths.add("Data/Strings/StringsIT.txt")
    paths.add("Data/Strings/StringsGE.txt")
    paths.add("Data/Strings/StringsSP.txt")
    paths.add("Data/Strings/StringsJP.txt")
    paths.add("Data/Strings/StringsKO.txt")
    paths.add("Data/Strings/StringsSC.txt")
    paths.add("Data/Strings/StringsTC.txt")

    paths.add("Data/Sprites/Players/Sonic.bin")
    paths.add("Data/Sprites/Players/Sonic1.gif")
    paths.add("Data/Sprites/Players/Sonic2.gif")
    paths.add("Data/Sprites/Players/Sonic3.gif")
    paths.add("Data/Sprites/Players/Tails.bin")
    paths.add("Data/Sprites/Players/Tails1.gif")
    paths.add("Data/Sprites/Players/Tails2.gif")
    paths.add("Data/Sprites/Players/Tails3.gif")
    paths.add("Data/Sprites/Players/Knux.bin")
    paths.add("Data/Sprites/Players/Knux1.gif")
    paths.add("Data/Sprites/Players/Knux2.gif")
    paths.add("Data/Sprites/Players/Knux3.gif")
    paths.add("Data/Sprites/Players/Mighty.bin")
    paths.add("Data/Sprites/Players/Ray.bin")

    # ---------------------------------------------------------------------
    # Phase 3.0-prep (2026-05-28) Menu / Logos comprehensive asset audit.
    # Sourced systematically from 54 tools/_decomp_raw/SonicMania_Objects_Menu_*.c
    # files (batch-fetched from upstream) plus Menu/Scene1.bin Music entity
    # trackFile attributes parsed via tools/parse_title_entities.py.
    # See docs/menu_scene_asset_audit.md for the per-line citation table.
    # ---------------------------------------------------------------------
    # LogoSetup.c:48 RSDK.GetSfx("Stage/Sega.wav")
    paths.add("Data/SoundFX/Stage/Sega.wav")
    # LogoSetup.c:50 + UIPicture.c:77/95 RSDK.LoadSpriteAnimation("Logos/Logos.bin")
    paths.add("Data/Sprites/Logos/Logos.bin")
    # DAControl.c:93/113 RSDK.LoadSpriteAnimation("UI/DAGarden.bin")
    paths.add("Data/Sprites/UI/DAGarden.bin")
    # DASetup.c:69-72 RSDK.GetSfx("Special|Global/*.wav")
    paths.add("Data/SoundFX/Special/Emerald.wav")
    paths.add("Data/SoundFX/Special/Medal.wav")
    paths.add("Data/SoundFX/Special/SSExit.wav")
    paths.add("Data/SoundFX/Global/ScoreTotal.wav")
    # DemoMenu.c:73/214 RSDK.LoadSpriteAnimation("Title/DemoMenu.bin") -- covered above
    # LevelSelect.c:65-70 RSDK.GetSfx + :75-76 RSDK.LoadSpriteAnimation
    paths.add("Data/SoundFX/Stage/Fail.wav")
    paths.add("Data/SoundFX/Special/Continue.wav")
    paths.add("Data/SoundFX/Special/MedalCaught.wav")
    paths.add("Data/Sprites/LSelect/Icons.bin")
    paths.add("Data/Sprites/LSelect/Text.bin")
    # ThanksSetup.c:57/61/174 RSDK.GetSfx("Stage/Sega.wav") -- covered + Decorations
    paths.add("Data/Sprites/Thanks/Decorations.bin")
    # UIButtonLabel.c:55/75 RSDK.LoadSpriteAnimation("UI/ButtonLabel.bin")
    paths.add("Data/Sprites/UI/ButtonLabel.bin")
    # UIButtonPrompt.c:167/542 + UIKeyBinder.c:187/471 LoadSpriteAnimation("UI/Buttons.bin")
    paths.add("Data/Sprites/UI/Buttons.bin")
    # UICharButton.c:108/266 + UIChoice.c:144/350 + many others LoadSpriteAnimation("UI/SaveSelect.bin")
    paths.add("Data/Sprites/UI/SaveSelect.bin")
    # UICreditsText.c:72/266 RSDK.LoadSpriteAnimation("UI/CreditsText.bin")
    paths.add("Data/Sprites/UI/CreditsText.bin")
    # UIDiorama.c:103-114 -- 11 sprite anims for character dioramas
    paths.add("Data/Sprites/UI/Diorama.bin")
    paths.add("Data/Sprites/AIZ/SchrodingersCapsule.bin")
    # Players/Sonic|Tails|Knux|Mighty|Ray.bin already covered globally above
    paths.add("Data/Sprites/Players/KnuxCutsceneAIZ.bin")
    paths.add("Data/Sprites/Players/KnuxCutsceneHPZ.bin")
    # Global/Ring.bin already covered globally above
    paths.add("Data/Sprites/Global/SpeedGate.bin")
    paths.add("Data/Sprites/SpecialBS/Sonic.bin")
    paths.add("Data/Sprites/SpecialBS/StageObjects.bin")
    # UIHeading.c:59-94 -- 9 language-specific headings .bin
    paths.add("Data/Sprites/UI/HeadingsEN.bin")
    paths.add("Data/Sprites/UI/HeadingsFR.bin")
    paths.add("Data/Sprites/UI/HeadingsIT.bin")
    paths.add("Data/Sprites/UI/HeadingsGE.bin")
    paths.add("Data/Sprites/UI/HeadingsSP.bin")
    paths.add("Data/Sprites/UI/HeadingsJP.bin")
    paths.add("Data/Sprites/UI/HeadingsKO.bin")
    paths.add("Data/Sprites/UI/HeadingsSC.bin")
    paths.add("Data/Sprites/UI/HeadingsTC.bin")
    # UIInfoLabel.c:89 + UIText.c:89 + UIVsResults.c:76/306 + UIVsZoneButton.c:129/396 +
    # UIWidgets.c:76-85/342 -- 9 language-specific text .bin
    paths.add("Data/Sprites/UI/TextEN.bin")
    paths.add("Data/Sprites/UI/TextFR.bin")
    paths.add("Data/Sprites/UI/TextIT.bin")
    paths.add("Data/Sprites/UI/TextGE.bin")
    paths.add("Data/Sprites/UI/TextSP.bin")
    paths.add("Data/Sprites/UI/TextJP.bin")
    paths.add("Data/Sprites/UI/TextKO.bin")
    paths.add("Data/Sprites/UI/TextSC.bin")
    paths.add("Data/Sprites/UI/TextTC.bin")
    # UIKeyBinder.c:189 RSDK.GetSfx("Stage/Fail.wav") -- covered above
    # UIMedallionPanel.c:36/66 RSDK.LoadSpriteAnimation("UI/MedallionPanel.bin")
    paths.add("Data/Sprites/UI/MedallionPanel.bin")
    # UIModeButton.c:107/325 RSDK.LoadSpriteAnimation("UI/MainIcons.bin")
    paths.add("Data/Sprites/UI/MainIcons.bin")
    # UIPicture.c:75/93 LoadSpriteAnimation("UI/Picture.bin") (Logos.bin already covered)
    paths.add("Data/Sprites/UI/Picture.bin")
    # UIWaitSpinner.c:73 RSDK.LoadSpriteAnimation("UI/WaitSpinner.bin")
    paths.add("Data/Sprites/UI/WaitSpinner.bin")
    # UIWidgets.c:34/337 LoadSpriteAnimation("UI/UIElements.bin")
    paths.add("Data/Sprites/UI/UIElements.bin")
    # UIWidgets.c:38/341 LoadSpriteAnimation("UI/SmallFont.bin")
    paths.add("Data/Sprites/UI/SmallFont.bin")
    # UIWidgets.c:46-48 RSDK.GetSfx("Global|Special/*.wav")
    paths.add("Data/SoundFX/Global/SpecialWarp.wav")
    paths.add("Data/SoundFX/Special/Event.wav")
    paths.add("Data/SoundFX/Global/MenuWoosh.wav")
    # Sheet GIFs referenced inside the above .bin sprite-animations (parsed
    # at gate-run time via parse_spr_sheets). Pre-enumerate the candidate
    # GIF paths so rsdk_extract.py hash-matches them; the gate then validates
    # each .bin's sheets[] list resolves.
    for p in [
        "Logos/Logos.gif",
        "UI/DAGarden.gif",
        "Thanks/Decorations.gif",
        "Thanks/Logos.gif",
        "UI/ButtonLabel.gif",
        "UI/Buttons.gif",
        "UI/Controllers.gif",
        "UI/SaveSelect.gif",
        "UI/SaveSelectEN.gif",
        "UI/SaveSelectFR.gif",
        "UI/SaveSelectIT.gif",
        "UI/SaveSelectGE.gif",
        "UI/SaveSelectSP.gif",
        "UI/SaveSelectJP.gif",
        "UI/SaveSelectKO.gif",
        "UI/SaveSelectSC.gif",
        "UI/SaveSelectTC.gif",
        "UI/CreditsText.gif",
        "UI/Diorama.gif",
        "Credits/Images.gif",
        "Players/KnuxCutsceneAIZ.gif",
        "Players/KnuxCutsceneAIZ1.gif",
        "Players/KnuxCutsceneAIZ2.gif",
        "Players/KnuxCutsceneHPZ.gif",
        "Players/KnuxCutsceneHPZ1.gif",
        "Players/KnuxCutsceneHPZ2.gif",
        "Players/Mighty1.gif",
        "Players/Mighty2.gif",
        "Players/Mighty3.gif",
        "Players/Ray1.gif",
        "Players/Ray2.gif",
        "Players/Ray3.gif",
        "Global/SpeedGate.gif",
        "SpecialBS/Sonic.gif",
        "SpecialBS/Players.gif",
        "SpecialBS/StageObjects.gif",
        "UI/HeadingsEN.gif",
        "UI/HeadingsFR.gif",
        "UI/HeadingsIT.gif",
        "UI/HeadingsGE.gif",
        "UI/HeadingsSP.gif",
        "UI/HeadingsJP.gif",
        "UI/HeadingsKO.gif",
        "UI/HeadingsSC.gif",
        "UI/HeadingsTC.gif",
        "UI/TextEN.gif",
        "UI/TextFR.gif",
        "UI/TextIT.gif",
        "UI/TextGE.gif",
        "UI/TextSP.gif",
        "UI/TextJP.gif",
        "UI/TextKO.gif",
        "UI/TextSC.gif",
        "UI/TextTC.gif",
        "UI/PauseEN.gif",
        "UI/PauseFR.gif",
        "UI/PauseIT.gif",
        "UI/PauseGE.gif",
        "UI/PauseSP.gif",
        "UI/PauseJP.gif",
        "UI/PauseKO.gif",
        "UI/PauseSC.gif",
        "UI/PauseTC.gif",
        "UI/MedallionPanel.gif",
        "UI/MainIcons.gif",
        "UI/Picture.gif",
        "UI/WaitSpinner.gif",
        "UI/UIElements.gif",
        "UI/UI.gif",
        "UI/SmallFont.gif",
        "UI/SmallFont1.gif",
        "UI/SmallFont2.gif",
        "UI/Zones.gif",
        "AIZ/SchrodingersCapsule.gif",
        "LSelect/Icons.gif",
        "LSelect/Text.gif",
    ]:
        paths.add(f"Data/Sprites/{p}")

    # AIZ/SchrodingersCapsule.bin -- not in any Phase-1.x extracted set but
    # referenced by UIDiorama.c:104. Already added above as Data/Sprites/AIZ/...
    paths.add("Data/Sprites/AIZ/SchrodingersCapsule.bin")
    # UI/Diorama.bin -- the SaveSelect character-diorama atlas anim.
    paths.add("Data/Sprites/UI/Diorama.bin")
    # Players/KnuxCutsceneAIZ.bin + KnuxCutsceneHPZ.bin
    paths.add("Data/Sprites/Players/KnuxCutsceneAIZ.bin")
    paths.add("Data/Sprites/Players/KnuxCutsceneHPZ.bin")

    # Menu/Scene1.bin Music entities (parsed via tools/parse_title_entities.py).
    # Four Music entities at slots 129/198/199/200 carry trackFile attrs
    # tied to trackID 0..3 (MainMenu/Competition/Results/SaveSelect).
    # MenuSetup_ChangeMenuTrack:890-905 calls Music_PlayTrack(trackID 0..3)
    # which reads Music->trackNames[track] populated by each Music entity's
    # Create from Music.c:92-105 Music_SetMusicTrack(trackFile, ...).
    paths.add("Data/Music/MainMenu.ogg")
    paths.add("Data/Music/Competition.ogg")
    paths.add("Data/Music/Results.ogg")
    paths.add("Data/Music/SaveSelect.ogg")


    # ---------------------------------------------------------------------
    # Phase 3.0-prep++ (2026-05-28) Whole-game asset audit.
    # Sourced systematically from 518 cached
    # tools/_decomp_raw/SonicMania_Objects_*.c files (every scene + every
    # subsystem) -- 1477 RSDK.LoadSpriteAnimation/LoadSpriteSheet/
    # LoadStringList/LoadVideo/PlayStream/GetSfx/Music_SetMusicTrack call
    # sites enumerated by tools/phase3_0_plus_scan_assets.py. PLUS 161
    # Music-entity trackFile attrs parsed from every
    # extracted/Data/Stages/<Folder>/Scene*.bin via
    # tools/phase3_0_plus_enumerate.py + parse_title_entities.parse_entities.
    # See docs/whole_game_asset_audit.md for the per-scene per-object
    # inventory and gate verdict.
    # ---------------------------------------------------------------------
    # Music tracks (PlayStream / Music_SetMusicTrack literals + Scene1.bin
    # Music entity trackFile attrs).
    paths.add("Data/Music/1Up.ogg")
    paths.add("Data/Music/1up.ogg")
    paths.add("Data/Music/ActClear.ogg")
    paths.add("Data/Music/AngelIsland.ogg")
    paths.add("Data/Music/BlueSpheres.ogg")
    paths.add("Data/Music/BlueSpheresSPD.ogg")
    paths.add("Data/Music/BossEggman1.ogg")
    paths.add("Data/Music/BossEggman2.ogg")
    paths.add("Data/Music/BossFinal.ogg")
    paths.add("Data/Music/BossHBH.ogg")
    paths.add("Data/Music/BossMini.ogg")
    paths.add("Data/Music/BossPuyo.ogg")
    paths.add("Data/Music/ChemicalPlant1.ogg")
    paths.add("Data/Music/ChemicalPlant2.ogg")
    paths.add("Data/Music/Competition.ogg")
    paths.add("Data/Music/Countdown.ogg")
    paths.add("Data/Music/Credits.ogg")
    paths.add("Data/Music/Drowning.ogg")
    paths.add("Data/Music/EggReverie.ogg")
    paths.add("Data/Music/EggReveriePinch.ogg")
    paths.add("Data/Music/FlyingBattery1.ogg")
    paths.add("Data/Music/FlyingBattery2.ogg")
    paths.add("Data/Music/GameOver.ogg")
    paths.add("Data/Music/GreenHill1.ogg")
    paths.add("Data/Music/GreenHill2.ogg")
    paths.add("Data/Music/HBHMischief.ogg")
    paths.add("Data/Music/Hydrocity1.ogg")
    paths.add("Data/Music/Hydrocity2.ogg")
    paths.add("Data/Music/IntroHP.ogg")
    paths.add("Data/Music/IntroTee.ogg")
    paths.add("Data/Music/Invincible.ogg")
    paths.add("Data/Music/LavaReef1.ogg")
    paths.add("Data/Music/LavaReef2.ogg")
    paths.add("Data/Music/MainMenu.ogg")
    paths.add("Data/Music/MetalSonic.ogg")
    paths.add("Data/Music/MetallicMadness1.ogg")
    paths.add("Data/Music/MetallicMadness2.ogg")
    paths.add("Data/Music/MirageSaloon1.ogg")
    paths.add("Data/Music/MirageSaloon1k.ogg")
    paths.add("Data/Music/MirageSaloon2.ogg")
    paths.add("Data/Music/OilOcean1.ogg")
    paths.add("Data/Music/OilOcean2.ogg")
    paths.add("Data/Music/PulpSolstice1.ogg")
    paths.add("Data/Music/PulpSolstice2.ogg")
    paths.add("Data/Music/Results.ogg")
    paths.add("Data/Music/RubyPresence.ogg")
    paths.add("Data/Music/SPZ1.ogg")
    paths.add("Data/Music/SaveSelect.ogg")
    paths.add("Data/Music/ShiversawExplosion.ogg")
    paths.add("Data/Music/Sneakers.ogg")
    paths.add("Data/Music/StardustSpeedway1.ogg")
    paths.add("Data/Music/StardustSpeedway2.ogg")
    paths.add("Data/Music/Studiopolis1.ogg")
    paths.add("Data/Music/Studiopolis2.ogg")
    paths.add("Data/Music/Super.ogg")
    paths.add("Data/Music/TimeWarp.ogg")
    paths.add("Data/Music/TitanicMonarch1.ogg")
    paths.add("Data/Music/TitanicMonarch2.ogg")
    paths.add("Data/Music/TitleScreen.ogg")
    paths.add("Data/Music/UFOSpecial.ogg")
    # String lists (LoadStringList literals).
    paths.add("Data/Strings/Credits.txt")
    paths.add("Data/Strings/StringsEN.txt")
    paths.add("Data/Strings/StringsFR.txt")
    paths.add("Data/Strings/StringsGE.txt")
    paths.add("Data/Strings/StringsIT.txt")
    paths.add("Data/Strings/StringsJP.txt")
    paths.add("Data/Strings/StringsKO.txt")
    paths.add("Data/Strings/StringsSC.txt")
    paths.add("Data/Strings/StringsSP.txt")
    paths.add("Data/Strings/StringsTC.txt")
    # Video / cinematic streams (LoadVideo literals).
    paths.add("Data/Video/Mania.ogv")
    # SFX (GetSfx literals, grouped by folder).
    # -- Data/SoundFX/CPZ/
    paths.add("Data/SoundFX/CPZ/CPZ2HitBlocksStop.wav")
    paths.add("Data/SoundFX/CPZ/ChemChange.wav")
    paths.add("Data/SoundFX/CPZ/ChemDrop.wav")
    paths.add("Data/SoundFX/CPZ/ChemRed.wav")
    paths.add("Data/SoundFX/CPZ/ChemYellow.wav")
    paths.add("Data/SoundFX/CPZ/DNABurst.wav")
    paths.add("Data/SoundFX/CPZ/DNAGrab.wav")
    paths.add("Data/SoundFX/CPZ/DNAScan.wav")
    paths.add("Data/SoundFX/CPZ/DNATiny0.wav")
    paths.add("Data/SoundFX/CPZ/DNATiny1.wav")
    paths.add("Data/SoundFX/CPZ/DNATiny2.wav")
    paths.add("Data/SoundFX/CPZ/DNATiny3.wav")
    paths.add("Data/SoundFX/CPZ/DNATiny4.wav")
    paths.add("Data/SoundFX/CPZ/DNATiny5.wav")
    paths.add("Data/SoundFX/CPZ/DroidBounce.wav")
    paths.add("Data/SoundFX/CPZ/DroidGather.wav")
    paths.add("Data/SoundFX/CPZ/DroidRelease.wav")
    # -- Data/SoundFX/ERZ/
    paths.add("Data/SoundFX/ERZ/FlyIn.wav")
    # -- Data/SoundFX/FBZ/
    paths.add("Data/SoundFX/FBZ/Elevator.wav")
    paths.add("Data/SoundFX/FBZ/FBZFan.wav")
    paths.add("Data/SoundFX/FBZ/Magnet.wav")
    paths.add("Data/SoundFX/FBZ/Orbinaut.wav")
    paths.add("Data/SoundFX/FBZ/Rain.wav")
    paths.add("Data/SoundFX/FBZ/SpiderCharge.wav")
    paths.add("Data/SoundFX/FBZ/SpiderElecBall.wav")
    paths.add("Data/SoundFX/FBZ/SpiderFall.wav")
    paths.add("Data/SoundFX/FBZ/SpiderFlailing.wav")
    paths.add("Data/SoundFX/FBZ/SpiderHitGround.wav")
    paths.add("Data/SoundFX/FBZ/SpiderRecovery.wav")
    paths.add("Data/SoundFX/FBZ/Thunder.wav")
    paths.add("Data/SoundFX/FBZ/WarpDoor.wav")
    # -- Data/SoundFX/Global/
    paths.add("Data/SoundFX/Global/BlueShield.wav")
    paths.add("Data/SoundFX/Global/BubbleBounce.wav")
    paths.add("Data/SoundFX/Global/BubbleShield.wav")
    paths.add("Data/SoundFX/Global/Charge.wav")
    paths.add("Data/SoundFX/Global/Destroy.wav")
    paths.add("Data/SoundFX/Global/DropDash.wav")
    paths.add("Data/SoundFX/Global/FireDash.wav")
    paths.add("Data/SoundFX/Global/FireShield.wav")
    paths.add("Data/SoundFX/Global/Flying.wav")
    paths.add("Data/SoundFX/Global/Grab.wav")
    paths.add("Data/SoundFX/Global/Hurt.wav")
    paths.add("Data/SoundFX/Global/HyperRing.wav")
    paths.add("Data/SoundFX/Global/InstaShield.wav")
    paths.add("Data/SoundFX/Global/Jump.wav")
    paths.add("Data/SoundFX/Global/Land.wav")
    paths.add("Data/SoundFX/Global/LightningJump.wav")
    paths.add("Data/SoundFX/Global/LightningShield.wav")
    paths.add("Data/SoundFX/Global/LoseRings.wav")
    paths.add("Data/SoundFX/Global/MenuAccept.wav")
    paths.add("Data/SoundFX/Global/MenuBleep.wav")
    paths.add("Data/SoundFX/Global/MenuWoosh.wav")
    paths.add("Data/SoundFX/Global/MightyDeflect.wav")
    paths.add("Data/SoundFX/Global/MightyDrill.wav")
    paths.add("Data/SoundFX/Global/MightyLand.wav")
    paths.add("Data/SoundFX/Global/MightyUnspin.wav")
    paths.add("Data/SoundFX/Global/OuttaHere.wav")
    paths.add("Data/SoundFX/Global/PeelCharge.wav")
    paths.add("Data/SoundFX/Global/PeelRelease.wav")
    paths.add("Data/SoundFX/Global/Push.wav")
    paths.add("Data/SoundFX/Global/Recovery.wav")
    paths.add("Data/SoundFX/Global/Release.wav")
    paths.add("Data/SoundFX/Global/Ring.wav")
    paths.add("Data/SoundFX/Global/Roll.wav")
    paths.add("Data/SoundFX/Global/ScoreAdd.wav")
    paths.add("Data/SoundFX/Global/ScoreTotal.wav")
    paths.add("Data/SoundFX/Global/SignPost.wav")
    paths.add("Data/SoundFX/Global/SignPost2p.wav")
    paths.add("Data/SoundFX/Global/Skidding.wav")
    paths.add("Data/SoundFX/Global/Slide.wav")
    paths.add("Data/SoundFX/Global/SpecialRing.wav")
    paths.add("Data/SoundFX/Global/SpecialWarp.wav")
    paths.add("Data/SoundFX/Global/Spike.wav")
    paths.add("Data/SoundFX/Global/SpikesMove.wav")
    paths.add("Data/SoundFX/Global/Spring.wav")
    paths.add("Data/SoundFX/Global/StarPost.wav")
    paths.add("Data/SoundFX/Global/Swap.wav")
    paths.add("Data/SoundFX/Global/SwapFail.wav")
    paths.add("Data/SoundFX/Global/Teleport.wav")
    paths.add("Data/SoundFX/Global/Tired.wav")
    paths.add("Data/SoundFX/Global/Twinkle.wav")
    # -- Data/SoundFX/HCZ/
    paths.add("Data/SoundFX/HCZ/BigFan.wav")
    paths.add("Data/SoundFX/HCZ/Dunkey.wav")
    paths.add("Data/SoundFX/HCZ/EggMobile.wav")
    paths.add("Data/SoundFX/HCZ/PullChain.wav")
    paths.add("Data/SoundFX/HCZ/Skim.wav")
    paths.add("Data/SoundFX/HCZ/SmallFan.wav")
    paths.add("Data/SoundFX/HCZ/Spear.wav")
    paths.add("Data/SoundFX/HCZ/Wash.wav")
    paths.add("Data/SoundFX/HCZ/WaterGush.wav")
    paths.add("Data/SoundFX/HCZ/WaterLevel_L.wav")
    paths.add("Data/SoundFX/HCZ/WaterLevel_R.wav")
    paths.add("Data/SoundFX/HCZ/Whirlpool.wav")
    # -- Data/SoundFX/LRZ/
    paths.add("Data/SoundFX/LRZ/Charge.wav")
    paths.add("Data/SoundFX/LRZ/Drill.wav")
    paths.add("Data/SoundFX/LRZ/DrillJump.wav")
    paths.add("Data/SoundFX/LRZ/ElecOn.wav")
    paths.add("Data/SoundFX/LRZ/KingCharge.wav")
    paths.add("Data/SoundFX/LRZ/LaserErupt.wav")
    paths.add("Data/SoundFX/LRZ/LaserSweep.wav")
    paths.add("Data/SoundFX/LRZ/LavaGeyser.wav")
    paths.add("Data/SoundFX/LRZ/RiderCharge.wav")
    paths.add("Data/SoundFX/LRZ/RiderCheer.wav")
    paths.add("Data/SoundFX/LRZ/RiderJump.wav")
    paths.add("Data/SoundFX/LRZ/RiderLaunch.wav")
    paths.add("Data/SoundFX/LRZ/RiderSkid.wav")
    paths.add("Data/SoundFX/LRZ/RodPlant.wav")
    paths.add("Data/SoundFX/LRZ/RodShine.wav")
    paths.add("Data/SoundFX/LRZ/Sizzle.wav")
    paths.add("Data/SoundFX/LRZ/Turbine.wav")
    paths.add("Data/SoundFX/LRZ/TwinCharge.wav")
    paths.add("Data/SoundFX/LRZ/TwinShot.wav")
    paths.add("Data/SoundFX/LRZ/WalkerLegs.wav")
    paths.add("Data/SoundFX/LRZ/WalkerLegs2.wav")
    paths.add("Data/SoundFX/LRZ/Warp.wav")
    # -- Data/SoundFX/MMZ/
    paths.add("Data/SoundFX/MMZ/Alarm.wav")
    paths.add("Data/SoundFX/MMZ/Giggle.wav")
    paths.add("Data/SoundFX/MMZ/Grow2.wav")
    paths.add("Data/SoundFX/MMZ/Indicator.wav")
    paths.add("Data/SoundFX/MMZ/PistonLand.wav")
    paths.add("Data/SoundFX/MMZ/PistonLaunch.wav")
    paths.add("Data/SoundFX/MMZ/SawDown.wav")
    paths.add("Data/SoundFX/MMZ/SawUp.wav")
    paths.add("Data/SoundFX/MMZ/Shrink2.wav")
    paths.add("Data/SoundFX/MMZ/TicTock.wav")
    # -- Data/SoundFX/MSZ/
    paths.add("Data/SoundFX/MSZ/CactDrop.wav")
    paths.add("Data/SoundFX/MSZ/CaterJump.wav")
    paths.add("Data/SoundFX/MSZ/LocoChugga.wav")
    paths.add("Data/SoundFX/MSZ/LocoSmoke.wav")
    paths.add("Data/SoundFX/MSZ/LocoWhistle.wav")
    paths.add("Data/SoundFX/MSZ/MagicBox.wav")
    paths.add("Data/SoundFX/MSZ/Mayday.wav")
    paths.add("Data/SoundFX/MSZ/MysticAppearAct1.wav")
    paths.add("Data/SoundFX/MSZ/MysticBleeps.wav")
    paths.add("Data/SoundFX/MSZ/MysticHat.wav")
    paths.add("Data/SoundFX/MSZ/MysticHatNode.wav")
    paths.add("Data/SoundFX/MSZ/MysticPoof.wav")
    paths.add("Data/SoundFX/MSZ/MysticTransform.wav")
    paths.add("Data/SoundFX/MSZ/MysticTwinkle.wav")
    paths.add("Data/SoundFX/MSZ/Piano00C2.wav")
    paths.add("Data/SoundFX/MSZ/Pinata.wav")
    paths.add("Data/SoundFX/MSZ/SandFall.wav")
    paths.add("Data/SoundFX/MSZ/SandSwim.wav")
    paths.add("Data/SoundFX/MSZ/Spray.wav")
    paths.add("Data/SoundFX/MSZ/StoolHop.wav")
    paths.add("Data/SoundFX/MSZ/StoolSpin.wav")
    paths.add("Data/SoundFX/MSZ/Vultron.wav")
    # -- Data/SoundFX/OOZ/
    paths.add("Data/SoundFX/OOZ/GasPop.wav")
    paths.add("Data/SoundFX/OOZ/Harpoon.wav")
    paths.add("Data/SoundFX/OOZ/LaserSplash.wav")
    paths.add("Data/SoundFX/OOZ/OOZBullet.wav")
    paths.add("Data/SoundFX/OOZ/OOZLaser.wav")
    paths.add("Data/SoundFX/OOZ/OOZSurface.wav")
    paths.add("Data/SoundFX/OOZ/SmogClear.wav")
    paths.add("Data/SoundFX/OOZ/SubDescend.wav")
    paths.add("Data/SoundFX/OOZ/SubHatchClose.wav")
    paths.add("Data/SoundFX/OOZ/SubHatchOpen.wav")
    paths.add("Data/SoundFX/OOZ/SubSurface.wav")
    paths.add("Data/SoundFX/OOZ/Toss.wav")
    paths.add("Data/SoundFX/OOZ/Valve.wav")
    paths.add("Data/SoundFX/OOZ/Wrench.wav")
    # -- Data/SoundFX/PSZ/
    paths.add("Data/SoundFX/PSZ/ArrowHit.wav")
    paths.add("Data/SoundFX/PSZ/ArrowLaunch.wav")
    paths.add("Data/SoundFX/PSZ/ChipperChips.wav")
    paths.add("Data/SoundFX/PSZ/ChipperWood.wav")
    paths.add("Data/SoundFX/PSZ/Freeze.wav")
    paths.add("Data/SoundFX/PSZ/FrostThrower.wav")
    paths.add("Data/SoundFX/PSZ/Juggle.wav")
    paths.add("Data/SoundFX/PSZ/JuggleThrow.wav")
    paths.add("Data/SoundFX/PSZ/Letter.wav")
    paths.add("Data/SoundFX/PSZ/Paper.wav")
    paths.add("Data/SoundFX/PSZ/Peck.wav")
    paths.add("Data/SoundFX/PSZ/Petals.wav")
    paths.add("Data/SoundFX/PSZ/Press.wav")
    paths.add("Data/SoundFX/PSZ/ShinobiDefeat.wav")
    paths.add("Data/SoundFX/PSZ/ShinobiDropIn.wav")
    paths.add("Data/SoundFX/PSZ/ShinobiExplode.wav")
    paths.add("Data/SoundFX/PSZ/ShinobiGlitch.wav")
    paths.add("Data/SoundFX/PSZ/ShinobiJump.wav")
    paths.add("Data/SoundFX/PSZ/ShinobiParry.wav")
    paths.add("Data/SoundFX/PSZ/ShinobiSlash.wav")
    paths.add("Data/SoundFX/PSZ/ShinobiStick.wav")
    paths.add("Data/SoundFX/PSZ/ShinobiThrow.wav")
    paths.add("Data/SoundFX/PSZ/SplatsLand.wav")
    paths.add("Data/SoundFX/PSZ/SplatsSpawn.wav")
    paths.add("Data/SoundFX/PSZ/Struggle.wav")
    # -- Data/SoundFX/Puyo/
    paths.add("Data/SoundFX/Puyo/Attack.wav")
    paths.add("Data/SoundFX/Puyo/Chain0.wav")
    paths.add("Data/SoundFX/Puyo/Chain1.wav")
    paths.add("Data/SoundFX/Puyo/Chain2.wav")
    paths.add("Data/SoundFX/Puyo/Chain3.wav")
    paths.add("Data/SoundFX/Puyo/Chain4.wav")
    paths.add("Data/SoundFX/Puyo/Chain5.wav")
    paths.add("Data/SoundFX/Puyo/Fall.wav")
    paths.add("Data/SoundFX/Puyo/Junk.wav")
    paths.add("Data/SoundFX/Puyo/Land.wav")
    paths.add("Data/SoundFX/Puyo/Rotate.wav")
    # -- Data/SoundFX/Ruby/
    paths.add("Data/SoundFX/Ruby/Attack1_L.wav")
    paths.add("Data/SoundFX/Ruby/Attack1_R.wav")
    paths.add("Data/SoundFX/Ruby/Attack2_L.wav")
    paths.add("Data/SoundFX/Ruby/Attack2_R.wav")
    paths.add("Data/SoundFX/Ruby/Attack3_L.wav")
    paths.add("Data/SoundFX/Ruby/Attack3_R.wav")
    paths.add("Data/SoundFX/Ruby/Attack4_L.wav")
    paths.add("Data/SoundFX/Ruby/Attack4_R.wav")
    paths.add("Data/SoundFX/Ruby/Attack5_L.wav")
    paths.add("Data/SoundFX/Ruby/Attack5_R.wav")
    paths.add("Data/SoundFX/Ruby/Attack6_L.wav")
    paths.add("Data/SoundFX/Ruby/Attack6_R.wav")
    paths.add("Data/SoundFX/Ruby/RedCube_L.wav")
    paths.add("Data/SoundFX/Ruby/RedCube_R.wav")
    # -- Data/SoundFX/SPZ/
    paths.add("Data/SoundFX/SPZ/Flyover.wav")
    paths.add("Data/SoundFX/SPZ/Funnel.wav")
    paths.add("Data/SoundFX/SPZ/Score.wav")
    paths.add("Data/SoundFX/SPZ/ShBugSnap.wav")
    paths.add("Data/SoundFX/SPZ/TheBuzz.wav")
    # -- Data/SoundFX/SPZ1/
    paths.add("Data/SoundFX/SPZ1/BazookaCharge.wav")
    paths.add("Data/SoundFX/SPZ1/BazookaThrow.wav")
    paths.add("Data/SoundFX/SPZ1/EggRoboFlyIn.wav")
    paths.add("Data/SoundFX/SPZ1/HeliWooshIn.wav")
    paths.add("Data/SoundFX/SPZ1/HeliWooshOut.wav")
    paths.add("Data/SoundFX/SPZ1/RocketFlip.wav")
    paths.add("Data/SoundFX/SPZ1/Rumble.wav")
    # -- Data/SoundFX/SPZ2/
    paths.add("Data/SoundFX/SPZ2/CardAppear.wav")
    paths.add("Data/SoundFX/SPZ2/CardFlip.wav")
    paths.add("Data/SoundFX/SPZ2/CardSelected.wav")
    paths.add("Data/SoundFX/SPZ2/Cloud.wav")
    paths.add("Data/SoundFX/SPZ2/Lightning.wav")
    paths.add("Data/SoundFX/SPZ2/Sun.wav")
    paths.add("Data/SoundFX/SPZ2/Wind.wav")
    # -- Data/SoundFX/SSZ1/
    paths.add("Data/SoundFX/SSZ1/BeanChomp.wav")
    paths.add("Data/SoundFX/SSZ1/BeanNode.wav")
    paths.add("Data/SoundFX/SSZ1/BouncePlant.wav")
    paths.add("Data/SoundFX/SSZ1/Flail.wav")
    paths.add("Data/SoundFX/SSZ1/Future.wav")
    paths.add("Data/SoundFX/SSZ1/HHWAppear.wav")
    paths.add("Data/SoundFX/SSZ1/HHWCharge.wav")
    paths.add("Data/SoundFX/SSZ1/HHWFlash.wav")
    paths.add("Data/SoundFX/SSZ1/HHWFlyUp.wav")
    paths.add("Data/SoundFX/SSZ1/HotaruAppear.wav")
    paths.add("Data/SoundFX/SSZ1/HotaruCharge.wav")
    paths.add("Data/SoundFX/SSZ1/HotaruFly.wav")
    paths.add("Data/SoundFX/SSZ1/HotaruLaser.wav")
    # -- Data/SoundFX/SSZ2/
    paths.add("Data/SoundFX/SSZ2/MSBall.wav")
    paths.add("Data/SoundFX/SSZ2/MSChargeFire.wav")
    paths.add("Data/SoundFX/SSZ2/MSElecPulse.wav")
    paths.add("Data/SoundFX/SSZ2/MSFireball.wav")
    paths.add("Data/SoundFX/SSZ2/MSShoot.wav")
    paths.add("Data/SoundFX/SSZ2/MSTransform.wav")
    paths.add("Data/SoundFX/SSZ2/SSArm.wav")
    paths.add("Data/SoundFX/SSZ2/SSBoost.wav")
    paths.add("Data/SoundFX/SSZ2/SSDash.wav")
    paths.add("Data/SoundFX/SSZ2/SSJump.wav")
    paths.add("Data/SoundFX/SSZ2/SSRebound.wav")
    paths.add("Data/SoundFX/SSZ2/Spark.wav")
    # -- Data/SoundFX/Special/
    paths.add("Data/SoundFX/Special/BlueSphere.wav")
    paths.add("Data/SoundFX/Special/BlueSphere2.wav")
    paths.add("Data/SoundFX/Special/Continue.wav")
    paths.add("Data/SoundFX/Special/Emerald.wav")
    paths.add("Data/SoundFX/Special/Event.wav")
    paths.add("Data/SoundFX/Special/GrittyGround.wav")
    paths.add("Data/SoundFX/Special/MachSpeed.wav")
    paths.add("Data/SoundFX/Special/Medal.wav")
    paths.add("Data/SoundFX/Special/MedalCaught.wav")
    paths.add("Data/SoundFX/Special/SSExit.wav")
    paths.add("Data/SoundFX/Special/SSJettison.wav")
    paths.add("Data/SoundFX/Special/Skid.wav")
    # -- Data/SoundFX/Stage/
    paths.add("Data/SoundFX/Stage/Assemble.wav")
    paths.add("Data/SoundFX/Stage/Beep3.wav")
    paths.add("Data/SoundFX/Stage/Beep4.wav")
    paths.add("Data/SoundFX/Stage/Bloop.wav")
    paths.add("Data/SoundFX/Stage/BossHit.wav")
    paths.add("Data/SoundFX/Stage/Breathe.wav")
    paths.add("Data/SoundFX/Stage/BulbPop.wav")
    paths.add("Data/SoundFX/Stage/Bumper.wav")
    paths.add("Data/SoundFX/Stage/Bumper2.wav")
    paths.add("Data/SoundFX/Stage/Bumper3.wav")
    paths.add("Data/SoundFX/Stage/Button.wav")
    paths.add("Data/SoundFX/Stage/Button2.wav")
    paths.add("Data/SoundFX/Stage/Buzzsaw.wav")
    paths.add("Data/SoundFX/Stage/CannonFire.wav")
    paths.add("Data/SoundFX/Stage/Chain.wav")
    paths.add("Data/SoundFX/Stage/Clack.wav")
    paths.add("Data/SoundFX/Stage/Clack2.wav")
    paths.add("Data/SoundFX/Stage/Clacker.wav")
    paths.add("Data/SoundFX/Stage/Clang.wav")
    paths.add("Data/SoundFX/Stage/Clang2.wav")
    paths.add("Data/SoundFX/Stage/Click.wav")
    paths.add("Data/SoundFX/Stage/Door.wav")
    paths.add("Data/SoundFX/Stage/Drop.wav")
    paths.add("Data/SoundFX/Stage/Drown.wav")
    paths.add("Data/SoundFX/Stage/DrownAlert.wav")
    paths.add("Data/SoundFX/Stage/ElecCharge.wav")
    paths.add("Data/SoundFX/Stage/ElecPulse.wav")
    paths.add("Data/SoundFX/Stage/Electrify.wav")
    paths.add("Data/SoundFX/Stage/Electrify2.wav")
    paths.add("Data/SoundFX/Stage/Explosion.wav")
    paths.add("Data/SoundFX/Stage/Explosion2.wav")
    paths.add("Data/SoundFX/Stage/Explosion3.wav")
    paths.add("Data/SoundFX/Stage/Extend.wav")
    paths.add("Data/SoundFX/Stage/Fail.wav")
    paths.add("Data/SoundFX/Stage/FanStart.wav")
    paths.add("Data/SoundFX/Stage/Fireball.wav")
    paths.add("Data/SoundFX/Stage/Flame2.wav")
    paths.add("Data/SoundFX/Stage/Flap.wav")
    paths.add("Data/SoundFX/Stage/Flipper.wav")
    paths.add("Data/SoundFX/Stage/GoodFuture.wav")
    paths.add("Data/SoundFX/Stage/Huff.wav")
    paths.add("Data/SoundFX/Stage/HullClose.wav")
    paths.add("Data/SoundFX/Stage/Impact2.wav")
    paths.add("Data/SoundFX/Stage/Impact3.wav")
    paths.add("Data/SoundFX/Stage/Impact4.wav")
    paths.add("Data/SoundFX/Stage/Impact5.wav")
    paths.add("Data/SoundFX/Stage/Impact6.wav")
    paths.add("Data/SoundFX/Stage/Jump2.wav")
    paths.add("Data/SoundFX/Stage/Landing.wav")
    paths.add("Data/SoundFX/Stage/Launch.wav")
    paths.add("Data/SoundFX/Stage/Lava.wav")
    paths.add("Data/SoundFX/Stage/LedgeBreak.wav")
    paths.add("Data/SoundFX/Stage/LedgeBreak3.wav")
    paths.add("Data/SoundFX/Stage/LetterTurn.wav")
    paths.add("Data/SoundFX/Stage/LottoBounce.wav")
    paths.add("Data/SoundFX/Stage/MachineActivate.wav")
    paths.add("Data/SoundFX/Stage/Magnet.wav")
    paths.add("Data/SoundFX/Stage/Open.wav")
    paths.add("Data/SoundFX/Stage/PimPom.wav")
    paths.add("Data/SoundFX/Stage/Plus.wav")
    paths.add("Data/SoundFX/Stage/Pon.wav")
    paths.add("Data/SoundFX/Stage/PopcornLaunch.wav")
    paths.add("Data/SoundFX/Stage/PowerDown.wav")
    paths.add("Data/SoundFX/Stage/PowerUp.wav")
    paths.add("Data/SoundFX/Stage/Pulley.wav")
    paths.add("Data/SoundFX/Stage/Push.wav")
    paths.add("Data/SoundFX/Stage/Repel.wav")
    paths.add("Data/SoundFX/Stage/Retract.wav")
    paths.add("Data/SoundFX/Stage/RockemSockem.wav")
    paths.add("Data/SoundFX/Stage/RocketJet.wav")
    paths.add("Data/SoundFX/Stage/Rotate.wav")
    paths.add("Data/SoundFX/Stage/Rumble.wav")
    paths.add("Data/SoundFX/Stage/Rush.wav")
    paths.add("Data/SoundFX/Stage/Satellite1.wav")
    paths.add("Data/SoundFX/Stage/Satellite2.wav")
    paths.add("Data/SoundFX/Stage/Sega.wav")
    paths.add("Data/SoundFX/Stage/Sharp.wav")
    paths.add("Data/SoundFX/Stage/Shoot1.wav")
    paths.add("Data/SoundFX/Stage/Shot.wav")
    paths.add("Data/SoundFX/Stage/SpeedBooster.wav")
    paths.add("Data/SoundFX/Stage/SpewBall.wav")
    paths.add("Data/SoundFX/Stage/Splash.wav")
    paths.add("Data/SoundFX/Stage/Targeting1.wav")
    paths.add("Data/SoundFX/Stage/Transform2.wav")
    paths.add("Data/SoundFX/Stage/Unravel.wav")
    paths.add("Data/SoundFX/Stage/Wall.wav")
    paths.add("Data/SoundFX/Stage/Warning.wav")
    paths.add("Data/SoundFX/Stage/Waterfall.wav")
    paths.add("Data/SoundFX/Stage/Waterfall2.wav")
    paths.add("Data/SoundFX/Stage/Whack.wav")
    paths.add("Data/SoundFX/Stage/Win.wav")
    paths.add("Data/SoundFX/Stage/WindowShatter.wav")
    paths.add("Data/SoundFX/Stage/Zap.wav")
    # -- Data/SoundFX/TMZ1/
    paths.add("Data/SoundFX/TMZ1/Bouncer.wav")
    paths.add("Data/SoundFX/TMZ1/CarRev.wav")
    paths.add("Data/SoundFX/TMZ1/Crash.wav")
    paths.add("Data/SoundFX/TMZ1/CyberSwarm.wav")
    paths.add("Data/SoundFX/TMZ1/Elevator.wav")
    paths.add("Data/SoundFX/TMZ1/FlasherFlop.wav")
    paths.add("Data/SoundFX/TMZ1/FlasherZap.wav")
    paths.add("Data/SoundFX/TMZ1/HogDrop.wav")
    paths.add("Data/SoundFX/TMZ1/HogJump.wav")
    paths.add("Data/SoundFX/TMZ1/Hover.wav")
    paths.add("Data/SoundFX/TMZ1/Hover2.wav")
    paths.add("Data/SoundFX/TMZ1/JacobsLadder.wav")
    paths.add("Data/SoundFX/TMZ1/PlasmaBall.wav")
    paths.add("Data/SoundFX/TMZ1/TrafficLight.wav")
    paths.add("Data/SoundFX/TMZ1/TurtleWalk.wav")
    paths.add("Data/SoundFX/TMZ1/TurtleWalk2.wav")
    # -- Data/SoundFX/TMZ3/
    paths.add("Data/SoundFX/TMZ3/Alarm.wav")
    paths.add("Data/SoundFX/TMZ3/BigLaser.wav")
    paths.add("Data/SoundFX/TMZ3/CupSwap.wav")
    paths.add("Data/SoundFX/TMZ3/Jump.wav")
    paths.add("Data/SoundFX/TMZ3/Land.wav")
    paths.add("Data/SoundFX/TMZ3/Missile.wav")
    paths.add("Data/SoundFX/TMZ3/Repel.wav")
    paths.add("Data/SoundFX/TMZ3/RubyGet.wav")
    paths.add("Data/SoundFX/TMZ3/Shield.wav")
    paths.add("Data/SoundFX/TMZ3/ShinobiBlade.wav")
    paths.add("Data/SoundFX/TMZ3/ShinobiHit.wav")
    paths.add("Data/SoundFX/TMZ3/Shock.wav")
    paths.add("Data/SoundFX/TMZ3/Summon.wav")
    # -- Data/SoundFX/Tube/
    paths.add("Data/SoundFX/Tube/Exit.wav")
    paths.add("Data/SoundFX/Tube/Travel.wav")
    # -- Data/SoundFX/VO/
    paths.add("Data/SoundFX/VO/Go.wav")
    paths.add("Data/SoundFX/VO/Goal.wav")
    paths.add("Data/SoundFX/VO/ItsADraw.wav")
    paths.add("Data/SoundFX/VO/ItsADraw_Set.wav")
    paths.add("Data/SoundFX/VO/Knuckles.wav")
    paths.add("Data/SoundFX/VO/KnuxWins.wav")
    paths.add("Data/SoundFX/VO/Mighty.wav")
    paths.add("Data/SoundFX/VO/MightyWins.wav")
    paths.add("Data/SoundFX/VO/NewRecordMid.wav")
    paths.add("Data/SoundFX/VO/NewRecordTop.wav")
    paths.add("Data/SoundFX/VO/One.wav")
    paths.add("Data/SoundFX/VO/Player1.wav")
    paths.add("Data/SoundFX/VO/Player2.wav")
    paths.add("Data/SoundFX/VO/Player3.wav")
    paths.add("Data/SoundFX/VO/Player4.wav")
    paths.add("Data/SoundFX/VO/Ray.wav")
    paths.add("Data/SoundFX/VO/RayWins.wav")
    paths.add("Data/SoundFX/VO/Sonic.wav")
    paths.add("Data/SoundFX/VO/SonicWins.wav")
    paths.add("Data/SoundFX/VO/Tails.wav")
    paths.add("Data/SoundFX/VO/TailsWins.wav")
    paths.add("Data/SoundFX/VO/TheWinnerIs.wav")
    paths.add("Data/SoundFX/VO/Three.wav")
    paths.add("Data/SoundFX/VO/Two.wav")
    # Sprite animations (LoadSpriteAnimation / LoadSpriteSheet literals,
    # grouped by Data/Sprites/<folder>/).
    # -- Data/Sprites/AIZ/
    paths.add("Data/Sprites/AIZ/AIZEggRobo.bin")
    paths.add("Data/Sprites/AIZ/AIZTornado.bin")
    paths.add("Data/Sprites/AIZ/AniTiles.gif")
    paths.add("Data/Sprites/AIZ/CaterkillerJr.bin")
    paths.add("Data/Sprites/AIZ/Claw.bin")
    paths.add("Data/Sprites/AIZ/Decoration.bin")
    paths.add("Data/Sprites/AIZ/Platform.bin")
    paths.add("Data/Sprites/AIZ/Portal.bin")
    paths.add("Data/Sprites/AIZ/SchrodingersCapsule.bin")
    paths.add("Data/Sprites/AIZ/Sweep.bin")
    paths.add("Data/Sprites/AIZ/SwingRope.bin")
    # -- Data/Sprites/Blueprint/
    paths.add("Data/Sprites/Blueprint/BuzzBomber.bin")
    paths.add("Data/Sprites/Blueprint/CheckerBall.bin")
    paths.add("Data/Sprites/Blueprint/CircleBumper.bin")
    paths.add("Data/Sprites/Blueprint/Motobug.bin")
    paths.add("Data/Sprites/Blueprint/Platform.bin")
    # -- Data/Sprites/CPZ/
    paths.add("Data/Sprites/CPZ/AmoebaDroid.bin")
    paths.add("Data/Sprites/CPZ/CPZParallax.bin")
    paths.add("Data/Sprites/CPZ/CaterkillerJr.bin")
    paths.add("Data/Sprites/CPZ/ChemPool.bin")
    paths.add("Data/Sprites/CPZ/ChemicalBall.bin")
    paths.add("Data/Sprites/CPZ/DNARiser.bin")
    paths.add("Data/Sprites/CPZ/Decoration.bin")
    paths.add("Data/Sprites/CPZ/Grabber.bin")
    paths.add("Data/Sprites/CPZ/MBMKnux.bin")
    paths.add("Data/Sprites/CPZ/MBMMighty.bin")
    paths.add("Data/Sprites/CPZ/MBMRay.bin")
    paths.add("Data/Sprites/CPZ/MBMSonic.bin")
    paths.add("Data/Sprites/CPZ/MBMTails.bin")
    paths.add("Data/Sprites/CPZ/Objects.gif")
    paths.add("Data/Sprites/CPZ/OneWayDoor.bin")
    paths.add("Data/Sprites/CPZ/Particles.bin")
    paths.add("Data/Sprites/CPZ/Platform.bin")
    paths.add("Data/Sprites/CPZ/Shutter.bin")
    paths.add("Data/Sprites/CPZ/SpeedBooster.bin")
    paths.add("Data/Sprites/CPZ/Spiny.bin")
    paths.add("Data/Sprites/CPZ/Springboard.bin")
    paths.add("Data/Sprites/CPZ/Staircase.bin")
    paths.add("Data/Sprites/CPZ/StickyPlatform.bin")
    paths.add("Data/Sprites/CPZ/Sweep.bin")
    paths.add("Data/Sprites/CPZ/Syringe.bin")
    paths.add("Data/Sprites/CPZ/TubeSpring.bin")
    paths.add("Data/Sprites/CPZ/TwistedTubes.bin")
    # -- Data/Sprites/Credits/
    paths.add("Data/Sprites/Credits/AnimalHBH.bin")
    paths.add("Data/Sprites/Credits/Silhouettes.bin")
    paths.add("Data/Sprites/Credits/TryAgain.bin")
    # -- Data/Sprites/Cutscene/
    paths.add("Data/Sprites/Cutscene/DamagedKing.bin")
    paths.add("Data/Sprites/Cutscene/Emeralds.bin")
    paths.add("Data/Sprites/Cutscene/HBHPile.bin")
    paths.add("Data/Sprites/Cutscene/KingTMZ2.bin")
    # -- Data/Sprites/Eggman/
    paths.add("Data/Sprites/Eggman/EggMobile.bin")
    paths.add("Data/Sprites/Eggman/EggmanAll.bin")
    paths.add("Data/Sprites/Eggman/EggmanCPZ.bin")
    paths.add("Data/Sprites/Eggman/EggmanFBZ.bin")
    paths.add("Data/Sprites/Eggman/EggmanGHZCutt.bin")
    paths.add("Data/Sprites/Eggman/EggmanHCZ1.bin")
    paths.add("Data/Sprites/Eggman/EggmanHCZ2.bin")
    paths.add("Data/Sprites/Eggman/EggmanMMZ1.bin")
    paths.add("Data/Sprites/Eggman/EggmanMMZ2.bin")
    paths.add("Data/Sprites/Eggman/EggmanOOZ.bin")
    paths.add("Data/Sprites/Eggman/EggmanPSZ.bin")
    paths.add("Data/Sprites/Eggman/EggmanTMZ.bin")
    # -- Data/Sprites/FBZ/
    paths.add("Data/Sprites/FBZ/AniTiles.gif")
    paths.add("Data/Sprites/FBZ/BigSqueeze.bin")
    paths.add("Data/Sprites/FBZ/Blaster.bin")
    paths.add("Data/Sprites/FBZ/Button.bin")
    paths.add("Data/Sprites/FBZ/Clucker.bin")
    paths.add("Data/Sprites/FBZ/Crane.bin")
    paths.add("Data/Sprites/FBZ/Current.bin")
    paths.add("Data/Sprites/FBZ/Decoration.bin")
    paths.add("Data/Sprites/FBZ/ElectroMagnet.bin")
    paths.add("Data/Sprites/FBZ/FBZFan.bin")
    paths.add("Data/Sprites/FBZ/FBZParallax.bin")
    paths.add("Data/Sprites/FBZ/FlameSpring.bin")
    paths.add("Data/Sprites/FBZ/HangPoint.bin")
    paths.add("Data/Sprites/FBZ/LightBarrier.bin")
    paths.add("Data/Sprites/FBZ/MagSpikeBall.bin")
    paths.add("Data/Sprites/FBZ/Mine.bin")
    paths.add("Data/Sprites/FBZ/Missile.bin")
    paths.add("Data/Sprites/FBZ/Platform.bin")
    paths.add("Data/Sprites/FBZ/Propeller.bin")
    paths.add("Data/Sprites/FBZ/SpiderMobile.bin")
    paths.add("Data/Sprites/FBZ/Spikes.bin")
    paths.add("Data/Sprites/FBZ/Storm.bin")
    paths.add("Data/Sprites/FBZ/Technosqueek.bin")
    paths.add("Data/Sprites/FBZ/TetherBall.bin")
    paths.add("Data/Sprites/FBZ/Trash.bin")
    paths.add("Data/Sprites/FBZ/TubeSpring.bin")
    paths.add("Data/Sprites/FBZ/Tuesday.bin")
    paths.add("Data/Sprites/FBZ/TwistingDoor.bin")
    # -- Data/Sprites/GHZ/
    paths.add("Data/Sprites/GHZ/AniTiles.gif")
    paths.add("Data/Sprites/GHZ/Batbrain.bin")
    paths.add("Data/Sprites/GHZ/Bridge.bin")
    paths.add("Data/Sprites/GHZ/BuzzBomber.bin")
    paths.add("Data/Sprites/GHZ/CheckerBall.bin")
    paths.add("Data/Sprites/GHZ/Chopper.bin")
    paths.add("Data/Sprites/GHZ/Crabmeat.bin")
    paths.add("Data/Sprites/GHZ/DDWrecker.bin")
    paths.add("Data/Sprites/GHZ/DERobot.bin")
    paths.add("Data/Sprites/GHZ/Decoration.bin")
    paths.add("Data/Sprites/GHZ/Fireball.bin")
    paths.add("Data/Sprites/GHZ/Motobug.bin")
    paths.add("Data/Sprites/GHZ/Newtron.bin")
    paths.add("Data/Sprites/GHZ/Platform.bin")
    paths.add("Data/Sprites/GHZ/SpikeLog.bin")
    paths.add("Data/Sprites/GHZ/Splats.bin")
    paths.add("Data/Sprites/GHZ/ZipLine.bin")
    # -- Data/Sprites/GHZCutscene/
    paths.add("Data/Sprites/GHZCutscene/Claw.bin")
    paths.add("Data/Sprites/GHZCutscene/Platform.bin")
    # -- Data/Sprites/Global/
    paths.add("Data/Sprites/Global/Animals.bin")
    paths.add("Data/Sprites/Global/Announcer.bin")
    paths.add("Data/Sprites/Global/Dust.bin")
    paths.add("Data/Sprites/Global/HUD.bin")
    paths.add("Data/Sprites/Global/ItemBox.bin")
    paths.add("Data/Sprites/Global/PhantomRuby.bin")
    paths.add("Data/Sprites/Global/PlaneSwitch.bin")
    paths.add("Data/Sprites/Global/Ring.bin")
    paths.add("Data/Sprites/Global/Shields.bin")
    paths.add("Data/Sprites/Global/SignPost.bin")
    paths.add("Data/Sprites/Global/SpeedGate.bin")
    paths.add("Data/Sprites/Global/Spikes.bin")
    paths.add("Data/Sprites/Global/Springs.bin")
    paths.add("Data/Sprites/Global/StarPost.bin")
    paths.add("Data/Sprites/Global/SuperButtons.bin")
    paths.add("Data/Sprites/Global/TicMark.bin")
    paths.add("Data/Sprites/Global/TitleCard.bin")
    paths.add("Data/Sprites/Global/Water.bin")
    # -- Data/Sprites/HCZ/
    paths.add("Data/Sprites/HCZ/AniTiles.gif")
    paths.add("Data/Sprites/HCZ/AniTiles2.gif")
    paths.add("Data/Sprites/HCZ/AniTiles3.gif")
    paths.add("Data/Sprites/HCZ/BigBubble.bin")
    paths.add("Data/Sprites/HCZ/Blastoid.bin")
    paths.add("Data/Sprites/HCZ/BreakBar.bin")
    paths.add("Data/Sprites/HCZ/Bridge.bin")
    paths.add("Data/Sprites/HCZ/Buggernaut.bin")
    paths.add("Data/Sprites/HCZ/Button.bin")
    paths.add("Data/Sprites/HCZ/ButtonDoor.bin")
    paths.add("Data/Sprites/HCZ/Decoration.bin")
    paths.add("Data/Sprites/HCZ/DiveEggman.bin")
    paths.add("Data/Sprites/HCZ/Fan.bin")
    paths.add("Data/Sprites/HCZ/Gondola.bin")
    paths.add("Data/Sprites/HCZ/HandLauncher.bin")
    paths.add("Data/Sprites/HCZ/HangConveyor.bin")
    paths.add("Data/Sprites/HCZ/Jawz.bin")
    paths.add("Data/Sprites/HCZ/Jellygnite.bin")
    paths.add("Data/Sprites/HCZ/LaundroMobile.bin")
    paths.add("Data/Sprites/HCZ/MegaChopper.bin")
    paths.add("Data/Sprites/HCZ/Platform.bin")
    paths.add("Data/Sprites/HCZ/Pointdexter.bin")
    paths.add("Data/Sprites/HCZ/PullChain.bin")
    paths.add("Data/Sprites/HCZ/ScrewMobile.bin")
    paths.add("Data/Sprites/HCZ/Spear.bin")
    paths.add("Data/Sprites/HCZ/TurboSpiker.bin")
    paths.add("Data/Sprites/HCZ/Wake.bin")
    paths.add("Data/Sprites/HCZ/WaterGush.bin")
    # -- Data/Sprites/HPZ/
    paths.add("Data/Sprites/HPZ/Jellygnite.bin")
    # -- Data/Sprites/LRZ1/
    paths.add("Data/Sprites/LRZ1/Bridge.bin")
    paths.add("Data/Sprites/LRZ1/BuckwildBall.bin")
    paths.add("Data/Sprites/LRZ1/Button.bin")
    paths.add("Data/Sprites/LRZ1/ButtonDoor.bin")
    paths.add("Data/Sprites/LRZ1/Drillerdroid.bin")
    paths.add("Data/Sprites/LRZ1/Fireworm.bin")
    paths.add("Data/Sprites/LRZ1/Iwamodoki.bin")
    paths.add("Data/Sprites/LRZ1/LRZFireball.bin")
    paths.add("Data/Sprites/LRZ1/LRZRockPile.bin")
    paths.add("Data/Sprites/LRZ1/LavaFall.bin")
    paths.add("Data/Sprites/LRZ1/LavaGeyser.bin")
    paths.add("Data/Sprites/LRZ1/OrbitSpike.bin")
    paths.add("Data/Sprites/LRZ1/Particles.bin")
    paths.add("Data/Sprites/LRZ1/Platform.bin")
    paths.add("Data/Sprites/LRZ1/Rexon.bin")
    paths.add("Data/Sprites/LRZ1/RockDrill.bin")
    paths.add("Data/Sprites/LRZ1/Stalactite.bin")
    paths.add("Data/Sprites/LRZ1/Toxomister.bin")
    paths.add("Data/Sprites/LRZ1/TurretSwitch.bin")
    paths.add("Data/Sprites/LRZ1/WalkerLegs.bin")
    # -- Data/Sprites/LRZ2/
    paths.add("Data/Sprites/LRZ2/Button.bin")
    paths.add("Data/Sprites/LRZ2/ButtonDoor.bin")
    paths.add("Data/Sprites/LRZ2/Fireworm.bin")
    paths.add("Data/Sprites/LRZ2/Flamethrower.bin")
    paths.add("Data/Sprites/LRZ2/Iwamodoki.bin")
    paths.add("Data/Sprites/LRZ2/LRZConvControl.bin")
    paths.add("Data/Sprites/LRZ2/LRZConvDropper.bin")
    paths.add("Data/Sprites/LRZ2/LRZConvItem.bin")
    paths.add("Data/Sprites/LRZ2/LRZConvSwitch.bin")
    paths.add("Data/Sprites/LRZ2/LRZConveyor.bin")
    paths.add("Data/Sprites/LRZ2/LRZParallax.bin")
    paths.add("Data/Sprites/LRZ2/LRZRockPile.bin")
    paths.add("Data/Sprites/LRZ2/LRZSpikeBall.bin")
    paths.add("Data/Sprites/LRZ2/LavaGeyser.bin")
    paths.add("Data/Sprites/LRZ2/OrbitSpike.bin")
    paths.add("Data/Sprites/LRZ2/Particles.bin")
    paths.add("Data/Sprites/LRZ2/Platform.bin")
    paths.add("Data/Sprites/LRZ2/Rexon.bin")
    paths.add("Data/Sprites/LRZ2/Toxomister.bin")
    paths.add("Data/Sprites/LRZ2/Turbine.bin")
    paths.add("Data/Sprites/LRZ2/WalkerLegs.bin")
    # -- Data/Sprites/LRZ3/
    paths.add("Data/Sprites/LRZ3/Claw.bin")
    paths.add("Data/Sprites/LRZ3/Emerald.bin")
    paths.add("Data/Sprites/LRZ3/Flamethrower.bin")
    paths.add("Data/Sprites/LRZ3/HeavyKing.bin")
    paths.add("Data/Sprites/LRZ3/HeavyRider.bin")
    paths.add("Data/Sprites/LRZ3/SkyTeleporter.bin")
    paths.add("Data/Sprites/LRZ3/ThoughtBubble.bin")
    # -- Data/Sprites/LSelect/
    paths.add("Data/Sprites/LSelect/Icons.bin")
    paths.add("Data/Sprites/LSelect/Text.bin")
    # -- Data/Sprites/Logos/
    paths.add("Data/Sprites/Logos/Logos.bin")
    # -- Data/Sprites/MMZ/
    paths.add("Data/Sprites/MMZ/AniTiles.gif")
    paths.add("Data/Sprites/MMZ/BladePole.bin")
    paths.add("Data/Sprites/MMZ/Button.bin")
    paths.add("Data/Sprites/MMZ/BuzzSaw.bin")
    paths.add("Data/Sprites/MMZ/ConveyorWheel.bin")
    paths.add("Data/Sprites/MMZ/Decoration.bin")
    paths.add("Data/Sprites/MMZ/EggPistonsMKII.bin")
    paths.add("Data/Sprites/MMZ/Gachapandora.bin")
    paths.add("Data/Sprites/MMZ/MMZWheel.bin")
    paths.add("Data/Sprites/MMZ/MatryoshkaBom.bin")
    paths.add("Data/Sprites/MMZ/MechaBu.bin")
    paths.add("Data/Sprites/MMZ/OneWayDoor.bin")
    paths.add("Data/Sprites/MMZ/Platform.bin")
    paths.add("Data/Sprites/MMZ/PohBee.bin")
    paths.add("Data/Sprites/MMZ/RPlaneShifter.bin")
    paths.add("Data/Sprites/MMZ/RTeleporter.bin")
    paths.add("Data/Sprites/MMZ/Scarab.bin")
    paths.add("Data/Sprites/MMZ/SeeSaw.bin")
    paths.add("Data/Sprites/MMZ/SizeLaser.bin")
    paths.add("Data/Sprites/MMZ/SpikeCorridor.bin")
    # -- Data/Sprites/MSZ/
    paths.add("Data/Sprites/MSZ/AniTiles.gif")
    paths.add("Data/Sprites/MSZ/Armadiloid.bin")
    paths.add("Data/Sprites/MSZ/BarStool.bin")
    paths.add("Data/Sprites/MSZ/Bumpalo.bin")
    paths.add("Data/Sprites/MSZ/Cactula.bin")
    paths.add("Data/Sprites/MSZ/Decoration.bin")
    paths.add("Data/Sprites/MSZ/Ending.bin")
    paths.add("Data/Sprites/MSZ/Flipper.bin")
    paths.add("Data/Sprites/MSZ/HeavyMystic.bin")
    paths.add("Data/Sprites/MSZ/HonkyTonk.bin")
    paths.add("Data/Sprites/MSZ/LightBulb.bin")
    paths.add("Data/Sprites/MSZ/MSZParallax.bin")
    paths.add("Data/Sprites/MSZ/PaintingEyes.bin")
    paths.add("Data/Sprites/MSZ/Pinata.bin")
    paths.add("Data/Sprites/MSZ/Pistol.bin")
    paths.add("Data/Sprites/MSZ/Platform.bin")
    paths.add("Data/Sprites/MSZ/RattleKiller.bin")
    paths.add("Data/Sprites/MSZ/Rattlekiller.bin")
    paths.add("Data/Sprites/MSZ/Rogues.bin")
    paths.add("Data/Sprites/MSZ/RollerMKII.bin")
    paths.add("Data/Sprites/MSZ/RotatingSpikes.bin")
    paths.add("Data/Sprites/MSZ/SandCollapse.bin")
    paths.add("Data/Sprites/MSZ/Sandworm.bin")
    paths.add("Data/Sprites/MSZ/SeeSaw.bin")
    paths.add("Data/Sprites/MSZ/Seltzer.bin")
    paths.add("Data/Sprites/MSZ/SideBarrel.bin")
    paths.add("Data/Sprites/MSZ/SwingRope.bin")
    paths.add("Data/Sprites/MSZ/Tornado.bin")
    paths.add("Data/Sprites/MSZ/Train.bin")
    paths.add("Data/Sprites/MSZ/Vultron.bin")
    # -- Data/Sprites/OOZ/
    paths.add("Data/Sprites/OOZ/AniTiles.gif")
    paths.add("Data/Sprites/OOZ/Aquis.bin")
    paths.add("Data/Sprites/OOZ/BallCannon.bin")
    paths.add("Data/Sprites/OOZ/Fan.bin")
    paths.add("Data/Sprites/OOZ/Flames.bin")
    paths.add("Data/Sprites/OOZ/Hatch.bin")
    paths.add("Data/Sprites/OOZ/MegaOctus.bin")
    paths.add("Data/Sprites/OOZ/MeterDroid.bin")
    paths.add("Data/Sprites/OOZ/OOZParallax.bin")
    paths.add("Data/Sprites/OOZ/Octus.bin")
    paths.add("Data/Sprites/OOZ/Platform.bin")
    paths.add("Data/Sprites/OOZ/PullSwitch.bin")
    paths.add("Data/Sprites/OOZ/PushSpring.bin")
    paths.add("Data/Sprites/OOZ/Smog.gif")
    paths.add("Data/Sprites/OOZ/Sol.bin")
    paths.add("Data/Sprites/OOZ/Splash.bin")
    paths.add("Data/Sprites/OOZ/Valve.bin")
    # -- Data/Sprites/PSZ1/
    paths.add("Data/Sprites/PSZ1/AniTiles.gif")
    paths.add("Data/Sprites/PSZ1/AniTiles2.gif")
    paths.add("Data/Sprites/PSZ1/AniTiles3.gif")
    paths.add("Data/Sprites/PSZ1/Crate.bin")
    paths.add("Data/Sprites/PSZ1/DoorTrigger.bin")
    paths.add("Data/Sprites/PSZ1/Dragonfly.bin")
    paths.add("Data/Sprites/PSZ1/FrostThrower.bin")
    paths.add("Data/Sprites/PSZ1/Ice.bin")
    paths.add("Data/Sprites/PSZ1/IceBomba.bin")
    paths.add("Data/Sprites/PSZ1/JuggleSaw.bin")
    paths.add("Data/Sprites/PSZ1/PSZDoor.bin")
    paths.add("Data/Sprites/PSZ1/PSZLauncher.bin")
    paths.add("Data/Sprites/PSZ1/PaperRoller.bin")
    paths.add("Data/Sprites/PSZ1/Petal.bin")
    paths.add("Data/Sprites/PSZ1/Platform.bin")
    paths.add("Data/Sprites/PSZ1/Press.bin")
    paths.add("Data/Sprites/PSZ1/PrintBlock.bin")
    paths.add("Data/Sprites/PSZ1/SP500.bin")
    paths.add("Data/Sprites/PSZ1/SP500MkII.bin")
    paths.add("Data/Sprites/PSZ1/Shiversaw.bin")
    paths.add("Data/Sprites/PSZ1/Splats.bin")
    paths.add("Data/Sprites/PSZ1/Turntable.bin")
    paths.add("Data/Sprites/PSZ1/Woodrow.bin")
    # -- Data/Sprites/PSZ2/
    paths.add("Data/Sprites/PSZ2/AniTiles.gif")
    paths.add("Data/Sprites/PSZ2/AniTiles2.gif")
    paths.add("Data/Sprites/PSZ2/ControlPanel.bin")
    paths.add("Data/Sprites/PSZ2/Dragonfly.bin")
    paths.add("Data/Sprites/PSZ2/FrostThrower.bin")
    paths.add("Data/Sprites/PSZ2/Ice.bin")
    paths.add("Data/Sprites/PSZ2/IceBomba.bin")
    paths.add("Data/Sprites/PSZ2/IceSpring.bin")
    paths.add("Data/Sprites/PSZ2/JuggleSaw.bin")
    paths.add("Data/Sprites/PSZ2/Petal.bin")
    paths.add("Data/Sprites/PSZ2/Platform.bin")
    paths.add("Data/Sprites/PSZ2/Shinobi.bin")
    paths.add("Data/Sprites/PSZ2/Shuriken.bin")
    paths.add("Data/Sprites/PSZ2/Snowflakes.bin")
    paths.add("Data/Sprites/PSZ2/Spikes.bin")
    paths.add("Data/Sprites/PSZ2/WoodChipper.bin")
    paths.add("Data/Sprites/PSZ2/Woodrow.bin")
    # -- Data/Sprites/Phantom/
    paths.add("Data/Sprites/Phantom/AlertScreen.bin")
    paths.add("Data/Sprites/Phantom/Decoration.bin")
    paths.add("Data/Sprites/Phantom/EggMissile.bin")
    paths.add("Data/Sprites/Phantom/EggShield.bin")
    paths.add("Data/Sprites/Phantom/EscapeCar.bin")
    paths.add("Data/Sprites/Phantom/Flames.bin")
    paths.add("Data/Sprites/Phantom/KleptoMobile.bin")
    paths.add("Data/Sprites/Phantom/PhantomEgg.bin")
    paths.add("Data/Sprites/Phantom/PhantomGunner.bin")
    paths.add("Data/Sprites/Phantom/PhantomHand.bin")
    paths.add("Data/Sprites/Phantom/PhantomKing.bin")
    paths.add("Data/Sprites/Phantom/PhantomMystic.bin")
    paths.add("Data/Sprites/Phantom/PhantomRider.bin")
    paths.add("Data/Sprites/Phantom/PhantomShinobi.bin")
    paths.add("Data/Sprites/Phantom/Portal.bin")
    paths.add("Data/Sprites/Phantom/Sky.gif")
    # -- Data/Sprites/Players/
    paths.add("Data/Sprites/Players/CTailSprite.bin")
    paths.add("Data/Sprites/Players/ChibiKnux.bin")
    paths.add("Data/Sprites/Players/ChibiMighty.bin")
    paths.add("Data/Sprites/Players/ChibiRay.bin")
    paths.add("Data/Sprites/Players/ChibiSonic.bin")
    paths.add("Data/Sprites/Players/ChibiTails.bin")
    paths.add("Data/Sprites/Players/Continue.bin")
    paths.add("Data/Sprites/Players/CutsceneCPZ.bin")
    paths.add("Data/Sprites/Players/Knux.bin")
    paths.add("Data/Sprites/Players/KnuxCutsceneAIZ.bin")
    paths.add("Data/Sprites/Players/KnuxCutsceneHPZ.bin")
    paths.add("Data/Sprites/Players/Mighty.bin")
    paths.add("Data/Sprites/Players/Ray.bin")
    paths.add("Data/Sprites/Players/Sonic.bin")
    paths.add("Data/Sprites/Players/SuperSonic.bin")
    paths.add("Data/Sprites/Players/TailSprite.bin")
    paths.add("Data/Sprites/Players/Tails.bin")
    # -- Data/Sprites/Puyo/
    paths.add("Data/Sprites/Puyo/Combos.bin")
    paths.add("Data/Sprites/Puyo/PuyoBeans.bin")
    paths.add("Data/Sprites/Puyo/PuyoIndicator.bin")
    paths.add("Data/Sprites/Puyo/PuyoUI.bin")
    # -- Data/Sprites/SBZ/
    paths.add("Data/Sprites/SBZ/Platform.bin")
    # -- Data/Sprites/SPZ1/
    paths.add("Data/Sprites/SPZ1/AniTiles.gif")
    paths.add("Data/Sprites/SPZ1/Boss.bin")
    paths.add("Data/Sprites/SPZ1/Canista.bin")
    paths.add("Data/Sprites/SPZ1/CircleBumper.bin")
    paths.add("Data/Sprites/SPZ1/Clapperboard.bin")
    paths.add("Data/Sprites/SPZ1/Decoration.bin")
    paths.add("Data/Sprites/SPZ1/DirectorChair.bin")
    paths.add("Data/Sprites/SPZ1/FilmProjector.bin")
    paths.add("Data/Sprites/SPZ1/FilmReel.bin")
    paths.add("Data/Sprites/SPZ1/LED.bin")
    paths.add("Data/Sprites/SPZ1/ManholeCover.bin")
    paths.add("Data/Sprites/SPZ1/MicDrop.bin")
    paths.add("Data/Sprites/SPZ1/Platform.bin")
    paths.add("Data/Sprites/SPZ1/PopcornMachine.bin")
    paths.add("Data/Sprites/SPZ1/RockemSockem.bin")
    paths.add("Data/Sprites/SPZ1/SPZParallax.bin")
    paths.add("Data/Sprites/SPZ1/ShopWindow.bin")
    paths.add("Data/Sprites/SPZ1/Shutterbug.bin")
    paths.add("Data/Sprites/SPZ1/SpinSign.bin")
    paths.add("Data/Sprites/SPZ1/TVVan.bin")
    paths.add("Data/Sprites/SPZ1/Tubinaut.bin")
    # -- Data/Sprites/SPZ2/
    paths.add("Data/Sprites/SPZ2/AniTiles1.gif")
    paths.add("Data/Sprites/SPZ2/AniTiles2.gif")
    paths.add("Data/Sprites/SPZ2/CableWarp.bin")
    paths.add("Data/Sprites/SPZ2/Canista.bin")
    paths.add("Data/Sprites/SPZ2/CircleBumper.bin")
    paths.add("Data/Sprites/SPZ2/Clapperboard.bin")
    paths.add("Data/Sprites/SPZ2/EggTV.bin")
    paths.add("Data/Sprites/SPZ2/FilmReel.bin")
    paths.add("Data/Sprites/SPZ2/Funnel.bin")
    paths.add("Data/Sprites/SPZ2/GreenScreen.bin")
    paths.add("Data/Sprites/SPZ2/Letterboard.bin")
    paths.add("Data/Sprites/SPZ2/LottoBall.bin")
    paths.add("Data/Sprites/SPZ2/LottoMachine.bin")
    paths.add("Data/Sprites/SPZ2/LoveTester.bin")
    paths.add("Data/Sprites/SPZ2/MicDrop.bin")
    paths.add("Data/Sprites/SPZ2/PathInverter.bin")
    paths.add("Data/Sprites/SPZ2/PimPom.bin")
    paths.add("Data/Sprites/SPZ2/Platform.bin")
    paths.add("Data/Sprites/SPZ2/RockemSockem.bin")
    paths.add("Data/Sprites/SPZ2/Shutterbug.bin")
    paths.add("Data/Sprites/SPZ2/TVFlyingBattery.bin")
    paths.add("Data/Sprites/SPZ2/TVPole.bin")
    paths.add("Data/Sprites/SPZ2/Tubinaut.bin")
    paths.add("Data/Sprites/SPZ2/WeatherMobile.bin")
    # -- Data/Sprites/SSZ/
    paths.add("Data/Sprites/SSZ/Decoration.bin")
    paths.add("Data/Sprites/SSZ/HiLoSign.bin")
    paths.add("Data/Sprites/SSZ/RTeleporter.bin")
    # -- Data/Sprites/SSZ1/
    paths.add("Data/Sprites/SSZ1/Beanstalk.bin")
    paths.add("Data/Sprites/SSZ1/Constellation.bin")
    paths.add("Data/Sprites/SSZ1/Dango.bin")
    paths.add("Data/Sprites/SSZ1/Fireflies.bin")
    paths.add("Data/Sprites/SSZ1/FlowerPod.bin")
    paths.add("Data/Sprites/SSZ1/Hotaru.bin")
    paths.add("Data/Sprites/SSZ1/HotaruHiWatt.bin")
    paths.add("Data/Sprites/SSZ1/HotaruMKII.bin")
    paths.add("Data/Sprites/SSZ1/JunctionWheel.bin")
    paths.add("Data/Sprites/SSZ1/Kabasira.bin")
    paths.add("Data/Sprites/SSZ1/Kanabun.bin")
    paths.add("Data/Sprites/SSZ1/LaunchSpring.bin")
    paths.add("Data/Sprites/SSZ1/MSHologram.bin")
    paths.add("Data/Sprites/SSZ1/Plants.bin")
    paths.add("Data/Sprites/SSZ1/Platform.bin")
    paths.add("Data/Sprites/SSZ1/RotatingSpikes.bin")
    paths.add("Data/Sprites/SSZ1/SDashWheel.bin")
    paths.add("Data/Sprites/SSZ1/Spark.bin")
    paths.add("Data/Sprites/SSZ1/SpeedBooster.bin")
    paths.add("Data/Sprites/SSZ1/SpikeBall.bin")
    paths.add("Data/Sprites/SSZ1/SpikeFlail.bin")
    paths.add("Data/Sprites/SSZ1/TTSparkle.bin")
    paths.add("Data/Sprites/SSZ1/TimePost.bin")
    # -- Data/Sprites/SSZ2/
    paths.add("Data/Sprites/SSZ2/Bungee.bin")
    paths.add("Data/Sprites/SSZ2/Dango.bin")
    paths.add("Data/Sprites/SSZ2/Firework.bin")
    paths.add("Data/Sprites/SSZ2/Hotaru.bin")
    paths.add("Data/Sprites/SSZ2/HotaruMKII.bin")
    paths.add("Data/Sprites/SSZ2/Kabasira.bin")
    paths.add("Data/Sprites/SSZ2/Kanabun.bin")
    paths.add("Data/Sprites/SSZ2/LaunchSpring.bin")
    paths.add("Data/Sprites/SSZ2/MSFactory.bin")
    paths.add("Data/Sprites/SSZ2/MSHologram.bin")
    paths.add("Data/Sprites/SSZ2/MSPanel.bin")
    paths.add("Data/Sprites/SSZ2/MetalSonic.bin")
    paths.add("Data/Sprites/SSZ2/Platform.bin")
    paths.add("Data/Sprites/SSZ2/SilverSonic.bin")
    paths.add("Data/Sprites/SSZ2/Spark.bin")
    paths.add("Data/Sprites/SSZ2/SpeedBooster.bin")
    paths.add("Data/Sprites/SSZ2/SpikeBall.bin")
    # -- Data/Sprites/Special/
    paths.add("Data/Sprites/Special/Results.bin")
    # -- Data/Sprites/SpecialBS/
    paths.add("Data/Sprites/SpecialBS/Globe.bin")
    paths.add("Data/Sprites/SpecialBS/HUD.bin")
    paths.add("Data/Sprites/SpecialBS/Horizon.bin")
    paths.add("Data/Sprites/SpecialBS/Knuckles.bin")
    paths.add("Data/Sprites/SpecialBS/Mighty.bin")
    paths.add("Data/Sprites/SpecialBS/Ray.bin")
    paths.add("Data/Sprites/SpecialBS/Ring.bin")
    paths.add("Data/Sprites/SpecialBS/Sonic.bin")
    paths.add("Data/Sprites/SpecialBS/StageObjects.bin")
    paths.add("Data/Sprites/SpecialBS/Tails.bin")
    # -- Data/Sprites/SpecialUFO/
    paths.add("Data/Sprites/SpecialUFO/Dust.bin")
    paths.add("Data/Sprites/SpecialUFO/HUD.bin")
    paths.add("Data/Sprites/SpecialUFO/Items.bin")
    paths.add("Data/Sprites/SpecialUFO/Plasma.gif")
    paths.add("Data/Sprites/SpecialUFO/Spheres.bin")
    paths.add("Data/Sprites/SpecialUFO/Water.gif")
    # -- Data/Sprites/TMZ1/
    paths.add("Data/Sprites/TMZ1/AniTiles.gif")
    paths.add("Data/Sprites/TMZ1/BallHog.bin")
    paths.add("Data/Sprites/TMZ1/Button.bin")
    paths.add("Data/Sprites/TMZ1/CrashTest.bin")
    paths.add("Data/Sprites/TMZ1/CrimsonEye.bin")
    paths.add("Data/Sprites/TMZ1/Decoration.bin")
    paths.add("Data/Sprites/TMZ1/DynTiles.gif")
    paths.add("Data/Sprites/TMZ1/FlasherMKII.bin")
    paths.add("Data/Sprites/TMZ1/GymBar.bin")
    paths.add("Data/Sprites/TMZ1/JacobsLadder.bin")
    paths.add("Data/Sprites/TMZ1/LargeGear.bin")
    paths.add("Data/Sprites/TMZ1/MagnetSphere.bin")
    paths.add("Data/Sprites/TMZ1/MetalArm.bin")
    paths.add("Data/Sprites/TMZ1/MonarchBG.bin")
    paths.add("Data/Sprites/TMZ1/Platform.bin")
    paths.add("Data/Sprites/TMZ1/PopOut.bin")
    paths.add("Data/Sprites/TMZ1/Portal.bin")
    paths.add("Data/Sprites/TMZ1/SentryBug.bin")
    paths.add("Data/Sprites/TMZ1/TeeterTotter.bin")
    paths.add("Data/Sprites/TMZ1/TurboTurtle.bin")
    paths.add("Data/Sprites/TMZ1/WallBumper.bin")
    # -- Data/Sprites/Thanks/
    paths.add("Data/Sprites/Thanks/Decorations.bin")
    # -- Data/Sprites/Title/
    paths.add("Data/Sprites/Title/Background.bin")
    paths.add("Data/Sprites/Title/DemoMenu.bin")
    paths.add("Data/Sprites/Title/Electricity.bin")
    paths.add("Data/Sprites/Title/Logo.bin")
    paths.add("Data/Sprites/Title/PlusLogo.bin")
    paths.add("Data/Sprites/Title/Sonic.bin")
    # -- Data/Sprites/UI/
    paths.add("Data/Sprites/UI/ButtonLabel.bin")
    paths.add("Data/Sprites/UI/Buttons.bin")
    paths.add("Data/Sprites/UI/CreditsText.bin")
    paths.add("Data/Sprites/UI/DAGarden.bin")
    paths.add("Data/Sprites/UI/Diorama.bin")
    paths.add("Data/Sprites/UI/HeadingsEN.bin")
    paths.add("Data/Sprites/UI/HeadingsFR.bin")
    paths.add("Data/Sprites/UI/HeadingsGE.bin")
    paths.add("Data/Sprites/UI/HeadingsIT.bin")
    paths.add("Data/Sprites/UI/HeadingsJP.bin")
    paths.add("Data/Sprites/UI/HeadingsKO.bin")
    paths.add("Data/Sprites/UI/HeadingsSC.bin")
    paths.add("Data/Sprites/UI/HeadingsSP.bin")
    paths.add("Data/Sprites/UI/HeadingsTC.bin")
    paths.add("Data/Sprites/UI/MainIcons.bin")
    paths.add("Data/Sprites/UI/MedallionPanel.bin")
    paths.add("Data/Sprites/UI/Picture.bin")
    paths.add("Data/Sprites/UI/SaveSelect.bin")
    paths.add("Data/Sprites/UI/SmallFont.bin")
    paths.add("Data/Sprites/UI/TextEN.bin")
    paths.add("Data/Sprites/UI/TextFR.bin")
    paths.add("Data/Sprites/UI/TextGE.bin")
    paths.add("Data/Sprites/UI/TextIT.bin")
    paths.add("Data/Sprites/UI/TextJP.bin")
    paths.add("Data/Sprites/UI/TextKO.bin")
    paths.add("Data/Sprites/UI/TextSC.bin")
    paths.add("Data/Sprites/UI/TextSP.bin")
    paths.add("Data/Sprites/UI/TextTC.bin")
    paths.add("Data/Sprites/UI/UIElements.bin")
    paths.add("Data/Sprites/UI/WaitSpinner.bin")
    # Sheet GIFs derived from cached .bin sheet[] arrays. The hash matcher
    # silently drops any extras (Plus-DLC/Encore-encrypted), so
    # over-enumeration is safe. See docs/whole_game_asset_audit.md for
    # the per-bin sheet-list parse output.


    # Phase 3.0-prep++ derived sheet GIFs (parsed from extracted .bin sheets[]).
    paths.add("Data/Sprites/Blueprint/Objects.gif")
    paths.add("Data/Sprites/CPZ/AmoebaDroid.gif")
    paths.add("Data/Sprites/CPZ/ChemPool.gif")
    paths.add("Data/Sprites/CPZ/Enemies.gif")
    paths.add("Data/Sprites/CPZ/MBMKnux.gif")
    paths.add("Data/Sprites/CPZ/MBMSonic.gif")
    paths.add("Data/Sprites/CPZ/MBMTails.gif")
    paths.add("Data/Sprites/Credits/TryAgain.gif")
    paths.add("Data/Sprites/Cutscene/Emeralds.gif")
    paths.add("Data/Sprites/Eggman/EggMobile.gif")
    paths.add("Data/Sprites/Eggman/EggmanButton.gif")
    paths.add("Data/Sprites/Eggman/EggmanGHZCutt.gif")
    paths.add("Data/Sprites/Eggman/EggmanGacha.gif")
    paths.add("Data/Sprites/Eggman/EggmanMBM.gif")
    paths.add("Data/Sprites/Eggman/EggmanPanel.gif")
    paths.add("Data/Sprites/Eggman/EggmanPopup.gif")
    paths.add("Data/Sprites/Eggman/EggmanStand1.gif")
    paths.add("Data/Sprites/Eggman/EggmanStand2.gif")
    paths.add("Data/Sprites/Eggman/EggmanTF.gif")
    paths.add("Data/Sprites/Eggman/EggmanTMZ.gif")
    paths.add("Data/Sprites/FBZ/BigSqueeze.gif")
    paths.add("Data/Sprites/FBZ/Enemies.gif")
    paths.add("Data/Sprites/FBZ/FGClouds.gif")
    paths.add("Data/Sprites/FBZ/Objects-Classic.gif")
    paths.add("Data/Sprites/FBZ/SpiderMobile.gif")
    paths.add("Data/Sprites/GHZ/Ball.gif")
    paths.add("Data/Sprites/GHZ/DDWrecker.gif")
    paths.add("Data/Sprites/GHZ/DERobot.gif")
    paths.add("Data/Sprites/Global/Animals.gif")
    paths.add("Data/Sprites/Global/Announcer.gif")
    paths.add("Data/Sprites/Global/Display.gif")
    paths.add("Data/Sprites/Global/Explosions.gif")
    paths.add("Data/Sprites/Global/PhantomRuby.gif")
    paths.add("Data/Sprites/Global/Shields.gif")
    paths.add("Data/Sprites/Global/SuperButtons.gif")
    paths.add("Data/Sprites/Global/Water.gif")
    paths.add("Data/Sprites/HCZ/Boss.gif")
    paths.add("Data/Sprites/HCZ/Enemies.gif")
    paths.add("Data/Sprites/HPZ/Objects-Classic.gif")
    paths.add("Data/Sprites/LRZ2/AniTiles.gif")
    paths.add("Data/Sprites/LRZ3/HeavyKing.gif")
    paths.add("Data/Sprites/LRZ3/HeavyKing2.gif")
    paths.add("Data/Sprites/LRZ3/HeavyRider.gif")
    paths.add("Data/Sprites/LRZ3/Teleporter.gif")
    paths.add("Data/Sprites/MMZ/Boss.gif")
    paths.add("Data/Sprites/MMZ/Enemies.gif")
    paths.add("Data/Sprites/MMZ/Objects-Classic.gif")
    paths.add("Data/Sprites/MSZ/Enemies.gif")
    paths.add("Data/Sprites/MSZ/HeavyMystic.gif")
    paths.add("Data/Sprites/MSZ/OOZPeek.gif")
    paths.add("Data/Sprites/MSZ/Objects1.gif")
    paths.add("Data/Sprites/MSZ/Rogues.gif")
    paths.add("Data/Sprites/MSZ/Train.gif")
    paths.add("Data/Sprites/OOZ/Enemies.gif")
    paths.add("Data/Sprites/OOZ/Flames.gif")
    paths.add("Data/Sprites/OOZ/MegaOctus.gif")
    paths.add("Data/Sprites/OOZ/MeterDroid.gif")
    paths.add("Data/Sprites/OOZ/Objects.gif")
    paths.add("Data/Sprites/PSZ1/Boss.gif")
    paths.add("Data/Sprites/PSZ1/Enemies.gif")
    paths.add("Data/Sprites/PSZ2/Boss.gif")
    paths.add("Data/Sprites/PSZ2/Boss2.gif")
    paths.add("Data/Sprites/PSZ2/Enemies.gif")
    paths.add("Data/Sprites/PSZ2/Objects-Temp.gif")
    paths.add("Data/Sprites/Phantom/Flames.gif")
    paths.add("Data/Sprites/Phantom/KleptoMobile.gif")
    paths.add("Data/Sprites/Phantom/Objects.gif")
    paths.add("Data/Sprites/Phantom/PhantomKing.gif")
    paths.add("Data/Sprites/Phantom/PhantomKing2.gif")
    paths.add("Data/Sprites/Phantom/PhantomKing3.gif")
    paths.add("Data/Sprites/Phantom/PhantomKing4.gif")
    paths.add("Data/Sprites/Phantom/Portal1.gif")
    paths.add("Data/Sprites/Phantom/Portal2.gif")
    paths.add("Data/Sprites/Players/ChibiKnux.gif")
    paths.add("Data/Sprites/Players/ChibiKnux2.gif")
    paths.add("Data/Sprites/Players/ChibiSonic.gif")
    paths.add("Data/Sprites/Players/ChibiTails.gif")
    paths.add("Data/Sprites/Players/ChibiTails2.gif")
    paths.add("Data/Sprites/Players/Continue.gif")
    paths.add("Data/Sprites/Players/CutsceneTMZ.gif")
    paths.add("Data/Sprites/Players/KnuxCutsceneCPZ.gif")
    paths.add("Data/Sprites/Players/SonicCutsceneCPZ.gif")
    paths.add("Data/Sprites/Players/TailsCutsceneCPZ.gif")
    paths.add("Data/Sprites/Puyo/Puyo.gif")
    paths.add("Data/Sprites/SBZ/Objects-Classic.gif")
    paths.add("Data/Sprites/SPZ1/Boss.gif")
    paths.add("Data/Sprites/SPZ1/Enemies.gif")
    paths.add("Data/Sprites/SPZ1/Letters.gif")
    paths.add("Data/Sprites/SPZ1/Reflection.gif")
    paths.add("Data/Sprites/SPZ2/EggTV.gif")
    paths.add("Data/Sprites/SPZ2/Enemies.gif")
    paths.add("Data/Sprites/SPZ2/WeatherMobile.gif")
    paths.add("Data/Sprites/SPZ2/WeatherTVFBZ.gif")
    paths.add("Data/Sprites/SSZ/Objects-Classic.gif")
    paths.add("Data/Sprites/SSZ1/Constellations.gif")
    paths.add("Data/Sprites/SSZ1/Enemies.gif")
    paths.add("Data/Sprites/SSZ1/HotaruHiWatt.gif")
    paths.add("Data/Sprites/SSZ1/Laser.gif")
    paths.add("Data/Sprites/SSZ1/Objects-Classic.gif")
    paths.add("Data/Sprites/SSZ1/TTSparkle.gif")
    paths.add("Data/Sprites/SSZ2/Enemies.gif")
    paths.add("Data/Sprites/SSZ2/Laser.gif")
    paths.add("Data/Sprites/SSZ2/MetalSonic1.gif")
    paths.add("Data/Sprites/SSZ2/MetalSonic2.gif")
    paths.add("Data/Sprites/SSZ2/Objects-Classic.gif")
    paths.add("Data/Sprites/Special/Emeralds.gif")
    paths.add("Data/Sprites/Special/Results.gif")
    paths.add("Data/Sprites/SpecialBS/Globe.gif")
    paths.add("Data/Sprites/SpecialBS/Horizon.gif")
    paths.add("Data/Sprites/SpecialUFO/Display.gif")
    paths.add("Data/Sprites/SpecialUFO/Objects.gif")
    paths.add("Data/Sprites/TMZ1/Boss.gif")
    paths.add("Data/Sprites/TMZ1/Enemies.gif")
    paths.add("Data/Sprites/TMZ1/Enemies2.gif")
    paths.add("Data/Sprites/TMZ1/MonarchBottom.gif")
    paths.add("Data/Sprites/TMZ1/MonarchTop.gif")
    paths.add("Data/Sprites/TMZ1/Portal1.gif")
    paths.add("Data/Sprites/TMZ1/Portal2.gif")

    sorted_paths = sorted(paths)
    out_path = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        for p in sorted_paths:
            f.write(p + "\n")
    print(f"[output] wrote {len(sorted_paths)} candidate paths -> {out_path}")


if __name__ == "__main__":
    main()
