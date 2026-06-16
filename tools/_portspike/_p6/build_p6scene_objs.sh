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
$CC $CXXFLAGS $ENG_DEFS ${P6_XTEST:+-DP6_TRANSITION_TEST} ${P6_WARP:+-DP6_WARP_TEST} ${P6_SHT_NORES:+-DP6_SHT_NO_RESIDENT} ${P6_GHZ2_BOOT:+-DP6_GHZ2_BOOT} $CORE_INC \
    -c -o "$P6/p6_io_main.o" "$P6/p6_io_main.cpp"

echo "[2/7] p6_gfs.o      (Saturn GFS FileIO backend, UPPERCASE basename) ..."
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -ffunction-sections -fdata-sections \
    -I"$SGLINC" -I"$NEWLIB" \
    -c -o "$P6/p6_gfs.o" "$P6/p6_gfs.c"

echo "[3/7] Core_Reader.o (UNMODIFIED engine Reader.cpp) ..."
$CC $CXXFLAGS $ENG_DEFS $CORE_INC \
    -c -o "$P6/Core_Reader.o" "$SRC/RSDK/Core/Reader.cpp"

echo "[4/7] Scene_Scene.o (engine Scene.cpp -- LoadSceneAssets; P6_CART=1 loads full layers resident in cart STG) ..."
$CC $CXXFLAGS $ENG_DEFS ${P6_CART:+-DP6_CART} $CORE_INC \
    -c -o "$P6/Scene_Scene.o" "$SRC/RSDK/Scene/Scene.cpp"

echo "[5/7] Storage_Storage.o (engine Storage.cpp -- InitStorage + AllocateStorage; P6_CART=1 puts STG/TMP pools in the 4MB cart) ..."
$CC $CXXFLAGS $ENG_DEFS ${P6_CART:+-DP6_CART} $CORE_INC \
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
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -ffunction-sections -fdata-sections \
    -I"$SGLINC" -I"$NEWLIB" \
    -c -o "$P6/p6_vdp2.o" "$P6/p6_vdp2.c"
# NOTE: p6_vdp1.c (P6.5b2 sprite half) is NOT built here -- it includes
# jo/jo.h, whose struct layouts depend on the project's jo config flags, so
# the Makefile P6SCENE block compiles it inside the jo make (SRCS +=).

echo "[7d/9] Graphics_Animation.o (UNMODIFIED engine Animation.cpp -- LoadSpriteAnimation + SetSpriteAnimation + ProcessAnimation for P6.5b2; gc drops the string/editor surface) ..."
$CC $CXXFLAGS $ENG_DEFS $CORE_INC \
    -c -o "$P6/Graphics_Animation.o" "$SRC/RSDK/Graphics/Animation.cpp"

echo "[7e/9] Audio_Audio.o (UNMODIFIED engine Audio.cpp -- LoadSfx/LoadSfxToSlot WAV parse + PlaySfx channel allocator + InitAudioChannels for P6.6a; the in-TU stb_vorbis + stream/mixer surface gc-drops while unreferenced; dyn-init audit: all file-scope state constant/zero-init, no .init_array trap) ..."
$CC $CXXFLAGS $ENG_DEFS $CORE_INC \
    -c -o "$P6/Audio_Audio.o" "$SRC/RSDK/Audio/Audio.cpp"

echo "[7f] Scene_Object.o (UNMODIFIED engine Object.cpp -- RegisterObject + ResetEntitySlot + ProcessObjects + ProcessObjectDrawLists for P6.7a; editor/serialize surface gc-drops; +P6_PERF_OBJPROF Phase-2d per-classID Update timing diagnostic) ..."
$CC $CXXFLAGS $ENG_DEFS -DP6_PERF_OBJPROF $CORE_INC \
    -c -o "$P6/Scene_Object.o" "$SRC/RSDK/Scene/Object.cpp"

echo "[7q] Scene_Collision.o (VERBATIM engine Collision.cpp -- P6.7 W15b Task #227: ProcessObjectMovement/ProcessPathGrip/ProcessAirCollision_Down + the Find*/[ LR]Wall/Floor/Roof sensor walkers + CheckObjectCollision*; every tile read goes through the RSDK_*_MASK/_ANGLE seam -> PackedCollisionMask/PackedTileAngle (Scene.hpp:289-378, gate qa_p6_collision K1-K5); CopyCollisionMask + Legacy + ProcessAirCollision_Up preprocess out at REV02/MOD_LOADER=0; -Os NOT -O2: lone-TU census 2026-06-12 measured 18,696 B alloc at -O2 vs 13,016 B at -Os against a 2,708 B pre-land _end margin) ..."
$CC ${CXXFLAGS/-O2/-Os} $ENG_DEFS $CORE_INC \
    -c -o "$P6/Scene_Collision.o" "$SRC/RSDK/Scene/Collision.cpp"

echo "[7r] Input_Input.o (VERBATIM engine Input.cpp -- P6.7 W7 Task #227: the controller/stickL/stickR/triggerL/triggerR/touchInfo arrays + ClearInput/ProcessInput edge logic + InitInputDevices autoassign seed; no PC device backend compiles in (RETRO_INPUTDEVICE_KEYBOARD=0 under RETRO_SATURN, RetroEngine.hpp:449); -Os per the Collision census precedent -- lone-TU census 2026-06-12: text 1,332 B / bss 2,064 B, undefs videoSettings + SKU::userCore only, both pack-resident) ..."
$CC ${CXXFLAGS/-O2/-Os} $ENG_DEFS $CORE_INC \
    -c -o "$P6/Input_Input.o" "$SRC/RSDK/Input/Input.cpp"

echo "[7s] InputDevice_Saturn.o (Saturn SMPC pad device backend -- the AudioDevice_Saturn precedent; SGL Smpc_Peripheral snapshot reader, KBInputDevice.cpp:681-806 registration/Update/Process mirror; census: text 760 B / data 50 B) ..."
$CC ${CXXFLAGS/-O2/-Os} $ENG_DEFS $CORE_INC \
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
$CC $CXXFLAGS $ENG_DEFS -DP6_PACK_STUBS $CORE_INC \
    -c -o "$P6/p6_stubs.o" "$P6/p6_stubs.cpp"

echo "[7jb] p6_pack_stubs.o (the measured 43-symbol function-table closure remainder; real TUs replace these P6.7b+) ..."
$CC $CXXFLAGS $ENG_DEFS $CORE_INC -c -o "$P6/p6_pack_stubs.o" "$P6/p6_pack_stubs.cpp"

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
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -ffunction-sections -fdata-sections \
    $GAME_DEFS -I"$GINC" -I"$NEWLIB" \
    -c -o "$P6/p6_wave1_reg.o" "$P6/p6_wave1_reg.c"
# Player-wave closure boundary (NULL class ptrs + witnessed inert stubs +
# sprintf-over-vsprintf; rationale in-file).
"$CC" -x c -std=gnu11 -m2 -Os -fno-builtin -ffunction-sections -fdata-sections \
    $GAME_DEFS -I"$GINC" -I"$NEWLIB" \
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
for w4 in Common_BGSwitch:Game_BGSwitch \
          GHZ_GHZSetup:Game_GHZSetup; do
    src_tu="${w4%%:*}"; out_tu="${w4##*:}"
    "$CC" -x c -std=gnu11 -m2 -Os -fno-builtin -ffunction-sections -fdata-sections \
        $GAME_DEFS -I"$GINC" -I"$NEWLIB" \
        -c -o "$P6/$out_tu.o" "/work/tools/_decomp_raw/SonicMania_Objects_$src_tu.c"
done
# Integer-only vsprintf shim (rationale in-file: newlib's float closure is
# 10.2 KB and breached the 0x060C0000 floor by 2,856 B -- MEASURED).
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -ffunction-sections -fdata-sections \
    -I"$NEWLIB" \
    -c -o "$P6/p6_vsprintf.o" "$P6/p6_vsprintf.c"

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
$CC $CXXFLAGS $MINIZ_DEFS -DP6_CART -I"$DEPS" -I"$NEWLIB" \
    -c -o "$P6/SaturnSheet.o" "$PLAT/SaturnSheet.cpp"

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
    -u _p6_w_perf_vblanks -u _p6_w_perf_frames -u _p6_w_perf_vbl_max \
    -u _p6_w_perf_cyc_input -u _p6_w_perf_cyc_obj -u _p6_w_perf_cyc_draw \
    -u _p6_w_perf_cyc_present -u _p6_w_perf_cyc_total -u _p6_w_perf_cks \
    -u _p6_w_perf_vbl_frame -u _p6_w_perf_vbl_jo -u _p6_w_perf_vbl_jo_max \
    -u _p6_w_perf_vbl_input -u _p6_w_perf_vbl_obj -u _p6_w_perf_vbl_draw \
    -u _p6_w_perf_vbl_present \
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
    -u _p6_w_lay_slot_refills \
    -u _p6_w_lay_ring_wx -u _p6_w_lay_ring_wy -u _p6_w_lay_ring_pos \
    -u _p6_w_transitions -u _p6_w_xtile_lo -u _p6_w_xtile_hi \
    -u _p6_w_warp_plrx -u _p6_w_warp_signactive \
    -u _p6_w_ac_classid -u _p6_w_ac_state -u _p6_w_ac_timer -u _p6_w_ac_frames \
    -u _p6_w_ac_objcid -u _p6_w_sign_state -u _p6_w_ring_cid \
    -u _p6_w_ac_laststate -u _p6_w_listpos_max \
    -u _p6_w_mount_tag -u _p6_w_mount_listpos \
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
    "$P6/SaturnSheet.o" \
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
