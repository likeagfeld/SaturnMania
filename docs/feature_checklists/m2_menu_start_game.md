# M2 -- menu navigation + start-game -> AIZ SetScene (Cutscenes/Angel Island Zone)

Checkpoint: navigate the Mania main menu, select Mania Mode -> a new save slot ->
RSDK.SetScene("Cutscenes","Angel Island Zone") fires (the biplane intro). The AIZ
scene render is the NEXT checkpoint (M3); this checkpoint's GREEN is the SetScene
firing for AIZ, proven by a savestate witness.

Flag-guard: ALL changes behind `#if defined(P6_FRONTEND_MENU)` / `${P6_FRONTEND_MENU:+}`.
Default GHZ + Title byte-identical.

## Decomp spine (authoritative, non-Plus MANIA_USE_PLUS=0)

- `MenuSetup.c:507`  foreach_all(UIModeButton){ actionCB = MenuSetup_MenuButton_ActionCB }
- `MenuSetup.c:508`  foreach_all(UISaveSlot){ actionCB = MenuSetup_SaveSlot_ActionCB }
- `MenuSetup.c:921-934` MenuSetup_MenuButton_ActionCB case 0 (Mania Mode):
    MenuSetup->saveSelect->buttonID = 7; UIControl_MatchMenuTag("Save Select").
- `UIModeButton.c:189-218` UIModeButton_SelectedCB -> UITransition_StartTransition(self->actionCB, 14)
    (the mode-button confirm runs actionCB only THROUGH the transition).
- `UITransition.c:53-68` UITransition_StartTransition: uses UITransition->activeTransition
    (a placed entity) + derefs UIDialog->activeDialog (:57). Runs callback in
    UITransition_State_TransitionOut (:288-298) after the ~30-frame transition.
- `UISaveSlot.c:871-941` UISaveSlot_ProcessButtonCB: UIControl->anyConfirmPress -> UISaveSlot_SelectedCB.
- `UISaveSlot.c:943-983` UISaveSlot_SelectedCB: state=UISaveSlot_State_Selected.
- `UISaveSlot.c:1228-1290` UISaveSlot_State_Selected: timer++, fxRadius grows; at
    fxRadius>0x200 runs self->actionCB (= MenuSetup_SaveSlot_ActionCB) (:1283-1285).
- `MenuSetup.c:1046-1132` MenuSetup_SaveSlot_ActionCB: globals->gameMode=MODE_MANIA;
    isNewSave seeds saveRAM (saveState=1,zoneID=0,lives=3); SaveGame_SaveFile;
    :1116-1121 if isNewSave -> RSDK.SetScene("Cutscenes","Angel Island Zone").
- `Scene.cpp:1530-1553` RSDK::SetScene: sets sceneInfo.activeCategory + sceneInfo.listPos
    by md5(category) + md5(scene) match.

## Target (GameConfig.bin, verified): category "Cutscenes" (idx 6), scene
"Angel Island Zone", FOLDER "AIZ". So after the select fires,
sceneInfo.listData[sceneInfo.listPos].folder == "AIZ" -> tag 'A'<<8|'I' = 0x4149.

## Input path (already wired, verified)
- ProcessInput() runs in p6_frontend_frame:5851 -> Saturn SMPC device (InputDevice_Saturn.cpp)
  fills controller[CONT_ANY]; the engine assigns it to controller[CONT_P1].
- UIControl_ProcessInputs (UIControl.c:187-342) reads ControllerInfo[CONT_P1+i] for arrows,
  ControllerInfo-> (CONT_ANY) for any*Press; confirm = keyStart.press | keyA.press.
- Mednafen Saturn standard pad (.mednafen.cfg ss.input.port1.gamepad): A=SDL4, B=SDL22,
  DOWN=SDL81, UP=SDL82, START=SDL40(Enter). The harness injects PC make codes SDL maps:
  START=Enter make 0x1C; DOWN=Down-arrow make 0x50+EXT; A is the 'a' key (SDL usage 4)
  make 0x1E.

## POOL FIX (Task #296) -- FRONT-END uniform-wide entity pool (the last start-game blocker)

ROOT CAUSE (compile-measured): EntityUISaveSlot = 588 B. The GHZ dual-stride pool
(Object.hpp Saturn branch) has NARROW scene slots = sizeof(EntityBase) = 344 and WIDE
(reserve/temp) = 556. TWO refusals fire:
  - RegisterObject (Object.cpp:86)  `if (entityClassSize > ENTITY_WIDE_SIZE) return;`  588>556 -> classID=0.
  - ResetEntitySlot (Object.cpp:1509) `if (entityClassSize > sizeof(EntityBase) && scene slot) refuse`  588>344.
The 10 UISaveSlots are Menu/Scene1.bin placements (scene slots) -> both refusals -> S1 RED.

FIX (front-end-ONLY, `#if defined(P6_FRONTEND_MENU)`): a UNIFORM-WIDE pool, every slot
ENTITY_WIDE_SIZE = 592 (>= 588, even alignment). Scene region SHRINKS to 384 (0x180; covers
the 299 Menu placements + headroom) so the pool is SMALLER than the GHZ 445 KB:
  RESERVE 64 + SCENE 384 + TEMP 64 = 512 slots * 592 = 303,104 B (0x4A000).
  P6_LW_ENTITYLIST 0x243000 -> ends 0x28D000, under LAYOUTBANDS 0x2AFC00 (GHZ-only, the menu
  never loads it -- gated `#if !defined(P6_FRONTEND_TITLE)`) AND under the menu-USED
  DATASTORAGE 0x2BC400 (193,536 B headroom). NO WRAM-L overlap.
UNIFORM => P6_POOL_NARROW_STRIDE (the scene-region stride) == ENTITY_WIDE_SIZE -> the
dual-stride accessor (SaturnEntityAt/SaturnEntitySlot + the 3 `_ep` raw walks in Object.cpp
ProcessObjects + p6_i2_direct) collapses to base+slot*592, no narrow region to refuse 588.
ResetEntitySlot's scene-region refusal is gated OUT for the front-end (no narrow slot exists).
RegisterObject auto-passes (588 <= 592).

Files (all front-end-gated; GHZ/Title BYTE-IDENTICAL, P6_POOL_NARROW_STRIDE expands to
sizeof(EntityBase) for them -> textual no-op):
  - rsdkv5-src/.../Scene/Object.hpp -- P6_FRONTEND_MENU pool-count block + P6_POOL_NARROW_STRIDE
    macro; accessor uses it; P6_FE_* aliases for the offline gate.
  - rsdkv5-src/.../Scene/Object.cpp -- gate OUT the ResetEntitySlot scene refusal; the 3 raw
    `_ep`/`_shep`/`_cep` ProcessObjects stride walks use P6_POOL_NARROW_STRIDE.
  - tools/_portspike/_p6/p6_io_main.cpp -- p6_i2_direct uses P6_POOL_NARROW_STRIDE (oracle match).

RED gate (OFFLINE, zero binary cost): tools/_portspike/qa_p6_i3.py --frontend -- parses the
live P6_FE_* + the WRAM-L map, asserts (A) NARROW==WIDE uniform, (B) 588<=592, (C) pool end
<= DATASTORAGE. RED before the fix (P6_FE_* absent), GREEN after. Default (no flag) stays
GREEN + byte-identical (1216 slots, 445,440 B).

## AUDIT 1 -- Menu/Scene1.bin placement (tools/_portspike/_parse_menu_scene.py output)
- objectCount=41. UISaveSlot PRESENT x10 (8 regular + No-Save variants); UITransition x1;
  UIModeButton x4; UIDialog x0 (NOT placed -> NULL class ptr, MUST be a valid zeroed obj
  for UITransition's UIDialog->activeDialog deref). "Save Select" UIControl @ world(512,120)
  slot 24. UIModeButton rows are the 4 main-menu mode buttons.
- => Port UISaveSlot + UITransition; give UIDialog a real zeroed instance (activeDialog==NULL).

## AUDIT 2 -- Animation cadence: N/A (no NEW animated walker added; UISaveSlot/UITransition
  draw via the engine ProcessAnimation already running). UISaveSlot_State_Selected uses a
  frame-counted fxRadius (timer-based), driven by the engine tick -- no Saturn walker.

## AUDIT 3 -- Pivot+flip composite: N/A (no NEW parent+child flipped sprite pair added;
  UISaveSlot draw uses the engine DrawSprite path already validated for the menu rows).

## AUDIT 4 -- Boot-delay budget: UISaveSlot_StageLoad loads "UI/SaveSelect.bin"
  (SCOPE_STAGE) via the engine pack (already-mounted Data.rsdk windowed GFS) -- NOT a new
  synchronous cd/*.SHT add. The 10 placements + UITransition are entity instantiation
  (in-RAM, the existing InitObjects chain), ~0 boot delay. SaveSelect sheet is small
  (UI/SaveSelect.gif); its surface is bound on demand by the existing arm path. No new
  cd/ asset on the synchronous boot path.

## src/rsdk APIs required -- ALL present
- Input: ProcessInput + controller[] (Input.cpp) wired; AssignInputSlotToDevice/
  GetFilteredInputDeviceID/ResetInputSlotAssignments registered (Link.cpp:478-490).
- Closure: SaveGame_GetDataPtr/SaveFile/ClearCollectedSpecialRings, UIWaitSpinner_*,
  UIDialog_* , PhantomRuby_PlaySfx, APICallback_GetConfirmButtonFlip -- all in
  p6_closure_edge.c. UIWidgets draw fns in Game_UIWidgets.o. Music_Stop/Localization
  pack -R. UIDialog NULL -> replaced with a real zeroed instance (this port).

## Files to modify
- tools/_portspike/_p6/build_p6scene_objs.sh -- compile Game_UISaveSlot.o + Game_UITransition.o
  in the Menu wfm loop; -u-root the new menu start witnesses.
- tools/_portspike/_p6/build_shipping.sh -- add the 2 .o to OVL_FE; symbol-presence grep.
- tools/_portspike/_p6/p6_ovl_ghz.c -- register UISaveSlot + UITransition; UIDialog real-instance
  init (extend p6_menu_apic_init); S3 start-game witness extern.
- tools/_portspike/_p6/p6_menu_closure.c -- DELETE the UISaveSlot NULL stub (real TU lands);
  add a real zeroed ObjectUIDialog instance + wire UIDialog to it in p6_menu_apic_init.
- tools/_portspike/_p6/p6_io_main.cpp -- in p6_frontend_frame ENGINESTATE_LOAD branch (menu):
  latch S3 witness p6_w_menu_startscene_tag = folder-tag of sceneInfo.listData[listPos] +
  p6_w_menu_start_cat = sceneInfo.activeCategory, when the start-game SetScene fires.
  Add S1 (UISaveSlot classID) + S2 (UIControl active buttonID moved / confirm seen) witnesses.

## RED gate (tools/_portspike/qa_engine_menu_start.py)
- S1 p6_w_menu_saveslot_classid > 0   -- UISaveSlot registered + instantiated.
- S2 p6_w_menu_input_seen != 0        -- input reached the menu (a confirm/nav press
     observed by the live UIControl during the injected sequence).
- S3 p6_w_menu_startscene_tag == 0x4149 ('AI') AND p6_w_menu_start_cat == Cutscenes idx
     -- the start-game fired SetScene for AIZ.
- RED on current build: witnesses ABSENT (no UISaveSlot, no start-game). GREEN after the
  port + an injected select sequence (Enter to confirm Mania Mode -> arrows/A to pick a
  slot -> A/Enter to start) reaches the AIZ SetScene. Capture deep (Wait>=34) AFTER the
  injected sequence + SCREENSHOT the save-select screen.

## NON-goals (explicit)
- Do NOT port the AIZ biplane scene (M3). GREEN = the SetScene firing for AIZ.
- Do NOT enable DrawFace/plates (slPutPolygon front-end crash). UISaveSlot draws via
  DrawSprite (VDP1) -- fine.
