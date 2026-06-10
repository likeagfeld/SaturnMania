#!/usr/bin/env bash
# P6.1 authoritative undefined-symbol probe: compile the REAL Core/Link.cpp and
# link it in place of the empty FunctionTables_Saturn.o stub, then enumerate the
# exact set of symbols SetupFunctionTables() references that nothing in the
# current object set defines. This is the worklist p6_stubs.cpp must close.
set -u

CC=/work/jo-engine/Compiler/LINUX/bin/sh-none-elf-gcc-8.2.0
NEWLIB=/work/jo-engine/Compiler/WINDOWS/sh-elf/include
DEPS=/work/rsdkv5-src/dependencies/all
PLAT=/work/platform/Saturn
SHIM=/work/tools/_portspike/shim
SRC=/work/rsdkv5-src/RSDKv5
OBJ=/work/tools/_portspike/_trueport_obj
P5=/work/tools/_portspike/_p5
P6=/work/tools/_portspike/_p6
LINKER=$P5/p5.linker

CORE_DEFS="-DRETRO_USE_MOD_LOADER=0 -DRETRO_REVISION=2 -DSCREEN_XMAX=320 -DSCREEN_YSIZE=224"
CORE_INC="-I$PLAT -I$SHIM -I$SRC -I$DEPS -I$NEWLIB"
CXXFLAGS="-x c++ -std=gnu++11 -m2 -O2 -include stdlib.h -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-builtin"

echo "[1/3] Rebuilding 21 true-port objects (ensures fresh FunctionTables_Saturn.o etc.) ..."
bash /work/tools/_portspike/build_trueport.sh >/dev/null 2>&1 || { echo "build_trueport FAIL"; exit 1; }

echo "[2/3] Compiling REAL Core/Link.cpp -> Core_Link.o ..."
$CC $CXXFLAGS $CORE_DEFS $CORE_INC -c -o "$P6/Core_Link.o" "$SRC/RSDK/Core/Link.cpp" 2>"$P6/_link_compile_err.txt" \
  || { echo "Core/Link.cpp COMPILE FAIL:"; cat "$P6/_link_compile_err.txt"; exit 1; }
echo "        Core_Link.o = $(wc -c < "$P6/Core_Link.o") bytes."

echo "[3/3] Probe link: p5 objs + Core_Link.o, EXCLUDING FunctionTables_Saturn.o ..."
# Build the object list, dropping the empty stub TU that Link.cpp supersedes.
NONFT=""
for o in "$OBJ"/*.o; do
  case "$o" in
    *FunctionTables_Saturn.o) ;;   # drop -- Core_Link.o supplies these symbols
    *) NONFT="$NONFT $o" ;;
  esac
done

"$CC" -m2 -nostartfiles -T "$LINKER" -Wl,-Map="$P6/_probe.map" \
  -o "$P6/_probe.elf" \
  "$P5/crt0.o" "$P5/p5_main.o" "$P5/p5_ring.o" "$P5/p5_syscalls.o" "$P6/Core_Link.o" \
  $NONFT \
  -Wl,--start-group -lc -lnosys -lgcc -Wl,--end-group \
  2>"$P6/_probe_link_err.txt"

echo "=== link exit $? (nonzero = expected: undefined refs) ==="
echo "--- total undefined-reference lines ---"
grep -c "undefined reference" "$P6/_probe_link_err.txt" || true
echo "--- UNIQUE undefined symbols (sorted) ---"
grep -o "undefined reference to \`[^']*'" "$P6/_probe_link_err.txt" \
  | sed "s/undefined reference to \`//; s/'$//" \
  | sort -u | tee "$P6/_undef_symbols.txt"
echo "--- count unique ---"
wc -l < "$P6/_undef_symbols.txt"
