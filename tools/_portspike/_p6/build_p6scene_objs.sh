#!/usr/bin/env bash
# =============================================================================
# build_p6scene_objs.sh -- P6.3 Path A (Task #207). Build the SINGLE pre-gc'd
# proof pack `make P6SCENE=1` links into the full jo image. This is the P6.2
# file-I/O proof (build_p6io_objs.sh) EXTENDED to drive the UNMODIFIED engine
# RSDK::LoadSceneAssets() against the real on-disc Title/Scene1.bin:
#
#   p6_io_main.o    P6_SCENE_TEST body: scene witnesses + ALL relocated engine
#                   globals (objectEntityList/tileLayers/dataStorage WRAM-L,
#                   the measured-DEAD globals on a shared dummy, the Group-B
#                   arrays as absolute WRAM-L symbols) + a WRAM-L _sbrk + the
#                   full newlib syscall set + p6_scene_run().
#   p6_gfs.o        the Saturn GFS FileIO backend (UPPERCASES the basename so
#                   the engine's "Data/Stages/Title/Scene1.bin" -> "SCENE1.BIN"
#                   resolves on the ISO-9660 disc).
#   Core_Reader.o   UNMODIFIED RSDK/Core/Reader.cpp (LoadFile/ReadCompressed).
#   Scene_Scene.o   UNMODIFIED RSDK/Scene/Scene.cpp (LoadSceneAssets UNDER TEST).
#   Storage_Storage.o UNMODIFIED RSDK/Storage/Storage.cpp (InitStorage + the
#                   AllocateStorage pool allocator ReadCompressed/objectloop use).
#   miniz.o         dependencies/all/miniz/miniz.c, LEAN BUILD (-DMINIZ_NO_STDIO
#                   -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_APIS
#                   -DMINIZ_NO_ARCHIVE_WRITING_APIS -DNDEBUG): the engine's
#                   ReadCompressed calls mz_uncompress (a zlib-API -- so
#                   MINIZ_NO_ZLIB_APIS must NOT be set); the stdio/zip/time
#                   surface would otherwise drag newlib stdio + syscalls in.
#
# WHY ONE PACK OBJECT (p6_scene_pack.o): the full jo build's WRAM-H headroom is
# 41,936 B (_end 0x060B5C30 vs the 0x060C0000 SGL work-area floor, game.map
# 2026-06-06; Makefile:40-42 recurrence rule), but the six objects compiled
# whole measure ~107 KB ALLOC (text 82.5 KB + bss 18.2 KB, _dumpsizes run
# 2026-06-09). Everything is therefore compiled -ffunction-sections
# -fdata-sections and partial-linked with `ld -r --gc-sections` rooted at
# p6_scene_run (+ the map-required gate witnesses), so ONLY the LoadSceneAssets
# closure's code enters the image: LoadSceneFolder/LoadTileConfig/LoadStageGIF/
# ProcessParallax and the miniz deflate side all drop.
#
# EVERY engine C++ TU is compiled WITH -DP6_SCENE_TEST so they share ONE header
# view (the relocated pointer form of objectEntityList/dataStorage/tileLayers/
# tilesetPixels/collisionMasks/tileInfo/objectClassList/typeGroups/modelList/
# spriteAnimationList). Mixing pointer + array views across TUs would be an ODR
# violation. Run INSIDE the Docker toolchain image:
#
#   MSYS_NO_PATHCONV=1 docker run --rm -v "D:/sonicmaniasaturn":/work -w /work \
#       joengine-saturn:latest bash tools/_portspike/_p6/build_p6scene_objs.sh
#
# Emits tools/_portspike/_p6/p6_scene_pack.o (the Makefile P6SCENE LIBS path).
# The intermediate Core_Reader.o/p6_gfs.o REPLACE the P6IO-flavored ones at the
# same paths; P6IO and P6SCENE are never linked at once, so the last script run
# matches the knob you build.
# =============================================================================
set -eu

# M1 (qa_engine_menu): the MENU front-end flavor IMPLIES the TITLE flavor (which in
# turn implies LOGOS, below). The Menu scene reuses EVERY shared front-end thread
# (the frontend_frame UI tick, the VDP1 box, p6_vdp2_arm_sprites_only, the SaturnSheet
# slot store, the render-diag witnesses); the only Menu-specific bits are
# -DP6_FRONTEND_MENU on the io_main + ovl_ghz compiles (the Menu boot-select + the
# Menu register block + the M1-M4 witnesses), the 7 Menu Game_*.o + p6_menu_closure.o
# compiles, and the Menu witness -u roots. Set this FIRST so the title self-imply
# below then sets LOGOS -> the full cascade fires. The default GHZ build leaves all
# four unset -> byte-identical.
# M3.0 (Task #296): the AIZ-test diagnostic flavor BUILDS ON the MENU flavor (it boots
# straight to the AIZ scene via the same p6_frontend_frame machinery + the Menu witness
# threading). Set P6_FRONTEND_MENU FIRST so the full MENU->TITLE->LOGOS cascade fires and
# the `#elif defined(P6_FRONTEND_MENU)` boot branch + shared front-end machinery compile.
if [ -n "${P6_AIZ_TEST:-}" ]; then
    export P6_FRONTEND_MENU=1
fi
if [ -n "${P6_FRONTEND_MENU:-}" ]; then
    export P6_FRONTEND_TITLE=1
fi
# CP5c (Task #270): the front-end FLOW CHAIN flavor IMPLIES the TITLE flavor (which
# in turn implies LOGOS, just below). The chain reuses every shared front-end thread
# (the Logos + Title overlay objects, the SaturnSheet 12-slot store, the VDP1 box);
# the only chain-specific bit is -DP6_FRONTEND_CHAIN on the io_main + ovl_ghz pack
# compiles (the Logos->Title advance logic) + the chain witness -u roots. Set this
# FIRST so the title self-imply below then sets LOGOS -> the full cascade fires. The
# default GHZ build leaves all three unset -> byte-identical.
if [ -n "${P6_FRONTEND_CHAIN:-}" ]; then
    export P6_FRONTEND_TITLE=1
fi
# CP5a (Task #267): the TITLE front-end flavor IMPLIES the LOGOS flavor -- it
# reuses every shared `#if defined(P6_FRONTEND_LOGOS)` machinery (the generic
# p6_frontend_frame, the VDP1 oversize box in p6_vdp1.c, p6_vdp2_arm_sprites_only,
# the SaturnSheet front-end slot bump, the render-diag witnesses). Set
# P6_FRONTEND_LOGOS here when P6_FRONTEND_TITLE is set so every existing
# ${P6_FRONTEND_LOGOS:+...} thread below fires unchanged; the Title-specific
# threads (-DP6_FRONTEND_TITLE on the io_main/ovl/vdp2/sheet compiles, the Title
# game-TU compiles, the Title witness -u roots) are added explicitly. The default
# GHZ build leaves both unset -> byte-identical.
if [ -n "${P6_FRONTEND_TITLE:-}" ]; then
    export P6_FRONTEND_LOGOS=1
    # CP5b.5 (Task #275): default the TitleBG parallax VDP1 sprites OFF (they thrash
    # the 10-slot Title VDP1 cache -> the FG breaks up in 25/40 settled frames,
    # MEASURED qa_title_fg_stable.py). p6_ovl_ghz.o then compiles with
    # -DP6_TITLEBG_SPRITES_OFF (line 404 thread) so its TitleBG registration is
    # #if'd out, matching build_shipping.sh excluding Game_TitleBG.o from OVL_FE.
    # build_shipping.sh exports the same default before calling this script; set it
    # here too so a standalone P6_FRONTEND_TITLE=1 invocation is self-consistent.
    # A/B re-enable: P6_TITLEBG_SPRITES_OFF="" (explicit empty).
    export P6_TITLEBG_SPRITES_OFF="${P6_TITLEBG_SPRITES_OFF-1}"
fi

CC=/work/jo-engine/Compiler/LINUX/bin/sh-none-elf-gcc-8.2.0
LD=/work/jo-engine/Compiler/LINUX/sh-none-elf/bin/ld
NEWLIB=/work/jo-engine/Compiler/WINDOWS/sh-elf/include
DEPS=/work/rsdkv5-src/dependencies/all
PLAT=/work/platform/Saturn
SHIM=/work/tools/_portspike/shim
SRC=/work/rsdkv5-src/RSDKv5
P6=/work/tools/_portspike/_p6
SGLINC=/work/jo-engine/Compiler/COMMON/SGL_302j/INC

CORE_DEFS="-DRETRO_USE_MOD_LOADER=0 -DRETRO_REVISION=2 -DSCREEN_XMAX=320 -DSCREEN_YSIZE=224"
CORE_INC="-I$PLAT -I$SHIM -I$SRC -I$DEPS -I$NEWLIB"
CXXFLAGS="-x c++ -std=gnu++11 -m2 -O2 -include stdlib.h -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-builtin -ffunction-sections -fdata-sections"
# Every engine TU shares the SAME header view -> P6_SCENE_TEST on all of them.
ENG_DEFS="$CORE_DEFS -DRETRO_SATURN_FILEIO -DP6_SCENE_TEST"
# Task #296 (M2 front-end uniform-wide entity pool): the P6_FRONTEND_MENU pool block in
# Object.hpp changes ENTITY_COUNT / the region strides / ENTITYLIST_SIZE_BYTES + the inline
# SaturnEntityAt/SaturnEntitySlot accessors. EVERY engine TU that includes RetroEngine.hpp
# (Object/Scene/Collision/RetroEngine/Link/Math/Reader/Storage/Text/Sprite/DefaultObject/
# DevOutput/Input) bakes that geometry, so they MUST all see the SAME flag or the per-TU
# entity addresses diverge -- a SILENT ODR-class split (MEASURED 2026-06-25: p6_io_main.o
# compiled the 512x592 uniform pool while Scene_Object.o compiled the 1216 dual-stride pool
# -> the whole entity system reads garbage -> the Menu never registers; a settled frame-90
# capture showed MenuSetup/UIControl classID=0). Threading it through the SHARED ENG_DEFS
# guarantees every core TU agrees. P6_FRONTEND_MENU is the ONLY front-end flag that gates
# engine source (verified: its sole uses are Object.cpp:1512 + Object.hpp:59; P6_FRONTEND_TITLE
# /LOGOS gate Audio.cpp, NOT the pool) -> thread ONLY it here. DEFAULT / Title / GHZ -> the
# :+ expands to nothing -> ENG_DEFS unchanged -> every non-menu .o BYTE-IDENTICAL.
ENG_DEFS="$ENG_DEFS ${P6_FRONTEND_MENU:+-DP6_FRONTEND_MENU}"
MINIZ_DEFS="-DMINIZ_NO_STDIO -DMINIZ_NO_TIME -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ARCHIVE_WRITING_APIS -DNDEBUG"

echo "=============================================================="
echo "P6.3 Path A -- compiling 6 proof objects + gc-packing into \$P6"
echo "=============================================================="

echo "[1/7] p6_io_main.o  (P6_SCENE_TEST body: witnesses + relocated globals + _sbrk + p6_scene_run) ..."
# P6.8 Step F.1: diag-only injected scene-transition trigger (build_diag.sh sets
# P6_XTEST=1); shipping leaves it unset so the real trigger is Zone's RSDK.LoadScene.
# P6.8 F.2-followup: P6_WARP=1 instead builds the debug-warp variant (teleport the
# player to the GHZ1 signpost). The two are MUTUALLY EXCLUSIVE (build_diag.sh sets
# exactly one); shipping leaves both unset.
# #181: P6_WARP_BRIDGE builds the bridge-warp diag variant (pin the player onto
# the first GHZ1 bridge for a rendered-plank screenshot). Mutually exclusive with
# P6_WARP/P6_XTEST (build_diag.sh picks exactly one); shipping leaves all unset.
# CP4 (Task #265): P6_FRONTEND_LOGOS=1 builds the FRONT-END flavor whose lean
# tick boots the Logos splash scene instead of GHZ (p6_logos_reload +
# p6_frontend_frame). Mutually independent of the GHZ diag knobs; the default
# shipping build leaves it unset -> boots GHZ unchanged.
$CC $CXXFLAGS $ENG_DEFS ${P6_XTEST:+-DP6_TRANSITION_TEST} ${P6_WARP:+-DP6_WARP_TEST} ${P6_WARP_BRIDGE:+-DP6_WARP_BRIDGE_TEST} ${P6_SHT_NORES:+-DP6_SHT_NO_RESIDENT} ${P6_GHZ2_BOOT:+-DP6_GHZ2_BOOT} ${P6_NOSCAN:+-DP6_PERF_NOSCAN} ${P6_SHADOW:+-DP6_SHADOW_COMPARE} ${P6_STREAM_PERF:+-DP6_STREAM_PERF} ${P6_FRONTEND_LOGOS:+-DP6_FRONTEND_LOGOS} ${P6_FRONTEND_TITLE:+-DP6_FRONTEND_TITLE} ${P6_FRONTEND_MENU:+-DP6_FRONTEND_MENU} ${P6_FRONTEND_CHAIN:+-DP6_FRONTEND_CHAIN} ${P6_AIZ_TEST:+-DP6_AIZ_TEST} ${P6_GHZCUT_BOOT:+-DP6_GHZCUT_BOOT} ${P6_GHZCUT_DIRECTBOOT:+-DP6_GHZCUT_DIRECTBOOT} ${P6_GHZCUT_SEAMTEST:+-DP6_GHZCUT_SEAMTEST} ${P6_GHZCUT_HOLD:+-DP6_GHZCUT_HOLD} ${P6_GHZCUT_NOFIX:+-DP6_GHZCUT_NOFIX} ${P6_TITLE_NODRAW:+-DP6_TITLE_NODRAW} ${P6_TICK_CATCHUP:+-DP6_TICK_CATCHUP} ${P6_DIRECT_VDP1:+-DP6_DIRECT_VDP1} ${P6_FE_SLAVE_PRESENT:+-DP6_FE_SLAVE_PRESENT} ${P6_FRAMEDIR:+-DP6_FRAMEDIR} ${P6_GHZ_AUTORUN:+-DP6_GHZ_AUTORUN} ${P6_DDW_ARENA:+-DP6_DDW_ARENA} ${P6_DDW_KILL:+-DP6_DDW_KILL} $CORE_INC \
    -c -o "$P6/p6_io_main.o" "$P6/p6_io_main.cpp"

echo "[2/7] p6_gfs.o      (Saturn GFS FileIO backend, UPPERCASE basename) ..."
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -ffunction-sections -fdata-sections \
    ${P6_FRONTEND_LOGOS:+-DP6_FRONTEND_LOGOS} \
    -I"$SGLINC" -I"$NEWLIB" \
    -c -o "$P6/p6_gfs.o" "$P6/p6_gfs.c"

echo "[3/7] Core_Reader.o (UNMODIFIED engine Reader.cpp) ..."
$CC $CXXFLAGS $ENG_DEFS $CORE_INC \
    -c -o "$P6/Core_Reader.o" "$SRC/RSDK/Core/Reader.cpp"

echo "[4/7] Scene_Scene.o (engine Scene.cpp -- LoadSceneAssets; P6_CART=1 loads full layers resident in cart STG) ..."
$CC $CXXFLAGS $ENG_DEFS ${P6_CART:+-DP6_CART} $CORE_INC \
    -c -o "$P6/Scene_Scene.o" "$SRC/RSDK/Scene/Scene.cpp"

echo "[5/7] Storage_Storage.o (engine Storage.cpp -- InitStorage + AllocateStorage; P6_CART=1 puts STG/TMP pools in the 4MB cart) ..."
$CC $CXXFLAGS $ENG_DEFS ${P6_CART:+-DP6_CART} ${P6_CART_TMP:+-DP6_CART_TMP} $CORE_INC \
    -c -o "$P6/Storage_Storage.o" "$SRC/RSDK/Storage/Storage.cpp"

echo "[6/8] miniz.o       (lean inflate-only miniz -- mz_uncompress for ReadCompressed) ..."
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -ffunction-sections -fdata-sections \
    $MINIZ_DEFS -I"$DEPS" -I"$DEPS/miniz" -I"$NEWLIB" \
    -c -o "$P6/miniz.o" "$DEPS/miniz/miniz.c"

echo "[7/9] Storage_Text.o (UNMODIFIED engine Text.cpp -- REAL GenerateHashMD5 + StringLowerCase for the P6.4 pack hash lookup; gc drops the rest of the TU) ..."
$CC $CXXFLAGS $ENG_DEFS $CORE_INC \
    -c -o "$P6/Storage_Text.o" "$SRC/RSDK/Storage/Text.cpp"

echo "[7b/9] Graphics_Sprite.o (UNMODIFIED engine Sprite.cpp -- REAL ImageGIF::Load LZW decoder + vtable for P6.5a LoadStageGIF; the PNG half gc-drops; dyn-init audit: only the constant aggregate codeMasks[] at Sprite.cpp:16, no .init_array trap) ..."
$CC $CXXFLAGS $ENG_DEFS $CORE_INC \
    -c -o "$P6/Graphics_Sprite.o" "$SRC/RSDK/Graphics/Sprite.cpp"

echo "[7c/9] p6_vdp2.o (P6.5b1 present: engine Island layer -> NBG1 2-word-PND cell mode; ST-058 contracts in-file) ..."
# CP4c _end-leak FIX (Task #266): thread P6_FRONTEND_LOGOS so the front-end-only
# p6_vdp2_arm_sprites_only definition (gated in p6_vdp2.c, called by the gated
# p6_frontend_frame in p6_io_main.cpp) is COMPILED IN for the front-end build.
# p6_vdp2.c is built HERE (not by jo-make), so the Makefile CCFLAGS that thread
# the flag to p6_vdp1.c do NOT reach it -- without this, the front-end link fails
# (undefined reference to p6_vdp2_arm_sprites_only). DEFAULT build: the :+ expands
# to nothing -> the definition stays compiled out -> p6_vdp2.o byte-identical.
# CP5b.3 (#272): ALSO thread P6_FRONTEND_TITLE so the Title-backdrop present
# (p6_vdp2_present_title_backdrop + p6_vdp2_title_backdrop_scroll, gated in
# p6_vdp2.c, called by the gated frontend frame + load in p6_io_main.cpp) is
# COMPILED IN for the Title build (same reason as the LOGOS thread above --
# p6_vdp2.c is built here, not by jo-make). DEFAULT/Logos builds: the :+ expands
# to nothing -> the Title definitions stay compiled out -> p6_vdp2.o unchanged.
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -ffunction-sections -fdata-sections \
    ${P6_FRONTEND_LOGOS:+-DP6_FRONTEND_LOGOS} ${P6_FRONTEND_TITLE:+-DP6_FRONTEND_TITLE} \
    ${P6_TITLE_NORBG0:+-DP6_TITLE_NORBG0} ${P6_TITLE_ISLAND_STATIC:+-DP6_TITLE_ISLAND_STATIC} \
    ${P6_AIZ_TEST:+-DP6_AIZ_TEST} ${P6_GHZCUT_BOOT:+-DP6_GHZCUT_BOOT} ${P6_GHZCUT_HOLD:+-DP6_GHZCUT_HOLD} \
    ${P6_DV1_PROBE:+-DP6_DIRECT_VDP1_PROBE} ${P6_DIRECT_VDP1:+-DP6_DIRECT_VDP1} \
    -I"$SGLINC" -I"$NEWLIB" \
    -c -o "$P6/p6_vdp2.o" "$P6/p6_vdp2.c"
# NOTE: p6_vdp1.c (P6.5b2 sprite half) is NOT built here -- it includes
# jo/jo.h, whose struct layouts depend on the project's jo config flags, so
# the Makefile P6SCENE block compiles it inside the jo make (SRCS +=).

echo "[7d/9] Graphics_Animation.o (UNMODIFIED engine Animation.cpp -- LoadSpriteAnimation + SetSpriteAnimation + ProcessAnimation for P6.5b2; gc drops the string/editor surface) ..."
$CC $CXXFLAGS $ENG_DEFS $CORE_INC \
    -c -o "$P6/Graphics_Animation.o" "$SRC/RSDK/Graphics/Animation.cpp"

echo "[7e/9] Audio_Audio.o (UNMODIFIED engine Audio.cpp -- LoadSfx/LoadSfxToSlot WAV parse + PlaySfx channel allocator + InitAudioChannels for P6.6a; the in-TU stb_vorbis + stream/mixer surface gc-drops while unreferenced; dyn-init audit: all file-scope state constant/zero-init, no .init_array trap) ..."
$CC $CXXFLAGS $ENG_DEFS ${P6_FRONTEND_LOGOS:+-DP6_FRONTEND_LOGOS} ${P6_GHZ_AUTORUN:+-DP6_GHZ_AUTORUN} $CORE_INC \
    -c -o "$P6/Audio_Audio.o" "$SRC/RSDK/Audio/Audio.cpp"

echo "[7f] Scene_Object.o (UNMODIFIED engine Object.cpp -- RegisterObject + ResetEntitySlot + ProcessObjects + ProcessObjectDrawLists for P6.7a; editor/serialize surface gc-drops; +P6_PERF_OBJPROF Phase-2d per-classID Update timing diagnostic) ..."
$CC $CXXFLAGS $ENG_DEFS -DP6_PERF_OBJPROF ${P6_SHADOW:+-DP6_SHADOW_COMPARE} ${P6_STREAM_PROOF:+-DP6_STREAM_PROOF} $CORE_INC \
    -c -o "$P6/Scene_Object.o" "$SRC/RSDK/Scene/Object.cpp"

echo "[7q] Scene_Collision.o (VERBATIM engine Collision.cpp -- P6.7 W15b Task #227: ProcessObjectMovement/ProcessPathGrip/ProcessAirCollision_Down + the Find*/[ LR]Wall/Floor/Roof sensor walkers + CheckObjectCollision*; every tile read goes through the RSDK_*_MASK/_ANGLE seam -> PackedCollisionMask/PackedTileAngle (Scene.hpp:289-378, gate qa_p6_collision K1-K5); CopyCollisionMask + Legacy + ProcessAirCollision_Up preprocess out at REV02/MOD_LOADER=0; -Os NOT -O2: lone-TU census 2026-06-12 measured 18,696 B alloc at -O2 vs 13,016 B at -Os against a 2,708 B pre-land _end margin) ..."
$CC ${CXXFLAGS/-O2/-Os} $ENG_DEFS ${P6_GHZ_AUTORUN:+-DP6_GHZ_AUTORUN} $CORE_INC \
    -c -o "$P6/Scene_Collision.o" "$SRC/RSDK/Scene/Collision.cpp"

echo "[7r] Input_Input.o (VERBATIM engine Input.cpp -- P6.7 W7 Task #227: the controller/stickL/stickR/triggerL/triggerR/touchInfo arrays + ClearInput/ProcessInput edge logic + InitInputDevices autoassign seed; no PC device backend compiles in (RETRO_INPUTDEVICE_KEYBOARD=0 under RETRO_SATURN, RetroEngine.hpp:449); -Os per the Collision census precedent -- lone-TU census 2026-06-12: text 1,332 B / bss 2,064 B, undefs videoSettings + SKU::userCore only, both pack-resident) ..."
$CC ${CXXFLAGS/-O2/-Os} $ENG_DEFS $CORE_INC \
    -c -o "$P6/Input_Input.o" "$SRC/RSDK/Input/Input.cpp"

echo "[7s] InputDevice_Saturn.o (Saturn SMPC pad device backend -- the AudioDevice_Saturn precedent; SGL Smpc_Peripheral snapshot reader, KBInputDevice.cpp:681-806 registration/Update/Process mirror; census: text 760 B / data 50 B) ..."
$CC ${CXXFLAGS/-O2/-Os} $ENG_DEFS ${P6_GHZ_AUTORUN:+-DP6_GHZ_AUTORUN} $CORE_INC \
    -c -o "$P6/InputDevice_Saturn.o" "$PLAT/InputDevice_Saturn.cpp"

echo "[7t] CppRuntime_Saturn.o (freestanding operator new/delete -> newlib malloc/free -- the InitSaturnPadDevice registration news the device + the InputDevice virtual-dtor D0 references operator delete; census: text 76 B, gc keeps only the referenced operators) ..."
$CC ${CXXFLAGS/-O2/-Os} $ENG_DEFS $CORE_INC \
    -c -o "$P6/CppRuntime_Saturn.o" "$PLAT/CppRuntime_Saturn.cpp" 2>/dev/null

echo "[7g] Core_Link.o (UNMODIFIED engine Link.cpp -- SetupFunctionTables + RSDKFunctionTable[], the P6.1-proven dispatch; recipe from link_p6.sh:316-318) ..."
$CC $CXXFLAGS $ENG_DEFS $CORE_INC \
    -c -o "$P6/Core_Link.o" "$SRC/RSDK/Core/Link.cpp"

echo "[7h] Core_Math.o (engine Math.cpp, Saturn baked trig tables per Task #212 -- SetupFunctionTables -> CalculateTrigAngles closure) ..."
$CC $CXXFLAGS $ENG_DEFS $CORE_INC \
    -c -o "$P6/Core_Math.o" "$SRC/RSDK/Core/Math.cpp"

echo "[7hb] Core_RetroEngine.o (engine RetroEngine.cpp -- the REAL LoadGameConfig for P6.7c; gc keeps only its closure; engine/globalVarsPtr def conflicts resolved via the P6_SCENE_TEST guard + stub removal; 1.03 per-scene-filter shim in-file under RETRO_SATURN) ..."
$CC $CXXFLAGS $ENG_DEFS $CORE_INC \
    -c -o "$P6/Core_RetroEngine.o" "$SRC/RSDK/Core/RetroEngine.cpp"

echo "[7hc] Scene_Objects_DefaultObject.o (REAL engine DefaultObject @classID 0 -- the InitGameLink preamble mirror, P6.7c) ..."
$CC $CXXFLAGS $ENG_DEFS $CORE_INC \
    -c -o "$P6/Scene_Objects_DefaultObject.o" "$SRC/RSDK/Scene/Objects/DefaultObject.cpp"

echo "[7hd] Scene_Objects_DevOutput.o (REAL engine DevOutput @classID 1) ..."
$CC $CXXFLAGS $ENG_DEFS $CORE_INC \
    -c -o "$P6/Scene_Objects_DevOutput.o" "$SRC/RSDK/Scene/Objects/DevOutput.cpp"

echo "[7i] UserCore_Saturn.o (-DRSDK_SKU_GLOBALS_IN_LINK so Core_Link.o solely owns the SKU globals; recipe from link_p6.sh:321-324) ..."
$CC $CXXFLAGS $ENG_DEFS -DRSDK_SKU_GLOBALS_IN_LINK $CORE_INC \
    -c -o "$P6/UserCore_Saturn.o" "$PLAT/UserCore_Saturn.cpp"

echo "[7j] p6_stubs.o (backend entry points Link.cpp's table references that are NOT real in this pack) ..."
# M1b FIX: thread the front-end flags so p6_stubs.cpp's FillScreen forwards to
# p6_fillscreen_saturn (the #if defined(P6_FRONTEND_TITLE) arm) instead of the no-op
# #else. MEASURED root cause of the black menu backdrop: without -DP6_FRONTEND_TITLE
# here, RSDK::FillScreen was the no-op {} -> UIBackground_DrawNormal's FillScreen(gold)
# did nothing (p6_w_fill_calls==0). The title intro fade needs this too (its FadeBlack/
# Flash FillScreen). MENU implies TITLE -> LOGOS.
$CC $CXXFLAGS $ENG_DEFS -DP6_PACK_STUBS ${P6_FRONTEND_LOGOS:+-DP6_FRONTEND_LOGOS} ${P6_FRONTEND_TITLE:+-DP6_FRONTEND_TITLE} ${P6_FRONTEND_MENU:+-DP6_FRONTEND_MENU} $CORE_INC \
    -c -o "$P6/p6_stubs.o" "$P6/p6_stubs.cpp"

echo "[7jb] p6_pack_stubs.o (the measured 43-symbol function-table closure remainder; real TUs replace these P6.7b+) ..."
# M3 (Task #295) PLATE FIX: forward -DP6_FRONTEND_MENU so the pack's DrawFace stub compiles
# its FORWARDING body (p6_pack_stubs.cpp:65-73 -> p6_drawface_saturn), not the #else empty
# no-op (:75). MEASURED root cause of p6_w_drawface_calls==0: without this flag the empty
# DrawFace(){} linked into the pack -> RSDK.DrawFace was a valid non-null no-op (no crash, no
# plate). Mirrors the io_main/ovl_ghz menu compiles which already get this flag. Other front-
# end flavors (Logos/Title) and the default GHZ build never set P6_FRONTEND_MENU -> the empty
# #else still links there (byte-identical), so this is additive-only.
$CC $CXXFLAGS $ENG_DEFS ${P6_FRONTEND_MENU:+-DP6_FRONTEND_MENU} $CORE_INC -c -o "$P6/p6_pack_stubs.o" "$P6/p6_pack_stubs.cpp"

echo "[7k] p6_ring2.o (VERBATIM decomp Ring -- P6.7d.3: OVERLAY member, NOT a pack member; flat TU, bridges into the engine table) ..."
$CC $CXXFLAGS -DRETRO_SATURN_FILEIO -I"$P6" -I"$NEWLIB" \
    -c -o "$P6/p6_ring2.o" "$P6/p6_ring2.cpp"

echo "[7l] p6_ovl_ring.o (overlay ENTRY TU -- NO -ffunction-sections so the entry lands first at the window base) ..."
$CC -x c++ -std=gnu++11 -m2 -O2 -include stdlib.h -fno-exceptions -fno-rtti \
    -fno-threadsafe-statics -fno-builtin \
    -DRETRO_SATURN_FILEIO -I"$P6" -I"$NEWLIB" \
    -c -o "$P6/p6_ovl_ring.o" "$P6/p6_ovl_ring.cpp"

echo "[7m] P6.7 wave-1 GAME TUs (VERBATIM decomp Localization/LogHelpers/Options + p6_wave1_reg, Game.c role) ..."
# Compiled as C against the REAL Game.h/GameLink.h via the census include
# tree (tools/_portspike/_p67d_sizing/build_include_tree.py output --
# regenerate it after any tools/_decomp_raw header change). THE CENSUS KNOB
# IS BINDING: -DRETRO_REVISION=2 matches CORE_DEFS above -- at REV0U the
# function table gains entries and RegisterObject/RegisterGlobalVariables
# grow extra args (garbage through the memcpy'd table otherwise; gate
# qa_p6_globals G1 checks the slot ABI offline every run).
GAME_DEFS="-DRETRO_REVISION=2 -DGAME_VERSION=3 -DSATURN_GLOBALS_RETARGET -DRETRO_USE_MOD_LOADER=0"
GINC=/work/tools/_portspike/_p67d_sizing/include
if [ ! -d "$GINC/Global" ]; then
    echo "ERROR: census include tree missing -- run tools/_portspike/_p67d_sizing/build_include_tree.py first" >&2
    exit 1
fi
for w1 in Global_Localization:Game_Localization \
          Helpers_LogHelpers:Game_LogHelpers \
          Helpers_Options:Game_Options; do
    src_tu="${w1%%:*}"; out_tu="${w1##*:}"
    "$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -ffunction-sections -fdata-sections \
        $GAME_DEFS -I"$GINC" -I"$NEWLIB" \
        -c -o "$P6/$out_tu.o" "/work/tools/_decomp_raw/SonicMania_Objects_$src_tu.c"
done
# #181: the bridge-warp flag also gates p6_brg_witness's per-frame onScreen rescan
# in this game-side TU (shipping keeps the one-shot latch -- perf-safe).
# GL1 (2026-07-06): thread P6_GHZCUT_BOOT so the chain GHZ-landing act card
# (p6_titlecard_spawn/tick/draw, all inside #if defined(P6_GHZCUT_BOOT)) compiles
# INTO p6_wave1_reg.o -- p6_io_main.cpp references them from its own
# P6_GHZCUT_BOOT block, so without this the final link fails with undefined
# p6_titlecard_* (the block compiled to nothing here). Plain GHZ (no flag) is
# unaffected -- the block + the references both vanish together.
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -ffunction-sections -fdata-sections \
    $GAME_DEFS ${P6_WARP_BRIDGE:+-DP6_WARP_BRIDGE_TEST} ${P6_GHZCUT_BOOT:+-DP6_GHZCUT_BOOT} -I"$GINC" -I"$NEWLIB" \
    -c -o "$P6/p6_wave1_reg.o" "$P6/p6_wave1_reg.c"
# Player-wave closure boundary (NULL class ptrs + witnessed inert stubs +
# sprintf-over-vsprintf; rationale in-file).
# M3.1 (qa_p6_aiz_cutscene): thread P6_AIZ_TEST so p6_closure_edge.c points the pack's
# `StarPost` global at a REAL zeroed ObjectStarPost instance (instead of NULL). The AIZ
# overlay TUs AIZTornado_Create/AIZTornadoPath_Create deref StarPost->postIDs[0] (a fresh-
# boot 0 == arm the tornado); a NULL StarPost would crash. Default/GHZ/menu builds leave
# P6_AIZ_TEST unset -> StarPost stays NULL -> byte-identical.
"$CC" -x c -std=gnu11 -m2 -Os -fno-builtin -ffunction-sections -fdata-sections \
    $GAME_DEFS ${P6_AIZ_TEST:+-DP6_AIZ_TEST} ${P6_GHZCUT_BOOT:+-DP6_GHZCUT_BOOT} -I"$GINC" -I"$NEWLIB" \
    -c -o "$P6/p6_closure_edge.o" "$P6/p6_closure_edge.c"

echo "[7p] P6.7 Player wave GAME TUs (VERBATIM 17-TU closure, -Os census knob) ..."
# The Player depth-2 closure (Task #227): 13 live Global classes + the inert
# Ice/SizeLaser/ERZStart refs + DebugMode (referenced by Player + HUD).
# -Os MATCHES THE CENSUS NUMBERS (102,904 B alloc sections, P6.7d.1 obj_os):
# the -O2 wave-1 knob would inflate the set against the 142,108 B post-
# hand-port-drop margin. Same GAME_DEFS as [7m] (REV02 ABI is binding).
for w2 in Global_ActClear:Game_ActClear \
          Helpers_DrawHelpers:Game_DrawHelpers \
          Helpers_MathHelpers:Game_MathHelpers \
          Global_Soundboard:Game_Soundboard \
          Global_BoundsMarker:Game_BoundsMarker \
          Global_Camera:Game_Camera \
          Global_DebugMode:Game_DebugMode \
          Global_Dust:Game_Dust \
          ERZ_ERZStart:Game_ERZStart \
          Global_GameOver:Game_GameOver \
          Global_HUD:Game_HUD \
          PGZ_Ice:Game_Ice \
          Global_ImageTrail:Game_ImageTrail \
          Global_Music:Game_Music \
          Global_PauseMenu:Game_PauseMenu \
          Global_Player:Game_Player \
          Global_SaveGame:Game_SaveGame \
          Global_ScoreBonus:Game_ScoreBonus \
          Global_Shield:Game_Shield \
          Global_SignPost:Game_SignPost \
          MMZ_SizeLaser:Game_SizeLaser \
          Global_Zone:Game_Zone; do
    src_tu="${w2%%:*}"; out_tu="${w2##*:}"
    "$CC" -x c -std=gnu11 -m2 -Os -fno-builtin -ffunction-sections -fdata-sections \
        $GAME_DEFS -I"$GINC" -I"$NEWLIB" \
        -c -o "$P6/$out_tu.o" "/work/tools/_decomp_raw/SonicMania_Objects_$src_tu.c"
done
# F.4 (Task #235): GHZ1->GHZ2 ATL transition objects (BGSwitch + GHZSetup). Closures
# fully satisfied (Zone_StoreEntities/ReloadStoredEntities already in Game_Zone.o);
# funded by the +0x2000 WRAM-H re-budget (ANIMPAK floor 0x060BA000).
# #181: GHZ Bridge (planks) joins here -- a GHZ-scene object; its only out-of-pack
# dep (BurningLog) is NULL'd in p6_closure_edge.c, all other refs (Player/Zone/
# RSDK.*) already pack-resident.
# #254: GHZ1 loop fix -- PlaneSwitch (the collision-plane toggle for loops). The
# corkscrew force-objects are DEFERRED (code-budget). Spring is the FIRST object of
# the sweep, UNBLOCKED by the anim-pool funding (DATASET_STG 92->150 KB via TMP->cart,
# Storage.cpp P6_CART_TMP); the regression that deferred it (Spring overflow ->
# Bridge.bin alloc-fail) is now gated by qa_p6_animpool.py + qa_p6_ghz_regression.py.
# BATCH 2 (badnik break chain, 2026-06-18): the 3 CHAIN TUs (BadnikHelpers/
# Explosion/Animals) + the 6 GHZ1 BADNIKs. ALL 9 are compiled here (the w4 loop
# emits Game_<Obj>.o with -ffunction/-fdata-sections). PLACEMENT then SPLITS:
#  - the 3 chain TUs join the PACK object list below (Game_Player.o, a PACK TU,
#    references BadnikHelpers_BadnikBreakUnseeded/Explosion/Animals -- and the
#    pack->overlay dependency is one-way: the overlay imports from game.elf via
#    -R, never the reverse, so a chain TU in the overlay would leave the FINAL
#    jo link with BadnikHelpers_BadnikBreakUnseeded UNDEFINED);
#  - the 6 badniks join the OVERLAY link (build_shipping.sh [3b]); they reference
#    Player_CheckBadnikBreak/ProjectileHurt (pack) via -R game.elf, kept by the
#    -u roots added below.
# BATCH 3 (GHZ gameplay-parity sweep, 2026-07-09): ItemBox (38 authored GHZ1
# monitors) + its CREATE_ENTITY closure Debris (break shards, ItemBox.c:831; also
# Shield.c:194 sparks) + InvincibleStars (ITEMBOX_INVINCIBLE deref, ItemBox.c:511;
# 3 authored invincibility monitors). All three OVERLAY-resident (build_shipping.sh
# [3b]); pack readers reach them via the itembox/debris rewire seams + the
# p6_closure_edge forwards. Platform/InvisibleBlock follow in their own steps.
for w4 in Common_BGSwitch:Game_BGSwitch \
          GHZ_GHZSetup:Game_GHZSetup \
          GHZ_Bridge:Game_Bridge \
          Global_PlaneSwitch:Game_PlaneSwitch \
          Global_Spring:Game_Spring \
          Global_Ring:Game_Ring \
          GHZ_SpikeLog:Game_SpikeLog \
          Global_Spikes:Game_Spikes \
          Common_Decoration:Game_Decoration \
          Common_ForceSpin:Game_ForceSpin \
          Common_SpinBooster:Game_SpinBooster \
          Helpers_BadnikHelpers:Game_BadnikHelpers \
          Global_Explosion:Game_Explosion \
          Global_Animals:Game_Animals \
          GHZ_Newtron:Game_Newtron \
          GHZ_Crabmeat:Game_Crabmeat \
          GHZ_BuzzBomber:Game_BuzzBomber \
          GHZ_Chopper:Game_Chopper \
          GHZ_Motobug:Game_Motobug \
          GHZ_Batbrain:Game_Batbrain \
          Global_ItemBox:Game_ItemBox \
          Global_Debris:Game_Debris \
          Global_InvincibleStars:Game_InvincibleStars \
          Common_Platform:Game_Platform \
          Common_BreakableWall:Game_BreakableWall \
          Global_InvisibleBlock:Game_InvisibleBlock \
          GHZ_DDWrecker:Game_DDWrecker; do
    src_tu="${w4%%:*}"; out_tu="${w4##*:}"
    "$CC" -x c -std=gnu11 -m2 -Os -fno-builtin -ffunction-sections -fdata-sections \
        $GAME_DEFS -I"$GINC" -I"$NEWLIB" \
        -c -o "$P6/$out_tu.o" "/work/tools/_decomp_raw/SonicMania_Objects_$src_tu.c"
done
# CP4 (Task #266): the FRONT-END Logos splash objects, compiled VERBATIM from the
# cached decomp (SonicMania_Objects_Menu_{LogoSetup,UIPicture}.c) the same w4 way.
# Only built in the front-end flavor; they link into the OVERLAY (build_shipping.sh
# [3b]), NOT the pack -- so they are gc-dropped from the pack link regardless. The
# -Os census knob matches the other game TUs.
if [ -n "${P6_FRONTEND_LOGOS:-}" ]; then
    for wfe in Menu_LogoSetup:Game_LogoSetup \
               Menu_UIPicture:Game_UIPicture; do
        src_tu="${wfe%%:*}"; out_tu="${wfe##*:}"
        "$CC" -x c -std=gnu11 -m2 -Os -fno-builtin -ffunction-sections -fdata-sections \
            $GAME_DEFS -DP6_FRONTEND_LOGOS -I"$GINC" -I"$NEWLIB" \
            -c -o "$P6/$out_tu.o" "/work/tools/_decomp_raw/SonicMania_Objects_$src_tu.c"
    done
fi
# CP5a (Task #267): the FRONT-END Title scene objects, compiled VERBATIM from the
# cached decomp (SonicMania_Objects_Title_{TitleSetup,TitleLogo}.c) the same wfe way.
# Only built in the Title flavor; they link into the OVERLAY (build_shipping.sh [3b]),
# NOT the pack -- so they are gc-dropped from the pack link regardless. The -Os census
# knob matches the other game TUs. The MANIA_USE_PLUS=0 / RETRO_REV02 simple branch
# compiles (preprocessor-confirmed; the Plus cheat/SetupPlusLogo + State_* fns drop).
if [ -n "${P6_FRONTEND_TITLE:-}" ]; then
    # CP5b.2 (#269): + Title_TitleSonic (the ring-center head + finger-wave, verbatim
    # SonicMania_Objects_Title_TitleSonic.c -- 69 L, fully self-contained).
    # CP5b.3 (#272): + Title_TitleBG (mountains/water/wing-shine + island/cloud
    # scanline FX) + Title_Title3DSprite (the perspective billboards). Both verbatim
    # decomp, fully self-contained; they link into the overlay (build_shipping.sh [3b]).
    for wft in Title_TitleSetup:Game_TitleSetup \
               Title_TitleLogo:Game_TitleLogo \
               Title_TitleSonic:Game_TitleSonic \
               Title_TitleBG:Game_TitleBG \
               Title_Title3DSprite:Game_Title3DSprite; do
        src_tu="${wft%%:*}"; out_tu="${wft##*:}"
        "$CC" -x c -std=gnu11 -m2 -Os -fno-builtin -ffunction-sections -fdata-sections \
            $GAME_DEFS -DP6_FRONTEND_TITLE -DP6_FRONTEND_LOGOS -I"$GINC" -I"$NEWLIB" \
            -c -o "$P6/$out_tu.o" "/work/tools/_decomp_raw/SonicMania_Objects_$src_tu.c"
    done
fi
# M1 (qa_engine_menu): the FRONT-END MENU scene's min UI set + the closure TU. Built
# only in the Menu flavor; they link into the OVERLAY (build_shipping.sh [3b]), NOT
# the pack -- so they are gc-dropped from the pack link regardless. The -Os census
# knob matches the other game TUs. MenuSetup needs a 1-line REV02 compat patch:
# MenuSetup_SetupActions:515 uses `GameInfo->platform` directly, but RSDKGameInfo has
# no `platform` member at RETRO_REV02 (the field moved to RSDKSKUInfo; the build sets
# SKU::curSKU.platform). sku_platform (== SKU->platform, APICallback.h:43) is the
# decomp's intent at REV02 and is behavior-IDENTICAL (Saturn != PC/DEV either way ->
# the same ELSE branch that drops the PC "Exit Game" menu item). The cached decomp is
# the authoritative source (never edited); patch a BUILD-LOCAL copy with sed, exactly
# the project's "REV02 compat arm" precedent (APICallback.h:34-45). p6_menu_closure.c
# resolves the measured 68-symbol UI/API link cone of the 7 TUs (the closure_edge
# pattern; trial-link verified the residual undefs are all -R-pack + libgcc).
if [ -n "${P6_FRONTEND_MENU:-}" ]; then
    sed 's/GameInfo->platform/sku_platform/g' \
        "/work/tools/_decomp_raw/SonicMania_Objects_Menu_MenuSetup.c" > "$P6/_MenuSetup_rev02.c"
    "$CC" -x c -std=gnu11 -m2 -Os -fno-builtin -ffunction-sections -fdata-sections \
        $GAME_DEFS -DP6_FRONTEND_MENU -I"$GINC" -I"$NEWLIB" \
        -c -o "$P6/Game_MenuSetup.o" "$P6/_MenuSetup_rev02.c"
    # M1b: + Game_UIModeButton.o (the 4 main-menu rows). UIModeButton.c references
    # API_GetFilteredInputDeviceID/_ResetInputSlotAssignments/_AssignInputSlotToDevice
    # (APICallback.h macros) only in UIModeButton_SelectedCB (a press handler -- never
    # reached at the f90 settled capture); those resolve via the closure (p6_menu_closure
    # + p6_closure_edge -R import). No REV02 sed needed (it uses no GameInfo->platform).
    for wfm in Menu_UIControl:Game_UIControl \
               Menu_UIBackground:Game_UIBackground \
               Menu_UIButton:Game_UIButton \
               Menu_UIModeButton:Game_UIModeButton \
               Menu_UIWidgets:Game_UIWidgets \
               Menu_UISubHeading:Game_UISubHeading \
               Menu_UIButtonPrompt:Game_UIButtonPrompt \
               Menu_UISaveSlot:Game_UISaveSlot \
               Menu_UITransition:Game_UITransition; do
        src_tu="${wfm%%:*}"; out_tu="${wfm##*:}"
        "$CC" -x c -std=gnu11 -m2 -Os -fno-builtin -ffunction-sections -fdata-sections \
            $GAME_DEFS -DP6_FRONTEND_MENU -I"$GINC" -I"$NEWLIB" \
            -c -o "$P6/$out_tu.o" "/work/tools/_decomp_raw/SonicMania_Objects_$src_tu.c"
    done
    "$CC" -x c -std=gnu11 -m2 -Os -fno-builtin -ffunction-sections -fdata-sections \
        $GAME_DEFS -DP6_FRONTEND_MENU -I"$GINC" -I"$NEWLIB" \
        -c -o "$P6/p6_menu_closure.o" "$P6/p6_menu_closure.c"
fi
# M3.1 (qa_p6_aiz_cutscene): the AIZ intro-cutscene DRIVER objects, compiled VERBATIM
# from the cached decomp the same w4 way. Built only in the AIZ-test flavor; they link
# into the OVERLAY (build_shipping.sh [3b]), NOT the pack -- so they are gc-dropped from
# the pack link regardless. The -Os census knob matches the other game TUs. Game_
# Decoration.o is ALREADY compiled (w4 loop above, for GHZ) + linked into the overlay,
# so it is NOT recompiled here -- Decoration_StageLoad already handles the AIZ folder.
# MANIA_USE_PLUS=0 (GAME_VERSION=3): the AIZSetup Plus cutscene-states/BGSwitch/bellPlant
# blocks + the CutsceneSeq skip surface preprocess out (the simple non-Plus arm compiles).
if [ -n "${P6_AIZ_TEST:-}" ]; then
    # FXRuby REV02 compat patch (the PauseMenu.c:214-224 precedent): the non-Plus
    # FXRuby_Create branch calls RSDK.GetTintLookupTable() (FXRuby.c:77), but REV02 +
    # RETRO_USE_MOD_LOADER=0 removed GetTintLookupTable from the active RSDKFunctionTable
    # (GameLink.h:1199 struct is #if RETRO_USE_MOD_LOADER). The engine no longer owns the
    # tint table; on Saturn the INK_TINT draw path is a no-op anyway, so neutralize the
    # 64KB-table build (the M3.2/M3.3 white-fade effect, not M3.1 camera framing). Patch a
    # BUILD-LOCAL copy with sed (never the cached decomp). Mirrors the MenuSetup sed above.
    sed 's|uint16 \*tintLookupTable = RSDK.GetTintLookupTable();|uint16 *tintLookupTable = (uint16 *)0; /* P6_AIZ_TEST: REV02 no GetTintLookupTable */|; s|for (int32 c = 0; c < 0x10000; ++c) tintLookupTable\[0xFFFF - c\] = c;|if (tintLookupTable) for (int32 c = 0; c < 0x10000; ++c) tintLookupTable[0xFFFF - c] = c;|' \
        "/work/tools/_decomp_raw/SonicMania_Objects_Cutscene_FXRuby.c" > "$P6/_FXRuby_rev02.c"
    for waiz in AIZ_AIZSetup:Game_AIZSetup \
                Cutscene_CutsceneSeq:Game_CutsceneSeq \
                AIZ_AIZTornado:Game_AIZTornado \
                AIZ_AIZTornadoPath:Game_AIZTornadoPath \
                AIZ_AIZKingClaw:Game_AIZKingClaw \
                AIZ_AIZEggRobo:Game_AIZEggRobo \
                ERZ_PhantomRuby:Game_PhantomRuby; do
        src_tu="${waiz%%:*}"; out_tu="${waiz##*:}"
        "$CC" -x c -std=gnu11 -m2 -Os -fno-builtin -ffunction-sections -fdata-sections \
            $GAME_DEFS -DP6_AIZ_TEST -DP6_FRONTEND_MENU -I"$GINC" -I"$NEWLIB" \
            -c -o "$P6/$out_tu.o" "/work/tools/_decomp_raw/SonicMania_Objects_$src_tu.c"
    done
    # Game_FXRuby.o from the REV02-patched build-local copy.
    "$CC" -x c -std=gnu11 -m2 -Os -fno-builtin -ffunction-sections -fdata-sections \
        $GAME_DEFS -DP6_AIZ_TEST -DP6_FRONTEND_MENU -I"$GINC" -I"$NEWLIB" \
        -c -o "$P6/Game_FXRuby.o" "$P6/_FXRuby_rev02.c"
    # Task #309 (P6_GHZCUT_BOOT): the AIZ->GHZCutscene destination's 2 NEW cutscene-driver
    # objects, compiled VERBATIM from the cached decomp the same waiz way (GHZSetup/BGSwitch/
    # FXRuby are ALREADY compiled above -- w4 loop + AIZ block). Built only in the GHZCUT
    # flavor (which implies P6_AIZ_TEST -> P6_FRONTEND_MENU); they link into the OVERLAY
    # (build_shipping.sh [3b]), NOT the pack. -Os census knob matches the other game TUs.
    # MANIA_USE_PLUS=0 (GAME_VERSION=3): GHZCutsceneST's SkipCB/Encore branches + the
    # CutsceneHBH editor surface preprocess out (the simple non-Plus arm compiles).
    if [ -n "${P6_GHZCUT_BOOT:-}" ]; then
        for wgc in GHZ_GHZCutsceneST:Game_GHZCutsceneST \
                   Cutscene_CutsceneHBH:Game_CutsceneHBH; do
            src_tu="${wgc%%:*}"; out_tu="${wgc##*:}"
            "$CC" -x c -std=gnu11 -m2 -Os -fno-builtin -ffunction-sections -fdata-sections \
                $GAME_DEFS -DP6_AIZ_TEST -DP6_FRONTEND_MENU -DP6_GHZCUT_BOOT -I"$GINC" -I"$NEWLIB" \
                -c -o "$P6/$out_tu.o" "/work/tools/_decomp_raw/SonicMania_Objects_$src_tu.c"
        done
    fi
fi
# Integer-only vsprintf shim (rationale in-file: newlib's float closure is
# 10.2 KB and breached the 0x060C0000 floor by 2,856 B -- MEASURED).
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -ffunction-sections -fdata-sections \
    -I"$NEWLIB" \
    -c -o "$P6/p6_vsprintf.o" "$P6/p6_vsprintf.c"

# O1 (Task #254): GHZ zone-overlay ENTRY TU (multi-class). Compiled -x c with the
# census Game.h tree (for Spring's verbatim globals/callbacks). WITH
# -ffunction-sections so ovl_ring.ld can place .text.p6_overlay_entry FIRST at the
# window base (this TU has 2 functions, unlike the single-function Ring entry, so
# source order alone did NOT keep the entry at offset 0 -- MEASURED: entry landed
# +0x68). NOT a pack member -- links into cd/OVLRING.BIN (build_shipping.sh [3b])
# alongside p6_ring2.o + Game_Spring.o, vs game.elf (-R). GAME_DEFS/GINC in scope.
echo "[7l2] p6_ovl_ghz.o (O1 GHZ overlay entry -- Ring + Spring multi-class) ..."
# PERF (2026-06-18): forward P6_NOSCAN -> -DP6_PERF_NOSCAN so the overlay's per-frame
# badnik draw-state scan (p6_ovl_ghz.c BD_SCAN, ~19ms/frame, pure diagnostic) is
# compile-stripped from the SHIPPING build, the same way the pack's census is. Without
# this the #ifndef in the overlay never engages -> the scan ships -> fps halves.
$CC -x c -std=gnu11 -m2 -O2 -fno-builtin -ffunction-sections -fdata-sections \
    $GAME_DEFS ${P6_NOSCAN:+-DP6_PERF_NOSCAN} ${P6_BACKTRACK_PROOF:+-DP6_BACKTRACK_PROOF} ${P6_BT_NOSKIP:+-DP6_BT_NOSKIP} ${P6_POOLINV_LEAK:+-DP6_POOLINV_LEAK} ${P6_STREAM_PROOF:+-DP6_STREAM_PROOF} ${P6_FRONTEND_LOGOS:+-DP6_FRONTEND_LOGOS} ${P6_FRONTEND_TITLE:+-DP6_FRONTEND_TITLE} ${P6_FRONTEND_MENU:+-DP6_FRONTEND_MENU} ${P6_AIZ_TEST:+-DP6_AIZ_TEST} ${P6_GHZCUT_BOOT:+-DP6_GHZCUT_BOOT} ${P6_GHZCUT_HOLD:+-DP6_GHZCUT_HOLD} ${P6_GHZCUT_HOLD_WHITE:+-DP6_GHZCUT_HOLD_WHITE=$P6_GHZCUT_HOLD_WHITE} ${P6_MENU_AUTOSELECT:+-DP6_MENU_AUTOSELECT} ${P6_TITLEBG_SPRITES_OFF:+-DP6_TITLEBG_SPRITES_OFF} ${P6_TITLE3D_ON:+-DP6_TITLE3D_ON} ${P6_DDWRECKER:+-DP6_DDWRECKER} ${P6_DDW_ARENA:+-DP6_DDW_ARENA} ${P6_DDW_KILL:+-DP6_DDW_KILL} -I"$GINC" -I"$P6" -I"$NEWLIB" \
    -c -o "$P6/p6_ovl_ghz.o" "$P6/p6_ovl_ghz.c"

echo "[7n] SaturnLayout.o (P6.7 W11a: layout band store + camera-local sliding windows; miniz inflate decoder) ..."
$CC $CXXFLAGS $MINIZ_DEFS -I"$DEPS" -I"$NEWLIB" \
    -c -o "$P6/SaturnLayout.o" "$PLAT/SaturnLayout.cpp"

echo "[7o] SaturnSheet.o (P6.7 W12 sprite-sheet band stores; Task #241: P6_CART relocates the store to the 4MB cart) ..."
# Task #244: SaturnSheet ALWAYS uses the 4MB-cart band store (0x227A0000, 384 KB).
# The project targets the Extended-RAM cart (ss.cart=extram4; Data.rsdk ingestion
# requires it), and the 245 KB VDP2-fallback store overflows on the 7th sheet
# (TAILS1 by 19,105 B -> Tails1 fails to stage -> Tails surface unbound -> Tails
# invisible). Hardcoded (not ${P6_CART:+...}) because build_shipping.sh never set
# P6_CART, so shipping silently used the small store. Storage/Scene stay WRAM
# (env-gated) -- the cart's first 3.64 MB is unused here, so 0x227A0000 is free.
# CP5b.1 (#268): thread P6_FRONTEND_TITLE so SaturnSheet's SATURNSHEET_SLOTS=11 branch
# (slot 9=LOGOS, slot 10=TLOGO) compiles for the Title flavor. The Logos flavor keeps
# 10; the default GHZ build sets neither -> 9 (byte-identical s_sheets[]).
# M1b: thread P6_FRONTEND_MENU so SaturnSheet's SATURNSHEET_SLOTS=16 branch (the 2
# extra menu sheets MAINICON/TEXTEN) compiles for the Menu flavor.
# R3.1 (#305): AIZOBJ.SHT reuses the MENU 16-slot table (slot 15); NO P6_AIZ_TEST slot
# bump (a 17th slot shifted .bss -> #228 boot trap, MEASURED blue-screen at _end 0x060BA360).
$CC $CXXFLAGS $MINIZ_DEFS -DP6_CART ${P6_FRONTEND_LOGOS:+-DP6_FRONTEND_LOGOS} ${P6_FRONTEND_TITLE:+-DP6_FRONTEND_TITLE} ${P6_FRONTEND_MENU:+-DP6_FRONTEND_MENU} ${P6_FRAMEDIR:+-DP6_FRAMEDIR} -I"$DEPS" -I"$NEWLIB" \
    -c -o "$P6/SaturnSheet.o" "$PLAT/SaturnSheet.cpp"

# Stage-1 FRD (sprite frame directory, feature checklist sec 7): the pre-cut
# frame-directory store TU. Compiled + linked ONLY under P6_FRAMEDIR -- the
# default build's pack object list is unchanged (byte-identical). Stale-.o
# hygiene: rm when the flag is off so a flavor switch can never link a stale
# SaturnFrameDir.o (the jo-pool-stale-core-o gotcha class).
if [ -n "${P6_FRAMEDIR:-}" ]; then
    echo "[7o2] SaturnFrameDir.o (P6_FRAMEDIR stage-1: pre-cut FRD1 frame-directory store) ..."
    $CC $CXXFLAGS -DP6_FRAMEDIR -I"$DEPS" -I"$NEWLIB" \
        -c -o "$P6/SaturnFrameDir.o" "$PLAT/SaturnFrameDir.cpp"
else
    rm -f "$P6/SaturnFrameDir.o"
fi

echo "[8/8] p6_scene_pack.o (ld -r --gc-sections, roots: p6_scene_run + map-required witnesses) ..."
# NOTE: no libm in the pack. Text.cpp's MD5 T-table is BAKED for Saturn
# (MD5Table_Saturn.inc; Text.cpp Saturn branch) because (a) its upstream
# runtime form is a C++ dynamic initializer SLSTART never runs, and (b) the
# sin/pow soft-double closure measured the pack 644 B OVER the WRAM-H budget.
# Roots: p6_scene_run (the hook entry) transitively keeps every witness it
# writes + the engine closure; p6_w_magic is const + unreferenced so it needs
# an explicit root (the gate byte-order-calibrates from it, qa_p6_scene.py:148).
# The _sbrk + syscall stubs ALSO need explicit roots: nothing inside the pack
# references them (newlib's sbrkr/abort members reference them only at the
# FINAL link), so without -u the gc drops them and the final link silently
# pulls newlib's lib_a-syscalls.o instead -- whose _sbrk grows from _end
# straight into the 0x060C0000 SGL work area (measured ABSENT on the first
# pack build, 2026-06-09). C-level `_sbrk` is linker-level `__sbrk` here.
"$LD" -r --gc-sections \
    -u _p6_scene_run \
    -u _p6_scene_tick \
    -u _p6_engine_boot_and_run \
    -u _p6_lean_boot \
    ${P6_GHZ_AUTORUN:+-u _p6_w_plr_draws -u _p6_w_btch_calls -u _p6_w_btch_hits -u _p6_w_btch_lastdy -u _p6_w_btch_lastvy -u _p6_w_arun_brg_live -u _p6_w_arun_brg_active -u _p6_w_arun_brg_firstx -u _p6_w_arun_brg_gapmiss -u _p6_w_arun_inspan} \
    -u _p6_w_perf_vblanks -u _p6_w_perf_frames -u _p6_w_perf_vbl_max \
    -u _p6_w_perf_cyc_input -u _p6_w_perf_cyc_obj -u _p6_w_perf_cyc_draw \
    -u _p6_w_perf_cyc_present -u _p6_w_perf_cyc_total -u _p6_w_perf_cks \
    -u _p6_w_perf_vbl_frame -u _p6_w_perf_vbl_jo -u _p6_w_perf_vbl_jo_max \
    -u _p6_w_perf_vbl_input -u _p6_w_perf_vbl_obj -u _p6_w_perf_vbl_draw \
    -u _p6_w_perf_vbl_present \
    -u _p6_w_perf_v1_done -u _p6_w_perf_v1_busy -u _p6_w_perf_v1_copr \
    -u _p6_w_perf_v1_lopr -u _p6_w_perf_v1_edsr \
    -u _p6_w_perf_v1_busyvbl -u _p6_w_perf_v1_totvbl \
    -u _p6_w_perf_synch_frt -u _p6_w_perf_synch_max \
    -u _p6_w_perf_full_frt -u _p6_w_perf_full_max -u _p6_w_perf_head_frt \
    -u _p6_w_perf_kick_frt -u _p6_w_perf_tail_frt ${P6_STREAM_PERF:+-u _p6_w_perf_stream_frt} \
    ${P6_FRONTEND_MENU:+-u _p6_pool_remap_c} \
    -u _p6_w_xing_count -u _p6_w_xing_max_frt -u _p6_w_xing_present_max \
    -u _p6_w_manifest_n -u _p6_w_manifest_maxslot -u _p6_w_manifest_csum \
    -u _p6_w_phantom_purged -u _p6_w_i2_resolve_ok -u _p6_w_remap_ok \
    -u _p6_w_scancull_n -u _p6_w_scancull_near -u _p6_w_scan_always -u _p6_w_scancull_capped \
    -u _p6_w_dorm_bytes -u _p6_w_dorm_magic -u _p6_w_dorm_slots \
    -u _p6_w_mat_slot -u _p6_w_mat_classid -u _p6_w_mat_classcount -u _p6_w_mat_posx -u _p6_w_mat_posy \
    -u _p6_w_mat_nvars -u _p6_w_mat_nmatch -u _p6_w_mat_v0 -u _p6_w_mat_v1 -u _p6_w_mat_v2 -u _p6_w_mat_v3 \
    -u _p6_w_pool_npop -u _p6_w_pool_maxls -u _p6_w_pool_firstgap \
    -u _p6_w_compact_n -u _p6_w_compact_sphys -u _p6_w_compact_dummy \
    -u _p6_w_compact_bij_ok -u _p6_w_compact_lastL -u _p6_w_compact_lastP \
    -u _p6_eng_pool_flip -u _p6_eng_pool_geom -u _p6_eng_create -u _p6_stream_tick \
    -u _p6_w_stream_mat -u _p6_w_stream_dorm -u _p6_w_stream_free \
    -u _p6_w_stream_resident -u _p6_w_stream_starve \
    -u _p6_w_bt_logical -u _p6_w_bt_cid -u _p6_w_bt_life -u _p6_w_bt_reappear -u _p6_w_pool_inv_bad \
    -u _p6_eng_classid_resolve -u _p6_eng_serialize_begin -u _p6_eng_var_offset \
    -u _p6_eng_serialize_end -u _p6_eng_entity_prepare -u _p6_eng_write_placement \
    -u _p6_perf_vdp1_edsr -u _p6_perf_vdp1_lopr -u _p6_perf_vdp1_copr \
    -u _p6_w_slave_ticks \
    -u _p6_w_present_vbl_walk -u _p6_w_present_vbl_map \
    -u _p6_w_present_vbl_hash -u _p6_w_present_refills \
    -u _p6_fg_vblank -u _p6_w_fg_dma -u _p6_w_fg_highfill \
    -u _p6_fg_dma_pending -u _p6_fg_scroll_x -u _p6_fg_scroll_y \
    -u _p6_w_obj_inrange -u _p6_w_obj_topclass -u _p6_w_obj_topcount \
    -u _p6_w_obj_classcnt \
    -u _p6_w_objupd_vbl -u _p6_w_objupd_n -u _p6_w_objupd_us -u _p6_w_objupd_topclass \
    -u _p6_w_objupd_topvbl -u _p6_w_objupd_topus -u _p6_w_objupd_topn \
    -u _p6_w_hog_cid -u _p6_w_hog_x -u _p6_w_hog_y -u _p6_w_obj_refills \
    -u _p6_w_objsec_loop1 -u _p6_w_objsec_loop2 -u _p6_w_objsec_loop3 \
    -u _p6_w_draw_sort -u _p6_w_draw_cb -u _p6_w_draw_maxgrp -u _p6_w_draw_nents \
    -u _p6_w_scan_pop -u _p6_w_scan_maxslot -u _p6_w_scan_bounds \
    ${P6_SHADOW:+-u _p6_w_scan_divergence -u _p6_w_scan_divmax} \
    -u _p6_w_lay_slot_refills \
    -u _p6_w_col_t1hash -u _p6_w_col_nowhash -u _p6_w_col_badframe \
    -u _p6_w_lay_ring_wx -u _p6_w_lay_ring_wy -u _p6_w_lay_ring_pos \
    -u _SaturnLayout_SlotLayer \
    -u _p6_w_transitions -u _p6_w_xtile_lo -u _p6_w_xtile_hi \
    -u _p6_w_load_step \
    -u _p6_w_gfs_seeks_real -u _p6_w_gfs_io_vbl ${P6_FRONTEND_LOGOS:+-u _p6_w_gfs_bytes} \
    -u _p6_w_ghz2_loaded -u _p6_w_ghz2_xtile_lo -u _p6_w_ghz2_xtile_hi \
    -u _p6_w_ghz2_entcount -u _p6_w_ghz2_plrx -u _p6_w_ghz2_plry \
    -u _p6_w_ghz2_listpos -u _p6_w_ghz2_play_frames -u _p6_w_ghz2_max_plry \
    -u _p6_w_ghz2_exit_lp \
    -u _p6_w_ghz2_collplane -u _p6_w_ghz2_colllayers -u _p6_w_ghz2_tilecoll \
    -u _p6_w_ghz2_floorlayer -u _p6_w_ghz2_fghigh_idx -u _p6_w_ghz2_fglow_tile \
    -u _p6_w_ghz2_fghigh_tile -u _p6_w_ghz2_feetty -u _p6_w_ghz2_vely -u _p6_w_ghz2_tcoff \
    -u _p6_w_ghz2_slot1layer \
    -u _p6_w_warp_plrx -u _p6_w_warp_signactive \
    -u _p6_w_brg_classid -u _p6_w_brg_count -u _p6_w_brg_posx \
    -u _p6_w_brg_posy -u _p6_w_brg_onscreen -u _p6_w_brg_frames \
    -u _p6_w_plr_newgame_pre_rings -u _p6_w_plr_newgame_pre_pwr \
    -u _p6_w_plr_live_rings -u _p6_w_plr_live_shield \
    -u _p6_w_time_enabled -u _p6_w_timer \
    -u _p6_w_ghzobj_slot -u _p6_w_ghzobj_h0 -u _p6_w_brg_surfslot \
    -u _p6_w_brg_surfscope -u _p6_w_brg_surfh0 \
    -u _p6_w_loop_regmask -u _p6_w_loop_pscount \
    -u _p6_w_stg_limit -u _p6_w_spring_classid -u _p6_w_spring_frames \
    -u _Player_CheckCollisionBox -u _Player_CheckCollisionPlatform \
    -u _Player_CheckCollisionTouch -u _Player_State_Air -u _Player_State_Ground \
    -u _Player_State_Roll -u _Player_State_TubeAirRoll -u _Player_State_TubeRoll \
    -u _Ice_PlayerState_Frozen \
    -u _Player_GetHitbox -u _Player_State_KnuxLedgePullUp -u _Zone_RotateOnPivot \
    -u _BurningLog \
    -u _Player_Hurt -u _APICallback_UnlockAchievement -u _achievementList \
    -u _p6_w_spikelog_classid -u _p6_w_spikelog_frames \
    -u _p6_w_spikelog_aniframes -u _p6_w_spring_aniframes -u _p6_w_brg_aniframes \
    -u _p6_w_ring_aniframes -u _p6_w_ring_classid -u _p6_w_spikes_aniframes -u _p6_w_b1_registered \
    -u _p6_w_b2_registered -u _p6_w_b2_cids -u _p6_w_explosion_aniframes -u _p6_w_animals_aniframes -u _p6_w_newtron_aniframes \
    -u _p6_vdp1_handle_for_surface \
    -u _p6_w_ghzobj_surf_idx -u _p6_w_ghzobj_surf_slot -u _p6_w_ghzobj_surf_scope \
    -u _p6_w_ghzobj_surf_handle -u _p6_w_surfpop \
    -u _p6_w_explos_slot -u _p6_w_animals_slot \
    -u _p6_w_bd_found -u _p6_w_bd_classid -u _p6_w_bd_posx -u _p6_w_bd_posy \
    -u _p6_w_bd_onscreen -u _p6_w_bd_visible -u _p6_w_bd_drawgrp -u _p6_w_bd_active \
    -u _p6_w_bd_framesNN -u _p6_w_bd_animid -u _p6_w_bd_frameid -u _p6_w_bd_sheetid \
    -u _p6_w_bd_handle -u _p6_w_bd_drawn \
    -u _p6_w_bind_demand -u _p6_w_bind_log16 \
    -u _Player_CheckBadnikBreak -u _Player_ProjectileHurt -u _Player_CheckBadnikTouch \
    ${P6_DDWRECKER:+-u _p6_w_ddw_classid -u _Player_CheckBossHit} \
    ${P6_DDW_ARENA:+-u _p6_w_ddw_warp_fired -u _p6_w_ddw_seen -u _p6_w_ddw_state0 -u _p6_w_ddw_health_min} \
    ${P6_DDW_KILL:+-u _p6_w_ddw_hits_injected -u _p6_w_ddw_sign_live} \
    -u _p6_ovl_badnikbreak_unseeded_raw -u _p6_ovl_badnikbreak_raw \
    -u _DrawHelpers_DrawHitboxOutline \
    -u _Platform -u _Press -u _Crate -u _Ice -u _BigSqueeze -u _SpikeCorridor \
    -u _Platform_State_Falling2 -u _Platform_State_Hold \
    -u _Platform_State_Fall \
    -u _LRZConvItem -u _LRZConvItem_HandleLRZConvPhys \
    -u _Player_GiveRings -u _Player_GiveLife -u _Player_ApplyShield \
    -u _Player_UpdatePhysicsState -u _Player_TryTransform \
    -u _Zone_StartTeleportAction -u _Music_PlayJingle -u _Music_JingleFadeOut \
    -u _CompetitionSession_GetSession -u _Shield -u _ImageTrail \
    -u _p6_ovl_itembox_break_raw -u _p6_ovl_itembox_state_broken_raw \
    -u _p6_ovl_itembox_state_falling_raw -u _p6_ovl_itembox_state_idle_raw \
    -u _p6_ovl_debris_state_move_raw \
    -u _p6_w_itembox_classid -u _p6_w_itembox_aniframes \
    -u _p6_w_debris_classid -u _p6_w_invstars_classid -u _p6_w_batbrain_aniframes \
    -u _p6_w_scorebonus_classid -u _p6_w_scorebonus_aniframes \
    -u _p6_w_dust_classid -u _p6_w_dust_aniframes \
    -u _p6_w_shield_classid -u _p6_w_shield_aniframes \
    -u _p6_w_boundsmarker_classid \
    -u _p6_w_breakwall_classid \
    -u _p6_w_pdiag_posx -u _p6_w_pdiag_gvel -u _p6_w_pdiag_velx -u _p6_w_pdiag_gnd \
    -u _p6_w_pdiag_plane -u _p6_w_pdiag_mode -u _p6_w_pdiag_dir -u _p6_w_pdiag_ang \
    -u _HangPoint -u _PlatformNode -u _PlatformControl -u _TurboTurtle \
    -u _Player_State_Static -u _Player_Action_Jump -u _Player_State_KnuxWallClimb \
    -u _p6_w_platform_classid -u _p6_w_platform_aniframes \
    -u _p6_w_invblock_classid \
    ${P6_GHZCUT_BOOT:+-u _FXTrail} \
    ${P6_GHZCUT_BOOT:+-u _p6_w_hbh_slot -u _p6_w_hbh_aniframes -u _p6_w_hbh_landed -u _p6_heavy_palblock} \
    ${P6_GHZCUT_BOOT:+-u _p6_w_hbh_count -u _p6_w_hbh_vis -u _p6_w_hbh_posy -u _p6_w_hbh_posx -u _p6_w_hbh_handle -u _p6_w_hbh_camy -u _p6_w_hbh_animid} \
    ${P6_GHZCUT_BOOT:+-u _p6_w_plrsht_slot -u _p6_w_plr_cut_anif -u _p6_w_plr_cut_anif2 -u _p6_w_plr_cut_aniid -u _p6_w_plr_cut_surf -u _p6_w_plr_cut_handle -u _p6_w_plr_cut_landed -u _p6_plr_sheet_slot} \
    -u _p6_saturn_anim_allocfail -u _p6_w_anim_lastfail -u _p6_w_stg_at_fail \
    -u _p6_w_anim_log -u _p6_w_anim_logn -u _p6_w_anim_step \
    -u _p6_w_objapk_bytes \
    -u _p6_w_ac_classid -u _p6_w_ac_state -u _p6_w_ac_timer -u _p6_w_ac_frames \
    -u _p6_w_ac_objcid -u _p6_w_sign_state -u _p6_w_ring_cid \
    -u _p6_w_ac_laststate -u _p6_w_listpos_max \
    -u _p6_w_mount_tag -u _p6_w_mount_listpos \
    -u _p6_w_frontend_folder_tag -u _p6_w_logosetup_classid \
    -u _p6_w_uipicture_classid -u _p6_w_logos_objcount \
    -u _p6_w_titlesetup_classid -u _p6_w_titlelogo_classid -u _p6_w_title_objcount \
    ${P6_FRONTEND_MENU:+-u _p6_w_menusetup_classid -u _p6_w_uicontrol_classid -u _p6_w_menu_objcount} \
    ${P6_FRONTEND_MENU:+-u _p6_w_menu_treebuilt -u _p6_w_menu_modebtn_classid -u _p6_w_menu_vdp1_landed} \
    ${P6_FRONTEND_MENU:+-u _p6_w_menu_edge_calls -u _p6_w_menu_edge_last} \
    ${P6_FRONTEND_MENU:+-u _p6_w_draw_tail} \
    ${P6_FRONTEND_MENU:+-u _p6_w_menu_saveslot_classid -u _p6_w_menu_input_seen} \
    ${P6_FRONTEND_MENU:+-u _p6_w_uitrans_present -u _p6_w_uitrans_state -u _p6_w_uitrans_timer -u _p6_w_uitrans_istrans -u _p6_w_uitrans_active} \
    ${P6_FRONTEND_MENU:+-u _p6_w_active_btn_actioncb -u _p6_w_active_btn_id -u _p6_w_active_btn_count} \
    ${P6_FRONTEND_MENU:+-u _p6_w_ctrl_posx -u _p6_w_ctrl_tgtx} \
    ${P6_FRONTEND_MENU:+-u _p6_w_ctrl_state -u _p6_w_ctrl_active -u _p6_w_nosave_pi -u _p6_w_nosave_confirm} \
    ${P6_FRONTEND_MENU:+-u _p6_w_nosave_gate -u _p6_w_slot_state -u _p6_w_slot_fxradius} \
    ${P6_FRONTEND_MENU:+-u _p6_w_gate_seldisabled -u _p6_w_ctrl_seldisabled} \
    ${P6_MENU_AUTOSELECT:+-u _p6_w_as_stage} \
    ${P6_AIZ_TEST:+-u _p6_w_aiz_loaded -u _p6_w_aiz_fg_hash -u _p6_w_aiz_objcount -u _p6_w_aiz_nlayers} \
    ${P6_AIZ_TEST:+-u _p6_w_aiz_fglow_idx -u _p6_w_aiz_slots -u _p6_w_aiz_scrx -u _p6_w_aiz_scry} \
    ${P6_AIZ_TEST:+-u _p6_w_aiz_cutscene_state -u _p6_w_aiz_setup_classid -u _p6_w_aiz_seq_classid} \
    ${P6_AIZ_TEST:+-u _p6_w_aiz_tornado_classid -u _p6_w_aiz_path_classid -u _p6_w_aiz_cam_x} \
    ${P6_AIZ_TEST:+-u _p6_w_aiz_torn_x -u _p6_w_aiz_torn_dis -u _p6_w_aiz_torn_active -u _p6_w_aiz_torn_state -u _p6_w_aiz_path_movevel} \
    ${P6_AIZ_TEST:+-u _p6_w_aiz_pn_type -u _p6_w_aiz_pn_tgtspd -u _p6_w_aiz_pn_speed -u _p6_w_aiz_pn_state -u _p6_w_aiz_pn_active} \
    ${P6_AIZ_TEST:+-u _p6_w_aiz_sp_ptr -u _p6_w_aiz_sp_post0} \
    ${P6_AIZ_TEST:+-u _p6_w_aiz_trio_load -u _p6_w_aiz_trio_init -u _p6_w_aiz_trio_purge -u _p6_w_aiz_player_cls} \
    ${P6_AIZ_TEST:+-u _p6_w_aiz_gt_ctx -u _p6_w_aiz_gt_a -u _p6_w_aiz_gt_b} \
    ${P6_AIZ_TEST:+-u _p6_w_aiz_bg_filebytes -u _p6_w_aiz_bg_loaded -u _p6_w_aiz_bg_chrw0 -u _p6_w_aiz_bg_cram -u _p6_w_aiz_bg_nbg0 -u _p6_w_aiz_bg_ctx} \
    ${P6_AIZ_TEST:+-u _p6_w_aizobj_slot -u _p6_w_aiz_tornado_frames -u _p6_w_aiz_torn_aniframes -u _p6_w_aiz_torn_animid -u _p6_w_aiz_torn_count} \
    ${P6_AIZ_TEST:+-u _p6_w_aiz_claw_aniframes -u _p6_w_aiz_eggrobo_aniframes} \
    ${P6_AIZ_TEST:+-u _p6_w_aiz_p2_classid -u _p6_w_aiz_p2_onground -u _p6_w_aiz_p2_posx -u _p6_w_aiz_p2_posy -u _p6_w_aiz_p2_vely -u _p6_w_aiz_p2_sidekick -u _p6_w_aiz_p1_posy} \
    ${P6_AIZ_TEST:+-u _p6_w_aiz_p2_stateptr -u _p6_w_aiz_p2_inputptr -u _p6_w_aiz_p2_tilecoll -u _p6_w_aiz_p2_visible} \
    ${P6_AIZ_TEST:+-u _p6_w_aiz_ruby_active -u _p6_w_aiz_ruby_timer -u _p6_w_aiz_ruby_flashfin} \
    ${P6_FRONTEND_MENU:+-u _p6_w_menu_startscene_tag -u _p6_w_menu_start_cat -u _p6_w_menu_start_listpos} \
    ${P6_FRONTEND_MENU:+-u _p6_menu_start_witness_root} \
    ${P6_FRONTEND_MENU:+-u _MathHelpers_PointInHitbox} \
    ${P6_AIZ_TEST:+-u _DrawHelpers_DrawCross} \
    ${P6_FRONTEND_LOGOS:+-u _p6_w_uipicture_aniframes -u _p6_w_uipicture_framesNN} \
    ${P6_FRONTEND_LOGOS:+-u _p6_w_uipic_drawgrp -u _p6_w_uipic_active -u _p6_w_uipic_visible -u _p6_w_uipic_onscreen} \
    ${P6_FRONTEND_LOGOS:+-u _p6_w_uipic_posx -u _p6_w_uipic_posy -u _p6_w_uipic_animid -u _p6_w_uipic_frameid} \
    ${P6_FRONTEND_LOGOS:+-u _p6_w_uipic_sheetid -u _p6_w_uipic_handle} \
    ${P6_FRONTEND_LOGOS:+-u _p6_w_logos_shtslot -u _p6_w_logos_surfidx -u _p6_w_logos_surfslot -u _p6_w_logos_surfscope -u _p6_w_logos_surfh0 -u _p6_w_logos_h0} \
    ${P6_FRONTEND_TITLE:+-u _p6_w_tlogo_shtslot -u _p6_w_tlogo_surfidx -u _p6_w_tlogo_surfslot -u _p6_w_tlogo_surfscope -u _p6_w_tlogo_surfh0 -u _p6_w_tlogo_h0} \
    ${P6_FRONTEND_TITLE:+-u _p6_w_tlogo_drawgrp -u _p6_w_tlogo_visible -u _p6_w_tlogo_onscreen -u _p6_w_tlogo_type -u _p6_w_tlogo_sheetid -u _p6_w_tlogo_handle -u _p6_w_tlogo_landed} \
    ${P6_FRONTEND_TITLE:+-u _p6_w_tlogo_existmask -u _p6_w_tlogo_vismask -u _p6_w_tlogo_onscrmask -u _p6_w_tlogo_boundmask -u _p6_w_tsetup_statetag} \
    ${P6_FRONTEND_TITLE:+-u _p6_w_tsonic_shtslot -u _p6_w_tsonic_surfidx -u _p6_w_tsonic_surfslot -u _p6_w_tsonic_surfscope -u _p6_w_tsonic_surfh0 -u _p6_w_tsonic_h0} \
    ${P6_FRONTEND_TITLE:+-u _p6_w_tsonic_bandpre -u _p6_w_tsonic_bandcap -u _p6_w_tsonic_stageret} \
    ${P6_FRONTEND_TITLE:+-u _p6_w_tsonic_visible -u _p6_w_tsonic_onscreen -u _p6_w_tsonic_sheetid -u _p6_w_tsonic_handle -u _p6_w_tsonic_animid -u _p6_w_tsonic_frameid} \
    ${P6_FRONTEND_TITLE:+-u _p6_w_title_backdrop_done -u _p6_w_title_backdrop_armed} \
    ${P6_FRONTEND_TITLE:+-u _p6_w_title_island_armed -u _p6_w_title_island_angle -u _p6_w_title_island_kast -u _p6_w_title_island_coeff0 -u _p6_w_title_island_rpta -u _p6_w_isl_tx -u _p6_w_isl_ty -u _p6_w_title_clouds_armed -u _p6_w_title_clouds_ntiles} \
    ${P6_FRONTEND_TITLE:+-u _p6_w_tbg_shtslot -u _p6_w_tbg_surfidx -u _p6_w_tbg_surfslot -u _p6_w_tbg_handle} \
    ${P6_FRONTEND_TITLE:+-u _p6_w_titlebg_classid -u _p6_w_title3d_classid} \
    ${P6_FRONTEND_TITLE:+-u _p6_w_titlebg_vis -u _p6_w_title3d_vis} \
    ${P6_FRONTEND_CHAIN:+-u _p6_w_chain_fired -u _p6_w_chain_folder_pre -u _p6_w_chain_listpos_adv -u _p6_w_chain_listpos_title} \
    ${P6_FRAMEDIR:+-u _p6_w_frd_staged -u _p6_w_frd_active -u _p6_w_frd_lookups -u _p6_w_frd_misses} \
    ${P6_FRAMEDIR:+-u _p6_w_frd_hash -u _p6_w_frd_bytes -u _p6_w_frd_frames} \
    ${P6_FRAMEDIR:+-u _p6_w_frd_missrect -u _p6_w_frd_misswh -u _p6_w_frd_missslot} \
    ${P6_FRAMEDIR:+-u _p6_w_slot19_class -u _p6_w_slot19_class2 -u _p6_w_slot19_x -u _p6_w_slot19_y -u _p6_w_slot19_hits} \
    ${P6_FRAMEDIR:+-u _SaturnFrameDir_Lookup -u _SaturnFrameDir_StageDirect -u _SaturnFrameDir_Reset} \
    ${P6_FRONTEND_LOGOS:+-u _p6_w_lt_vbl -u _p6_w_lt_fills -u _p6_w_lt_kb -u _p6_w_lt_frt} \
    ${P6_FRONTEND_LOGOS:+-u _p6_w_lt_cks -u _p6_w_lt_masked_vbl -u _p6_w_lt_ph2_fills -u _p6_w_lt_ph2_vbl -u _p6_w_lt_sfx_savedopen} \
    ${P6_FRONTEND_TITLE:+-u _p6_w_p2seeks} ${P6_FRONTEND_LOGOS:+-u _p6_w_p2seeks} \
    -u _p6_w_sign_crossed \
    -u _p6_w_sign_count -u _p6_w_sign_type -u _p6_w_sign_posx \
    -u _p6_w_cart_ok -u _p6_w_cart_rb0 -u _p6_w_cart_rb1 \
    -u _p6_w_magic \
    -u _p6_bridge_proc_anim -u _p6_bridge_draw_sprite -u _p6_scene_entity \
    -u _p6_w_obj_classid -u _p6_w_obj_timer -u _p6_w_obj_vely \
    -u _p6_w_obj_posy -u _p6_w_obj_scalex -u _p6_w_obj_frameid \
    -u __sbrk -u __exit -u __close -u __fstat -u __isatty -u __lseek \
    -u __read -u __write -u __open -u __getpid -u __kill -u __fork \
    -u __wait -u __link -u __unlink -u __stat -u __times \
    -u __gettimeofday -u __execve \
    -u _isatty -u __creat -u __raise -u __chmod -u __chown -u __utime \
    -u __execv -u __pipe -u _errno \
    "$P6/p6_io_main.o" "$P6/p6_gfs.o" "$P6/Core_Reader.o" \
    "$P6/Scene_Scene.o" "$P6/Storage_Storage.o" "$P6/miniz.o" \
    "$P6/Storage_Text.o" "$P6/Graphics_Sprite.o" "$P6/p6_vdp2.o" \
    "$P6/Graphics_Animation.o" "$P6/Audio_Audio.o" \
    "$P6/Scene_Object.o" "$P6/Scene_Collision.o" "$P6/Core_Link.o" "$P6/Core_Math.o" \
    "$P6/Core_RetroEngine.o" \
    "$P6/Scene_Objects_DefaultObject.o" "$P6/Scene_Objects_DevOutput.o" \
    "$P6/Game_Localization.o" "$P6/Game_LogHelpers.o" "$P6/Game_Options.o" \
    "$P6/Game_DrawHelpers.o" \
    "$P6/Game_MathHelpers.o" "$P6/Game_Soundboard.o" \
    "$P6/p6_closure_edge.o" \
    "$P6/Game_BoundsMarker.o" "$P6/Game_Camera.o" "$P6/Game_DebugMode.o" \
    "$P6/Game_Dust.o" "$P6/Game_ERZStart.o" "$P6/Game_GameOver.o" \
    "$P6/Game_HUD.o" "$P6/Game_Ice.o" "$P6/Game_ImageTrail.o" \
    "$P6/Game_Music.o" "$P6/Game_PauseMenu.o" "$P6/Game_Player.o" \
    "$P6/Game_SaveGame.o" "$P6/Game_ScoreBonus.o" "$P6/Game_Shield.o" \
    "$P6/Game_SizeLaser.o" "$P6/Game_Zone.o" "$P6/Game_ActClear.o" \
    "$P6/Game_SignPost.o" "$P6/Game_BGSwitch.o" "$P6/Game_GHZSetup.o" \
    "$P6/p6_wave1_reg.o" "$P6/p6_vsprintf.o" "$P6/SaturnLayout.o" \
    "$P6/SaturnSheet.o" ${P6_FRAMEDIR:+$P6/SaturnFrameDir.o} \
    "$P6/Input_Input.o" "$P6/InputDevice_Saturn.o" "$P6/CppRuntime_Saturn.o" \
    "$P6/p6_stubs.o" "$P6/p6_pack_stubs.o" \
    -o "$P6/p6_scene_gc.o"

# W12b ROOT-CAUSE FIX (Task #227, 2026-06-12): second `ld -r` pass merging
# every -fdata/-ffunction-sections named section into the four standard
# sections (p6_pack_merge.ld). WITHOUT this, the final jo link (COFF-SH
# output via the do-not-modify COMMON sgl.linker) emits the pack's
# .bss.*/.data.* inputs as ORPHAN OUTPUT SECTIONS whose addresses OVERLAP
# the main .bss/.data spans -- MEASURED 179 overlapping pairs
# (qa_p6_mapoverlap.py); p6_typeGroupsBacking overlapped jo's __jo_fs_work
# and LoadSceneFolder's typeGroups[126].entryCount=0 store zeroed the GfsMng
# GFCF_Seek access pointer (gfs_mng+12) -> GFS_Seek jumped through NULL (the
# entire 'W12b layout-sensitive crash' class). gc already ran above, so the
# merge loses nothing.
"$LD" -r -T "$P6/p6_pack_merge.ld" "$P6/p6_scene_gc.o" -o "$P6/p6_scene_pack.o"
# P6.7d.3: p6_ring2.o is NOT a pack member -- it links into the fixed-base
# OVERLAY (build_diag.sh stage [3b]) against the finished game.elf. The
# bridge/witness -u roots above keep the overlay's -R import surface alive
# through this gc (nothing inside the pack references them anymore).

echo "--------------------------------------------------------------"
ls -l "$P6/p6_io_main.o" "$P6/p6_gfs.o" "$P6/Core_Reader.o" \
      "$P6/Scene_Scene.o" "$P6/Storage_Storage.o" "$P6/miniz.o" \
      "$P6/Storage_Text.o" "$P6/p6_scene_pack.o"
echo "DONE [p6_scene_pack.o built]."
