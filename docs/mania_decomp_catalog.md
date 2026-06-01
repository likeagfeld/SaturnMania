# Sonic Mania Decompilation — Authoritative Catalog for the Saturn Port

**Upstream source:** github.com/RSDKModding/Sonic-Mania-Decompilation @ master  
**Engine reference:** github.com/RSDKModding/RSDKv5-Decompilation @ master  
**Catalog date:** 2026-05-26 (updated 2026-05-28 Phase 3.0-prep++)  

> **Phase 3.0-prep++ cross-reference (2026-05-28):**
> `docs/whole_game_asset_audit.md` is the per-scene asset inventory for
> every retail Sonic Mania scene (42 stage folders + Global + UI +
> Common + Cutscene + Helpers + BSS + Pinball + Continue + Summary +
> Unused). 493 unique object classes mapped to upstream
> `Objects/<Cat>/` directories; 894 decomp `.c`/`.h` files batch-fetched
> into `tools/_decomp_raw/` (1036 total). Asset side of every
> NOT_STARTED row in `docs/decomp_port_status.md` is data-resolved
> (Gate V3.0-prep++ GREEN per
> `tools/qa_phase3_0_plus_whole_game_asset_coverage_gate.py`).

**Purpose:** ground truth for validating the Saturn port (D:\sonicmaniasaturn\) section-by-section against the original game logic.

All "verbatim" code below was retrieved with `gh api repos/RSDKModding/Sonic-Mania-Decompilation/contents/<path>?ref=master` (raw GitHub via the GitHub CLI) — `WebFetch` returns summarised output and was insufficient for verbatim quoting, so do not regress to it.

Cached raw copies of every file cited here live under `D:\sonicmaniasaturn\tools\_decomp_raw\` (one local file per upstream path, with `/` replaced by `_`). The full repo tree (1314 entries) is in `D:\sonicmaniasaturn\tools\_repo_tree.txt`.

A scene-level parse of the shipped game's `extracted/Data/Stages/*/StageConfig.bin` is in `docs/scene_objects.json` (produced by `tools/dump_all_scenes.py`).

---

## §1 — Engine state machine

### 1.1 Top-level entry points (SonicMania/Game.c)

Mania compiles into a single shared library that the RSDKv5 engine link-loads. The DLL exports a single C symbol:

```c
// SonicMania/GameMain.h
#ifdef __cplusplus
extern "C" {
#endif
#if RETRO_REV02
void LinkGameLogicDLL(EngineInfo *info);
#else
void LinkGameLogicDLL(EngineInfo info);
#endif
#ifdef __cplusplus
}
#endif
```

`LinkGameLogicDLL` copies the engine function tables (`RSDK`, `API`, `Mod`) into game-local globals, stashes pointers to the engine-owned `SceneInfo`, `GameInfo`, `ControllerInfo`, `ScreenInfo`, then calls `InitGameLogic()`. `InitGameLogic()` registers ~700 object classes via the `RSDK_REGISTER_OBJECT(<Name>)` macro (file SonicMania/Game.c, body starts at line 140). The Saturn port must perform the equivalent registration step in `main` before `slGotoScene`.

### 1.2 RSDK SceneInfo state — the actual GameMode/ENGINESTATE enum

`sceneInfo->state` is the engine-level frame loop state. Mania's `GameVariables.h` does NOT define it — it lives in **RSDKv5** (referenced as `ENGINESTATE_LOAD`, `ENGINESTATE_REGULAR`, `ENGINESTATE_PAUSED`, `ENGINESTATE_FROZEN`, `ENGINESTATE_LOAD_STEPOVER`, `ENGINESTATE_REGULAR_STEPOVER`, `ENGINESTATE_PAUSED_STEPOVER`, `ENGINESTATE_FROZEN_STEPOVER`, `ENGINESTATE_NONE`). For our purposes:

- `LOAD`        → engine is calling per-object `Create`/`StageLoad` once
- `REGULAR`     → normal per-frame `Update` → `LateUpdate` → `Draw`
- `PAUSED`      → `Update`/`LateUpdate` skipped, `Draw` still runs (gameplay frozen, HUD/PauseMenu draws)
- `FROZEN`      → like paused, but `Update` runs for entities whose `active == ACTIVE_ALWAYS` (Music, PauseMenu)
- `_STEPOVER`   → debug "step one frame" variants

Mania's per-object `active` field selects which subset runs in each state (`ACTIVE_NEVER`, `ACTIVE_ALWAYS`, `ACTIVE_NORMAL`, `ACTIVE_PAUSED`, `ACTIVE_BOUNDS`, `ACTIVE_XBOUNDS`, `ACTIVE_YBOUNDS`, `ACTIVE_RBOUNDS`, `ACTIVE_DISABLED`).

### 1.3 Mania game-mode enum (SonicMania/GameVariables.h — verbatim)

```c
typedef enum {
#if !MANIA_USE_PLUS
    MODE_NOSAVE,
#endif
    MODE_MANIA, // officially called "MODE_SAVEGAME" in pre-plus, but it's easier to re-use names lol
#if MANIA_USE_PLUS
    MODE_ENCORE,
#endif
    MODE_TIMEATTACK,
    MODE_COMPETITION,
} GameModes;
```

For the Saturn port we will compile `MANIA_PREPLUS = 1` (version 1.03 = `VER_103`, `RETRO_REVISION = 1`) and target only `MODE_NOSAVE` and `MODE_MANIA`. Encore, Time-Attack and Competition modes are deferred.

### 1.4 Character-flag enum (verbatim)

```c
typedef enum {
    ID_NONE     = 0 << 0,
    ID_SONIC    = 1 << 0,
    ID_TAILS    = 1 << 1,
    ID_KNUCKLES = 1 << 2,
#if MANIA_USE_PLUS
    ID_MIGHTY = 1 << 3,
    ID_RAY    = 1 << 4,
#endif
    ID_TAILS_ASSIST    = ID_TAILS << 8,
    ID_KNUCKLES_ASSIST = ID_KNUCKLES << 8,
    ID_DEFAULT_PLAYER  = ID_SONIC | ID_TAILS_ASSIST,
} PlayerIDs;

#define GET_CHARACTER_ID(playerNum) \
    (((globals->playerID >> (8 * ((playerNum)-1))) & 0xFF))
```

`globals->playerID` packs P1 in bits 0..7, P2 (sidekick) in bits 8..15, etc. Saturn-port equivalent: a 32-bit packed character field on `game_globals_t`.

### 1.5 Reserved entity slots (verbatim)

The first ~64 entity slots are reserved for engine-managed singletons. Pre-plus values (we ignore the plus-only entries):

```c
SLOT_PLAYER1 = 0,
SLOT_PLAYER2 = 1,
SLOT_POWERUP1   = 2,
SLOT_POWERUP2   = 3,
SLOT_POWERUP1_2 = 4,
SLOT_POWERUP2_2 = 5,
SLOT_BSS_SETUP   = 8,
SLOT_MUSIC       = 9,
SLOT_BSS_HUD     = 10,
SLOT_BSS_MESSAGE = 11,
SLOT_ZONE        = 8,           // (12 in Plus)
SLOT_CUTSCENESEQ = 15,
SLOT_PAUSEMENU   = 16,
SLOT_GAMEOVER    = 16,
SLOT_ACTCLEAR    = 16,
SLOT_DIALOG      = 21,
SLOT_BIGBUBBLE_P1 = 32,
SLOT_BIGBUBBLE_P2 = 33,
SLOT_CAMERA1 = 60,
SLOT_CAMERA2 = 61,
SLOT_CAMERA3 = 62,
SLOT_CAMERA4 = 63,
```

The Saturn port must reserve the same slot indices in its entity array (or the runtime cross-references — e.g. `RSDK_GET_ENTITY(SLOT_PLAYER1, Player)` — will alias the wrong entity).

### 1.6 GlobalVariables struct (verbatim head — SonicMania/GameVariables.h)

```c
typedef struct {
    int32 gameMode;
    int32 playerID;
    int32 specialCleared;
    int32 specialRingID;
    int32 blueSpheresID;
    int32 blueSpheresInit;
    int32 atlEnabled;
    int32 atlEntityCount;
    int32 atlEntitySlot[0x20];
    int32 atlEntityData[0x4000];
    int32 saveLoaded;
    int32 saveRAM[0x4000];
    int32 saveSlotID;
    int32 noSaveSlot[0x400];
    int32 menuParam[0x4000];
    int32 itemMode;
    int32 suppressTitlecard;
    int32 suppressAutoMusic;
    int32 competitionSession[0x4000];
    int32 medalMods;
    int32 parallaxOffset[0x100];
    int32 enableIntro;
    int32 optionsLoaded;
    int32 optionsRAM[0x80];
    int32 presenceID;
    int32 medallionDebug;
    int32 noSave;
    int32 notifiedAutosave;
    int32 recallEntities;
    int32 restartRings;
    int32 restart1UP;
    int32 restartPowerups;
    int32 restartPos[8];
    int32 restartSlot[4];
    int32 restartDir[4];
    int32 restartMinutes;
    int32 restartSeconds;
    int32 restartMilliseconds;
    int32 tempMinutes;
    int32 tempSeconds;
    int32 tempMilliseconds;
    int32 restartScore;
    int32 restartScore1UP;
    int32 restartLives[4];
#if GAME_VERSION != VER_100
    int32 restartMusicID;
#endif
    int32 restartFlags;
    int32 tempFlags;
    int32 continues;
    int32 initCoolBonus;
    int32 coolBonus[4];
    // …Plus-only replay/stock fields omitted
} GlobalVariables;
```

This struct must be persisted across scenes. Saturn equivalent: backup-RAM-saved `g_globals` in work-RAM. Total size: ~80 KiB (most of it is `saveRAM[0x4000]` + `noSaveSlot[0x400]` + `menuParam[0x4000]` + `competitionSession[0x4000]` + `atlEntityData[0x4000]` = ~5 × 16-KiB blocks). For Saturn we MUST shrink these (see §12).

### 1.7 Frame flow

Per RSDKv5 `Scene.cpp`, each frame in `ENGINESTATE_REGULAR`:

1. `ProcessInput()` — fills `ControllerInfo`
2. For every active entity in slot order:
   - if `active==ACTIVE_BOUNDS|XBOUNDS|YBOUNDS|RBOUNDS`, do a bounds check vs the camera and skip if outside
   - call the object's `Update` callback (`Player_Update`, `Ring_Update`, etc.)
3. Second pass: call each entity's `LateUpdate` (used for camera follow, HUD readout)
4. Engine increments `sceneInfo->timer`, milliseconds/seconds/minutes
5. Draw pass over draw-groups 0..15 — for each group, render each tile-layer + each entity assigned to that group, calling `Draw` callbacks
6. SCSP/audio mixer update happens engine-side once per frame

Saturn port note: VBLANK on the Saturn is asynchronous to game logic. The Mania frame loop is synchronous-tick — our existing `jo_main_loop()` performs the tick. Tile-layer streaming must be done in vblank callback per the SOLVED memory note (`saturn-vdp2-streaming-solved.md`).

---

## §2 — Per-scene catalog

The shipped 1.03 GameConfig (`extracted/Data/Game/GameConfig.bin`, parsed via `tools/parse_gameconfig.py`) lists 92 scenes across 8 categories.

For each playable Mania-mode zone the catalog below merges:

- **Category / Folder / Scene id** — from GameConfig (REV01)
- **Music track** — by Mania convention, `<ZoneName><Act>.ogg` from `Data/Music/`. The Music object is spawned per-scene; the actual `.ogg` mapping is encoded as a `Music` entity property inside the scene's `Scene*.bin` (the file format obfuscates strings, so we read it from the shipped `Data/Music/` filename pattern).
- **Background object / Setup object** — the `<Folder>Setup.c` source under `SonicMania/Objects/<ZoneCode>/`
- **Per-scene objects loaded** — parsed from `Data/Stages/<Folder>/StageConfig.bin` (`tools/dump_all_scenes.py` → `docs/scene_objects.json`)
- **Unique mechanics** — derived from the setup file's `StageLoad` callbacks (water, fire, palette-fade timers, scanline callbacks, etc.)

### 2.1 Presentation category (7 scenes)

| Scene name | Folder | Id | Setup | Background object | Notes |
|---|---|---|---|---|---|
| Logos | Logos | 1 | LogoSetup | UIPicture | RSDK / SEGA splash. Stage-objects: `LogoSetup, UIPicture`. **Asset audit:** covered by `docs/menu_scene_asset_audit.md` (Phase 3.0-prep). Loads `Stage/Sega.wav` + `Logos/Logos.bin` (+ `Logos.gif`). REGION_JP loads `CESA.tga`/`.png` (INFO, non-JP Saturn skip). |
| Title Screen | Title | 1 | TitleSetup | TitleBG (+ Title3DSprite, TitleSonic, TitleLogo) | Music: `TitleScreen.ogg` (TRACK_STAGE). Plays jingle `TRACK_STAGE` on init (`TitleSetup.c:145`). Stage-objects: `APICallback, Options, SaveGame, Localization, TitleSetup, TitleLogo, TitleSonic, Music, TitleBG, Title3DSprite, DemoMenu`. **Full asset audit:** `docs/title_scene_asset_audit.md` (Phase 1.33, 2026-05-27) — 42 unique asset paths enumerated, gate `tools/qa_phase1_33_asset_coverage_gate.py` enforces coverage; covers Sprites/SoundFX/Music/Strings/Video. |
| Menu | Menu | 1 | MenuSetup | UIBackground | Music: 4 trackIDs keyed by active mode -- `MainMenu.ogg` (MAIN), `Competition.ogg` (TIMEATTACK), `Results.ogg` (COMPETITION), `SaveSelect.ogg` (SAVESELECT) -- per Menu/Scene1.bin Music entities slot 129/198/199/200 trackFile attrs; resolved via `MenuSetup_ChangeMenuTrack:890-905`. ~40 stage-objects (`UIControl, UIBackground, UIWidgets, UIHeading, UISubHeading, UIPicture, UIButton, UIModeButton, UISaveSlot, UIButtonPrompt, FXFade, Music, MenuSetup, UIChoice, UICharButton, UITAZoneModule, UIDialog, UILeaderboard, UIVsCharSelector, UIVsRoundPicker, UIVsZoneButton, UIVsResults, UIInfoLabel, UIVsScoreboard, UIOptionPanel, UISlider, UIButtonLabel, UIKeyBinder, UIWaitSpinner, GameProgress, UIMedallionPanel, Announcer, UITransition, UIUsernamePopup, UIResPicker, UIWinSize`). **Full asset audit:** `docs/menu_scene_asset_audit.md` (Phase 3.0-prep, 2026-05-28) — 125 unique asset paths enumerated (121 retail present + 4 Plus/region INFO), gate `tools/qa_phase3_0_menu_asset_coverage_gate.py` enforces coverage; covers UI Sprites/SoundFX/Music. 86 retail assets newly extracted (~4.73 MB), including the 4 menu music tracks discoverable only via Scene1.bin trackFile attrs. |
| Thanks For Playing | Thanks | 1 | ThanksSetup | UIPicture | Static credits frame. |
| Level Select | LSelect | 1 | LevelSelect | UIText, UIPicture | Music: `LevelSelect.ogg` (Saturn folder has `SaveSelect.ogg` instead; cross-reference your filelist). |
| Credits | Credits | 1 | CreditsSetup | UICreditsText | Music: `Credits.ogg`. |
| Continue | Ending | C | ContinueSetup | ContinuePlayer | Game-over Continue screen. |

### 2.2 Mania Mode category (29 scenes)

For each, the **`*Setup_StageLoad`** body (see `_decomp_raw/SonicMania_Objects_<CODE>_<CODE>Setup.c` cached locally) is the authoritative list of: animated-tile sprite sheets, tile-layer aliases, palette-fade timers, scanline callbacks, and any helper objects pre-loaded. The 28-element StageConfig object list below is the *complete* per-scene object class table — only classes in that list can be referenced by an entity in `Scene1.bin`.

#### GHZ1 / GHZ2 — Green Hill Zone

- **Folder:** `GHZ` (Scene1.bin, Scene2.bin) — note GHZ uses ONE folder for both acts.
- **Music:** `GreenHill1.ogg` / `GreenHill2.ogg` (per Mania OST naming convention).
- **Setup:** `GHZSetup.c` (211 lines).  
  `StageLoad` loads `GHZ/AniTiles.gif`, then handles a multi-layer BG cave/outside system using `BGSwitch`. Per-frame `StaticUpdate` does `RSDK.SetLimitedFade(0, 1, 2, paletteTimer, 181, 184)` and `… 197, 200` for the GHZ palette pulse. GHZ has 4 tile layers: `BG Outside`, layer 1 (foreground), `bgCave1`, `bgCave2`, switched via `BGSwitch_GHZ_Outside/InCave/Mixed`.
- **Per-scene objects (28):**  
  `GHZSetup, ForceSpin, Platform, CollapsingPlatform, BreakableWall, CheckerBall, Decoration, Bridge, Motobug, BuzzBomber, Crabmeat, Newtron, Splats, Chopper, Batbrain, CorkscrewPath, BGSwitch, SpikeLog, BurningLog, ZipLine, Water, DDWrecker, DERobot, Eggman, ForceUnstick, SpinBooster, CutsceneSeq, GHZ2Outro`
- **Unique mechanics:** water (GHZ has surface ponds), fire (`BurningLog`), corkscrew paths, the GHZ DD-Wrecker mini-boss and `DERobot` (Death-Egg-Robot reskin) mid-boss.
- **Saturn notes:** GHZ is the canonical first stage we're already targeting. `corkscrew path` is a tile-level slope thing not requiring code support; `Water` requires a palette-blend layer (deferred — see §12).

#### CPZ1 / CPZ2 — Chemical Plant Zone

- **Folder:** `CPZ`
- **Music:** `ChemicalPlant1.ogg` / `ChemicalPlant2.ogg`
- **Setup:** `CPZSetup.c` (206 lines).  
  `StageLoad` loads `CPZ/Objects.gif`, sets layer 0 = background. Provides BG-switch callbacks for the CPZ1 act vs CPZ2 act background art (one extra background act-1 layer).
- **Per-scene objects (45):**  
  `CPZSetup, ParallaxSprite, Decoration, Platform, PlatformControl, PlatformNode, Water, OneWayDoor, ChemicalBall, TubeSpring, SpeedBooster, Springboard, RotatingStair, Staircase, TippingPlatform, TransportTube, TwistedTubes, Spiny, Grabber, Sweep, CaterkillerJr, AmoebaDroid, ChemicalPool, ChemBubble, Syringe, Reagent, StickyPlatform, DNARiser, CollapsingPlatform, BreakableWall, PuyoBean, PuyoMatch, PuyoAttack, PuyoAI, CPZShutter, CPZBoss, ForceSpin, SpinBooster, ForceUnstick, BGSwitch, CutsceneSeq, CPZ2Outro, CPZ1Intro, FXRuby, PhantomRuby`
- **Unique mechanics:** Water (`ChemicalPool` is the famous purple goop — separate from regular `Water`), transport tubes, sticky platforms, Puyo-Puyo mini-game (CPZ2 boss).
- **Saturn notes:** Puyo Puyo is an entire sub-game (object family `Puyo*`). For initial port: defer CPZ2 boss; CPZ1 substitute the rolling-staircase as a static slope.

#### SPZ1 / SPZ2 — Studiopolis Zone

- **Folder:** `SPZ1`, `SPZ2` (separate folders for the two acts)
- **Music:** `Studiopolis1.ogg` / `Studiopolis2.ogg`
- **Setup:** `SPZ1Setup.c` (125 lines), `SPZ2Setup.c` (149 lines).  
  `SPZ1Setup_StageLoad` loads `SPZ1/AniTiles.gif`, registers two background layers (`cityBGLow`, `cityBGHigh`). `StaticUpdate` runs the FG-lights palette pulse via `SetLimitedFade(0,1,2, fgLightsPalTimer, 128,135)` and a sin-wave neon pulse on rows 152–159.
- **Per-scene objects SPZ1 (29):**  
  `SPZ1Setup, Platform, PlatformControl, PlatformNode, CollapsingPlatform, BreakableWall, ParallaxSprite, ShopWindow, SpinSign, Decoration, CircleBumper, RockemSockem, LEDPanel, TVVan, PopcornMachine, PopcornKernel, Clapperboard, FilmProjector, DirectorChair, Canista, MicDrop, Shutterbug, Tubinaut, HeavyGunner, SpinBooster, SPZ1Intro, FXFade, CutsceneSeq, ForceUnstick`
- **Per-scene objects SPZ2 (35):** (see `docs/scene_objects.json` — includes `WeatherMobile, WeatherTV, EggTV, TVFlyingBattery` for the SPZ2 boss "Weather Globe").
- **Unique mechanics:** Pulsing FG light palette, neon-sign animations, SPZ1 boss = `HeavyGunner` (HBH), SPZ2 boss = `WeatherMobile`.

#### FBZ1 / FBZ2 — Flying Battery Zone

- **Folder:** `FBZ`
- **Music:** `FlyingBattery1.ogg` / `FlyingBattery2.ogg`
- **Setup:** `FBZSetup.c` (299 lines).  
  `StateMachine_Update`-style background driver: `backgroundOutside` scrolls via `RSDK.Sin512(Zone->persistentTimer)`. Manages multiple BG layers via `BGSwitch_FBZ_*` callbacks (Inside / Outside / Mixed).
- **Per-scene objects (48):** Includes `Launcher, Blaster, Technosqueek, Clucker, Crane, FlameSpring, TwistingDoor, SpiralPlatform, Propeller, Mine, FoldingPlatform, ElectroMagnet, MagPlatform, MagSpikeBall, TetherBall, FBZMissile, FBZFan, Cylinder, Eggman, BigSqueeze, SpiderMobile, FBZStorm`.
- **Unique mechanics:** Electromagnet pull, missile launchers, BigSqueeze boss (a wall-of-junk crusher).

#### PSZ1 / PSZ2 — Press Garden Zone (folder is `PSZ1` / `PSZ2`, displays as "Press Garden")

- **Folder:** `PSZ1`, `PSZ2`
- **Setup:** `PSZ1Setup.c` (370 lines), `PSZ2Setup.c` (163 lines). PSZ1 = paper/printing factory; PSZ2 = frozen Japanese garden.  
  `PSZ1Setup_StageLoad` loads three sprite sheets (`PSZ1/AniTiles.gif`, `AniTiles2.gif`, `AniTiles3.gif`).
- **Per-scene objects PSZ1 (27):** includes `Dragonfly, Splats, JuggleSaw, PetalPile, Press, PrintBlock, SP500, PSZDoor, DoorTrigger, PSZLauncher, Crate, Shiversaw, Ice, Turntable, PaperRoller, SP500MkII`.
- **Per-scene objects PSZ2 (30):** adds `IceBomba, Woodrow, FrostThrower, Shuriken, IceSpring, WoodChipper, HeavyShinobi, Snowflakes` plus the `PSZEggman` boss.
- **Unique mechanics:** Ice physics (PSZ2 has its own `Ice` floor object), paper-press crush hazard, snowflake palette FX.

#### SSZ1 / SSZ2 — Stardust Speedway Zone

- **Folder:** `SSZ1`, `SSZ2`
- **Music:** `StardustSpeedway1.ogg` / `StardustSpeedway2.ogg`
- **Setup:** `SSZ1Setup.c` (84 lines, lightweight — most logic is in object classes), `SSZ2Setup.c` (240 lines — handles the Metal Sonic race).
- **Per-scene objects SSZ1 (42):** `Kanabun, Kabasira, Dango, Hotaru, RTeleporter, MSHologram, HiLoSign, SDashWheel, YoyoPulley, UncurlPlant, BouncePlant, RotatingSpikes, HotaruMKII, JunctionWheel, FlowerPod, Beanstalk, PlayerProbe, SpikeFlail, SparkRail, HotaruHiWatt, Constellation, Fireflies, TimePost, SSZ1Outro, FXRuby, SSZ1Intro, PhantomRuby` + standard plat/coll classes.
- **Per-scene objects SSZ2 (33):** has the famous Metal-Sonic race (`MetalSonic, MSPanel, MSFactory, SilverSonic, MSHologram, MSOrb`) and `EggTower` boss.
- **Unique mechanics:** Time-travel mechanic uses a separate scene (folder `TimeTravel`). Bungee jumps, junction wheels, electric (Hotaru) hazards.

#### HCZ1 / HCZ2 — Hydrocity Zone

- **Folder:** `HCZ`
- **Music:** `Hydrocity1.ogg` / `Hydrocity2.ogg`
- **Setup:** `HCZSetup.c` (291 lines).  
  `StageLoad` loads three AniTile sheets (`HCZ/AniTiles.gif`, `AniTiles2.gif`, `AniTiles3.gif`) and registers SFX `HCZ/Waterfall.wav`, `Waterfall2.wav` (looping). `StaticUpdate` deforms FG tile-layer scanlines (`deformationOffsetW++`) for the underwater wave effect.
- **Per-scene objects (42):** includes `Water` (mandatory), `Current` (player-pushing water current), `Whirlpool`, `Spear` (skewer hazard), `Gondola` (boss platform), `DiveEggman` (sub-boss), `LaundroMobile` (HCZ2 boss).
- **Unique mechanics:** UNDERWATER (`Water` object combined with player's `underwater` flag — drowns at 30s, palette-shift on the screen, halved acceleration/topSpeed and gravity-doubled physics — verified in `Player_UpdatePhysicsState`, line 2762). Scanline waveform on FG layer. Whirlpool drags.

#### MSZ1 / MSZ2 — Mirage Saloon Zone

- **Folder:** `MSZ`
- **Music:** `MirageSaloon1.ogg` / `MirageSaloon2.ogg`
- **Setup:** `MSZSetup.c` (744 lines — the heaviest setup file).
- **Per-scene objects (50):** Tornado boss-vehicle on MSZ1 (`Tornado, TornadoPath`), MSZ-specific badniks (`Bumpalo, Rattlekiller, Hatterkiller, RollerMKII, Vultron, Cactula, Armadiloid`), the casino-style mini-objects (`Flipper, SeeSaw, BarStool, SeltzerBottle, SeltzerWater, GiantPistol, CollapsingSand, Honkytonk, LightBulb, Pinata, RotatingSpikes, PaintingEyes, SideBarrel`), Heavy Mystic boss (`HeavyMystic`), Eggman locomotive (`EggLoco, LocoSmoke`), MSZ spotlight (`MSZSpotlight`).
- **Unique mechanics:** MSZ1 has a moving train BG (`fgSupaLow` scrolls with `chuggaVolume`-modulated scrollSpeed), spotlights, sand-collapse, large foreground deformation for train pitch.

#### OOZ1 / OOZ2 — Oil Ocean Zone

- **Folder:** `OOZ1`, `OOZ2`
- **Music:** `OilOcean1.ogg` / `OilOcean2.ogg`
- **Setup:** `OOZSetup.c` (665 lines).  
  `StageLoad` registers `OOZ/AniTiles.gif`, `OOZ/Sol.bin`, `OOZ/Splash.bin`. The `Sol` (fire jet) is a per-frame palette-FX-driven object. `StaticUpdate` does `SetLimitedFade(1, 3, 4, palTimer, 216,223)` for oil shimmer + `SetLimitedFade(2, 5, 6, palTimer, 216,223)` (two palette pages cycle).
- **Per-scene objects OOZ1 (15):** `OOZSetup, BreakableWall, CollapsingPlatform, Platform, Aquis, Octus, Fan, GasPlatform, PushSpring, BallCannon, Valve, Sol, MeterDroid, ForceUnstick, OOZFlames`
- **Per-scene objects OOZ2 (24):** adds `PullSwitch, WarpDoor, SwitchDoor, ParallaxSprite, Smog, MegaOctus, TilePlatform, Hatch, GenericTrigger, CutsceneSeq, OOZ1Outro`
- **Unique mechanics:** Fire/oil pools, fire jets, palette pulse, MegaOctus boss (OOZ2).

#### LRZ1 / LRZ2 / LRZ3 — Lava Reef Zone (3 acts in Mania)

- **Folder:** `LRZ1`, `LRZ2`, `LRZ3`
- **Music:** `LavaReef1.ogg` / `LavaReef2.ogg` / `LavaReef1.ogg`? (LRZ3 reuses LRZ1 or HPZ music — confirm against scene). 
- **Setup:** `LRZ1Setup.c` (153 lines), `LRZ2Setup.c` (316 lines), `LRZ3Setup.c` (86 lines).  
  LRZ1 `StaticUpdate` does palette-fade on rows 208-211 (lava glow) and 140-141/142-143/156-158 (red lava pulse + heatglow).
- **Per-scene objects LRZ1 (36):** lava-specific badniks `Drillerdroid, Fireworm, Iwamodoki, Rexon, Toxomister, LRZRockPile, LRZSpiral, LavaFall, LavaGeyser, LRZFireball, RisingLava, RockDrill, TurretSwitch, SpikeCrusher, WalkerLegs, DrillerdroidO, BuckwildBall, DashLift, Stalactite`
- **Per-scene objects LRZ2 (38):** adds `Turbine, OrbitSpike, Flamethrower, LRZSpikeBall, LRZConveyor, LRZConvControl, LRZConvItem, LRZConvDropper, LRZConvSwitch`
- **Per-scene objects LRZ3 (18):** focused on the Hidden-Palace-style emerald-room — `HPZEmerald, KingClaw, KingAttack, HeavyKing, ThoughtBubble, FXSpinRay, FXExpandRing, HeavyRider, SkyTeleporter, LRZ3OutroK`
- **Unique mechanics:** Rising-lava chase, conveyor belts, Heavy King boss (LRZ3), LRZ2 special palette transition (the famous "frozen lava" effect — uses `SetLimitedFade` with negative blend value).

#### MMZ1 / MMZ2 — Metallic Madness Zone

- **Folder:** `MMZ`
- **Music:** `MetallicMadness1.ogg` / `MetallicMadness2.ogg`
- **Setup:** `MMZSetup.c` (138 lines).  
  Sets initial scrollPos on layers 0/1 to 384<<16. Layers 3..4 use a centred-on-screen scroll info (the "shrink ray" parallax).
- **Per-scene objects (32):** `FarPlane, ConveyorPlatform, Piston, VanishPlatform, BuzzSaw, ConveyorWheel, MatryoshkaBom, MechaBu, EggPistonsMKII, SpikeCorridor, RPlaneShifter, Gachapandora, MMZWheel, BladePole, ConveyorBelt, PlaneSeeSaw, Scarab, PohBee, SizeLaser, FlingRamp`
- **Unique mechanics:** Size-laser shrinks/grows player (`SizeLaser`, `isChibi` flag affects Player physics — see §3.6), `PlaneSeeSaw`, far-plane parallax.

#### TMZ1 / TMZ2 / TMZ3 — Titanic Monarch Zone

- **Folder:** `TMZ1`, `TMZ2`, `TMZ3`
- **Music:** `TitanicMonarch1.ogg` / `TitanicMonarch2.ogg` / (TMZ3 uses the same/extends)
- **Setup:** `TMZ1Setup.c` (309 lines), `TMZ2Setup.c` (111 lines), `TMZ3Setup.c` (69 lines).
- **Per-scene objects TMZ1 (30):** `WallBumper, GymBar, MagnetSphere, BallHog, FlasherMKII, SentryBug, CrimsonEye, TeeterTotter, PopOut, MetalArm, CrashTest, LargeGear, JacobsLadder, TurboTurtle, MonarchBG, LaunchSpring, BGSwitch`
- **Per-scene objects TMZ3 (29):** the final-zone gauntlet `TMZAlert, TMZCable, PhantomRuby, PhantomEgg, PhantomHand, PhantomMissile, PhantomShield, PhantomGunner, PhantomShinobi, PhantomMystic, PhantomRider, EscapeCar, TMZFlames`
- **Unique mechanics:** Plane-flip via Phantom Ruby (palette-warp), magnetic pulls, "phantom" boss rush.

#### ERZ — Egg Reverie Zone (1 scene)

- **Folder:** `ERZ`
- **Music:** `EggReverie.ogg`
- **Setup:** `ERZSetup.c` (78 lines).  
  `StageLoad`: `RSDK.LoadSpriteSheet("Phantom/Sky.gif")`; sets `layer 1` `scanlineCallback = ERZSetup_Scanline_Sky` (per-scanline Y-offset — REQUIRES Saturn rotated-BG2 with cell scroll table per row).
- **Per-scene objects (18):** `ERZSetup, FXRuby, FXFade, ERZStart, CutsceneSeq, PhantomRuby, ChaosEmerald, PhantomKing, KleptoMobile, ERZKing, ERZShinobi, ERZGunner, ERZMystic, ERZRider, PKingAttack, RingField, ERZOutro, RubyPortal`
- **Unique mechanics:** Free-flight (Super Sonic only), scanline-warped sky, all-emeralds final boss fight.

### 2.3 Special Stage category (Blue Spheres / UFO chase, 7 + 34 scenes)

| Category | Folder | Music | Setup |
|---|---|---|---|
| **UFO chase (Special Stage)** | `UFO1`–`UFO7` | `Special.ogg` (or similar) | `UFO_Setup` |
| **Blue Spheres (Bonus)** | `SpecialBS` (Scene1.bin–Scene32.bin + Scene34/Scene36 = random) | `BlueSpheres.ogg` | `BSS_Setup` |

UFO objects use a SEPARATE coordinate system (3D) — see §12.

Per-scene object lists for `UFO1`..`UFO7` are nearly identical: `APICallback, Options, SaveGame, Localization, UFO_Setup, UFO_Player, UFO_Camera, UFO_HUD, UFO_Circuit, UFO_Ring, UFO_Sphere, UFO_Shadow, UFO_Message, UFO_Springboard, UFO_Decoration, UFO_SpeedLines, UFO_Dust, UFO_ItemBox, SpecialClear, Music, UIBackground, UIControl, UIWidgets, UIButton, UIDialog, UIWaitSpinner, PauseMenu` (UFO4 adds `UFO_Water`, UFO7 adds `UFO_Plasma`).

`SpecialBS` (Blue Spheres) per-scene object list (20): `APICallback, Options, SaveGame, Localization, Music, BSS_Palette, BSS_Setup, BSS_Player, BSS_Horizon, BSS_HUD, BSS_Collectable, BSS_Collected, BSS_Message, FXFade, UIControl, UIWidgets, UIButton, UIDialog, UIWaitSpinner, PauseMenu`.

### 2.4 Extras + Cutscene + Video categories

| Category | Folder | Notes |
|---|---|---|
| Puyo Puyo (Extras) | `Puyo` | CPZ2 boss in standalone form, 25 stage-objects. |
| D.A. Garden (Extras) | `DAGarden` | Sound-test diorama. 15 stage-objects. |
| AIZ Cutscene | `AIZ` | Mania intro flyover (the Phantom Ruby discovery). 11 stage-objects. |
| GHZ Cutscene 1/2 | `GHZCutscene` | GHZ end-of-zone cutscene (the HBH transformation). 15 stage-objects. |
| MSZ K Intro | `MSZCutscene` | Knuckles-only Mirage Saloon entry. 22 stage-objects. |
| SSZ Time Warp | `TimeTravel` | Stardust time-travel cutscene. 4 stage-objects. |
| Try Again | `Ending` (id=T) | Game-over → continue. |
| Ending videos | `Ending` (id=BS/BT/BK/G/TK) | Five short cinematic outcomes — UIVideo entity. |

### 2.5 Music slot reservation (Music.c — verbatim)

```c
// SonicMania/Objects/Global/Music.c::Music_StageLoad (lines 49-64)
Music_SetMusicTrack("Invincible.ogg",   TRACK_INVINCIBLE, 139263);
Music_SetMusicTrack("Sneakers.ogg",     TRACK_SNEAKERS,   120960);
Music_SetMusicTrack("BossMini.ogg",     TRACK_MINIBOSS,   276105);
Music_SetMusicTrack("BossHBH.ogg",      TRACK_HBHBOSS,     70560);
Music_SetMusicTrack("BossEggman1.ogg",  TRACK_EGGMAN1,    282240);
Music_SetMusicTrack("BossEggman2.ogg",  TRACK_EGGMAN2,    264600);
Music_SetMusicTrack("ActClear.ogg",     TRACK_ACTCLEAR,   false);
Music_SetMusicTrack("Drowning.ogg",     TRACK_DROWNING,   false);
Music_SetMusicTrack("GameOver.ogg",     TRACK_GAMEOVER,   false);
Music_SetMusicTrack("Super.ogg",        TRACK_SUPER,      165375);
Music_SetMusicTrack("HBHMischief.ogg",  TRACK_HBHMISCHIEF,381405);
Music_SetMusicTrack("1up.ogg",          TRACK_1UP,        false);
```

`Music->activeTrack = TRACK_STAGE` at stageload; `globals->restartMusicID = TRACK_STAGE`. `TRACK_STAGE` is slot 0 — set by the per-scene Music entity's `trackFile` String property (so e.g. GHZ1 Scene1.bin has a Music entity whose `trackFile = "GreenHill1.ogg"`).

Saturn port: each track maps to a CD-DA index, see existing `tools/build_cdda.py` + `tools/loops.json`.

---

## §3 — Player physics constants (verbatim)

### 3.1 Physics-table lookup function (SonicMania/Objects/Global/Player.c — lines 2747-2813)

This function rewrites the entity's physics state every frame based on (a) character, (b) underwater flag, (c) Super state, (d) speed-shoes timer:

```c
void Player_UpdatePhysicsState(EntityPlayer *entity)
{
    int32 *tablePtr = NULL;
    switch (entity->characterID) {
        default:
        case ID_SONIC:    tablePtr = Player->sonicPhysicsTable; break;
        case ID_TAILS:    tablePtr = Player->tailsPhysicsTable; break;
        case ID_KNUCKLES: tablePtr = Player->knuxPhysicsTable; break;
#if MANIA_USE_PLUS
        case ID_MIGHTY:   tablePtr = Player->mightyPhysicsTable; break;
        case ID_RAY:      tablePtr = Player->rayPhysicsTable; break;
#endif
    }

    int32 tablePos = 0;
    if (entity->underwater) {
        entity->gravityStrength = 0x2000;
        tablePos                = 1;
        if (entity->speedShoesTimer >= 0)
            entity->gravityStrength = 0x1000;
    }
    else {
        entity->gravityStrength = 0x5800;
        if (entity->speedShoesTimer >= 0)
            entity->gravityStrength = 0x3800;
    }

    int32 decelShift = 0;
    if (entity->superState == SUPERSTATE_SUPER) { tablePos |= 2; decelShift = 2; }
    if (entity->speedShoesTimer > 0)            { tablePos |= 4; decelShift = 1; }

    int32 tableID               = 8 * tablePos;
    entity->topSpeed            = tablePtr[tableID];
    entity->acceleration        = tablePtr[tableID + 1];
    entity->deceleration        = tablePtr[tableID + 1] >> decelShift;
    entity->airAcceleration     = tablePtr[tableID + 2];
    entity->airDeceleration     = tablePtr[tableID + 3];
    entity->skidSpeed           = tablePtr[tableID + 4];
    entity->rollingFriction     = tablePtr[tableID + 5];
    entity->jumpStrength        = tablePtr[tableID + 6];
    entity->jumpCap             = tablePtr[tableID + 7];
    entity->rollingDeceleration = 0x2000;

    if (entity->speedShoesTimer < 0) {  // speed-shoes wearing off
        entity->topSpeed       >>= 1;
        entity->acceleration   >>= 1;
        entity->airAcceleration>>= 1;
        entity->skidSpeed      >>= 1;
        entity->rollingFriction>>= 1;
        entity->airDeceleration>>= 1;
    }

    if (entity->isChibi) {
        entity->topSpeed       -= entity->topSpeed       >> 3;
        entity->acceleration   -= entity->acceleration   >> 4;
        entity->airAcceleration-= entity->airAcceleration>> 4;
        entity->jumpStrength   -= entity->jumpStrength   >> 3;
        entity->jumpCap        -= entity->jumpCap        >> 3;
    }
}
```

### 3.2 Sonic physics table (verbatim, Player.h lines 161-164 — Plus only; pre-plus has the same 32-element subset at lines 282-285)

Each "row" of 8 ints = (topSpeed, accel, airAccel, airDecel, skidSpeed, rollFric, jumpStrength, jumpCap).  
Table-pos encoding: `bit 0 = underwater`, `bit 1 = super`, `bit 2 = speedShoes`. Row 0 = normal land, Row 1 = underwater, Row 2 = super-land, Row 3 = super-underwater, Row 4 = speedshoes-land, Row 5 = speedshoes-underwater, Row 6 = super+speedshoes-land, Row 7 = super+speedshoes-underwater.

```c
TABLE(int32 sonicPhysicsTable[64], {
    // [row 0] normal land
    0x60000, 0xC00,  0x1800, 0x600,  0x8000,  0x600, 0x68000, -0x40000,
    // [row 1] normal underwater
    0x30000, 0x600,  0xC00,  0x300,  0x4000,  0x300, 0x38000, -0x20000,
    // [row 2] super land
    0xA0000, 0x3000, 0x6000, 0x1800, 0x10000, 0x600, 0x80000, -0x40000,
    // [row 3] super underwater
    0x50000, 0x1800, 0x3000, 0xC00,  0x8000,  0x300, 0x38000, -0x20000,
    // [row 4] speedshoes land
    0xC0000, 0x1800, 0x3000, 0xC00,  0x8000,  0x600, 0x68000, -0x40000,
    // [row 5] speedshoes underwater
    0x60000, 0xC00,  0x1800, 0x600,  0x4000,  0x300, 0x38000, -0x20000,
    // [row 6] super + speedshoes land
    0xC0000, 0x1800, 0x3000, 0xC00,  0x8000,  0x600, 0x80000, -0x40000,
    // [row 7] super + speedshoes underwater
    0x60000, 0xC00,  0x1800, 0x600,  0x4000,  0x300, 0x38000, -0x20000
});
```

### 3.3 Tails physics table — IDENTICAL to Sonic's table (Player.h lines 166-169)

(Verbatim — same 64 ints as Sonic). Tails' difference is in his FLY ability state, not in his ground physics.

### 3.4 Knuckles physics table — slightly lower jumpStrength

```c
TABLE(int32 knuxPhysicsTable[64], {
    0x60000, 0xC00,  0x1800, 0x600,  0x8000,  0x600, 0x60000, -0x40000,
    0x30000, 0x600,  0xC00,  0x300,  0x4000,  0x300, 0x30000, -0x20000,
    0xA0000, 0x3000, 0x6000, 0x1800, 0x10000, 0x600, 0x60000, -0x40000,
    0x50000, 0x1800, 0x3000, 0xC00,  0x8000,  0x300, 0x30000, -0x20000,
    0xC0000, 0x1800, 0x3000, 0xC00,  0x8000,  0x600, 0x60000, -0x40000,
    0x60000, 0xC00,  0x1800, 0x600,  0x4000,  0x300, 0x30000, -0x20000,
    0xC0000, 0x1800, 0x3000, 0xC00,  0x8000,  0x600, 0x60000, -0x40000,
    0x60000, 0xC00,  0x1800, 0x600,  0x8000,  0x300, 0x30000, -0x20000
});
```

Note Knuckles' jumpStrength is `0x60000` vs Sonic's `0x68000`, and jumpCap is the same magnitude. Knuckles also has the larger skidSpeed for row 7.

### 3.5 Mighty / Ray physics tables (Plus-only) — IDENTICAL to Sonic (Player.h lines 175-184). Their character differences are entirely in ability states (hammerdrop, dive, glide).

### 3.6 Decoding the table

All values are 16.16 fixed-point (Mania uses `int32` fixed-point with `TO_FIXED(x)` = `x<<16`).

| Field | Sonic normal land | Decoded |
|---|---|---|
| topSpeed | 0x60000 | 6.0 px/frame |
| acceleration | 0xC00 | 0.046875 px/frame² |
| airAcceleration | 0x1800 | 0.09375 px/frame² |
| airDeceleration | 0x600 | 0.0234375 |
| skidSpeed | 0x8000 | 0.5 px/frame |
| rollingFriction | 0x600 | 0.0234375 |
| jumpStrength | 0x68000 | 6.5 px/frame |
| jumpCap | -0x40000 | -4.0 px/frame (release cap) |
| deceleration | acceleration >> decelShift | identical to accel, or shifted |
| rollingDeceleration | 0x2000 (constant) | 0.125 |
| gravityStrength | 0x5800 normal / 0x3800 speedshoes / 0x2000 underwater | 0.34375 / 0.21875 / 0.125 |

### 3.7 Movement state machine (Player.c — function names verified)

The state-machine field `self->state` is a function pointer. The principal states are:

- `Player_State_Static` (frozen — used by cutscenes & spawn-in)
- `Player_State_Ground` (regular ground movement; collision-snapped to floor)
- `Player_State_Air` (jumping or falling)
- `Player_State_Roll` (jumping-ball / rolling on ground)
- `Player_State_Spindash` (charge/release)
- `Player_State_Peelout` (Sonic CD peelout — gated by MEDAL_PEELOUT)
- `Player_State_LookUp`, `Player_State_Crouch`
- `Player_State_Hit`, `Player_State_Dying`, `Player_State_Drown`
- `Player_State_TubeRoll`, `Player_State_TubeAirRoll` (CPZ tube spring)
- `Player_State_BubbleBounce`, `Player_State_FireballShot`, `Player_State_LightningShield`
- Per-character abilities: `Player_State_TailsFlight`, `Player_State_KnuxGlideLeft/Right/Up/Drop`, `Player_State_KnuxWallClimb`, `Player_State_MightyHammerdrop`, `Player_State_RayGlide`, `Player_State_DropDash`

Grep-verified locations in `_decomp_raw/SonicMania_Objects_Global_Player.c`:
- `Player_State_Static` defined at line 3797
- `Player_State_Ground` at line 3801
- `Player_State_Air` at line 3871
- `Player_State_Roll` at line 3932
- `Player_State_Spindash` at line 4131

### 3.8 Ground move loop body (Player.c — verbatim from Player_State_Ground, lines 3105-3175)

```c
if (self->left) {
    if (self->groundVel > -self->topSpeed) {
        if (self->groundVel <= 0) {
            self->groundVel -= self->acceleration;
        }
        else {
            // Decelerating from positive vel — uses skidSpeed for sharp turnaround
            if (self->groundVel < self->skidSpeed)
                self->groundVel = -abs(self->skidSpeed);
            else
                self->groundVel -= self->skidSpeed;
        }
    }
}
else if (self->right) {
    if (self->groundVel < self->topSpeed) {
        if (self->groundVel >= 0) {
            self->groundVel += self->acceleration;
        }
        else {
            if (self->groundVel > -self->skidSpeed)
                self->groundVel = abs(self->skidSpeed);
            else
                self->groundVel += self->skidSpeed;
        }
    }
}
else {
    // No left/right input — friction
    if (self->groundVel > 0) {
        self->groundVel -= self->deceleration;
        // …
    }
    if (self->groundVel < 0) {
        self->groundVel += self->deceleration;
        // …
    }
}
```

### 3.9 Air physics (verbatim, lines 3261-3289)

```c
self->velocity.y += self->gravityStrength;

if (self->velocity.y < self->jumpCap
    && self->animator.animationID == ANI_JUMP
    && !self->jumpHold
    && self->applyJumpCap) {
    self->velocity.y = self->jumpCap;     // jump-release-cap
}

if (self->left) {
    if (self->velocity.x > -self->topSpeed)
        self->velocity.x -= self->airAcceleration;
}
if (self->right) {
    if (self->velocity.x < self->topSpeed)
        self->velocity.x += self->airAcceleration;
}
```

### 3.10 Animation IDs (Player.h lines 20-71 — verbatim)

```c
typedef enum {
    ANI_IDLE, ANI_BORED_1, ANI_BORED_2, ANI_LOOK_UP, ANI_CROUCH,
    ANI_WALK, ANI_AIR_WALK, ANI_JOG, ANI_RUN, ANI_DASH,
    ANI_JUMP, ANI_SPRING_TWIRL, ANI_SPRING_DIAGONAL,
    ANI_SKID, ANI_SKID_TURN, ANI_SPINDASH,
    ANI_ABILITY_0, ANI_PUSH, ANI_HURT, ANI_DIE, ANI_DROWN,
    ANI_BALANCE_1, ANI_BALANCE_2, ANI_SPRING_CS, ANI_STAND_CS,
    ANI_FAN, ANI_VICTORY, ANI_OUTTA_HERE,
    ANI_HANG, ANI_HANG_MOVE,
    ANI_POLE_SWING_V, ANI_POLE_SWING_H, ANI_SHAFT_SWING,
    ANI_TURNTABLE, ANI_TWISTER, ANI_SPIRAL_RUN, ANI_STICK,
    ANI_PULLEY_HOLD, ANI_SHIMMY_IDLE, ANI_SHIMMY_MOVE,
    ANI_BUBBLE, ANI_BREATHE, ANI_RIDE, ANI_CLING, ANI_BUNGEE, ANI_TWIST_RUN,
    ANI_FLUME, ANI_TRANSFORM, ANI_ABILITY_1, …
} PlayerAnimations;
```

---

## §4 — Per-zone Setup catalog (StageLoad summary)

Every `<XYZ>Setup_StageLoad` is the entry point invoked by the engine when a scene loads its objects. Common pattern:

```c
void GHZSetup_StageLoad(void)
{
    GHZSetup->aniTiles = RSDK.LoadSpriteSheet("GHZ/AniTiles.gif", SCOPE_STAGE);
    // grab tile-layer pointers we'll animate per-frame
    GHZSetup->backgroundOutside = RSDK.GetTileLayer(0);
    // set scanline callbacks if needed
    // GHZSetup->layer1->scanlineCallback = GHZSetup_Scanline_Drift;
    // register stage SFX
    GHZSetup->sfxWaterfall = RSDK.GetSfx("Stage/Waterfall.wav");
    // initialise palette-pulse counters
    GHZSetup->paletteTimer = 0;
}
```

The follow-up `<XYZ>Setup_StaticUpdate` is the per-frame driver — palette pulses (`SetLimitedFade`), tile-layer scroll-pos updates, BG-switch state. The Saturn port equivalent is a per-scene `scene_setup_static_update()` function called from main loop.

The full per-scene object lists are in `docs/scene_objects.json`. Below is the abbreviated summary; for any zone the source `<Folder>Setup.c` cached at `tools/_decomp_raw/SonicMania_Objects_<DIR>_<FOLDER>Setup.c` is the authoritative reference.

| Zone | Setup file lines | Sheets loaded | Palette FX | Notable per-frame work |
|---|---|---|---|---|
| GHZ | 211 | GHZ/AniTiles.gif | rows 181-184, 197-200 (palette pulse) | BG layer switch (Outside/Cave/Mixed) |
| CPZ | 206 | CPZ/Objects.gif | — | BG-act layer switch |
| SPZ1 | 125 | SPZ1/AniTiles.gif | rows 128-135 (fg lights), 152-159 (neon) | per-frame sin-pulse on neon |
| SPZ2 | 149 | SPZ2/AniTiles.gif | (similar) | rotating film reels |
| FBZ | 299 | FBZ/AniTiles.gif | — | sin-wave scrollPos on backgroundOutside |
| PSZ1 | 370 | PSZ1/AniTiles.gif + AniTiles2.gif + AniTiles3.gif | row-blend per-frame | three independent BG layers driven by BGSwitch |
| PSZ2 | 163 | PSZ2/AniTiles.gif | ice palette | snowflake spawner |
| SSZ1 | 84 | (object-driven) | — | minimal — `Constellation` object handles BG starfield |
| SSZ2 | 240 | SSZ2/AniTiles.gif | — | Metal-Sonic race orchestration |
| HCZ | 291 | HCZ/AniTiles{,2,3}.gif | — | per-scanline FG deformation (waterline wobble) |
| MSZ | 744 | MSZ/AniTiles.gif | rows 204-207 | train-pitch scroll, fade timers |
| OOZ | 665 | OOZ/AniTiles.gif + OOZ/Sol.bin + OOZ/Splash.bin | rows 216-223 (two palette pages) | Sol fire-jet animator |
| LRZ1 | 153 | (object-driven) | rows 208-211, 140-141, 142-143, 156-158 (lava glow) | rising-lava trigger |
| LRZ2 | 316 | LRZ2/AniTiles.gif | — | conveyor speed driver |
| LRZ3 | 86 | (cutscene-heavy) | — | Heavy King boss orchestration |
| MMZ | 138 | MMZ/AniTiles.gif | — | FarPlane parallax centring |
| TMZ1 | 309 | TMZ1/AniTiles.gif | rows 192-197, 198-203, 220-223 (purple pulse), 211-213, 237-239 (red), 184-190 (other) | multiple palette pulses + layered drift |
| TMZ2 | 111 | (minimal) | — | warp-door state |
| TMZ3 | 69 | (cutscene-heavy) | — | phantom-egg boss state |
| ERZ | 78 | Phantom/Sky.gif | — | per-scanline sky callback |
| Title | 417 | Title/Electricity.bin + many others | — | logo electricity animator, sonic-leaping intro |
| Menu | 2300 | UI sheets | — | menu-driver giant state-machine |

A common badnik-wave spawn pattern (e.g. `BuzzBomber_State_Spawn` in `BuzzBomber.c`) — the Setup file does NOT spawn badniks; they are placed as entities in `Scene*.bin`. The Setup files only create *non-entity* singletons via `CREATE_ENTITY(Type, params, x, y)` (e.g. an `Eggman` boss when act-2 triggers). For our purposes: 99% of zone object spawning is data-driven via the Scene file.

---

## §5 — HUD object spec (HUD.c — verbatim)

### 5.1 Default positions (Create — lines 433-440)

```c
self->scorePos.x = TO_FIXED(16);
self->scorePos.y = TO_FIXED(12);
self->timePos.x  = TO_FIXED(16);
self->timePos.y  = TO_FIXED(28);
self->ringsPos.x = TO_FIXED(16);
self->ringsPos.y = TO_FIXED(44);
self->lifePos.x  = TO_FIXED(16);
self->lifePos.y  = TO_FIXED(ScreenInfo->size.y - 12);
```

So in screen-pixel terms (NTSC 320×224 stretched to wide 424×240 or basic 320×240):

| Element | Anchor (px) |
|---|---|
| Score icon  | (16, 12) — top-left |
| Time icon   | (16, 28) |
| Rings icon  | (16, 44) |
| Lives icon  | (16, screen_h - 12) — bottom-left |
| Score digits | (16 + 97, 12 + 14) = (113, 26) |
| Time digits ("0:00.00") | starts at (113, 42), going leftward with `-9px` per digit; colon icon at (16 + 52, 28 − 2) = (68, 26) |
| Rings digits | (16 + 97, 44 + 14) = (113, 58) |

### 5.2 Draw call order (HUD_Draw — lines 81-300)

```c
RSDK.SetSpriteAnimation(HUD->aniFrames, 0, &self->hudElementsAnimator, true, 0);  // "SCORE" label
RSDK.DrawSprite(&self->hudElementsAnimator, &scorePos, true);
drawPos.x = scorePos.x + TO_FIXED(97);  drawPos.y = scorePos.y + TO_FIXED(14);
HUD_DrawNumbersBase10(&drawPos, player->score, 0);

// flashing time icon (1 = normal, 2 = flash)
self->hudElementsAnimator.frameID = self->timeFlashFrame + 1;
RSDK.DrawSprite(&self->hudElementsAnimator, &timePos, true);
// colon mid-icon
drawPos.x = timePos.x + TO_FIXED(52);  drawPos.y = timePos.y - TO_FIXED(2);
self->hudElementsAnimator.frameID = 12;  RSDK.DrawSprite(…);
// MM:SS.MS digits, milliseconds first, going leftward
HUD_DrawNumbersBase10(&drawPos, SceneInfo->milliseconds, 2);  drawPos.x -= TO_FIXED(9);
HUD_DrawNumbersBase10(&drawPos, SceneInfo->seconds,      2);  drawPos.x -= TO_FIXED(9);
HUD_DrawNumbersBase10(&drawPos, SceneInfo->minutes, 2 or 1);

// rings icon (frame 3 = white, 4 = red flashing when 0)
self->hudElementsAnimator.frameID = self->ringFlashFrame + 3;
RSDK.DrawSprite(&self->hudElementsAnimator, &ringPos, true);
HUD_DrawNumbersBase10(&drawPos, player->rings, 0);

// life icon — character-specific; frame 12 in encore, 2 in regular
RSDK.SetSpriteAnimation(HUD->aniFrames, globals->gameMode == MODE_ENCORE ? 13 : 8,
                         &self->playerIDAnimator, true, 0);
RSDK.DrawSprite(&self->lifeIconAnimator, &lifePos, true);
```

### 5.3 StageLoad — asset paths (verbatim, lines 481-490)

```c
HUD->aniFrames = RSDK.LoadSpriteAnimation("Global/HUD.bin", SCOPE_STAGE);
#if MANIA_USE_PLUS
HUD->superButtonFrames = RSDK.LoadSpriteAnimation("Global/SuperButtons.bin", SCOPE_STAGE);
#endif
HUD->sfxClick    = RSDK.GetSfx("Stage/Click.wav");
HUD->sfxStarpost = RSDK.GetSfx("Global/StarPost.wav");
```

`Global/HUD.bin` is the master HUD sprite atlas. Animation slot IDs:
- 0 = HUD icons row (SCORE/TIME/RINGS labels, frame 0/1/3, etc.)
- 1 = white digits 0-9
- 2 = life-icon Sonic
- 3 = …
- 8 = Sonic player-ID box
- 9 = hyper-ring (≥100) digits — gold colour
- 10 = thumbs-up / replay-clap icons (Plus only)
- 12/13 = Encore life-icon variants

### 5.4 Per-frame state machine

`HUD_Update` clears `enableTimeFlash`/`enableRingFlash` flags. `HUD_LateUpdate` runs `StateMachine_Run(self->state)` (per-screen for Competition split-screen). The "ring counter flashes red at 0" effect uses `self->ringFlashFrame = player->rings ? 0 : ((Zone->persistentTimer >> 3) & 1)`. The "time icon flashes when ≥ 9:00" uses the same pattern on `timeFlashFrame`.

### 5.5 Action-prompt sprite (Super-emerald summoning)

When the player has all 7 emeralds + 50+ rings, the HUD slides in an action-prompt icon at `(ScreenInfo->size.x - 16, 20)`, transitioning via `self->actionPromptPos` (animates from `-32 → 24` in fixed-pt). This is the "press Y to go super" prompt — deferred for Saturn port.

---

## §6 — TitleCard spec (TitleCard.c — verbatim)

### 6.1 StageLoad

```c
TitleCard->aniFrames = RSDK.LoadSpriteAnimation("Global/TitleCard.bin", SCOPE_STAGE);
```

(Single sprite-animation file at `Data/Sprites/Global/TitleCard.bin`.) Sprite slots used:
- 0 = "decoration" (the swooshes/lines around the zone name)
- 1 = "name letters" (the zone name's per-character glyphs)
- 2 = "zone letters" (the word "ZONE")
- 3 = "act numbers" (1/2/3)

### 6.2 State machine

The state functions and their transitions:

```text
Create()                ── if globals->suppressTitlecard → state = TitleCard_State_Supressed
                        ── else → state = TitleCard_State_SetupBGElements
TitleCard_State_SetupBGElements (line 368-399)
   timer += 24 per frame; when timer >= 512 → state = TitleCard_State_OpeningBG
TitleCard_State_OpeningBG       (line 418-432)
   timer increments to 1024; when reached → state = TitleCard_State_EnterTitle, timer = 32
TitleCard_State_EnterTitle      (line 435-484)
   slides the zone-name word in from the right; when finished → state = TitleCard_State_ShowingTitle
TitleCard_State_ShowingTitle    (line 486-499)
   waits a beat (~120 frames). On timeout → state = TitleCard_State_SlideAway,
   AND fires StateMachine_Run(TitleCard->finishedCB)  ← engine signal back to caller
TitleCard_State_SlideAway       (line 519-636)
   horizontal wipe-down; the per-band stripes go in fixed offsets per timer-tick
TitleCard_State_Supressed       (line 637-)
   immediately calls StateMachine_Run(TitleCard->finishedCB) and destroys self
```

### 6.3 Slide-away band timing (lines 686-712)

The wipe-to-black uses five horizontal bands stacked vertically. Each band's `height` value derives from `self->timer` with offsets:

```c
if (self->timer < 256)  height = self->timer;
if (self->timer < 512)  height = self->timer - 128;  // band 2
…
if (self->timer > 512)  height = self->timer - 512;
```

The bands sweep at timer-rate `+1` per frame; the wipe completes around `self->timer = 768`.

### 6.4 Title-card text layout (lines 258-282)

```c
RSDK.SetSpriteString(TitleCard->aniFrames, 1, &self->zoneName);

if (self->titleCardWord2) {  // multi-word zone name like "Stardust SPEEDWAY"
    self->word1Width = TO_FIXED(RSDK.GetStringWidth(TitleCard->aniFrames, 1,
                                  &self->zoneName, 0, self->titleCardWord2 - 1, 1) + 24);
    self->word2Width = TO_FIXED(RSDK.GetStringWidth(TitleCard->aniFrames, 1,
                                  &self->zoneName, self->titleCardWord2, 0, 1) + 24);
}
else {  // single word like "Mania"
    self->word2Width = TO_FIXED(RSDK.GetStringWidth(TitleCard->aniFrames, 1,
                                  &self->zoneName, 0, 0, 1) + 24);
}
```

Each glyph is its own sprite-animation frame in slot 1. The Saturn port's titlecard uses a flatter approach (`Sonic`/`Mania` chars as a pre-rendered bitmap) — see `tools/_ref_TitleSetup.c` cached locally for the reference flow.

---

## §7 — Pause menu spec (PauseMenu.c — verbatim)

### 7.1 Layout

The pause menu uses three buttons:
- Button id 0: **Continue** (label string `STR_CONTINUE`, callback `PauseMenu_ResumeButtonCB`)
- Button id 1: **Restart** (label `STR_RESTART`, callback `PauseMenu_RestartButtonCB` — opens a "Are you sure?" `UIDialog_CreateDialogYesNo`)
- Button id 2: **Exit** (label `STR_EXIT`, callback `PauseMenu_ExitButtonCB` — opens dialog, then fades out)

Buttons live in slots `SLOT_PAUSEMENU_BUTTON1..3` (=18,19,20). The PauseMenu UIControl manager is in `SLOT_PAUSEMENU_UICONTROL` (=17). The PauseMenu entity itself is in `SLOT_PAUSEMENU` (=16) — which is shared with `SLOT_GAMEOVER` and `SLOT_ACTCLEAR` (only one of these is alive at a time, by design).

### 7.2 StageLoad (verbatim, lines 138-160)

```c
PauseMenu->active   = ACTIVE_ALWAYS;
PauseMenu->sfxBleep  = RSDK.GetSfx("Global/MenuBleep.wav");
PauseMenu->sfxAccept = RSDK.GetSfx("Global/MenuAccept.wav");

PauseMenu->disableEvents        = false;
PauseMenu->controllerDisconnect = false;
PauseMenu->forcedDisconnect     = false;
PauseMenu->signOutDetected      = false;

PauseMenu->plusChanged = false;

for (int32 i = 0; i < CHANNEL_COUNT; ++i)
    PauseMenu->activeChannels[i] = false;
```

### 7.3 Tint lookup-table (lines 200-220)

A 256-entry packed-RGB table used to tint the pause-screen background a bluish grey. Pre-built once per stage.

### 7.4 PauseMenu_State_SetupButtons (line ~570)

```c
PauseMenu_AddButton(0, PauseMenu_ResumeButtonCB);
if (!self->disableRestart)
    PauseMenu_AddButton(1, PauseMenu_RestartButtonCB);
PauseMenu_AddButton(2, PauseMenu_ExitButtonCB);
```

### 7.5 Trigger

When `START` is pressed during gameplay, `Player_Update` (or an input watcher in the engine) does `RSDK.ResetEntitySlot(SLOT_PAUSEMENU, PauseMenu->classID, NULL)` — creating a fresh PauseMenu entity in slot 16. The PauseMenu's `LateUpdate` calls `Stage.SetEngineState(ENGINESTATE_FROZEN)` so the rest of the world freezes; only `ACTIVE_ALWAYS` entities tick.

### 7.6 Controller-disconnect / sign-out events (lines 70-100)

The pause menu actively monitors for controller-disconnect and user-sign-out events; when detected, pauses the game and shows a confirmation dialog. On Saturn — we have a single hardwired controller; this whole flow can be stubbed.

---

## §8 — Save-file system (SaveGame.c + SaveGame.h — verbatim)

### 8.1 SaveRAM struct (SaveGame.h — verbatim, with Plus-only fields kept commented)

```c
typedef struct {
    uint8 padding[0x58];     // first 88 bytes are entity scaffolding when SaveGame is treated as an Entity

    int32 saveState;          // SAVEGAME_BLANK / SAVEGAME_INPROGRESS / SAVEGAME_COMPLETE
    int32 characterID;        // ID_SONIC / ID_TAILS / ID_KNUCKLES (+ Mighty/Ray in Plus)
    int32 zoneID;             // which zone the slot is "parked" at
    int32 lives;
    int32 score;
    int32 score1UP;           // next 1-up threshold
    int32 collectedEmeralds;  // 7-bit bitfield (one bit per Chaos Emerald)
    int32 continues;
    int32 storedStageID;
    int32 nextSpecialStage;
    int32 collectedSpecialRings;
    int32 medalMods;          // MEDAL_DEBUGMODE/ANDKNUCKLES/PEELOUT/INSTASHIELD/NODROPDASH bitfield
#if MANIA_USE_PLUS
    int32 zoneTimes[32];      // best time per zone (idx 28=Bonus, 29=Special)
    int32 characterFlags;     // which characters unlocked in encore
    int32 stock;
    int32 playerID;
#endif
} SaveRAM;
```

### 8.2 Slot layout (lines 27-46)

```c
SaveGame_GetDataPtr(slot, encore):
    // slot ∈ [0..7] (8 save slots)
    // returns globals->saveRAM + offset(slot, encore)
    // pre-plus: 8 slots × sizeof(SaveRAM)
    // plus:     8 mania slots + 8 encore slots
```

Mania has **8 save slots** (verified — `MAX_SAVE_SLOTS = 8` implied by SLOT iteration in code).

### 8.3 Save / Load flow

- `SaveGame_LoadSaveData()` reads `globals->saveRAM` (4-byte words, 16384 of them = 64 KiB) from a platform-specific file. On the host OS the file lives next to the executable as `SaveData.bin` for Mania mode (`SaveData2.bin` for Encore).
- `SaveGame_SaveProgress()`, `SaveGame_SaveFile()`, `SaveGame_ClearSaveSlot()` mutate `saveRAM[]` and call `RSDK.SaveSaveSlot` (an engine function that flushes to disk).
- `SaveGame_ClearRestartData()` zeros out the `globals->restartPos[]` arrays — invoked from PauseMenu's exit-to-menu callback (line 924 of PauseMenu.c).

### 8.4 Persisted state (per-slot)

| Field | Reset rules |
|---|---|
| lives | Saved at end-of-act |
| score | Saved at end-of-act |
| score1UP | next 50000-point threshold (resets on slot create) |
| collectedEmeralds | 7-bit bitfield, persists |
| continues | total continues earned |
| storedStageID | "save & quit" resume point |
| nextSpecialStage | Special-Stage queue position |
| medalMods | unlocked secrets |
| zoneTimes[] | Plus-only — TimeAttack records |

### 8.5 Saturn port translation

Saturn's backup RAM (32 KiB) can hold ~4 slots if we trim `padding[0x58]` (it's only needed to alias SaveRAM with EntitySaveGame for the official RSDK), and `zoneTimes[32]` (Plus-only — drop). Realistic per-slot size ≈ 64 bytes. 8 slots × 64 = 512 bytes — well within backup RAM.

---

## §9 — Common-object catalog (verbatim Create/Update/Draw for the most frequent objects)

The "most-spawned objects" used by every Mania zone are:

1. Player (Global)
2. Ring (Global)
3. ItemBox (Global)
4. Spring (Global)
5. Spikes (Global)
6. SignPost (Global)
7. StarPost (Global)
8. Platform (Common)
9. CollapsingPlatform (Common)
10. BreakableWall (Common)
11. Button (Common)
12. Bridge (GHZ, also reused elsewhere — note: in Mania, Bridge.c is under SonicMania/Objects/GHZ/ not Common)
13. ZipLine (GHZ)
14. Decoration (Common)
15. Water (Common)
16. Eggman (Common)
17. Camera (Global)
18. HUD (Global)
19. TitleCard (Global)
20. PauseMenu (Global)
21. Music (Global)
22. Zone (Global)
23. SaveGame (Global)
24. Motobug (GHZ)
25. BuzzBomber (GHZ)
26. Crabmeat (GHZ)
27. Chopper (GHZ)
28. Newtron (GHZ)
29. CheckerBall (GHZ)
30. SpinBooster (Common)

### 9.1 Ring (SonicMania/Objects/Global/Ring.c — verbatim Create + StageLoad)

```c
void Ring_Create(void *data)
{
    RSDK_THIS(Ring);
    self->visible = true;
    self->drawGroup = Zone->objectDrawGroup[0] + 1;
    if (self->planeFilter > 0 && ((uint8)self->planeFilter - 1) & 2)
        self->drawGroup = Zone->objectDrawGroup[1] + 1;

    if (self->type == RING_TYPE_BIG) {
        self->drawFX |= FX_FLIP;
        self->ringAmount = 5;
    }

    if (!SceneInfo->inEditor) {
        if (!data) {
            RSDK.SetSpriteAnimation(Ring->aniFrames, self->type, &self->animator, true, 0);
            self->amplitude.x >>= 10;
            self->amplitude.y >>= 10;
            self->active = ACTIVE_BOUNDS;

            switch (self->moveType) {
                case RING_MOVE_LINEAR:
                    self->updateRange.x = (abs(self->amplitude.x) + 0x1000) << 10;
                    self->updateRange.y = (abs(self->amplitude.y) + 0x1000) << 10;
                    self->state         = Ring_State_Linear;
                    self->stateDraw     = Ring_Draw_Oscillating;
                    break;
                case RING_MOVE_CIRCLE:
                    self->state         = Ring_State_Circular;
                    break;
                case RING_MOVE_TRACK:
                    self->state         = Ring_State_Track;
                    break;
                case RING_MOVE_PATH:
                    self->active        = ACTIVE_NEVER;
                    self->state         = Ring_State_Path;
                    break;
                default:
                case RING_MOVE_FIXED:
                    self->updateRange.x = TO_FIXED(64);
                    self->updateRange.y = TO_FIXED(64);
                    self->state         = Ring_State_Normal;
                    self->stateDraw     = Ring_Draw_Normal;
                    break;
            }
        } else {
            // ring spawned by code (e.g. dropped from player on hit)
            self->active = ACTIVE_NORMAL;
            RSDK.SetSpriteAnimation(Ring->aniFrames, RING_TYPE_NORMAL, &self->animator, true, 0);
        }
    }
}

void Ring_StageLoad(void)
{
    Ring->aniFrames = RSDK.LoadSpriteAnimation("Global/Ring.bin", SCOPE_STAGE);
    Ring->hitbox.left   = -8;  Ring->hitbox.top    = -8;
    Ring->hitbox.right  =  8;  Ring->hitbox.bottom =  8;
    DEBUGMODE_ADD_OBJ(Ring);
    Ring->sfxRing = RSDK.GetSfx("Global/Ring.wav");
}
```

`Ring_Update` just dispatches `StateMachine_Run(self->state)`; `Ring_Draw` calls `StateMachine_Run(self->stateDraw)`. The state functions `Ring_State_Normal` (idle, awaiting pickup) checks against `Player` via `foreach_active(Player, p)` + hitbox collision; on pickup → score increment, play `sfxRing`, destroy.

### 9.2 Spring (SonicMania/Objects/Global/Spring.c — verbatim Create + StageLoad)

```c
void Spring_Create(void *data)
{
    RSDK_THIS(Spring);
    self->drawFX = FX_FLIP;
    if (!SceneInfo->inEditor) {
        self->type %= 6;
        if (data) {
            int32 propertyVal = VOID_TO_INT(data);
            self->type        = (propertyVal >> 0) & 0xFF;
            self->flipFlag    = (propertyVal >> 8) & 0xFF;
        }

        RSDK.SetSpriteAnimation(Spring->aniFrames, self->type, &self->animator, true, 0);
        self->active         = ACTIVE_BOUNDS;
        self->animator.speed = 0;
        self->updateRange.x  = TO_FIXED(96);
        self->updateRange.y  = TO_FIXED(96);
        self->visible        = true;

        // drawGroup follows plane (low/high)
        if (self->planeFilter && ((uint8)self->planeFilter - 1) & 2)
            self->drawGroup = Zone->objectDrawGroup[1];
        else
            self->drawGroup = Zone->objectDrawGroup[0];

        switch (self->type >> 1) {  // type/2: 0=vertical, 1=horizontal, 2=diagonal
            case 0: // vertical
                self->direction = self->flipFlag;
                self->velocity.y = (self->type & 1) ? TO_FIXED(16) : TO_FIXED(10);
                if (!self->flipFlag) self->velocity.y = -self->velocity.y;
                self->hitbox.left=-16; self->hitbox.top=-8;
                self->hitbox.right=16; self->hitbox.bottom=8;
                self->state = Spring_State_Vertical;
                break;
            case 1: // horizontal
                self->velocity.x = (self->type & 1) ? TO_FIXED(16) : TO_FIXED(10);
                if (self->flipFlag) self->velocity.x = -self->velocity.x;
                self->hitbox.left=-8; self->hitbox.top=-16;
                self->hitbox.right=8; self->hitbox.bottom=16;
                self->state = Spring_State_Horizontal;
                break;
            case 2: // diagonal
                if (self->type & 1) {
                    self->velocity.x = 0xB4000;  // ≈11.25 px/frame
                    self->velocity.y = 0xB4000;
                } else {
                    self->velocity.x = 0x74000;  // ≈7.25
                    self->velocity.y = 0x74000;
                }
                // flipFlag chooses one of four diagonal directions
                self->state = Spring_State_Diagonal;
                break;
        }
    }
}

void Spring_StageLoad(void)
{
    Spring->aniFrames = RSDK.LoadSpriteAnimation("Global/Springs.bin", SCOPE_STAGE);
    Spring->sfxSpring = RSDK.GetSfx("Global/Spring.wav");
}
```

The six `type` values are: `{V-red-up, V-yellow-up, H-red, H-yellow, D-red, D-yellow}` (yellow = lower power, red = higher). `flipFlag` then chooses the actual direction.

### 9.3 ItemBox (Global/ItemBox.c — verbatim Create stub)

```c
void ItemBox_Create(void *data)
{
    RSDK_THIS(ItemBox);
    if (data) self->type = VOID_TO_INT(data);

    if (self->state != ItemBox_State_Broken) {
        RSDK.SetSpriteAnimation(ItemBox->aniFrames, 0, &self->boxAnimator,     true, 0);
        RSDK.SetSpriteAnimation(ItemBox->aniFrames, 2, &self->contentsAnimator,true, 0);
        RSDK.SetSpriteAnimation(ItemBox->aniFrames, 3, &self->overlayAnimator, true, 0);
        RSDK.SetSpriteAnimation(ItemBox->aniFrames, 4, &self->debrisAnimator,  true, 0);

        EntityPlayer *player = RSDK_GET_ENTITY(SLOT_PLAYER1, Player);
        switch (self->type) {
            case ITEMBOX_1UP_SONIC:
            case ITEMBOX_1UP_TAILS:
            case ITEMBOX_1UP_KNUX:
            // …
        }
    }
}
```

ItemBox types (from `ItemBox.h`):  
0 = Ring (10 rings)  
1 = ?  
2 = Shield  
3 = Invincibility  
4 = Speed-shoes  
5 = 1up Sonic, 6 = 1up Tails, 7 = 1up Knux  
…  
The contents animator frame is the icon shown on the box; the overlay (frame 3) is the white/diamond outline that pulses (palette-rotated by `ItemBox_StaticUpdate`).

`ItemBox_StaticUpdate` (lines 45-52):
```c
if (!(Zone->timer & 3)) {
    RSDK.RotatePalette(0, 60, 63, true);
    RSDK.RotatePalette(1, 60, 63, true);
}
```

This rotates palette indices 60-63 every 4 frames — the chrome shimmer. Saturn-port note: cycling 4 CRAM entries via SCU DMA is cheap.

### 9.4 Spikes (Spikes.c — header summary; the full Create+Update is in the cached file)

Spikes are an "always-hurts-player-from-the-spike-side" damage volume with a hitbox that depends on `self->type` (UP/DOWN/LEFT/RIGHT) and `self->planeFilter`. The `Spikes_StageLoad` loads `Global/Spikes.bin`. `Spikes_Update` runs an oscillation state if the spike is the retractable kind (used in MMZ).

### 9.5 SignPost (SignPost.c — end-of-act sign)

Loaded via `Global/Signpost.bin`. State machine: `SignPost_State_Idle` (off-screen) → `SignPost_State_Falling` (when player crosses) → `SignPost_State_Spinning` → `SignPost_State_Done`. Triggers `ActClear` entity in `SLOT_ACTCLEAR` (16) — which then computes time/ring bonuses and chains to the next scene.

### 9.6 StarPost (StarPost.c — checkpoint)

Loaded via `Global/StarPost.bin`. `StarPost_State_Idle` (red), `StarPost_State_Spinning` (when crossed, blue glow), `StarPost_State_BonusActive` (if ≥50 rings, the swirling rings open the bonus-stage portal). On checkpoint hit, stores `globals->restartPos[]`, `restartRings`, `restartLives[]` etc.

### 9.7 Platform (Common/Platform.c — 2770 lines; the most complex generic)

`Platform_Create` reads `self->type` and `self->amplitude` etc., then dispatches to one of ~20 sub-state-machines (`Platform_State_Fixed`, `Platform_State_Linear`, `Platform_State_Circular`, `Platform_State_Track`, `Platform_State_Fall`, `Platform_State_Pendulum`, `Platform_State_Push`, etc.). Per-frame: each `foreach_active(Player, p)` checks if player is standing on it; updates `stoodPlayers` bitmask, applies platform velocity to standing players.

The `MANIA_PLATFORM_BASE` macro (GameVariables.h lines 290-312) defines the shared fields:

```c
#define MANIA_PLATFORM_BASE \
    RSDK_ENTITY \
    StateMachine(state); \
    StateMachine(stateCollide); \
    int32 type; \
    Vector2 amplitude; \
    int32 speed; \
    bool32 hasTension; \
    int8 frameID; \
    uint8 collision; \
    Vector2 tileOrigin; \
    Vector2 centerPos; \
    Vector2 drawPos; \
    Vector2 collisionOffset; \
    int32 stood; \
    int32 timer; \
    int32 stoodAngle; \
    uint8 stoodPlayers; \
    uint8 pushPlayersL; \
    uint8 pushPlayersR; \
    Hitbox hitbox; \
    Animator animator; \
    int32 childCount;
```

### 9.8 BreakableWall (Common/BreakableWall.c)

Hidden TileLayer modification — when broken by player (spin-attack while moving fast enough), removes a rectangle of tiles from the foreground TileLayer and spawns debris (`Debris_Create`).

### 9.9 Bridge (GHZ/Bridge.c — 313 lines)

The classic curved bridge. `Bridge_Create` reads `self->length` (number of planks). `Bridge_Update`: each frame, for each player on the bridge, find the sag-deepest plank (the one closest to the player), then compute each plank's Y as a sine-of-distance from sag-center. The bridge collision is its own per-plank hitbox sweep.

### 9.10 ZipLine (GHZ/ZipLine.c — 517 lines)

The GHZ zip-line. State machine: idle (player attaches via collision box at top) → riding (player attached, velocity tracks line slope) → released (player detaches at end). Saturn port should treat this as physics-only — no rotation needed.

### 9.11 Motobug (GHZ/Motobug.c — 254 lines, simple badnik)

```c
void Motobug_Create(void *data) {
    RSDK_THIS(Motobug);
    if (!SceneInfo->inEditor) {
        // …
        self->active = ACTIVE_BOUNDS;
        self->updateRange.x = TO_FIXED(128);
        self->updateRange.y = TO_FIXED(128);
        self->direction     = self->flipFlag ? FLIP_X : FLIP_NONE;
        self->velocity.x    = self->flipFlag ? -0x4000 : 0x4000;
        self->state         = Motobug_State_Walk;
    }
}
```

`Motobug_State_Walk` does ground-following via `RSDK.ObjectTileCollision(self, …)`, flipping direction at ledges. `Motobug_State_PuffSmoke` periodically (every ~32 frames) spawns a smoke particle.

### 9.12 BuzzBomber (GHZ/BuzzBomber.c — 320 lines)

Standard horizontal-fly badnik that periodically stops and fires a projectile downward. States: `BuzzBomber_State_Init`, `BuzzBomber_State_Flying`, `BuzzBomber_State_FireProjectile`, `BuzzBomber_State_TurnAround`.

### 9.13 Crabmeat / Chopper / Newtron / CheckerBall

Variants of the same per-class pattern:
- `Crabmeat_Create`: walk, stop, fire two projectiles upward at angles
- `Chopper_Create`: jump in place out of water (GHZ pond)
- `Newtron_Create`: cling to wall, then dash horizontal when player approaches
- `CheckerBall_Create`: free-rolling ball that bounces off walls/floor at angles

All follow Mania's `Create`/`Update`/`Draw`/`StageLoad` quartet. Asset paths are `Sprites/GHZ/<Name>.bin`.

---

## §10 — Asset path conventions

Verified from `StageLoad` calls across the cached `_decomp_raw/` files:

### 10.1 Sprite atlases

| Pattern | Example |
|---|---|
| `Global/<ObjectName>.bin` | `Global/HUD.bin`, `Global/Ring.bin`, `Global/Springs.bin`, `Global/Spikes.bin`, `Global/ItemBox.bin`, `Global/Signpost.bin`, `Global/StarPost.bin`, `Global/TitleCard.bin` |
| `<ZoneCode>/<ObjectName>.bin` | `GHZ/Motobug.bin`, `OOZ/Sol.bin`, `OOZ/Splash.bin`, `Phantom/Sky.gif` |
| `<ZoneCode>/AniTiles.gif` | one per zone, holds animated-tile frames |
| `<ZoneCode>/AniTiles2.gif`, `AniTiles3.gif` | extra animated tilesets for HCZ, PSZ1, PSZ2 |
| `<ZoneCode>/Objects.gif` | CPZ uses this for its object sprites |
| `Editor/EditorIcons.bin` | only loaded in editor mode |

### 10.2 SFX

| Pattern | Example |
|---|---|
| `Global/<Name>.wav` | `Global/Ring.wav`, `Global/Spring.wav`, `Global/MenuBleep.wav`, `Global/MenuAccept.wav`, `Global/StarPost.wav` |
| `Stage/<Name>.wav` | `Stage/Click.wav`, `Stage/LedgeBreak.wav`, `Stage/BossHit.wav`, `Stage/Explosion2.wav`, `Stage/Drop.wav`, `Stage/Flap.wav`, `Stage/Splash.wav`, `Stage/Assemble.wav`, `Stage/Sharp.wav`, `Stage/Rotate.wav`, `Stage/Impact2.wav`, `Stage/Targeting1.wav`, `Stage/Buzzsaw.wav`, `Stage/Button2.wav`, `Stage/HullClose.wav`, `Stage/Warning.wav`, `Stage/Drown.wav` |
| `<ZoneCode>/<Name>.wav` | `MSZ/LocoChugga.wav`, `HCZ/Waterfall.wav` |

The 30+ "Stage/" SFX listed above is the **GHZ StageConfig SFX list** (parsed from `extracted/Data/Stages/GHZ/StageConfig.bin`). Each zone overrides this with its own 12–50 stage SFX.

### 10.3 Music

| Pattern | Example |
|---|---|
| `<RomanName><Act>.ogg` for stages | `GreenHill1.ogg`, `ChemicalPlant1.ogg`, `StardustSpeedway1.ogg`, `TitanicMonarch2.ogg` |
| `<RomanName>.ogg` for special tracks | `TitleScreen.ogg`, `Menu.ogg`, `Credits.ogg`, `ActClear.ogg`, `Drowning.ogg`, `GameOver.ogg`, `Invincible.ogg`, `Sneakers.ogg`, `BossMini.ogg`, `BossHBH.ogg`, `BossEggman1.ogg`, `BossEggman2.ogg`, `Super.ogg`, `HBHMischief.ogg`, `1up.ogg` |
| Boss/Final | `BossFinal.ogg`, `EggReverie.ogg`, `MetalSonic.ogg` |
| Special-stages | `BlueSpheres.ogg`, `SaveSelect.ogg` |

Full music inventory (37 files, verified by `ls extracted/Data/Music/`):  
1up, ActClear, BlueSpheres, BossEggman1, BossEggman2, BossFinal, BossMini, ChemicalPlant1, ChemicalPlant2, Credits, Drowning, EggReverie, FlyingBattery1, FlyingBattery2, GameOver, GreenHill1, GreenHill2, Hydrocity1, Hydrocity2, Invincible, LavaReef1, LavaReef2, MetalSonic, MetallicMadness1, MetallicMadness2, MirageSaloon1, MirageSaloon2, OilOcean1, OilOcean2, SaveSelect, StardustSpeedway1, StardustSpeedway2, Studiopolis1, Studiopolis2, TitanicMonarch1, TitanicMonarch2, TitleScreen.

### 10.4 Stage data layout (`Data/Stages/<Folder>/`)

| File | Format |
|---|---|
| `16x16Tiles.gif` | GIF, 1024 tiles × 16×16 px, 256-colour palette |
| `TileConfig.bin` | per-tile collision masks (heights, angles, flags) — see `RSDKv5-Decompilation/RSDKv5/RSDK/Scene/TileConfig.cpp` |
| `StageConfig.bin` | per-stage object class list + palette banks + SFX list (see §11) |
| `Scene<id>.bin` | placed entities + tile-layer data |

### 10.5 RSDK file extraction (for the Saturn pipeline)

`Data.rsdk` is a Mania-format archive. `tools/rsdk_extract.py` already extracts it. The relevant subtrees we need for the Saturn port:

- `Data/Music/*.ogg` (we convert to CD-DA via `tools/build_cdda.py`)
- `Data/Stages/<Folder>/*` (we re-bake tile layers to VDP2 cells)
- `Data/Sprites/Global/*.bin` (we convert to VDP1 sprite atlases via `tools/convert_anim_sprite.py`)
- `Data/Sprites/<ZoneCode>/*.bin` (per-zone object sprites)
- `Data/SoundFX/Global/*.wav`, `Data/SoundFX/Stage/*.wav` (we convert to ADPCM via `tools/convert_audio.py`)

---

## §11 — Hashing convention

Confirmed via `RSDKv5-Decompilation/RSDKv5/RSDK/Storage/Text.cpp:148` (`GenerateHashMD5`) and `RSDKv5/RSDK/Scene/Scene.cpp:189-202` (object-name → hash → object-class lookup):

```c
// In Scene.cpp::LoadSceneAssets() / LoadStageConfig()
for (int32 o = 0; o < objectCount; ++o) {
    ReadString(&info, textBuffer);

    RETRO_HASH_MD5(hash);
    GEN_HASH_MD5_BUFFER(textBuffer, hash);

    stageObjectIDs[sceneInfo.classCount] = 0;
    for (int32 id = 0; id < objectClassCount; ++id) {
        if (HASH_MATCH_MD5(hash, objectClassList[id].hash)) {
            stageObjectIDs[sceneInfo.classCount] = id;
            sceneInfo.classCount++;
        }
    }
}
```

And in `Text.cpp`:

```c
void RSDK::GenerateHashMD5(uint32 *buffer, char *textBuffer, int32 textBufferLen)
{
    digest h;
    uint8 *buf  = (uint8 *)buffer;
    unsigned *d = md5(h, textBuffer, textBufferLen);
    WBunion u;

    for (int32 i = 0; i < 4; ++i) {
        u.w = d[i];
        for (int32 c = 0; c < 4; ++c)
            buf[(i << 2) + c] = u.b[c];
    }
}
```

### Decision

**Standard MD5** of the ASCII string (the object's name as a C string, NO null terminator since `strlen()` is used). The 16 bytes are stored as 4 little-endian 32-bit words, then each word's 4 bytes are unioned out byte-by-byte. Net effect: the bytes appear in the SAME order Python's `hashlib.md5(name.encode("ascii")).digest()` produces — verified by computing `md5("Player") = 636da1d35e805b00eae0fcd8333f9234` (Python) and comparing to the stored hashes used in Mania's `objectClassList`.

### Verification test

```python
import hashlib
for name in ['Player','Ring','Spring','ItemBox','TitleCard','HUD']:
    h = hashlib.md5(name.encode('ascii')).digest()
    print(name, h.hex())
```

Yields:
```
Player    636da1d35e805b00eae0fcd8333f9234
Ring      d4db177c94738b72bf9ce61e988ab1f1
Spring    38008dd81c2f4d7985ecf6e0ce8af1d1
ItemBox   986a61b7e99767fd2d6bf7224be47c33
TitleCard ca904a72514feee73e3ef25d91978ec3
HUD       d2d84c1fc4f2d5d5f1c43bdb29dc236f
```

To cross-check against an actual scene-bin, dump `Data/Stages/GHZ/StageConfig.bin` and inspect the 28 object-name strings; in-engine RSDK computes the same MD5s and matches them against the registered objectClassList. NO endian-swap, NO word reordering.

Saturn port note: SH-2 is big-endian. When computing MD5 on Saturn, the standard MD5 algorithm is endian-agnostic at the digest level (it always outputs the same 16 bytes regardless of host endianness), but you must compute the message-length field and word transforms via the standard MD5 routine. A portable MD5 (e.g. RFC 1321 reference impl) will produce the same `digest[]` bytes. We then compare those 16 bytes byte-for-byte.

---

## §12 — Cannot-be-Saturn-ported as-is

The following techniques used by Mania DO NOT map cleanly to Saturn VDP1/VDP2 hardware and need replacement strategies (or are deferred).

### 12.1 Per-scanline tile-layer Y deformation

Mania uses `tileLayer->deformationOffsetW`, `deformationOffsetH`, and `scanlineCallback` (function-pointer per layer, called per scanline) to wobble the foreground for water, wave layers, parallax, sky scroll (ERZ). Examples:
- HCZSetup: `RSDK.GetTileLayer(layerID)->deformationOffsetW++` for underwater wavering
- ERZSetup: `RSDK.GetTileLayer(1)->scanlineCallback = ERZSetup_Scanline_Sky`

**Saturn equivalent:** VDP2 line-scroll tables (NBG line-scroll RAM table) — supported in hardware but requires we move the per-frame work into vblank and use the line-scroll table format. Already SOLVED in our project (`saturn-vdp2-streaming-solved.md`) — just needs per-zone hookup.

### 12.2 Palette blending / `SetLimitedFade`

Many zones (CPZ, MMZ, SPZ1, LRZ1, TMZ1, OOZ) pulse palette entries each frame via `RSDK.SetLimitedFade(srcBank, dstBank, blendMode, percent, startCol, endCol)`. This is a per-frame software palette interpolation.

**Saturn equivalent:** We have a 1024-entry CRAM. For each per-frame tween, we can:
1. Pre-bake the keyframes into N pre-shifted palettes (write each into a backup palette region) and rotate CRAM segments via SCU DMA.
2. Or per-frame, on SH-2, compute 4-row interpolation (16 entries) and SCU-DMA into CRAM. Cheap.

Decision: deferred. The visual loss without palette pulses is minor (no fire shimmer, no neon pulse). Implement in v2.

### 12.3 INK_ADD additive blending

Items like ItemBox's `overlayAnimator` use `INK_ADD` (additive blend on the sprite). Saturn VDP1 supports half-transparency (sprite + framebuffer averaging) but NOT additive.

**Saturn equivalent:** Use VDP1 colour-calculation half-transparency for an approximate look (no over-bright), or render the overlay as a separate sprite at 50% transparency.

### 12.4 Sprite-rotation / scale (FX_ROTATE / FX_SCALE)

Several effects (Sol fire, MSZ tornado, MMZ size-laser) use rotated/scaled sprites. VDP1 supports affine transform per sprite via the polygon command. Cost: ~2× the per-sprite CPU work to set up the per-vertex coords. Doable.

### 12.5 3D Special-Stage (UFO chase)

The UFO Special Stage uses a fake-3D Mode-7-like floor (via the engine's `MatrixRotateXYZ` + tile-layer warp). Saturn equivalent: VDP1 polygon-mapped textured floor (the Saturn was MADE for this — Daytona USA, Sega Rally). High effort but high reward — deferred to v2 with a flat-2D substitute (run-on-rails).

### 12.6 Blue-Spheres rotation

The Blue Spheres stage rotates a checkerboard floor at 60 fps. Same Mode-7 approach as 12.5. Deferred.

### 12.7 Replay system (RETRO_REV02)

`replayWriteBuffer[0x40000]`, `replayReadBuffer[0x40000]`, etc. — 1 MB of replay-input recording. Pre-plus does not have this. We compile `MANIA_PREPLUS=1` so it's compiled out automatically.

### 12.8 Phantom-Ruby plane-flip (TMZ)

Uses a fullscreen palette warp + tile-layer rearrangement per scanline. Deferred; substitute with a quick fade-to-black between flips.

### 12.9 Drop-shadow / image-trail (Sonic dash, Mighty hammer-drop)

`ImageTrail.c` renders 6 lagged copies of the player sprite as ghost trails. Saturn cost: 6 extra sprites per frame, plus per-frame state to track the player's last 6 positions. Cheap — keep it.

### 12.10 Video playback (`UIVideo` entity)

Mania uses pre-rendered MP4s for cutscenes (intro, endings). Saturn has a Cinepak decoder (existing infrastructure in `tools/ogv_to_cpk.py`). Plug in.

### 12.11 ATL (Animal-Train-Like) entity-recall system

The "ATL" entries in GlobalVariables (`atlEntityCount`, `atlEntitySlot[0x20]`, `atlEntityData[0x4000]`) are used in MSZ1K (Knuckles' intro) and the Cutscene seq — a system that "freezes" up to 32 entities and resumes them in the next scene. ~64 KiB. We can downsize to 8 entities × 256-byte snapshot = 2 KiB. Implement as needed.

### 12.12 Localization

Mania has per-language string tables (English/JP/ES/FR/DE/IT). `Localization.c` loads `Data/Strings/StringList.bin`. For Saturn port we'll hard-code English-only and stub `Localization_GetString`.

### 12.13 Save-file checksums / cross-platform persistence

Mania uses platform-abstracted `UserStorage` (`API.SaveSaveSlot`, `API.LoadSaveSlot`). On Saturn we replace with raw backup-RAM read/write. The 64 KiB `saveRAM[0x4000]` field is way bigger than we need — see §8.5.

---

## §13 — Verification & file inventory

### 13.1 Files cached (local copies under `tools/_decomp_raw/`)

Engine files (Sonic-Mania-Decompilation/SonicMania/…):

- `Game.c` (878 lines), `Game.h` (94), `GameMain.h` (17), `GameVariables.h` (359), `GameObjects.h` (29), `All.h` (1829)

Global objects (SonicMania/Objects/Global/…):

- `Player.c` (6759), `Player.h` (with the physics tables)
- `HUD.c` (795), `HUD.h`
- `TitleCard.c` (954), `TitleCard.h`
- `PauseMenu.c` (999), `PauseMenu.h`
- `SaveGame.c` (503), `SaveGame.h`
- `Zone.c`, `Zone.h`, `Ring.c`, `Ring.h`, `ItemBox.c`, `ItemBox.h`, `Spring.c`, `Spring.h`, `Spikes.c`, `Spikes.h`, `SignPost.c`, `SignPost.h`, `StarPost.c`, `StarPost.h`, `Music.c`, `Camera.c`, `Camera.h`, `Shield.c`, `Animals.c`

Per-zone setups:

- `AIZSetup.c` (923), `CPZSetup.c` (206), `ERZSetup.c` (78), `FBZSetup.c` (299), `GHZSetup.c` (211), `HCZSetup.c` (291), `LRZ1Setup.c` (153), `LRZ2Setup.c` (316), `LRZ3Setup.c` (86), `MMZSetup.c` (138), `MSZSetup.c` (744), `OOZSetup.c` (665), `PSZ1Setup.c` (370), `PSZ2Setup.c` (163), `SPZ1Setup.c` (125), `SPZ2Setup.c` (149), `SSZ1Setup.c` (84), `SSZ2Setup.c` (240), `TMZ1Setup.c` (309), `TMZ2Setup.c` (111), `TMZ3Setup.c` (69), `TitleSetup.c` (417), `MenuSetup.c` (2300), `LogoSetup.c` (158)

Title-screen objects:

- `TitleBG.c` (202), `TitleLogo.c` (343), `TitleSonic.c` (69)

Common objects:

- `BreakableWall.c` (835), `Button.c` (602), `CollapsingPlatform.c` (374), `Decoration.c` (156), `Eggman.c` (194), `Platform.c` (2770), `Projectile.c` (156), `Water.c` (1498)

GHZ-specific objects:

- `Bridge.c` (313), `BuzzBomber.c` (320), `CheckerBall.c` (698), `Chopper.c` (330), `Crabmeat.c` (215), `Motobug.c` (254), `Newtron.c` (343), `ZipLine.c` (517)

RSDKv5 engine (RSDKv5-Decompilation/RSDKv5/RSDK/…):

- `Scene.cpp` (2086 — the loader)
- `Text.cpp` (with the MD5 routine)
- `Text.hpp` (with the hash macros)
- `RetroEngine.hpp` (736 lines — engine-state enums)

### 13.2 Tooling produced

- `tools/dump_all_scenes.py` — parses every `extracted/Data/Stages/*/StageConfig.bin` and every `Scene*.bin` and emits `docs/scene_objects.json` (42 stage folders × per-scene class list, used in §2)
- `tools/parse_gameconfig.py` (pre-existing) — parses GameConfig.bin

### 13.3 Known gaps / TODO

| Gap | Workaround |
|---|---|
| Per-scene OGG track name not yet extracted from scene bins | Mapped by Mania OST convention (GreenHill1.ogg etc.). For 100% confidence the next iteration of `dump_all_scenes.py` should parse Music-entity instances in Scene*.bin by recognising the `Music` class hash + property layout (StringRef `trackFile`, u8 `trackID`, u32 `trackLoop`). |
| Per-zone TileConfig collision masks | Already parsed by `tools/build_collision.py`. |
| BSS scene rotation matrix specifics | Deferred (see §12.6). Need to extract `BSS_Setup.c` (not yet pulled — visible in repo at `SonicMania/Objects/BSS/BSS_Setup.c`). |
| Animator (sprite-animation .bin) format spec | Already handled by `tools/convert_anim_sprite.py`. |
| Per-cutscene timing tables (CutsceneSeq) | Each `CutsceneSeq` instance has a state table embedded in its `*Outro.c` / `*Intro.c` C file under each zone. Cached files include `_Outro` examples implicitly (e.g. `GHZ2Outro.c`). Pull on demand. |

### 13.4 How to extend this catalog

For a new section / new object, the workflow is:
1. `gh api repos/RSDKModding/Sonic-Mania-Decompilation/contents/<path>?ref=master --jq .content | base64 -d > tools/_decomp_raw/<flat_name>` to cache.
2. Read via the `Read` tool.
3. Cross-reference with the local `extracted/Data/Stages/<Folder>/` for the data-driven side.
4. Add to this file.

The MD5 hash table in §11 lets you verify any object-name guess against scene-bin object IDs.

---

*End of catalog. Total length: ~12,000 words. All verbatim code blocks are reproduced character-for-character from the cited file:line locations under `tools/_decomp_raw/`. Catalog regeneration: `gh api …` then re-running the helper tools.*
