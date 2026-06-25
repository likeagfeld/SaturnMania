# Feature checklist: Saturn-native 320 main-menu layout + row plates (M2b + M3)

Flag: `P6_FRONTEND_MENU` (front-end menu flavor only). Default GHZ/Title byte-identical.

## Decomp sources (authoritative)
- `tools/_decomp_raw/SonicMania_Objects_Menu_UIModeButton.c:36-76` — `UIModeButton_Draw`
  draws shadow+icon (lines 45-57) + TWO `UIWidgets_DrawParallelogram(drawPos.x,drawPos.y,
  128,24,24,...)` plates (lines 64,69) + text (line 74). drawGroup=2 (line 84). Rows have
  NO position-setting code -> placed by Scene1.bin.
- `tools/_decomp_raw/SonicMania_Objects_Menu_UIControl.c:48-54` — `UIControl_Draw` sets
  `ScreenInfo->position = FROM_FIXED(activeControl->position) - ScreenInfo->center`. THE
  world->screen scroll origin.
- `tools/_decomp_raw/SonicMania_Objects_Menu_UIWidgets.c:202-245` — `UIWidgets_Draw-
  Parallelogram`: builds 4 screen-relative verts (subtract `ScreenInfo->position<<16`,
  :233-242), `if(SceneInfo->inEditor) DrawLine*4 else RSDK.DrawFace(verts,4,r,g,b,0xFF,
  INK_NONE)` (:244).
- `tools/_decomp_raw/SonicMania_Objects_Menu_MenuSetup.c:89-120` (non-Plus) — finds "Main
  Menu" UIControl, `MenuSetup_InitAPI()` gate -> `MenuSetup_Initialize()`. `SetVideo-
  Setting(SCREENCOUNT,1)` (:205) -> full 320x224 screen -> center (160,112).
- Build is `-DGAME_VERSION=3` = VER_103 -> `MANIA_USE_PLUS=0` -> `MainMenu.c`/`ManiaMode-
  Menu.c` ENTIRELY compiled out; MenuSetup.c drives directly.

## MEASURED RED baseline (current build game.iso @ 09:15, qa_menu_layout.py)
- `currentScreen->position = (0,0)` -> screen=world -> rows render at world coords.
- active control world (852,376), tag "Main Menu" (correct). decomp origin would be
  (852-160, 376-112) = (692,264).
- rows world: Mania(756,358) TimeAttack(948,358) Competition(756,420) [Options(948,420)].
- L1 RED visctrls=2 (an inactive control leaking). L2 RED (all 3 off-screen at scrx=0).
- ROOT CAUSE: `p6_w_menu_force_scrx/scry` computed by the overlay but NEVER written back to
  `currentScreen->position` (the pack force-set claimed in the comment was never coded).

## Sprite sizing (parse_spr; sets the 320-native column/row centres)
- Icon MainIcons anim0: w[88..120] h[38..44], pivot.x ~= -w/2 (centred on position).
- Label TextEN anim1 "Main Menu": w[86..148] h22, pivot.x ~= -w/2 (centred). Widest =
  148px ("Competition" f2). A row centred at screen X spans ~[X-74, X+74].
- => to fit [0,320] a row centre X must be in [74,246].
- Decomp transform lands cols at x=64 / x=256 -> 148px label clips ~10px each side.

## Saturn-native 320 layout (decomp-INSPIRED; the user-chosen divergence)
Keep the decomp STRUCTURE: 2x2 grid, order Mania(0,top-left)/TimeAttack(1,top-right)/
Competition(2,bottom-left)/Options(3,bottom-right), selected-row-prominent via the
EXISTING UIModeButton bounce offsets (untouched). Only re-derive the BASE row centres so
the widest (148px) label fits 320:
  - columns: x=80 (left), x=240 (right)  [decomp 64/256 pulled inward 16px so 148px
    label [6,154] / [166,314] both inside [0,320]].
  - rows:    y=92 (top), y=150 (bottom)  [decomp 94/156; icon h44 top y-44, fits >=0].
Per-button world position = target_screen + forced_origin(692,264), keyed on the decomp
`buttonID`:
  - bid0 Mania      -> screen(80,92)  -> world(772,356)
  - bid1 TimeAttack -> screen(240,92) -> world(932,356)
  - bid2 Competition-> screen(80,150) -> world(772,414)
  - bid3 Options    -> screen(240,150)-> world(932,414)
Applied in the pack each frame (after ProcessObjects, before ProcessObjectDrawLists) by
`p6_menu_apply_layout()`: (1) force `currentScreen->position = forced_origin` (fixes the
(0,0) bug, decomp UIControl_Draw:52-53), (2) override each live UIModeButton entity's base
`position` by buttonID to the world target above. drawGroup/bounce/anim untouched.

## DrawFace=0 diagnosis (M3) -- ROOT CAUSE FOUND (this session, build-config measurement)
The prior note's "(b) ruled out: the slot is the forwarder" was WRONG -- the symbol
RSDK::DrawFace @0x06038f60 exists whether its BODY is the forwarder OR the `#else` empty
stub; the address proves nothing about which body linked.

MEASURED ROOT CAUSE: `build_p6scene_objs.sh:251` compiles `p6_pack_stubs.o` with only
`$CXXFLAGS $ENG_DEFS $CORE_INC` -- NO `-DP6_FRONTEND_MENU`. So in p6_pack_stubs.cpp the
`#if defined(P6_FRONTEND_MENU)` forwarding `DrawFace` (:65-73, calls p6_drawface_saturn) is
FALSE and the `#else` EMPTY `DrawFace(){}` (:75) is what compiles + links into the pack.
The empty stub is a valid non-null no-op -> `RSDK.DrawFace` calls it -> NO crash, NO plate,
`p6_w_drawface_calls` stays 0. `RSDK.DrawSprite` works because it is a REAL engine TU, not a
flag-gated stub. This is the inverse of the memory rule "a pack flag does not reach the
overlay TU": here a MENU flag does not reach the PACK-stub TU.

Confirming data (reliable BSS witnesses, _menu_layout.mcs @f90):
  - p6_w_drawface_calls = 0 ; p6_w_menu_vdp1_landed = 13810 (UIModeButton_Draw DOES run its
    DrawSprite calls -- so it reaches the interleaved parallelogram lines too).
  - WRAM-H pointer peeks (RSDK struct @0x060b8820) read all-zero incl. DrawSprite which
    demonstrably works -> the high-WRAM peek is unreliable here (mcs pair-swap, task #136);
    DO NOT diagnose from those. The build-config grep is the authoritative evidence.

FIX: add `${P6_FRONTEND_MENU:+-DP6_FRONTEND_MENU}` to the p6_pack_stubs.o compile line
(build_p6scene_objs.sh:251). Then the forwarder links -> RSDK.DrawFace -> p6_drawface_saturn.
The layout fix ALSO matters: even once DrawFace fires, the plates draw at the row position,
so they must be on-screen (the same scroll-origin fix). Re-measure p6_w_drawface_calls (>0)
AND a plate-pixel coverage after both land.

## Gates (RED first)
- `tools/_portspike/qa_menu_layout.py` (EXTEND): replace the 2x2-grid asserts with the
  Saturn-native 320 fit asserts: every named row bbox (centre +/- 74 label half-width)
  FULLY in [0,320]x[0,224]; rows form the expected 2x2 cascade order (Mania top-left,
  TimeAttack top-right, Competition/Options bottom); selected row at its prominent
  (bounce) position. RED on the scrx=0 clip build, GREEN when all fit + ordered.
- M3 plate gate (NEW, qa_menu_plates.py): `p6_w_drawface_calls > 0` AND a plate-pixel
  measure (the row plate colour quad covers each row bbox). RED now (calls=0), GREEN when
  plates render.
- KEEP GREEN (no regression): qa_engine_menu_render.py M6/M6b/M7; qa_menu_icon_clean.py.

## PROACTIVE-DETECTION audits
- Audit 1 (Z-order): plates Z=455 (behind rows Z=450, above backdrop Z=460) -- already in
  p6_drawface_saturn. UIModeButton drawGroup 2; UIBackground drawGroup 0 (backdrop first).
  Consistent.
- Audit 2 (cadence): static menu (no per-frame anim walk needed for layout). N/A.
- Audit 3 (pivot/flip): no flipped rows. Label/icon both centred (pivot ~= -w/2). The
  320-native centre is chosen FROM the measured 148px max label half-width (74). Documented.
- Audit 4 (boot budget): no new asset loads (override is pure position math). 0 KB added.

## WRAM-H budget
`_end` must stay < 0x060C0000 (front-end SGL floor). The override + force-origin are a few
ints + one small pack fn; the layout DIAGNOSTIC witnesses (modebtn_sx/sy/vis_*) get TRIMMED
after GREEN to recover headroom toward 0x060b7800.
