#!/usr/bin/env bash
# P5 two-bank link -- crt0 + real core + the flat Ring TU, first-object proof (#204).
#
# Mirrors _p3/link_p3.sh's compile recipe but for the TWO-TU split:
#   - p5_main.cpp : engine headers (`namespace RSDK`); owns main() + the bridges.
#                   Compiled with the SAME CORE flags as p3_main (CORE_DEFS/INC).
#   - p5_ring.cpp : FLAT (no engine header); compiles the verbatim decomp Ring
#                   bodies against p5_gameapi.h. Compiled with FLATCXX (NO CORE_INC
#                   so the engine's `namespace RSDK` and this TU never meet).
# The P3 witness TU (P3Witness_Saturn.o) is NOT compiled -- P5's 9 witnesses live
# in p5_ring.o. build_trueport.sh `rm -rf`s $OBJ first, so no stale witness leaks.
#
# This is a REAL link: NO --noinhibit-exec, so an unresolved symbol or a p5.linker
# ASSERT (stack/heap overrun) is a hard build error. Emits _p5.elf + _p5.map + 0.bin.
set -u

CC=/work/jo-engine/Compiler/LINUX/bin/sh-none-elf-gcc-8.2.0
BINDIR=$(dirname "$CC")
OBJCOPY="$BINDIR/sh-none-elf-objcopy"
NEWLIB=/work/jo-engine/Compiler/WINDOWS/sh-elf/include
DEPS=/work/rsdkv5-src/dependencies/all
PLAT=/work/platform/Saturn
SHIM=/work/tools/_portspike/shim
SRC=/work/rsdkv5-src/RSDKv5
OBJ=/work/tools/_portspike/_trueport_obj
P5=/work/tools/_portspike/_p5
LINKER=$P5/p5.linker
MAP=$P5/_p5.map
ELF=$P5/_p5.elf
BIN=$P5/0.bin
ERRLOG=$P5/_p5_link_err.txt

CORE_DEFS="-DRETRO_USE_MOD_LOADER=0 -DRETRO_REVISION=2 -DSCREEN_XMAX=320 -DSCREEN_YSIZE=224"
CORE_INC="-I$PLAT -I$SHIM -I$SRC -I$DEPS -I$NEWLIB"
CXXFLAGS="-x c++ -std=gnu++11 -m2 -O2 -include stdlib.h -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-builtin"
# FLATCXX: the Ring TU sees ONLY p5_gameapi.h (-I$P5) + newlib string.h. No CORE_INC.
FLATCXX="-x c++ -std=gnu++11 -m2 -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-builtin -I$P5 -I$NEWLIB"

echo "=============================================================="
echo "P5 two-bank link: crt0 + real core + flat Ring -> object witness"
echo "=============================================================="

echo "[1/6] Rebuilding 21 true-port objects ..."
if ! bash /work/tools/_portspike/build_trueport.sh >/dev/null 2>&1; then
  echo "  FAIL  build_trueport.sh did not yield the true-port objects."; exit 1
fi
echo "        $(ls "$OBJ"/*.o 2>/dev/null | wc -l | tr -d ' ') core+shim objects present."

echo "[2/6] Compiling support objects (miniz + libm + C++ rt) ..."
: > "$ERRLOG"
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -DMINIZ_NO_TIME -I"$DEPS" -I"$NEWLIB" \
      -c -o "$OBJ/miniz.o" "$DEPS/miniz/miniz.c" 2>>"$ERRLOG" || { echo "miniz FAIL"; cat "$ERRLOG"; exit 1; }
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -I"$NEWLIB" \
      -c -o "$OBJ/libm_saturn.o" "$PLAT/libm_saturn.c" 2>>"$ERRLOG" || { echo "libm FAIL"; cat "$ERRLOG"; exit 1; }
"$CC" -x c++ -std=gnu++11 -m2 -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-builtin -I"$NEWLIB" \
      -c -o "$OBJ/CppRuntime_Saturn.o" "$PLAT/CppRuntime_Saturn.cpp" 2>>"$ERRLOG" || { echo "cpprt FAIL"; cat "$ERRLOG"; exit 1; }
echo "        support objects compiled (NO P3Witness -- p5_ring owns the witnesses)."

echo "[3/6] Assembling crt0.o + compiling p5_main.o (engine) + p5_ring.o (flat) + p5_syscalls.o ..."
"$CC" -m2 -c -o "$P5/crt0.o" "$P5/crt0.s" 2>>"$ERRLOG" || { echo "crt0 FAIL"; cat "$ERRLOG"; exit 1; }
$CC $CXXFLAGS $CORE_DEFS $CORE_INC \
      -c -o "$P5/p5_main.o" "$P5/p5_main.cpp" 2>>"$ERRLOG" || { echo "p5_main FAIL"; cat "$ERRLOG"; exit 1; }
$CC $FLATCXX \
      -c -o "$P5/p5_ring.o" "$P5/p5_ring.cpp" 2>>"$ERRLOG" || { echo "p5_ring FAIL"; cat "$ERRLOG"; exit 1; }
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -I"$NEWLIB" \
      -c -o "$P5/p5_syscalls.o" "$P5/p5_syscalls.c" 2>>"$ERRLOG" || { echo "p5_syscalls FAIL"; cat "$ERRLOG"; exit 1; }
echo "        crt0.o + p5_main.o + p5_ring.o + p5_syscalls.o built."

echo "[4/6] Linking with two-bank p5.linker (ASSERTs are hard errors) ..."
: > "$ERRLOG"
"$CC" -m2 -nostartfiles -T "$LINKER" -Wl,-Map="$MAP" \
  -o "$ELF" \
  "$P5/crt0.o" "$P5/p5_main.o" "$P5/p5_ring.o" "$P5/p5_syscalls.o" \
  "$OBJ"/*.o \
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

echo "[5/6] objcopy -O binary -> 0.bin (IP.BIN payload) ..."
"$OBJCOPY" -O binary "$ELF" "$BIN" 2>>"$ERRLOG" || { echo "objcopy FAIL"; cat "$ERRLOG"; exit 1; }
echo "        0.bin = $(wc -c < "$BIN" | tr -d ' ') bytes (expect ~226 KB image, NOT ~1 MB)."

echo "[6/6] Key symbol addresses from the map ..."
echo "--------------------------------------------------------------"
for sym in ___Start __text_start __text_end __bss_h_start __bss_h_end \
           __bss_l_start __bss_l_end ___heap_start ___heap_end \
           _p5_w_magic _p5_w_ticks _p5_w_draw_calls _p5_w_last_frameid \
           _p5_w_ring_classid _p5_w_ring_timer _p5_w_ring_scalex \
           _p5_w_ring_vely _p5_w_ring_posy; do
  line=$(grep -E "0x[0-9a-fA-F]+[[:space:]]+${sym}\$" "$MAP" | head -1)
  [ -n "$line" ] && echo "  $line" || echo "  (missing) $sym"
done
echo "--------------------------------------------------------------"
echo "DONE."
