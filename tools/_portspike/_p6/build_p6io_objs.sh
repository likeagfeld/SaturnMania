#!/usr/bin/env bash
# =============================================================================
# build_p6io_objs.sh -- P6.2 Path A (Task #206). Compile the THREE pre-built
# proof objects the full-jo `make P6IO=1` build links in (Makefile ifeq P6IO):
#
#   p6_io_main.o   lean proof driver + 3 RSDK stubs + the witnesses.
#                  Compiled WITHOUT -DP6_IO_TEST -> only the lean p6_io_run()
#                  (no SGL/CD/GFS bring-up, no park) is emitted -- jo owns
#                  slInitSystem/CDC_CdInit/GFS_Init and the jo_core_run park.
#   p6_gfs.o       the Saturn GFS FileIO backend UNDER TEST (Saturn_fOpen/
#                  fRead/fSeek/fTell/fClose ride jo's already-live GFS).
#   Core_Reader.o  the engine's UNMODIFIED RSDK/Core/Reader.cpp (LoadFile ->
#                  fOpen -> fRead), -DRETRO_SATURN_FILEIO so its FileInfo layout
#                  + fXxx routing match p6_io_main.o / p6_gfs.o exactly.
#
# Flags are byte-identical to link_p6.sh's io-jo branch (lines 233-241), the
# build that already proved Core_Reader.o needs ONLY the 3 stubs in
# p6_io_main.cpp. Run INSIDE the Docker toolchain image:
#
#   MSYS_NO_PATHCONV=1 docker run --rm -v "D:/sonicmaniasaturn":/work -w /work \
#       joengine-saturn:latest bash tools/_portspike/_p6/build_p6io_objs.sh
#
# Emits the 3 .o into /work/tools/_portspike/_p6/ (the paths the Makefile P6IO
# block references). NOT an LTO build: these are plain objects, always fully
# linked, and GCC mixes them cleanly into jo's LTO object set at final link.
# =============================================================================
set -eu

CC=/work/jo-engine/Compiler/LINUX/bin/sh-none-elf-gcc-8.2.0
NEWLIB=/work/jo-engine/Compiler/WINDOWS/sh-elf/include
DEPS=/work/rsdkv5-src/dependencies/all
PLAT=/work/platform/Saturn
SHIM=/work/tools/_portspike/shim
SRC=/work/rsdkv5-src/RSDKv5
P6=/work/tools/_portspike/_p6
SGLINC=/work/jo-engine/Compiler/COMMON/SGL_302j/INC

CORE_DEFS="-DRETRO_USE_MOD_LOADER=0 -DRETRO_REVISION=2 -DSCREEN_XMAX=320 -DSCREEN_YSIZE=224"
CORE_INC="-I$PLAT -I$SHIM -I$SRC -I$DEPS -I$NEWLIB"
CXXFLAGS="-x c++ -std=gnu++11 -m2 -O2 -include stdlib.h -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-builtin"

echo "=============================================================="
echo "P6.2 Path A -- compiling the 3 proof objects into \$P6"
echo "=============================================================="

echo "[1/3] p6_io_main.o  (lean p6_io_run -- NO -DP6_IO_TEST) ..."
$CC $CXXFLAGS $CORE_DEFS -DRETRO_SATURN_FILEIO $CORE_INC \
    -c -o "$P6/p6_io_main.o" "$P6/p6_io_main.cpp"

echo "[2/3] p6_gfs.o      (Saturn GFS FileIO backend under test) ..."
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -I"$SGLINC" -I"$NEWLIB" \
    -c -o "$P6/p6_gfs.o" "$P6/p6_gfs.c"

echo "[3/3] Core_Reader.o (UNMODIFIED engine Reader.cpp) ..."
$CC $CXXFLAGS $CORE_DEFS -DRETRO_SATURN_FILEIO $CORE_INC \
    -c -o "$P6/Core_Reader.o" "$SRC/RSDK/Core/Reader.cpp"

echo "--------------------------------------------------------------"
ls -l "$P6/p6_io_main.o" "$P6/p6_gfs.o" "$P6/Core_Reader.o"
echo "DONE [3 proof objects built]."
