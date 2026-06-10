#!/usr/bin/env bash
# =============================================================================
# link_p6.sh -- P6.1 engine-dispatch link (Task #205). RED/GREEN toggle.
#
#   bash tools/_portspike/_p6/link_p6.sh red     # empty table  -> gate RED
#   bash tools/_portspike/_p6/link_p6.sh green    # real  table  -> gate GREEN  (default)
#
# The two builds share the ENTIRE object set EXCEPT the function-table provider:
#
#   RED   : FunctionTables_Saturn.o (empty SetupFunctionTables, zeroed
#           RSDKFunctionTable[]) is linked; Core_Link.o + p6_stubs.o are NOT.
#           p6_main calls the empty SetupFunctionTables -> table stays all-zero
#           -> both slot witnesses 0 (not in .text) -> the bridges' `if(fn)`
#           guards skip -> ProcessAnimation never runs -> last_frameid 0.
#           This is the SAME object set the clean P5 link used (minus p5_*),
#           so it links with no unresolved refs (nothing references the 51
#           backend entry points without Core_Link.o's SetupFunctionTables).
#
#   GREEN : Core_Link.o (the REAL SetupFunctionTables + real RSDKFunctionTable[]/
#           APIFunctionTable[]/gameVerInfo) + p6_stubs.o (the 51 backend entry
#           points SetupFunctionTables takes the address of) are linked;
#           FunctionTables_Saturn.o is EXCLUDED (it would multiply-define the
#           four table symbols Core_Link.o now owns). UserCore_Saturn.o is
#           recompiled with -DRSDK_SKU_GLOBALS_IN_LINK so Core_Link.o is the
#           sole owner of curSKU/unknownInfo (otherwise a 2-symbol collision).
#           p6_main calls the REAL SetupFunctionTables -> the table carries live
#           .text addresses -> both slot witnesses land in [__text_start,
#           __text_end) -> ProcessAnimation dispatches -> last_frameid 6 and all
#           9 Ring witnesses match P5.
#
# Both are REAL links (NO --noinhibit-exec): an unresolved symbol or a p6.linker
# ASSERT (stack/heap overrun) is a hard build error. Emits <base>.elf/.map/0<sfx>.bin.
# =============================================================================
set -u

MODE="${1:-green}"
# P6.1 (dispatch) modes: red/green. P6.2 (file-I/O, Task #206) modes: io-red/io-green.
#   DISPATCH=green -> real SetupFunctionTables (Core_Link.o + p6_stubs.o).
#   IO=1 (io-red)  -> FULL-engine p6_main with -DP6_IO_TEST (the LoadFile witness
#                     block), booting via crt0.s. No GFS backend, so LoadFile falls
#                     through newlib stdio -> _open=-1 stub -> loaded=0 (RED reference).
#   io-green       -> the DECOUPLED GFSDEMO-style proof, built by the self-contained
#                     branch below: a MINIMAL single-region SGL image (SGL ___Start +
#                     slInitSystem + slInitSynch + CDC_CdInit + GFS_Init) linking the
#                     REAL Reader.cpp LoadFile path but NOT the engine entity/scene
#                     BSS. It exits before the full-engine shared path runs.
DISPATCH=green; IO=0; FILEIO=0; JOBOOT=0
case "$MODE" in
  red)      BASE=_p6_red;   BINSFX=_red;   DISPATCH=red    ;;
  green)    BASE=_p6;       BINSFX=""                      ;;
  io-red)   BASE=_p6_iored; BINSFX=_iored; IO=1            ;;
  io-green) BASE=_p6_io;    BINSFX=_io;    IO=1; FILEIO=1  ;;
  io-jo)    BASE=_p6_iojo;  BINSFX=_iojo;  IO=1; FILEIO=1; JOBOOT=1 ;;
  *) echo "usage: $0 [red|green|io-red|io-green|io-jo]"; exit 2 ;;
esac

CC=/work/jo-engine/Compiler/LINUX/bin/sh-none-elf-gcc-8.2.0
BINDIR=$(dirname "$CC")
OBJCOPY="$BINDIR/sh-none-elf-objcopy"
NEWLIB=/work/jo-engine/Compiler/WINDOWS/sh-elf/include
DEPS=/work/rsdkv5-src/dependencies/all
PLAT=/work/platform/Saturn
SHIM=/work/tools/_portspike/shim
SRC=/work/rsdkv5-src/RSDKv5
OBJ=/work/tools/_portspike/_trueport_obj
P6=/work/tools/_portspike/_p6
# P6.2 file-I/O backend deps (uppercase paths -- Docker is case-sensitive; all
# confirmed present via ls). Used by the DECOUPLED io-green branch below. The GFS
# file library lives in LIBCD.A; SEGA_SYS.A is its base-system dependency; LIBSGL.A
# supplies sglI00.o's ___Start SLSTART + slInitSystem (the decoupled image BOOTS
# through it). SEGA_GFS.H (which pulls SEGA_CDC.H) is under the SGL_302j INC dir;
# SGLAREA.O (the slInitSystem work-area table) is under SGL_302j LIB_ELF.
SGLINC=/work/jo-engine/Compiler/COMMON/SGL_302j/INC
SGLLIB=/work/jo-engine/Compiler/COMMON/SGL_302j/LIB_ELF
LIBCD=$SGLLIB/LIBCD.A
LIBSGL=$SGLLIB/LIBSGL.A
SEGASYS=$SGLLIB/SEGA_SYS.A
# The shared full-engine path (red/green/io-red) boots through the hand-rolled
# crt0.s (which defines ___Start at the .slstart head) + the two-bank p6.linker.
# io-green does NOT use these -- its self-contained branch above boots through SGL's
# real ___Start + the single-region p6_io.linker.
LINKER=$P6/p6.linker
MAP=$P6/$BASE.map
ELF=$P6/$BASE.elf
BIN=$P6/0$BINSFX.bin
ERRLOG=$P6/${BASE}_link_err.txt

CORE_DEFS="-DRETRO_USE_MOD_LOADER=0 -DRETRO_REVISION=2 -DSCREEN_XMAX=320 -DSCREEN_YSIZE=224"
CORE_INC="-I$PLAT -I$SHIM -I$SRC -I$DEPS -I$NEWLIB"
CXXFLAGS="-x c++ -std=gnu++11 -m2 -O2 -include stdlib.h -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-builtin"
# FLATCXX: the Ring TU sees ONLY p6_gameapi.h (-I$P6) + newlib string.h. No CORE_INC.
FLATCXX="-x c++ -std=gnu++11 -m2 -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-builtin -I$P6 -I$NEWLIB"
# P6.2: the full-engine p6_main.cpp gains -DP6_IO_TEST (the LoadFile witness block)
# when IO=1 (io-red). FILEIO=1 (io-green) is the DECOUPLED proof, handled entirely by
# the self-contained branch below, which never reaches this shared compile.
P6MAIN_DEFS=""
[ "$IO" = 1 ]     && P6MAIN_DEFS="$P6MAIN_DEFS -DP6_IO_TEST"

# =============================================================================
# io-green (Task #206, P6.2) -- DECOUPLED GFSDEMO-style file-I/O proof.
# Self-contained: builds + links + exits HERE, never touching the full-engine
# shared path below. The image is MINIMAL -- it links the REAL unmodified engine
# Core_Reader.o (LoadFile/ReadBytes) over a Saturn GFS backend, but does NOT drag
# the 1.2 MB entity/scene BSS (no Scene_*.o, no build_trueport objects). It boots
# through SGL's STANDARD single-region runtime (SGL ___Start SLSTART -> asm _main
# shim -> p6_io_proof), where slInitSystem is proven to work (jo + Coup), instead
# of the bare crt0.s path that faulted (crt0 left GBR unset -> SGL VBlank ISR
# address error at PC=0x06000956). See p6_io.linker / p6_sgl_boot.c headers.
#
# Object set (5 .o + SGLAREA.O + libs), all compiled into $P6 so the shared
# trueport $OBJ tree is never touched:
#   p6_io_boot.o  asm _main shim (BSS-zero + ctors, NO stack set) -> p6_io_proof
#   p6_io_main.o  engine-headers proof body: witnesses + the 3 measured RSDK stubs
#                 + p6_io_proof (-DP6_IO_TEST -DRETRO_SATURN_FILEIO)
#   p6_sgl_boot.o slInitSystem + slInitSynch (SGL.H, its own C TU)
#   p6_gfs.o      CDC_CdInit + GFS_Init + Saturn_fXxx backend (SEGA_GFS.H)
#   Core_Reader.o REAL engine Reader.cpp, -DRETRO_SATURN_FILEIO (standalone in $P6)
#   SGLAREA.O     SGL work-area table slInitSystem dereferences (SLPROG)
# NO p6_syscalls.o here (unlike the two-bank green/red link): an SGL lib references
# a syscall in newlib's lib_a-syscalls.o, which then collides with p6_syscalls.o's
# whole-set override. This SINGLE-REGION image instead lets newlib own the syscall
# set (incl _sbrk) and points the `end` heap symbol at the WRAM-L heap base in
# p6_io.linker -- the jo/Coup/GFSDEMO single-region convention.
# Libs in --start-group: -lc -lnosys -lgcc + SEGA_SYS.A + LIBCD.A + LIBSGL.A
#   (LIBSGL.A supplies sglI00.o SLSTART + slInitSystem; LIBCD.A supplies GFS_*/CDC_*).
# =============================================================================
if [ "$MODE" = io-green ]; then
  IOLINK=$P6/p6_io.linker
  SGLAREA=$SGLLIB/SGLAREA.O
  echo "=============================================================="
  echo "P6.2 DECOUPLED file-I/O proof  [MODE=io-green]  -> $BASE"
  echo "=============================================================="
  : > "$ERRLOG"

  echo "[1/4] Compiling the 5 decoupled objects into \$P6 ..."
  "$CC" -m2 -c -o "$P6/p6_io_boot.o" "$P6/p6_io_boot.s" 2>>"$ERRLOG" \
      || { echo "p6_io_boot FAIL"; cat "$ERRLOG"; exit 1; }
  $CC $CXXFLAGS $CORE_DEFS -DP6_IO_TEST -DRETRO_SATURN_FILEIO $CORE_INC \
      -c -o "$P6/p6_io_main.o" "$P6/p6_io_main.cpp" 2>>"$ERRLOG" \
      || { echo "p6_io_main FAIL"; cat "$ERRLOG"; exit 1; }
  "$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -I"$SGLINC" -I"$NEWLIB" \
      -c -o "$P6/p6_sgl_boot.o" "$P6/p6_sgl_boot.c" 2>>"$ERRLOG" \
      || { echo "p6_sgl_boot FAIL"; cat "$ERRLOG"; exit 1; }
  "$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -I"$SGLINC" -I"$NEWLIB" \
      -c -o "$P6/p6_gfs.o" "$P6/p6_gfs.c" 2>>"$ERRLOG" \
      || { echo "p6_gfs FAIL"; cat "$ERRLOG"; exit 1; }
  $CC $CXXFLAGS $CORE_DEFS -DRETRO_SATURN_FILEIO $CORE_INC \
      -c -o "$P6/Core_Reader.o" "$SRC/RSDK/Core/Reader.cpp" 2>>"$ERRLOG" \
      || { echo "Core_Reader FAIL"; cat "$ERRLOG"; exit 1; }
  echo "        5 objects built (+ SGLAREA.O: $(basename "$SGLAREA"))."

  echo "[2/4] Linking single-region $(basename "$IOLINK") (SGL ___Start entry) ..."
  : > "$ERRLOG"
  "$CC" -m2 -nostartfiles -T "$IOLINK" -Wl,-Map="$MAP" -o "$ELF" \
      "$P6/p6_io_boot.o" "$P6/p6_io_main.o" "$P6/p6_sgl_boot.o" "$P6/p6_gfs.o" \
      "$P6/Core_Reader.o" "$SGLAREA" \
      -Wl,--start-group -lc -lnosys -lgcc "$SEGASYS" "$LIBCD" "$LIBSGL" -Wl,--end-group \
      2>"$ERRLOG"
  if [ ! -f "$ELF" ]; then
    echo "  FAIL  link produced no ELF (ASSERT or unresolved symbol):"; cat "$ERRLOG"; exit 1
  fi
  [ -s "$ERRLOG" ] && { echo "  link stderr (warnings tolerated):"; cat "$ERRLOG"; }
  echo "        linked: $ELF"

  echo "[3/4] objcopy -O binary -> $(basename "$BIN") ..."
  "$OBJCOPY" -O binary "$ELF" "$BIN" 2>>"$ERRLOG" || { echo "objcopy FAIL"; cat "$ERRLOG"; exit 1; }
  echo "        0$BINSFX.bin = $(wc -c < "$BIN" | tr -d ' ') bytes (expect ~80-160 KB, decoupled)."

  echo "[4/4] Key symbols ..."
  echo "--------------------------------------------------------------"
  for sym in ___Start _main _p6_io_proof __text_start __text_end \
             __bss_h_start __bss_h_end __bss_l_start __bss_l_end \
             ___heap_start ___heap_end \
             _p6_w_magic _p6_w_io_loaded _p6_w_io_filesize _p6_w_io_firstbytes \
             _p6_w_io_gfsinit _p6_w_io_fid _p6_w_io_step; do
    bare="${sym#_}"
    line=$(grep -E "0x[0-9a-fA-F]+[[:space:]]+_?${bare}([[:space:]=]|\$)" "$MAP" | head -1)
    [ -n "$line" ] && echo "  $(echo "$line" | sed -E 's/^[[:space:]]+//')" || echo "  (missing) $sym"
  done
  echo "--------------------------------------------------------------"
  echo "DONE [io-green decoupled]."
  exit 0
fi

# =============================================================================
# io-jo (Task #206, P6.2 Path 2) -- relink the engine file-I/O proof body into
# jo-engine's EXISTING, PROVEN boot. Self-contained: builds + links + exits HERE.
#
# Unlike io-green (bare-SLSTART crt0-less asm _main shim whose own slInitSystem
# path faulted with an unset GBR), Path 2 boots through jo's REAL main():
#   SGL SLSTART (LIBSGL.A sglI00.o ___Start @0x06004000, pulled by jo's
#     slInitSystem ref) sets SR/SP/CCR/GBR -> jmp @main.
#   jo main (core.c:665, ELF _main) zeroes WRAM-H .bss (__bstart/__bend) ->
#     jo_main() (p6_jo_boot.c) -> jo_core_init() (slInitSystem -> jo_audio_init/
#     CDC_CdInit -> jo_fs_init/GFS_Init w/ root dir) -> p6_io_run() (the engine
#     LoadFile witness body, lean: NO -DP6_IO_TEST) -> jo_core_run() (park).
#
# Object set: 22 jo modules (recompiled WITHOUT -flto for a deterministic self-
# contained link; the project's prebuilt jo *.o are LTO) + p6_jo_boot.o (jo_main)
# + p6_io_main.o (witnesses + 3 RSDK stubs + p6_io_run, NO -DP6_IO_TEST) +
# p6_gfs.o (Saturn_fXxx GFS backend, IDENTICAL to io-green) + Core_Reader.o (REAL
# engine Reader.cpp) + SGLAREA.O (SLPROG work-area table). The 17 jo -D defines +
# the non-video CCFLAGS are derived EXACTLY from Makefile toggles x jo_engine_-
# makefile (lines 119-273): VIDEO/3D/MODE7/EFFECTS/PRINTF off; BACKUP/TGA/AUDIO/
# FS/DUAL_CPU(slave)/RAM_CARD/SPRITE_HASHTABLE/NTSC on; pool=262144; DEBUG on.
# Libs in --start-group: -lc -lnosys -lgcc + SEGA_SYS.A + LIBCD.A + LIBSGL.A.
# =============================================================================
if [ "$MODE" = io-jo ]; then
  IOJOLINK=$P6/p6_io_jo.linker
  SGLAREA=$SGLLIB/SGLAREA.O
  JOSRC=/work/jo-engine/jo_engine
  JO_LIST="font input physics core math malloc tools palette sprites map list sprite_animator image vdp2 time vdp1_command_pipeline vdp2_malloc backup tga audio fs slave"
  JO_DEFS="-DJO_FRAMERATE=1 -DJO_COMPILE_USING_SGL -DJO_COMPILE_WITH_BACKUP_SUPPORT -DJO_COMPILE_WITH_TGA_SUPPORT -DJO_COMPILE_WITH_AUDIO_SUPPORT -DJO_COMPILE_WITH_FS_SUPPORT -DJO_GLOBAL_MEMORY_SIZE_FOR_MALLOC=262144 -DJO_MAX_SPRITE=255 -DJO_MAX_FILE_IN_IMAGE_PACK=32 -DJO_MAP_MAX_LAYER=8 -DJO_MAX_SPRITE_ANIM=16 -DJO_MAX_FS_BACKGROUND_JOBS=1 -DJO_DEBUG -DJO_COMPILE_WITH_SPRITE_HASHTABLE -DJO_NTSC_VERSION -DJO_COMPILE_WITH_RAM_CARD_SUPPORT -DJO_COMPILE_WITH_DUAL_CPU_SUPPORT"
  JO_CCFLAGS="-fkeep-inline-functions -W -Wall -Wshadow -Wbad-function-cast -Winline -Wcomment -Wsign-compare -Wextra -Wno-strict-aliasing -fno-common -ffast-math --param max-inline-insns-single=50 -fms-extensions -std=gnu99 -fmerge-all-constants -fno-ident -fno-unwind-tables -fno-asynchronous-unwind-tables -fomit-frame-pointer -fstrength-reduce -frerun-loop-opt -O2 -fno-builtin -m2"
  JO_INC="-I$JOSRC -I$SGLINC -I$NEWLIB"
  echo "=============================================================="
  echo "P6.2 Path 2 jo-HAL file-I/O proof  [MODE=io-jo]  -> $BASE"
  echo "=============================================================="
  : > "$ERRLOG"

  echo "[1/5] Compiling 22 jo SRCS (no -flto) into \$P6/jo_*.o ..."
  JO_OBJS=""
  for j in $JO_LIST; do
    "$CC" $JO_CCFLAGS $JO_DEFS $JO_INC -c -o "$P6/jo_$j.o" "$JOSRC/$j.c" 2>>"$ERRLOG" \
        || { echo "jo $j FAIL"; cat "$ERRLOG"; exit 1; }
    JO_OBJS="$JO_OBJS $P6/jo_$j.o"
  done
  echo "        $(echo $JO_OBJS | wc -w | tr -d ' ') jo objects built."

  echo "[2/5] Compiling proof objects (p6_jo_boot + p6_io_main + p6_gfs + Core_Reader) ..."
  "$CC" $JO_CCFLAGS $JO_DEFS $JO_INC -c -o "$P6/p6_jo_boot.o" "$P6/p6_jo_boot.c" 2>>"$ERRLOG" \
      || { echo "p6_jo_boot FAIL"; cat "$ERRLOG"; exit 1; }
  $CC $CXXFLAGS $CORE_DEFS -DRETRO_SATURN_FILEIO $CORE_INC \
      -c -o "$P6/p6_io_main.o" "$P6/p6_io_main.cpp" 2>>"$ERRLOG" \
      || { echo "p6_io_main FAIL"; cat "$ERRLOG"; exit 1; }
  "$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -I"$SGLINC" -I"$NEWLIB" \
      -c -o "$P6/p6_gfs.o" "$P6/p6_gfs.c" 2>>"$ERRLOG" \
      || { echo "p6_gfs FAIL"; cat "$ERRLOG"; exit 1; }
  $CC $CXXFLAGS $CORE_DEFS -DRETRO_SATURN_FILEIO $CORE_INC \
      -c -o "$P6/Core_Reader.o" "$SRC/RSDK/Core/Reader.cpp" 2>>"$ERRLOG" \
      || { echo "Core_Reader FAIL"; cat "$ERRLOG"; exit 1; }
  echo "        proof objects built (p6_io_main WITHOUT -DP6_IO_TEST -> lean p6_io_run)."

  echo "[3/5] Linking single-region $(basename "$IOJOLINK") (SGL ___Start -> jo main) ..."
  : > "$ERRLOG"
  "$CC" -m2 -nostartfiles -T "$IOJOLINK" -Wl,-Map="$MAP" -o "$ELF" \
      $JO_OBJS "$P6/p6_jo_boot.o" "$P6/p6_io_main.o" "$P6/p6_gfs.o" "$P6/Core_Reader.o" "$SGLAREA" \
      -Wl,--start-group -lc -lnosys -lgcc "$SEGASYS" "$LIBCD" "$LIBSGL" -Wl,--end-group \
      2>"$ERRLOG"
  if [ ! -f "$ELF" ]; then
    echo "  FAIL  link produced no ELF (ASSERT or unresolved symbol):"; cat "$ERRLOG"; exit 1
  fi
  [ -s "$ERRLOG" ] && { echo "  link stderr (warnings tolerated):"; cat "$ERRLOG"; }
  echo "        linked: $ELF"

  echo "[4/5] objcopy -O binary -> $(basename "$BIN") ..."
  "$OBJCOPY" -O binary "$ELF" "$BIN" 2>>"$ERRLOG" || { echo "objcopy FAIL"; cat "$ERRLOG"; exit 1; }
  echo "        0$BINSFX.bin = $(wc -c < "$BIN" | tr -d ' ') bytes."

  echo "[5/5] Key symbols + ctor-array check ..."
  echo "--------------------------------------------------------------"
  for sym in ___Start _main _jo_main __text_start __text_end \
             __bstart __bend __bss_h_start __bss_h_end \
             __init_array_start __init_array_end \
             ___heap_start ___heap_end \
             _p6_w_magic _p6_w_io_loaded _p6_w_io_filesize _p6_w_io_firstbytes \
             _p6_w_io_gfsinit _p6_w_io_fid _p6_w_io_step; do
    bare="${sym#_}"
    line=$(grep -E "0x[0-9a-fA-F]+[[:space:]]+_?${bare}([[:space:]=]|\$)" "$MAP" | head -1)
    [ -n "$line" ] && echo "  $(echo "$line" | sed -E 's/^[[:space:]]+//')" || echo "  (missing) $sym"
  done
  echo "--------------------------------------------------------------"
  echo "NOTE: if __init_array_start != __init_array_end above, Core_Reader has"
  echo "      C++ ctors -> jo_main must run them before p6_io_run (see header)."
  echo "DONE [io-jo Path 2]."
  exit 0
fi

echo "=============================================================="
echo "P6.1 engine-dispatch link  [MODE=$MODE]  -> $BASE"
echo "=============================================================="

echo "[1/7] Rebuilding 21 true-port objects ..."
if ! bash /work/tools/_portspike/build_trueport.sh >/dev/null 2>&1; then
  echo "  FAIL  build_trueport.sh did not yield the true-port objects."; exit 1
fi
echo "        $(ls "$OBJ"/*.o 2>/dev/null | wc -l | tr -d ' ') core+shim objects present."

echo "[2/7] Compiling support objects (miniz + libm + C++ rt) into \$OBJ ..."
: > "$ERRLOG"
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -DMINIZ_NO_TIME -I"$DEPS" -I"$NEWLIB" \
      -c -o "$OBJ/miniz.o" "$DEPS/miniz/miniz.c" 2>>"$ERRLOG" || { echo "miniz FAIL"; cat "$ERRLOG"; exit 1; }
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -I"$NEWLIB" \
      -c -o "$OBJ/libm_saturn.o" "$PLAT/libm_saturn.c" 2>>"$ERRLOG" || { echo "libm FAIL"; cat "$ERRLOG"; exit 1; }
"$CC" -x c++ -std=gnu++11 -m2 -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-builtin -I"$NEWLIB" \
      -c -o "$OBJ/CppRuntime_Saturn.o" "$PLAT/CppRuntime_Saturn.cpp" 2>>"$ERRLOG" || { echo "cpprt FAIL"; cat "$ERRLOG"; exit 1; }
echo "        support objects compiled (p6_ring owns the witnesses, no P3Witness)."

# Boot entry object: ALL modes (red/green/io-red/io-green) use the hand-rolled
# crt0.s -- it is the SLSTART entry, sets up the minimal runtime, zeroes both BSS
# banks, runs ctors, and calls _main. io-green needs no different entry: file I/O
# does not need SGL, so there is no SGL ___Start to boot through (the prior boot
# shim p6_boot.c is now unused).
echo "[3/7] crt0.o + p6_main.o (engine) + p6_ring.o (flat) + p6_syscalls.o ..."
BOOTOBJ="$P6/crt0.o"
"$CC" -m2 -c -o "$BOOTOBJ" "$P6/crt0.s" 2>>"$ERRLOG" || { echo "crt0 FAIL"; cat "$ERRLOG"; exit 1; }
$CC $CXXFLAGS $CORE_DEFS $P6MAIN_DEFS $CORE_INC \
      -c -o "$P6/p6_main.o" "$P6/p6_main.cpp" 2>>"$ERRLOG" || { echo "p6_main FAIL"; cat "$ERRLOG"; exit 1; }
$CC $FLATCXX \
      -c -o "$P6/p6_ring.o" "$P6/p6_ring.cpp" 2>>"$ERRLOG" || { echo "p6_ring FAIL"; cat "$ERRLOG"; exit 1; }
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -I"$NEWLIB" \
      -c -o "$P6/p6_syscalls.o" "$P6/p6_syscalls.c" 2>>"$ERRLOG" || { echo "p6_syscalls FAIL"; cat "$ERRLOG"; exit 1; }
echo "        $(basename "$BOOTOBJ") + p6_main.o + p6_ring.o + p6_syscalls.o built."

if [ "$DISPATCH" = green ]; then
  echo "[4/7] GREEN: Core_Link.o + p6_stubs.o + UserCore (-DRSDK_SKU_GLOBALS_IN_LINK) ..."
  $CC $CXXFLAGS $CORE_DEFS $CORE_INC \
        -c -o "$P6/Core_Link.o" "$SRC/RSDK/Core/Link.cpp" 2>>"$ERRLOG" || { echo "Core/Link FAIL"; cat "$ERRLOG"; exit 1; }
  $CC $CXXFLAGS $CORE_DEFS $CORE_INC \
        -c -o "$P6/p6_stubs.o" "$P6/p6_stubs.cpp" 2>>"$ERRLOG" || { echo "p6_stubs FAIL"; cat "$ERRLOG"; exit 1; }
  # Recompile UserCore_Saturn.o WITH the flag so Core_Link.o solely owns
  # curSKU/unknownInfo (overwrites build_trueport's flag-off object in $OBJ).
  $CC $CXXFLAGS $CORE_DEFS -DRSDK_SKU_GLOBALS_IN_LINK $CORE_INC \
        -c -o "$OBJ/UserCore_Saturn.o" "$PLAT/UserCore_Saturn.cpp" 2>>"$ERRLOG" || { echo "UserCore re-c FAIL"; cat "$ERRLOG"; exit 1; }
  echo "        Core_Link.o=$(wc -c < "$P6/Core_Link.o")B  p6_stubs.o=$(wc -c < "$P6/p6_stubs.o")B  UserCore re-compiled."
else
  echo "[4/7] RED: empty FunctionTables_Saturn.o kept; no Core_Link.o / p6_stubs.o."
fi

echo "[5/7] Assembling object list (MODE=$MODE) ..."
EXTRA=""
for o in "$OBJ"/*.o; do
  case "$o" in
    *FunctionTables_Saturn.o)
      # GREEN dispatch: Core_Link.o supersedes the four table symbols -> drop stub.
      [ "$DISPATCH" = green ] && continue ;;
  esac
  EXTRA="$EXTRA $o"
done
GREEN_EXTRA=""
[ "$DISPATCH" = green ] && GREEN_EXTRA="$P6/Core_Link.o $P6/p6_stubs.o"
echo "[6/7] Linking with two-bank $(basename "$LINKER") (ASSERTs are hard errors) ..."
: > "$ERRLOG"
"$CC" -m2 -nostartfiles -T "$LINKER" -Wl,-Map="$MAP" \
  -o "$ELF" \
  "$BOOTOBJ" "$P6/p6_main.o" "$P6/p6_ring.o" "$P6/p6_syscalls.o" \
  $GREEN_EXTRA \
  $EXTRA \
  -Wl,--start-group -lc -lnosys -lgcc -Wl,--end-group \
  2>"$ERRLOG"
if [ ! -f "$ELF" ]; then
  echo "  FAIL  link produced no ELF (ASSERT or unresolved symbol):"; cat "$ERRLOG"; exit 1
fi
if [ -s "$ERRLOG" ]; then
  echo "  link stderr (warnings tolerated, errors already gated by ELF existence):"
  cat "$ERRLOG"
fi
echo "        linked: $ELF"

echo "[7/7] objcopy -O binary -> $(basename "$BIN") + key symbols ..."
"$OBJCOPY" -O binary "$ELF" "$BIN" 2>>"$ERRLOG" || { echo "objcopy FAIL"; cat "$ERRLOG"; exit 1; }
echo "        0$BINSFX.bin = $(wc -c < "$BIN" | tr -d ' ') bytes (expect ~226 KB image, NOT ~1 MB)."
echo "--------------------------------------------------------------"
# GNU ld -Map strips ONE leading underscore from object symbols, so match a
# leading-underscore-stripped name with an optional underscore (mirrors the gate's
# map_symbol() tolerance): _p6_w_magic -> grep _?p6_w_magic -> map "p6_w_magic".
for sym in ___Start __text_start __text_end __bss_h_start __bss_h_end \
           __bss_l_start __bss_l_end ___heap_start ___heap_end \
           _p6_w_magic _p6_w_ticks _p6_w_draw_calls _p6_w_last_frameid \
           _p6_w_slot_procanim _p6_w_slot_drawsprite \
           _p6_w_ring_classid _p6_w_ring_timer _p6_w_ring_scalex \
           _p6_w_ring_vely _p6_w_ring_posy \
           _p6_w_io_loaded _p6_w_io_filesize _p6_w_io_firstbytes \
           _p6_w_io_gfsinit _p6_w_io_fid; do
  bare="${sym#_}"
  line=$(grep -E "0x[0-9a-fA-F]+[[:space:]]+_?${bare}([[:space:]=]|\$)" "$MAP" | head -1)
  [ -n "$line" ] && echo "  $(echo "$line" | sed -E 's/^[[:space:]]+//')" || echo "  (missing) $sym"
done
echo "--------------------------------------------------------------"
echo "DONE [$MODE]."
