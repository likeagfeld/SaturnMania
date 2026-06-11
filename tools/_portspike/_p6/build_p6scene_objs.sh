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
$CC $CXXFLAGS $ENG_DEFS $CORE_INC \
    -c -o "$P6/p6_io_main.o" "$P6/p6_io_main.cpp"

echo "[2/7] p6_gfs.o      (Saturn GFS FileIO backend, UPPERCASE basename) ..."
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -ffunction-sections -fdata-sections \
    -I"$SGLINC" -I"$NEWLIB" \
    -c -o "$P6/p6_gfs.o" "$P6/p6_gfs.c"

echo "[3/7] Core_Reader.o (UNMODIFIED engine Reader.cpp) ..."
$CC $CXXFLAGS $ENG_DEFS $CORE_INC \
    -c -o "$P6/Core_Reader.o" "$SRC/RSDK/Core/Reader.cpp"

echo "[4/7] Scene_Scene.o (UNMODIFIED engine Scene.cpp -- LoadSceneAssets UNDER TEST) ..."
$CC $CXXFLAGS $ENG_DEFS $CORE_INC \
    -c -o "$P6/Scene_Scene.o" "$SRC/RSDK/Scene/Scene.cpp"

echo "[5/7] Storage_Storage.o (UNMODIFIED engine Storage.cpp -- InitStorage + AllocateStorage) ..."
$CC $CXXFLAGS $ENG_DEFS $CORE_INC \
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

echo "[7f] Scene_Object.o (UNMODIFIED engine Object.cpp -- RegisterObject + ResetEntitySlot + ProcessObjects + ProcessObjectDrawLists for P6.7a; editor/serialize surface gc-drops) ..."
$CC $CXXFLAGS $ENG_DEFS $CORE_INC \
    -c -o "$P6/Scene_Object.o" "$SRC/RSDK/Scene/Object.cpp"

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

echo "[7k] p6_ring2.o (VERBATIM decomp Ring for the pack -- flat TU, bridges into the engine table; p6_gameapi.h two-TU split) ..."
$CC $CXXFLAGS -DRETRO_SATURN_FILEIO -I"$P6" -I"$NEWLIB" \
    -c -o "$P6/p6_ring2.o" "$P6/p6_ring2.cpp"

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
    -u _p6_w_magic \
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
    "$P6/Scene_Object.o" "$P6/Core_Link.o" "$P6/Core_Math.o" \
    "$P6/Core_RetroEngine.o" \
    "$P6/Scene_Objects_DefaultObject.o" "$P6/Scene_Objects_DevOutput.o" \
    "$P6/p6_stubs.o" "$P6/p6_pack_stubs.o" "$P6/p6_ring2.o" \
    -o "$P6/p6_scene_pack.o"

echo "--------------------------------------------------------------"
ls -l "$P6/p6_io_main.o" "$P6/p6_gfs.o" "$P6/Core_Reader.o" \
      "$P6/Scene_Scene.o" "$P6/Storage_Storage.o" "$P6/miniz.o" \
      "$P6/Storage_Text.o" "$P6/p6_scene_pack.o"
echo "DONE [p6_scene_pack.o built]."
