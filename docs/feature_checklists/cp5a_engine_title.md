# CP5a — engine LoadScene + run the TITLE scene (mirror CP4 Logos)

Last revised: 2026-06-20

## Goal / gate (DO NOT MODIFY THE GATE)
`python tools/_portspike/qa_engine_title.py --mcs <cap>` GREEN, T1-T5:
- T1 `p6_w_frontend_folder_tag == 0x5469` ('T'<<8|'i' — Title folder active)
- T2 `p6_w_titlesetup_classid > 0`
- T3 `p6_w_titlelogo_classid  > 0`
- T4 `p6_w_title_objcount     > 0`  (Title Scene1.bin placements instantiated)
- T5 `p6_w_cont_frames        > 0`  (engine reached ENGINESTATE_REGULAR + ticks)

Witness names FIXED by the gate. CP5a must define exactly:
`_p6_w_titlesetup_classid`, `_p6_w_titlelogo_classid`, `_p6_w_title_objcount`.
(`_p6_w_frontend_folder_tag` + `_p6_w_cont_frames` already exist — shared.)

## Decomp ports (verbatim, already cached)
- `tools/_decomp_raw/SonicMania_Objects_Title_TitleSetup.c` (417 L) -> `Game_TitleSetup.o`
- `tools/_decomp_raw/SonicMania_Objects_Title_TitleLogo.c`  (343 L) -> `Game_TitleLogo.o`
Both link into the OVERLAY (cd/OVLRING.BIN), NOT the pack — same as CP4 Logos.

## Pre-flight (PROVEN offline before building)
- (a) `MANIA_USE_PLUS = (3 >= (5)) = FALSE`. Preprocessor-confirmed: the
  Plus-only fns (TitleSetup_HandleCheatInputs/CheckCheatCode/State_SetupPlusLogo,
  TitleLogo_State_*) are NOT emitted. Both .c preprocess CLEAN against the
  census `include/Title/` tree.
- (b) The census include tree `tools/_portspike/_p67d_sizing/include/Title/`
  ALREADY EXISTS (regenerated Jun 12 with the cached Title headers:
  TitleSetup.h/TitleLogo.h/TitleSonic.h/TitleBG.h/...). All.h already
  `#include "Title/..."`. No header pull / regen needed.
- (c) Title assets present in `extracted/Data/{Stages,Sprites}/Title/`
  (Scene1.bin, StageConfig.bin, Logo.bin, Electricity.bin) and the
  174 MB `cd/DATA.RSDK` is built from them. Title is a standard GameConfig scene.

## Link-closure (resolved)
- `Music_PlayTrack`/`Music_Stop` — REAL (Game_Music.o, map 0x06026738/0x060262e4). OK.
- `Localization_GetString` — REAL (Game_Localization.o). OK.
- `API_SetRichPresence` -> `APICallback_SetRichPresence` — already stubbed
  (p6_closure_edge.c:167). OK.
- `API_SetNoSave(s)` -> `globals->noSave = s` — pure macro. `globals` resolves. OK.
- `API_ClearPrerollErrors` -> `APICallback_ClearPrerollErrors` — NEW, stub in overlay.
- `TimeAttackData_Clear` — no .c cached -> stub in overlay.
- `Unknown_anyKeyPress` -> `UnknownInfo->anyKeyPress` (REV02 macro). `UnknownInfo`
  resolves (map 0x060b1314). Read only in State_WaitForEnter (>256 f), never at f90. OK.
- `TitleBG_SetupFX()` — TitleBG is CP5b -> stub in overlay (called only in FlashIn, >128 f).
- `TitleSonic` global — provide `ObjectTitleSonic *TitleSonic = NULL;` in overlay
  (foreach_all(TitleSonic) in FlashIn, never deref'd at f90).
- `TitleLogo` — REAL registered object (carries Scene1.bin placements -> T4).
- `sku_platform` -> `SKU->platform`; `SKU` resolves (p6_wave1_reg.c, map 0x060b1310).
  Reached only via TitleLogo_SetupPressStart (PRESSSTART Create), not at f90 anyway.

The 4 NEW overlay-resident closure symbols (ClearPrerollErrors, TimeAttackData_Clear,
TitleSonic, TitleBG_SetupFX) are referenced ONLY by the overlay's Game_TitleSetup.o,
so they live in p6_ovl_ghz.c under `#if defined(P6_FRONTEND_TITLE)` (flat-TU rule).

## Architecture (additive, flag-isolated)
New flag `P6_FRONTEND_TITLE`. When set, build scripts ALSO pass `-DP6_FRONTEND_LOGOS`
so every shared `#if defined(P6_FRONTEND_LOGOS)` machinery (VDP1 box, p6_vdp2_arm_
sprites_only, frontend_frame, SaturnSheet slot bump, render-diag witnesses) compiles
UNCHANGED. Do NOT widen the LOGOS guards. Title takes precedence in dispatch
(`#if defined(P6_FRONTEND_TITLE) ... #elif defined(P6_FRONTEND_LOGOS) ...`).

## Files touched
1. `tools/_portspike/_p6/p6_io_main.cpp`
   - Title witnesses (`p6_w_titlesetup_classid`/`p6_w_titlelogo_classid`/
     `p6_w_title_objcount`) — unconditional `__attribute__((used))` defs (CP4 pattern).
   - `p6_title_reload()` — copy of `p6_logos_reload` matching folder "Title", tag 0x5469.
   - Title objcount latch (mirror lines 4412-4421, latch on "Title").
   - boot dispatch (4938-4968): add `#if defined(P6_FRONTEND_TITLE)` branch calling
     `p6_title_reload()` / `p6_frontend_frame()` BEFORE the LOGOS branch.
2. `tools/_portspike/_p6/p6_ovl_ghz.c`
   - `#if defined(P6_FRONTEND_TITLE)`: extern TitleSetup/TitleLogo + the 3 witnesses;
     register_object_full(TitleSetup) + register_object_full(TitleLogo);
     the 4 closure defs (TitleSonic NULL, TitleBG_SetupFX/TimeAttackData_Clear/
     APICallback_ClearPrerollErrors stubs); witness writes in p6_ghz_ovl_witness.
3. `tools/_portspike/_p6/build_p6scene_objs.sh`
   - compile Game_TitleSetup.o + Game_TitleLogo.o (mirror the LOGOS [wfe] block).
   - thread `${P6_FRONTEND_TITLE:+-DP6_FRONTEND_TITLE -DP6_FRONTEND_LOGOS}` to
     p6_io_main.o, p6_ovl_ghz.o, p6_vdp2.o, SaturnSheet.o compiles.
   - `-u` roots: `_p6_w_titlesetup_classid -u _p6_w_titlelogo_classid -u _p6_w_title_objcount`.
4. `tools/_portspike/_p6/build_shipping.sh`
   - `P6_FRONTEND_TITLE` => export P6_FRONTEND_LOGOS too; OVL_FE += Game_TitleSetup.o
     Game_TitleLogo.o; thread to `make`; symbol-presence grep.
5. `Makefile`
   - `ifeq ($(P6_FRONTEND_TITLE),1) CCFLAGS += -DP6_FRONTEND_TITLE -DP6_FRONTEND_LOGOS`
     (reaches the jo-side p6_vdp1.c compile).

## Regression guard
All new code under `#if defined(P6_FRONTEND_TITLE)`; build only sets it for the
Title flavor. Default GHZ `_end == 0x060b6ba0` (< ANIMPAK 0x060b6c00) must hold.
Rebuild the DEFAULT GHZ build at the end so the on-disk artifact is unchanged GHZ.

## Capture
`pwsh tools/qa_savestate.ps1 -Cue game.cue -SaveFrame 90 -Out tools/_portspike/_engine_title.mcs`
then `python tools/_portspike/qa_engine_title.py --mcs ...`. Title is sprite-only
(light load); tune SaveFrame empirically (90 lands in State_Wait; bump to 120/160 if
T1-T4 are 0 = too early; lower if cont not increasing = advanced past Wait).
