# Sonic Mania Saturn — Decomp Port Status Tracker

**Generated:** 2026-05-26 (Phase 0.5 foundation alignment)
**Source list:** `tools/decomp_objects_list.txt` (610 entries)
**Source repo:** https://github.com/RSDKModding/Sonic-Mania-Decompilation

This document tracks the per-file status of porting the official Sonic
Mania Decompilation source tree onto the Saturn engine-compat layer
(`src/rsdk/`). The port strategy is: take each `SonicMania/Objects/*.c`
file VERBATIM (or with minimal Saturn-side adaptations confined to a
`#ifdef SATURN` block) into `src/mania/Objects/*` and have it
compile + link against the `src/rsdk/` API surface documented in
`docs/RSDK_TO_SATURN_API_MAP.md`.

## 2026-07-01 audit note — TWO TRACKS: `src/mania` targets below are the RETIRED hand-port; the LIVE build is the P6 engine track

Measured audit pass (offline, read-only) at HEAD `cc9f333` on branch
`ghzcutscene-intro-ghz-loop`:

- The paragraph above describes the ORIGINAL strategy (port each decomp
  `.c` onto the `src/rsdk/` compat layer as `src/mania/Objects/*`). That
  hand-port track is RETIRED. The LIVE build is the P6 true-port engine
  track (`tools/_portspike/_p6/` + `platform/Saturn/` + the `rsdkv5-src`
  engine TUs, phases P6.1-P6.8): `build_p6scene_objs.sh` compiles the
  UNMODIFIED RSDKv5 engine TUs (Reader/Scene/Storage/Object/Collision/
  Animation/Audio/Input/Link/Math/RetroEngine/Sprite/Text) plus VERBATIM
  decomp object TUs straight from `tools/_decomp_raw/` (w1 loop :306-313,
  w2 :336-362, w4 :385-409, front-end blocks :415-538).
- Rows updated 2026-07-01 with status `ENGINE_VERBATIM` name the engine
  track (following the pre-existing Localization/LogHelpers/Options and
  GHZCutsceneST row precedents). Rows still pointing at `src/mania/*`
  with PORTED/BUILDS/PARTIAL statuses describe the retired hand-port
  only; the GHZ Act 1 histogram table below (Phase 2.4-master) is a
  hand-port-era snapshot kept for history.
- `tools/_decomp_raw/` holds 1174 files (CLAUDE.md §2 "41 files" is stale).
- Registered classes on the engine track: 27 pack
  (`RSDK_REGISTER_OBJECT`, `p6_wave1_reg.c:156-189`) + 18 always-on
  overlay (`register_object_full`, `p6_ovl_ghz.c:384-716`) = 45 in the
  default Green Hill Zone shipping build; front-end flavors add LOGOS 2
  + TITLE 3 (+2 default-off TitleBG/Title3DSprite per
  P6_TITLEBG_SPRITES_OFF, `build_p6scene_objs.sh:93-101`) + MENU 10 +
  AIZ 8 + GHZCUT 2 = 72 total.
- Decomp-authority spot-audit result: the cached decomp is compiled
  directly; 2 of 1174 cached files carry `#if`-gated Saturn arms with
  the PC path verbatim in `#else` (`TitleBG.c:119-122` scanline-callback
  install; `CutsceneHBH.c:186-214` palette no-ops), 1 generated header
  diverges (`tools/_portspike/_p67d_sizing/include/Cutscene/
  CutsceneHBH.h:50-62`, colors[128]->[1], gated), and 2 build-local sed
  patches exist (MenuSetup `GameInfo->platform` REV02 arm,
  `build_p6scene_objs.sh:460-465`; FXRuby GetTintLookupTable REV02 arm,
  :505-506). No silent logic edits found in the cache.

## Status legend

- `NOT_STARTED` — file has not yet been ported. The Saturn target
  path is empty.
- `IN_PROGRESS` — file is being ported in the current turn. Some
  functions may be stubbed.
- `BUILDS` — file compiles + links into the Saturn binary but is
  not yet exercised at runtime.
- `RUNTIME` — file is exercised at runtime (callbacks fire, entities
  spawn, etc.) but visual / gameplay fidelity may diverge from PC.
- `PARITY` — file produces frame-to-frame match with PC reference
  per the visual gate in `tools/verify_done.ps1`.

## Phase 3.0-prep++ update (2026-05-28) — whole-game asset audit landed

The whole-game asset side of every NOT_STARTED row is now resolved at the
data level: every retail asset referenced by every cached decomp `.c`
file is present in `extracted/Data/` (Gate V3.0-prep++ GREEN per
`tools/qa_phase3_0_plus_whole_game_asset_coverage_gate.py`). 894 new
upstream `.c`/`.h` files were batch-fetched into `tools/_decomp_raw/`.

Per-scene object/asset counts: see `docs/whole_game_asset_audit.md`.
Recommended implementation-level phase ordering: same doc, "Implementation-
level roadmap" section. Port-status of individual `.c` files below is
unchanged (still NOT_STARTED for everything outside Phase 1.x Title set
and the Phase 2.x partial GHZ work), but the prerequisite data is now
ready.

## Phase 2.4-master (Task #137, 2026-05-28) — GHZ Act 1 parity audit

Authoritative GHZ Act 1 entity histogram, measured by parsing
`extracted/Data/Stages/GHZ/Scene1.bin` (85191 bytes, consumed 85191/85191,
75 object-class slots) with the RSDK object-table reader in
`tools/parse_title_entities.py` (class names resolved by MD5 against the
28 StageConfig.bin classes + GameConfig.bin globals). Instance counts are
ground truth for what GHZ Act 1 spawns.

| Class | Instances | Saturn status | Notes |
|---|---:|---|---|
| Ring | 446 | PORTED | atlas + behavior (Entities.c) |
| PlaneSwitch | 106 | PORTED | Phase 2.4g.3 — THIRD (largest) GHZ entity on the RSDK entity engine; writes player->collisionPlane (A/B) as player crosses marker X inside its size window; two-plane bridge (Player.h sms_world_t raw_alt+active_path) makes the surface probe select the path keyed on collisionPlane |
| SpikeLog | 61 | PORTED | Phase 2.4c.2 |
| Platform | 59 | PORTED | Phase 2.4c.2 |
| Spikes | 41 | PORTED | Phase 2.4c static subset |
| ItemBox | 38 | PORTED | |
| Spring | 35 | PORTED | |
| BreakableWall | 23 | PORTED | Phase 2.4-PLAT — RSDK entity engine; State_Wall/Floor/Ceiling break-on-spin via Player_CheckCollisionBox (C_TOP/BOTTOM/non-C_NONE). Invisible (FG tilemap surface). Saturn-fit break = destroyEntity + 100 score (tile-shatter not ported, static FG.TMP) |
| BoundsMarker | 22 | PORTED | Phase 2.4g.2 — SECOND GHZ entity on the RSDK entity engine; writes Zone camera/player/death bounds (g_zone_*) as player crosses marker X; GHZ camera clamp (scene_ghz.c ghz_set_camera) reads them |
| Decoration | 21 | **NOT_STARTED** | visual |
| Newtron | 21 | PORTED | Phase 2.4c.2 |
| InvisibleBlock | 18 | PORTED | Phase 2.4g.1 — FIRST GHZ entity on the RSDK entity engine (per memory/ghz-pivot-to-rsdk-engine.md); solid collision via Player_CheckCollisionBox -> player_t.collisionFlagV/H |
| BuzzBomber | 18 | PORTED | |
| CollapsingPlatform | 15 | PORTED | Phase 2.4-PLAT — RSDK entity engine; stood-trigger via Player_CheckCollisionBox + collapseDelay countdown -> destroy (or respawn-reset). Invisible (FG tilemap surface). Tile-debris + storedTiles[256] not ported (static FG.TMP, 256B slot stride) |
| ForceSpin | 13 | PORTED | Phase 2.4-PLAT — RSDK entity engine; invisible trigger; Zone_RotateOnPivot bbox-test in rotated frame -> Saturn-fit force-roll (min gsp in booster dir + clear jumping). Tube-roll state machine has no minimal-player analogue |
| Bridge | 13 | PORTED | Phase 2.4-PLAT — RSDK entity engine; the SOLE visible class. Draws GHZ/Bridge.bin planks + sine depression under player weight via Bridge_draw_only. Atlas cd/BRIDGE.SP2 reproducible from extracted/Data/Sprites/GHZ/Bridge.bin |
| Chopper | 13 | PORTED | Phase 2.4h — RSDK entity engine (Jump/Swim states; charge/Water branch inert per GHZ Act 1 scene). Collision via Player_CheckCollisionBox (stomp/hurt). Draw via g_chopper_atlas (24 frames) Chopper_draw_only |
| BGSwitch | 12 | **NOT_STARTED** | parallax-layer switch |
| Crabmeat | 11 | PORTED | Phase 2.4h — RSDK entity engine (Init/Moving/Shoot states; no-floor-ahead turn via Player_SurfaceY; projectile spawn Saturn-fit no-op FIXME 2.5). Collision via Player_CheckCollisionBox. Draw via g_crabmeat_atlas (22 frames) |
| Motobug | 9 | PORTED | |
| (unresolved global) | 7 | n/a | hash 875e224b… not in StageConfig/GameConfig strings |
| Batbrain | 7 | PORTED | Phase 2.4h — RSDK entity engine (CheckPlayerInRange/DropToPlayer/Fly/FlyToCeiling; RSDK.Rand(0,8) -> deterministic LCG; FlyToCeiling stops at startPos.y). Collision via Player_CheckCollisionBox. Draw via g_batbrain_atlas (11 frames) |
| Water | 6 | **NOT_STARTED** | |
| StarPost | 4 | **NOT_STARTED** | checkpoint — gameplay-critical |
| SpinBooster | 4 | **NOT_STARTED** | |
| SpecialRing | 3 | **NOT_STARTED** | bonus-stage entry |
| TimeAttackGate | 3 | **NOT_STARTED** | TA mode only |
| Player | 2 | FR-1 (anim system) | **Animation layer faithfully re-ported (FR-1, 2026-06-01).** Supersedes the 2.5.1-2.5.4 increments, which ported the STATE machines but never the ANIMATION system -- the old draw selected idle-vs-walk only by onGround && \|gsp\| and ignored animationID, and only Idle+Walk of Sonic's 53 anims shipped to disc, so jump-ball/roll/crouch/spindash/lookup/dropdash all rendered as idle or walk (user: "crouch doesn't work, spindash doesn't work, he doesn't turn into a ball when jumping"). FR-1 ports Player_HandleIdleAnimation (Sonic branch, Player.c:2820-2854), Player_HandleGroundAnimation (2917-3080: skid/walk/jog/run/dash by minJog/minRun/minDashVelocity), the air-state ANI_AIR_WALK/ANI_JUMP switch (3900-3929), and sets animationID at each ported state's SetSpriteAnimation site (Jump/Roll/Spindash/Crouch/LookUp/DropDash). Residency = MRU pool + per-anim CD slices (player_atlas.{c,h}); Game.c driver maps animationID -> kept-anim index and draws the current frame w/ pivot+facing flip. cd/SONIC.SP2 (149 frames) + cd/SONIC.MET (15 kept anims) reproducible from extracted Sonic.bin; frame/speed/loop/duration tables match decomp. **DROP list (cited, surfaced at checkpoint -- not silent):** Bored 2 (rare 2nd idle, 720-tick hold), Balance 1/2 (needs 6-sensor flailing detection, no Saturn analogue yet), all spring-twirl/pole/zip/CPZ-tube/water/super/cutscene anims (no GHZ-Act-1 trigger ported), Hurt/Die/Drown (land with FR-2 Hurt/Death/Drown states). **Gate:** tools/qa_fr1_parity_gate.py STATIC S1-S4 wired into verify_done Gate V-FR1 (GREEN); RUNTIME R1 verified GREEN -- 5 scripted-input savestates captured in GHZ gameplay (ts=6) render 5 DISTINCT anims + 5 distinct sprite-ids: idle(ANI_IDLE=0,spr=186), walk(ANI_WALK=5,spr=197), crouch(ANI_CROUCH=4,spr=190), jump(ANI_JUMP=10,spr=192), spindash(ANI_SPINDASH=16,spr=188). R1 is a manual post-build check (diag symbol addrs shift per build -> re-capture tools/_fr1_states/*.mcs first). Prior moveset state machines unchanged (Action_Jump/Spindash/crouch/lookup/dropdash per Player.c:3325/3341/3849/3857/4026/4082/4131/4455-4543/6114-6216). super/water/shields/hurt/death state ports still deferred (FR-2+) |
| SignPost | 2 | PORTED | |
| CorkscrewPath | 2 | **NOT_STARTED** | loop/corkscrew path router |
| ForceUnstick | 2 | **NOT_STARTED** | |
| HUD | 1 | PORTED (partial) | |
| TitleCard | 1 | PORTED | Phase 2.4j.1 — act-intro card on the RSDK engine via the Bridge-model (registered class + single module-static EntityTitleCard driven by titlecard_tick/_draw_only from Game.c; g_titlecard_active freezes Player/jump/HUD during the intro). All 6 states + 3 draw states + text trio (SetSpriteString/GetStringWidth/DrawString) ported. Atlas cd/TITLECARD.SP2/.MET (36 frames) reproducible from extracted/Data/Sprites/Global/TitleCard.bin. FX_SCALE act-num grow-in omitted (no jo per-sprite scale draw); HandleCamera no-op. Gate V-2.4j1 GREEN |
| Music | 1 | PORTED | |
| DDWrecker | 1 | **NOT_STARTED** | GHZ Act 1 boss (Death Egg Wrecker) |

GHZSetup (StageConfig global, 0 placed instances) drives 5 simultaneous
palette/tile animations per decomp GHZSetup.c:17-43 — currently STUBBED in
`src/mania/Objects/GHZ/GHZSetup.c` (Phase 2.1 stub: 2 water-line palette
rotations, 2 waterfall palette rotations, sun-flower tile 427,
extending-flower tile 431 all unimplemented).

PORTED so far (8 placed classes + Player + Ring/ItemBox/Spring/Spikes/SignPost
+ Motobug/BuzzBomber/Newtron/SpikeLog/Platform): cover ~83% of placed
instances by count (mostly Rings). The 20 NOT_STARTED classes are the
gameplay-correctness gap.

Prioritized RED-gate port sequence (gameplay-criticality x instance count):
1. **Phase 2.4g — collision/bounds layer**: PlaneSwitch (106) + BoundsMarker
   (22) + InvisibleBlock (18). Without these the ported player physics has
   no plane routing, no camera bounds, no invisible solids — the foundation
   every other entity sits on. RED gate: savestate-assert plane-id toggles
   at PlaneSwitch X + camera clamps at BoundsMarker X.
   - **2.4g.1 DONE (Task #153)**: InvisibleBlock (18) ported as the FIRST GHZ
     entity on the RSDK entity engine — the runtime pivot off the bespoke
     per-class `*_tick_and_draw` modules (per
     memory/ghz-pivot-to-rsdk-engine.md). Proves the full engine path
     end-to-end: GHZSCN1.BIN object-table spawn -> RSDK slot ->
     rsdk_object_tick (with rsdk_object_set_camera sync so ACTIVE_BOUNDS
     passes) -> InvisibleBlock_Update -> Player_CheckCollisionBox -> C_TOP ->
     player_t.collisionFlagV. Gate `tools/qa_phase2_4g1_gate.py` (Gate
     V-2.4g1 in verify_done.ps1): static P1-P3 GREEN on the release build;
     runtime P4 RED->GREEN demonstrated against a QA-probe build
     (spawned=36, collisionFlagV=1) in samples/qa_phase2_4g1_probe.mcs.
   - **2.4g.2 DONE (Task #153)**: Zone object (camera-bounds + death-boundary
     SUBSET, NOT the full Zone state machine) + BoundsMarker (22) ported as
     the SECOND GHZ entity on the RSDK entity engine. New
     src/mania/Objects/Global/Zone.{c,h} export volatile g_zone_cameraBounds*
     / g_zone_playerBounds* / g_zone_deathBoundary[PLAYER_COUNT] +
     zone_init_default_bounds (decomp Zone.c:221-235) seeded at GHZ load.
     BoundsMarker_ApplyBounds (decomp BoundsMarker.c:51-98) overrides
     cameraBoundsB/T (+paired playerBounds + deathBoundary) as the player
     crosses each marker's X. scene_ghz.c ghz_set_camera rewired to clamp
     from g_zone_cameraBounds* when g_zone_bounds_valid, world-size fallback
     otherwise. RSDK_TEMPENTITY_COUNT 0x20->0x30 so IB18+BM22=40 overflow
     entities all spawn (heap +4 KB, BSS +16 B; _end 0x060a5690, ~107 KB
     under the 0x060C0000 SGL floor). Gate `tools/qa_phase2_4g2_gate.py`
     (Gate V-2.4g2 in verify_done.ps1): static P1-P3 GREEN; runtime P4
     (g_zone_cameraBoundsB[0] nonzero after GHZ load) is the
     --with-savestate variant.
   - **2.4g.3 DONE (Task #153)**: PlaneSwitch (106) ported as the THIRD
     (largest) GHZ entity on the RSDK entity engine + the two-plane
     surface-probe bridge. src/mania/Objects/Global/PlaneSwitch.{c,h}
     mirror decomp PlaneSwitch.c: _Update gates the single bespoke player
     on mania_is_ghz_active() and calls PlaneSwitch_CheckCollisions
     (decomp PlaneSwitch.c:81-113), which rotates the player pos/vel into
     the marker's un-angled frame via negAngle (Zone_RotateOnPivot),
     AABB-tests the TO_FIXED(24)-wide / (size<<19)-tall window, and writes
     player->collisionPlane from (flags>>3)&1 or (flags>>1)&1 by approach
     side. Two-plane bridge: Player.h sms_world_t gains raw_alt + active_path;
     Player_SurfaceY/Angle/Flag select the table via player_surface_table();
     Player_Tick copies collisionPlane&1 -> active_path each frame. GHZ1SURF
     has one real plane so raw_alt == raw (degenerate B path) — the
     collisionPlane WRITE still toggles probe-path SELECTION (gate tests the
     toggle, not divergent geometry; a real B asset is a later phase).
     scene.c fill_planeswitch_attributes fills size/angle/onPath from the
     Scene1.bin attribute payload. RSDK_TEMPENTITY_COUNT 0x30->0xA0 (160) so
     IB18+BM22+PS106=146 overflow entities all spawn (_end 0x060a5cd0, ~106
     KB under the 0x060C0000 SGL floor). Gate `tools/qa_phase2_4g3_gate.py`
     (Gate V-2.4g3 in verify_done.ps1): static P1-P3 GREEN on the release
     build; runtime P4 (collisionPlane toggles) is the --with-savestate
     variant. Also fixed a latent build.bat bug: `--skip-qa` was forwarded
     to GNU make via %* (cmd `shift` doesn't update %*), so verify_done's
     internal build never recompiled — now parses args into EXTRA_ARGS.
     **Phase 2.4g GHZ collision/bounds layer COMPLETE.**
2. **Phase 2.4h DONE (Task #154)**: badnik trio Chopper (13) + Crabmeat (11)
   + Batbrain (7) ported onto the RSDK entity engine (Scene1.bin object-table
   spawn -> rsdk_object_tick -> per-class _Update state machine + collision via
   Player_CheckCollisionBox). RSDK_TEMPENTITY_COUNT bumped 0xA0->0xC0 (192) to
   budget the combined overflow (IB18+BM22+PS100+Chopper13+Crabmeat11+Batbrain7
   = 171, 21 spare); _end = 0x060A9640 (~27 KB under the 0x060C0000 SGL floor).
   Per-frame variable-size atlases via entity_atlas (CHOPPER.SP2 45284 B/24
   frames, CRABMEAT.SP2 45640 B/22 frames, BATBRAIN.SP2 18954 B/11 frames),
   all reproducible from extracted/Data/Sprites/GHZ/<Name>.bin. Bespoke
   <class>_draw_only walkers (rsdk_object_draw_all suppressed in GHZ path).
   Gate `tools/qa_phase2_4h_gate.py` (Gate V-2.4h in verify_done.ps1): P1-P3 +
   P5 GREEN on the release build; P4 (savestate spawn-count peek) SKIPs without
   --with-savestate (same as the 2.4g gates). **Phase 2.4h badnik trio COMPLETE.**
3. **Phase 2.4i DONE (Task #154)**: synthesized/fabricated assets PURGED and
   replaced by authentic ones from extracted/Data, verified by CONTENT
   PROVENANCE (byte-identity), not filename. Per
   memory/decomp-assets-only-no-synthesis.md. Deleted 3 synthesis scripts
   (tools/make_audio.py, make_digit_font.py, make_object_sprites.py) and 5
   fabricated outputs (cd/DIGITS.SPR hand-drawn glyph font 1542 B,
   cd/STAGEBGM.PCM dead synth BGM, cd/{SPRING,MONITOR,SIGNPOST}.SPR dead fake
   object sprites — live code uses .SP2). HUD: cd/DIGITS.SPR replaced by
   cd/HUD.SP2 (30 frames, 22270 B) + cd/HUD.MET (3 anims) built from
   extracted/Data/Sprites/Global/HUD.bin via build_entity_atlas (anim 0 HUD
   Elements 17 frames, anim 1 Numbers 10 = digits 0-9, anim 2 Life Icons 3);
   7 unused anims dropped + Numbers capped to 10 to fit ENTITY_ATLAS_MAX_FRAMES
   = 34. Entities.c HUD section rewritten onto the entity_atlas SPR2+MET loader
   (g_hud_atlas; hud_load/hud_blit/hud_number_right/hud_draw). SFX: all 7
   cd/*SFX.PCM are ffmpeg re-encodes (convert_audio.py @ 22050 Hz s8 mono) of
   extracted/Data/SoundFX/Global/*.wav (RING<-Ring, JUMP<-Jump, BREAK/STOMP
   <-Destroy, BOUNCE<-Spring, HURT<-Hurt, LOSE<-LoseRings), each with a decomp
   PlaySfx/GetSfx citation in build.bat + Entities.c. build.bat regenerates
   HUD.SP2/MET + the 7 PCMs every build so a fabricated asset cannot drift
   back in. Release build: 0 warnings / 0 errors, _end = 0x060AA250 (~87 KB
   under the 0x060C0000 SGL work-area floor). Gate
   `tools/qa_phase2_4i_asset_authenticity_gate.py` (Gate V-2.4i in
   verify_done.ps1): P1 (no synth scripts) + P2 (no fabricated outputs) + P3
   (HUD.SP2/MET byte-identical to fresh HUD.bin build) + P4 (5 live SFX
   byte-identical to decomp-WAV re-encodes) GREEN; fired RED on the pre-fix
   build (3 scripts present, 5 fakes present, STOMPSFX 14702 B synth mismatch).
   **Phase 2.4i asset purge COMPLETE.**
4. Phase 2.4j — platforming mechanics: CollapsingPlatform (15) + Bridge (13)
   + ForceSpin (13) + BreakableWall (23) + SpinBooster (4).
5. Phase 2.4k — checkpoint/visual/setup: StarPost (4) + GHZSetup 5 palette/
   tile animations + Decoration (21) + Water (6) + BGSwitch (12) + TitleCard.
6. Phase 2.4l — boss/special: DDWrecker (1) + SpecialRing (3) + CorkscrewPath.

This sequence is the GHZ Act 1 branch of BIBLE.md Phase 2 / COMPREHENSIVE_PLAN
§2. One port -> one RED gate -> one user checkpoint per CLAUDE.md §7.

## Phase 2.4e v1 (Task #142, 2026-05-28) — entity animation completeness

File-level entity SPR atlas completeness audit + RED-firing gate
landed. Every in-scope GHZ Act 1 entity now has a `.SP2` (per-frame
variable-size) + `.MET` (per-frame duration + pivot) pair under `cd/`
extracted from the decomp `.bin` via `tools/build_entity_atlas.py`.

Coverage gap (RED before fix → GREEN after):

| Entity | RED before (Saturn / decomp) | GREEN after | Kept anims | Dropped anims (cited) |
|---|---|---|---|---|
| Ring        | 16 / 81 (19.8 %) | 65 / 65 (100 %)  | Normal + Sparkle 1/2/3 | Hyper Ring (Plus mode only) |
| ItemBox     | 1 / 69 (1.4 %)   | 45 / 45 (100 %)  | Normal + Broken + Powerups + Scanlines + ItemDisappear + Debris | Snow (IIZ Ice overlay) |
| Spring      | 9 / 54 (16.7 %)  | 54 / 54 (100 %)  | Yellow/Red × V/H/D (all 6) | — |
| SignPost    | 8 / 34 (23.5 %)  | 12 / 12 (100 %)  | Sonic + Eggman + Post Bits | Tails, Knuckles (Sonic-only scope per decomp L506-511) |
| Spikes      | 4 frames oversized canvas | 2 / 2 (100 %, tight 32x32) | Spikes V + Spikes H | — |
| Motobug     | 12 / 29 (41.4 %) | 29 / 29 (100 %)  | Move + Idle + Turn + Puff | — |
| BuzzBomber  | 5 / 32 (15.6 %)  | 32 / 32 (100 %)  | Fly + Shoot + Wings + Thrust + Projectile | — |

Gate `tools/qa_phase2_4e_anim_completeness_gate.py` ran RED on the
pre-Phase-2.4e build (all 7 entities failed P1+P2+P3) and ran GREEN
after running `tools/build_entity_atlas.py --all`. The gate is wired
into `verify_done.ps1` as Gate V-2.4e.

The Saturn-side consumer migration (`src/rsdk/entity_atlas.{c,h}` +
the 7 per-entity loaders + the runtime savestate P4 check) is the
v2 follow-up. Until that lands the legacy `cd/*.SPR` (SPR1) files
remain unchanged so the current Saturn runtime keeps booting; the
`.SP2` + `.MET` files ship side-by-side as the v2 input.

## Phase 0.5 / 1.1 state (this row)

| Decomp file | Status | Saturn target | Phase / Notes |
|---|---|---|---|
| `SonicMania/Game.c` | ENGINE_VERBATIM role (P6 pack TU) | `tools/_portspike/_p6/p6_wave1_reg.c` | 2026-07-01 audit: the engine track mirrors Game.c InitGameLogic (p6_wave1_reg.c:111-189, per-class Game.c line citations inline; RegisterGlobalVariables :144-147). Supersedes the src/mania/Game.c Phase 1.1 hand-port row. |
| `RSDKv5/RSDK/Graphics/Animation.cpp` | (engine) | `src/rsdk/animation.c` | BUILDS (Phase A3); gaps in docs/rsdk_compat_audit.md §3 |
| `RSDKv5/RSDK/Graphics/Drawing.cpp` | (engine) | `src/rsdk/drawing.c` | BUILDS (Phase A4); gaps in docs/rsdk_compat_audit.md §4 |
| `RSDKv5/RSDK/Scene/Object.cpp` | PORTED (Phase 1.1) | `src/rsdk/object.c` | static_vars allocator + drawGroup priority queues + static/late_update wired + RSDK_THIS macro |
| `RSDKv5/RSDK/Scene/Scene.cpp` | PORTED (Phase 1.1) | `src/rsdk/scene.c` | Full LoadScene pipeline: path-build -> rsdk_scene_load -> per-entity rsdk_create_entity -> per-class stage_load callback chain |
| `RSDKv5/RSDK/Core/Math.cpp` | PORTED (Phase 1.1) | `src/rsdk/math.c` | Sin/Cos/Tan 1024/512/256 LUTs + ArcTan + LCG Rand. Uses local sinf polynomial (no libm on SH-2 build) |
| `RSDKv5/RSDK/Graphics/Palette.cpp` | PORTED (Phase 1.1) | `src/rsdk/palette.c` | 8 banks x 256 entries RGB565 mirror + RotatePalette + SetLimitedFade + dirty-mask for CRAM DMA |
| `RSDKv5/RSDK/Storage/Text.cpp` | PORTED (Phase 1.1) | `src/rsdk/string.c` | InitString/SetString/AppendText/AppendString/CopyString/GetCString/CompareStrings + DATASET_STR drain |
| `RSDKv5/RSDK/Scene/TileLayer.cpp` (layer subset) | PORTED (Phase 1.1) | `src/rsdk/tilelayer.c` | GetTileLayer/ID + GetTile/SetTile + scanline-callback registration storage |
| (link-time stub layer) | PORTED (Phase 1.1) | `src/rsdk/api.c` | API_SetRichPresence/SetNoSave/ClearPrerollErrors/CheckDLC/UnlockAchievement/ClearSaveStatus/ClearUserDB no-ops + Localization_GetString stub |
| `SonicMania/Objects/Title/TitleSetup.c` | PORTED (Phase 1.2) | `src/mania/Objects/Title/TitleSetup.c` | Full state machine ported (8 states + 3 stateDraws). Lines cited inline. Plus-only paths stripped. |
| `SonicMania/Objects/Title/TitleLogo.c` | PORTED (Phase 1.2) | `src/mania/Objects/Title/TitleLogo.c` | Per-type Update + Draw + Create ported (5 types: EMBLEM, RIBBON, GAMETITLE, COPYRIGHT, RINGBOTTOM, PRESSSTART). PressStart blink, ribbon-center toggle, FLIP_X mirroring all wired. |
| `SonicMania/Objects/Title/TitleSonic.c` | PORTED (Phase 1.2) | `src/mania/Objects/Title/TitleSonic.c` | Update advances animatorSonic + animatorFinger (only when body on last frame). Draw clips Y<160 for body, full clip for finger. |
| `SonicMania/Objects/Title/TitleBG.c` | PORTED (Phase 1.2) | `src/mania/Objects/Title/TitleBG.c` | 5-subtype Create with INK_BLEND (MOUNTAIN2) / INK_ADD (REFLECTION+WATERSPARKLE) / INK_MASKED (WINGSHINE). StaticUpdate rotates palette 140-143 every 6 ticks. SetupFX masks pal[55]. Scanline FX (Clouds/Island) stored but not invoked (Phase 2 — VDP2 H-IRQ raster). |
| `SonicMania/Objects/Title/Title3DSprite.c` | DEFERRED-PHASE-Z | `src/mania/Objects/Title/Title3DSprite.c` | Create-only stub; entity is ACTIVE_NEVER + visible=false so it doesn't participate in draw. Update/Draw deferred to Phase Z (requires Sin1024-modulated perspective projection + VDP1 distorted-sprite scaling). |
| `SonicMania/Objects/Global/Spikes.c` | PORTED (Phase 2.4c) | `src/mania/Objects/Global/Spikes.c` | Static-only subset of Spikes_Update (decomp L12-308 non-Plus/non-Ice branch). 4 spike types (UP/DOWN/LEFT/RIGHT). Decomp-exact hitbox per Create L355-358 + L373-376. Moving-spike state machine (HIDDEN/APPEAR/SHOWN/DISAPPEAR) deferred — no GHZ Act 1 instance uses it. Asset: cd/SPIKES.SPR + cd/GHZ1SPIKE.BIN (41 instances per Scene1.bin). |
| `SonicMania/Objects/GHZ/BuzzBomber.c` | PORTED (Phase 2.4c) | `src/mania/Objects/GHZ/BuzzBomber.c` | 3-state subset: Flying/Idle/DetectedPlayer (decomp State_Flying L169-190 + State_Idle L192-207 + State_DetectedPlayer L209-247). Decomp-exact hitboxBadnik {-24,-12,24,12} per StageLoad L91-94. velocity.x = +/-0x40000 step approximated as 1 px every 4 ticks. Projectile sub-entity spawn at timer == 45 (decomp L222-240) deferred to Phase 2.4d. Asset: cd/BUZZ.SPR + cd/GHZ1BUZZ.BIN (18 instances per Scene1.bin). |
| `SonicMania/Objects/GHZ/Motobug.c` | UPGRADED (Phase 2.4c) | `src/mania/Objects/Common/Entities.c::motobug_*` | Phase 2.3 patrol-only port upgraded with decomp-exact hitboxBadnik per Motobug.c:59-62. MOTOBUG_HITBOX_LEFT/TOP/RIGHT/BOTTOM = +/-14. Smoke trail (State_Smoke L199-207) + 30-tick turn anim (State_Idle/Turn) deferred to Phase 2.4d. |

## SonicMania/Objects/*.c — 610 files

| Decomp file | Status | Saturn target | Phase / Notes |
|---|---|---|---|
| SonicMania/Objects/AIZ/AIZEggRobo.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_AIZ_TEST-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:610) | 2026-07-01 audit: verbatim TU compiled in the AIZ flavor (build_p6scene_objs.sh:497-518), OVL_FE-linked (build_shipping.sh:252). Commit cc9f333 (R3.4). |
| SonicMania/Objects/AIZ/AIZEncoreTutorial.c | NOT_STARTED | src/mania/Objects/AIZ/AIZEncoreTutorial.c | Phase 7: Angel Island Zone |
| SonicMania/Objects/AIZ/AIZKingClaw.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_AIZ_TEST-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:606) | 2026-07-01 audit: verbatim TU compiled in the AIZ flavor (build_p6scene_objs.sh:497-518), OVL_FE-linked (build_shipping.sh:252). Commit cc9f333 (R3.4 anim via AIZOBJ.PAK). |
| SonicMania/Objects/AIZ/AIZRockPile.c | NOT_STARTED | src/mania/Objects/AIZ/AIZRockPile.c | Phase 7: Angel Island Zone |
| SonicMania/Objects/AIZ/AIZSetup.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_AIZ_TEST-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:584) | 2026-07-01 audit: verbatim TU compiled in the AIZ flavor (build_p6scene_objs.sh:497-518), OVL_FE-linked (build_shipping.sh:252). Commit cc9f333 — all 9 intro beats play. CAVEAT (gated logic workaround, root cause open): beat-3 Tails Static->P2Enter does not fire on Saturn; a one-shot census nudge applies the decomp-exact action (p6_ovl_ghz.c:1115-1131, cites AIZSetup.c:414-415). |
| SonicMania/Objects/AIZ/AIZTornado.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_AIZ_TEST-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:592) | 2026-07-01 audit: verbatim TU compiled in the AIZ flavor (build_p6scene_objs.sh:497-518), OVL_FE-linked (build_shipping.sh:252). Commit cc9f333 (R3 biplane; CRAM-bank collision fix R3.3). |
| SonicMania/Objects/AIZ/AIZTornadoPath.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_AIZ_TEST-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:596) | 2026-07-01 audit: verbatim TU compiled in the AIZ flavor (build_p6scene_objs.sh:497-518), OVL_FE-linked (build_shipping.sh:252). StarPost deref satisfied by a real zeroed instance in p6_closure_edge.c (build_p6scene_objs.sh:321-328). Commit cc9f333. |
| SonicMania/Objects/AIZ/Bloominator.c | NOT_STARTED | src/mania/Objects/AIZ/Bloominator.c | Phase 7: Angel Island Zone |
| SonicMania/Objects/AIZ/EncoreIntro.c | NOT_STARTED | src/mania/Objects/AIZ/EncoreIntro.c | Phase 7: Angel Island Zone |
| SonicMania/Objects/AIZ/FernParallax.c | NOT_STARTED | src/mania/Objects/AIZ/FernParallax.c | Phase 7: Angel Island Zone |
| SonicMania/Objects/AIZ/MonkeyDude.c | NOT_STARTED | src/mania/Objects/AIZ/MonkeyDude.c | Phase 7: Angel Island Zone |
| SonicMania/Objects/AIZ/Rhinobot.c | NOT_STARTED | src/mania/Objects/AIZ/Rhinobot.c | Phase 7: Angel Island Zone |
| SonicMania/Objects/AIZ/SchrodingersCapsule.c | NOT_STARTED | src/mania/Objects/AIZ/SchrodingersCapsule.c | Phase 7: Angel Island Zone |
| SonicMania/Objects/AIZ/Sweep.c | NOT_STARTED | src/mania/Objects/AIZ/Sweep.c | Phase 7: Angel Island Zone |
| SonicMania/Objects/All.c | NOT_STARTED | src/mania/Objects/All.c | Phase 1: All.c (registration header) |
| SonicMania/Objects/BSS/BSS_Collectable.c | NOT_STARTED | src/mania/Objects/BSS/BSS_Collectable.c | Phase 5: Blue Spheres special stage |
| SonicMania/Objects/BSS/BSS_Collected.c | NOT_STARTED | src/mania/Objects/BSS/BSS_Collected.c | Phase 5: Blue Spheres special stage |
| SonicMania/Objects/BSS/BSS_Horizon.c | NOT_STARTED | src/mania/Objects/BSS/BSS_Horizon.c | Phase 5: Blue Spheres special stage |
| SonicMania/Objects/BSS/BSS_HUD.c | NOT_STARTED | src/mania/Objects/BSS/BSS_HUD.c | Phase 5: Blue Spheres special stage |
| SonicMania/Objects/BSS/BSS_Message.c | NOT_STARTED | src/mania/Objects/BSS/BSS_Message.c | Phase 5: Blue Spheres special stage |
| SonicMania/Objects/BSS/BSS_Palette.c | NOT_STARTED | src/mania/Objects/BSS/BSS_Palette.c | Phase 5: Blue Spheres special stage |
| SonicMania/Objects/BSS/BSS_Player.c | NOT_STARTED | src/mania/Objects/BSS/BSS_Player.c | Phase 5: Blue Spheres special stage |
| SonicMania/Objects/BSS/BSS_Setup.c | NOT_STARTED | src/mania/Objects/BSS/BSS_Setup.c | Phase 5: Blue Spheres special stage |
| SonicMania/Objects/Common/BGSwitch.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:157) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w4 loop (:385-409), PACK-linked (:788); registered via RSDK_REGISTER_OBJECT (F.4 GHZ Act 1->2 BG switch). |
| SonicMania/Objects/Common/BreakableWall.c | PORTED | src/mania/Objects/Common/BreakableWall.c | Phase 2.4-PLAT: invisible breakable wall on the RSDK entity engine; State_Wall/Floor/Ceiling break-on-spin via Player_CheckCollisionBox; Saturn-fit break = destroyEntity + 100 score (tile-shatter not ported, static FG.TMP) |
| SonicMania/Objects/Common/Button.c | NOT_STARTED | src/mania/Objects/Common/Button.c | Phase 2+: shared gimmick base (used by many zones) |
| SonicMania/Objects/Common/CollapsingPlatform.c | PORTED | src/mania/Objects/Common/CollapsingPlatform.c | Phase 2.4-PLAT: invisible collapsing platform on the RSDK entity engine; stood-trigger via Player_CheckCollisionBox + collapseDelay -> destroy/respawn; tile-debris + storedTiles[256] not ported (static FG.TMP, 256B slot stride) |
| SonicMania/Objects/Common/Decoration.c | ENGINE_VERBATIM (P6 GHZ overlay TU) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:655) | 2026-07-01 audit: verbatim TU (build_p6scene_objs.sh w4 loop :385-409) linked into the cart overlay (build_shipping.sh:268), registered with full decomp callbacks (mass-port batch 1; also serves the AIZ folder). |
| SonicMania/Objects/Common/Eggman.c | NOT_STARTED | src/mania/Objects/Common/Eggman.c | Phase 2+: shared gimmick base (used by many zones) |
| SonicMania/Objects/Common/FlingRamp.c | NOT_STARTED | src/mania/Objects/Common/FlingRamp.c | Phase 2+: shared gimmick base (used by many zones) |
| SonicMania/Objects/Common/ForceSpin.c | ENGINE_VERBATIM (P6 GHZ overlay TU) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:659) | 2026-07-01 audit: verbatim TU (build_p6scene_objs.sh w4 loop :385-409) linked into the cart overlay (build_shipping.sh:268), registered with full decomp callbacks — the real tube-roll states now run against the verbatim Player. Supersedes the Phase 2.4-PLAT Saturn-fit hand-port. |
| SonicMania/Objects/Common/ForceUnstick.c | NOT_STARTED | src/mania/Objects/Common/ForceUnstick.c | Phase 2+: shared gimmick base (used by many zones) |
| SonicMania/Objects/Common/GenericTrigger.c | NOT_STARTED | src/mania/Objects/Common/GenericTrigger.c | Phase 2+: shared gimmick base (used by many zones) |
| SonicMania/Objects/Common/Palette.c | NOT_STARTED | src/mania/Objects/Common/Palette.c | Phase 2+: shared gimmick base (used by many zones) |
| SonicMania/Objects/Common/ParallaxSprite.c | NOT_STARTED | src/mania/Objects/Common/ParallaxSprite.c | Phase 2+: shared gimmick base (used by many zones) |
| SonicMania/Objects/Common/Platform.c | NOT_STARTED | src/mania/Objects/Common/Platform.c | Phase 2+: shared gimmick base (used by many zones) |
| SonicMania/Objects/Common/PlatformControl.c | NOT_STARTED | src/mania/Objects/Common/PlatformControl.c | Phase 2+: shared gimmick base (used by many zones) |
| SonicMania/Objects/Common/PlatformNode.c | NOT_STARTED | src/mania/Objects/Common/PlatformNode.c | Phase 2+: shared gimmick base (used by many zones) |
| SonicMania/Objects/Common/Projectile.c | NOT_STARTED | src/mania/Objects/Common/Projectile.c | Phase 2+: shared gimmick base (used by many zones) |
| SonicMania/Objects/Common/SpinBooster.c | ENGINE_VERBATIM (P6 GHZ overlay TU) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:663) | 2026-07-01 audit: verbatim TU (build_p6scene_objs.sh w4 loop :385-409) linked into the cart overlay (build_shipping.sh:268), registered with full decomp callbacks. Supersedes the Phase 2.4-PLAT Saturn-fit hand-port. |
| SonicMania/Objects/Common/TilePlatform.c | NOT_STARTED | src/mania/Objects/Common/TilePlatform.c | Phase 2+: shared gimmick base (used by many zones) |
| SonicMania/Objects/Common/Water.c | NOT_STARTED | src/mania/Objects/Common/Water.c | Phase 2+: shared gimmick base (used by many zones) |
| SonicMania/Objects/Continue/ContinuePlayer.c | NOT_STARTED | src/mania/Objects/Continue/ContinuePlayer.c | Phase 5: Continue screen |
| SonicMania/Objects/Continue/ContinueSetup.c | NOT_STARTED | src/mania/Objects/Continue/ContinueSetup.c | Phase 5: Continue screen |
| SonicMania/Objects/CPZ/AmoebaDroid.c | NOT_STARTED | src/mania/Objects/CPZ/AmoebaDroid.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/Ball.c | NOT_STARTED | src/mania/Objects/CPZ/Ball.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/Bubbler.c | NOT_STARTED | src/mania/Objects/CPZ/Bubbler.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/CaterkillerJr.c | NOT_STARTED | src/mania/Objects/CPZ/CaterkillerJr.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/ChemBubble.c | NOT_STARTED | src/mania/Objects/CPZ/ChemBubble.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/ChemicalBall.c | NOT_STARTED | src/mania/Objects/CPZ/ChemicalBall.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/ChemicalPool.c | NOT_STARTED | src/mania/Objects/CPZ/ChemicalPool.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/CPZ1Intro.c | NOT_STARTED | src/mania/Objects/CPZ/CPZ1Intro.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/CPZ2Outro.c | NOT_STARTED | src/mania/Objects/CPZ/CPZ2Outro.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/CPZBoss.c | NOT_STARTED | src/mania/Objects/CPZ/CPZBoss.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/CPZSetup.c | NOT_STARTED | src/mania/Objects/CPZ/CPZSetup.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/CPZShutter.c | NOT_STARTED | src/mania/Objects/CPZ/CPZShutter.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/DNARiser.c | NOT_STARTED | src/mania/Objects/CPZ/DNARiser.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/Grabber.c | NOT_STARTED | src/mania/Objects/CPZ/Grabber.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/OneWayDoor.c | NOT_STARTED | src/mania/Objects/CPZ/OneWayDoor.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/Reagent.c | NOT_STARTED | src/mania/Objects/CPZ/Reagent.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/RotatingStair.c | NOT_STARTED | src/mania/Objects/CPZ/RotatingStair.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/SpeedBooster.c | NOT_STARTED | src/mania/Objects/CPZ/SpeedBooster.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/Spiny.c | NOT_STARTED | src/mania/Objects/CPZ/Spiny.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/Springboard.c | NOT_STARTED | src/mania/Objects/CPZ/Springboard.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/Staircase.c | NOT_STARTED | src/mania/Objects/CPZ/Staircase.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/StickyPlatform.c | NOT_STARTED | src/mania/Objects/CPZ/StickyPlatform.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/Syringe.c | NOT_STARTED | src/mania/Objects/CPZ/Syringe.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/TippingPlatform.c | NOT_STARTED | src/mania/Objects/CPZ/TippingPlatform.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/TransportTube.c | NOT_STARTED | src/mania/Objects/CPZ/TransportTube.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/TubeSpring.c | NOT_STARTED | src/mania/Objects/CPZ/TubeSpring.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/CPZ/TwistedTubes.c | NOT_STARTED | src/mania/Objects/CPZ/TwistedTubes.c | Phase 7: Chemical Plant Zone |
| SonicMania/Objects/Credits/AnimalHBH.c | NOT_STARTED | src/mania/Objects/Credits/AnimalHBH.c | Phase Z: end credits |
| SonicMania/Objects/Credits/CreditsSetup.c | NOT_STARTED | src/mania/Objects/Credits/CreditsSetup.c | Phase Z: end credits |
| SonicMania/Objects/Credits/EncoreGoodEnd.c | NOT_STARTED | src/mania/Objects/Credits/EncoreGoodEnd.c | Phase Z: end credits |
| SonicMania/Objects/Credits/TAEmerald.c | NOT_STARTED | src/mania/Objects/Credits/TAEmerald.c | Phase Z: end credits |
| SonicMania/Objects/Credits/TryAgain.c | NOT_STARTED | src/mania/Objects/Credits/TryAgain.c | Phase Z: end credits |
| SonicMania/Objects/Credits/TryAgainE.c | NOT_STARTED | src/mania/Objects/Credits/TryAgainE.c | Phase Z: end credits |
| SonicMania/Objects/Cutscene/ChaosEmerald.c | NOT_STARTED | src/mania/Objects/Cutscene/ChaosEmerald.c | Phase 4+: shared cutscene helpers |
| SonicMania/Objects/Cutscene/CutsceneHBH.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_GHZCUT_BOOT-gated; gated palette shims) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:646) | 2026-07-01 audit: verbatim TU compiled in the GHZCUT flavor (build_p6scene_objs.sh:530-538), OVL_FE-linked (build_shipping.sh:264). Saturn-fit shims, all #if P6_GHZCUT_BOOT-gated with the PC path verbatim in #else: palette Setup/Store/Restore no-ops (CutsceneHBH.c:186-214; Heavies use resident CRAM blocks), colors[128]->[1] in the census header (_p67d_sizing/include/Cutscene/CutsceneHBH.h:50-62; RegisterObject 592 B pool cap), Draw registered via the p6_cuthbh_draw shim which CALLS the verbatim CutsceneHBH_Draw (p6_ovl_ghz.c:643-650). Commit cc9f333. |
| SonicMania/Objects/Cutscene/CutsceneRules.c | NOT_STARTED | src/mania/Objects/Cutscene/CutsceneRules.c | Phase 4+: shared cutscene helpers |
| SonicMania/Objects/Cutscene/CutsceneSeq.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_AIZ_TEST-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:588) | 2026-07-01 audit: verbatim TU compiled in the AIZ flavor (build_p6scene_objs.sh:497-518), OVL_FE-linked (build_shipping.sh:252). Commit cc9f333. |
| SonicMania/Objects/Cutscene/FXExpandRing.c | NOT_STARTED | src/mania/Objects/Cutscene/FXExpandRing.c | Phase 4+: shared cutscene helpers |
| SonicMania/Objects/Cutscene/FXFade.c | NOT_STARTED | src/mania/Objects/Cutscene/FXFade.c | Phase 4+: shared cutscene helpers |
| SonicMania/Objects/Cutscene/FXRuby.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_AIZ_TEST-gated; 1 REV02 sed) | tools/_decomp_raw via _FXRuby_rev02.c (overlay; p6_ovl_ghz.c:618) | 2026-07-01 audit: verbatim TU with ONE build-local sed (GetTintLookupTable neutralized — REV02 table absent from RSDKFunctionTable; build_p6scene_objs.sh:499-506, cache untouched), OVL_FE-linked (build_shipping.sh:252). Ruby-warp fade rendered via VDP2 color offset (Tier-B.1, ST-058-R2 Ch.13). Commit cc9f333. |
| SonicMania/Objects/Cutscene/FXSpinRay.c | NOT_STARTED | src/mania/Objects/Cutscene/FXSpinRay.c | Phase 4+: shared cutscene helpers |
| SonicMania/Objects/Cutscene/FXTrail.c | NOT_STARTED | src/mania/Objects/Cutscene/FXTrail.c | Phase 4+: shared cutscene helpers |
| SonicMania/Objects/Cutscene/FXWaveRing.c | NOT_STARTED | src/mania/Objects/Cutscene/FXWaveRing.c | Phase 4+: shared cutscene helpers |
| SonicMania/Objects/Cutscene/RubyPortal.c | NOT_STARTED | src/mania/Objects/Cutscene/RubyPortal.c | Phase 4+: shared cutscene helpers |
| SonicMania/Objects/ERZ/ERZGunner.c | NOT_STARTED | src/mania/Objects/ERZ/ERZGunner.c | Phase 8: Egg Reverie boss |
| SonicMania/Objects/ERZ/ERZKing.c | NOT_STARTED | src/mania/Objects/ERZ/ERZKing.c | Phase 8: Egg Reverie boss |
| SonicMania/Objects/ERZ/ERZMystic.c | NOT_STARTED | src/mania/Objects/ERZ/ERZMystic.c | Phase 8: Egg Reverie boss |
| SonicMania/Objects/ERZ/ERZOutro.c | NOT_STARTED | src/mania/Objects/ERZ/ERZOutro.c | Phase 8: Egg Reverie boss |
| SonicMania/Objects/ERZ/ERZRider.c | NOT_STARTED | src/mania/Objects/ERZ/ERZRider.c | Phase 8: Egg Reverie boss |
| SonicMania/Objects/ERZ/ERZSetup.c | NOT_STARTED | src/mania/Objects/ERZ/ERZSetup.c | Phase 8: Egg Reverie boss |
| SonicMania/Objects/ERZ/ERZShinobi.c | NOT_STARTED | src/mania/Objects/ERZ/ERZShinobi.c | Phase 8: Egg Reverie boss |
| SonicMania/Objects/ERZ/ERZStart.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:164) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack (inert Player-closure ref at GHZ scope). |
| SonicMania/Objects/ERZ/KleptoMobile.c | NOT_STARTED | src/mania/Objects/ERZ/KleptoMobile.c | Phase 8: Egg Reverie boss |
| SonicMania/Objects/ERZ/PhantomEgg.c | NOT_STARTED | src/mania/Objects/ERZ/PhantomEgg.c | Phase 8: Egg Reverie boss |
| SonicMania/Objects/ERZ/PhantomGunner.c | NOT_STARTED | src/mania/Objects/ERZ/PhantomGunner.c | Phase 8: Egg Reverie boss |
| SonicMania/Objects/ERZ/PhantomHand.c | NOT_STARTED | src/mania/Objects/ERZ/PhantomHand.c | Phase 8: Egg Reverie boss |
| SonicMania/Objects/ERZ/PhantomKing.c | NOT_STARTED | src/mania/Objects/ERZ/PhantomKing.c | Phase 8: Egg Reverie boss |
| SonicMania/Objects/ERZ/PhantomMissile.c | NOT_STARTED | src/mania/Objects/ERZ/PhantomMissile.c | Phase 8: Egg Reverie boss |
| SonicMania/Objects/ERZ/PhantomMystic.c | NOT_STARTED | src/mania/Objects/ERZ/PhantomMystic.c | Phase 8: Egg Reverie boss |
| SonicMania/Objects/ERZ/PhantomRider.c | NOT_STARTED | src/mania/Objects/ERZ/PhantomRider.c | Phase 8: Egg Reverie boss |
| SonicMania/Objects/ERZ/PhantomRuby.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_AIZ_TEST-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:614) | 2026-07-01 audit: verbatim TU compiled in the AIZ flavor (build_p6scene_objs.sh:497-518), OVL_FE-linked (build_shipping.sh:252). CAVEAT (gated logic workaround, root cause open): beat-7 ruby is ACTIVE_BOUNDS while off-screen so its flash timer never ticks on Saturn; forced ACTIVE_NORMAL at cutscene_state>=7 (p6_ovl_ghz.c:1136-1147, #309). Commit cc9f333. |
| SonicMania/Objects/ERZ/PhantomShield.c | NOT_STARTED | src/mania/Objects/ERZ/PhantomShield.c | Phase 8: Egg Reverie boss |
| SonicMania/Objects/ERZ/PhantomShinobi.c | NOT_STARTED | src/mania/Objects/ERZ/PhantomShinobi.c | Phase 8: Egg Reverie boss |
| SonicMania/Objects/ERZ/PKingAttack.c | NOT_STARTED | src/mania/Objects/ERZ/PKingAttack.c | Phase 8: Egg Reverie boss |
| SonicMania/Objects/ERZ/RingField.c | NOT_STARTED | src/mania/Objects/ERZ/RingField.c | Phase 8: Egg Reverie boss |
| SonicMania/Objects/FBZ/BigSqueeze.c | NOT_STARTED | src/mania/Objects/FBZ/BigSqueeze.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/Blaster.c | NOT_STARTED | src/mania/Objects/FBZ/Blaster.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/Clucker.c | NOT_STARTED | src/mania/Objects/FBZ/Clucker.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/Crane.c | NOT_STARTED | src/mania/Objects/FBZ/Crane.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/Cylinder.c | NOT_STARTED | src/mania/Objects/FBZ/Cylinder.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/ElectroMagnet.c | NOT_STARTED | src/mania/Objects/FBZ/ElectroMagnet.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/FBZ1Outro.c | NOT_STARTED | src/mania/Objects/FBZ/FBZ1Outro.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/FBZ2Outro.c | NOT_STARTED | src/mania/Objects/FBZ/FBZ2Outro.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/FBZFan.c | NOT_STARTED | src/mania/Objects/FBZ/FBZFan.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/FBZMissile.c | NOT_STARTED | src/mania/Objects/FBZ/FBZMissile.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/FBZSetup.c | NOT_STARTED | src/mania/Objects/FBZ/FBZSetup.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/FBZSinkTrash.c | NOT_STARTED | src/mania/Objects/FBZ/FBZSinkTrash.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/FBZStorm.c | NOT_STARTED | src/mania/Objects/FBZ/FBZStorm.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/FBZTrash.c | NOT_STARTED | src/mania/Objects/FBZ/FBZTrash.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/FlameSpring.c | NOT_STARTED | src/mania/Objects/FBZ/FlameSpring.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/FoldingPlatform.c | NOT_STARTED | src/mania/Objects/FBZ/FoldingPlatform.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/HangGlider.c | NOT_STARTED | src/mania/Objects/FBZ/HangGlider.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/HangPoint.c | NOT_STARTED | src/mania/Objects/FBZ/HangPoint.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/Launcher.c | NOT_STARTED | src/mania/Objects/FBZ/Launcher.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/LightBarrier.c | NOT_STARTED | src/mania/Objects/FBZ/LightBarrier.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/MagPlatform.c | NOT_STARTED | src/mania/Objects/FBZ/MagPlatform.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/MagSpikeBall.c | NOT_STARTED | src/mania/Objects/FBZ/MagSpikeBall.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/Mine.c | NOT_STARTED | src/mania/Objects/FBZ/Mine.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/Propeller.c | NOT_STARTED | src/mania/Objects/FBZ/Propeller.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/PropellerShaft.c | NOT_STARTED | src/mania/Objects/FBZ/PropellerShaft.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/SpiderMobile.c | NOT_STARTED | src/mania/Objects/FBZ/SpiderMobile.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/SpiralPlatform.c | NOT_STARTED | src/mania/Objects/FBZ/SpiralPlatform.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/SwitchDoor.c | NOT_STARTED | src/mania/Objects/FBZ/SwitchDoor.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/Technosqueek.c | NOT_STARTED | src/mania/Objects/FBZ/Technosqueek.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/TetherBall.c | NOT_STARTED | src/mania/Objects/FBZ/TetherBall.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/Tuesday.c | NOT_STARTED | src/mania/Objects/FBZ/Tuesday.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/TwistingDoor.c | NOT_STARTED | src/mania/Objects/FBZ/TwistingDoor.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/FBZ/WarpDoor.c | NOT_STARTED | src/mania/Objects/FBZ/WarpDoor.c | Phase 7: Flying Battery Zone |
| SonicMania/Objects/GHZ/Batbrain.c | ENGINE_VERBATIM (P6 GHZ overlay TU) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:712) | 2026-07-01 audit: verbatim TU (build_p6scene_objs.sh w4 loop :385-409) linked into the cart overlay (build_shipping.sh:270), registered with full decomp callbacks (engine RSDK.Rand, no LCG substitute). Supersedes the Phase 2.4h hand-port. |
| SonicMania/Objects/GHZ/Bridge.c | ENGINE_VERBATIM (P6 GHZ overlay TU) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:396) | 2026-07-01 audit: verbatim TU (build_p6scene_objs.sh w4 loop :385-409) linked into the cart overlay (build_shipping.sh:267), registered with full decomp callbacks (#181; BurningLog dep NULLed in p6_closure_edge.c). Supersedes the Phase 2.4-PLAT src/mania hand-port. |
| SonicMania/Objects/GHZ/BurningLog.c | NOT_STARTED | src/mania/Objects/GHZ/BurningLog.c | Phase 2d: GHZ burning log |
| SonicMania/Objects/GHZ/BuzzBomber.c | ENGINE_VERBATIM (P6 GHZ overlay TU) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:700) | 2026-07-01 audit: verbatim TU (build_p6scene_objs.sh w4 loop :385-409) linked into the cart overlay (build_shipping.sh:270), registered with full decomp callbacks (badnik batch 2). |
| SonicMania/Objects/GHZ/CheckerBall.c | NOT_STARTED | src/mania/Objects/GHZ/CheckerBall.c | Phase 2d: GHZ checker ball gimmick |
| SonicMania/Objects/GHZ/Chopper.c | ENGINE_VERBATIM (P6 GHZ overlay TU) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:704) | 2026-07-01 audit: verbatim TU (build_p6scene_objs.sh w4 loop :385-409) linked into the cart overlay (build_shipping.sh:270), registered with full decomp callbacks. Supersedes the Phase 2.4h hand-port. |
| SonicMania/Objects/GHZ/CorkscrewPath.c | NOT_STARTED | src/mania/Objects/GHZ/CorkscrewPath.c | Phase 2d: GHZ corkscrew path |
| SonicMania/Objects/GHZ/Crabmeat.c | ENGINE_VERBATIM (P6 GHZ overlay TU) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:696) | 2026-07-01 audit: verbatim TU (build_p6scene_objs.sh w4 loop :385-409) linked into the cart overlay (build_shipping.sh:270), registered with full decomp callbacks — real projectile spawn included (no Saturn-fit no-op). Supersedes the Phase 2.4h hand-port. |
| SonicMania/Objects/GHZ/DDWrecker.c | NOT_STARTED | src/mania/Objects/GHZ/DDWrecker.c | Phase 2f: GHZ Act 1 boss (Death Egg Wrecker) |
| SonicMania/Objects/GHZ/DERobot.c | NOT_STARTED | src/mania/Objects/GHZ/DERobot.c | Phase 2f: GHZ Act 2 boss |
| SonicMania/Objects/GHZ/Fireball.c | NOT_STARTED | src/mania/Objects/GHZ/Fireball.c | Phase 2d: GHZ fireball hazard |
| SonicMania/Objects/GHZ/GHZ2Outro.c | NOT_STARTED | src/mania/Objects/GHZ/GHZ2Outro.c | Phase 2: GHZ Act 2 outro cutscene |
| SonicMania/Objects/GHZ/GHZCutsceneK.c | NOT_STARTED | src/mania/Objects/GHZ/GHZCutsceneK.c | Phase 2: GHZ Knuckles intro cutscene |
| SonicMania/Objects/GHZ/GHZCutsceneST.c | PORTED (P6 engine track, front-end-gated, UNCOMMITTED on cc9f333) | tools/_portspike/_p6/ (NOT src/mania/) | Task #309: ported VERBATIM on the P6.8 RSDKv5 engine (not the src/mania hand-port). Drives the AIZ->GHZCutscene->playable-GHZ handoff; all 4 cutscene beats (FadeIn/FinishRubyWarp/ExitHBH/SetupGHZ1) run, handoff to playable Green Hill Zone GREEN. Fade (VDP2 color-offset), 5 Heavies (HBHOBJ.SHT/PAK), and the live menu->intro->cutscene->GHZ loop all DONE+VERIFIED. OPEN (task #309 #2b): the cutscene BLACK SKY — Sonic/Tails warp in against black because the GHZ BG Outside layer is un-ported; the FG sky cells are populated-transparent and NO flat-fill works (4 shortcuts RED, gate tools/qa_ghzcut_sky.py). NEEDS a real VDP2 NBG behind the FG (AIZ-BG style, task #253 class). See memory ghzcutscene-sky-needs-bg-outside-render + ghzcutscene-handoff-budget-feasible. |
| SonicMania/Objects/GHZ/GHZSetup.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:166) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w4 loop (:385-409), PACK-linked (:788); registered via RSDK_REGISTER_OBJECT (F.4 GHZ1->GHZ2 ATL trigger). Supersedes the Phase 2.1 src/mania subset. StaticUpdate palette rotation blocked engine-side by the DrawAniTile stub (object_census.json draw_stub_set). |
| SonicMania/Objects/GHZ/Motobug.c | ENGINE_VERBATIM (P6 GHZ overlay TU) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:708) | 2026-07-01 audit: verbatim TU (build_p6scene_objs.sh w4 loop :385-409) linked into the cart overlay (build_shipping.sh:270), registered with full decomp callbacks (badnik batch 2). Supersedes the UPGRADED (Phase 2.4c) Entities.c hand-port row above. |
| SonicMania/Objects/GHZ/Newtron.c | ENGINE_VERBATIM (P6 GHZ overlay TU) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:692) | 2026-07-01 audit: verbatim TU (build_p6scene_objs.sh w4 loop :385-409) linked into the cart overlay (build_shipping.sh:270), registered with full decomp callbacks (badnik batch 2). |
| SonicMania/Objects/GHZ/SpikeLog.c | ENGINE_VERBATIM (P6 GHZ overlay TU) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:407) | 2026-07-01 audit: verbatim TU (build_p6scene_objs.sh w4 loop :385-409) linked into the cart overlay (build_shipping.sh:267), registered with full decomp callbacks (O3 step 1). |
| SonicMania/Objects/GHZ/Splats.c | NOT_STARTED | src/mania/Objects/GHZ/Splats.c | Phase 2c: GHZ Splats badnik (cut-content) |
| SonicMania/Objects/GHZ/WaterfallSound.c | NOT_STARTED | src/mania/Objects/GHZ/WaterfallSound.c | Phase 2: GHZ waterfall ambient sound |
| SonicMania/Objects/GHZ/ZipLine.c | NOT_STARTED | src/mania/Objects/GHZ/ZipLine.c | Phase 2d: GHZ zip-line gimmick |
| SonicMania/Objects/Global/ActClear.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:156) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack; registered via RSDK_REGISTER_OBJECT (F.2 stageFinishCallback chain). |
| SonicMania/Objects/Global/Animals.c | ENGINE_VERBATIM (P6 GHZ overlay TU) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:688) | 2026-07-01 audit: verbatim TU (build_p6scene_objs.sh w4 loop :385-409) linked into the cart overlay (build_shipping.sh:269; overlay-resident because Bridge_HandleCollisions references it intra-overlay per build_shipping.sh:198-199). |
| SonicMania/Objects/Global/Announcer.c | NOT_STARTED | src/mania/Objects/Global/Announcer.c | Phase 4: Announcer voice |
| SonicMania/Objects/Global/APICallback.c | NOT_STARTED | src/mania/Objects/Global/APICallback.c | Phase 1: API callback bridge |
| SonicMania/Objects/Global/BoundsMarker.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:158) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack. Supersedes the Phase 2.4g.2 src/mania hand-port. |
| SonicMania/Objects/Global/Camera.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:160) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack (Player-wave closure, commit 1751544). |
| SonicMania/Objects/Global/Competition.c | NOT_STARTED | src/mania/Objects/Global/Competition.c | Phase 4: Competition mode |
| SonicMania/Objects/Global/COverlay.c | NOT_STARTED | src/mania/Objects/Global/COverlay.c | Phase 4: Competition overlay |
| SonicMania/Objects/Global/Debris.c | NOT_STARTED | src/mania/Objects/Global/Debris.c | Phase 2: Debris particles |
| SonicMania/Objects/Global/DebugMode.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:161) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack (referenced by Player + HUD). |
| SonicMania/Objects/Global/DialogRunner.c | NOT_STARTED | src/mania/Objects/Global/DialogRunner.c | Phase 4: Modal dialog |
| SonicMania/Objects/Global/Dust.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:163) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack. |
| SonicMania/Objects/Global/EggPrison.c | NOT_STARTED | src/mania/Objects/Global/EggPrison.c | Phase 2f: Egg prison capsule |
| SonicMania/Objects/Global/EncoreRoute.c | NOT_STARTED | src/mania/Objects/Global/EncoreRoute.c | Phase Z: Encore route (Plus DLC) |
| SonicMania/Objects/Global/Explosion.c | ENGINE_VERBATIM (P6 GHZ overlay TU) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:684) | 2026-07-01 audit: verbatim TU (build_p6scene_objs.sh w4 loop :385-409) linked into the cart overlay (build_shipping.sh:269), registered with full decomp callbacks (badnik-break chain, batch 2). |
| SonicMania/Objects/Global/GameOver.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:165) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack. |
| SonicMania/Objects/Global/HUD.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:167) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack. HUD text render blocked engine-side by the DrawString stub (object_census.json draw_stub_set; massport plan S6-adjacent). |
| SonicMania/Objects/Global/ImageTrail.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:169) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack. |
| SonicMania/Objects/Global/InvincibleStars.c | NOT_STARTED | src/mania/Objects/Global/InvincibleStars.c | Phase 2: Invincibility stars |
| SonicMania/Objects/Global/InvisibleBlock.c | PORTED | src/mania/Objects/Global/InvisibleBlock.c | Phase 2.4g.1: Invisible-block collision helper — first GHZ entity on the RSDK entity engine (Scene1.bin object-table spawn -> rsdk_object_tick -> Player_CheckCollisionBox) |
| SonicMania/Objects/Global/ItemBox.c | NOT_STARTED | src/mania/Objects/Global/ItemBox.c | Phase 3: Monitors / item boxes |
| SonicMania/Objects/Global/Localization.c | ENGINE_VERBATIM | tools/_decomp_raw (pack TU, P6.7 wave-1) | UNMODIFIED TU compiled into the engine pack; full StageLoad chain proven on hardware (qa_p6_globals G8) |
| SonicMania/Objects/Global/Music.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:173) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack; stage BGM via CD-DA (#182, commit c4b3059-era wiring). Whole-game 58-track CD-DA budget = massport plan §9.2 item 5 (Phase S-AUDIO, open). |
| SonicMania/Objects/Global/NoSwap.c | NOT_STARTED | src/mania/Objects/Global/NoSwap.c | Phase 4: NoSwap helper |
| SonicMania/Objects/Global/PauseMenu.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:175) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack (REV02 compat arm precedent cited at PauseMenu.c:214-224 per :499 comment). |
| SonicMania/Objects/Global/PlaneSwitch.c | ENGINE_VERBATIM (P6 GHZ overlay TU) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:400) | 2026-07-01 audit: verbatim TU (build_p6scene_objs.sh w4 loop :385-409) linked into the cart overlay (build_shipping.sh:267), registered with full decomp callbacks (#254 loop fix, 106 GHZ1 placements). Supersedes the Phase 2.4g.3 src/mania hand-port. |
| SonicMania/Objects/Global/Player.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:176) | 2026-07-01 audit: the FULL verbatim decomp Player TU is compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack (21-TU Player-wave closure, commit 1751544) — complete state machines + animation system, not a subset. Supersedes the PARTIAL_2.5.1/FR-1 src/mania hand-port rows entirely. Pack->overlay Ring_LoseRings forward via p6_closure_edge.c:344 (#258b). |
| SonicMania/Objects/Global/ReplayRecorder.c | NOT_STARTED | src/mania/Objects/Global/ReplayRecorder.c | Phase Z: Replay recorder |
| SonicMania/Objects/Global/Ring.c | ENGINE_VERBATIM (P6 GHZ overlay TU) | tools/_decomp_raw via p6_ring2.cpp (overlay; p6_ovl_ghz.c:384) | 2026-07-01 audit: verbatim decomp Ring compiled as the overlay member p6_ring2.o (build_p6scene_objs.sh:282-284), registered with full decomp callbacks. Pack-side Ring_LoseRings/LoseHyperRings forward pack->overlay at runtime (p6_closure_edge.c:335-352, #258b; edge-audited). |
| SonicMania/Objects/Global/SaveGame.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:177) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack. CAVEAT: SaveGame_LoadFile/ResetPlayerState/SaveLoadedCB are STUBBED overlay-side for the menu (p6_menu_closure.c:247-273) — menu start runs the No-Save path; backup-RAM persistence (massport plan S7) still open. |
| SonicMania/Objects/Global/ScoreBonus.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:178) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack. |
| SonicMania/Objects/Global/Shield.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:179) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack. |
| SonicMania/Objects/Global/SignPost.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:180) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack (F.3: spawns ActClear -> GHZ1->GHZ2 advance, #232/#236). Sparkle-ring Ring_State_Sparkle/Draw_Sparkle are pack-side no-op stubs (p6_closure_edge.c:387-388, cosmetic, edge-witnessed). |
| SonicMania/Objects/Global/Soundboard.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:182) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack. |
| SonicMania/Objects/Global/SpecialRing.c | NOT_STARTED | src/mania/Objects/Global/SpecialRing.c | Phase 5: Special stage ring |
| SonicMania/Objects/Global/SpeedGate.c | NOT_STARTED | src/mania/Objects/Global/SpeedGate.c | Phase 2: Speed gate |
| SonicMania/Objects/Global/Spikes.c | ENGINE_VERBATIM (P6 GHZ overlay TU) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:415) | 2026-07-01 audit: verbatim TU (build_p6scene_objs.sh w4 loop :385-409) linked into the cart overlay (build_shipping.sh:267), registered with full decomp callbacks (Press = lone GHZ1-dead NULL per p6_closure_edge.c). Supersedes the Phase 2.4c static-subset src/mania hand-port row above. |
| SonicMania/Objects/Global/Spring.c | ENGINE_VERBATIM (P6 GHZ overlay TU) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:392) | 2026-07-01 audit: verbatim TU (build_p6scene_objs.sh w4 loop :385-409) linked into the cart overlay (build_shipping.sh:267), registered with full decomp callbacks (O1 step 1, commit db9dfe4). |
| SonicMania/Objects/Global/StarPost.c | NOT_STARTED | src/mania/Objects/Global/StarPost.c | Phase 3: Star post (checkpoint) |
| SonicMania/Objects/Global/SuperSparkle.c | NOT_STARTED | src/mania/Objects/Global/SuperSparkle.c | Phase 5: Super-Sonic sparkle |
| SonicMania/Objects/Global/TimeAttackGate.c | NOT_STARTED | src/mania/Objects/Global/TimeAttackGate.c | Phase Z: Time-attack gate |
| SonicMania/Objects/Global/TitleCard.c | PORTED | src/mania/Objects/Global/TitleCard.c | Phase 2.4j.1: act-intro card on the RSDK engine (Bridge-model — registered class + module-static EntityTitleCard driven by titlecard_tick/_draw_only; g_titlecard_active freezes Player/jump/HUD). 6 states + 3 draw states + text trio ported; atlas TITLECARD.SP2/.MET reproducible from extracted Global/TitleCard.bin; Gate V-2.4j1 GREEN. Phase 2.4j.2: fixed user-reported act-intro defects (2026-05-29) — (1) oversized black slab + missing GREEN HILL/ZONE text: atlas failed to load because "TITLECARD.SP2" (13 chars) exceeded SGL GFS_FNAME_LEN=12 -> jo_fs_read_file NULL; renamed to TITLCARD (8-char base, 12-char file) + LWRAM-scratch loader (entity_atlas_load_ex bypasses jo_malloc OOM). (2) garbled/sheared ZONE letters: jo/SGL slDispSprite truncates VDP1 char-size width to `width & 0x1f8` (sprites.c:212) while DMA-copying data at the actual width (:220), so non-mult-8 widths >=8 (ZONE Z/O/N/E = 26/26/26/28) shear diagonally; build_entity_atlas.py now pads every SP2 frame width up to a multiple of 8 with transparent right-columns (pivots/origin unchanged, layout preserved) — applied to all 16 atlases for provenance consistency. Gate V-2.4j2 GREEN (P1 names<=12, P2 base<=8, P4 all widths mult-8); visually confirmed clean ZONE at 3fps |
| SonicMania/Objects/Global/Zone.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:189) | 2026-07-01 audit: the FULL verbatim Zone TU is compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack (incl. Zone_StoreEntities/ReloadStoredEntities for the F.4 act transition). Supersedes the Phase 2.4g.2 bounds-subset hand-port. |
| SonicMania/Objects/HCZ/Blastoid.c | NOT_STARTED | src/mania/Objects/HCZ/Blastoid.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/BreakBar.c | NOT_STARTED | src/mania/Objects/HCZ/BreakBar.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/Buggernaut.c | NOT_STARTED | src/mania/Objects/HCZ/Buggernaut.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/ButtonDoor.c | NOT_STARTED | src/mania/Objects/HCZ/ButtonDoor.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/Current.c | NOT_STARTED | src/mania/Objects/HCZ/Current.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/DCEvent.c | NOT_STARTED | src/mania/Objects/HCZ/DCEvent.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/DiveEggman.c | NOT_STARTED | src/mania/Objects/HCZ/DiveEggman.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/Fan.c | NOT_STARTED | src/mania/Objects/HCZ/Fan.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/Gondola.c | NOT_STARTED | src/mania/Objects/HCZ/Gondola.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/HandLauncher.c | NOT_STARTED | src/mania/Objects/HCZ/HandLauncher.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/HangConveyor.c | NOT_STARTED | src/mania/Objects/HCZ/HangConveyor.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/HCZ1Intro.c | NOT_STARTED | src/mania/Objects/HCZ/HCZ1Intro.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/HCZOneWayDoor.c | NOT_STARTED | src/mania/Objects/HCZ/HCZOneWayDoor.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/HCZSetup.c | NOT_STARTED | src/mania/Objects/HCZ/HCZSetup.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/HCZSpikeBall.c | NOT_STARTED | src/mania/Objects/HCZ/HCZSpikeBall.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/Jawz.c | NOT_STARTED | src/mania/Objects/HCZ/Jawz.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/Jellygnite.c | NOT_STARTED | src/mania/Objects/HCZ/Jellygnite.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/LaundroMobile.c | NOT_STARTED | src/mania/Objects/HCZ/LaundroMobile.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/MegaChopper.c | NOT_STARTED | src/mania/Objects/HCZ/MegaChopper.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/Pointdexter.c | NOT_STARTED | src/mania/Objects/HCZ/Pointdexter.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/PullChain.c | NOT_STARTED | src/mania/Objects/HCZ/PullChain.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/ScrewMobile.c | NOT_STARTED | src/mania/Objects/HCZ/ScrewMobile.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/Spear.c | NOT_STARTED | src/mania/Objects/HCZ/Spear.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/TurboSpiker.c | NOT_STARTED | src/mania/Objects/HCZ/TurboSpiker.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/TwistingSlide.c | NOT_STARTED | src/mania/Objects/HCZ/TwistingSlide.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/WaterGush.c | NOT_STARTED | src/mania/Objects/HCZ/WaterGush.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/HCZ/Whirlpool.c | NOT_STARTED | src/mania/Objects/HCZ/Whirlpool.c | Phase 7: Hydrocity Zone |
| SonicMania/Objects/Helpers/BadnikHelpers.c | ENGINE_VERBATIM (P6 GHZ overlay TU) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:680) | 2026-07-01 audit: verbatim TU (build_p6scene_objs.sh w4 loop :385-409) linked into the cart overlay (build_shipping.sh:269); pack-side BadnikBreak/BadnikBreakUnseeded forward pack->overlay at runtime (p6_closure_edge.c:362-381, badnik-break effect chain, batch 2). |
| SonicMania/Objects/Helpers/ColorHelpers.c | NOT_STARTED | src/mania/Objects/Helpers/ColorHelpers.c | Phase 1+: helper utilities (used by many objects) |
| SonicMania/Objects/Helpers/CompetitionSession.c | NOT_STARTED | src/mania/Objects/Helpers/CompetitionSession.c | Phase 1+: helper utilities (used by many objects) |
| SonicMania/Objects/Helpers/DrawHelpers.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:162) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack. |
| SonicMania/Objects/Helpers/GameProgress.c | NOT_STARTED | src/mania/Objects/Helpers/GameProgress.c | Phase 1+: helper utilities (used by many objects) |
| SonicMania/Objects/Helpers/LogHelpers.c | ENGINE_VERBATIM | tools/_decomp_raw (pack TU, P6.7 wave-1) | Verbatim + cited REV02 PrintText compat arm (PrintMessage is REV01-only) |
| SonicMania/Objects/Helpers/MathHelpers.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:172) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack; -u-rooted for menu UIControl PointInHitbox use (build_p6scene_objs.sh:737). |
| SonicMania/Objects/Helpers/Options.c | ENGINE_VERBATIM | tools/_decomp_raw (pack TU, P6.7 wave-1) | UNMODIFIED TU; console StageLoad branch proven (SKU compat arm in APICallback.h) |
| SonicMania/Objects/Helpers/ParticleHelpers.c | NOT_STARTED | src/mania/Objects/Helpers/ParticleHelpers.c | Phase 1+: helper utilities (used by many objects) |
| SonicMania/Objects/Helpers/PlayerHelpers.c | NOT_STARTED | src/mania/Objects/Helpers/PlayerHelpers.c | Phase 1+: helper utilities (used by many objects) |
| SonicMania/Objects/Helpers/PlayerProbe.c | NOT_STARTED | src/mania/Objects/Helpers/PlayerProbe.c | Phase 1+: helper utilities (used by many objects) |
| SonicMania/Objects/Helpers/ReplayDB.c | NOT_STARTED | src/mania/Objects/Helpers/ReplayDB.c | Phase 1+: helper utilities (used by many objects) |
| SonicMania/Objects/Helpers/TimeAttackData.c | NOT_STARTED | src/mania/Objects/Helpers/TimeAttackData.c | Phase 1+: helper utilities (used by many objects) |
| SonicMania/Objects/HPZ/Batbot.c | NOT_STARTED | src/mania/Objects/HPZ/Batbot.c | Phase Z: Hidden Palace Zone (cut) |
| SonicMania/Objects/HPZ/Redz.c | NOT_STARTED | src/mania/Objects/HPZ/Redz.c | Phase Z: Hidden Palace Zone (cut) |
| SonicMania/Objects/HPZ/Stegway.c | NOT_STARTED | src/mania/Objects/HPZ/Stegway.c | Phase Z: Hidden Palace Zone (cut) |
| SonicMania/Objects/LRZ/BuckwildBall.c | NOT_STARTED | src/mania/Objects/LRZ/BuckwildBall.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/DashLift.c | NOT_STARTED | src/mania/Objects/LRZ/DashLift.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/Drillerdroid.c | NOT_STARTED | src/mania/Objects/LRZ/Drillerdroid.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/DrillerdroidO.c | NOT_STARTED | src/mania/Objects/LRZ/DrillerdroidO.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/Fireworm.c | NOT_STARTED | src/mania/Objects/LRZ/Fireworm.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/Flamethrower.c | NOT_STARTED | src/mania/Objects/LRZ/Flamethrower.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/HeavyKing.c | NOT_STARTED | src/mania/Objects/LRZ/HeavyKing.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/HeavyRider.c | NOT_STARTED | src/mania/Objects/LRZ/HeavyRider.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/HPZEmerald.c | NOT_STARTED | src/mania/Objects/LRZ/HPZEmerald.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/Iwamodoki.c | NOT_STARTED | src/mania/Objects/LRZ/Iwamodoki.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/KingAttack.c | NOT_STARTED | src/mania/Objects/LRZ/KingAttack.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/KingClaw.c | NOT_STARTED | src/mania/Objects/LRZ/KingClaw.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/LavaFall.c | NOT_STARTED | src/mania/Objects/LRZ/LavaFall.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/LavaGeyser.c | NOT_STARTED | src/mania/Objects/LRZ/LavaGeyser.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/LRZ1Intro.c | NOT_STARTED | src/mania/Objects/LRZ/LRZ1Intro.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/LRZ1Outro.c | NOT_STARTED | src/mania/Objects/LRZ/LRZ1Outro.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/LRZ1Setup.c | NOT_STARTED | src/mania/Objects/LRZ/LRZ1Setup.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/LRZ2Setup.c | NOT_STARTED | src/mania/Objects/LRZ/LRZ2Setup.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/LRZ3Cutscene.c | NOT_STARTED | src/mania/Objects/LRZ/LRZ3Cutscene.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/LRZ3Outro.c | NOT_STARTED | src/mania/Objects/LRZ/LRZ3Outro.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/LRZ3OutroK.c | NOT_STARTED | src/mania/Objects/LRZ/LRZ3OutroK.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/LRZ3Setup.c | NOT_STARTED | src/mania/Objects/LRZ/LRZ3Setup.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/LRZConvControl.c | NOT_STARTED | src/mania/Objects/LRZ/LRZConvControl.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/LRZConvDropper.c | NOT_STARTED | src/mania/Objects/LRZ/LRZConvDropper.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/LRZConveyor.c | NOT_STARTED | src/mania/Objects/LRZ/LRZConveyor.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/LRZConvItem.c | NOT_STARTED | src/mania/Objects/LRZ/LRZConvItem.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/LRZConvSwitch.c | NOT_STARTED | src/mania/Objects/LRZ/LRZConvSwitch.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/LRZFireball.c | NOT_STARTED | src/mania/Objects/LRZ/LRZFireball.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/LRZRockPile.c | NOT_STARTED | src/mania/Objects/LRZ/LRZRockPile.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/LRZSpikeBall.c | NOT_STARTED | src/mania/Objects/LRZ/LRZSpikeBall.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/LRZSpiral.c | NOT_STARTED | src/mania/Objects/LRZ/LRZSpiral.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/OrbitSpike.c | NOT_STARTED | src/mania/Objects/LRZ/OrbitSpike.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/Rexon.c | NOT_STARTED | src/mania/Objects/LRZ/Rexon.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/RisingLava.c | NOT_STARTED | src/mania/Objects/LRZ/RisingLava.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/RockDrill.c | NOT_STARTED | src/mania/Objects/LRZ/RockDrill.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/SkyTeleporter.c | NOT_STARTED | src/mania/Objects/LRZ/SkyTeleporter.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/SpikeCrusher.c | NOT_STARTED | src/mania/Objects/LRZ/SpikeCrusher.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/Stalactite.c | NOT_STARTED | src/mania/Objects/LRZ/Stalactite.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/ThoughtBubble.c | NOT_STARTED | src/mania/Objects/LRZ/ThoughtBubble.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/Toxomister.c | NOT_STARTED | src/mania/Objects/LRZ/Toxomister.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/Turbine.c | NOT_STARTED | src/mania/Objects/LRZ/Turbine.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/TurretSwitch.c | NOT_STARTED | src/mania/Objects/LRZ/TurretSwitch.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/LRZ/WalkerLegs.c | NOT_STARTED | src/mania/Objects/LRZ/WalkerLegs.c | Phase 7: Lava Reef Zone |
| SonicMania/Objects/Menu/CompetitionMenu.c | NOT_STARTED | src/mania/Objects/Menu/CompetitionMenu.c | Phase 4: menu system |
| SonicMania/Objects/Menu/DAControl.c | NOT_STARTED | src/mania/Objects/Menu/DAControl.c | Phase 4: menu system |
| SonicMania/Objects/Menu/DASetup.c | NOT_STARTED | src/mania/Objects/Menu/DASetup.c | Phase 4: menu system |
| SonicMania/Objects/Menu/DemoMenu.c | NOT_STARTED | src/mania/Objects/Menu/DemoMenu.c | Phase 4: menu system |
| SonicMania/Objects/Menu/E3MenuSetup.c | NOT_STARTED | src/mania/Objects/Menu/E3MenuSetup.c | Phase 4: menu system |
| SonicMania/Objects/Menu/ExtrasMenu.c | NOT_STARTED | src/mania/Objects/Menu/ExtrasMenu.c | Phase 4: menu system |
| SonicMania/Objects/Menu/LevelSelect.c | NOT_STARTED | src/mania/Objects/Menu/LevelSelect.c | Phase 4: menu system |
| SonicMania/Objects/Menu/LogoSetup.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_FRONTEND_LOGOS-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:424) | 2026-07-01 audit: verbatim TU compiled in the LOGOS flavor (build_p6scene_objs.sh:415-423), OVL_FE-linked (build_shipping.sh:210). Open: #266 splash sprite render gap, #272 Logos->Title noise band. |
| SonicMania/Objects/Menu/MainMenu.c | NOT_STARTED | src/mania/Objects/Menu/MainMenu.c | Phase 4: menu system |
| SonicMania/Objects/Menu/ManiaModeMenu.c | NOT_STARTED | src/mania/Objects/Menu/ManiaModeMenu.c | Phase 4: menu system |
| SonicMania/Objects/Menu/MenuParam.c | NOT_STARTED | src/mania/Objects/Menu/MenuParam.c | Phase 4: menu system |
| SonicMania/Objects/Menu/MenuSetup.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_FRONTEND_MENU-gated; 1-line REV02 sed) | tools/_decomp_raw via _MenuSetup_rev02.c (overlay; p6_ovl_ghz.c:511) | 2026-07-01 audit: verbatim TU with ONE build-local sed (GameInfo->platform -> sku_platform, REV02 compat arm; build_p6scene_objs.sh:460-465 — cache untouched), OVL_FE-linked (build_shipping.sh:243). Commits 532f2c5/317a651. Menu start routes Mania Mode -> No Save -> AIZ intro (SetScene "Cutscenes"/"Angel Island Zone", decomp MenuSetup.c:1121). |
| SonicMania/Objects/Menu/OptionsMenu.c | NOT_STARTED | src/mania/Objects/Menu/OptionsMenu.c | Phase 4: menu system |
| SonicMania/Objects/Menu/ThanksSetup.c | NOT_STARTED | src/mania/Objects/Menu/ThanksSetup.c | Phase 4: menu system |
| SonicMania/Objects/Menu/TimeAttackMenu.c | NOT_STARTED | src/mania/Objects/Menu/TimeAttackMenu.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIBackground.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_FRONTEND_MENU-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:519) | 2026-07-01 audit: verbatim TU compiled in the MENU flavor (build_p6scene_objs.sh:471-484), OVL_FE-linked (build_shipping.sh:243); FillScreen routes to p6_fillscreen_saturn (p6_stubs.cpp M1b fix, build_p6scene_objs.sh:263-269). Supersedes the Phase 3.2.a/b src/mania hand-port. |
| SonicMania/Objects/Menu/UIButton.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_FRONTEND_MENU-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:523) | 2026-07-01 audit: verbatim TU compiled in the MENU flavor (build_p6scene_objs.sh:471-484), OVL_FE-linked (build_shipping.sh:243). UIChoice/UIResPicker/UIVsRoundPicker/UIWinSize cross-class edges are inert stubs in p6_menu_closure.c:196-206 (edge-witnessed). Supersedes the Phase 3.2.c.1 src/mania hand-port. |
| SonicMania/Objects/Menu/UIButtonLabel.c | NOT_STARTED | src/mania/Objects/Menu/UIButtonLabel.c | Phase 3.2.c.2: UIHeading + UIText + UIInfoLabel + UIButtonLabel batch |
| SonicMania/Objects/Menu/UIButtonPrompt.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_FRONTEND_MENU-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:535) | 2026-07-01 audit: verbatim TU compiled in the MENU flavor (build_p6scene_objs.sh:471-484), OVL_FE-linked (build_shipping.sh:243); APICallback_GetConfirmButtonFlip stub returns false (p6_closure_edge.c:164). Supersedes the Phase 3.2.c.1 src/mania hand-port. |
| SonicMania/Objects/Menu/UICarousel.c | NOT_STARTED | src/mania/Objects/Menu/UICarousel.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UICharButton.c | NOT_STARTED | src/mania/Objects/Menu/UICharButton.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIChoice.c | NOT_STARTED | src/mania/Objects/Menu/UIChoice.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIControl.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_FRONTEND_MENU-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:515) | 2026-07-01 audit: verbatim TU compiled in the MENU flavor (build_p6scene_objs.sh:471-484), OVL_FE-linked (build_shipping.sh:243). Supersedes the Phase 3.2.a src/mania hand-port. |
| SonicMania/Objects/Menu/UICreditsText.c | NOT_STARTED | src/mania/Objects/Menu/UICreditsText.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIDialog.c | NOT_STARTED | src/mania/Objects/Menu/UIDialog.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIDiorama.c | NOT_STARTED | src/mania/Objects/Menu/UIDiorama.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIHeading.c | NOT_STARTED | src/mania/Objects/Menu/UIHeading.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIInfoLabel.c | NOT_STARTED | src/mania/Objects/Menu/UIInfoLabel.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIKeyBinder.c | NOT_STARTED | src/mania/Objects/Menu/UIKeyBinder.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UILeaderboard.c | NOT_STARTED | src/mania/Objects/Menu/UILeaderboard.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIMedallionPanel.c | NOT_STARTED | src/mania/Objects/Menu/UIMedallionPanel.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIModeButton.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_FRONTEND_MENU-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:545) | 2026-07-01 audit: verbatim TU compiled in the MENU flavor (build_p6scene_objs.sh:471-484, M1b), OVL_FE-linked (build_shipping.sh:243). |
| SonicMania/Objects/Menu/UIOptionPanel.c | NOT_STARTED | src/mania/Objects/Menu/UIOptionPanel.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIPicture.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_FRONTEND_LOGOS-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:428) | 2026-07-01 audit: verbatim TU compiled in the LOGOS flavor (build_p6scene_objs.sh:415-423), OVL_FE-linked (build_shipping.sh:210). |
| SonicMania/Objects/Menu/UIPopover.c | NOT_STARTED | src/mania/Objects/Menu/UIPopover.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIRankButton.c | NOT_STARTED | src/mania/Objects/Menu/UIRankButton.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIReplayCarousel.c | NOT_STARTED | src/mania/Objects/Menu/UIReplayCarousel.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIResPicker.c | NOT_STARTED | src/mania/Objects/Menu/UIResPicker.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UISaveSlot.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_FRONTEND_MENU-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:560) | 2026-07-01 audit: verbatim TU compiled in the MENU flavor (build_p6scene_objs.sh:471-484, M2), OVL_FE-linked (build_shipping.sh:243). Seated by the dual-stride wide-scene sub-pool (588 B entity; commit da7ccaa). Save-file backing still stubbed (see SaveGame row) — No-Save path only. |
| SonicMania/Objects/Menu/UIShifter.c | NOT_STARTED | src/mania/Objects/Menu/UIShifter.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UISlider.c | NOT_STARTED | src/mania/Objects/Menu/UISlider.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UISubHeading.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_FRONTEND_MENU-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:531) | 2026-07-01 audit: verbatim TU compiled in the MENU flavor (build_p6scene_objs.sh:471-484), OVL_FE-linked (build_shipping.sh:243). Supersedes the Phase 3.2.c.1 src/mania hand-port. |
| SonicMania/Objects/Menu/UITABanner.c | NOT_STARTED | src/mania/Objects/Menu/UITABanner.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UITAZoneModule.c | NOT_STARTED | src/mania/Objects/Menu/UITAZoneModule.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIText.c | NOT_STARTED | src/mania/Objects/Menu/UIText.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UITransition.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_FRONTEND_MENU-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:569) | 2026-07-01 audit: verbatim TU compiled in the MENU flavor (build_p6scene_objs.sh:471-484, M2), OVL_FE-linked (build_shipping.sh:243). Commit da7ccaa. |
| SonicMania/Objects/Menu/UIUsernamePopup.c | NOT_STARTED | src/mania/Objects/Menu/UIUsernamePopup.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIVideo.c | NOT_STARTED | src/mania/Objects/Menu/UIVideo.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIVsCharSelector.c | NOT_STARTED | src/mania/Objects/Menu/UIVsCharSelector.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIVsResults.c | NOT_STARTED | src/mania/Objects/Menu/UIVsResults.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIVsRoundPicker.c | NOT_STARTED | src/mania/Objects/Menu/UIVsRoundPicker.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIVsScoreboard.c | NOT_STARTED | src/mania/Objects/Menu/UIVsScoreboard.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIVsZoneButton.c | NOT_STARTED | src/mania/Objects/Menu/UIVsZoneButton.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIWaitSpinner.c | NOT_STARTED | src/mania/Objects/Menu/UIWaitSpinner.c | Phase 4: menu system |
| SonicMania/Objects/Menu/UIWidgets.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_FRONTEND_MENU-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:527) | 2026-07-01 audit: verbatim TU compiled in the MENU flavor (build_p6scene_objs.sh:471-484), OVL_FE-linked (build_shipping.sh:243); parallelogram/plate draws route through RSDK.DrawFace -> p6_drawface_saturn (M3 #295 fix, p6_pack_stubs.cpp forward, build_p6scene_objs.sh:273-280). Supersedes the Phase 3.2.b src/mania hand-port. |
| SonicMania/Objects/Menu/UIWinSize.c | NOT_STARTED | src/mania/Objects/Menu/UIWinSize.c | Phase 4: menu system |
| SonicMania/Objects/MMZ/BladePole.c | NOT_STARTED | src/mania/Objects/MMZ/BladePole.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/BuzzSaw.c | NOT_STARTED | src/mania/Objects/MMZ/BuzzSaw.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/ConveyorBelt.c | NOT_STARTED | src/mania/Objects/MMZ/ConveyorBelt.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/ConveyorPlatform.c | NOT_STARTED | src/mania/Objects/MMZ/ConveyorPlatform.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/ConveyorWheel.c | NOT_STARTED | src/mania/Objects/MMZ/ConveyorWheel.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/EggPistonsMKII.c | NOT_STARTED | src/mania/Objects/MMZ/EggPistonsMKII.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/FarPlane.c | NOT_STARTED | src/mania/Objects/MMZ/FarPlane.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/Gachapandora.c | NOT_STARTED | src/mania/Objects/MMZ/Gachapandora.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/MatryoshkaBom.c | NOT_STARTED | src/mania/Objects/MMZ/MatryoshkaBom.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/MechaBu.c | NOT_STARTED | src/mania/Objects/MMZ/MechaBu.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/MMZ2Outro.c | NOT_STARTED | src/mania/Objects/MMZ/MMZ2Outro.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/MMZLightning.c | NOT_STARTED | src/mania/Objects/MMZ/MMZLightning.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/MMZSetup.c | NOT_STARTED | src/mania/Objects/MMZ/MMZSetup.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/MMZWheel.c | NOT_STARTED | src/mania/Objects/MMZ/MMZWheel.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/Piston.c | NOT_STARTED | src/mania/Objects/MMZ/Piston.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/PlaneSeeSaw.c | NOT_STARTED | src/mania/Objects/MMZ/PlaneSeeSaw.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/PohBee.c | NOT_STARTED | src/mania/Objects/MMZ/PohBee.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/RPlaneShifter.c | NOT_STARTED | src/mania/Objects/MMZ/RPlaneShifter.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/Scarab.c | NOT_STARTED | src/mania/Objects/MMZ/Scarab.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/SizeLaser.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:181) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack (inert Player-closure ref at GHZ scope). |
| SonicMania/Objects/MMZ/SpikeCorridor.c | NOT_STARTED | src/mania/Objects/MMZ/SpikeCorridor.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MMZ/VanishPlatform.c | NOT_STARTED | src/mania/Objects/MMZ/VanishPlatform.c | Phase 7: Mirage Saloon (MMZ) Zone |
| SonicMania/Objects/MSZ/Armadiloid.c | NOT_STARTED | src/mania/Objects/MSZ/Armadiloid.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/BarStool.c | NOT_STARTED | src/mania/Objects/MSZ/BarStool.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/Bumpalo.c | NOT_STARTED | src/mania/Objects/MSZ/Bumpalo.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/Cactula.c | NOT_STARTED | src/mania/Objects/MSZ/Cactula.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/CollapsingSand.c | NOT_STARTED | src/mania/Objects/MSZ/CollapsingSand.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/DBTower.c | NOT_STARTED | src/mania/Objects/MSZ/DBTower.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/EggLoco.c | NOT_STARTED | src/mania/Objects/MSZ/EggLoco.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/Flipper.c | NOT_STARTED | src/mania/Objects/MSZ/Flipper.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/GiantPistol.c | NOT_STARTED | src/mania/Objects/MSZ/GiantPistol.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/Hatterkiller.c | NOT_STARTED | src/mania/Objects/MSZ/Hatterkiller.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/HeavyMystic.c | NOT_STARTED | src/mania/Objects/MSZ/HeavyMystic.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/Honkytonk.c | NOT_STARTED | src/mania/Objects/MSZ/Honkytonk.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/LightBulb.c | NOT_STARTED | src/mania/Objects/MSZ/LightBulb.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/LocoSmoke.c | NOT_STARTED | src/mania/Objects/MSZ/LocoSmoke.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/MSZ1KIntro.c | NOT_STARTED | src/mania/Objects/MSZ/MSZ1KIntro.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/MSZ2Cutscene.c | NOT_STARTED | src/mania/Objects/MSZ/MSZ2Cutscene.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/MSZCutsceneK.c | NOT_STARTED | src/mania/Objects/MSZ/MSZCutsceneK.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/MSZCutsceneST.c | NOT_STARTED | src/mania/Objects/MSZ/MSZCutsceneST.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/MSZSetup.c | NOT_STARTED | src/mania/Objects/MSZ/MSZSetup.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/MSZSpotlight.c | NOT_STARTED | src/mania/Objects/MSZ/MSZSpotlight.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/PaintingEyes.c | NOT_STARTED | src/mania/Objects/MSZ/PaintingEyes.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/Pinata.c | NOT_STARTED | src/mania/Objects/MSZ/Pinata.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/Rattlekiller.c | NOT_STARTED | src/mania/Objects/MSZ/Rattlekiller.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/RollerMKII.c | NOT_STARTED | src/mania/Objects/MSZ/RollerMKII.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/RotatingSpikes.c | NOT_STARTED | src/mania/Objects/MSZ/RotatingSpikes.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/SeeSaw.c | NOT_STARTED | src/mania/Objects/MSZ/SeeSaw.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/SeltzerBottle.c | NOT_STARTED | src/mania/Objects/MSZ/SeltzerBottle.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/SeltzerWater.c | NOT_STARTED | src/mania/Objects/MSZ/SeltzerWater.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/SideBarrel.c | NOT_STARTED | src/mania/Objects/MSZ/SideBarrel.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/SwingRope.c | NOT_STARTED | src/mania/Objects/MSZ/SwingRope.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/Tornado.c | NOT_STARTED | src/mania/Objects/MSZ/Tornado.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/TornadoPath.c | NOT_STARTED | src/mania/Objects/MSZ/TornadoPath.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/UberCaterkiller.c | NOT_STARTED | src/mania/Objects/MSZ/UberCaterkiller.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/MSZ/Vultron.c | NOT_STARTED | src/mania/Objects/MSZ/Vultron.c | Phase 7: Mirage Saloon Zone |
| SonicMania/Objects/OOZ/Aquis.c | NOT_STARTED | src/mania/Objects/OOZ/Aquis.c | Phase 7: Oil Ocean Zone |
| SonicMania/Objects/OOZ/BallCannon.c | NOT_STARTED | src/mania/Objects/OOZ/BallCannon.c | Phase 7: Oil Ocean Zone |
| SonicMania/Objects/OOZ/GasPlatform.c | NOT_STARTED | src/mania/Objects/OOZ/GasPlatform.c | Phase 7: Oil Ocean Zone |
| SonicMania/Objects/OOZ/Hatch.c | NOT_STARTED | src/mania/Objects/OOZ/Hatch.c | Phase 7: Oil Ocean Zone |
| SonicMania/Objects/OOZ/MegaOctus.c | NOT_STARTED | src/mania/Objects/OOZ/MegaOctus.c | Phase 7: Oil Ocean Zone |
| SonicMania/Objects/OOZ/MeterDroid.c | NOT_STARTED | src/mania/Objects/OOZ/MeterDroid.c | Phase 7: Oil Ocean Zone |
| SonicMania/Objects/OOZ/Octus.c | NOT_STARTED | src/mania/Objects/OOZ/Octus.c | Phase 7: Oil Ocean Zone |
| SonicMania/Objects/OOZ/OOZ1Outro.c | NOT_STARTED | src/mania/Objects/OOZ/OOZ1Outro.c | Phase 7: Oil Ocean Zone |
| SonicMania/Objects/OOZ/OOZ2Outro.c | NOT_STARTED | src/mania/Objects/OOZ/OOZ2Outro.c | Phase 7: Oil Ocean Zone |
| SonicMania/Objects/OOZ/OOZFlames.c | NOT_STARTED | src/mania/Objects/OOZ/OOZFlames.c | Phase 7: Oil Ocean Zone |
| SonicMania/Objects/OOZ/OOZSetup.c | NOT_STARTED | src/mania/Objects/OOZ/OOZSetup.c | Phase 7: Oil Ocean Zone |
| SonicMania/Objects/OOZ/PullSwitch.c | NOT_STARTED | src/mania/Objects/OOZ/PullSwitch.c | Phase 7: Oil Ocean Zone |
| SonicMania/Objects/OOZ/PushSpring.c | NOT_STARTED | src/mania/Objects/OOZ/PushSpring.c | Phase 7: Oil Ocean Zone |
| SonicMania/Objects/OOZ/Smog.c | NOT_STARTED | src/mania/Objects/OOZ/Smog.c | Phase 7: Oil Ocean Zone |
| SonicMania/Objects/OOZ/Sol.c | NOT_STARTED | src/mania/Objects/OOZ/Sol.c | Phase 7: Oil Ocean Zone |
| SonicMania/Objects/OOZ/Valve.c | NOT_STARTED | src/mania/Objects/OOZ/Valve.c | Phase 7: Oil Ocean Zone |
| SonicMania/Objects/PGZ/Acetone.c | NOT_STARTED | src/mania/Objects/PGZ/Acetone.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/Crate.c | NOT_STARTED | src/mania/Objects/PGZ/Crate.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/DoorTrigger.c | NOT_STARTED | src/mania/Objects/PGZ/DoorTrigger.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/Dragonfly.c | NOT_STARTED | src/mania/Objects/PGZ/Dragonfly.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/FrostThrower.c | NOT_STARTED | src/mania/Objects/PGZ/FrostThrower.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/HeavyShinobi.c | NOT_STARTED | src/mania/Objects/PGZ/HeavyShinobi.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/Ice.c | ENGINE_VERBATIM (P6 pack TU) | tools/_decomp_raw (pack; p6_wave1_reg.c:168) | 2026-07-01 audit: verbatim TU compiled by build_p6scene_objs.sh w2 loop (:336-362) into the engine pack (inert Player-closure ref at GHZ scope). |
| SonicMania/Objects/PGZ/IceBomba.c | NOT_STARTED | src/mania/Objects/PGZ/IceBomba.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/IceSpring.c | NOT_STARTED | src/mania/Objects/PGZ/IceSpring.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/Ink.c | NOT_STARTED | src/mania/Objects/PGZ/Ink.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/InkWipe.c | NOT_STARTED | src/mania/Objects/PGZ/InkWipe.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/JuggleSaw.c | NOT_STARTED | src/mania/Objects/PGZ/JuggleSaw.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/Newspaper.c | NOT_STARTED | src/mania/Objects/PGZ/Newspaper.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/PaperRoller.c | NOT_STARTED | src/mania/Objects/PGZ/PaperRoller.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/PetalPile.c | NOT_STARTED | src/mania/Objects/PGZ/PetalPile.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/Press.c | NOT_STARTED | src/mania/Objects/PGZ/Press.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/PrintBlock.c | NOT_STARTED | src/mania/Objects/PGZ/PrintBlock.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/PSZ1Intro.c | NOT_STARTED | src/mania/Objects/PGZ/PSZ1Intro.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/PSZ1Setup.c | NOT_STARTED | src/mania/Objects/PGZ/PSZ1Setup.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/PSZ2Intro.c | NOT_STARTED | src/mania/Objects/PGZ/PSZ2Intro.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/PSZ2Outro.c | NOT_STARTED | src/mania/Objects/PGZ/PSZ2Outro.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/PSZ2Setup.c | NOT_STARTED | src/mania/Objects/PGZ/PSZ2Setup.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/PSZDoor.c | NOT_STARTED | src/mania/Objects/PGZ/PSZDoor.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/PSZEggman.c | NOT_STARTED | src/mania/Objects/PGZ/PSZEggman.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/PSZLauncher.c | NOT_STARTED | src/mania/Objects/PGZ/PSZLauncher.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/Shiversaw.c | NOT_STARTED | src/mania/Objects/PGZ/Shiversaw.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/Shuriken.c | NOT_STARTED | src/mania/Objects/PGZ/Shuriken.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/Snowflakes.c | NOT_STARTED | src/mania/Objects/PGZ/Snowflakes.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/SP500.c | NOT_STARTED | src/mania/Objects/PGZ/SP500.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/SP500MkII.c | NOT_STARTED | src/mania/Objects/PGZ/SP500MkII.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/Turntable.c | NOT_STARTED | src/mania/Objects/PGZ/Turntable.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/WoodChipper.c | NOT_STARTED | src/mania/Objects/PGZ/WoodChipper.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/PGZ/Woodrow.c | NOT_STARTED | src/mania/Objects/PGZ/Woodrow.c | Phase 7: Press Garden Zone |
| SonicMania/Objects/Pinball/PBL_Bumper.c | NOT_STARTED | src/mania/Objects/Pinball/PBL_Bumper.c | Phase 6: Pinball mini-game |
| SonicMania/Objects/Pinball/PBL_Camera.c | NOT_STARTED | src/mania/Objects/Pinball/PBL_Camera.c | Phase 6: Pinball mini-game |
| SonicMania/Objects/Pinball/PBL_Crane.c | NOT_STARTED | src/mania/Objects/Pinball/PBL_Crane.c | Phase 6: Pinball mini-game |
| SonicMania/Objects/Pinball/PBL_Flipper.c | NOT_STARTED | src/mania/Objects/Pinball/PBL_Flipper.c | Phase 6: Pinball mini-game |
| SonicMania/Objects/Pinball/PBL_HUD.c | NOT_STARTED | src/mania/Objects/Pinball/PBL_HUD.c | Phase 6: Pinball mini-game |
| SonicMania/Objects/Pinball/PBL_Player.c | NOT_STARTED | src/mania/Objects/Pinball/PBL_Player.c | Phase 6: Pinball mini-game |
| SonicMania/Objects/Pinball/PBL_Ring.c | NOT_STARTED | src/mania/Objects/Pinball/PBL_Ring.c | Phase 6: Pinball mini-game |
| SonicMania/Objects/Pinball/PBL_Sector.c | NOT_STARTED | src/mania/Objects/Pinball/PBL_Sector.c | Phase 6: Pinball mini-game |
| SonicMania/Objects/Pinball/PBL_Setup.c | NOT_STARTED | src/mania/Objects/Pinball/PBL_Setup.c | Phase 6: Pinball mini-game |
| SonicMania/Objects/Pinball/PBL_TargetBumper.c | NOT_STARTED | src/mania/Objects/Pinball/PBL_TargetBumper.c | Phase 6: Pinball mini-game |
| SonicMania/Objects/Puyo/PuyoAI.c | NOT_STARTED | src/mania/Objects/Puyo/PuyoAI.c | Phase 6: Puyo Puyo mini-game |
| SonicMania/Objects/Puyo/PuyoAttack.c | NOT_STARTED | src/mania/Objects/Puyo/PuyoAttack.c | Phase 6: Puyo Puyo mini-game |
| SonicMania/Objects/Puyo/PuyoBean.c | NOT_STARTED | src/mania/Objects/Puyo/PuyoBean.c | Phase 6: Puyo Puyo mini-game |
| SonicMania/Objects/Puyo/PuyoGame.c | NOT_STARTED | src/mania/Objects/Puyo/PuyoGame.c | Phase 6: Puyo Puyo mini-game |
| SonicMania/Objects/Puyo/PuyoIndicator.c | NOT_STARTED | src/mania/Objects/Puyo/PuyoIndicator.c | Phase 6: Puyo Puyo mini-game |
| SonicMania/Objects/Puyo/PuyoLabel.c | NOT_STARTED | src/mania/Objects/Puyo/PuyoLabel.c | Phase 6: Puyo Puyo mini-game |
| SonicMania/Objects/Puyo/PuyoLevelSelect.c | NOT_STARTED | src/mania/Objects/Puyo/PuyoLevelSelect.c | Phase 6: Puyo Puyo mini-game |
| SonicMania/Objects/Puyo/PuyoMatch.c | NOT_STARTED | src/mania/Objects/Puyo/PuyoMatch.c | Phase 6: Puyo Puyo mini-game |
| SonicMania/Objects/Puyo/PuyoScore.c | NOT_STARTED | src/mania/Objects/Puyo/PuyoScore.c | Phase 6: Puyo Puyo mini-game |
| SonicMania/Objects/SBZ/Bomb.c | NOT_STARTED | src/mania/Objects/SBZ/Bomb.c | Phase Z: Scrap Brain Zone (cut) |
| SonicMania/Objects/SBZ/Caterkiller.c | NOT_STARTED | src/mania/Objects/SBZ/Caterkiller.c | Phase Z: Scrap Brain Zone (cut) |
| SonicMania/Objects/SBZ/Orbinaut.c | NOT_STARTED | src/mania/Objects/SBZ/Orbinaut.c | Phase Z: Scrap Brain Zone (cut) |
| SonicMania/Objects/SPZ/CableWarp.c | NOT_STARTED | src/mania/Objects/SPZ/CableWarp.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/Canista.c | NOT_STARTED | src/mania/Objects/SPZ/Canista.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/CircleBumper.c | NOT_STARTED | src/mania/Objects/SPZ/CircleBumper.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/Clapperboard.c | NOT_STARTED | src/mania/Objects/SPZ/Clapperboard.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/DirectorChair.c | NOT_STARTED | src/mania/Objects/SPZ/DirectorChair.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/EggJanken.c | NOT_STARTED | src/mania/Objects/SPZ/EggJanken.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/EggJankenPart.c | NOT_STARTED | src/mania/Objects/SPZ/EggJankenPart.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/EggTV.c | NOT_STARTED | src/mania/Objects/SPZ/EggTV.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/FilmProjector.c | NOT_STARTED | src/mania/Objects/SPZ/FilmProjector.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/FilmReel.c | NOT_STARTED | src/mania/Objects/SPZ/FilmReel.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/Funnel.c | NOT_STARTED | src/mania/Objects/SPZ/Funnel.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/GreenScreen.c | NOT_STARTED | src/mania/Objects/SPZ/GreenScreen.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/HeavyGunner.c | NOT_STARTED | src/mania/Objects/SPZ/HeavyGunner.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/LEDPanel.c | NOT_STARTED | src/mania/Objects/SPZ/LEDPanel.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/Letterboard.c | NOT_STARTED | src/mania/Objects/SPZ/Letterboard.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/LottoBall.c | NOT_STARTED | src/mania/Objects/SPZ/LottoBall.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/LottoMachine.c | NOT_STARTED | src/mania/Objects/SPZ/LottoMachine.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/LoveTester.c | NOT_STARTED | src/mania/Objects/SPZ/LoveTester.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/MicDrop.c | NOT_STARTED | src/mania/Objects/SPZ/MicDrop.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/PathInverter.c | NOT_STARTED | src/mania/Objects/SPZ/PathInverter.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/PimPom.c | NOT_STARTED | src/mania/Objects/SPZ/PimPom.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/PopcornKernel.c | NOT_STARTED | src/mania/Objects/SPZ/PopcornKernel.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/PopcornMachine.c | NOT_STARTED | src/mania/Objects/SPZ/PopcornMachine.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/RockemSockem.c | NOT_STARTED | src/mania/Objects/SPZ/RockemSockem.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/ShopWindow.c | NOT_STARTED | src/mania/Objects/SPZ/ShopWindow.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/Shutterbug.c | NOT_STARTED | src/mania/Objects/SPZ/Shutterbug.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/SpinSign.c | NOT_STARTED | src/mania/Objects/SPZ/SpinSign.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/SPZ1Intro.c | NOT_STARTED | src/mania/Objects/SPZ/SPZ1Intro.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/SPZ1Setup.c | NOT_STARTED | src/mania/Objects/SPZ/SPZ1Setup.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/SPZ2Outro.c | NOT_STARTED | src/mania/Objects/SPZ/SPZ2Outro.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/SPZ2Setup.c | NOT_STARTED | src/mania/Objects/SPZ/SPZ2Setup.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/Tubinaut.c | NOT_STARTED | src/mania/Objects/SPZ/Tubinaut.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/TVFlyingBattery.c | NOT_STARTED | src/mania/Objects/SPZ/TVFlyingBattery.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/TVPole.c | NOT_STARTED | src/mania/Objects/SPZ/TVPole.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/TVVan.c | NOT_STARTED | src/mania/Objects/SPZ/TVVan.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/WeatherMobile.c | NOT_STARTED | src/mania/Objects/SPZ/WeatherMobile.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SPZ/WeatherTV.c | NOT_STARTED | src/mania/Objects/SPZ/WeatherTV.c | Phase 7: Studiopolis Zone |
| SonicMania/Objects/SSZ/Beanstalk.c | NOT_STARTED | src/mania/Objects/SSZ/Beanstalk.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/BouncePlant.c | NOT_STARTED | src/mania/Objects/SSZ/BouncePlant.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/Bungee.c | NOT_STARTED | src/mania/Objects/SSZ/Bungee.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/Constellation.c | NOT_STARTED | src/mania/Objects/SSZ/Constellation.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/Dango.c | NOT_STARTED | src/mania/Objects/SSZ/Dango.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/EggTower.c | NOT_STARTED | src/mania/Objects/SSZ/EggTower.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/Fireflies.c | NOT_STARTED | src/mania/Objects/SSZ/Fireflies.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/Firework.c | NOT_STARTED | src/mania/Objects/SSZ/Firework.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/FlowerPod.c | NOT_STARTED | src/mania/Objects/SSZ/FlowerPod.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/GigaMetal.c | NOT_STARTED | src/mania/Objects/SSZ/GigaMetal.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/HiLoSign.c | NOT_STARTED | src/mania/Objects/SSZ/HiLoSign.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/Hotaru.c | NOT_STARTED | src/mania/Objects/SSZ/Hotaru.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/HotaruHiWatt.c | NOT_STARTED | src/mania/Objects/SSZ/HotaruHiWatt.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/HotaruMKII.c | NOT_STARTED | src/mania/Objects/SSZ/HotaruMKII.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/JunctionWheel.c | NOT_STARTED | src/mania/Objects/SSZ/JunctionWheel.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/Kabasira.c | NOT_STARTED | src/mania/Objects/SSZ/Kabasira.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/Kanabun.c | NOT_STARTED | src/mania/Objects/SSZ/Kanabun.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/MetalSonic.c | NOT_STARTED | src/mania/Objects/SSZ/MetalSonic.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/MonarchPlans.c | NOT_STARTED | src/mania/Objects/SSZ/MonarchPlans.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/MSBomb.c | NOT_STARTED | src/mania/Objects/SSZ/MSBomb.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/MSFactory.c | NOT_STARTED | src/mania/Objects/SSZ/MSFactory.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/MSHologram.c | NOT_STARTED | src/mania/Objects/SSZ/MSHologram.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/MSOrb.c | NOT_STARTED | src/mania/Objects/SSZ/MSOrb.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/MSPanel.c | NOT_STARTED | src/mania/Objects/SSZ/MSPanel.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/RTeleporter.c | NOT_STARTED | src/mania/Objects/SSZ/RTeleporter.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/SDashWheel.c | NOT_STARTED | src/mania/Objects/SSZ/SDashWheel.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/SilverSonic.c | NOT_STARTED | src/mania/Objects/SSZ/SilverSonic.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/SparkRail.c | NOT_STARTED | src/mania/Objects/SSZ/SparkRail.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/SpikeFlail.c | NOT_STARTED | src/mania/Objects/SSZ/SpikeFlail.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/SSZ1Intro.c | NOT_STARTED | src/mania/Objects/SSZ/SSZ1Intro.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/SSZ1Outro.c | NOT_STARTED | src/mania/Objects/SSZ/SSZ1Outro.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/SSZ1Setup.c | NOT_STARTED | src/mania/Objects/SSZ/SSZ1Setup.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/SSZ2Setup.c | NOT_STARTED | src/mania/Objects/SSZ/SSZ2Setup.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/SSZ3Cutscene.c | NOT_STARTED | src/mania/Objects/SSZ/SSZ3Cutscene.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/SSZEggman.c | NOT_STARTED | src/mania/Objects/SSZ/SSZEggman.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/SSZSpikeBall.c | NOT_STARTED | src/mania/Objects/SSZ/SSZSpikeBall.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/SSZSpotlight.c | NOT_STARTED | src/mania/Objects/SSZ/SSZSpotlight.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/TimePost.c | NOT_STARTED | src/mania/Objects/SSZ/TimePost.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/TimeTravelSetup.c | NOT_STARTED | src/mania/Objects/SSZ/TimeTravelSetup.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/TTCutscene.c | NOT_STARTED | src/mania/Objects/SSZ/TTCutscene.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/UncurlPlant.c | NOT_STARTED | src/mania/Objects/SSZ/UncurlPlant.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/SSZ/YoyoPulley.c | NOT_STARTED | src/mania/Objects/SSZ/YoyoPulley.c | Phase 7: Stardust Speedway Zone |
| SonicMania/Objects/Summary/Summary.c | NOT_STARTED | src/mania/Objects/Summary/Summary.c | Phase 5: results summary |
| SonicMania/Objects/Summary/SummaryEmerald.c | NOT_STARTED | src/mania/Objects/Summary/SummaryEmerald.c | Phase 5: results summary |
| SonicMania/Objects/Title/Title3DSprite.c | ENGINE_VERBATIM (P6 front-end overlay TU; compiled in TITLE flavor, registration default-gated OFF) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:495) | 2026-07-01 audit: verbatim TU compiled in the TITLE flavor (build_p6scene_objs.sh:430-446, CP5b.3 #272); registration behind P6_TITLE3D_ON (default off — 5 billboard rects would thrash the 10-slot title VDP1 cache, memory title-vdp1-slot-thrash). |
| SonicMania/Objects/Title/TitleBG.c | ENGINE_VERBATIM (P6 front-end overlay TU; compiled in TITLE flavor, registration default-gated OFF) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:478) | 2026-07-01 audit: verbatim TU compiled in the TITLE flavor (build_p6scene_objs.sh:430-446). The ONLY cached-decomp Saturn gate outside CutsceneHBH: TitleBG.c:119-122 #if !defined(P6_FRONTEND_TITLE) skips the scanline-callback installs (PC path verbatim in the build without the flag); backdrop drives VDP2 natively (p6_vdp2_present_title_backdrop + RBG0 island #276). Registration excluded by default per P6_TITLEBG_SPRITES_OFF (build_p6scene_objs.sh:93-101; VDP1 slot thrash, CP5b.5). Open: #290 clouds NBG1/B1 vs RBG0/B0 bank conflict. |
| SonicMania/Objects/Title/TitleEggman.c | NOT_STARTED | src/mania/Objects/Title/TitleEggman.c | Phase 1: Title Eggman cameo |
| SonicMania/Objects/Title/TitleLogo.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_FRONTEND_TITLE-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:444) | 2026-07-01 audit: verbatim TU compiled in the TITLE flavor (build_p6scene_objs.sh:430-446), OVL_FE-linked (build_shipping.sh:219). Supersedes the src/mania Phase 1.2 hand-port row above. |
| SonicMania/Objects/Title/TitleSetup.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_FRONTEND_TITLE-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:440) | 2026-07-01 audit: verbatim TU compiled in the TITLE flavor (build_p6scene_objs.sh:430-446), OVL_FE-linked (build_shipping.sh:219). Supersedes the src/mania Phase 1.2 hand-port row above. |
| SonicMania/Objects/Title/TitleSonic.c | ENGINE_VERBATIM (P6 front-end overlay TU, P6_FRONTEND_TITLE-gated) | tools/_decomp_raw (overlay; p6_ovl_ghz.c:453) | 2026-07-01 audit: verbatim TU compiled in the TITLE flavor (build_p6scene_objs.sh:430-446, CP5b.2 #269), OVL_FE-linked (build_shipping.sh:219). Supersedes the src/mania Phase 1.2 hand-port row above. |
| SonicMania/Objects/TMZ/BallHog.c | NOT_STARTED | src/mania/Objects/TMZ/BallHog.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/CrashTest.c | NOT_STARTED | src/mania/Objects/TMZ/CrashTest.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/CrimsonEye.c | NOT_STARTED | src/mania/Objects/TMZ/CrimsonEye.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/EscapeCar.c | NOT_STARTED | src/mania/Objects/TMZ/EscapeCar.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/FlasherMKII.c | NOT_STARTED | src/mania/Objects/TMZ/FlasherMKII.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/GymBar.c | NOT_STARTED | src/mania/Objects/TMZ/GymBar.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/JacobsLadder.c | NOT_STARTED | src/mania/Objects/TMZ/JacobsLadder.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/LargeGear.c | NOT_STARTED | src/mania/Objects/TMZ/LargeGear.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/LaunchSpring.c | NOT_STARTED | src/mania/Objects/TMZ/LaunchSpring.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/MagnetSphere.c | NOT_STARTED | src/mania/Objects/TMZ/MagnetSphere.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/MetalArm.c | NOT_STARTED | src/mania/Objects/TMZ/MetalArm.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/MonarchBG.c | NOT_STARTED | src/mania/Objects/TMZ/MonarchBG.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/PopOut.c | NOT_STARTED | src/mania/Objects/TMZ/PopOut.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/SentryBug.c | NOT_STARTED | src/mania/Objects/TMZ/SentryBug.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/TeeterTotter.c | NOT_STARTED | src/mania/Objects/TMZ/TeeterTotter.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/TMZ1Outro.c | NOT_STARTED | src/mania/Objects/TMZ/TMZ1Outro.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/TMZ1Setup.c | NOT_STARTED | src/mania/Objects/TMZ/TMZ1Setup.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/TMZ2Outro.c | NOT_STARTED | src/mania/Objects/TMZ/TMZ2Outro.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/TMZ2Setup.c | NOT_STARTED | src/mania/Objects/TMZ/TMZ2Setup.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/TMZ3Setup.c | NOT_STARTED | src/mania/Objects/TMZ/TMZ3Setup.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/TMZAlert.c | NOT_STARTED | src/mania/Objects/TMZ/TMZAlert.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/TMZBarrier.c | NOT_STARTED | src/mania/Objects/TMZ/TMZBarrier.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/TMZCable.c | NOT_STARTED | src/mania/Objects/TMZ/TMZCable.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/TMZFlames.c | NOT_STARTED | src/mania/Objects/TMZ/TMZFlames.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/TurboTurtle.c | NOT_STARTED | src/mania/Objects/TMZ/TurboTurtle.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/TMZ/WallBumper.c | NOT_STARTED | src/mania/Objects/TMZ/WallBumper.c | Phase 7: Titanic Monarch Zone |
| SonicMania/Objects/UFO/SpecialClear.c | NOT_STARTED | src/mania/Objects/UFO/SpecialClear.c | Phase 5: UFO bonus stage |
| SonicMania/Objects/UFO/UFO_Camera.c | NOT_STARTED | src/mania/Objects/UFO/UFO_Camera.c | Phase 5: UFO bonus stage |
| SonicMania/Objects/UFO/UFO_Circuit.c | NOT_STARTED | src/mania/Objects/UFO/UFO_Circuit.c | Phase 5: UFO bonus stage |
| SonicMania/Objects/UFO/UFO_Decoration.c | NOT_STARTED | src/mania/Objects/UFO/UFO_Decoration.c | Phase 5: UFO bonus stage |
| SonicMania/Objects/UFO/UFO_Dust.c | NOT_STARTED | src/mania/Objects/UFO/UFO_Dust.c | Phase 5: UFO bonus stage |
| SonicMania/Objects/UFO/UFO_HUD.c | NOT_STARTED | src/mania/Objects/UFO/UFO_HUD.c | Phase 5: UFO bonus stage |
| SonicMania/Objects/UFO/UFO_ItemBox.c | NOT_STARTED | src/mania/Objects/UFO/UFO_ItemBox.c | Phase 5: UFO bonus stage |
| SonicMania/Objects/UFO/UFO_Message.c | NOT_STARTED | src/mania/Objects/UFO/UFO_Message.c | Phase 5: UFO bonus stage |
| SonicMania/Objects/UFO/UFO_Plasma.c | NOT_STARTED | src/mania/Objects/UFO/UFO_Plasma.c | Phase 5: UFO bonus stage |
| SonicMania/Objects/UFO/UFO_Player.c | NOT_STARTED | src/mania/Objects/UFO/UFO_Player.c | Phase 5: UFO bonus stage |
| SonicMania/Objects/UFO/UFO_Ring.c | NOT_STARTED | src/mania/Objects/UFO/UFO_Ring.c | Phase 5: UFO bonus stage |
| SonicMania/Objects/UFO/UFO_Setup.c | NOT_STARTED | src/mania/Objects/UFO/UFO_Setup.c | Phase 5: UFO bonus stage |
| SonicMania/Objects/UFO/UFO_Shadow.c | NOT_STARTED | src/mania/Objects/UFO/UFO_Shadow.c | Phase 5: UFO bonus stage |
| SonicMania/Objects/UFO/UFO_SpeedLines.c | NOT_STARTED | src/mania/Objects/UFO/UFO_SpeedLines.c | Phase 5: UFO bonus stage |
| SonicMania/Objects/UFO/UFO_Sphere.c | NOT_STARTED | src/mania/Objects/UFO/UFO_Sphere.c | Phase 5: UFO bonus stage |
| SonicMania/Objects/UFO/UFO_Springboard.c | NOT_STARTED | src/mania/Objects/UFO/UFO_Springboard.c | Phase 5: UFO bonus stage |
| SonicMania/Objects/UFO/UFO_Water.c | NOT_STARTED | src/mania/Objects/UFO/UFO_Water.c | Phase 5: UFO bonus stage |
| SonicMania/Objects/Unused/Pendulum.c | NOT_STARTED | src/mania/Objects/Unused/Pendulum.c | Phase Z: unused / cut content |
| SonicMania/Objects/Unused/SpearBlock.c | NOT_STARTED | src/mania/Objects/Unused/SpearBlock.c | Phase Z: unused / cut content |
| SonicMania/Objects/Unused/TargetBumper.c | NOT_STARTED | src/mania/Objects/Unused/TargetBumper.c | Phase Z: unused / cut content |
| SonicMania/Objects/Unused/WallCrawl.c | NOT_STARTED | src/mania/Objects/Unused/WallCrawl.c | Phase Z: unused / cut content |
| SonicMania/Objects/Unused/Wisp.c | NOT_STARTED | src/mania/Objects/Unused/Wisp.c | Phase Z: unused / cut content |

---

## CP4 FRONT-END KEYSTONE PLAN (2026-06-20) — engine LoadScene of a non-GHZ (Logos) scene

**Goal.** Prove the P6.8 engine LoadScene + run path serves a NON-gameplay
UI scene (the Logos Sega/RSDK splash), through the SAME chain that runs GHZ.
Once GREEN, Title/Menu/intro/transitions all load by the same path (just a
different folder). Behind a NEW compile flag `P6_FRONTEND_LOGOS` so the
DEFAULT shipping build still boots GHZ unchanged (qa_p6_ghz_regression must
stay GREEN).

**Gate.** `tools/_portspike/qa_engine_logos.py` (E1-E5). RED baseline
confirmed 2026-06-20 (4/5 witnesses absent from game.map).

**Docs consulted (skill methodology §1-3).** complete-doc-index.md (whole);
VDP1 = ST-013-R3 + vdp1-reference.md (UIPicture DrawSprite->VDP1 sprite);
VDP2 = ST-058-R2 (backdrop/cell — the Logos scene has NO real FG tilemap, so
the cell upload is a no-op for it); SGL = ST-238-R1 (slDispSprite contract).

**Asset facts (MEASURED on disk).**
- `extracted/Data/Stages/Logos/`: Scene1.bin 362 B, 16x16Tiles.gif 1731 B
  (placeholder), StageConfig.bin 203 B, TileConfig.bin 110 B. This is NOT a
  tilemap-driven scene -- it is UIPicture sprites over a FillScreen fade.
- `docs/scene_census.json` Logos/Scene1.bin: object_count 3
  (`875e224b` unmapped@0 instances, LogoSetup@0, UIPicture@4), entity_total 4.
  UIPicture placements: frameID 0 @ (256,120); 1 @ (164,316); 2 @ (364,312);
  3 @ (252,412). The 4 logos stack vertically; LogoSetup scrolls the screen
  down through them with a fade.
- `extracted/Data/Sprites/Logos/Logos.gif` = the 4 logos: SEGA / PagodaWest /
  Christian Whitehead / HeadCannon. `Logos.bin` is the sprite animation
  LogoSetup_StageLoad + UIPicture_StageLoad both load ("Logos/Logos.bin").

**Decomp closure (CLEAN).** LogoSetup.c (158 L) only object dep is UIPicture
(no CreateEntity of others). Engine deps all implemented: GetSfx/LoadSprite-
Animation/PlaySfx/LoadScene/ResetEntitySlot, globals playerID/sku_region,
ControllerInfo, ScreenInfo/SceneInfo. FillScreen is a NO-OP stub in this port
(p6_stubs.cpp:204) -> the fade is not drawn, but the UIPicture logo SPRITES
render via DrawSprite->VDP1 (the implemented GHZ-ring/Sonic path). LoadImage
(CESA, JP-only) is not reached (sku_region != REGION_JP). UIPicture.c (114 L):
ProcessAnimation + DrawSprite + (CopyPalette only when zonePalette set; the
Logos placements leave it 0 -> skipped).

### CP4a (the keystone): boot + run Logos on the engine
- New flag `P6_FRONTEND_LOGOS` threaded through build_p6scene_objs.sh like the
  existing P6_GHZ2_BOOT/P6_STREAM_PROOF env knobs (-> -DP6_FRONTEND_LOGOS).
- `p6_scene_tick` first lean tick: under the flag call `p6_logos_reload()`
  (select folder "Logos" via the category scan, the GHZ-select mirror) instead
  of `p6_ghz_reload()`; then run `p6_frontend_frame()` (a generic ProcessInput
  -> ProcessObjects -> ProcessObjectDrawLists -> present loop that bumps
  p6_w_cont_frames) instead of the GHZ-FG-present `p6_ghz_frame`.
- `p6_scene_load_and_arm` GHZ-tilemap steps GUARDED for the UI scene via a
  `folder=="GHZ"` test: skip the 0x060E0000 collision-hash, the resident
  pre-inflate, the GHZ scan-index/pool-compact, and the GHZ BGM. The band-store
  mount (`p6_layout_mount_for_scene`) already no-ops safely when <TAG>LAYT.BIN
  is absent (Logos has none -> rsdk_storage_load_to_lwram returns <=0 -> the
  `if(b>0 && Mount>0)` guard skips). The VDP2 cell upload is harmless (1731 B
  placeholder) but also skipped for non-GHZ.
- Witnesses added (pack `__attribute__((used)) int32`, -u-rooted): 
  `p6_w_frontend_folder_tag` (2-char tag of the selected folder, e.g. 'L'<<8|'o'),
  `p6_w_logosetup_classid`, `p6_w_uipicture_classid`, `p6_w_logos_objcount`.
- Gate target: E1 (folder_tag != 0) + E5 (cont_frames > 0).

### CP4b (the render): port LogoSetup + UIPicture into the overlay
- Port both decomp files VERBATIM as Game_LogoSetup.o / Game_UIPicture.o (the
  w4 build-loop pattern), link into OVLRING.BIN, register via
  register_object_full from p6_ovl_ghz.c's entry. classIDs resolve in
  registration order.
- The overlay witness fn writes p6_w_logosetup_classid / p6_w_uipicture_classid
  (the p6_w_spring_classid pattern). p6_w_logos_objcount set in
  p6_scene_load_and_arm after InitObjects (live-classID scene-entity census,
  the GHZ entcount mirror).
- Gate target: E2/E3 (classids > 0) + E4 (objcount > 0) + a SCREENSHOT of the
  splash (>=1 logo sprite rendered).

**Acceptance.** qa_engine_logos.py E1-E5 all GREEN on a P6_FRONTEND_LOGOS
capture; qa_p6_mapoverlap.py GREEN (_end < ANIMPAK 0x060B6C00) on that build;
qa_p6_ghz_regression.py R0-R16 GREEN on the DEFAULT (no-flag) build; splash
screenshot saved.

### RESULT (2026-06-20) — measured

- **CP4a keystone: DONE, all GREEN.** Front-end flavor (P6_FRONTEND_LOGOS):
  - qa_engine_logos.py E1 folder_tag=19567 ('Lo'), E2 logosetup_classid=2,
    E3 uipicture_classid=3, E4 logos_objcount=4, E5 cont_frames=1216 — ALL GREEN.
  - qa_p6_mapoverlap.py GREEN: _end 0x060B6640 < ANIMPAK 0x060B6C00 (1472 B headroom).
- **DEFAULT (GHZ) shipping build: NOT regressed.** qa_p6_ghz_regression.py
  R0-R16 all GREEN (boot health, Bridge/Spring/SpikeLog/PlaneSwitch reg + anim
  loads + VDP1 binds + no alloc-fails). qa_p6_mapoverlap GREEN: _end 0x060B6BA0
  < ANIMPAK (96 B headroom). The CP4 code is compile-isolated to the front-end
  flavor (`#if defined(P6_FRONTEND_LOGOS)`); the GHZ build is byte-near-identical.
- **CP4b render (splash PIXELS): REMAINING GAP, root cause narrowed.** The 4
  UIPicture entities load + instantiate (E4=4), the Logos.bin animation loads
  (UIPicture->aniFrames=0, not -1), and a live UIPicture entity has a frame table
  (animator.frames != NULL). LOGOS.SHT (Logos.gif, 512x256, 6.4 KB banded) is
  built + staged into the 10th SaturnSheet slot with the path hash "Logos/Logos.gif"
  (verified = the exact path string inside Logos.bin). BUT the sprite pixels still
  do NOT blit -- the screen stays uniform blue (tools/_portspike/_logos_splash.png).
  Every render-chain link is confirmed GREEN EXCEPT the final VDP1 sheet bind/draw:
  the surface->banded-slot->VDP1-handle resolution for the Logos sheet (and/or the
  Logos palette into fullPalette[0]) is the unresolved link. Leading hypothesis:
  the Logos gfxSurface's saturnSheetSlot isn't resolving to the staged slot at
  bind time, OR the active palette is the GHZ one. NEXT: witness the Logos
  surface's saturnSheetSlot + p6_vdp1HandleBySurface[] after p6_ghz_arm_env, and
  the active palette djb2, to localise which of the two it is.
- LOGOS.SHT added to tools/build_sheet_bands.py SHEETS so the asset regenerates
  (cd/ is .gitignored).


---

## CP5b.6 (#276) — Title island Mode-7 RBG0: coefficient sign-magnitude + lower-band placement (2026-06-23)

DOCS CITED (read this session, end-to-end):
- ST-058-R2 VDP2 §6.4 (VDP2_Manual.txt:7113-7164 Figure 6.7 = the 2-word coefficient
  bit layout; :7196-7208 = MSB transparency semantics; :7181 = KAst*4H 2-word address).
- ST-058-R2 §6.1/6.3 (VDP2_Manual.txt:6340-6371 = the X=kx(Xsp+dX*Hcnt)+Xp rotation
  formula; kx=ky=coeff[line] in mode 0).
- DEMOCOEF MAIN.C:229-290 (CurvedCoeff = the AUTHORITATIVE 2-word per-line writer:
  it stores Abs()-positive `d + FIXED1`, NEVER a negative two's-complement value).
- DEMOCOEF UTIL/VDP2.H:187-192 (vdp2RotParam struct = the VRAM RPT byte layout:
  Xst+0x00 Yst+0x04 ... A+0x1C..F+0x30 ... KAst+0x54 dKAst+0x58 dKAx+0x5C -- CONFIRMS
  the offsets already in p6_vdp2.c, cross-checked vs SGL ROTSCROLL SL_DEF.H:481-510).
- decomp TitleBG.c:159-178 (TitleBG_Scanline_Island = the per-line ground-plane math).

ROOT CAUSE (the SOLE remaining bug, the static bisect P6_TITLE_ISLAND_STATIC already
PROVED every register/bank/cell/map/priority is correct -> a block rendered):
  The live `_frame()` writes `coeff[line] = (long)deform_x` where deform_x = -cos>>7
  is a two's-complement value that is FREQUENTLY NEGATIVE. Per ST-058 Fig 6.7 the
  2-word coeff is SIGN-MAGNITUDE: high-word bit15 = TRANSPARENCY, bit7 = sign. A
  negative two's-complement 16.16 (e.g. -0x28000 = 0xFFFD8000) has bit31 SET =
  bit15-of-high-word = TRANSPARENCY -> the line is forced transparent -> blank island.
  DEMOCOEF sidesteps this with Abs() (always-positive d+FIXED1). 

FIX (2 parts, data-driven, PIXEL-gated):
  1. Coeff encoding: write kx = |deform.x| as a POSITIVE 16.16 magnitude (sign+transp
     bits clear). kx is the per-line magnification (the horizon foreshortening); the
     rotation DIRECTION rides the A/B/D/E matrix, not the coeff sign. |deform.x| in
     [~0.46, 2.5] -> 7-bit integer part fits, bit31 stays 0. Per ST-058 §6.4: a
     positive 16.16 == sign-magnitude positive (integer in bits22-16, fraction bits15-0).
  2. Placement: from the static-bisect MEASURED origin (Xst=Yst=384<<16 -> screen 0,0),
     put island texels 384..640 into screen lower band y[168,240] centered in X:
       Yst = (384-168)<<16 = 0x00D80000 ; Xst = (512-160)<<16 = 0x01600000.
     Rotation: A=cos,B=-sin,D=sin,E=cos (16.16, from Sin/Cos1024(-angle)>>2 <<8).

ACCEPTANCE (PIXEL gate qa_title_island_pixel.py, RED->GREEN):
  P1 lower-band green island mass >= 3000 px in >= half the settled frames.
  P2 the lower-band green column profile differs >= 10% between two rotation angles.
  Register gate qa_title_island_rot.py is INSUFFICIENT (known false-GREEN trap).

FILES TOUCHED: tools/_portspike/_p6/p6_vdp2.c (the _frame coeff loop + Xst/Yst);
  build_p6scene_objs.sh + build_shipping.sh (-u roots for the island witnesses so the
  coeff/Xst/Yst are peekable). NO commit -- user verifies pixels.

SKY/CLOUD TRADEOFF (to MEASURE + REPORT): RBG0 claims bank B0 (RAMCTL 0x1327 RDBSB0=10),
  the bank NBG1's map used -> NBG1 cannot render alongside cell-mode RBG0; the sky is the
  back-color (P6_TITLE_SKY_COL), clouds are lost. Report the measured NBG1/back-color state.

### CP5b.6 OUTCOME (2026-06-23) -- BREAKTHROUGH: island RENDERS + ROTATES

PIXEL GATE qa_title_island_pixel.py: P2 ROTATES = PASS (lower-band green column
profile differs 71% between two rotation angles); lower_green 0->6420 across settled
frames; 9/24 settled frames green>=3000. Screenshot _isl_18.png: green grass island
material visible in the lower-left band, FG fully intact, sky = back-color (clouds gone).
P1 'visible' still RED (needs >=12/24 frames green>=3000; have 9) = a PLACEMENT-coverage
gap, NOT a rendering failure. _end=0x060b61a0 SAFE. Build exit 0.

TWO ROOT-CAUSE BUGS FIXED (both MEASURED; every prior session mis-blamed RPTA/priority/
banks/KTCTL/RPMD -- all RED HERRINGS, re-verified correct):
  BUG 1 (ST-058 Fig 6.7): the 2-word coeff is SIGN-MAGNITUDE (bit31=transparency,
    bit23=sign). The prior `coeff=deform_x` was NEGATIVE two's-complement -> bit31 set ->
    line transparent -> blank. FIX: write |deform.x| POSITIVE (p6_vdp2.c ~line 645).
  BUG 2 (ST-058 Table 6.3): dKAst=0x00040000 = 4 entries/line -> screen line L read
    coeff[L*4]; the island band coeff[168..239] was consumed by screen lines 42..60 (the
    small dark block at screen-top that EVERY prior memory called 'the static bisect
    rendered' -- it was the MIS-INDEXED band all along, never a placement). FIX: dKAst=
    0x00010000 = 1 entry/line + clear non-island lines to 0x80000000 transparent.
  Mode-1 coeff (K_MODE1, kx-only, ky=1.0 from RPT) decouples the clean vertical sweep
    (Yst/dYst place texels 384@line168..640@line239) from the per-line horizontal kx
    perspective. KTCTL peek 0x0005 = RAKTE+mode1 confirms it latched.

SKY/CLOUD TRADEOFF (MEASURED): RBG0 claims VRAM bank B0 (RAMCTL 0x1327 RDBSB0=10 = the
RBG0 PN bank), the same bank NBG1's cloud backdrop map used. A bank claimed for RBG0 has
its cycle pattern ignored (ST-058:6449) so NBG1 cannot coexist -> NBG1 OFF, sky = flat
back-color, CLOUDS GONE. Documented rotating-island-vs-clouds tradeoff. Coexistence
follow-up (out of scope): NBG1 on B1 + VDP2 cycle-pattern reconfigure.

REMAINING (next pass, to clear P1 + finish): tune Xst/scale to center+fill the band
(the green lands lower-left; Xst=571 was the MEASURED best, 1390 was worse); the island
also VANISHES at cos(angle)~0 angles because mode-1 only rotates the horizontal (kx) --
a true 2D affine (matrix A=cos,B=-sin,D=sin,E=cos rotating BOTH axes) keeps it present
at all angles but re-couples placement (tune in one combined pass). NO commit (user
verifies pixels).

TITLE ISLAND -- FULL FIX (2026-06-23, P6_FRONTEND_TITLE; user pixel-verified). The Mania
island now renders as a VDP2 RBG0 Mode-7 perspective floor showing the COMPLETE four-quadrant
Sonic-head, centered, rotating. Two measured root causes, both fixed: (1) decomp-exact pivot
restored -- Mx/My center on texel 512 with the full 0xA000 viewpoint swing (an earlier
session's 448 center + halved 0x5000 swing was a green-centroid-gate over-fit that showed only
a sliver). Proven byte-exact vs an offline decomp simulator (tools/_portspike/sim_island_decomp.py
reproduces TitleBG_Scanline_Island; band mean|diff|=0.00 at every angle) AND the live hardware
witness (isl_tx/ty == the hand-computed decomp formula). (2) "only 1 of 4 pieces" -- the RBG0
plane renders a single 512x512 page and the 256x256 head straddles the texel-512 page boundary,
so only its top-left quadrant landed in the rendered page (SGL 302j sl16MapRA multi-page is
non-functional; 3 builds with it changed nothing). FIX = repack: shift the head into one page
(source read +16 tiles -> texels 128..384) and re-center the rotation on texel 256 -- works WITH
the single-page render. Gate tools/_portspike/qa_title_island_match.py GREEN (full island present
100% of settled frames, centered; HW-calibrated land floor); qa_title_sonic_intact.py GREEN (no
FG regression). Clouds restored on NBG1 bank B1 (still flicker from per-frame slScrAutoDisp cycle-
pattern churn -- next fix). Mountains + Title3DSprite 3D-rocks remain OFF (VDP1 slot capacity).

M3.1 AIZ INTRO-CUTSCENE DRIVER (2026-06-26, P6_AIZ_TEST flavor; gate qa_p6_aiz_cutscene.py
GREEN; UNCOMMITTED, GHZ/menu byte-identical). Registered the AIZ cutscene DRIVER + the placed
actors into the overlay (cart-resident; all edits #if defined(P6_AIZ_TEST), GHZ/menu unchanged):
AIZSetup (cid 30), CutsceneSeq (cid 34), AIZTornado (cid 32), AIZTornadoPath (cid 33), +
AIZKingClaw/AIZEggRobo/PhantomRuby/FXRuby (Decoration already in the GHZ batch). Compiled as
Game_*.o in build_p6scene_objs.sh (P6_AIZ_TEST block) -> OVL_FE (build_shipping.sh). MEASURED
entity-size gate: every PLACED AIZ entity <=344 B (engine narrow scene slot, sizeof(EntityBase)
Saturn); CutsceneSeq (476 B) spawns at SLOT_CUTSCENESEQ=15 (RESERVE/wide 556) -> fits.
CLOSURE: Camera_*/Music_*/Player_*/MathHelpers_GetBezierPoint resolve PACK via -R game.elf;
DrawHelpers_DrawCross needed a -u root (gc-dropped, AIZ-only caller); StarPost (pack NULL stub)
-> a zeroed ObjectStarPost instance (p6_closure_edge.c, #if P6_AIZ_TEST) so AIZTornado/Path
Create's StarPost->postIDs[0] reads 0 (fresh-boot, no crash); FXRuby_Create's non-Plus
RSDK.GetTintLookupTable() (REV02+MOD_LOADER=0 removed it) sed-stubbed (the PauseMenu.c:214
precedent). RESULT (witnessed, savestate): the AIZ cutscene STARTS (cutscene_state=0 == EnterAIZ
running) + the camera is CUTSCENE-DRIVEN -- cam_x=320 (the AIZSetup_CutsceneSonic_EnterAIZ clamp
camera->position.x = ScreenInfo->size.x<<16) vs the M3.0c leftover 10676. GHZ regression
qa_p6_ghz_regression.py R0-R16 GREEN; GHZ _end 0x060B5A40 (4.5KB under ANIMPAK 0x060B6C00),
unchanged. REMAINING GAP (M3.2 scope, precisely localized): the tornado fly-in does NOT animate
-- the live AIZTornadoPath set has 7 of 8 placed nodes and NO type-0 START node (pn_count=7,
has_START=0, first node x=640=slot7; the slot-6 x=128 START is absent from the live pool).
Without the START node nothing grabs SLOT_CAMERA1 / sets State_SetTornadoSpeed -> path moveVel.x=0
-> tornado pinned at spawn x=60 -> EnterAIZ holds the camera at 320 (state 0). So the camera is
framed at the intro position but the pan does not progress, and the screenshot still shows the
FG-Low ground as a uniform repeating fill (the present composites only FG-Low; the AIZ BG
parallax jungle/sky is not yet presented). NEXT: root-cause the missing slot-6 START node
(scene-entity instantiation, not the M3.1 registration) + present the AIZ BG layers, then the
later cutscene beats (M3.2 claw/PhantomRuby, M3.3 FXRuby warp-to-GHZ).
