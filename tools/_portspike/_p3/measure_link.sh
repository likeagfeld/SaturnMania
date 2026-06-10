#!/usr/bin/env bash
# P3 measurement link -- per-OBJECT .bss sizing for the two-bank p3.linker.
#
# The two-bank split (WRAM-H 0x06000000 + WRAM-L 0x00200000) must assign whole
# objects to a bank; a single *(.bss) wildcard cannot span the non-contiguous
# banks. To decide which objects land where, we need each object's .bss
# contribution. The jo LINUX toolchain ships no nm/objdump/readelf, so the only
# source of per-object section sizes is the linker -Map.
#
# This is a FLAT measurement link (default ld script, entry 0, p2_main_stub for
# the main closure -- crt0/p3_main are NOT linked here; they need the two-bank
# symbols p3.linker defines, which is exactly what this measurement informs).
# It mirrors qa_p2_saturn_link.sh's recipe + a -Map, then prints the per-object
# .bss lines so p3.linker can pin the big WRAM-H residents (engine + sceneInfo +
# whatever else fits) and sweep the remainder to WRAM-L.
#
# Also compiles P3Witness_Saturn.o to confirm its .bss ~ 0 (all four exports are
# initialised .data, so the witness adds no .bss pressure to either bank).
set -u

CC=/work/jo-engine/Compiler/LINUX/bin/sh-none-elf-gcc-8.2.0
NEWLIB=/work/jo-engine/Compiler/WINDOWS/sh-elf/include
DEPS=/work/rsdkv5-src/dependencies/all
PLAT=/work/platform/Saturn
SHIM=/work/tools/_portspike/shim
OBJ=/work/tools/_portspike/_trueport_obj
MAP=/work/tools/_portspike/_p3/_p3_measure.map
ELF=/work/tools/_portspike/_p3/_p3_measure.elf
ERRLOG=/work/tools/_portspike/_p3/_p3_measure_err.txt

echo "=============================================================="
echo "P3 measurement link: per-object .bss for the two-bank p3.linker"
echo "=============================================================="

echo "[1/4] Rebuilding 21 true-port objects ..."
if ! bash /work/tools/_portspike/build_trueport.sh >/dev/null 2>&1; then
  echo "  FAIL  build_trueport.sh did not yield 21/21 objects."
  exit 1
fi
echo "        $(ls "$OBJ"/*.o 2>/dev/null | wc -l | tr -d ' ') core+shim objects present."

echo "[2/4] Compiling support objects (miniz + libm + C++ rt + main stub + witness) ..."
: > "$ERRLOG"
"$CC" -x c   -std=gnu11   -m2 -O2 -fno-builtin -DMINIZ_NO_TIME -I"$DEPS" -I"$NEWLIB" \
      -c -o "$OBJ/miniz.o" "$DEPS/miniz/miniz.c" 2>>"$ERRLOG" || { echo "miniz FAIL"; cat "$ERRLOG"; exit 1; }
"$CC" -x c   -std=gnu11   -m2 -O2 -fno-builtin -I"$NEWLIB" \
      -c -o "$OBJ/libm_saturn.o" "$PLAT/libm_saturn.c" 2>>"$ERRLOG" || { echo "libm FAIL"; cat "$ERRLOG"; exit 1; }
"$CC" -x c++ -std=gnu++11 -m2 -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-builtin -I"$NEWLIB" \
      -c -o "$OBJ/CppRuntime_Saturn.o" "$PLAT/CppRuntime_Saturn.cpp" 2>>"$ERRLOG" || { echo "cpprt FAIL"; cat "$ERRLOG"; exit 1; }
"$CC" -x c   -std=gnu11   -m2 -O2 -fno-builtin -I"$NEWLIB" \
      -c -o "$OBJ/p2_main_stub.o" "$SHIM/p2_main_stub.c" 2>>"$ERRLOG" || { echo "mainstub FAIL"; cat "$ERRLOG"; exit 1; }
"$CC" -x c++ -std=gnu++11 -m2 -O2 -include stdlib.h -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-builtin \
      -DRETRO_USE_MOD_LOADER=0 -DRETRO_REVISION=2 -DSCREEN_XMAX=320 -DSCREEN_YSIZE=224 \
      -I"$PLAT" -I"$SHIM" -I"/work/rsdkv5-src/RSDKv5" -I"$DEPS" -I"$NEWLIB" \
      -c -o "$OBJ/P3Witness_Saturn.o" "$PLAT/P3Witness_Saturn.cpp" 2>>"$ERRLOG" || { echo "witness FAIL"; cat "$ERRLOG"; exit 1; }
echo "        support objects compiled (incl. P3Witness_Saturn.o)."

echo "[3/4] Flat measurement link (default script, -Map) ..."
: > "$ERRLOG"
"$CC" -m2 -nostartfiles -Wl,-e,0 \
  -Wl,--unresolved-symbols=report-all -Wl,--noinhibit-exec \
  -Wl,-Map="$MAP" \
  -o "$ELF" \
  "$OBJ"/*.o \
  -Wl,--start-group -lc -lnosys -lgcc -Wl,--end-group \
  2>"$ERRLOG" || true
if [ ! -f "$MAP" ]; then
  echo "  FAIL  no map produced"; cat "$ERRLOG"; exit 1
fi
echo "        map: $MAP ($(wc -c < "$MAP" | tr -d ' ') bytes)"

echo "[4/4] Per-object .bss contributions (sorted, bytes) ..."
echo "--------------------------------------------------------------"
# In a GNU ld -Map, each input .bss chunk appears as:
#    .bss.<sym>     0x<addr>   0x<size>   <path>(<obj>)
#  or a plain ' .bss' line followed by the object path. Capture both the
# explicit-symbol form and the per-object aggregate by scanning the .bss output
# section block and summing 0xSIZE per owning object file.
awk '
  /^\.bss/ {inbss=1}
  /^\.[a-zA-Z]/ && !/^\.bss/ {inbss=0}
  inbss && /_trueport_obj\/.*\.o/ {
    # fields: .name  addr  size  path(obj)  OR  addr size path
    size=""; obj="";
    for (i=1;i<=NF;i++){
      if ($i ~ /^0x[0-9a-fA-F]+$/ && size=="" && i>1 && $(i-1) ~ /^0x/) {size=$i}
    }
    # simpler: last 0x field before the path is the size; path is last field
    for (i=NF;i>=1;i--){ if ($i ~ /\.o$/ || $i ~ /\.o\)$/){obj=$i; objidx=i; break} }
    # size = last 0x token with index < objidx
    for (i=objidx-1;i>=1;i--){ if ($i ~ /^0x[0-9a-fA-F]+$/){size=$i; break} }
    if (obj!="" && size!=""){
      gsub(/.*\//,"",obj); gsub(/\)$/,"",obj);
      bytes=strtonum(size); tot[obj]+=bytes;
    }
  }
  END{ for (o in tot) printf "%10d  %s\n", tot[o], o }
' "$MAP" | sort -rn
echo "--------------------------------------------------------------"
echo "DONE. Use these per-object .bss sizes to assign WRAM-H vs WRAM-L banks."
