# Feature checklist — M3.1 AIZ intro-cutscene DRIVER registration

Status: planning -> implement. Last revised 2026-06-26.

## Goal (bounded)

Register the AIZ intro-cutscene DRIVER objects into the overlay so the AIZ
scene positions its OWN camera (cutscene-driven), making the AIZ background
render correctly framed instead of the leftover-`screens[0].position.x=10676`
repeating-fill (the M3.0c MEASURED bug). The later cutscene beats (claw grab,
PhantomRuby flash, FXRuby warp-to-GHZ) are M3.2/M3.3.

EVERY edit gated `#if defined(P6_AIZ_TEST)` (P6_AIZ_TEST implies P6_FRONTEND_MENU)
or compiles out when undefined, so GHZ (no flag) + menu (P6_FRONTEND_MENU only)
builds stay byte-identical.

## Decomp source (authoritative, ported VERBATIM)

| Object | Decomp file | Role for M3.1 camera framing |
|---|---|---|
| AIZSetup     | `tools/_decomp_raw/SonicMania_Objects_AIZ_AIZSetup.{c,h}`        | StageLoad sets `Zone->cameraBoundsB` (:123); StaticUpdate (:16) runs SetupObjects + GetCutsceneSetupPtr -> `CutsceneSeq_StartSequence` (:336); cutscene state `EnterAIZ` (:360-361) clamps `camera->position.x`. |
| CutsceneSeq  | `tools/_decomp_raw/SonicMania_Objects_Cutscene_CutsceneSeq.{c,h}` (+ CutsceneRules.h for the header type only) | The sequencer; LateUpdate (:21) drives the cutscene state machine, switching `SceneInfo->entity` to AIZSetup so the cutscene-state fns' `RSDK_THIS(AIZSetup)` resolve. NON-PLUS: Update is empty (no skip). |
| AIZTornado     | `tools/_decomp_raw/SonicMania_Objects_AIZ_AIZTornado.{c,h}`     | The biplane. Create (:63-66) arms `state=AIZTornado_State_Move` when `!StarPost->postIDs[0]` (fresh boot). HandleMovement drives `self->newPos` from `AIZTornadoPath->moveVel`. |
| AIZTornadoPath | `tools/_decomp_raw/SonicMania_Objects_AIZ_AIZTornadoPath.{c,h}` | **THE CAMERA DRIVER.** START node Create (:29-47): grabs SLOT_CAMERA1, sets `camera->state=StateMachine_None` + `camera->position = node->position` (:35-36), sets `ScreenInfo->position.y` (:43). State_SetTornadoSpeed -> HandleMoveSpeed (:97-137) writes `camera->position` along the path each frame. |

Placed-actor classes registered so AIZSetup_SetupObjects' `foreach_all` /
`CREATE_ENTITY` resolve the real entities (instead of inert classID-0 blanks):

| Object | Decomp file | Placements (scene_census AIZ/Scene1.bin) | Asset in DATA.RSDK |
|---|---|---|---|
| AIZKingClaw | `..._AIZ_AIZKingClaw.{c,h}` | 1 (x~11120, late-beat) | `aiz/claw.bin` PRESENT |
| AIZEggRobo  | `..._AIZ_AIZEggRobo.{c,h}` | 10 (x~11120) | `aiz/aizeggrobo.bin` PRESENT |
| Decoration  | `..._Common_Decoration.{c,h}` | 3 | `aiz/decoration.bin` PRESENT |
| PhantomRuby | `..._ERZ_PhantomRuby.{c,h}` | 1 (x~11120) | `global/phantomruby.bin` ABSENT -> StageLoad LoadSpriteAnimation returns -1 (graceful); draws nothing; only used in M3.2/M3.3 beats |
| FXRuby      | `..._Cutscene_FXRuby.{c,h}` | 0 (CREATE_ENTITY in late beats) | `global/fxruby.bin` ABSENT -> graceful; ObjectFXRuby static-vars 2,060 B in DATASET_STG (166 KB pool, ample) |

(Platform + Animals + InvisibleBlock already exist in the GHZ pack.)

## MEASURED entity-size gate (#228/.bss-overflow class) — ALL PASS

Scene-placed entities are written by the engine DIRECTLY into the NARROW scene
slot (`sizeof(EntityBase)` engine = **344 B**, Object.hpp OBJECT_DATA_COUNT=0x40;
NOT via ResetEntitySlot, so NO front-end wide-scene routing for placed entities --
Scene.cpp:659/676 `RSDK_ENTITY_AT(slotID+RESERVE)`). Game-side EntityX must be
<= 344 B. Measured via SH-2 toolchain probe against the census Game.h:

| Placed EntityX | size (B) | <= 344? |
|---|---|---|
| EntityAIZTornado     | 252 | YES |
| EntityAIZTornadoPath | 136 | YES |
| EntityAIZKingClaw    | 340 | YES (4 B spare) |
| EntityAIZEggRobo     | 208 | YES |
| EntityPhantomRuby    | 172 | YES |
| EntityFXRuby         | 148 | YES |
| EntityDecoration     | 140 | YES |

CutsceneSeq entity = **476 B > 344** but is NOT placed -- it is created via
`ResetEntitySlot(SLOT_CUTSCENESEQ=15, ...)`. Slot 15 < RESERVE_ENTITY_COUNT(64)
=> RESERVE region => WIDE 556-B stride => 476 fits. AIZSetup entity = 88 B (but
AIZSetup is a setup object, 0 placements; its static instance runs StaticUpdate
like GHZSetup). Registration refusal threshold (front-end P6_MAX_ENTITY_SIZE=592):
all classes <= 592, so all register.

## Closure (symbols the AIZ TUs reference; resolution)

- `Music_SetMusicTrack/TransitionTrack/FadeOut` -> Game_Music.o (PACK, -R game.elf)
- `Camera_SetupLerp/ShakeScreen/State_FollowXY` -> Game_Camera.o (PACK, -R)
- `Player_State_Ground/Static/Action_Jump/Input_P1/Input_P2_AI/HandleSidekickRespawn`,
  `Player->targetLeaderPosition` -> Game_Player.o (PACK, -R)
- `MathHelpers_GetBezierPoint` -> Game_MathHelpers.o (PACK, -R)
- `Animals->animalTypes[]` -> overlay Animals (intra-overlay)
- `StarPost->postIDs[0]` -> Game_StarPost.o ... **NOT currently a pack member** ->
  verify; if absent, the StarPost static must resolve (AIZTornado/Path Create deref it).
- `PhantomRuby_SetupFlash/PlaySfx`, `FXRuby_State_IncreaseStageDeform`,
  `AIZKingClaw_State_Grab` -> intra-overlay (their TUs registered).
- `CutsceneHBH->paletteColors[]` -> Plus-only (`#if MANIA_USE_PLUS`), compiled out.
- `BGSwitch->switchCallback[]` -> Plus-only AIZSetup block, compiled out; BGSwitch is
  pack-resident anyway.

## Files to create / modify

1. `tools/_portspike/_p6/p6_ovl_ghz.c` — add the 9 AIZ `register_object_full` calls
   inside `#if defined(P6_AIZ_TEST)` (extern the AIZ object globals first); add a
   per-tick cutscene-state witness writing `p6_w_aiz_cutscene_state`.
2. `tools/_portspike/_p6/build_p6scene_objs.sh` — compile the AIZ/Cutscene `Game_*.o`
   in an `if [ -n "${P6_AIZ_TEST:-}" ]` block (mirror the menu block); add `-u`-roots
   for the new witnesses.
3. `tools/_portspike/_p6/build_shipping.sh` — add the AIZ `Game_*.o` to `OVL_FE` in an
   `if [ -n "${P6_AIZ_TEST:-}" ]` block; add map-presence trip-wires.
4. `tools/_portspike/_p6/p6_io_main.cpp` — define the new witnesses
   `p6_w_aiz_cutscene_state` + `p6_w_aiz_cam_x` (gated P6_AIZ_TEST), -u-root them.
5. `tools/_portspike/qa_p6_aiz_cutscene.py` — the M3.1 RED gate (C1/C2/C3 + screenshot).

## Visual gate threshold

- C1 `p6_w_aiz_cutscene_state` advances > 0 after capture (CutsceneSeq instantiated +
  its LateUpdate ran the state machine; the AIZSetup cutscene started).
- C2 `p6_w_aiz_scrx` is NO LONGER the leftover 10676 (camera became cutscene-driven;
  expect a small intro-region x, the path START node region ~128px or the clamp
  `ScreenInfo->size.x << 16` region).
- C3 an AIZSetup + CutsceneSeq classID witness > 0 (registered + instantiated).
- Screenshot: the AIZ biplane-intro region frames correctly (jungle BG, not the
  repeating fill). Proxy-trap guard: a GREEN gate with a black/wrong screen is NOT done.

## PROACTIVE-DETECTION-CHECKLIST (§4.5.1)

- Audit 1 (Z-order): N/A for M3.1 — no NEW draw call lands in Game.c; the AIZ FG
  present (M3.0b) is unchanged. Object draw groups are the decomp's own (Zone->
  objectDrawGroup), composited by the existing ProcessObjectDrawLists path.
- Audit 2 (anim cadence): the AIZTornado propeller/flame animators advance via
  RSDK.ProcessAnimation (the engine's verbatim per-frame walker) — decomp-exact, no
  hand-rolled cadence.
- Audit 3 (pivot+flip): N/A — no parent/child flipped composite added in M3.1.
- Audit 4 (boot-delay): the AIZ anims (AIZTornado/Claw/AIZEggRobo/Decoration .bin,
  each a few KB) load in the existing AIZ scene-load path (already ~50 s emulated CD).
  No NEW synchronous boot asset beyond the scene's own StageLoads. STG pool 166 KB.

## #228 WRAM-H invariant

Object CODE goes in the overlay (cart-resident at 0x02690000, 128 KB window ending
~0x226B0000, ~485 KB gap above the GHZ1 layout high-water). The AIZ-test `_end`
is already above ANIMPAK but boots fine ONLY because no anim pack loads at
0x060B6C00 (GHZANIM.PAK is `#if !P6_FRONTEND_TITLE`, compiled out). AIZ anims load
to CART (DATASET_STG, P6_CART_TMP). Watch `_end` vs P6_HW_ANIMPAK in game.map.
