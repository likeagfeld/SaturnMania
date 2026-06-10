#!/usr/bin/env bash
# P3 two-bank link -- crt0 + real core entry, booting to ENGINESTATE (#202).
#
# Mirrors qa_p2_saturn_link.sh's compile recipe but: (a) assembles crt0.s, (b)
# compiles the real p3_main.cpp (calls RSDK::RunRetroEngine) instead of the P2
# main stub, (c) links with the two-bank tools/_portspike/_p3/p3.linker (WRAM-H
# pinned engine+sceneInfo+Scene/Object .bss, WRAM-L swept .bss + 608 KB heap).
#
# Unlike the measurement link this is a REAL link: NO --noinhibit-exec / report-
# all, so an unresolved symbol or a p3.linker ASSERT failure (stack/heap overrun)
# is a hard build error. Emits _p3.elf + _p3.map + 0.bin (the IP.BIN payload).
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
P3=/work/tools/_portspike/_p3
LINKER=$P3/p3.linker
MAP=$P3/_p3.map
ELF=$P3/_p3.elf
BIN=$P3/0.bin
ERRLOG=$P3/_p3_link_err.txt

CORE_DEFS="-DRETRO_USE_MOD_LOADER=0 -DRETRO_REVISION=2 -DSCREEN_XMAX=320 -DSCREEN_YSIZE=224"
CORE_INC="-I$PLAT -I$SHIM -I$SRC -I$DEPS -I$NEWLIB"
CXXFLAGS="-x c++ -std=gnu++11 -m2 -O2 -include stdlib.h -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-builtin"

echo "=============================================================="
echo "P3 two-bank link: crt0 + real core entry -> boot witness"
echo "=============================================================="

echo "[1/6] Rebuilding 21 true-port objects ..."
if ! bash /work/tools/_portspike/build_trueport.sh >/dev/null 2>&1; then
  echo "  FAIL  build_trueport.sh did not yield the true-port objects."; exit 1
fi
echo "        $(ls "$OBJ"/*.o 2>/dev/null | wc -l | tr -d ' ') core+shim objects present."

echo "[2/6] Compiling support objects (miniz + libm + C++ rt + witness) ..."
: > "$ERRLOG"
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -DMINIZ_NO_TIME -I"$DEPS" -I"$NEWLIB" \
      -c -o "$OBJ/miniz.o" "$DEPS/miniz/miniz.c" 2>>"$ERRLOG" || { echo "miniz FAIL"; cat "$ERRLOG"; exit 1; }
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -I"$NEWLIB" \
      -c -o "$OBJ/libm_saturn.o" "$PLAT/libm_saturn.c" 2>>"$ERRLOG" || { echo "libm FAIL"; cat "$ERRLOG"; exit 1; }
"$CC" -x c++ -std=gnu++11 -m2 -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-builtin -I"$NEWLIB" \
      -c -o "$OBJ/CppRuntime_Saturn.o" "$PLAT/CppRuntime_Saturn.cpp" 2>>"$ERRLOG" || { echo "cpprt FAIL"; cat "$ERRLOG"; exit 1; }
$CC $CXXFLAGS $CORE_DEFS $CORE_INC \
      -c -o "$OBJ/P3Witness_Saturn.o" "$PLAT/P3Witness_Saturn.cpp" 2>>"$ERRLOG" || { echo "witness FAIL"; cat "$ERRLOG"; exit 1; }
echo "        support objects compiled (no p2_main_stub -- p3_main owns main)."

echo "[3/6] Assembling crt0.o + compiling p3_main.o + p3_syscalls.o ..."
"$CC" -m2 -c -o "$P3/crt0.o" "$P3/crt0.s" 2>>"$ERRLOG" || { echo "crt0 FAIL"; cat "$ERRLOG"; exit 1; }
$CC $CXXFLAGS $CORE_DEFS $CORE_INC \
      -c -o "$P3/p3_main.o" "$P3/p3_main.cpp" 2>>"$ERRLOG" || { echo "p3_main FAIL"; cat "$ERRLOG"; exit 1; }
"$CC" -x c -std=gnu11 -m2 -O2 -fno-builtin -I"$NEWLIB" \
      -c -o "$P3/p3_syscalls.o" "$P3/p3_syscalls.c" 2>>"$ERRLOG" || { echo "p3_syscalls FAIL"; cat "$ERRLOG"; exit 1; }
echo "        crt0.o + p3_main.o + p3_syscalls.o built."

echo "[4/6] Linking with two-bank p3.linker (ASSERTs are hard errors) ..."
: > "$ERRLOG"
"$CC" -m2 -nostartfiles -T "$LINKER" -Wl,-Map="$MAP" \
  -o "$ELF" \
  "$P3/crt0.o" "$P3/p3_main.o" "$P3/p3_syscalls.o" \
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
           _p3_w_magic _p3_w_expect_none _p3_w_scene_state _p3_w_engine_init; do
  line=$(grep -E "0x[0-9a-fA-F]+[[:space:]]+${sym}\$" "$MAP" | head -1)
  [ -n "$line" ] && echo "  $line" || echo "  (missing) $sym"
done
echo "--------------------------------------------------------------"
echo "DONE."
