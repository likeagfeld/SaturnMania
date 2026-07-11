#!/usr/bin/env bash
# =============================================================================
# build_shipping.sh -- P6.8 Step B (Task #211): build the LEAN ENGINE SHIPPING
# image. Identical to build_diag.sh EXCEPT the jo make knob is
# P6_ENGINE_SHIPPING=1 (not P6SCENE=1): main.c boots p6_engine_boot_and_run()
# straight into a continuously-running GHZ -- no diagnostic burst/proofs/Title
# reload/legacy Ring. The engine pack (p6_scene_pack.o) is the SAME object as
# the diag flavor; build_p6scene_objs.sh now -u-roots p6_engine_boot_and_run so
# the lean entry survives the pack gc. The Ring OVERLAY (OVLRING.BIN) is still
# loaded + registered by the shared masked load core (p6_scene_run step 1.5), so
# the overlay link stage is kept.
#
# Run INSIDE the Docker toolchain image:
#   MSYS_NO_PATHCONV=1 docker run --rm -v "D:/sonicmaniasaturn":/work -w /work \
#       joengine-saturn:latest bash tools/_portspike/_p6/build_shipping.sh
#
# #182 BGM CD-DA (HOST post-step -- this Docker toolchain image has NO python):
# the `make` CueMaker emits a DATA-ONLY single-track game.cue, so the GHZ stage
# BGM (PlayStream("GreenHill1.ogg") -> HandleStreamLoad -> CUE audio track 2) has
# no CD-DA track to play. After this build, run ON THE HOST (mirrors build.bat:16):
#   python tools/build_cdda.py cd_audio/track02.wav cd_audio/track03.wav \
#       --cue-out game.cue --iso game.iso
# -> rewrites game.cue multi-track: TRACK 01 (game.iso) + TRACK 02 AUDIO
#    (cd_audio/track02.bin = GreenHill1/GHZ) + TRACK 03 AUDIO (track03.bin = title).
#
# The do-not-touch COMMON SGL tree is untouched; the engine-sized SGL work area
# (SaturnSGLArea.c) replaces stock SGLAREA.O via the make SYSOBJS override, same
# as the diag (the engine pack does not use the 3D sortlist capacity the stock
# area sizes for). The flavor-switch rm (p6-5b3 memory) clears the
# shipping-flavored intermediates so a knob switch cannot reuse a hybrid image.
# =============================================================================
set -eu

CC=/work/jo-engine/Compiler/LINUX/bin/sh-none-elf-gcc-8.2.0

# #246 (MEASURED): the SHIPPING production frame SKIPS the diagnostic census +
# hog-locator + ActClear/SignPost scans in p6_ghz_frame -- two full
# ENTITY_COUNT(1216)-slot entity-table scans = a measured 5.08ms tail of
# read-only diagnostics with ZERO render/gameplay effect. Cutting them flipped
# GHZ fps 29.92 -> 48.91 in-motion (the diagnostic-tail experiment, commit
# 3e57818, PROVED the 30fps was master compute-overrun, not VDP1). Default-ON for
# shipping, overridable: build `-e P6_NOSCAN=` (empty) to profile the shipping
# flavor WITH the in-range/hog/sign witnesses. The diag flavor (make P6SCENE=1)
# always keeps them. set -u-safe: ${VAR-default} expands only when UNSET.
export P6_NOSCAN="${P6_NOSCAN-1}"
# #254 anim-pool funding (shipping-only): relocate DATASET_TMP's 80 KB backing to the
# 4MB cart (0x22730000, MEASURED-disjoint from the resident sheets / GFS windows /
# sheet store) so the freed WRAM-L grows DATASET_STG 92->150 KB -- ending the STG
# anim-pool overflow that vanished the bridges when objects were added (Storage.cpp
# P6_CART_TMP arm; gated by qa_p6_animpool.py). The diag flavor (build_diag.sh) does
# NOT set this, so it stays byte-identical (no re-validation of its gate sweep).
export P6_CART_TMP="${P6_CART_TMP-1}"

# DDWrecker boss (2026-07-11): P6_DDW_ARENA (the arena-warp bring-up diagnostic)
# IMPLIES P6_DDWRECKER (the boss must be registered to fight it). Export both so
# the child build_p6scene_objs.sh (line ~184) compiles the register block + the
# arena warp + the witness -u roots. Both are diag-only; plain/plain-chain leave
# them unset -> byte-identical. Export P6_DDWRECKER too so the child inherits it
# even when passed only via docker -e (already in env, but make the intent explicit).
# P6_DDW_KILL (the defeat-injection diagnostic) IMPLIES P6_DDW_ARENA (warp to the
# arena) which implies P6_DDWRECKER (the boss).
if [ -n "${P6_DDW_KILL:-}" ]; then
    export P6_DDW_ARENA=1
    export P6_DDW_KILL
fi
if [ -n "${P6_DDW_ARENA:-}" ]; then
    export P6_DDWRECKER=1
    export P6_DDW_ARENA
fi
[ -n "${P6_DDWRECKER:-}" ] && export P6_DDWRECKER

# Task #309 (P6_GHZCUT_BOOT): the AIZ->GHZCutscene direct-boot diagnostic BUILDS ON the
# AIZ-test flavor (it reuses the AIZ overlay objects + the AIZ FG present block + the
# closure_edge AIZ stubs), and additionally registers GHZSetup/BGSwitch/GHZCutsceneST/
# CutsceneHBH + boots straight to GHZCutscene. Set P6_AIZ_TEST FIRST so the full
# AIZ->MENU->TITLE->LOGOS cascade below fires and every ${P6_AIZ_TEST:+...} thread (the
# AIZ overlay OVL_FE, the AIZ witness grep, the pack -DP6_AIZ_TEST) engages.
# build_p6scene_objs.sh (invoked as a child below, inheriting this exported env) self-
# implies P6_FRONTEND_MENU from P6_AIZ_TEST. The GHZCUT-specific bits are added explicitly
# (the 4 Game_*.o in OVL_FE, the 2 symbol greps). Default GHZ leaves both unset.
# Task #309 Tier-B.1 (P6_GHZCUT_HOLD): the FXRuby-fade RED-gate capture flavor. It
# FREEZES the cutscene at a fixed visible white wash (the overlay pins fxRuby->fadeWhite)
# so a savestate captures the fade on-screen. Implies P6_GHZCUT_DIRECTBOOT (the held
# captures all DIRECT-BOOT straight to GHZCutscene), -> P6_GHZCUT_BOOT -> P6_AIZ_TEST.
# Default GHZ + plain AIZ + plain GHZCUT-boot leave it unset -> byte-identical.
if [ -n "${P6_GHZCUT_HOLD:-}" ]; then
    export P6_GHZCUT_HOLD   # ensure the child build_p6scene_objs.sh inherits it
    export P6_GHZCUT_DIRECTBOOT=1
    # Optional: override the held wash intensity (default 256 = full white). A
    # partial value (e.g. 144) ramps a ~56% wash so the FG composites THROUGH it,
    # proving the VDP2 color offset is variable-alpha (not a fixed blank).
    [ -n "${P6_GHZCUT_HOLD_WHITE:-}" ] && export P6_GHZCUT_HOLD_WHITE
fi
# Task #309 gate-2 (P6_GHZCUT_DIRECTBOOT): the DIRECT-BOOT diagnostic flavor -- boots
# STRAIGHT to GHZCutscene (the boot-dispatch p6_ghzcut_reload + the reload fn def are the
# ONLY two code sites gated by this flag). DECOUPLED from P6_GHZCUT_BOOT so the LIVE-loop
# build (P6_GHZCUT_BOOT alone) boots the normal menu->AIZ path and the AIZ intro's own
# beat-9 LoadGHZ reaches GHZCutscene through the ENGINESTATE_LOAD seam. Implies
# P6_GHZCUT_BOOT (-> the support: objects/assets/fade/Heavies/seam-routing). The gate-1/
# B.1/B.2 captures use this flag (so they reproduce byte-identically).
if [ -n "${P6_GHZCUT_DIRECTBOOT:-}" ]; then
    export P6_GHZCUT_DIRECTBOOT  # ensure the child build_p6scene_objs.sh inherits it
    export P6_GHZCUT_BOOT=1
fi
# Task #309 gate-2 (P6_GHZCUT_SEAMTEST): the LIVE-SEAM RED-gate capture flavor. Boots the
# LIVE menu->AIZ path (P6_GHZCUT_BOOT support on, NOT direct-boot), then injects a ONE-SHOT
# AIZ->GHZCutscene transition (the exact AIZSetup_Cutscene_LoadGHZ SetScene the beat-9 fires)
# at a fixed cont-frame count -> exercises the ENGINESTATE_LOAD AIZ->GHZCutscene seam in
# seconds instead of the ~minutes 4fps AIZ playthrough (the proven P6_TRANSITION_TEST
# act-advance pattern). Implies P6_GHZCUT_BOOT (the support) but NOT DIRECTBOOT (so the live
# boot path runs). Default leaves it unset -> byte-identical.
if [ -n "${P6_GHZCUT_SEAMTEST:-}" ]; then
    export P6_GHZCUT_SEAMTEST    # ensure the child build_p6scene_objs.sh inherits it
    export P6_GHZCUT_BOOT=1
fi
# Task #309 gate-2 FOLLOW-UP (P6_GHZCUT_NOFIX): the A-B RED-proof knob for the live
# render-clean fix (the p6_fg_blank_char_override reset). NOFIX disables BOTH reset
# sites in p6_io_main.cpp so the SEAMTEST live render reproduces the pre-fix RED
# (checkerboard bleed). Implies P6_GHZCUT_SEAMTEST (the live seam) -> P6_GHZCUT_BOOT.
# Default unset -> the fix is ON (and the no-flag GHZ build never sees it).
if [ -n "${P6_GHZCUT_NOFIX:-}" ]; then
    export P6_GHZCUT_NOFIX       # ensure the child build_p6scene_objs.sh inherits it
    export P6_GHZCUT_SEAMTEST=1
    export P6_GHZCUT_BOOT=1
fi
if [ -n "${P6_GHZCUT_BOOT:-}" ]; then
    export P6_AIZ_TEST=1
fi
# M3.1 (qa_p6_aiz_cutscene): the AIZ-test flavor IMPLIES the MENU flavor (it boots the
# AIZ/GHZCutscene scene through the same p6_frontend_frame machinery + the Menu witness
# threading + the shared MENU->TITLE->LOGOS front-end stack). Set P6_FRONTEND_MENU here so
# the make knob (${P6_FRONTEND_MENU:+P6_FRONTEND_MENU=1}, lines below) + the MENU overlay
# OVL_FE + the MENU symbol grep all fire for the AIZ/GHZCUT build. build_p6scene_objs.sh
# self-implies it too (line 66-68). Default GHZ leaves it unset -> byte-identical.
if [ -n "${P6_AIZ_TEST:-}" ]; then
    export P6_FRONTEND_MENU=1
fi

# M1 (qa_engine_menu): the MENU front-end flavor IMPLIES the TITLE flavor (which
# implies LOGOS, below). The Menu scene reuses every shared front-end thread (the
# frontend_frame UI tick, the SaturnSheet store, the VDP1 box); the only Menu-specific
# bits are the make knob P6_FRONTEND_MENU=1, the 8 Menu objects in OVL_FE, and the
# Menu witness grep. Set this FIRST so the title self-imply below then sets LOGOS ->
# the full cascade fires. Default GHZ leaves all four unset.
if [ -n "${P6_FRONTEND_MENU:-}" ]; then
    export P6_FRONTEND_TITLE=1
fi
# CP5c (Task #270): the front-end FLOW CHAIN flavor IMPLIES the TITLE flavor (which
# implies LOGOS, just below). The chain reuses every shared front-end thread (the
# Title overlay objects, the SaturnSheet 12-slot store, the VDP1 box); the only
# chain-specific bit is the make knob P6_FRONTEND_CHAIN=1 + the -DP6_FRONTEND_CHAIN
# the pack build self-implies for p6_io_main.cpp + the chain witness grep. Set this
# FIRST so the title self-imply below then sets LOGOS -> the full cascade fires.
# Default GHZ leaves all three unset.
if [ -n "${P6_FRONTEND_CHAIN:-}" ]; then
    export P6_FRONTEND_TITLE=1
    # #315 game-speed fix (audit-1 headline): the chain runs logic at 60 Hz via the
    # decomp-verbatim multi-tick (RetroEngine.cpp:392-412 shape; N = elapsed
    # vblanks, clamped P6_TICK_CAP=4). Default ON for the chain; A/B with
    # P6_TICK_CATCHUP="" (explicit empty). Plain GHZ + single-scene flavors
    # untouched (byte-identical).
    export P6_TICK_CATCHUP="${P6_TICK_CATCHUP-1}"
    # #316 F1 (contract step 1): direct VDP1 command list -- every front-end
    # sprite/fill emits into our double-buffered command block (p6_vdp1.c),
    # linked into SGL's preamble chain by the vblank trampoline (p6_vdp2.c).
    # The SGL sprite pipeline carries ZERO sprites -> the measured transfer
    # tear (title 73% torn, landing empty plans) has nothing to break.
    # Default ON for the chain; A/B with P6_DIRECT_VDP1="" (explicit empty).
    export P6_DIRECT_VDP1="${P6_DIRECT_VDP1-1}"
    # #325 stage-1: fork the GHZ-landing FG present COMPUTE onto the slave SH-2
    # (p6_present_kick/join, the plain-GHZ-proven A2 mechanism) from the front-end
    # frame -- crosses the 4->3 vblank quantization tier (15->20+ fps cascade).
    # Default ON for the chain; A/B with P6_FE_SLAVE_PRESENT="" (explicit empty).
    export P6_FE_SLAVE_PRESENT="${P6_FE_SLAVE_PRESENT-1}"
fi
# CP5a (Task #267): the TITLE front-end flavor IMPLIES the LOGOS flavor (it reuses
# every shared #if defined(P6_FRONTEND_LOGOS) machinery -- frontend_frame, the VDP1
# box in p6_vdp1.c, p6_vdp2_arm_sprites_only, the SaturnSheet slot bump). Setting
# P6_FRONTEND_LOGOS=1 here makes the existing ${P6_FRONTEND_LOGOS:+...} threads in
# THIS script (the make knob + the overlay OVL_FE + the symbol grep) fire, and is
# also seen by build_p6scene_objs.sh (which self-implies it too). The Title-specific
# bits (the make knob P6_FRONTEND_TITLE=1 for p6_vdp1.c, the Title overlay objects,
# the Title witness grep) are added explicitly. Default GHZ leaves both unset.
if [ -n "${P6_FRONTEND_TITLE:-}" ]; then
    export P6_FRONTEND_LOGOS=1
    # CP5b.5 (Task #275): default the verbatim TitleBG parallax VDP1 sprites
    # (Mountain1/2, Reflection, WaterSparkle, WingShine) OFF. They THRASH the 10-slot
    # Title VDP1 cache (FG ~9 distinct rects + the TitleBG on-screen rects exceed
    # P6_VDP1_NSLOTS=10 -> LRU evicts the FG slots -> the Sonic/logo foreground breaks
    # up). MEASURED: 25/40 settled frames had a broken FG (qa_title_fg_stable.py),
    # savestate p6_w_vdp1_evicts=273. The title backdrop stays the Saturn-native VDP2
    # NBG1 sky+cloud+island present (zero VDP1 slot cost), so the FG keeps all 10
    # slots in every frame. Re-enable the TitleBG sprites for an A/B bisect by setting
    # P6_TITLEBG_SPRITES_OFF="" (explicit empty). This MUST be set BEFORE step [1/5]
    # so build_p6scene_objs.sh compiles p6_ovl_ghz.o with -DP6_TITLEBG_SPRITES_OFF
    # (else its TitleBG registration links while Game_TitleBG.o is excluded from
    # OVL_FE below -> undefined reference).
    export P6_TITLEBG_SPRITES_OFF="${P6_TITLEBG_SPRITES_OFF-1}"
fi

echo "[1/5] proof pack (engine TUs, ld -r gc-pack; -u p6_engine_boot_and_run root) ..."
bash /work/tools/_portspike/_p6/build_p6scene_objs.sh > /dev/null

echo "[2/5] engine-sized SGL area block ..."
$CC -x c -std=gnu99 -m2 -O2 -fno-builtin \
    -I/work/jo-engine/Compiler/COMMON/SGL_302j/INC \
    -I/work/jo-engine/Compiler/WINDOWS/sh-elf/include \
    -c -o /work/platform/Saturn/SaturnSGLArea.o \
    /work/platform/Saturn/SaturnSGLArea.c

echo "[3/5] jo image (P6_ENGINE_SHIPPING flavor, flavor-switch rm, SYSOBJS override) ..."
cd /work
# jo's malloc pool is flavor-dependent (engine flavor = 32 KB, hand-port =
# 256 KB) and the jo-side P6 TUs compile under make with NO CCFLAGS dependency
# -- rm the flavor-sensitive intermediates so a knob switch can never reuse the
# wrong pool size / a stale p6_vdp1.o (jo-pool-stale-core-o-gotcha + the W12b
# hybrid-image rule).
rm -f src/main.o jo-engine/jo_engine/core.o game.elf game.map \
      tools/_portspike/_p6/p6_vdp1.o tools/_portspike/_p6/p6_snd.o
make P6_ENGINE_SHIPPING=1 ${P6_FRONTEND_LOGOS:+P6_FRONTEND_LOGOS=1} ${P6_FRONTEND_TITLE:+P6_FRONTEND_TITLE=1} ${P6_FRONTEND_CHAIN:+P6_FRONTEND_CHAIN=1} ${P6_FRONTEND_MENU:+P6_FRONTEND_MENU=1} ${P6_GHZCUT_BOOT:+P6_GHZCUT_BOOT=1} ${P6_FRAMEDIR:+P6_FRAMEDIR=1} SYSOBJS=platform/Saturn/SaturnSGLArea.o

echo "[3b/5] Ring OVERLAY (P6.7d.3): fixed-base link vs game.elf -> cd/OVLRING.BIN ..."
LD=/work/jo-engine/Compiler/LINUX/sh-none-elf/bin/ld
OBJCOPY=/work/jo-engine/Compiler/LINUX/bin/sh-none-elf-objcopy
P6=/work/tools/_portspike/_p6
cd "$P6"   # ovl_ring.ld names input objects by basename (the build_diag rule)
# O1 (Task #254): GHZ multi-class overlay -- p6_ovl_ghz.o (entry, FIRST = window
# base) + p6_ring2.o (Ring harness) + Game_Spring.o (Spring, moved out of the pack).
# Spring's internal refs resolve among these; RSDK table/Player/Zone import from
# game.elf via -R. p6_ovl_ring.o is RETIRED (its single-class entry superseded).
# BATCH 2 (badnik break chain): the 3 CHAIN TUs (BadnikHelpers/Explosion/Animals)
# AND the 6 GHZ1 badniks ALL join the overlay. This is the NEEDS_FORWARD resolution
# (merged harvest verdict): Animals_CheckGroundCollision references Bridge_Handle-
# Collisions (Game_Bridge.o, overlay) so Animals MUST be overlay-resident to resolve
# it intra-overlay -- it cannot live in the pack. The pack->overlay edge (Game_Player.o
# Player_CheckBadnikBreak calls BadnikHelpers_BadnikBreakUnseeded) is bridged by a
# forward stub in p6_closure_edge.c that routes pack->overlay via a runtime fn pointer
# (set after the overlay entry runs), the #258b Ring_LoseRings pattern verbatim. The
# badniks' Player_CheckBadnik*/ProjectileHurt refs import from game.elf via -R (pack,
# -u rooted). Explosion/Animals register from the overlay entry (overlay-resident).
# CP4 (Task #266): the front-end flavor adds the Logos splash objects to the
# overlay so they link against game.elf via -R (same as every other overlay obj).
# Default (GHZ) flavor leaves OVL_FE empty -> the overlay is byte-identical.
OVL_FE=""
if [ -n "${P6_FRONTEND_LOGOS:-}" ]; then
    OVL_FE="Game_LogoSetup.o Game_UIPicture.o"
fi
# CP5a (Task #267): the Title flavor adds the Title scene objects to the overlay so
# they link against game.elf via -R (same as every other overlay obj). They are
# ADDITIVE to the Logos objects (P6_FRONTEND_TITLE implies P6_FRONTEND_LOGOS, so the
# Logos objects compile + register too -- inert on the Title scene, no placements).
if [ -n "${P6_FRONTEND_TITLE:-}" ]; then
    # CP5b.2 (#269): + Game_TitleSonic.o (the ring-center head + finger-wave). Links
    # into the overlay against game.elf via -R like every other overlay obj.
    OVL_FE="$OVL_FE Game_TitleSetup.o Game_TitleLogo.o Game_TitleSonic.o"
    # CP5b.4 (#272): Game_TitleBG.o (mountains/water/wing-shine) + Game_Title3DSprite.o
    # (the MountainL/M/S/Tree/Bush perspective billboards) now link BY DEFAULT in the
    # Title flavor. The MEASURED destabilizer was the per-frame scanline-callback
    # INSTALL in TitleBG_SetupFX (SetClipBounds corrupted the Saturn FG sprite clip);
    # that install is now #if-guarded OUT on the Saturn/Title build (verbatim TitleBG.c,
    # P6_FRONTEND_TITLE). The VDP2 sky+cloud+island backdrop stays independent; these
    # objects add the island mountains/water/billboards via the proven DrawSprite path.
    # P6_TITLEBG_SPRITES_OFF restores the gated-off state (A/B bisect).
    if [ -z "${P6_TITLEBG_SPRITES_OFF:-}" ]; then
        OVL_FE="$OVL_FE Game_TitleBG.o Game_Title3DSprite.o"
    fi
fi
# M1 (qa_engine_menu): the Menu flavor adds the 7 Menu UI objects + the closure TU
# to the overlay so they link against game.elf via -R (same as every other overlay
# obj). They are ADDITIVE to the Title/Logos objects (P6_FRONTEND_MENU implies
# P6_FRONTEND_TITLE -> LOGOS, so those compile + register too -- inert on the Menu
# scene, no placements). p6_menu_closure.o resolves the 68-symbol UI/API link cone.
if [ -n "${P6_FRONTEND_MENU:-}" ]; then
    # M1b: + Game_UIModeButton.o (the 4 main-menu rows). Links into the overlay vs
    # game.elf (-R) like every other Menu UI obj.
    # M2: + Game_UISaveSlot.o (the save-select start-game slot) + Game_UITransition.o (the
    # mode-button -> Save-Select wipe; UIModeButton_SelectedCB runs the actionCB only through
    # it). Both link into the overlay vs game.elf (-R) like every other Menu UI obj.
    OVL_FE="$OVL_FE Game_MenuSetup.o Game_UIControl.o Game_UIBackground.o Game_UIButton.o Game_UIModeButton.o Game_UIWidgets.o Game_UISubHeading.o Game_UIButtonPrompt.o Game_UISaveSlot.o Game_UITransition.o p6_menu_closure.o"
fi
# M3.1 (qa_p6_aiz_cutscene): the AIZ flavor adds the 8 AIZ DRIVER objects to the overlay
# so they link against game.elf via -R (same as every other overlay obj). ADDITIVE to the
# Menu objects (P6_AIZ_TEST implies P6_FRONTEND_MENU -> TITLE -> LOGOS, so those compile +
# register too -- inert on the AIZ scene). Game_Decoration.o is ALREADY in the base link
# line below (the GHZ batch), so it is NOT added here. Their cross-class refs (Camera/
# Music/Player/MathHelpers/StarPost) import from game.elf via -R.
if [ -n "${P6_AIZ_TEST:-}" ]; then
    OVL_FE="$OVL_FE Game_AIZSetup.o Game_CutsceneSeq.o Game_AIZTornado.o Game_AIZTornadoPath.o Game_AIZKingClaw.o Game_AIZEggRobo.o Game_PhantomRuby.o Game_FXRuby.o"
fi
# Task #309 (P6_GHZCUT_BOOT): the 2 NEW AIZ->GHZCutscene driver TUs join the overlay so
# they link against game.elf via -R like every other overlay obj. GHZSetup/BGSwitch are
# NOT added here -- they are PACK-resident (the base link line below: Game_BGSwitch.o
# Game_GHZSetup.o), so the overlay's register_object_full((void**)&GHZSetup,...) +
# GHZSetup_Update/... symbols import from game.elf via -R (the same way the AIZ overlay
# imports Camera/Music/Zone). Adding them to OVL_FE would duplicate their .text in both
# link units. GHZCutsceneST/CutsceneHBH are overlay-ONLY (not in the pack list) -> they
# MUST be here. Their cross-class refs (Player/Camera/Music/Zone/PhantomRuby/FXRuby/
# CutsceneRules_SetupEntity) import from game.elf via -R.
if [ -n "${P6_GHZCUT_BOOT:-}" ]; then
    OVL_FE="$OVL_FE Game_GHZCutsceneST.o Game_CutsceneHBH.o"
fi
# DDWrecker GHZ1 boss (2026-07-11): the placed boss at (15792,1588) whose defeat
# fires DDWrecker_State_SpawnSignpost -> SignPost_State_Falling -> Spin -> ActClear
# (the natural end-of-act signpost spin). Gated behind P6_DDWRECKER during bring-up
# so plain GHZ stays BYTE-IDENTICAL (the overlay is shared by plain-GHZ + chain).
# Overlay-resident (like the 6 badniks): references Explosion (overlay) + Player_
# CheckBossHit/Music/Camera/Zone/SignPost (pack via -R). Window grown 0x28000->
# 0x30000 for its ~9.8 KB (p6_ovl_api.h). Promote to default once (a)-(c) GREEN.
if [ -n "${P6_DDWRECKER:-}" ]; then
    OVL_FE="$OVL_FE Game_DDWrecker.o"
fi
$LD -b elf32-sh -T ovl_ring.ld -Map ovl_ring.map \
    p6_ovl_ghz.o Game_Ring.o Game_Spring.o Game_Bridge.o Game_PlaneSwitch.o Game_SpikeLog.o Game_Spikes.o \
    Game_Decoration.o Game_ForceSpin.o Game_SpinBooster.o \
    Game_BadnikHelpers.o Game_Explosion.o Game_Animals.o \
    Game_Newtron.o Game_Crabmeat.o Game_BuzzBomber.o Game_Chopper.o Game_Motobug.o Game_Batbrain.o \
    Game_ItemBox.o Game_Debris.o Game_InvincibleStars.o Game_Platform.o Game_InvisibleBlock.o \
    $OVL_FE \
    -b coff-sh -R /work/game.elf -o ovl_ring.elf
$OBJCOPY -O binary "$P6/ovl_ring.elf" /work/cd/OVLRING.BIN
ls -l /work/cd/OVLRING.BIN
cd /work

echo "[4/5] re-master the ISO with the overlay on disc ..."
rm -f game.iso
make P6_ENGINE_SHIPPING=1 ${P6_FRONTEND_LOGOS:+P6_FRONTEND_LOGOS=1} ${P6_FRONTEND_TITLE:+P6_FRONTEND_TITLE=1} ${P6_FRONTEND_CHAIN:+P6_FRONTEND_CHAIN=1} ${P6_FRONTEND_MENU:+P6_FRONTEND_MENU=1} ${P6_GHZCUT_BOOT:+P6_GHZCUT_BOOT=1} ${P6_FRAMEDIR:+P6_FRAMEDIR=1} SYSOBJS=platform/Saturn/SaturnSGLArea.o

echo "[5/5] sanity: _end + lean-boot entry + flavor flag + overlay entry ..."
grep " _end = " game.map
grep -m1 " _p6_engine_boot_and_run" game.map || true
grep -m1 " _p6_lean_boot" game.map || true
grep -m1 "p6_overlay_entry" "$P6/ovl_ring.map" || true
# CP4 (Task #265/#266): in the front-end flavor, confirm the new witnesses landed
# in game.map (build_p6scene_objs.sh SWALLOWS compile errors -> a stale .o would
# leave them ABSENT; that is the silent-fail signature this grep catches) + the
# Logos objects landed in the overlay.
if [ -n "${P6_FRONTEND_LOGOS:-}" ]; then
    echo "[5b] CP4 front-end symbol presence:"
    grep -m1 "p6_w_frontend_folder_tag$" game.map || echo "  MISSING p6_w_frontend_folder_tag (compile failed silently?)"
    grep -m1 "p6_w_logosetup_classid$"   game.map || echo "  MISSING p6_w_logosetup_classid"
    grep -m1 "p6_w_uipicture_classid$"   game.map || echo "  MISSING p6_w_uipicture_classid"
    grep -m1 "p6_w_logos_objcount$"      game.map || echo "  MISSING p6_w_logos_objcount"
    grep -m1 "p6_w_uipicture_aniframes$" game.map || echo "  MISSING p6_w_uipicture_aniframes (FE render-diag block compiled out?)"
    grep -m1 "p6_w_uipic_handle$"        game.map || echo "  MISSING p6_w_uipic_handle (CP4c blue-screen diag compiled out?)"
    grep -m1 "p6_w_logos_shtslot$"       game.map || echo "  MISSING p6_w_logos_shtslot (arm_env Logos scan compiled out?)"
    grep -m1 "LogoSetup_Update"  "$P6/ovl_ring.map" || echo "  MISSING LogoSetup in overlay"
    grep -m1 "UIPicture_Update"  "$P6/ovl_ring.map" || echo "  MISSING UIPicture in overlay"
fi
# CP5a (Task #267): in the Title flavor, confirm the Title witnesses landed in
# game.map (build_p6scene_objs.sh SWALLOWS compile errors -> a stale .o leaves them
# ABSENT; this grep catches the silent-fail) + the Title objects landed in the overlay.
if [ -n "${P6_FRONTEND_TITLE:-}" ]; then
    echo "[5c] CP5a Title front-end symbol presence:"
    grep -m1 "p6_w_titlesetup_classid$" game.map || echo "  MISSING p6_w_titlesetup_classid (compile failed silently?)"
    grep -m1 "p6_w_titlelogo_classid$"  game.map || echo "  MISSING p6_w_titlelogo_classid"
    grep -m1 "p6_w_title_objcount$"     game.map || echo "  MISSING p6_w_title_objcount"
    grep -m1 "TitleSetup_Update" "$P6/ovl_ring.map" || echo "  MISSING TitleSetup in overlay"
    grep -m1 "TitleLogo_Update"  "$P6/ovl_ring.map" || echo "  MISSING TitleLogo in overlay"
    # CP5b.1 (Task #268): the render-diag witnesses (Title-sheet bind chain). MISSING
    # tlogo_handle == the render-diag block / TLOGO.SHT staging compiled out silently.
    grep -m1 "p6_w_tlogo_handle$"   game.map || echo "  MISSING p6_w_tlogo_handle (CP5b.1 render-diag compiled out?)"
    grep -m1 "p6_w_tlogo_shtslot$"  game.map || echo "  MISSING p6_w_tlogo_shtslot (TLOGO.SHT arm-env scan compiled out?)"
    # CP5b.2 (Task #269): the TitleSonic render-diag witnesses + the registered object.
    # MISSING tsonic_handle == the render-diag block / TSONIC.SHT staging compiled out;
    # MISSING TitleSonic_Update in the overlay == Game_TitleSonic.o not linked (stale .o
    # / OVL_FE miss). Either silent-fails the head; this grep is the trip-wire.
    grep -m1 "p6_w_tsonic_handle$"  game.map || echo "  MISSING p6_w_tsonic_handle (CP5b.2 render-diag compiled out?)"
    grep -m1 "p6_w_tsonic_shtslot$" game.map || echo "  MISSING p6_w_tsonic_shtslot (TSONIC.SHT arm-env scan compiled out?)"
    grep -m1 "TitleSonic_Update" "$P6/ovl_ring.map" || echo "  MISSING TitleSonic in overlay (Game_TitleSonic.o not linked?)"
fi
# M1 (qa_engine_menu): in the Menu flavor, confirm the Menu witnesses landed in
# game.map (build_p6scene_objs.sh SWALLOWS compile errors -> a stale p6_io_main.o /
# Game_MenuSetup.o leaves them ABSENT; this grep is the silent-fail trip-wire) + the
# Menu UI objects landed in the overlay.
if [ -n "${P6_FRONTEND_MENU:-}" ]; then
    echo "[5f] M1 Menu front-end symbol presence:"
    grep -m1 "p6_w_menusetup_classid$" game.map || echo "  MISSING p6_w_menusetup_classid (compile failed silently?)"
    grep -m1 "p6_w_uicontrol_classid$" game.map || echo "  MISSING p6_w_uicontrol_classid"
    grep -m1 "p6_w_menu_objcount$"     game.map || echo "  MISSING p6_w_menu_objcount"
    grep -m1 "MenuSetup_Update"    "$P6/ovl_ring.map" || echo "  MISSING MenuSetup in overlay (Game_MenuSetup.o not linked -- REV02 sed/compile fail?)"
    grep -m1 "UIControl_Update"    "$P6/ovl_ring.map" || echo "  MISSING UIControl in overlay"
    grep -m1 "UIBackground_Update" "$P6/ovl_ring.map" || echo "  MISSING UIBackground in overlay"
    grep -m1 "p6_w_menu_edge_calls" "$P6/ovl_ring.map" || echo "  MISSING p6_w_menu_edge_calls (closure TU not linked into overlay?)"
    # M2: the start-game path TUs + witnesses (silent-fail trip-wire).
    grep -m1 "UISaveSlot_Update"   "$P6/ovl_ring.map" || echo "  MISSING UISaveSlot in overlay (Game_UISaveSlot.o not linked?)"
    grep -m1 "UITransition_Update" "$P6/ovl_ring.map" || echo "  MISSING UITransition in overlay (Game_UITransition.o not linked?)"
    grep -m1 "p6_w_menu_startscene_tag$" game.map || echo "  MISSING p6_w_menu_startscene_tag (M2 start-game witness compiled out?)"
    grep -m1 "p6_w_menu_saveslot_classid$" game.map || echo "  MISSING p6_w_menu_saveslot_classid (M2 S1 witness compiled out?)"
fi
# M3.1 (qa_p6_aiz_cutscene): in the AIZ flavor, confirm the M3.1 cutscene-driver
# witnesses landed in game.map (build_p6scene_objs.sh SWALLOWS compile errors -> a stale
# p6_io_main.o / Game_AIZSetup.o leaves them ABSENT; this grep is the silent-fail trip-
# wire) + the AIZ driver objects landed in the overlay.
if [ -n "${P6_AIZ_TEST:-}" ]; then
    echo "[5g] M3.1 AIZ cutscene-driver symbol presence:"
    grep -m1 "p6_w_aiz_cutscene_state$" game.map || echo "  MISSING p6_w_aiz_cutscene_state (compile failed silently?)"
    grep -m1 "p6_w_aiz_setup_classid$"  game.map || echo "  MISSING p6_w_aiz_setup_classid"
    grep -m1 "p6_w_aiz_seq_classid$"    game.map || echo "  MISSING p6_w_aiz_seq_classid"
    grep -m1 "p6_w_aiz_cam_x$"          game.map || echo "  MISSING p6_w_aiz_cam_x"
    grep -m1 "AIZSetup_StaticUpdate"       "$P6/ovl_ring.map" || echo "  MISSING AIZSetup in overlay (Game_AIZSetup.o not linked?)"
    grep -m1 "CutsceneSeq_LateUpdate"      "$P6/ovl_ring.map" || echo "  MISSING CutsceneSeq in overlay (Game_CutsceneSeq.o not linked?)"
    grep -m1 "AIZTornadoPath_Create"       "$P6/ovl_ring.map" || echo "  MISSING AIZTornadoPath in overlay (Game_AIZTornadoPath.o not linked?)"
    grep -m1 "AIZTornado_Create"           "$P6/ovl_ring.map" || echo "  MISSING AIZTornado in overlay (Game_AIZTornado.o not linked?)"
fi
# Task #309 (P6_GHZCUT_BOOT): confirm the 2 NEW GHZCutscene driver TUs linked into the
# overlay (build_p6scene_objs.sh SWALLOWS compile errors -> a stale/absent Game_*.o leaves
# them MISSING; this grep is the silent-fail trip-wire) + the GHZCutscene reload reached the
# pack (its boot dispatch is compiled under -DP6_GHZCUT_BOOT). GHZSetup/BGSwitch are pack-
# resident (no overlay symbol); their registration imports via -R.
if [ -n "${P6_GHZCUT_BOOT:-}" ]; then
    echo "[5h] Task #309 GHZCutscene driver symbol presence:"
    grep -m1 "GHZCutsceneST_Create" "$P6/ovl_ring.map" || echo "  MISSING GHZCutsceneST_Create in overlay (Game_GHZCutsceneST.o not linked / compile failed silently?)"
    grep -m1 "CutsceneHBH_Create"   "$P6/ovl_ring.map" || echo "  MISSING CutsceneHBH_Create in overlay (Game_CutsceneHBH.o not linked / compile failed silently?)"
fi
# Task #309 gate-2 (P6_GHZCUT_DIRECTBOOT): note on the direct-boot reload. p6_ghzcut_reload
# is a file-STATIC fn called ONCE from the boot dispatch, so GCC -O2 INLINES it into
# p6_scene_tick -> it has NO standalone game.map symbol (a grep for it MISLEADINGLY reports
# "missing" even though it executes -- VERIFIED 2026-06-30: the direct-boot reaches
# currentSceneFolder=="GHZCutscene" via qa_ghzcut_load.py). The reliable presence witness is
# the GHZCutscene SUPPORT (the [5h] overlay-driver grep above, gated by P6_GHZCUT_BOOT which
# DIRECTBOOT implies). So just confirm p6_scene_tick (the inline host) is present + remind that
# the behavioral proof is the qa_ghzcut_load capture, not a map grep.
if [ -n "${P6_GHZCUT_DIRECTBOOT:-}" ]; then
    echo "[5h2] Task #309 gate-2 direct-boot: p6_ghzcut_reload inlines into p6_scene_tick (-O2);"
    echo "      behavioral proof = qa_ghzcut_load.py folder=='GHZCutscene' on a direct-boot capture."
    grep -m1 " p6_scene_tick$" game.map || echo "  MISSING p6_scene_tick (the inline host -- pack link broken?)"
fi
# Task #271: front-end LOAD-TIMING witnesses + the SFX-early-out fix witness. The
# front-end flavors gate these on P6_FRONTEND_LOGOS; build_p6scene_objs.sh swallows
# compile errors -> a stale .o would leave them ABSENT (the silent-fail signature).
if [ -n "${P6_FRONTEND_LOGOS:-}" ]; then
    echo "[5e] Task #271 load-timing + SFX-early-out symbol presence:"
    grep -m1 "p6_w_lt_vbl$"          game.map || echo "  MISSING p6_w_lt_vbl (load-timing instrumentation compiled out?)"
    grep -m1 "p6_w_lt_fills$"        game.map || echo "  MISSING p6_w_lt_fills"
    grep -m1 "p6_w_lt_sfx_savedopen$" game.map || echo "  MISSING p6_w_lt_sfx_savedopen (SFX early-out witness compiled out?)"
    grep -m1 "p6_saturn_sfx_pool_full" game.map || echo "  MISSING p6_saturn_sfx_pool_full (SFX early-out LATCH compiled out -- the fix is INERT?)"
    grep -m1 "p6_w_gfs_bytes$"       game.map || echo "  MISSING p6_w_gfs_bytes (GFS byte counter compiled out?)"
fi
# CP5c (Task #270): in the CHAIN flavor, confirm the chain witnesses landed in
# game.map (build_p6scene_objs.sh SWALLOWS compile errors -> a stale p6_io_main.o
# leaves the -DP6_FRONTEND_CHAIN advance compiled out + the witnesses ABSENT; this
# grep is the trip-wire). The Title witnesses + overlay objects are already checked
# by the [5c] block above (CHAIN implies TITLE).
if [ -n "${P6_FRONTEND_CHAIN:-}" ]; then
    echo "[5d] CP5c front-end CHAIN symbol presence:"
    grep -m1 "p6_w_chain_fired$"      game.map || echo "  MISSING p6_w_chain_fired (the Logos->Title advance compiled out -- stale p6_io_main.o?)"
    grep -m1 "p6_w_chain_folder_pre$" game.map || echo "  MISSING p6_w_chain_folder_pre"
fi
# Stage-1 FRD (feature checklist sec 7): confirm the frame-directory witnesses
# + the lookup landed in game.map (build_p6scene_objs.sh SWALLOWS compile
# errors -> a stale p6_io_main.o / absent SaturnFrameDir.o leaves them MISSING;
# this grep is the silent-fail trip-wire). qa_p6_frd.py is the behavioral gate.
if [ -n "${P6_FRAMEDIR:-}" ]; then
    echo "[5i] P6_FRAMEDIR symbol presence:"
    grep -m1 "p6_w_frd_staged$"  game.map || echo "  MISSING p6_w_frd_staged (SaturnFrameDir.o not linked / compile failed silently?)"
    grep -m1 "p6_w_frd_active$"  game.map || echo "  MISSING p6_w_frd_active"
    grep -m1 "p6_w_frd_misses$"  game.map || echo "  MISSING p6_w_frd_misses"
    grep -m1 "SaturnFrameDir_Lookup$" game.map || echo "  MISSING SaturnFrameDir_Lookup (the p6_vdp1_set_frd hook is INERT?)"
fi
echo "DONE [shipping image built: game.iso/game.cue + cd/OVLRING.BIN]."
