#!/usr/bin/env bash
# P2 gate -- real Saturn link of the true-port object set (engine true-port, #201).
#
# Links the 21 P1 true-port relocatables (16 logic-core + 5 Saturn backend shim)
# + 4 P2 support objects (step [2/4]) against the REAL jo Saturn toolchain
# runtime libraries:
#     newlib  libc.a      (the 37 LIBC externals: mem*/str*/printf-family/malloc)
#     newlib  libnosys.a  (the _sbrk/_write/_read/_close/_lseek/_exit syscall
#                          stubs that malloc/printf/fopen pull in)
#     gcc     libgcc.a    (the 32 TOOLCHAIN externals: SH-2 soft-float/64-bit
#                          arithmetic helpers + C++ ABI runtime)
# to prove SYMBOL CLOSURE: a real Saturn link leaves ZERO unresolved symbols.
#
# This toolchain ships NO libm.a (verified empirically via `sh-none-elf-gcc -m2
# -print-file-name=libm.a` -> bare "libm.a" == NOT FOUND, while libc.a/libnosys.a/
# libgcc.a all resolve to real absolute paths), and newlib's libc.a does NOT fold
# in the transcendentals. So P2 AUTHORS the missing math as platform/Saturn/
# libm_saturn.c (fdlibm-derived, freestanding, host-verified bit-exact against
# the canonical RFC-1321 MD5 table by qa_p2_libm_verify.py). The two core float-
# math callers it satisfies:
#     Math.cpp:55-103  sinf cosf tanf asinf acosf atan2  (trig lookup tables)
#     Text.cpp:38-40   pow fabs sin                       (MD5 sine table)
# Two more gaps the support objects close: operator new/delete (no libsupc++ on
# jo) via platform/Saturn/CppRuntime_Saturn.cpp, and `main` (libc init/exit
# reference) via a trivial P2 stub (tools/_portspike/shim/p2_main_stub.c, dropped
# at P3 for the real engine entry). After these, the measured gap is ZERO.
#
# SCOPE: P2 is SYMBOL CLOSURE only. It links FLAT (default ld script, entry 0,
# -nostartfiles) purely to resolve references -- it does NOT place the image with
# sgl.linker, because the stock 5.88 MB .bss (Probe 7) would overflow the Saturn
# RAM regions. Section placement + the 2 MB data fit is P4's retarget problem.
# This gate asserts: 0 unresolved symbols after a real-library link. The linked
# ELF it emits (_p2_link.elf) feeds the P2 code-size measure (qa_p2_memfit.py).
set -u

CC=/work/jo-engine/Compiler/LINUX/bin/sh-none-elf-gcc-8.2.0
NEWLIB=/work/jo-engine/Compiler/WINDOWS/sh-elf/include
DEPS=/work/rsdkv5-src/dependencies/all
PLAT=/work/platform/Saturn
SHIM=/work/tools/_portspike/shim
OBJ=/work/tools/_portspike/_trueport_obj
EVID=/work/tools/_portspike/_p2_link.txt
LINKOUT=/work/tools/_portspike/_p2_link.elf
ERRLOG=/work/tools/_portspike/_p2_link_err.txt

: > "$EVID"
emit() { echo "$1" | tee -a "$EVID"; }

# Compile one P2 support TU into _trueport_obj/ (so the [3/4] "$OBJ"/*.o glob
# picks it up). Fail-fast, naming the failing TU + its first error.
# Usage: cc_one <src> <out.o> <label> <flags...>
cc_one() {
  local src="$1" out="$2" label="$3"; shift 3
  if "$CC" "$@" -c -o "$out" "$src" 2>>"$ERRLOG"; then
    emit "        $label compiled."
  else
    emit "  FAIL  $label did not compile:"
    sed 's/^/          /' "$ERRLOG" | tee -a "$EVID"
    exit 1
  fi
}

emit "=============================================================="
emit "P2 gate: real Saturn link -- 21 true-port .o + miniz + newlib + libgcc"
emit "=============================================================="

# [1/4] Ensure the 21 true-port objects exist (delegate to the P1 builder, which
#       rm -rf's and rebuilds _trueport_obj/ -> a clean, current 16+5 set).
emit "[1/4] Rebuilding 21 true-port objects (build_trueport.sh) ..."
if ! bash /work/tools/_portspike/build_trueport.sh >/dev/null 2>&1; then
  emit "  FAIL  build_trueport.sh did not yield 21/21 objects (P1 regressed)."
  exit 1
fi
nobj=$(ls "$OBJ"/*.o 2>/dev/null | wc -l | tr -d ' ')
emit "        $nobj core+shim objects present in _trueport_obj/."

# [2/4] Compile the P2 support objects that close the link alongside the 21
#       true-port .o. Each lands in $OBJ so the [3/4] "$OBJ"/*.o glob includes
#       it. Same -m2 -O2 -fno-builtin environment as the core (-fno-builtin is
#       load-bearing for libm: it forces real emitted sin/cos/pow/fabs symbols
#       instead of GCC folding them to intrinsics).
#         miniz.o             1 MINIZ symbol  : uncompress (Reader.hpp:413)
#         libm_saturn.o       9 LIBM symbols  : sinf cosf tanf asinf acosf
#                                               atan2 pow sin fabs (Math.cpp,
#                                               Text.cpp init tables) -- this
#                                               toolchain ships no libm.a
#         CppRuntime_Saturn.o operator delete(void*) (+ new/new[]/delete[]/
#                                               sized) : no libsupc++ on jo
#         p2_main_stub.o      main            : libc init/exit reference; a
#                                               trivial P2 closure, replaced by
#                                               the real engine entry at P3
emit "[2/4] Compiling P2 support objects (miniz + libm + C++ runtime + main) ..."
: > "$ERRLOG"
cc_one "$DEPS/miniz/miniz.c"          "$OBJ/miniz.o"             "miniz.o (decompressor)" \
       -x c   -std=gnu11   -m2 -O2 -fno-builtin -DMINIZ_NO_TIME -I"$DEPS" -I"$NEWLIB"
cc_one "$PLAT/libm_saturn.c"          "$OBJ/libm_saturn.o"       "libm_saturn.o (9 math symbols)" \
       -x c   -std=gnu11   -m2 -O2 -fno-builtin -I"$NEWLIB"
cc_one "$PLAT/CppRuntime_Saturn.cpp"  "$OBJ/CppRuntime_Saturn.o" "CppRuntime_Saturn.o (operator new/delete)" \
       -x c++ -std=gnu++11 -m2 -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-builtin -I"$NEWLIB"
cc_one "$SHIM/p2_main_stub.c"         "$OBJ/p2_main_stub.o"      "p2_main_stub.o (main closure)" \
       -x c   -std=gnu11   -m2 -O2 -fno-builtin -I"$NEWLIB"

# [3/4] Link flat against the real toolchain libraries. --start-group/--end-group
#       resolves the libc<->libnosys<->libgcc back-references. report-all +
#       noinhibit-exec let the link complete + emit the ELF even with residual
#       undefined, so we can NAME every gap rather than die on the first.
emit "[3/4] Linking (flat, entry 0, real libc/libnosys/libgcc) ..."
: > "$ERRLOG"
"$CC" -m2 -nostartfiles -Wl,-e,0 \
  -Wl,--unresolved-symbols=report-all -Wl,--noinhibit-exec \
  -o "$LINKOUT" \
  "$OBJ"/*.o \
  -Wl,--start-group -lc -lnosys -lgcc -Wl,--end-group \
  2>"$ERRLOG" || true

# [4/4] Harvest + classify residual undefined symbols. P2 GREEN == zero.
undef=$(grep "undefined reference to" "$ERRLOG" \
        | sed 's/.*undefined reference to .//' | sed 's/.$//' | sort -u)
if [ -z "$undef" ]; then
  nundef=0
else
  nundef=$(printf '%s\n' "$undef" | grep -c .)
fi

emit "--------------------------------------------------------------"
if [ "$nundef" -eq 0 ]; then
  if [ -f "$LINKOUT" ]; then
    nbytes=$(wc -c < "$LINKOUT" | tr -d ' ')
    emit "  Linked ELF: _p2_link.elf ($nbytes bytes)."
  fi
  emit "  UNRESOLVED SYMBOLS: 0"
  emit "--------------------------------------------------------------"
  emit "RESULT: GREEN -- the true-port object set LINKS CLEAN against the real"
  emit "        Saturn toolchain (newlib libc + libnosys + libgcc) plus the P2"
  emit "        support objects (miniz + libm_saturn + CppRuntime_Saturn + main"
  emit "        stub). Zero unresolved symbols: every core reference is satisfied"
  emit "        by a Saturn shim, the toolchain runtime, libc, our libm, the C++"
  emit "        runtime, or miniz. Symbol closure done."
  emit "        Next: P2 code-size measure (qa_p2_memfit.py on _p2_link.elf)."
  exit 0
else
  emit "  UNRESOLVED SYMBOLS: $nundef (P2 requires 0)"
  emit "--------------------------------------------------------------"
  # Label each residual so the gap class is obvious (math? syscall? other?).
  printf '%s\n' "$undef" | while IFS= read -r s; do
    [ -n "$s" ] || continue
    case "$s" in
      sinf|cosf|tanf|asinf|acosf|atan2|atan2f|pow|powf|sin|cos|fabs|fabsf|sqrt|sqrtf|fmod|fmodf|ceil|ceilf|floor|floorf|exp|expf|log|logf|ldexp|frexp|modf|scalbn|copysign|copysignf|__ieee754*)
        cls="LIBM-MATH (no libm.a in toolchain)";;
      _*) cls="SYSCALL/RUNTIME stub";;
      *)  cls="OTHER";;
    esac
    emit "$(printf '    %-42s %s' "$s" "$cls")"
  done
  emit "--------------------------------------------------------------"
  emit "RESULT: RED -- $nundef unresolved after a real-library link. The named"
  emit "        symbols are the measured P2 gap to close before the core links"
  emit "        on the Saturn toolchain."
  exit 1
fi
