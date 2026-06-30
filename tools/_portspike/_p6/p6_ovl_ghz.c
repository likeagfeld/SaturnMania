// =============================================================================
// p6_ovl_ghz.c -- P6.8 O1 (Task #210/#254): the GHZ zone-overlay ENTRY TU.
//
// Extends the proven P6.7d.3 Ring overlay to a MULTI-CLASS entry. O1 moves the
// resident GHZ objects out of the pack INTO the chain-loaded overlay (so pack
// _end stops being the code wall), registering them via the api thunks (flat-TU
// rule: the overlay names no C++ engine symbols -- pointers only) and writing the
// ld -R-imported main witnesses (the Ring p6_w_obj_* pattern).
//   step 1 (db9dfe4): Ring + Spring.
//   step 2 (this):     + Bridge + PlaneSwitch (their p6_brg/loop witnesses MIGRATE
//                      here too -- the resident pack can no longer name the
//                      Bridge/PlaneSwitch globals).
//
// Compiled -x c with the census Game.h/GameLink.h tree (Spring/Bridge/PlaneSwitch
// verbatim globals + callbacks) and WITH -ffunction-sections so ovl_ring.ld places
// .text.p6_overlay_entry FIRST at the window base (the main calls that constant).
// =============================================================================
#include "Game.h"          /* Object*/Entity* + globals + X_* callbacks + foreach_all */
#include "p6_ovl_api.h"

/* Ring is now the VERBATIM Game_Ring (Global/Ring.c) compiled into the overlay --
 * ObjectRing *Ring + Ring_Create/StageLoad/State_Normal/Draw arrive via Game.h
 * (same as Spring/Bridge). The flat p6_ring2 harness is RETIRED (#W18 ring-arm). */

/* Witnesses -- DEFINED in p6_io_main.cpp (main image; gates read them from
 * game.map); written here via the ld -R import. */
extern int32 p6_w_ring_aniframes, p6_w_ring_classid;
extern int32 p6_w_spring_classid, p6_w_spring_frames;
extern int32 p6_w_brg_classid, p6_w_brg_count, p6_w_brg_posx, p6_w_brg_posy,
             p6_w_brg_onscreen, p6_w_brg_frames;
extern int32 p6_w_loop_regmask, p6_w_loop_pscount;
extern int32 p6_w_spikelog_classid, p6_w_spikelog_frames;
/* RANGE-INDEPENDENT anim-load status read straight off the Object struct
 * (<Obj>->aniFrames = LoadSpriteAnimation() result; (int16)-cast so 0xFFFF
 * reads as -1 == load FAILED). Definitive per-object gate signal -- does not
 * depend on a live entity being near the camera. */
extern int32 p6_w_spikelog_aniframes, p6_w_spring_aniframes, p6_w_brg_aniframes;
extern int32 p6_w_spikes_aniframes;
extern int32 p6_w_b1_registered; /* mass-port Batch 1: count of the 4 clean objects with classID>0 */
extern ObjectDecoration *Decoration;
extern ObjectForceSpin *ForceSpin;
extern ObjectSpinBooster *SpinBooster;
/* ForceUnstick deferred to Batch 3 -- its StageLoad loads the 69-frame
 * Global/ItemBox.bin which overflows DATASET_STG by ~1.3KB here (MEASURED:
 * pool 153600, at_fail 152376). It shares ItemBox's anim, so it ports for free
 * alongside the Monitor. */

/* MASS-PORT BATCH 2 (badnik break chain, 2026-06-18). The 9 objects: the 3 CHAIN
 * TUs (Explosion/Animals registered for live classIDs -- BadnikHelpers_BadnikBreak-
 * Unseeded derefs Explosion->sfxDestroy + Animals->animalTypes[]; BadnikHelpers
 * itself is a registered no-op helper class, matching the decomp object list) and
 * the 6 GHZ1 BADNIKs. ALL 9 are OVERLAY-resident (this link): Animals references the
 * overlay's Bridge_HandleCollisions so it cannot live in the pack. The pack->overlay
 * edge (Game_Player.o calls BadnikHelpers_BadnikBreakUnseeded) is bridged by a
 * p6_closure_edge forward stub routed through api->badnikbreak_unseeded_fn below. */
/* All 9 are defined by their overlay-resident TUs (Game_BadnikHelpers/Explosion/
 * Animals/<6 badniks>.o, in this overlay link); Game.h declares the externs. */
extern ObjectBadnikHelpers *BadnikHelpers;
extern ObjectExplosion *Explosion;
extern ObjectAnimals *Animals;
extern ObjectNewtron *Newtron;
extern ObjectCrabmeat *Crabmeat;
extern ObjectBuzzBomber *BuzzBomber;
extern ObjectChopper *Chopper;
extern ObjectMotobug *Motobug;
extern ObjectBatbrain *Batbrain;
/* CP4 FRONT-END KEYSTONE (Task #265/#266): the Logos splash objects. Registered
 * only in the -DP6_FRONTEND_LOGOS overlay flavor (their Game_*.o are linked into
 * the overlay only in that build) -- guarded so the default GHZ overlay does not
 * reference them. LogoSetup_StageLoad loads "Logos/Logos.bin" (the 4-logo sheet)
 * + spawns itself at slot 0; UIPicture draws each logo via DrawSprite->VDP1. */
#if defined(P6_FRONTEND_LOGOS)
extern ObjectLogoSetup *LogoSetup;
extern ObjectUIPicture *UIPicture;
extern int32 p6_w_logosetup_classid, p6_w_uipicture_classid; /* pack witnesses, ld -R */
extern int32 p6_w_uipicture_aniframes, p6_w_uipicture_framesNN; /* CP4b render diag */
/* CP4c BLUE-SCREEN diag: the first live UIPicture entity's full draw-chain state
 * (mirrors the BD_SCAN badnik witness). Written below in the witness fn. */
extern int32 p6_w_uipic_drawgrp, p6_w_uipic_active, p6_w_uipic_visible,
             p6_w_uipic_onscreen, p6_w_uipic_posx, p6_w_uipic_posy,
             p6_w_uipic_animid, p6_w_uipic_frameid, p6_w_uipic_sheetid,
             p6_w_uipic_handle;
#endif
#if defined(P6_FRONTEND_TITLE)
/* CP5a FRONT-END link 2 (Task #267): the Title scene objects. Registered only in
 * the -DP6_FRONTEND_TITLE overlay flavor (their Game_TitleSetup.o/Game_TitleLogo.o
 * link into the overlay only in that build). TitleSetup self-places at slot 0 +
 * drives the State machine; TitleLogo carries the Scene1.bin logo-piece placements
 * (the T4 objcount evidence). Their classID witnesses are pack globals (ld -R). */
extern ObjectTitleSetup *TitleSetup;
extern ObjectTitleLogo  *TitleLogo;
extern int32 p6_w_titlesetup_classid, p6_w_titlelogo_classid, p6_w_title_objcount;
/* CP5b.3/4 (Task #272): TitleBG/Title3DSprite classID + visible-count witnesses
 * (pack globals, ld -R import; DEFINED in p6_io_main). */
extern int32 p6_w_titlebg_classid, p6_w_title3d_classid;
extern int32 p6_w_titlebg_vis, p6_w_title3d_vis;
/* CP5b.1 (Task #268) RENDER diag: the first live TitleLogo entity's draw-chain state
 * (mirrors the CP4c p6_w_uipic_* witnesses). handle = the resolved frame's surface ->
 * p6_vdp1_handle_for_surface; >=0 == Title/Logo.gif bound == the logo blit CAN land. */
extern int32 p6_w_tlogo_drawgrp, p6_w_tlogo_visible, p6_w_tlogo_onscreen,
             p6_w_tlogo_type, p6_w_tlogo_sheetid, p6_w_tlogo_handle, p6_w_tlogo_landed;
extern int32 p6_w_tlogo_existmask, p6_w_tlogo_vismask, p6_w_tlogo_onscrmask,
             p6_w_tlogo_boundmask, p6_w_tsetup_statetag;
extern int p6_w_vdp1_landed; /* global VDP1 landed-blit counter (p6_vdp1.c) */
/* CP5b.2 (Task #269): TitleSonic is now a REGISTERED, overlay-resident object (its
 * verbatim Game_TitleSonic.o links into this overlay), no longer a NULL closure stub.
 * Its global is provided by Game_TitleSonic.o (Game.h declares the extern). The
 * render-diag witnesses (the live head's draw-chain state) are DEFINED in p6_io_main
 * (main image, ld -R import). handle = the resolved frame's surface ->
 * p6_vdp1_handle_for_surface; >=0 == Title/Sonic.gif bound == the head blit CAN land. */
extern ObjectTitleSonic *TitleSonic;
extern int32 p6_w_tsonic_visible, p6_w_tsonic_onscreen, p6_w_tsonic_sheetid,
             p6_w_tsonic_handle, p6_w_tsonic_animid, p6_w_tsonic_frameid;
/* CLOSURE EDGE (overlay-resident, flat-TU rule): symbols TitleSetup.c references
 * that no other linked TU provides.
 *   - TitleBG_SetupFX: called in TitleSetup_State_FlashIn. CP5b.4 (Task #272):
 *     the TitleBG + Title3DSprite sprites are now registered + rendered BY DEFAULT
 *     in the Title flavor (the verbatim Game_TitleBG.o/Game_Title3DSprite.o link
 *     into the overlay), so the REAL TitleBG_SetupFX runs. It was made Saturn-safe
 *     in the verbatim decomp (SonicMania_Objects_Title_TitleBG.c): the per-frame
 *     scanline-callback INSTALL is #if-guarded OUT on the Saturn/Title build (no
 *     Saturn scanline consumer -- the VDP2 backdrop is driven natively by
 *     p6_vdp2_present_title_backdrop), which was the MEASURED destabilizer (its
 *     SetClipBounds corrupted the FG sprite clip -> "alternating island/logo
 *     frames; head vanishes"). The visibility flips + palette setup are KEPT.
 *     The P6_TITLEBG_SPRITES_OFF knob restores the no-op stub (A/B bisect path).
 *   - TimeAttackData_Clear: TitleSetup_StageLoad (no .c cached); a clear of save
 *     tables that are unused on Saturn -> inert no-op is correct.
 *   - APICallback_ClearPrerollErrors: TitleSetup_StageLoad (API_ClearPrerollErrors
 *     macro -> this under !PLUS/REV02); the removed-at-REV02 preroll-error surface,
 *     a no-op on Saturn. */
#if defined(P6_TITLEBG_SPRITES_OFF)
void TitleBG_SetupFX(void) {}
#else
extern ObjectTitleBG       *TitleBG;
extern ObjectTitle3DSprite *Title3DSprite;
#endif
void TimeAttackData_Clear(void) {}
void APICallback_ClearPrerollErrors(void) {}
/* TitleSetup_State_WaitForEnter (reached only on a button-press, ~256+ frames in --
 * never at the CP5a f90 capture) calls API_ResetInputSlotAssignments ->
 * APICallback_ResetControllerAssignments (the !PLUS/REV02 macro arm, APICallback.h:69).
 * No .c cached -> inert stub. The sibling AssignControllerID/MostRecentActiveControllerID
 * macros it also uses are already stubbed in p6_closure_edge.c. */
void APICallback_ResetControllerAssignments(void) {}
#endif
#if defined(P6_FRONTEND_MENU)
/* M1 FRONT-END link 3 (qa_engine_menu.py): the MENU scene's min UI set. Registered
 * only in the -DP6_FRONTEND_MENU overlay flavor (their Game_*.o link into the
 * overlay only in that build, alongside p6_menu_closure.o which provides the
 * 68-symbol link cone of the unregistered UI/API classes). MenuSetup is the live
 * non-Plus director; UIControl carries the menu rows (the M3 classID + M4 objcount
 * evidence); UIBackground draws the animated backdrop; UIButton/UIWidgets/
 * UISubHeading/UIButtonPrompt are the placed-and-renderable widgets. Their object
 * globals are provided by their own verbatim TUs (Game.h declares the externs).
 * The classID witnesses are pack globals (ld -R import). */
extern ObjectMenuSetup      *MenuSetup;
extern ObjectUIControl      *UIControl;
extern ObjectUIBackground   *UIBackground;
extern ObjectUIButton       *UIButton;
extern ObjectUIWidgets      *UIWidgets;
extern ObjectUISubHeading   *UISubHeading;
extern ObjectUIButtonPrompt *UIButtonPrompt;
extern int32 p6_w_menusetup_classid, p6_w_uicontrol_classid, p6_w_menu_objcount;
// M1b: UIModeButton is the 4 MAIN-MENU rows (Mania Mode/Time Attack/Competition/
// Options). Its verbatim Game_UIModeButton.o links into the overlay (OVL_FE) only in
// the Menu flavor; the global is provided by that .o (Game.h declares the extern). The
// render witnesses are pack globals (ld -R import, DEFINED in p6_io_main.cpp):
//   p6_w_menu_treebuilt       = (MenuSetup->mainMenu != NULL) (M6, the row tree built)
//   p6_w_menu_modebtn_classid = UIModeButton->classID         (M6b, the rows registered)
// p6_menu_apic_init is the AUTH-GATE FLIP (defined in p6_menu_closure.c, this overlay),
// exported to the pack via api->menu_apic_init_fn so it runs at the top of the first
// frontend frame (before MenuSetup's first StaticUpdate -> InitAPI).
extern ObjectUIModeButton   *UIModeButton;
extern int32 p6_w_menu_treebuilt, p6_w_menu_modebtn_classid;
extern int32 p6_w_menu_force_scrx, p6_w_menu_force_scry; /* pack witnesses, -R import */
void p6_menu_apic_init(void);
/* M2 (qa_engine_menu_start.py): the start-game path. UISaveSlot is the save-select
 * slot widget (the start-game trigger); UITransition is the mode-button -> Save-Select
 * transition (UIModeButton_SelectedCB -> UITransition_StartTransition runs the actionCB
 * through the transition). Both verbatim Game_*.o link into the overlay (OVL_FE) in the
 * Menu flavor; their globals come from those TUs. The S1/S2 witnesses are pack globals
 * (ld -R import, DEFINED in p6_io_main.cpp):
 *   p6_w_menu_saveslot_classid = UISaveSlot->classID (S1, the slot widget is registered)
 *   p6_w_menu_input_seen       = sticky OR of UIControl->any{Confirm,Down,Up,Left,Right}Press
 *                                (S2, a Saturn-pad press reached the live UIControl). */
extern ObjectUISaveSlot     *UISaveSlot;
extern ObjectUITransition   *UITransition;
extern int32 p6_w_menu_saveslot_classid, p6_w_menu_input_seen;
extern int32 p6_w_uitrans_present, p6_w_uitrans_state, p6_w_uitrans_timer, p6_w_uitrans_istrans, p6_w_uitrans_active;
extern int32 p6_w_active_btn_actioncb, p6_w_active_btn_id, p6_w_active_btn_count;
extern int32 p6_w_ctrl_posx, p6_w_ctrl_tgtx;
extern int32 p6_w_ctrl_state, p6_w_ctrl_active, p6_w_nosave_pi, p6_w_nosave_confirm;
extern int32 p6_w_nosave_gate, p6_w_slot_state, p6_w_slot_fxradius;
extern int32 p6_w_gate_seldisabled, p6_w_ctrl_seldisabled;
extern void UIControl_ProcessInputs(void);  /* S3: cmp uc->state -- intra-overlay symbol */
#if defined(P6_MENU_AUTOSELECT)
extern void UIControl_MatchMenuTag(const char *text); /* #296 debug-inject: activate No-Save */
extern void UISaveSlot_State_Selected(void);          /* #296 debug-inject: force the select state */
extern int32 p6_w_as_stage;                           /* #296 debug-inject progress witness */
#endif

/* M2b/M3 (Task #294/#295): SATURN-NATIVE 320 MENU LAYOUT, applied by the pack each frame
 * (api->menu_layout_fn) after ProcessObjects + before ProcessObjectDrawLists.
 *
 * USER DECISION: Saturn-native 320 layout. Mania's 4 main-menu rows (UIModeButton:
 * Mania/TimeAttack/Competition/Options) are authored as a 2x2 grid at world px
 * (756,358)(948,358)(756,420)(948,420) for a 424x240 screen -> the right column lands at
 * screen x~256 with ~148px labels spilling past x=320 on Saturn's 320x224 (MEASURED:
 * qa_menu_layout.py, _menuok_4.png). Keep the decomp STRUCTURE (the 2x2 mode grid, order
 * Mania top-left / TimeAttack top-right / Competition bot-left / Options bot-right, the
 * SELECTED row pulled prominent via the EXISTING UIModeButton bounce offsets, untouched)
 * but re-derive the row positions to fit 320 cleanly.
 *
 * (1) ORIGIN: the active "Main Menu" UIControl sits at world (852,376); the decomp
 *     world->screen transform (UIControl_Draw, UIControl.c:52-53) is
 *     screen = world - (FROM_FIXED(ctrl->position) - center), center (160,112) for the
 *     full 320x224 screen (MenuSetup SetVideoSetting(SCREENCOUNT,1)) -> origin
 *     (852-160, 376-112) = (692,264). Write that into p6_w_menu_force_scrx/scry; the pack
 *     forces currentScreen->position to it (the missing M2b consumption -- the decomp's own
 *     UIControl_Draw write was clobbered to (0,0) by a 2nd leaking control later in the frame).
 * (2) ROW POSITIONS (Saturn-native 320 fit): columns at screen x {80,240}, rows at screen
 *     y {92,150}. The widest label is 148px (TextEN "Competition") centred on the row -> a
 *     row at x80 spans [6,154], at x240 spans [166,314]: both fully inside [0,320]. (The
 *     raw decomp 64/256 columns would clip a 148px label ~10px each side.) world = screen +
 *     origin(692,264), keyed on the decomp buttonID enum (UIModeButton.h: MANIA=0,
 *     TIMEATTACK=1, COMPETITION=2, OPTIONS=3):
 *       bid0 Mania       screen(80,92)  -> world(772,356)
 *       bid1 TimeAttack  screen(240,92) -> world(932,356)
 *       bid2 Competition screen(80,150) -> world(772,414)
 *       bid3 Options     screen(240,150)-> world(932,414)
 *     Override mb->position (FIXED) each frame; drawGroup/bounce/anim/selection untouched,
 *     so the selected-row prominence (UIModeButton_State_HandleButtonEnter) is preserved.
 * MENU flavor only (the whole block is #if'd); GHZ/Logos/Title overlay is byte-identical. */
static void p6_menu_apply_layout(void)
{
    /* Saturn-native 320-fit world targets, indexed by buttonID 0..3. */
    static const int32 s_row_wx[4] = { 772, 932, 772, 932 };
    static const int32 s_row_wy[4] = { 356, 356, 414, 414 };

    if (UIModeButton && UIModeButton->classID) {
        foreach_all(UIModeButton, mb) {
            int32 bid = mb->buttonID;
            if (bid >= 0 && bid < 4) {
                mb->position.x = TO_FIXED(s_row_wx[bid]);
                mb->position.y = TO_FIXED(s_row_wy[bid]);
            }
        }
    }
    /* Origin from the ACTIVE "Main Menu" control (decomp UIControl_Draw transform). */
    if (UIControl && UIControl->classID) {
        foreach_all(UIControl, c) {
            if (c->active == ACTIVE_ALWAYS) {
                p6_w_menu_force_scrx = (int32)(c->position.x >> 16) - 160;
                p6_w_menu_force_scry = (int32)(c->position.y >> 16) - 112;
                foreach_break;
            }
        }
    }
}
#endif
#if defined(P6_AIZ_TEST)
/* M3.1 (qa_p6_aiz_cutscene): the AIZ intro-cutscene DRIVER objects, registered into the
 * overlay only in the P6_AIZ_TEST flavor (their verbatim Game_*.o link into OVL_FE only in
 * that build). AIZSetup is the setup director (StaticUpdate runs SetupObjects +
 * GetCutsceneSetupPtr -> CutsceneSeq_StartSequence); CutsceneSeq is the sequencer (LateUpdate
 * drives the state machine); AIZTornado is the biplane + AIZTornadoPath is the CAMERA driver
 * (its START node grabs SLOT_CAMERA1 + writes camera->position along the path). The placed
 * actors AIZKingClaw/AIZEggRobo/PhantomRuby/FXRuby are registered so AIZSetup_SetupObjects'
 * foreach_all/CREATE_ENTITY resolve the real entities (Decoration is ALREADY registered in
 * the GHZ batch above -- its StageLoad handles the AIZ folder). Their object globals are
 * overlay-resident (provided by the Game_*.o); Game.h declares the externs. The classID
 * witnesses are pack globals the overlay writes via ld -R import. MEASURED entity sizes all
 * <= 344 (placed -> narrow scene slot); CutsceneSeq (476) is created at SLOT_CUTSCENESEQ=15
 * (reserve/wide 556) so it fits (feature checklist aiz_m3_1). */
extern ObjectAIZSetup      *AIZSetup;
extern ObjectCutsceneSeq   *CutsceneSeq;
extern ObjectAIZTornado    *AIZTornado;
extern ObjectAIZTornadoPath *AIZTornadoPath;
extern ObjectAIZKingClaw   *AIZKingClaw;
extern ObjectAIZEggRobo    *AIZEggRobo;
extern ObjectPhantomRuby   *PhantomRuby;
extern ObjectFXRuby        *FXRuby;
/* the M3.1 witnesses are pack globals (DEFINED in p6_io_main.cpp), -R import */
extern int32 p6_w_aiz_cutscene_state, p6_w_aiz_setup_classid, p6_w_aiz_seq_classid;
extern int32 p6_w_aiz_tornado_classid, p6_w_aiz_path_classid, p6_w_aiz_cam_x;
/* SLOT_CAMERA1 = 60 (GameVariables.h); the overlay reads the live camera through the
 * engine RSDK.GetEntity to witness the cutscene-driven camera x (C2 corroboration). */
#define P6_AIZ_SLOT_CAMERA1 (60)
#define P6_AIZ_SLOT_CUTSCENESEQ (15)
#endif
extern int32 p6_w_b2_registered;       /* count of the 9 chain+badnik objs with classID>0 */
extern int32 p6_w_explosion_aniframes; /* Explosion->aniFrames (load-status latch) */
extern int32 p6_w_animals_aniframes;   /* Animals->aniFrames */
extern int32 p6_w_newtron_aniframes;   /* Newtron->aniFrames */

/* BADNIK-VIS diag (2026-06-18): the overlay names the badnik globals, so the live
 * draw-state scan lives here. Witnesses DEFINED in p6_io_main.cpp (main image). The
 * handle accessor is also there (the pack static table). bd_* latch the first live
 * badnik entity each frame: pos/onScreen/visible/drawGroup/active + animator.frames
 * (NULL?) + the current frame's sheetID + the resolved VDP1 handle for it. */
extern int32 p6_w_bd_found, p6_w_bd_classid, p6_w_bd_posx, p6_w_bd_posy,
             p6_w_bd_onscreen, p6_w_bd_visible, p6_w_bd_drawgrp, p6_w_bd_active,
             p6_w_bd_framesNN, p6_w_bd_animid, p6_w_bd_frameid, p6_w_bd_sheetid,
             p6_w_bd_handle, p6_w_bd_drawn;
extern int32 p6_vdp1_handle_for_surface(int32 sheetID);

/* Forward decl so p6_overlay_entry is the FIRST function (window base). */
static void p6_ghz_ovl_witness(const void *ringSlot);
#if defined(P6_GHZCUT_BOOT)
/* Task #309 Tier-B.1: forward-declared so p6_overlay_entry (which sets api->fade_fn
 * = p6_ghzcut_fade_fn) stays the FIRST function; defined after the witness below. */
static void p6_ghzcut_fade_fn(int *outWhite, int *outBlack);
/* Task #309 Tier-B.2: the CutsceneHBH Draw SHIM -- selects this Heavy's CRAM
 * palette block (jo colno) before the verbatim decomp CutsceneHBH_Draw, resets
 * after. Registered as the CutsceneHBH Draw callback (instead of the raw decomp
 * Draw) so the verbatim port stays untouched. */
static void p6_cuthbh_draw(void);
/* Engine globals (defined in p6_vdp1.c / p6_io_main.cpp; ld -R cross-link). */
extern int   p6_heavy_palblock;          /* per-draw VDP1 palette block (p6_vdp1.c) */
extern int32 p6_w_hbh_aniframes, p6_w_hbh_landed; /* Tier-B.2 witnesses (p6_io_main) */
extern int32 p6_w_hbh_count, p6_w_hbh_vis, p6_w_hbh_posy, p6_w_hbh_posx,
             p6_w_hbh_handle, p6_w_hbh_camy, p6_w_hbh_animid; /* Tier-B.2 DIAG */
extern ObjectCutsceneHBH *CutsceneHBH;
#endif

/* I3b 2b: the camera-local-pool MATERIALIZE, overlay-resident (new engine code -> cart per the
 * residency rule, freeing WRAM-H for the shrink manager). Forward-declared so p6_overlay_entry stays
 * first; defined at file end. The ENGINE-touching ops are thin extern "C" PACK thunks (the overlay
 * can't name C++-mangled engine syms -- flat-TU rule; ld -R game.elf resolves these). The overlay
 * does the DORM navigation + LE var-replay (raw offset writes -- no engine types). */
static void p6_ovl_materialize(unsigned logical_slot, unsigned dest_slot);
extern int32 p6_eng_classid_resolve(const unsigned char *objhash_le); /* -> classID (0=unregistered) */
extern void  p6_eng_serialize_begin(int32 classID);                   /* rebuild editableVarList (cart scratch) */
extern int32 p6_eng_var_offset(const unsigned char *varhash_le);      /* var-hash -> field offset (-1=none) */
extern void  p6_eng_serialize_end(void);                              /* restore editableVarList */
extern void *p6_eng_entity_prepare(int32 slot);                       /* RSDK_ENTITY_AT(slot) + memset */
extern void  p6_eng_write_placement(void *ent, int32 classID, int32 px, int32 py);
/* the materialize witnesses are PACK globals (extern "C"); the overlay writes them via ld -R game.elf */
extern int32 p6_w_mat_slot, p6_w_mat_classid, p6_w_mat_nvars, p6_w_mat_nmatch;
extern int32 p6_w_mat_posx, p6_w_mat_posy, p6_w_mat_v0, p6_w_mat_v1, p6_w_mat_v2, p6_w_mat_v3;
/* I3b 2b COMPACTION (overlay-resident per the residency rule -- the pack-placed version overflowed
 * WRAM-H/ANIMPAK by 80 B). Relocates every populated scene entity into a dense physical pool via the
 * non-identity remap (byte-plan proven offline by qa_p6_pool_compact_model). Pure byte-math with the
 * pool GEOMETRY from the pack (p6_eng_pool_geom -- no struct/define hardcode); the only engine-touching
 * op is the atomic scene_phys flip (p6_eng_pool_flip). Forward-declared so p6_overlay_entry stays first. */
static void p6_ovl_pool_compact(void);
extern void p6_eng_pool_geom(int32 *out);              /* {classID off, NARROW, R, SCN, TEMP, WIDE, SCENE_PHYS} */
extern void p6_eng_pool_flip(int32 sphys, int32 dummy);/* atomic flip of the RSDK pool ints (LAST) */
extern unsigned char p6_scan_near[];                   /* WRAM-H near bitfield (pack; per-frame near-set) */
extern int32 p6_w_compact_n, p6_w_compact_sphys, p6_w_compact_dummy;
extern int32 p6_w_compact_bij_ok, p6_w_compact_lastL, p6_w_compact_lastP;
/* I3b 2b STREAMING (per-frame manager). */
static void p6_ovl_stream(void);
extern void p6_eng_create(int32 slot);                 /* re-Create a materialized entity (InitObjects mirror) */
extern int32 p6_w_stream_mat, p6_w_stream_dorm, p6_w_stream_free, p6_w_stream_resident, p6_w_stream_starve;
extern int32 p6_w_bt_logical, p6_w_bt_cid, p6_w_bt_life, p6_w_bt_reappear; /* I3b 2b backtrack-proof witnesses */
extern int32 p6_w_pool_inv_bad; /* I3b 2b: sticky free-list-invariant violation latch (resident+free!=SP-1) */
/* cart structures -- the verified-free [0x226BC980,0x226C0000) gap (inv-end -> shadow buffer). */
#define P6_STREAM_FREELIST 0x226BD000u  /* unsigned short[SCENE_PHYS] -- free physical-slot stack */
#define P6_STREAM_FREECNT  0x226BD600u  /* int -- free-list count */
#define P6_STREAM_LIFE     0x226BD700u  /* unsigned char[(SCENEENTITY_COUNT+7)/8] -- destroyed (lifecycle) bits */
/* I3b 2b PERF #2 (scan narrowing): the resident-list -- logical slots currently holding a physical slot.
   The per-frame DORMANT/RETIRE pass iterates THIS ~42-entry list; the MATERIALIZE pass byte-scans the
   existing WRAM-H p6_scan_near bitfield (zero new pack code -> no WRAM-H growth, CART-only). Verified-free
   hole 0x226BDE00 (after the 136-B life bitfield), u16[SCENE_PHYS=640]=1280 B + count, < shadow 0x226C0000. */
#define P6_STREAM_RESIDLIST  0x226BDE00u  /* unsigned short[SCENE_PHYS] -- resident logical slots */
#define P6_STREAM_RESIDCNT   0x226BE300u  /* int -- resident-list count */

// =============================================================================
// p6_overlay_entry -- MUST be first (window base). Registers the overlay's
// classes through the api thunks; the engine LoadGameConfig hash loop matches
// each by md5(name), so classID assignment is order-independent (P6.7d.3).
// =============================================================================
int p6_overlay_entry(p6_ovl_api *api)
{
    /* Ring -- verbatim FULL-callback form (the real Game_Ring): Ring_StageLoad loads
     * Global/Ring.bin into DATASET_STG + Ring_Create arms each placed ring's animator
     * (ACTIVE_BOUNDS, Ring_State_Normal) so rings RENDER (#W18 ring-arm gap). */
    api->register_object_full((void **)&Ring, "Ring",
                              (unsigned)sizeof(EntityRing), (unsigned)sizeof(ObjectRing),
                              Ring_Update, Ring_LateUpdate, Ring_StaticUpdate,
                              Ring_Draw, Ring_Create, Ring_StageLoad, Ring_Serialize);

    /* Spring / Bridge / PlaneSwitch -- verbatim FULL-callback form (NULL editor
     * matches the resident RSDK_REGISTER_OBJECT REV02/non-REV0U arm, GameLink.h
     * :1799: these objects define no _EditorLoad/_EditorDraw). */
    api->register_object_full((void **)&Spring, "Spring",
                              (unsigned)sizeof(EntitySpring), (unsigned)sizeof(ObjectSpring),
                              Spring_Update, Spring_LateUpdate, Spring_StaticUpdate,
                              Spring_Draw, Spring_Create, Spring_StageLoad, Spring_Serialize);
    api->register_object_full((void **)&Bridge, "Bridge",
                              (unsigned)sizeof(EntityBridge), (unsigned)sizeof(ObjectBridge),
                              Bridge_Update, Bridge_LateUpdate, Bridge_StaticUpdate,
                              Bridge_Draw, Bridge_Create, Bridge_StageLoad, Bridge_Serialize);
    api->register_object_full((void **)&PlaneSwitch, "PlaneSwitch",
                              (unsigned)sizeof(EntityPlaneSwitch), (unsigned)sizeof(ObjectPlaneSwitch),
                              PlaneSwitch_Update, PlaneSwitch_LateUpdate, PlaneSwitch_StaticUpdate,
                              PlaneSwitch_Draw, PlaneSwitch_Create, PlaneSwitch_StageLoad,
                              PlaneSwitch_Serialize);
    /* O3 step 1: SpikeLog (the rolling GHZ spike log, 1023 B -- fits the existing
     * window, no re-budget). Shares GHZ/Objects.gif (GHZOBJ.SHT, already staged). */
    api->register_object_full((void **)&SpikeLog, "SpikeLog",
                              (unsigned)sizeof(EntitySpikeLog), (unsigned)sizeof(ObjectSpikeLog),
                              SpikeLog_Update, SpikeLog_LateUpdate, SpikeLog_StaticUpdate,
                              SpikeLog_Draw, SpikeLog_Create, SpikeLog_StageLoad, SpikeLog_Serialize);
    /* Spikes (Global hazard, 41 GHZ1 placements): verbatim Game_Spikes. Closure
     * fully resolved -- Ice/Shield/MathHelpers/Player are ported TUs; Press is the
     * lone GHZ1-dead NULL (p6_closure_edge.c). Loads Global/Spikes.bin + the Global
     * Spikes sheet (census = staged). No editor callbacks (GAME_INCLUDE_EDITOR off). */
    api->register_object_full((void **)&Spikes, "Spikes",
                              (unsigned)sizeof(EntitySpikes), (unsigned)sizeof(ObjectSpikes),
                              Spikes_Update, Spikes_LateUpdate, Spikes_StaticUpdate,
                              Spikes_Draw, Spikes_Create, Spikes_StageLoad, Spikes_Serialize);
#if defined(P6_FRONTEND_LOGOS)
    /* CP4: the Logos splash objects. Register order is irrelevant (the engine
     * LoadGameConfig matches by md5(name)); these resolve their classIDs the same
     * registration-time way Spring/Ring do. NULL editor callbacks (GAME_INCLUDE_
     * EDITOR off) -- matches the verbatim RSDK_REGISTER_OBJECT non-editor arm. */
    api->register_object_full((void **)&LogoSetup, "LogoSetup",
                              (unsigned)sizeof(EntityLogoSetup), (unsigned)sizeof(ObjectLogoSetup),
                              LogoSetup_Update, LogoSetup_LateUpdate, LogoSetup_StaticUpdate,
                              LogoSetup_Draw, LogoSetup_Create, LogoSetup_StageLoad, LogoSetup_Serialize);
    api->register_object_full((void **)&UIPicture, "UIPicture",
                              (unsigned)sizeof(EntityUIPicture), (unsigned)sizeof(ObjectUIPicture),
                              UIPicture_Update, UIPicture_LateUpdate, UIPicture_StaticUpdate,
                              UIPicture_Draw, UIPicture_Create, UIPicture_StageLoad, UIPicture_Serialize);
#endif
#if defined(P6_FRONTEND_TITLE)
    /* CP5a: the Title scene objects. Register order is irrelevant (the engine
     * LoadGameConfig matches by md5(name)); these resolve their classIDs the same
     * registration-time way Ring/LogoSetup do. NULL editor callbacks (matches the
     * verbatim RSDK_REGISTER_OBJECT non-editor arm). TitleSetup_StageLoad runs
     * RSDK.ResetEntitySlot(0,...) to self-place at slot 0 + loads Title/Electricity.bin;
     * TitleLogo_StageLoad loads Title/Logo.bin and TitleLogo carries the placements. */
    api->register_object_full((void **)&TitleSetup, "TitleSetup",
                              (unsigned)sizeof(EntityTitleSetup), (unsigned)sizeof(ObjectTitleSetup),
                              TitleSetup_Update, TitleSetup_LateUpdate, TitleSetup_StaticUpdate,
                              TitleSetup_Draw, TitleSetup_Create, TitleSetup_StageLoad, TitleSetup_Serialize);
    api->register_object_full((void **)&TitleLogo, "TitleLogo",
                              (unsigned)sizeof(EntityTitleLogo), (unsigned)sizeof(ObjectTitleLogo),
                              TitleLogo_Update, TitleLogo_LateUpdate, TitleLogo_StaticUpdate,
                              TitleLogo_Draw, TitleLogo_Create, TitleLogo_StageLoad, TitleLogo_Serialize);
    /* CP5b.2 (Task #269): TitleSonic -- the ring-center head + finger-wave. Verbatim
     * decomp (SonicMania_Objects_Title_TitleSonic.c). TitleSonic_StageLoad loads
     * Title/Sonic.bin (sheet Title/Sonic.gif, staged as TSONIC.SHT); Create sets the
     * head/finger animators + visible=false (TitleSetup_State_FlashIn flips it visible).
     * NULL editor callbacks (the verbatim RSDK_REGISTER_OBJECT non-editor arm). */
    api->register_object_full((void **)&TitleSonic, "TitleSonic",
                              (unsigned)sizeof(EntityTitleSonic), (unsigned)sizeof(ObjectTitleSonic),
                              TitleSonic_Update, TitleSonic_LateUpdate, TitleSonic_StaticUpdate,
                              TitleSonic_Draw, TitleSonic_Create, TitleSonic_StageLoad, TitleSonic_Serialize);
    /* CP5b.3 (Task #272): TitleBG (the mountains/water/wing-shine sprites + the
     * island/cloud scanline FX) + Title3DSprite (the perspective billboards:
     * MountainL/M/S, Tree, Bush). Verbatim decomp (SonicMania_Objects_Title_
     * TitleBG.c / Title3DSprite.c). Both StageLoad load Title/Background.bin (sheet
     * Title/BG.gif, staged as TBG.SHT) so their DrawSprite blits resolve a banded
     * slot. TitleBG_SetupFX (called from TitleSetup_State_FlashIn) flips them
     * visible + installs the cloud/island scanlineCallbacks + SetPaletteMask.
     *
     * CP5b.4 (Task #272): NOW REGISTERED BY DEFAULT in the Title flavor. The
     * MEASURED destabilizer was the per-frame Scanline_Island/_Clouds INSTALL in
     * TitleBG_SetupFX (their SetClipBounds corrupted the Saturn FG sprite clip ->
     * "alternating island/logo frames; head vanishes"). That install is now
     * #if-guarded OUT on the Saturn/Title build (verbatim TitleBG.c SetupFX,
     * P6_FRONTEND_TITLE) -- the deform tables have NO Saturn consumer (the VDP2
     * sky+cloud+island BACKDROP is driven natively by p6_vdp2_present_title_
     * backdrop). SetupFX's visibility flips + palette setup are KEPT, so the 9
     * TitleBG + 58 Title3DSprite entities (parse_title_entities.py) flip visible +
     * blit via the proven DrawSprite path. The P6_TITLEBG_SPRITES_OFF knob restores
     * the gated-off state (A/B bisect). Part-4b (live Mode-7 island rotation) feeds
     * the deform affine into VDP2 RBG0+KTBL -- a separate follow-on. */
#if !defined(P6_TITLEBG_SPRITES_OFF)
    api->register_object_full((void **)&TitleBG, "TitleBG",
                              (unsigned)sizeof(EntityTitleBG), (unsigned)sizeof(ObjectTitleBG),
                              TitleBG_Update, TitleBG_LateUpdate, TitleBG_StaticUpdate,
                              TitleBG_Draw, TitleBG_Create, TitleBG_StageLoad, TitleBG_Serialize);
    // CP5b.4 (Task #272) MEASURED ISOLATION: registering Title3DSprite (the 58
    // perspective billboards) CORRUPTS the foreground -- the Sonic head + logo
    // emblem scramble to the bottom of the screen (MEASURED A/B: with ONLY TitleBG
    // registered the FG renders PERFECTLY, sonic_peach 10763 == the gated-off
    // baseline; adding Title3DSprite drops sonic_peach to 0 and scatters the FG
    // pieces). TitleBG ALONE delivers the stable Part-4a win (Sonic + full logo +
    // the distant mountains/reflection/water-sparkle). Title3DSprite is DEFERRED
    // behind P6_TITLE3D_ON until its FG-corruption root cause is found (its
    // orbiting Draw -- islandSize*relativePos/depth -- or its 58-sprite draw-path
    // interaction). The island billboards were CONFIRMED to render (a rich island
    // with mountains/trees) when enabled, but only at the cost of the FG, so they
    // stay off by default. This is the honest, no-regression default.
#if defined(P6_TITLE3D_ON)
    api->register_object_full((void **)&Title3DSprite, "Title3DSprite",
                              (unsigned)sizeof(EntityTitle3DSprite), (unsigned)sizeof(ObjectTitle3DSprite),
                              Title3DSprite_Update, Title3DSprite_LateUpdate, Title3DSprite_StaticUpdate,
                              Title3DSprite_Draw, Title3DSprite_Create, Title3DSprite_StageLoad, Title3DSprite_Serialize);
#endif
#endif /* !P6_TITLEBG_SPRITES_OFF */
#endif
#if defined(P6_FRONTEND_MENU)
    /* M1: the MENU scene's min UI set. Register order is irrelevant (the engine
     * LoadGameConfig matches by md5(name)); these resolve their classIDs the same
     * registration-time way Ring/TitleSetup do. NULL editor callbacks (matches the
     * verbatim RSDK_REGISTER_OBJECT non-editor arm, GAME_INCLUDE_EDITOR off).
     * MenuSetup_StageLoad stops music + clears the save slot + arms FXFade;
     * UIControl/UIButton/UIBackground/UIWidgets/UISubHeading/UIButtonPrompt Create +
     * StageLoad run inside the generic InitObjects chain to instantiate the Menu
     * Scene1.bin placements (the M4 objcount evidence). */
    api->register_object_full((void **)&MenuSetup, "MenuSetup",
                              (unsigned)sizeof(EntityMenuSetup), (unsigned)sizeof(ObjectMenuSetup),
                              MenuSetup_Update, MenuSetup_LateUpdate, MenuSetup_StaticUpdate,
                              MenuSetup_Draw, MenuSetup_Create, MenuSetup_StageLoad, MenuSetup_Serialize);
    api->register_object_full((void **)&UIControl, "UIControl",
                              (unsigned)sizeof(EntityUIControl), (unsigned)sizeof(ObjectUIControl),
                              UIControl_Update, UIControl_LateUpdate, UIControl_StaticUpdate,
                              UIControl_Draw, UIControl_Create, UIControl_StageLoad, UIControl_Serialize);
    api->register_object_full((void **)&UIBackground, "UIBackground",
                              (unsigned)sizeof(EntityUIBackground), (unsigned)sizeof(ObjectUIBackground),
                              UIBackground_Update, UIBackground_LateUpdate, UIBackground_StaticUpdate,
                              UIBackground_Draw, UIBackground_Create, UIBackground_StageLoad, UIBackground_Serialize);
    api->register_object_full((void **)&UIButton, "UIButton",
                              (unsigned)sizeof(EntityUIButton), (unsigned)sizeof(ObjectUIButton),
                              UIButton_Update, UIButton_LateUpdate, UIButton_StaticUpdate,
                              UIButton_Draw, UIButton_Create, UIButton_StageLoad, UIButton_Serialize);
    api->register_object_full((void **)&UIWidgets, "UIWidgets",
                              (unsigned)sizeof(EntityUIWidgets), (unsigned)sizeof(ObjectUIWidgets),
                              UIWidgets_Update, UIWidgets_LateUpdate, UIWidgets_StaticUpdate,
                              UIWidgets_Draw, UIWidgets_Create, UIWidgets_StageLoad, UIWidgets_Serialize);
    api->register_object_full((void **)&UISubHeading, "UISubHeading",
                              (unsigned)sizeof(EntityUISubHeading), (unsigned)sizeof(ObjectUISubHeading),
                              UISubHeading_Update, UISubHeading_LateUpdate, UISubHeading_StaticUpdate,
                              UISubHeading_Draw, UISubHeading_Create, UISubHeading_StageLoad, UISubHeading_Serialize);
    api->register_object_full((void **)&UIButtonPrompt, "UIButtonPrompt",
                              (unsigned)sizeof(EntityUIButtonPrompt), (unsigned)sizeof(ObjectUIButtonPrompt),
                              UIButtonPrompt_Update, UIButtonPrompt_LateUpdate, UIButtonPrompt_StaticUpdate,
                              UIButtonPrompt_Draw, UIButtonPrompt_Create, UIButtonPrompt_StageLoad, UIButtonPrompt_Serialize);
    /* M1b: UIModeButton -- the 4 MAIN-MENU rows. Registered so foreach_all(UIModeButton)
     * at MenuSetup_SetupActions:507 wires each row's actionCB, and the 4 Scene1.bin
     * UIModeButton placements instantiate + draw (icon+text+plates). UIModeButton_StageLoad
     * loads UI/MainIcons.bin (the mode icons); the text labels come from UIWidgets->textFrames
     * (UI/TextEN.bin). NULL editor callbacks (the verbatim RSDK_REGISTER_OBJECT non-editor
     * arm). Its closure (UIModeButton_SetupSprites/_State_*) is intra-Game_UIModeButton.o. */
    api->register_object_full((void **)&UIModeButton, "UIModeButton",
                              (unsigned)sizeof(EntityUIModeButton), (unsigned)sizeof(ObjectUIModeButton),
                              UIModeButton_Update, UIModeButton_LateUpdate, UIModeButton_StaticUpdate,
                              UIModeButton_Draw, UIModeButton_Create, UIModeButton_StageLoad, UIModeButton_Serialize);
    /* M2 (Task #298): UISaveSlot + UITransition registration RE-ENABLED. The
     * 0x06000956 crash was the UNIFORM 512x592 pool (DECISIVE bisect: the GHZ
     * dual-stride pool renders the menu); the fix keeps the dual-stride pool
     * byte-identical and routes EntityUISaveSlot (588 B) to a wide-scene sub-pool
     * (Object.hpp/Object.cpp #if P6_FRONTEND_MENU). The bisect #if 0 is removed. */
    /* M2: UISaveSlot -- the 10 save-select slot widgets (Menu/Scene1.bin AUDIT 1). The
     * SELECT chain (confirm a slot -> UISaveSlot_State_Selected grows fxRadius -> runs
     * actionCB = MenuSetup_SaveSlot_ActionCB -> SetScene("Cutscenes","Angel Island Zone")).
     * UISaveSlot_StageLoad loads "UI/SaveSelect.bin" via the engine pack (no new cd/ asset).
     * Its closure (SaveGame, UIWidgets, UIDialog, UIWaitSpinner, API helpers) resolves via
     * p6_closure_edge + Game_UIWidgets.o + the engine table (MEASURED satisfiable). */
    api->register_object_full((void **)&UISaveSlot, "UISaveSlot",
                              (unsigned)sizeof(EntityUISaveSlot), (unsigned)sizeof(ObjectUISaveSlot),
                              UISaveSlot_Update, UISaveSlot_LateUpdate, UISaveSlot_StaticUpdate,
                              UISaveSlot_Draw, UISaveSlot_Create, UISaveSlot_StageLoad, UISaveSlot_Serialize);
    /* M2: UITransition -- the screen-wipe transition (1 placement). UIModeButton_SelectedCB
     * (UIModeButton.c:208) runs the mode row's actionCB ONLY through UITransition_StartTransition;
     * without it, Mania Mode -> "Save Select" never fires. UITransition_StartTransition derefs
     * UIDialog->activeDialog (UITransition.c:57) -- p6_menu_apic_init installs a real zeroed
     * UIDialog instance (activeDialog==NULL) so that deref is valid (UIDialog is NOT placed). */
    api->register_object_full((void **)&UITransition, "UITransition",
                              (unsigned)sizeof(EntityUITransition), (unsigned)sizeof(ObjectUITransition),
                              UITransition_Update, UITransition_LateUpdate, UITransition_StaticUpdate,
                              UITransition_Draw, UITransition_Create, UITransition_StageLoad, UITransition_Serialize);
#endif
#if defined(P6_AIZ_TEST)
    /* M3.1: the AIZ intro-cutscene DRIVER set. Register order is irrelevant (the engine
     * LoadGameConfig matches by md5(name)); these resolve their classIDs the same way the
     * Menu/GHZ objects do. NULL editor callbacks (the verbatim RSDK_REGISTER_OBJECT
     * non-editor arm; GAME_INCLUDE_EDITOR off). AIZSetup_StageLoad sets Zone->cameraBoundsB
     * + arms the AIZ anims; AIZSetup_StaticUpdate runs SetupObjects + the cutscene start.
     * CutsceneSeq is spawned dynamically (ResetEntitySlot(SLOT_CUTSCENESEQ)) by
     * StartSequence -- registering it gives it a live classID so that ResetEntitySlot
     * resolves. AIZTornado/AIZTornadoPath Create at the scene's placed positions; the
     * START AIZTornadoPath node grabs + positions SLOT_CAMERA1 (the camera fix). */
    api->register_object_full((void **)&AIZSetup, "AIZSetup",
                              (unsigned)sizeof(EntityAIZSetup), (unsigned)sizeof(ObjectAIZSetup),
                              AIZSetup_Update, AIZSetup_LateUpdate, AIZSetup_StaticUpdate,
                              AIZSetup_Draw, AIZSetup_Create, AIZSetup_StageLoad, AIZSetup_Serialize);
    api->register_object_full((void **)&CutsceneSeq, "CutsceneSeq",
                              (unsigned)sizeof(EntityCutsceneSeq), (unsigned)sizeof(ObjectCutsceneSeq),
                              CutsceneSeq_Update, CutsceneSeq_LateUpdate, CutsceneSeq_StaticUpdate,
                              CutsceneSeq_Draw, CutsceneSeq_Create, CutsceneSeq_StageLoad, CutsceneSeq_Serialize);
    api->register_object_full((void **)&AIZTornado, "AIZTornado",
                              (unsigned)sizeof(EntityAIZTornado), (unsigned)sizeof(ObjectAIZTornado),
                              AIZTornado_Update, AIZTornado_LateUpdate, AIZTornado_StaticUpdate,
                              AIZTornado_Draw, AIZTornado_Create, AIZTornado_StageLoad, AIZTornado_Serialize);
    api->register_object_full((void **)&AIZTornadoPath, "AIZTornadoPath",
                              (unsigned)sizeof(EntityAIZTornadoPath), (unsigned)sizeof(ObjectAIZTornadoPath),
                              AIZTornadoPath_Update, AIZTornadoPath_LateUpdate, AIZTornadoPath_StaticUpdate,
                              AIZTornadoPath_Draw, AIZTornadoPath_Create, AIZTornadoPath_StageLoad, AIZTornadoPath_Serialize);
    /* Placed actors -- registered so AIZSetup_SetupObjects' foreach_all/CREATE_ENTITY
     * resolve the real entities (inert for M3.1; their motion is the M3.2/M3.3 beats).
     * PhantomRuby/FXRuby anims (Global/PhantomRuby.bin, Global/FXRuby.bin) are ABSENT
     * from DATA.RSDK -> their StageLoad LoadSpriteAnimation returns -1 gracefully (RSDK
     * contract); they draw nothing until the assets are converted, which is fine for the
     * M3.1 camera-framing goal (the early cutscene states never deref them). */
    api->register_object_full((void **)&AIZKingClaw, "AIZKingClaw",
                              (unsigned)sizeof(EntityAIZKingClaw), (unsigned)sizeof(ObjectAIZKingClaw),
                              AIZKingClaw_Update, AIZKingClaw_LateUpdate, AIZKingClaw_StaticUpdate,
                              AIZKingClaw_Draw, AIZKingClaw_Create, AIZKingClaw_StageLoad, AIZKingClaw_Serialize);
    api->register_object_full((void **)&AIZEggRobo, "AIZEggRobo",
                              (unsigned)sizeof(EntityAIZEggRobo), (unsigned)sizeof(ObjectAIZEggRobo),
                              AIZEggRobo_Update, AIZEggRobo_LateUpdate, AIZEggRobo_StaticUpdate,
                              AIZEggRobo_Draw, AIZEggRobo_Create, AIZEggRobo_StageLoad, AIZEggRobo_Serialize);
    api->register_object_full((void **)&PhantomRuby, "PhantomRuby",
                              (unsigned)sizeof(EntityPhantomRuby), (unsigned)sizeof(ObjectPhantomRuby),
                              PhantomRuby_Update, PhantomRuby_LateUpdate, PhantomRuby_StaticUpdate,
                              PhantomRuby_Draw, PhantomRuby_Create, PhantomRuby_StageLoad, PhantomRuby_Serialize);
    api->register_object_full((void **)&FXRuby, "FXRuby",
                              (unsigned)sizeof(EntityFXRuby), (unsigned)sizeof(ObjectFXRuby),
                              FXRuby_Update, FXRuby_LateUpdate, FXRuby_StaticUpdate,
                              FXRuby_Draw, FXRuby_Create, FXRuby_StageLoad, FXRuby_Serialize);
#endif
#if defined(P6_GHZCUT_BOOT)
    /* Task #309: the AIZ->GHZCutscene destination scene's 2 NEW driver objects. Register
     * order is irrelevant (the engine LoadGameConfig matches by md5(name)). NULL editor
     * callbacks (the verbatim RSDK_REGISTER_OBJECT non-editor arm). FXRuby is ALREADY
     * registered in the AIZ block above; GHZSetup + BGSwitch are ALREADY registered by the
     * PACK (p6_wave1_reg.c:157,166 RSDK_REGISTER_OBJECT, unconditional in every flavor) --
     * registering EITHER again here would DOUBLE-register (a second class-table entry ->
     * corrupt classID resolution). So ONLY GHZCutsceneST + CutsceneHBH (which are NOT in
     * the pack) are registered here. GHZCutsceneST_Create -> CutsceneRules_SetupEntity sets
     * the cutscene-trigger hitbox (the real impl gated into p6_closure_edge.c under
     * P6_GHZCUT_BOOT); its Update's Player_CheckCollisionTouch then starts the fixed-timer
     * beat chain that reaches GHZCutsceneST_Cutscene_SetupGHZ1 -> SetScene("Mania Mode","")
     * + LoadScene (the playable-GHZ handoff). CutsceneHBH = the 5 Heavies (boss sheets
     * absent -> LoadSpriteAnimation returns -1 gracefully -> invisible; Tier-A acceptable). */
    extern ObjectGHZCutsceneST *GHZCutsceneST;
    extern ObjectCutsceneHBH *CutsceneHBH;
    api->register_object_full((void **)&GHZCutsceneST, "GHZCutsceneST",
                              (unsigned)sizeof(EntityGHZCutsceneST), (unsigned)sizeof(ObjectGHZCutsceneST),
                              GHZCutsceneST_Update, GHZCutsceneST_LateUpdate, GHZCutsceneST_StaticUpdate,
                              GHZCutsceneST_Draw, GHZCutsceneST_Create, GHZCutsceneST_StageLoad, GHZCutsceneST_Serialize);
    /* Task #309 Tier-B.2: register the Draw SHIM (p6_cuthbh_draw) instead of the raw
     * decomp CutsceneHBH_Draw so each Heavy routes to its own CRAM palette block. The
     * verbatim CutsceneHBH_Draw stays untouched (the shim calls it). */
    api->register_object_full((void **)&CutsceneHBH, "CutsceneHBH",
                              (unsigned)sizeof(EntityCutsceneHBH), (unsigned)sizeof(ObjectCutsceneHBH),
                              CutsceneHBH_Update, CutsceneHBH_LateUpdate, CutsceneHBH_StaticUpdate,
                              p6_cuthbh_draw, CutsceneHBH_Create, CutsceneHBH_StageLoad, CutsceneHBH_Serialize);
#endif
    /* MASS-PORT BATCH 1 (verified-CLEAN drop-ins; closure self-confirmed: only
     * Zone/Player/SceneInfo/DebugMode/Zone_RotateOnPivot, all ported). Decoration =
     * GHZ scenery; ForceSpin/ForceUnstick/SpinBooster = player-state trigger regions
     * (reuse PlaneSwitch/ItemBox anims, already staged). */
    api->register_object_full((void **)&Decoration, "Decoration",
                              (unsigned)sizeof(EntityDecoration), (unsigned)sizeof(ObjectDecoration),
                              Decoration_Update, Decoration_LateUpdate, Decoration_StaticUpdate,
                              Decoration_Draw, Decoration_Create, Decoration_StageLoad, Decoration_Serialize);
    api->register_object_full((void **)&ForceSpin, "ForceSpin",
                              (unsigned)sizeof(EntityForceSpin), (unsigned)sizeof(ObjectForceSpin),
                              ForceSpin_Update, ForceSpin_LateUpdate, ForceSpin_StaticUpdate,
                              ForceSpin_Draw, ForceSpin_Create, ForceSpin_StageLoad, ForceSpin_Serialize);
    api->register_object_full((void **)&SpinBooster, "SpinBooster",
                              (unsigned)sizeof(EntitySpinBooster), (unsigned)sizeof(ObjectSpinBooster),
                              SpinBooster_Update, SpinBooster_LateUpdate, SpinBooster_StaticUpdate,
                              SpinBooster_Draw, SpinBooster_Create, SpinBooster_StageLoad, SpinBooster_Serialize);

    /* MASS-PORT BATCH 2 -- the badnik break CHAIN. Register order is irrelevant
     * (engine matches by md5(name)). CHAIN TUs first: BadnikHelpers (no-op helper
     * class), Explosion (StageLoad loads Global/Explosions.bin + the Destroy.wav
     * sfxDestroy -- needs a live classID for the CREATE_ENTITY in the break path),
     * Animals (StageLoad loads Global/Animals.bin -- needs a live classID for the
     * spawnAnimals=TRUE CREATE_ENTITY + animalTypes[] deref). Then the 6 GHZ1
     * badniks. ALL closures verified (adversarial harvest): Player_CheckBadnikTouch
     * + Player_CheckBadnikBreak + Player_ProjectileHurt (pack, -u rooted),
     * Zone/DebugMode/SceneInfo (ported), APICallback_TrackEnemyDefeat (inert stub
     * added), Water/Platform/Press (GHZ1-dead NULLs), Bridge_HandleCollisions
     * (Game_Bridge.o, overlay-internal). Anims slow-path into DATASET_STG
     * (+9.8 KB MEASURED; pool grown 150 to 166 KB). */
    api->register_object_full((void **)&BadnikHelpers, "BadnikHelpers",
                              (unsigned)sizeof(EntityBadnikHelpers), (unsigned)sizeof(ObjectBadnikHelpers),
                              BadnikHelpers_Update, BadnikHelpers_LateUpdate, BadnikHelpers_StaticUpdate,
                              BadnikHelpers_Draw, BadnikHelpers_Create, BadnikHelpers_StageLoad, BadnikHelpers_Serialize);
    api->register_object_full((void **)&Explosion, "Explosion",
                              (unsigned)sizeof(EntityExplosion), (unsigned)sizeof(ObjectExplosion),
                              Explosion_Update, Explosion_LateUpdate, Explosion_StaticUpdate,
                              Explosion_Draw, Explosion_Create, Explosion_StageLoad, Explosion_Serialize);
    api->register_object_full((void **)&Animals, "Animals",
                              (unsigned)sizeof(EntityAnimals), (unsigned)sizeof(ObjectAnimals),
                              Animals_Update, Animals_LateUpdate, Animals_StaticUpdate,
                              Animals_Draw, Animals_Create, Animals_StageLoad, Animals_Serialize);
    api->register_object_full((void **)&Newtron, "Newtron",
                              (unsigned)sizeof(EntityNewtron), (unsigned)sizeof(ObjectNewtron),
                              Newtron_Update, Newtron_LateUpdate, Newtron_StaticUpdate,
                              Newtron_Draw, Newtron_Create, Newtron_StageLoad, Newtron_Serialize);
    api->register_object_full((void **)&Crabmeat, "Crabmeat",
                              (unsigned)sizeof(EntityCrabmeat), (unsigned)sizeof(ObjectCrabmeat),
                              Crabmeat_Update, Crabmeat_LateUpdate, Crabmeat_StaticUpdate,
                              Crabmeat_Draw, Crabmeat_Create, Crabmeat_StageLoad, Crabmeat_Serialize);
    api->register_object_full((void **)&BuzzBomber, "BuzzBomber",
                              (unsigned)sizeof(EntityBuzzBomber), (unsigned)sizeof(ObjectBuzzBomber),
                              BuzzBomber_Update, BuzzBomber_LateUpdate, BuzzBomber_StaticUpdate,
                              BuzzBomber_Draw, BuzzBomber_Create, BuzzBomber_StageLoad, BuzzBomber_Serialize);
    api->register_object_full((void **)&Chopper, "Chopper",
                              (unsigned)sizeof(EntityChopper), (unsigned)sizeof(ObjectChopper),
                              Chopper_Update, Chopper_LateUpdate, Chopper_StaticUpdate,
                              Chopper_Draw, Chopper_Create, Chopper_StageLoad, Chopper_Serialize);
    api->register_object_full((void **)&Motobug, "Motobug",
                              (unsigned)sizeof(EntityMotobug), (unsigned)sizeof(ObjectMotobug),
                              Motobug_Update, Motobug_LateUpdate, Motobug_StaticUpdate,
                              Motobug_Draw, Motobug_Create, Motobug_StageLoad, Motobug_Serialize);
    api->register_object_full((void **)&Batbrain, "Batbrain",
                              (unsigned)sizeof(EntityBatbrain), (unsigned)sizeof(ObjectBatbrain),
                              Batbrain_Update, Batbrain_LateUpdate, Batbrain_StaticUpdate,
                              Batbrain_Draw, Batbrain_Create, Batbrain_StageLoad, Batbrain_Serialize);

    /* Ring vtable; witness_fn is the COMBINED tick witness below. staticvars_slot
     * feeds the F.3 main-image Ring-global rewire (p6_io_main: Ring = *staticvars_slot
     * so SignPost's CREATE_ENTITY(Ring) resolves). arm_fn=0: the real Ring_Create arms
     * placed rings now (the p6_io_main:1767 manual-proof guard skips on NULL). */
    api->staticvars_slot = (void *)&Ring;
    api->entity_size     = (unsigned)sizeof(EntityRing);
    api->arm_fn          = 0;
    api->witness_fn      = (void (*)(const void *))p6_ghz_ovl_witness;
    api->update_fn       = (void *)Ring_Update;
    /* #258b: export the overlay's REAL hurt-ring-scatter entry points so the
     * pack-side Player (which calls Ring_LoseRings on hurt) reaches them via the
     * p6_closure_edge forward instead of the pack stub. */
    api->loserings_fn      = (void *)Ring_LoseRings;
    api->losehyperrings_fn = (void *)Ring_LoseHyperRings;
    /* BATCH 2: export the overlay's REAL BadnikHelpers break fns. Game_Player.o
     * (PACK) calls BadnikHelpers_BadnikBreakUnseeded on every badnik kill; the
     * p6_closure_edge stub forwards pack->overlay to these (the chain TUs are
     * overlay-resident because Animals refs the overlay's Bridge_HandleCollisions). */
    api->badnikbreak_unseeded_fn = (void *)BadnikHelpers_BadnikBreakUnseeded;
    api->badnikbreak_fn          = (void *)BadnikHelpers_BadnikBreak;
    /* BATCH 2: expose &Animals (the overlay's registered Animals object**) so the
     * pack's NULL Animals placeholder (ActClear.c:903 foreach_active) gets rewired
     * to the live object each frame (p6_io_main, the #235 Ring-seam). */
    api->animals_slot = (void *)&Animals;
    /* I3b 2b: the pack drives the materialize one-shot at load (s_ovl.materialize_fn). */
    api->materialize_fn = p6_ovl_materialize;
    /* I3b 2b: the pack drives the COMPACTION one-shot at load (s_ovl.compact_fn) -- relocates all
     * populated scene entities into a dense physical pool (overlay-resident per the residency rule). */
    api->compact_fn = p6_ovl_pool_compact;
    /* I3b 2b: the per-frame STREAMING manager (s_ovl.stream_fn) -- materialize newly-near + dormant
     * newly-far, the camera-local pool's live half. Called from ProcessObjects via p6_stream_tick. */
    api->stream_fn = p6_ovl_stream;
#if defined(P6_FRONTEND_MENU)
    /* M1b AUTH-GATE FLIP: export the menu auth init so the pack runs it at the top of
     * the first frontend frame (before MenuSetup's first StaticUpdate -> InitAPI). The
     * overlay owns the ObjectAPICallback struct (needs the Mania Game.h type the pack
     * lacks); it writes the pack APICallback/globals via -R. */
    api->menu_apic_init_fn = p6_menu_apic_init;
    /* M2b/M3 (Task #294/#295): export the Saturn-native 320 layout apply so the pack runs
     * it each frame after ProcessObjects + before ProcessObjectDrawLists (overrides the 4
     * UIModeButton world positions to the 320-fit grid + sets the scroll origin). */
    api->menu_layout_fn = p6_menu_apply_layout;
#endif
#if defined(P6_GHZCUT_BOOT)
    /* Task #309 Tier-B.1: export the FXRuby fade reader so the pack applies the
     * VDP2 color offset each frame (p6_vdp2_fade_apply) from the live FXRuby. */
    api->fade_fn = p6_ghzcut_fade_fn;
#endif
    return 0;
}

#if defined(P6_GHZCUT_BOOT)
// =============================================================================
// Task #309 Tier-B.1: the FXRuby fade reader (api->fade_fn). Called from
// p6_frontend_frame each tick AFTER ProcessObjectDrawLists. Reads the live
// FXRuby entity's fade fields into the engine-visible ints the engine hands to
// p6_vdp2_fade_apply (the VDP2 Color Offset write). The overlay does this (not
// the engine TU) because EntityFXRuby is a Mania Game.h type the engine cannot
// name. fadeWhite/fadeBlack are seeded 0x200 by GHZCutsceneST_SetupObjects then
// ramped by GHZCutsceneST_Cutscene_FadeIn (GHZCutsceneST.c:88-89,144-153).
//
// Under P6_GHZCUT_HOLD (the RED-gate capture flavor): FIRST pin the live FXRuby
// fade to a fixed visible wash so the cutscene FREEZES at it -- FXRuby's FadeIn
// beat returns true (-> SetupGHZ1 handoff) only when BOTH fades reach 0, so
// pinning fadeWhite>0 (and fadeBlack=0) keeps FadeIn returning false forever ->
// no handoff -> the wash is on-screen for the savestate capture.
//   P6_GHZCUT_HOLD_WHITE (default 256 = full white wash, FillScreen alpha 255).
// =============================================================================
#ifndef P6_GHZCUT_HOLD_WHITE
#define P6_GHZCUT_HOLD_WHITE 256
#endif
static void p6_ghzcut_fade_fn(int *outWhite, int *outBlack)
{
    int w = 0, b = 0;
    foreach_all(FXRuby, fx)
    {
#if defined(P6_GHZCUT_HOLD)
        /* Freeze the cutscene for the capture. Pin BEFORE reading so the returned
         * values reflect the held state. FadeIn (GHZCutsceneST.c:142-154) returns
         * true (-> advances past FadeIn toward the GHZ handoff) ONLY when BOTH
         * fadeWhite AND fadeBlack reach <=0. So to FREEZE the cutscene at FadeIn
         * (Heavies still at their placed positions, alive, BEFORE the ExitHBH beat
         * flies+destroys them), pin at least one fade > 0 every tick.
         *   P6_GHZCUT_HOLD_WHITE > 0  -> full white wash (Tier-B.1 fade gate).
         *   P6_GHZCUT_HOLD_WHITE == 0 -> Tier-B.2 Heavy capture: pin fadeBlack=1
         *     (a -1 VDP2 color offset, IMPERCEPTIBLE) so FadeIn never completes ->
         *     the cutscene is frozen at FadeIn with the Heavies VISIBLE + un-washed. */
        fx->fadeWhite = P6_GHZCUT_HOLD_WHITE;
        fx->fadeBlack = (P6_GHZCUT_HOLD_WHITE > 0) ? 0 : 1;
#endif
        w = (int)fx->fadeWhite;
        b = (int)fx->fadeBlack;
        foreach_break;
    }
    if (outWhite) *outWhite = w;
    if (outBlack) *outBlack = b;
}

// =============================================================================
// Task #309 Tier-B.2: the CutsceneHBH Draw shim (registered as the CutsceneHBH
// Draw callback). Each Heavy is a VDP1 8bpp sprite reading ONE combined atlas
// (HBHOBJ.SHT), but each owns a DISTINCT 128-color palette -> all 5 on-screen at
// once need 5 distinct CRAM blocks (HBHPAL.BIN @ CRAM[512/768/1024/1280/1536]).
// The Saturn selects a sprite's CRAM block via jo colno (= block*256 = the VDP1
// CMDCOLR high byte; DOC-CITED ST-013-R3 sec 6.4 + ST-058-R2 sec 10.1 Type-3
// full-11-bit DC, SPCAOS=0). So set p6_heavy_palblock = 2 + characterID (GUNNER=0
// -> block 2 -> colno 512 ... KING=4 -> block 6 -> colno 1536) BEFORE the verbatim
// decomp CutsceneHBH_Draw runs its two DrawSprite calls, then reset to 1 (the
// normal bank). The decomp's CutsceneHBH_SetupPalettes/RestorePalette (SetPaletteEntry
// to bank0 0x80-0xFF) are a NO-OP on Saturn (the palette lives in the CRAM block,
// not bank0), but they're harmless (the verbatim Draw still calls them).
//   characterID 0..4 (HBH_GUNNER..HBH_KING) -> block 2..6. cid>4 (the off-path
//   Rogues/PILE/damaged-King, never in the GHZ ExitHBH beat) falls back to block 1.
static void p6_cuthbh_draw(void)
{
    RSDK_THIS(CutsceneHBH);   /* self = (EntityCutsceneHBH *)SceneInfo->entity */
    int cid   = (int)self->characterID;
    int block = (cid >= HBH_GUNNER && cid <= HBH_KING) ? (2 + cid) : 1;

    /* witnesses: this Heavy's aniFrames (>=0 == HBHOBJ.PAK .bin loaded) + a blit
     * counter (incremented when the Heavy actually drew an on-screen frame). */
    p6_w_hbh_aniframes = (int32)(int16)self->aniFrames;

    p6_heavy_palblock = block;
    {
        /* count Heavy-region landed blits via the global VDP1 landed delta
         * (p6_w_vdp1_landed declared at file scope above). */
        int before = p6_w_vdp1_landed;
        CutsceneHBH_Draw();                 /* verbatim decomp Draw (2x DrawSprite) */
        p6_w_hbh_landed += (p6_w_vdp1_landed - before);
    }
    p6_heavy_palblock = 1;                   /* restore the normal sprite bank */
}
#endif /* P6_GHZCUT_BOOT */

// =============================================================================
// Combined per-tick witness (api->witness_fn, called from p6_ghz_frame's
// shipping loop): Ring residency + the Spring/Bridge/PlaneSwitch latches (each
// MIGRATED here from p6_wave1_reg.c -- the resident pack no longer names these
// globals). One-shot latches; gates read p6_w_*.
// =============================================================================
static void p6_ghz_ovl_witness(const void *ringSlot)
{
    (void)ringSlot; /* p6_ring2 harness retired; Ring witnesses are aniFrames-based now */

    /* Ring (#W18 ring-arm): aniFrames>=0 == Ring_StageLoad's LoadSpriteAnimation
     * succeeded (rings can arm + render); classid live == registered + instantiated.
     * (int16)-cast so a -1 (0xFFFF) load failure reads as -1, not 65535. */
    if (Ring) p6_w_ring_aniframes = (int32)(int16)Ring->aniFrames;
    if (Ring && Ring->classID) p6_w_ring_classid = (int32)Ring->classID;
#if defined(P6_GHZCUT_BOOT)
    /* Tier-B.2 DIAG: why don't the Heavies draw? Disambiguate the failure:
     *   count == -2 : CutsceneHBH Object* is NULL (never registered/resolved)
     *   count == -3 : CutsceneHBH registered but classID==0 (scene didn't bind it)
     *   count >= 0  : that many live CutsceneHBH entities. */
    if (!CutsceneHBH)
        p6_w_hbh_count = -2;
    else if (!CutsceneHBH->classID)
        p6_w_hbh_count = -3;
    else {
        int n = 0;
        EntityCutsceneHBH *first = NULL;
        foreach_all(CutsceneHBH, hbh) {
            if (!first) first = hbh;
            ++n;
        }
        p6_w_hbh_count = n;
        if (first) {
            p6_w_hbh_vis    = ((int32)(first->visible ? 1 : 0) << 16)
                            | ((int32)(first->onScreen ? 1 : 0) << 8)
                            | (int32)first->active;
            p6_w_hbh_posx   = (int32)(first->position.x >> 16);
            p6_w_hbh_posy   = (int32)(first->position.y >> 16);
            p6_w_hbh_aniframes = (int32)(int16)first->aniFrames;
            p6_w_hbh_animid = (int32)first->mainAnimator.animationID;
        }
    }
    /* Heavy sheet bind state, INDEPENDENT of the entities (the surface exists once
     * CutsceneHBH_LoadSprites' LoadSpriteAnimation resolved "Cutscene/HBH.gif").
     * LoadSpriteSheet is idempotent: returns the existing surface if already loaded,
     * or -1 if the sheet was never referenced. handle>=0 == bound to VDP1. */
    {
        int32 hs = (int32)(int16)RSDK.LoadSpriteSheet("Cutscene/HBH.gif", SCOPE_STAGE);
        p6_w_hbh_handle = (hs >= 0) ? p6_vdp1_handle_for_surface(hs) : -5;
    }
    p6_w_hbh_camy = (int32)(ScreenInfo ? ScreenInfo->position.y : -1);
#endif
#if defined(P6_FRONTEND_LOGOS)
    /* CP4 (E2/E3): latch the front-end classIDs once they resolve. Called from
     * p6_frontend_frame each tick (the api->witness_fn seam). */
    if (LogoSetup && LogoSetup->classID) p6_w_logosetup_classid = (int32)LogoSetup->classID;
    if (UIPicture && UIPicture->classID) p6_w_uipicture_classid = (int32)UIPicture->classID;
    /* CP4b render diag: did UIPicture's Logos.bin animation load + does a live
     * UIPicture entity have a frame table? (int16-cast so -1 reads as -1.) */
    if (UIPicture) p6_w_uipicture_aniframes = (int32)(int16)UIPicture->aniFrames;
    {
        EntityUIPicture *up = NULL;
        foreach_all(UIPicture, e) { up = e; break; }
        if (up) p6_w_uipicture_framesNN = (up->animator.frames != NULL) ? 1 : 0;
    }
    /* CP4c BLUE-SCREEN diag: latch the first live UIPicture entity's full draw-chain
     * state -- the exact links p6_vdp1_blit needs (mirrors BD_SCAN, :456). Prefer an
     * on-screen entity. The resolved frame's sheetID -> p6_vdp1_handle_for_surface
     * is the load-bearing one: handle<0 == Logos surface UNBOUND == the blit drops. */
    if (UIPicture && UIPicture->classID) {
        foreach_all(UIPicture, up2) {
            int32 onscr = (int32)up2->onScreen;
            if (p6_w_uipic_drawgrp < 0 || (onscr && p6_w_uipic_onscreen <= 0)) {
                p6_w_uipic_drawgrp  = (int32)up2->drawGroup;
                p6_w_uipic_active   = (int32)up2->active;
                p6_w_uipic_visible  = (int32)up2->visible;
                p6_w_uipic_onscreen = onscr;
                p6_w_uipic_posx     = (int32)(up2->position.x >> 16);
                p6_w_uipic_posy     = (int32)(up2->position.y >> 16);
                p6_w_uipic_animid   = (int32)up2->animator.animationID;
                p6_w_uipic_frameid  = (int32)up2->animator.frameID;
                SpriteFrame *ufr = RSDK.GetFrame(UIPicture->aniFrames,
                    up2->animator.animationID, up2->animator.frameID);
                p6_w_uipic_sheetid  = ufr ? (int32)ufr->sheetID : -1;
                p6_w_uipic_handle   = ufr ? p6_vdp1_handle_for_surface(ufr->sheetID) : -4;
            }
        }
    }
#endif
#if defined(P6_FRONTEND_TITLE)
    /* CP5a (T2/T3): latch the Title classIDs once they resolve. Called from
     * p6_frontend_frame each tick (the api->witness_fn seam). T4 (objcount) is
     * latched in InitObjects (p6_io_main); T1/T5 are set in p6_title_reload /
     * p6_frontend_frame. */
    if (TitleSetup && TitleSetup->classID) p6_w_titlesetup_classid = (int32)TitleSetup->classID;
    if (TitleLogo  && TitleLogo->classID)  p6_w_titlelogo_classid  = (int32)TitleLogo->classID;
#if !defined(P6_TITLEBG_SPRITES_OFF)
    /* CP5b.4 (Task #272): latch the TitleBG + Title3DSprite classIDs (registered ==
     * classID>0) + a count of entities flipped VISIBLE by TitleBG_SetupFX. Their
     * object globals are overlay-resident (in scope here); the witnesses are pack
     * globals (ld -R import). The vis counts are the V2 RED->GREEN signal: 0 while
     * the objects are unregistered (the bed5bac baseline), >0 once SetupFX ran the
     * foreach_all(...){visible=true}. Rebuilt each tick (snapshot, not latch). */
    if (TitleBG       && TitleBG->classID)       p6_w_titlebg_classid = (int32)TitleBG->classID;
    if (Title3DSprite && Title3DSprite->classID) p6_w_title3d_classid = (int32)Title3DSprite->classID;
    {
        int32 bgvis = 0, t3dvis = 0;
        if (TitleBG && TitleBG->classID) {
            foreach_all(TitleBG, bgE) { if (bgE->visible) ++bgvis; }
        }
        if (Title3DSprite && Title3DSprite->classID) {
            foreach_all(Title3DSprite, t3dE) { if (t3dE->visible) ++t3dvis; }
        }
        p6_w_titlebg_vis = bgvis;
        p6_w_title3d_vis = t3dvis;
    }
#endif
    /* CP5b.1 (Task #268) RENDER diag: latch the first live VISIBLE TitleLogo entity's
     * draw-chain state -- the exact links p6_vdp1_blit needs (mirrors the CP4c
     * p6_w_uipic_* block). Prefer a visible+on-screen piece (the emblem/ribbon/
     * gametitle become visible at AnimateUntilFlash/FlashIn). The resolved frame's
     * sheetID -> p6_vdp1_handle_for_surface is the load-bearing link: handle<0 ==
     * Title/Logo.gif UNBOUND == the blit drops (the CP5a RED). landed = the global
     * VDP1 landed-blit count snapshot (Title is sprite-only -> corroborates the logo
     * reached the framebuffer; the screenshot + pixel measure are the PRIMARY proof). */
    p6_w_tlogo_landed = p6_w_vdp1_landed;
    if (TitleLogo && TitleLogo->classID) {
        foreach_all(TitleLogo, tl2) {
            int32 onscr = (int32)tl2->onScreen;
            int32 vis   = (int32)tl2->visible;
            /* take the first piece, then UPGRADE to a visible+on-screen one if found */
            if (p6_w_tlogo_drawgrp < -1
                || (vis && onscr && !(p6_w_tlogo_visible > 0 && p6_w_tlogo_onscreen > 0))) {
                p6_w_tlogo_drawgrp  = (int32)tl2->drawGroup;
                p6_w_tlogo_visible  = vis;
                p6_w_tlogo_onscreen = onscr;
                p6_w_tlogo_type     = (int32)tl2->type;
                SpriteFrame *tfr = RSDK.GetFrame(TitleLogo->aniFrames,
                    tl2->mainAnimator.animationID, tl2->mainAnimator.frameID);
                p6_w_tlogo_sheetid  = tfr ? (int32)tfr->sheetID : -1;
                p6_w_tlogo_handle   = tfr ? p6_vdp1_handle_for_surface(tfr->sheetID) : -4;
            }
        }
        /* CP5b.1 per-TYPE diag: which logo pieces are visible / on-screen / bound.
         * Rebuilt every tick (a snapshot, not a latch) so the capture reflects the
         * title's CURRENT state. bit T = type T (0=EMBLEM,1=RIBBON,2=GAMETITLE,
         * 3=POWERLED,4=COPYRIGHT,5=RINGBOTTOM,6=PRESSSTART). */
        int32 em = 0, vm = 0, om = 0, bm = 0;
        foreach_all(TitleLogo, tl3) {
            int32 t = (int32)tl3->type;
            if (t < 0 || t > 15) continue;
            int32 bit = 1 << t;
            em |= bit;
            if (tl3->visible) {
                vm |= bit;
                if (tl3->onScreen) {
                    om |= bit;
                    SpriteFrame *fr = RSDK.GetFrame(TitleLogo->aniFrames,
                        tl3->mainAnimator.animationID, tl3->mainAnimator.frameID);
                    if (fr && p6_vdp1_handle_for_surface(fr->sheetID) >= 0)
                        bm |= bit;
                }
            }
        }
        p6_w_tlogo_existmask = em;
        p6_w_tlogo_vismask   = vm;
        p6_w_tlogo_onscrmask = om;
        p6_w_tlogo_boundmask = bm;
    }
    /* CP5b.2 (Task #269) RENDER diag: the live TitleSonic entity's draw-chain state --
     * the exact links p6_vdp1_blit needs for the head (mirrors the tlogo block). The
     * head's resolved frame (animatorSonic.animationID/frameID) -> GetFrame -> sheetID
     * -> p6_vdp1_handle_for_surface is the load-bearing link: handle<0 == Title/
     * Sonic.gif UNBOUND == the head drops (the CP5b.1 RED). visible flips true at
     * TitleSetup_State_FlashIn. There is ONE TitleSonic entity (Scene1.bin places it
     * once); take the first live one. */
    if (TitleSonic && TitleSonic->classID) {
        foreach_all(TitleSonic, ts2) {
            p6_w_tsonic_visible  = (int32)ts2->visible;
            p6_w_tsonic_onscreen = (int32)ts2->onScreen;
            p6_w_tsonic_animid   = (int32)ts2->animatorSonic.animationID;
            p6_w_tsonic_frameid  = (int32)ts2->animatorSonic.frameID;
            SpriteFrame *sfr = RSDK.GetFrame(TitleSonic->aniFrames,
                ts2->animatorSonic.animationID, ts2->animatorSonic.frameID);
            p6_w_tsonic_sheetid  = sfr ? (int32)sfr->sheetID : -1;
            p6_w_tsonic_handle   = sfr ? p6_vdp1_handle_for_surface(sfr->sheetID) : -4;
            break; /* one TitleSonic entity */
        }
    }
    /* which TitleSetup state: tag = a small int per state fn (compared by ptr). The
     * state field lives on the ENTITY (slot 0, where TitleSetup_StageLoad self-placed
     * it via ResetEntitySlot(0,...)), not the Object. RSDK_GET_ENTITY_GEN(0) gives it. */
    {
        Entity *e0 = RSDK_GET_ENTITY_GEN(0);
        EntityTitleSetup *ts = (EntityTitleSetup *)e0;
        if (e0 && TitleSetup && e0->classID == TitleSetup->classID) {
            void *st = (void *)ts->state;
            extern void TitleSetup_State_Wait(void);
            extern void TitleSetup_State_AnimateUntilFlash(void);
            extern void TitleSetup_State_FlashIn(void);
            extern void TitleSetup_State_WaitForSonic(void);
            extern void TitleSetup_State_SetupLogo(void);
            extern void TitleSetup_State_WaitForEnter(void);
            extern void TitleSetup_State_FadeToMenu(void);
            extern void TitleSetup_State_FadeToVideo(void);
            p6_w_tsetup_statetag =
                st == (void *)TitleSetup_State_Wait              ? 1 :
                st == (void *)TitleSetup_State_AnimateUntilFlash ? 2 :
                st == (void *)TitleSetup_State_FlashIn           ? 3 :
                st == (void *)TitleSetup_State_WaitForSonic      ? 4 :
                st == (void *)TitleSetup_State_SetupLogo         ? 5 :
                st == (void *)TitleSetup_State_WaitForEnter      ? 6 :
                st == (void *)TitleSetup_State_FadeToMenu        ? 7 :
                st == (void *)TitleSetup_State_FadeToVideo       ? 8 : 0;
        }
    }
#endif
#if defined(P6_AIZ_TEST)
    /* M3.1 (qa_p6_aiz_cutscene): the AIZ intro-cutscene DRIVER witnesses. Called per-tick
     * via the api->witness_fn seam (after ProcessObjects ran the cutscene state machine).
     * The class-id witnesses prove registration+instantiation (C3). cutscene_state reads
     * the LIVE CutsceneSeq entity's stateID (C1: -1 until the seq is spawned at
     * SLOT_CUTSCENESEQ by StartSequence, then 0+ == EnterAIZ running). cam_x reads
     * SLOT_CAMERA1's live position.x (C2 corroboration: the cutscene-driven camera). */
    if (AIZSetup       && AIZSetup->classID)       p6_w_aiz_setup_classid   = (int32)AIZSetup->classID;
    if (CutsceneSeq    && CutsceneSeq->classID)    p6_w_aiz_seq_classid     = (int32)CutsceneSeq->classID;
    if (AIZTornado     && AIZTornado->classID)     p6_w_aiz_tornado_classid = (int32)AIZTornado->classID;
    if (AIZTornadoPath && AIZTornadoPath->classID) p6_w_aiz_path_classid    = (int32)AIZTornadoPath->classID;
    /* the live CutsceneSeq sits at SLOT_CUTSCENESEQ (StartSequence ResetEntitySlot'd it
     * there). stateID is on the ENTITY. -1 stays until the seq classID matches (it is
     * spawned). The seq destroys itself at sequence end, so the read is guarded. */
    if (CutsceneSeq && CutsceneSeq->classID) {
        EntityCutsceneSeq *cs = (EntityCutsceneSeq *)RSDK.GetEntity(P6_AIZ_SLOT_CUTSCENESEQ);
        if (cs && cs->classID == CutsceneSeq->classID)
            p6_w_aiz_cutscene_state = (int32)cs->stateID;
    }
    /* #308 FIX (measured): the AIZ intro cutscene STALLS at beat 3 (P2FlyIn) because
     * Tails(SLOT_PLAYER2) is frozen in AIZSetup_PlayerState_Static (stateptr 0x026a7a88
     * confirmed vs ovl_ring.map; visible=0, inputptr=NULL, posy=0 -- the Static signature)
     * -> P2Enter never ran -> Tails never flies in/lands -> P2FlyIn waits on Tails->onGround
     * forever. The decomp's beat-2 (EnterHeavies) else does exactly this transition
     * (AIZSetup.c:414-415: if player2->state==Static -> P2Enter) but it does NOT take
     * effect on Saturn (ruled out: dispatch one-beat, pointer identity 0x026a7a88==map,
     * non-NULL slot ptr, timing). Apply the decomp's OWN transition from the census (which
     * provably runs each tick): once the heavies beat is reached (stateID>=2) and Tails is
     * still stuck in Static, set P2Enter -- the decomp-exact action. One-shot: the guard
     * fails the moment state leaves Static (P2Enter -> HandleSidekickRespawn -> HoldRespawn
     * -> the normal sidekick fly-in + land). AIZ-only (#if P6_AIZ_TEST). */
    if (p6_w_aiz_cutscene_state >= 2) {
        extern void AIZSetup_PlayerState_Static(void);
        extern void AIZSetup_PlayerState_P2Enter(void);
        EntityPlayer *p2f = RSDK_GET_ENTITY(SLOT_PLAYER2, Player);
        if (p2f && Player && p2f->classID == Player->classID
                && (void *)p2f->state == (void *)AIZSetup_PlayerState_Static) {
            p2f->state = AIZSetup_PlayerState_P2Enter;
        }
    }
    /* #309 beat 7 (RubyAppear): the PhantomRuby is ACTIVE_BOUNDS (PhantomRuby.c:53) and
     * off-screen this beat, so its Update (the 38-frame flash timer in State_PlaySfx) never
     * runs -> ruby->flashFinished never set -> the cutscene stalls (MEASURED: 1773 frames on
     * beat 7, currentSceneFolder still AIZ). Force the cutscene-critical ruby ACTIVE_NORMAL
     * during its flash beats (>=7) so its Update ticks (the timer is pure frame-counting, no
     * camera dep) -> the flash finishes -> beat 7 advances. Witness active/timer/flashFinished
     * to confirm the cause + the fix. AIZ-only (#if P6_AIZ_TEST). */
    if (p6_w_aiz_cutscene_state >= 7 && PhantomRuby && PhantomRuby->classID) {
        extern int32 p6_w_aiz_ruby_active, p6_w_aiz_ruby_timer, p6_w_aiz_ruby_flashfin;
        foreach_all(PhantomRuby, rb) {
            p6_w_aiz_ruby_active   = (int32)rb->active;
            p6_w_aiz_ruby_timer    = (int32)rb->timer;
            p6_w_aiz_ruby_flashfin = (int32)rb->flashFinished;
            if (rb->active == ACTIVE_BOUNDS)
                rb->active = ACTIVE_NORMAL;
            foreach_break;
        }
    }
    /* SLOT_CAMERA1 live position.x -- the AIZTornadoPath START node grabbed + positioned
     * it; HandleMoveSpeed drives it each frame. Read generically (position is at the
     * RSDK_ENTITY base offset, identical across entity types). */
    {
        Entity *cam = RSDK_GET_ENTITY_GEN(P6_AIZ_SLOT_CAMERA1);
        if (cam) p6_w_aiz_cam_x = (int32)(cam->position.x >> 16);
    }
    /* M3.1 DIAG (camera-progression): the live AIZTornado entity's position.x +
     * disableInteractions, the AIZTornadoPath static moveVel.x, and a count of active
     * AIZTornado/Path entities. If tornado_x stays at its spawn (60) and moveVel_x==0,
     * the path is not driving the tornado (the camera stays EnterAIZ-clamped at 320). */
    {
        extern int32 p6_w_aiz_torn_x, p6_w_aiz_torn_dis, p6_w_aiz_path_movevel, p6_w_aiz_torn_active, p6_w_aiz_torn_state;
        extern int32 p6_w_aiz_tornado_frames; /* R3.1 (#305): animatorTornado.frameCount (>0 == SetSpriteAnimation resolved the sheet) */
        extern int32 p6_w_aiz_torn_aniframes, p6_w_aiz_torn_animid, p6_w_aiz_torn_count; /* R3.1 anim-load diag */
        if (AIZTornado && AIZTornado->classID) {
            p6_w_aiz_torn_aniframes = (int32)(int16)AIZTornado->aniFrames; /* OBJECT-level load result */
            int32 tc = 0;
            foreach_all(AIZTornado, t) {
                if (tc == 0) {
                    p6_w_aiz_torn_x      = (int32)(t->position.x >> 16);
                    p6_w_aiz_torn_dis    = (int32)t->disableInteractions;
                    p6_w_aiz_torn_active = (int32)t->active;
                    p6_w_aiz_torn_state  = (t->state != NULL) ? 1 : 0;
                    p6_w_aiz_tornado_frames = (int32)t->animatorTornado.frameCount;
                    p6_w_aiz_torn_animid    = (int32)t->animatorTornado.animationID;
                }
                ++tc;
            }
            p6_w_aiz_torn_count = tc;
        }
        if (AIZTornadoPath) p6_w_aiz_path_movevel = (int32)(AIZTornadoPath->moveVel.x);
        /* R3.4 (#306 follow-on): the AIZ cutscene actor anim-load latches. Object-level
         * aniFrames (set by each StageLoad's LoadSpriteAnimation) -- >=0 == the .bin
         * resolved from AIZOBJ.PAK; -1 == not in the pack (the pre-R3.4 failure). */
        {
            extern int32 p6_w_aiz_claw_aniframes, p6_w_aiz_eggrobo_aniframes;
            if (AIZKingClaw && AIZKingClaw->classID)
                p6_w_aiz_claw_aniframes = (int32)(int16)AIZKingClaw->aniFrames;
            if (AIZEggRobo && AIZEggRobo->classID)
                p6_w_aiz_eggrobo_aniframes = (int32)(int16)AIZEggRobo->aniFrames;
        }
        /* #308: the Tails(P2) beat-3 stall localiser -- player2(SLOT_PLAYER2) state. */
        {
            extern int32 p6_w_aiz_p2_classid, p6_w_aiz_p2_onground, p6_w_aiz_p2_posx,
                         p6_w_aiz_p2_posy, p6_w_aiz_p2_vely, p6_w_aiz_p2_sidekick, p6_w_aiz_p1_posy;
            extern int32 p6_w_aiz_p2_stateptr, p6_w_aiz_p2_inputptr, p6_w_aiz_p2_tilecoll, p6_w_aiz_p2_visible;
            EntityPlayer *p2 = RSDK_GET_ENTITY(SLOT_PLAYER2, Player);
            EntityPlayer *p1 = RSDK_GET_ENTITY(SLOT_PLAYER1, Player);
            if (p2) {
                p6_w_aiz_p2_classid  = (int32)p2->classID;
                p6_w_aiz_p2_onground = (int32)p2->onGround;
                p6_w_aiz_p2_posx     = (int32)(p2->position.x >> 16);
                p6_w_aiz_p2_posy     = (int32)(p2->position.y >> 16);
                p6_w_aiz_p2_vely     = (int32)p2->velocity.y;
                p6_w_aiz_p2_sidekick = (int32)p2->sidekick;
                p6_w_aiz_p2_stateptr = (int32)(int)(void *)p2->state;
                p6_w_aiz_p2_inputptr = (int32)(int)(void *)p2->stateInput;
                p6_w_aiz_p2_tilecoll = (int32)p2->tileCollisions;
                p6_w_aiz_p2_visible  = (int32)p2->visible;
            }
            if (p1) p6_w_aiz_p1_posy = (int32)(p1->position.y >> 16);
        }
        /* probe the path nodes via foreach (same mechanism that finds the tornado).
         * pn_count = # path entities; pn_active = count with active==ACTIVE_NORMAL; the
         * ACTIVE node's type/targetSpeed/speed/state localise whether Create's START branch
         * ran + serialized (type==0 START, tgtspd!=0 serialize OK, state set == Create ran). */
        if (AIZTornadoPath && AIZTornadoPath->classID) {
            extern int32 p6_w_aiz_pn_type, p6_w_aiz_pn_tgtspd, p6_w_aiz_pn_speed, p6_w_aiz_pn_state, p6_w_aiz_pn_active;
            /* enumerate ALL path nodes: pn_type = per-node type nibble-packed (node N ->
             * bits [4N,4N+3]); pn_active = per-node active nibble-packed; pn_tgtspd = the
             * START node's (type==0) targetSpeed; pn_state = the START node's state-set +
             * its active in the high byte; pn_speed = START node's speed. */
            int32 typemask = 0, actmask = 0, idx = 0, has_start = 0, first_x = -1;
            foreach_all(AIZTornadoPath, pn) {
                if (idx == 0) first_x = (int32)(pn->position.x >> 16);
                if (idx < 8) {
                    typemask |= ((int32)pn->type & 0xF) << (idx * 4);
                    actmask  |= ((int32)pn->active & 0xF) << (idx * 4);
                }
                if (pn->type == 0) { /* START node */
                    has_start = 1;
                    p6_w_aiz_pn_tgtspd = (int32)pn->targetSpeed;
                    p6_w_aiz_pn_speed  = (int32)pn->speed;
                    p6_w_aiz_pn_state  = ((pn->state != NULL) ? 1 : 0) | ((int32)pn->active << 8);
                }
                ++idx;
            }
            p6_w_aiz_pn_type   = typemask;
            /* pn_active: low 28 bits = the per-node active nibbles; bit 28 = has_start;
             * but keep it simple -- store actmask, and stash {count<<8|has_start<<4|first_x_lo}
             * in pn_speed is taken; reuse pn_state high bits already used. Add: encode
             * count + has_start into the unused top of pn_type? No -- use cam_x-adjacent.
             * Simplest: write first_x into pn_speed when no START (sentinel preserved else). */
            p6_w_aiz_pn_active = actmask | (idx << 24) | (has_start << 20);
            if (!has_start) { p6_w_aiz_pn_tgtspd = -77; p6_w_aiz_pn_speed = first_x; }
        }
        /* StarPost validity (the START-node gate): sp_ptr!=0 == the zeroed instance is wired;
         * sp_post0 == StarPost->postIDs[0] (must be 0 for the !postIDs[0] START branch). */
        {
            extern int32 p6_w_aiz_sp_ptr, p6_w_aiz_sp_post0;
            extern ObjectStarPost *StarPost;
            p6_w_aiz_sp_ptr = (int32)(uint32)(void *)StarPost;
            if (StarPost) p6_w_aiz_sp_post0 = (int32)StarPost->postIDs[0];
        }
    }
#endif
#if defined(P6_FRONTEND_MENU)
    /* M1 (M2/M3): latch the Menu classIDs once they resolve. Called from
     * p6_frontend_frame each tick (the api->witness_fn seam). M1 (folder_tag) is
     * set in p6_menu_reload; M4 (objcount) is latched in InitObjects (p6_io_main);
     * M5 (cont_frames) is bumped by p6_frontend_frame. Independent of any live
     * entity / crash -- just the registered Object globals' classIDs. */
    if (MenuSetup && MenuSetup->classID) p6_w_menusetup_classid = (int32)MenuSetup->classID;
    if (UIControl && UIControl->classID) p6_w_uicontrol_classid = (int32)UIControl->classID;
    /* M1b RENDER witnesses (the auth-gate + rows). M6: MenuSetup_Initialize wired the
     * row tree (mainMenu set by the foreach_all(UIControl){match "Main Menu"}) IFF
     * InitAPI returned true -- 0 while it stays in the pre-handshake branch (the M1a
     * RED), 1 once the tree is built. M6b: UIModeButton registered (classID>0) so the
     * 4 main-menu rows instantiate + draw. Rebuilt each tick (a snapshot, not a latch). */
    p6_w_menu_treebuilt       = (MenuSetup && MenuSetup->mainMenu) ? 1 : 0;
    if (UIModeButton && UIModeButton->classID) p6_w_menu_modebtn_classid = (int32)UIModeButton->classID;
    /* M2a LAYOUT measurement (qa_menu_layout.py): the 4 UIModeButton positions + their
     * world->screen mapping, the visible-UIControl count, and the active control's
     * position/tag. Fills the pack witnesses (ld -R import). The scroll origin
     * (currentScreen->position) is latched pack-side in p6_menu_layout_scroll_latch()
     * AFTER the draw lists run -- see p6_frontend_frame. */
    /* DIAGNOSTIC (always-on for the menu flavor): this block only READS positions + writes
     * witness ints (incl. p6_w_menu_force_scrx, which the pack force-CONSUMER ignores while
     * gated off). MEASURED-safe: it ran in the working layout-diagnosis build; the isolated
     * first-frame crash was the DrawFace plate emitter, not this measurement. Needed to get
     * the real row positions for the Saturn-native 320 layout design. */
    {
        extern int32 p6_w_menu_scrx, p6_w_menu_scry;
        extern int32 p6_w_menu_modebtn_px[4], p6_w_menu_modebtn_py[4];
        extern int32 p6_w_menu_modebtn_sx[4], p6_w_menu_modebtn_sy[4];
        extern int32 p6_w_menu_modebtn_act[4], p6_w_menu_modebtn_bid[4];
        extern int32 p6_w_menu_visctrls, p6_w_menu_actctrl_px, p6_w_menu_actctrl_py;
        extern int32 p6_w_menu_actctrl_tagh, p6_w_menu_ctrl_count;
        if (UIModeButton && UIModeButton->classID) {
            int32 mi = 0;
            foreach_all(UIModeButton, mb) {
                if (mi < 4) {
                    p6_w_menu_modebtn_px[mi]  = (int32)(mb->position.x >> 16);
                    p6_w_menu_modebtn_py[mi]  = (int32)(mb->position.y >> 16);
                    p6_w_menu_modebtn_sx[mi]  = (int32)(mb->position.x >> 16) - p6_w_menu_scrx;
                    p6_w_menu_modebtn_sy[mi]  = (int32)(mb->position.y >> 16) - p6_w_menu_scry;
                    p6_w_menu_modebtn_act[mi] = (int32)mb->active;
                    p6_w_menu_modebtn_bid[mi] = (int32)mb->buttonID;
                    ++mi;
                }
            }
        }
        if (UIControl && UIControl->classID) {
            extern int32 p6_w_menu_vis_tagh[4], p6_w_menu_vis_px[4], p6_w_menu_vis_py[4], p6_w_menu_vis_act[4];
            int32 vis = 0, total = 0, vi = 0;
            foreach_all(UIControl, c) {
                ++total;
                if (c->visible) {
                    if (vi < 4) {
                        uint32 hh = 5381; int32 kk;
                        for (kk = 0; kk < c->tag.length && kk < 24; ++kk)
                            hh = ((hh << 5) + hh) + (uint32)c->tag.chars[kk];
                        p6_w_menu_vis_tagh[vi] = (int32)hh;
                        p6_w_menu_vis_px[vi]   = (int32)(c->position.x >> 16);
                        p6_w_menu_vis_py[vi]   = (int32)(c->position.y >> 16);
                        p6_w_menu_vis_act[vi]  = (int32)c->active;
                        ++vi;
                    }
                    ++vis;
                }
                if (c->active == ACTIVE_ALWAYS) {
                    extern int32 p6_w_menu_force_scrx, p6_w_menu_force_scry;
                    p6_w_menu_actctrl_px = (int32)(c->position.x >> 16);
                    p6_w_menu_actctrl_py = (int32)(c->position.y >> 16);
                    /* M2b FIX: hand the active control's scroll origin to the pack so it
                     * FORCES currentScreen->position before the draw lists. Mirrors
                     * UIControl_Draw (UIControl.c:52-53): pos = FROM_FIXED(self->position) -
                     * center. The menu runs SetVideoSetting(SCREENCOUNT,1) full 320x224
                     * (MenuSetup_StageLoad:205), so ScreenInfo->center is (160,112) -- the
                     * fixed value, no ScreenInfo deref needed (keeps the overlay TU free of
                     * the game ScreenInfo extern). */
                    p6_w_menu_force_scrx = (int32)(c->position.x >> 16) - 160;
                    p6_w_menu_force_scry = (int32)(c->position.y >> 16) - 112;
                    /* djb2 of the tag chars (CompareStrings stores chars in tag.chars) */
                    {
                        uint32 h = 5381; int32 k;
                        for (k = 0; k < c->tag.length && k < 24; ++k)
                            h = ((h << 5) + h) + (uint32)c->tag.chars[k];
                        p6_w_menu_actctrl_tagh = (int32)h;
                    }
                }
            }
            p6_w_menu_visctrls   = vis;
            p6_w_menu_ctrl_count = total;
        }
    }
    /* M2 (qa_engine_menu_start.py) S1+S2. S1: UISaveSlot registered + classID resolved
     * (the start-game slot widget is live). S2: a STICKY OR of the live UIControl input
     * flags -- UIControl_ProcessInputs (UIControl.c:227-245) sets any{Confirm,Down,Up,Left,
     * Right}Press from ControllerInfo when an input slot fed by the Saturn pad presses. The
     * UIControl object's any*Press fields are global on ObjectUIControl, so reading them
     * here proves the menu tick SAW a real pad press (||-accumulate across frames -> once
     * set, stays set; a single injected press registers it). */
    if (UISaveSlot && UISaveSlot->classID) p6_w_menu_saveslot_classid = (int32)UISaveSlot->classID;
    if (UIControl) {
        if (UIControl->anyConfirmPress || UIControl->anyDownPress || UIControl->anyUpPress
            || UIControl->anyLeftPress || UIControl->anyRightPress || UIControl->anyBackPress)
            p6_w_menu_input_seen = 1;
    }
    /* S3 (Task #296): latch the live UITransition state machine -- localize the Mania Mode ->
     * Save Select stall (StartTransition -> State_TransitionIn -> State_TransitionOut ->
     * callback). NOT sticky: reflects the CURRENT state at capture (so a stuck transition is
     * visible). present==0 => Create never assigned activeTransition; state ptr -> map offline. */
    if (!UITransition)
        p6_w_uitrans_present = -2;
    else if (!UITransition->activeTransition)
        p6_w_uitrans_present = 0;
    else {
        EntityUITransition *t = (EntityUITransition *)UITransition->activeTransition;
        p6_w_uitrans_present = 1;
        p6_w_uitrans_state   = (int32)(size_t)t->state;
        p6_w_uitrans_timer   = t->timer;
        p6_w_uitrans_istrans = (int32)t->isTransitioning;
        p6_w_uitrans_active  = (int32)t->active;
    }
    /* S3 root-cause (Task #296): the active UIControl button's actionCB -- 0==NULL is the
     * UIButton_ProcessButtonCB_Scroll:397 stall (hasNoAction -> no selectedCB -> no transition). */
    {
        EntityUIControl *uc = UIControl_GetUIControl();
        if (uc) {
            p6_w_active_btn_id    = uc->buttonID;
            p6_w_active_btn_count = uc->buttonCount;
            p6_w_ctrl_posx        = uc->position.x;   // UISaveSlot_ProcessButtonCB:19 gate
            p6_w_ctrl_tgtx        = uc->targetPos.x;  // confirm only if posx==tgtx (settled)
            p6_w_ctrl_state       = (int32)(size_t)uc->state;   // cmp UIControl_ProcessInputs (0x26a1288) offline
            p6_w_ctrl_active      = (int32)uc->active;          // ACTIVE_ALWAYS=4 expected
            /* S3 (Task #296): the No-Save control is settled+active+actionCB-wired yet 5 confirm
             * taps fired NO SetScene. The break is upstream of actionCB. ProcessButtonCB only runs
             * (-> sees anyConfirmPress -> SelectedCB) when the control's state IS ProcessInputs.
             * Sticky-latch whether the 1-button control reached ProcessInputs, and whether a confirm
             * edge ever coincided with it -- pinpoints state-stuck vs confirm-not-seen vs downstream. */
            if (uc->buttonCount == 1 && (void *)uc->state == (void *)UIControl_ProcessInputs) {
                p6_w_nosave_pi = 1;
                if (UIControl->anyConfirmPress) {
                    p6_w_nosave_confirm = 1;
                    /* EXACT UISaveSlot_ProcessButtonCB:889 SelectedCB-call gate: posx==tgtx
                     * AFTER line 886 set targetPos.x = slot.position.x (end-of-frame read
                     * reflects the gate outcome -- nothing moves posx/tgtx after ProcessButtonCB). */
                    if (uc->position.x == uc->targetPos.x) {
                        p6_w_nosave_gate = 1;
                        /* UIControl.c:262 gates ProcessButtonInput on !selectionDisabled.
                         * If this is 1 at the gate+confirm frame, the confirm dispatch is SKIPPED. */
                        p6_w_gate_seldisabled = (int32)uc->selectionDisabled;
                    }
                }
                p6_w_ctrl_seldisabled = (int32)uc->selectionDisabled;
                if (uc->buttonID >= 0 && uc->buttons[uc->buttonID]) {
                    EntityUISaveSlot *ss = (EntityUISaveSlot *)uc->buttons[uc->buttonID];
                    p6_w_slot_state    = (int32)(size_t)ss->state;   // cmp UISaveSlot_State_Selected offline
                    p6_w_slot_fxradius = ss->fxRadius;               // >0 = State_Selected ramp started
                }
            }
            if (uc->buttonID >= 0 && uc->buttonID < uc->buttonCount && uc->buttons[uc->buttonID])
                p6_w_active_btn_actioncb = (int32)(size_t)uc->buttons[uc->buttonID]->actionCB;
        }
    }
#if defined(P6_MENU_AUTOSELECT)
    /* #296 DEBUG-INJECT (user-chosen): prove the No-Save start-game chain end-to-end on the
     * real build, bypassing the savestate harness's unreliable 20fps confirm-edge injection.
     * MEASURED already: nav (Mania Mode -> No-Save) + the confirm reaching the settle-gate with
     * every SelectedCB precondition true (nosave_gate=1) + selectionDisabled clears (capM). The
     * one un-captured link is SelectedCB -> State_Selected ramp -> actionCB -> SetScene. Drive it
     * directly: stage 0 -- once the main menu settled, MatchMenuTag("No Save Mode") (exactly what
     * MenuButton_ActionCB:928 does for Mania Mode when API_GetNoSave; immediate, no wipe, so the
     * No-Save control comes up active+ProcessInputs+selectionDisabled=false == the post-transition
     * state). stage 1 -- once that control settles, run UISaveSlot_SelectedCB on its slot. The
     * decomp State_Selected ramp then fires the slot actionCB (MenuSetup_SaveSlot_ActionCB) ->
     * RSDK.SetScene("Cutscenes","Angel Island Zone") -> p6_w_menu_startscene_tag = 0x4149. */
    {
        static int32 as_stage = 0;
        static int32 as_timer = 0;
        EntityUIControl *asuc = UIControl_GetUIControl();
        ++as_timer;
        if (as_stage == 0 && asuc && asuc->buttonCount > 1 && as_timer > 120) {
            UIControl_MatchMenuTag("No Save Mode");
            as_stage = 1;
            as_timer = 0;
        }
        else if (as_stage == 1 && asuc && asuc->buttonCount == 1 && as_timer > 20) {
            /* MEASURED: calling actionCB() directly from THIS witness block (render phase)
             * crashed (SH2-M PC=0x06000956, the #228 fault) -- MenuSetup_SaveSlot_ActionCB does a
             * SCENE CHANGE (RSDK.SetScene + RSDK.LoadScene) that is only safe from the engine's
             * main-loop ENGINESTATE context, NOT mid-render. So instead FORCE the slot into
             * State_Selected exactly as UISaveSlot_SelectedCB:950-954 does, and let the engine's
             * UISaveSlot_Update run the fxRadius ramp -> StateMachine_Run(actionCB) IN-CONTEXT.
             * The main-loop ENGINESTATE_LOAD hook then latches p6_w_menu_startscene_tag=0x4149
             * before the (M3-unported) AIZ load. */
            EntityUISaveSlot *asss = (EntityUISaveSlot *)asuc->buttons[0];
            if (asss && asss->actionCB) {
                asuc->state           = 0;                       /* SelectedCB:950 control->state = None */
                asss->isSelected      = 0;                       /* :952 */
                asss->currentlySelected = 0;                     /* :953 */
                asss->processButtonCB = 0;                       /* :954 */
                asss->timer           = 0;                       /* clean State_Selected ramp */
                asss->fxRadius        = 0;
                asss->state           = UISaveSlot_State_Selected; /* :951 -- engine drives the ramp */
                as_stage = 2;
            }
        }
        p6_w_as_stage = as_stage;
    }
#endif
#endif
    if (Spikes) p6_w_spikes_aniframes = (int32)(int16)Spikes->aniFrames;
    {   /* Batch 1: count how many of the 4 clean objects registered (classID>0). */
        int32 b1 = 0;
        if (Decoration && Decoration->classID) ++b1;
        if (ForceSpin && ForceSpin->classID) ++b1;
        if (SpinBooster && SpinBooster->classID) ++b1;
        p6_w_b1_registered = b1;
    }
    {   /* Batch 2: count how many of the 9 chain+badnik objects registered (classID>0),
         * and latch each one's classID for the per-object diagnostic. */
        extern int32 p6_w_b2_cids[9];
        int32 b2 = 0;
        p6_w_b2_cids[0] = (BadnikHelpers) ? (int32)BadnikHelpers->classID : -1;
        p6_w_b2_cids[1] = (Explosion)     ? (int32)Explosion->classID     : -1;
        p6_w_b2_cids[2] = (Animals)       ? (int32)Animals->classID       : -1;
        p6_w_b2_cids[3] = (Newtron)       ? (int32)Newtron->classID       : -1;
        p6_w_b2_cids[4] = (Crabmeat)      ? (int32)Crabmeat->classID      : -1;
        p6_w_b2_cids[5] = (BuzzBomber)    ? (int32)BuzzBomber->classID    : -1;
        p6_w_b2_cids[6] = (Chopper)       ? (int32)Chopper->classID       : -1;
        p6_w_b2_cids[7] = (Motobug)       ? (int32)Motobug->classID       : -1;
        p6_w_b2_cids[8] = (Batbrain)      ? (int32)Batbrain->classID      : -1;
        for (int i = 0; i < 9; ++i) if (p6_w_b2_cids[i] > 0) ++b2;
        p6_w_b2_registered = b2;
    }
    /* Batch 2 anim-load latches (range-independent; -1 == LoadSpriteAnimation failed,
     * which on STG-overflow is the R13 sentinel for these slow-path anims). */
    if (Explosion) p6_w_explosion_aniframes = (int32)(int16)Explosion->aniFrames;
    if (Animals)   p6_w_animals_aniframes   = (int32)(int16)Animals->aniFrames;
    if (Newtron)   p6_w_newtron_aniframes   = (int32)(int16)Newtron->aniFrames;

    /* RANGE-INDEPENDENT anim-load status, every tick, straight off the Object
     * struct -- the definitive "did StageLoad's LoadSpriteAnimation succeed"
     * signal. (int16)-cast so a -1 (0xFFFF) load failure reads as -1, not 65535
     * (which would falsely pass a >0 gate). This is what makes an unloaded anim
     * impossible to miss regardless of where the camera/player is. */
    if (Spring)   p6_w_spring_aniframes   = (int32)(int16)Spring->aniFrames;
    if (Bridge)   p6_w_brg_aniframes      = (int32)(int16)Bridge->aniFrames;
    if (SpikeLog) p6_w_spikelog_aniframes = (int32)(int16)SpikeLog->aniFrames;

    /* Spring canary (#254 funding): first Spring's animator.frames. */
    static int32 s_spring_latched = 0;
    if (Spring && Spring->classID) {
        p6_w_spring_classid = (int32)Spring->classID;
        if (!s_spring_latched) {
            EntitySpring *sp = NULL;
            foreach_all(Spring, e) { sp = e; break; }
            if (sp) { p6_w_spring_frames = (int32)(size_t)sp->animator.frames; s_spring_latched = 1; }
        }
    }

    /* Bridge (#181): classid live; count/pos/frames one-shot (planks exist from
     * frame 1). brg_frames 0 == Bridge.bin alloc-failed; the regression sentinel. */
    static int32 s_brg_latched = 0;
    if (Bridge && Bridge->classID) {
        p6_w_brg_classid = (int32)Bridge->classID;
        if (!s_brg_latched) {
            int32 cnt = 0; EntityBridge *first = NULL;
            foreach_all(Bridge, b) { ++cnt; if (!first) first = b; }
            if (cnt > 0) {
                p6_w_brg_count    = cnt;
                p6_w_brg_posx     = first->position.x;
                p6_w_brg_posy     = first->position.y;
                p6_w_brg_onscreen = (int32)first->onScreen;
                p6_w_brg_frames   = (int32)(size_t)first->animator.frames;
                s_brg_latched     = 1;
            }
        }
    }

    /* PlaneSwitch (#254 loop): bit 3 = the loop fix; pscount = # placed (expect 106). */
    static int32 s_loop_latched = 0;
    if (!s_loop_latched) {
        p6_w_loop_regmask = (PlaneSwitch && PlaneSwitch->classID) ? 0x08 : 0;
#if defined(P6_STREAM_PROOF)
        /* PERF (2026-06-20, MEASURED): pscount is a foreach_all FULL-POOL scan. Under the camera-local
         * pool a PlaneSwitch may NEVER be near (nearest is x=3352, >2200px past the x~108 spawn window)
         * so this latch NEVER fires -> the scan ran EVERY shipping frame == ~half the 9.65ms "tail"
         * (qa_p6_perf), the perf-diagnostic-in-hotloop regression the streaming introduced. pscount is
         * consumed ONLY by qa_p6_stream_in (the P6_STREAM_PROOF warp build, where PlaneSwitches ARE near
         * and it DOES latch). Shipping uses regmask (above) for R3 / qa_p6_loop, so the scan is pure
         * diagnostic overhead there -> compile it out of shipping. */
        if (PlaneSwitch && PlaneSwitch->classID) {
            int32 cnt = 0;
            foreach_all(PlaneSwitch, ps) { ++cnt; }
            if (cnt > 0) { p6_w_loop_pscount = cnt; s_loop_latched = 1; }
        }
#endif
    }

    /* SpikeLog (O3 step 1): classid live; first instance's animator.frames latched
     * (0 == GHZ/SpikeLog.bin anim failed to load; >0 == the spike log is armed). */
    static int32 s_sl_latched = 0;
    if (SpikeLog && SpikeLog->classID) {
        p6_w_spikelog_classid = (int32)SpikeLog->classID;
#if defined(P6_STREAM_PROOF)
        /* PERF: same regression as PlaneSwitch -- SpikeLog (min x=2632) is never near the x~108 spawn
         * so this latch never fires -> foreach_all scanned EVERY shipping frame (the other ~half of the
         * tail). spikelog_frames is diag-only (qa_p6_ghz_regression checks classid above + the
         * load-status aniframes witness, NOT this latch) -> strip from shipping, keep for diag builds. */
        if (!s_sl_latched) {
            EntitySpikeLog *sl = NULL;
            foreach_all(SpikeLog, e) { sl = e; break; }
            if (sl) { p6_w_spikelog_frames = (int32)(size_t)sl->animator.frames; s_sl_latched = 1; }
        }
#endif
    }

    /* BADNIK-VIS: live draw-state scan. Walk every badnik type; count live entities;
     * latch the FIRST one that is on-screen (or, failing that, the first live one) and
     * record its full draw state so the decision tree resolves in one capture. Each
     * badnik Entity has its `animator` field; GetFrame(aniFrames, animID, frameID) is
     * stride-safe (->frames is opaque here). The handle accessor reads the pack table.
     * Non-sticky every frame so the LATEST on-screen badnik is what the capture sees.
     *
     * PERF (2026-06-18): this 6x-foreach_all scan ran EVERY shipping frame and cost
     * ~19ms in-motion -- it HALVED fps (48->20) until measured (qa_p6_perf M8/M9). It
     * is PURE DIAGNOSTIC (feeds qa_p6_ghz_regression R16 only). Compile-strip it from
     * the shipping build via P6_PERF_NOSCAN (set by build_shipping.sh) -- the SAME flag
     * that strips the census in p6_io_main.cpp. R14/R15 (arm_env bind witnesses) still
     * prove GHZ/Objects.gif binds in shipping; R16 (a LIVE badnik's handle) is diag-
     * only and reads the -1 sentinel below as "skipped". */
#ifndef P6_PERF_NOSCAN
    {
        int32 found = 0;
        /* Macro: scan one badnik type's live entities in its OWN block scope (foreach_all
         * declares Entity##OBJP *_b -- a fresh _b per block avoids redeclaration). Latch
         * the first live entity, preferring an on-screen one. */
        #define BD_SCAN(OBJP, AFW)                                                     \
            if (OBJP && (OBJP)->classID) {                                             \
                foreach_all(OBJP, _b) {                                                \
                    ++found;                                                           \
                    int32 onscr = (int32)_b->onScreen;                                 \
                    if (p6_w_bd_classid < 0 || (onscr && p6_w_bd_onscreen <= 0)) {     \
                        p6_w_bd_classid  = (int32)_b->classID;                         \
                        p6_w_bd_posx     = (int32)(_b->position.x >> 16);              \
                        p6_w_bd_posy     = (int32)(_b->position.y >> 16);              \
                        p6_w_bd_onscreen = onscr;                                      \
                        p6_w_bd_visible  = (int32)_b->visible;                         \
                        p6_w_bd_drawgrp  = (int32)_b->drawGroup;                       \
                        p6_w_bd_active   = (int32)_b->active;                          \
                        p6_w_bd_framesNN = (_b->animator.frames != NULL) ? 1 : 0;      \
                        p6_w_bd_animid   = (int32)_b->animator.animationID;            \
                        p6_w_bd_frameid  = (int32)_b->animator.frameID;                \
                        SpriteFrame *_fr = RSDK.GetFrame((AFW),                        \
                            _b->animator.animationID, _b->animator.frameID);           \
                        p6_w_bd_sheetid  = _fr ? (int32)_fr->sheetID : -1;             \
                        p6_w_bd_handle   = _fr ? p6_vdp1_handle_for_surface(_fr->sheetID) : -4; \
                    }                                                                  \
                }                                                                      \
            }
        /* reset the latch each frame so an on-screen badnik (when one exists) wins */
        p6_w_bd_classid = -1; p6_w_bd_onscreen = -1;
        BD_SCAN(Motobug,    Motobug->aniFrames)
        BD_SCAN(Newtron,    Newtron->aniFrames)
        BD_SCAN(Crabmeat,   Crabmeat->aniFrames)
        BD_SCAN(BuzzBomber, BuzzBomber->aniFrames)
        BD_SCAN(Chopper,    Chopper->aniFrames)
        BD_SCAN(Batbrain,   Batbrain->aniFrames)
        #undef BD_SCAN
        p6_w_bd_found = found;
    }
#else
    /* Shipping (P6_PERF_NOSCAN): the per-frame badnik scan is stripped from the hot
     * loop. The -1 sentinel tells qa_p6_ghz_regression R16 to SKIP (diag-only); the
     * R14/R15 bind-table checks (arm_env witnesses) still prove the surface binds. */
    p6_w_bd_found = -1;
#endif
}

// =============================================================================
// I3b 2b: the camera-local-pool MATERIALIZE (overlay-resident). Reconstructs scene entity
// `logical_slot` from the cart DORM store (0x226C8000; big-endian header/index/records + raw LE
// Scene.bin var-bytes) into `dest_slot`. Mirrors the proven pack logic (qa_p6_materialize_write
// GREEN @ 55c77e5) EXACTLY -- only the home moved (pack -> cart, residency rule). The overlay does
// the DORM navigation + LE var-replay (raw offset writes); the ENGINE-touching ops are pack thunks.
// =============================================================================
static unsigned int   p6m_be32(const unsigned char *p) { return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) | ((unsigned int)p[2] << 8) | p[3]; }
static unsigned short p6m_be16(const unsigned char *p) { return (unsigned short)(((unsigned int)p[0] << 8) | p[1]); }
static unsigned int   p6m_le32(const unsigned char *p) { return ((unsigned int)p[3] << 24) | ((unsigned int)p[2] << 16) | ((unsigned int)p[1] << 8) | p[0]; }
static unsigned short p6m_le16(const unsigned char *p) { return (unsigned short)(((unsigned int)p[1] << 8) | p[0]); }

// I3b 2b STREAMING (load phase, overlay-resident). Camera-local NEAR-shrink: relocate the camera-NEAR
// populated scene entities (p6_scan_near, seeded for the spawn camera) into a dense pool [R,R+nNear),
// DORMANT the FAR ones (remap -> the reserved dummy at R+SCENE_PHYS-1), make the free slots
// [R+nNear,R+SCENE_PHYS-1) inert (materialize-targets for the per-frame stream, Build 2), shift temp
// down, flip p6_pool_scene_phys to SCENE_PHYS=640. Relocation reuses the PROVEN compaction byte-plan
// (near is a subset; qa_p6_pool_compact_model). LOAD-ONLY (no per-frame stream yet) -> this SHRINKS the
// pool (qa_p6_pool_shrink GREEN, maxslot<768) but DROPS entities the camera later reaches (R0-R16 RED
// baseline); Build 2's per-frame materialize/dormant turns R0-R16 GREEN. The pack seeds p6_scan_near via
// p6_scan_update_near(spawn camX) before calling this.
static void p6_ovl_pool_compact(void)
{
    int32 g[7];
    unsigned char  *base, *src, *dst, *e, *pe;
    unsigned short *remap, *inv;
    unsigned int    SCN_BASE;
    int CIDOFF, NARROW, R, SCN, TEMP, WIDE, SP;
    int n, lastL, dummy, L, k, t, i, ok, P, isnear;

    p6_eng_pool_geom(g);
    CIDOFF = (int)g[0]; NARROW = (int)g[1]; R = (int)g[2]; SCN = (int)g[3]; TEMP = (int)g[4]; WIDE = (int)g[5];
    SP = (int)g[6];     /* P6_POOL_SCENE_PHYS = 640 (the shrunk physical scene-slot count) */
    base  = (unsigned char *)0x00243000u;        /* P6_LW_ENTITYLIST (WRAM-L, cached, master SH-2) */
    remap = (unsigned short *)0x226B8000u;        /* p6_pool_remap (cache-through cart) */
    inv   = (unsigned short *)0x226BC000u;        /* p6_pool_remap_inv */
    SCN_BASE = (unsigned int)R * (unsigned int)WIDE;
    dummy = R + SP - 1; /* reserved classID=0 dummy = the LAST scene-physical slot (kept OUT of the free
                           pool so a future materialize never overwrites it) */
    n = 0; lastL = -1;

    /* Pass A -- classify NEAR-and-populated scene slots ascending into a dense pool. p6_scan_near already
       includes the always-iterate set (NORMAL/managers), so they are kept resident. */
    for (L = R; L < R + SCN; ++L) {
        e = base + SCN_BASE + (unsigned int)(L - R) * (unsigned int)NARROW;
        isnear = (p6_scan_near[L >> 3] >> (L & 7)) & 1;
        if (isnear && *(unsigned short *)(e + CIDOFF)) {
            remap[L]   = (unsigned short)(R + n);
            inv[R + n] = (unsigned short)L;
            lastL = L; ++n;
        } else {
            remap[L] = (unsigned short)0xFFFFu;    /* far or empty -> dummy in A2 */
        }
    }
    /* A2 -- far/empty logical slots -> the reserved dummy. */
    for (L = R; L < R + SCN; ++L)
        if (remap[L] == (unsigned short)0xFFFFu) remap[L] = (unsigned short)dummy;
    /* Pass B -- relocate near ascending (dst<=src, per-entity non-overlap -> forward byte copy; proven). */
    for (k = 0; k < n; ++k) {
        L   = (int)inv[R + k];
        src = base + SCN_BASE + (unsigned int)(L - R) * (unsigned int)NARROW;
        dst = base + SCN_BASE + (unsigned int)k * (unsigned int)NARROW;
        if (dst != src) for (i = 0; i < NARROW; ++i) dst[i] = src[i];
    }
    /* Pass C -- make every NON-near scene-physical slot inert: [R+n, R+SP) = the free pool + the dummy.
       loop1 iterates the whole physical scene region, so a STALE classID here would be a phantom entity
       -> zero the classID (2 B) + set inv=self (a safe in-bounds _L for loop1's near-cull index). */
    for (P = R + n; P < R + SP; ++P) {
        pe = base + SCN_BASE + (unsigned int)(P - R) * (unsigned int)NARROW;
        *(unsigned short *)(pe + CIDOFF) = 0;
        inv[P] = (unsigned short)P;
    }
    /* Pass D -- shift temp 1:1 DOWN to [R+SP, R+SP+TEMP). */
    for (t = 0; t < TEMP; ++t) {
        src = base + SCN_BASE + (unsigned int)SCN * (unsigned int)NARROW + (unsigned int)t * (unsigned int)WIDE;
        dst = base + SCN_BASE + (unsigned int)SP  * (unsigned int)NARROW + (unsigned int)t * (unsigned int)WIDE;
        if (dst != src) for (i = 0; i < WIDE; ++i) dst[i] = src[i];
        remap[R + SCN + t] = (unsigned short)(R + SP + t);
        inv[R + SP + t]    = (unsigned short)(R + SCN + t);
    }
    /* STREAMING init: the free pool = [R+n, R+SP-1) (the inert slots Pass C made, EXCLUDING the reserved
       dummy at R+SP-1). Push them onto the free-list stack; zero the lifecycle (destroyed) bitfield. The
       resident set is the spawn-near [R,R+n) (already placed). The per-frame p6_ovl_stream uses these. */
    {
        unsigned short *fl   = (unsigned short *)P6_STREAM_FREELIST;
        int            *fc   = (int *)P6_STREAM_FREECNT;
        unsigned char  *life = (unsigned char *)P6_STREAM_LIFE;
        unsigned short *rl   = (unsigned short *)P6_STREAM_RESIDLIST;  /* I3b 2b PERF #2: resident logical-slot list */
        int            *rc   = (int *)P6_STREAM_RESIDCNT;
        int f = 0, q;
        for (q = R + n; q < R + SP - 1; ++q) fl[f++] = (unsigned short)q;
        *fc = f;
        for (q = 0; q < (SCN + 7) / 8; ++q) life[q] = 0;
        /* I3b 2b PERF #2 (scan narrowing): seed the resident-list with the n spawn-near residents [R,R+n).
           Their logical slots are inv[R+0..R+n-1] (Pass A set inv[R+k]=L). The per-frame stream's DORMANT/
           RETIRE pass iterates THIS ~42-entry list instead of all 1088 scene slots. */
        for (q = 0; q < n; ++q) rl[q] = inv[R + q];
        *rc = n;
    }
    /* witnesses (compact_n is now the spawn NEAR count; sphys is the shrunk 640). */
    p6_w_compact_n     = n;
    p6_w_compact_sphys = SP;
    p6_w_compact_dummy = dummy;
    p6_w_compact_lastL = lastL;
    p6_w_compact_lastP = (lastL >= 0) ? (int32)remap[lastL] : -1;
    /* light bij self-check: dummy inert + the highest-near entity at the last dense slot. The real
       gameplay catch is qa_p6_ghz_regression R0-R16 (RED here without the per-frame stream). */
    ok = 1;
    if (*(unsigned short *)(base + SCN_BASE + (unsigned int)(dummy - R) * (unsigned int)NARROW + CIDOFF) != 0) ok = 0;
    if (lastL >= 0 && (int)remap[lastL] != R + n - 1) ok = 0;
    p6_w_compact_bij_ok = ok;
    /* ATOMIC FLIP (pack thunk, LAST) -- scene_phys = SCENE_PHYS (640). */
    p6_eng_pool_flip(SP, dummy);
}

static void p6_ovl_materialize(unsigned logical_slot, unsigned dest_slot)
{
    const unsigned char *D = (const unsigned char *)0x226C8000u; // cart DORM store
    if (p6m_be32(D) != 0x4D443650u) return;                      // 'P6DM'
    unsigned short slot_count   = p6m_be16(D + 6);
    unsigned short obj_count    = p6m_be16(D + 8);
    unsigned int   slot_idx_off = p6m_be32(D + 12);
    unsigned int   recs_off     = p6m_be32(D + 16);
    if (logical_slot >= slot_count) return;
    unsigned int rec = p6m_be32(D + slot_idx_off + logical_slot * 4u);
    if (rec == 0xFFFFFFFFu) return;
    const unsigned char *R = D + recs_off + rec;
    unsigned short obj_idx = p6m_be16(R + 0);
    int px = (int)p6m_be32(R + 4);
    int py = (int)p6m_be32(R + 8);
    const unsigned char *vb = R + 12;
    if (obj_idx >= obj_count) return;

    // navigate the variable-length object table: each = hash16 | nvars u8 | nvars*(hash16|type u8)
    const unsigned char *O = D + 20;
    for (unsigned short i = 0; i < obj_idx; ++i) { unsigned char nv = O[16]; O += 17u + (unsigned int)nv * 17u; }
    const unsigned char *obj_hash_le = O;
    unsigned char nvars = O[16];
    const unsigned char *attribs = O + 17;

    p6_w_mat_slot    = (int32)logical_slot;
    int classID      = p6_eng_classid_resolve(obj_hash_le); // pack thunk (also latches classcount)
    p6_w_mat_classid = classID;
    p6_w_mat_nvars   = (int32)nvars;
    if (!classID) return;                                   // unregistered -> skip

    p6_eng_serialize_begin(classID);                        // pack thunk: rebuild editableVarList
    unsigned char *eb = (unsigned char *)p6_eng_entity_prepare((int32)dest_slot);
    p6_eng_write_placement((void *)eb, classID, px, py);
    p6_w_mat_posx = px >> 16;
    p6_w_mat_posy = py >> 16;

    // replay var values: per attrib, match hash -> offset (pack thunk), LE-decode the raw bytes
    // (byte-wise = alignment-safe), write into the matched field by offset (no engine types here).
    const unsigned char *vp = vb;
    int nmatch = 0, vi = 0, wv[4] = { 0, 0, 0, 0 };
    for (unsigned short a = 0; a < nvars; ++a) {
        int off = p6_eng_var_offset(attribs + (unsigned int)a * 17u);
        unsigned char vt = attribs[(unsigned int)a * 17u + 16];
        int val = 0, have = 0;
        switch (vt) {
            case 0: case 3: // VAR_UINT8 / VAR_INT8 (1 byte)
                val = (vt == 3) ? (int)(signed char)vp[0] : (int)vp[0]; vp += 1; have = 1;
                if (off >= 0) eb[off] = (unsigned char)val;
                break;
            case 1: case 4: // VAR_UINT16 / VAR_INT16 (2)
                val = (vt == 4) ? (int)(short)p6m_le16(vp) : (int)p6m_le16(vp); vp += 2; have = 1;
                if (off >= 0) *(short *)(eb + off) = (short)val;
                break;
            case 2: case 5: case 6: case 7: case 10: case 11: // U32/I32/ENUM/BOOL/FLOAT/COLOR (4)
                val = (int)p6m_le32(vp); vp += 4; have = 1;
                if (off >= 0) *(int *)(eb + off) = val;
                break;
            case 9: { // VAR_VECTOR2 (8)
                int vx = (int)p6m_le32(vp), vy = (int)p6m_le32(vp + 4); vp += 8; val = vx; have = 1;
                if (off >= 0) { *(int *)(eb + off) = vx; *(int *)(eb + off + 4) = vy; }
                break;
            }
            case 8: { unsigned short ln = p6m_le16(vp); vp += 2u + (unsigned int)ln * 2u; break; } // STRING skip
            default: break;
        }
        if (off >= 0) ++nmatch;
        if (have && vi < 4) { wv[vi] = val; ++vi; }
    }
    p6_eng_serialize_end();                                  // pack thunk: restore editableVarList
    p6_w_mat_nmatch = nmatch;
    p6_w_mat_v0 = wv[0]; p6_w_mat_v1 = wv[1]; p6_w_mat_v2 = wv[2]; p6_w_mat_v3 = wv[3];
    // I3b 2b STREAMING: re-CREATE the entity. Placement alone is INERT (no animator/state machine); Create
    // runs the object's spawn setup so a re-materialized entity is LIVE. Mirrors InitObjects (the gap the
    // qa_p6_materialize_write gate did NOT cover -- it only checked placement M1-M5).
    p6_eng_create((int32)dest_slot);
}

// I3b 2b STREAMING per-frame manager. Runs EVERY frame from ProcessObjects (after p6_scan_update_near
// rebuilt p6_scan_near for the LIVE camera, BEFORE loop1). One pass over the scene logical range:
//   - gameplay-destroyed (resident but classID==0) -> retire: set lifecycle bit, free the slot, dormant.
//   - newly-near + not-resident + not-destroyed -> MATERIALIZE: pop a free physical slot, re-Create from
//     DORM into it (live), remap[L]=P, inv[P]=L.
//   - newly-far + resident -> DORMANT: make the slot inert, push it to the free-list, remap[L]=dummy.
// Guarded by p6_w_compact_n>=0 (the load-near-shrink ran -> pool is shrunk). The materialize + relocate +
// dummy are PROVEN; this diff/free-list/lifecycle + the mid-frame Create are the new code -> R3 RED->GREEN.
static void p6_ovl_stream(void)
{
    int32 g[7];
    unsigned char  *base, *pe;
    unsigned short *remap, *inv, *fl;
    unsigned char  *life;
    int *fc;
    unsigned int SCN_BASE;
    int CIDOFF, NARROW, R, SCN, WIDE, SP, dummy, L, P, isnear, res;

    if (p6_w_compact_n < 0) return;   /* the load-near-shrink has not run -> not shrunk -> no-op */
    p6_eng_pool_geom(g);
    CIDOFF = (int)g[0]; NARROW = (int)g[1]; R = (int)g[2]; SCN = (int)g[3]; WIDE = (int)g[5]; SP = (int)g[6];
    base  = (unsigned char *)0x00243000u;
    remap = (unsigned short *)0x226B8000u;
    inv   = (unsigned short *)0x226BC000u;
    fl    = (unsigned short *)P6_STREAM_FREELIST;
    fc    = (int *)P6_STREAM_FREECNT;
    life  = (unsigned char *)P6_STREAM_LIFE;
    SCN_BASE = (unsigned int)R * (unsigned int)WIDE;
    dummy = R + SP - 1;
    res = 0;

#ifdef P6_BACKTRACK_PROOF
    /* BACKTRACK PROOF (diag-only): once the stream has settled (call 30), synthetically DESTROY the
       first resident scene entity -- set classID=0, EXACTLY how RSDK destroyEntity/ResetEntitySlot
       signals a kill (collected ring, broken badnik). The main loop below then RETIRES it (lifecycle
       bit, free, dummy); on every later frame it stays NEAR (camera fixed) but dormant -> the
       skip-destroyed path must keep it dead. p6_w_bt_logical (set LAST) arms the post-loop recorder.
       RED demo: -DP6_BT_NOSKIP forces materialize -> the dead entity RE-MATERIALIZES (reappear=1). */
    {
        static int s_bt_frame = 0;
        ++s_bt_frame;
        if (p6_w_bt_logical < 0 && s_bt_frame == 30) {
            int tl;
            for (tl = R; tl < R + SCN; ++tl) {
                int tp = (int)remap[tl];
                if (tp != dummy) {
                    unsigned char *te = base + SCN_BASE + (unsigned int)(tp - R) * (unsigned int)NARROW;
                    unsigned short cid = *(unsigned short *)(te + CIDOFF);
                    if (cid != 0) {
                        p6_w_bt_cid = (int32)cid;
                        *(unsigned short *)(te + CIDOFF) = 0;  /* the synthetic gameplay destroy */
                        p6_w_bt_logical = tl;                  /* set LAST -- arms the recorder */
                        break;
                    }
                }
            }
        }
    }
#endif

    /* I3b 2b PERF #2 (scan narrowing) -- the old full scan walked all 1088 scene slots EVERY frame
       (3.512 ms = 32.4% of ProcessObjects, qa_p6_streamscan). Replace it with two passes over only the
       ~83 active slots: DORMANT/RETIRE over the resident-list, then MATERIALIZE by byte-scanning the
       WRAM-H near bitfield. Per-slot logic is BYTE-IDENTICAL to the old scan; only the iteration changed.
       The always-iterate set (managers/NORMAL) is resident from the load-near-shrink + kept by Pass A's
       `isnear` (its p6_scan_near bit is always set); it appears in the byte-scan too but is skipped resident. */
    {
        unsigned short *rl  = (unsigned short *)P6_STREAM_RESIDLIST;
        int            *rc  = (int *)P6_STREAM_RESIDCNT;
        int k, w, b, bit, lo_b = R >> 3, hi_b = (R + SCN + 7) >> 3;
        unsigned int bv;

        /* PASS A -- DORMANT/RETIRE first (frees slots for the materialize below). Walk the resident-list,
           in-place swap-compacting survivors. classID==0 -> gameplay-destroyed -> RETIRE (lifecycle bit +
           free); no longer near -> DORMANT (free); else KEEP. */
        w = 0;
        for (k = 0; k < *rc; ++k) {
            L  = (int)rl[k];
            P  = (int)remap[L];
            pe = base + SCN_BASE + (unsigned int)(P - R) * (unsigned int)NARROW;
            isnear = (p6_scan_near[L >> 3] >> (L & 7)) & 1;
            if (*(unsigned short *)(pe + CIDOFF) == 0) {       /* destroyed -> retire permanently */
                /* lifecycle bitfield indexed by the SCENE slot (L - R), NOT bare L -- sized
                   (SCENEENTITY_COUNT+7)/8; a bare-L index overflows by RESERVE/8 into un-zeroed cart for
                   the top RESERVE slots (qa_p6_lifecycle_index, static, deterministic). */
                life[(L - R) >> 3] |= (unsigned char)(1 << ((L - R) & 7));
                fl[(*fc)++] = (unsigned short)P;
                remap[L] = (unsigned short)dummy;
                ++p6_w_stream_dorm;                            /* retire counts as a dorm for the witness */
            } else if (!isnear) {                              /* newly far -> dormant */
                *(unsigned short *)(pe + CIDOFF) = 0;
#if !defined(P6_POOLINV_LEAK)
                fl[(*fc)++] = (unsigned short)P;               /* return the freed slot to the pool */
#endif                                                         /* P6_POOLINV_LEAK: skip the return -> LEAK (RED demo) */
                remap[L] = (unsigned short)dummy;
                ++p6_w_stream_dorm;
            } else {
                rl[w++] = (unsigned short)L;                   /* still near + live -> keep */
            }
        }
        *rc = w;

        /* PASS B -- MATERIALIZE: byte-scan the WRAM-H near bitfield p6_scan_near (the overlay links it via
           -R; the pack rebuilt it just before this tick). Skip-zero-byte -> ~136 WRAM-H reads for a sparse
           near set instead of 1088 cart remap reads. Each set bit is a near logical slot; materialize it if
           dormant + not destroyed, then append to the resident-list. */
        for (b = lo_b; b < hi_b; ++b) {
            bv = p6_scan_near[b];
            if (!bv) continue;
            for (bit = 0; bit < 8; ++bit) {
                if (!(bv & (1u << bit))) continue;
                L = (b << 3) + bit;
                if (L < R || L >= R + SCN) continue;           /* scene slots only */
                if ((int)remap[L] != dummy) continue;          /* already resident (Pass A handled retire) */
#if defined(P6_BT_NOSKIP)
                if (1) {  /* BACKTRACK RED demo (diag): ignore the lifecycle bit -> a destroyed entity RE-MATERIALIZES */
#else
                if (!(life[(L - R) >> 3] & (1 << ((L - R) & 7)))) {  /* not destroyed (scene-slot L-R index) */
#endif
                    if (*fc > 0) {
                        P = (int)fl[--(*fc)];
                        remap[L] = (unsigned short)P;
                        inv[P]   = (unsigned short)L;
                        /* DORM is indexed by the raw Scene.bin slotID = L-RESERVE; dest_slot=L ->
                           RSDK_ENTITY_AT(L)=remap[L]=P -> the entity is written + Created at physical P. */
                        p6_ovl_materialize((unsigned)(L - R), (unsigned)L);
                        ++p6_w_stream_mat;
                        rl[(*rc)++] = (unsigned short)L;        /* add to the resident-list */
                    } else {
                        ++p6_w_stream_starve;                  /* free-list empty (SP undersized -- gate) */
                    }
                }
            }
        }
        res = *rc;
    }
    p6_w_stream_free     = *fc;
    p6_w_stream_resident = res;

    /* POOL-INVARIANT GUARD (always-on, ~1 compare/frame): every physical scene slot [R,R+SP) is exactly
       one of resident / free / the single dummy, so resident + free == SP - 1 EVERY frame. A free-list
       leak (a dormant that fails to return its slot) or a double-free breaks it. p6_w_pool_inv_bad is a
       STICKY latch -> a transient or multi-cycle violation that self-corrects by capture-time is still
       caught. Permanent self-check: peek p6_w_pool_inv_bad from any savestate if entities ever vanish
       after scrolling. RED demo: -DP6_POOLINV_LEAK (above) skips the dormant return. Gate qa_p6_stream_in S4. */
    if (res + *fc != SP - 1) p6_w_pool_inv_bad = 1;

#ifdef P6_BACKTRACK_PROOF
    /* RECORD (post-loop, every frame once armed): did the destroyed entity get RETIRED (life bit) and
       does it STAY dead (remap == dummy)?  life=1 + reappear=0 == the lifecycle works. Under
       -DP6_BT_NOSKIP the dead entity re-materialized this frame -> remap != dummy -> reappear=1 (RED). */
    if (p6_w_bt_logical >= 0) {
        int tl = (int)p6_w_bt_logical;
        p6_w_bt_life     = (int32)((life[(tl - R) >> 3] >> ((tl - R) & 7)) & 1);
        p6_w_bt_reappear = (remap[tl] != (unsigned short)dummy) ? 1 : 0;
    }
#endif
}
