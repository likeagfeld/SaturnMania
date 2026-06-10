#!/usr/bin/env bash
# P0 gate -- Saturn backend-contract CLOSURE (engine true-port pivot, Task #199).
#
# Probe 6 (link_probe_core.sh) measured that the 16 platform-INDEPENDENT logic-core
# TUs, linked alone, leave exactly 50 UNDEFINED "BACKEND" symbols: the device-backend
# API a true Saturn port must implement (Draw* primitives, RenderDeviceBase::*, the
# render/scene globals, AudioDevice::Release + channels + LoadSfx/ClearStageSfx, the
# SKU user services, the RSDK/API function tables, settings INI, ProcessVideo). That
# enumerated set IS the contract.
#
# This gate adds the Saturn backend SHIM TUs (platform/Saturn/*_Saturn.cpp) to the
# link and asserts the BACKEND class drops 50 -> 0. Each shim #includes the REAL
# engine header (RSDK/Core/RetroEngine.hpp) and DEFINES its contract symbols, so the
# C++ compiler enforces ABI-EXACT mangled-name matching: a wrong signature leaves the
# contract symbol undefined -> it stays in the BACKEND count -> the gate stays RED and
# names the still-open symbol. There is no way to "accidentally pass" with a wrong
# signature.
#
# The shims compile with the SAME compiler + SAME flags as the core. RETRO_PLATFORM
# auto-resolves to RETRO_SATURN via the compiler's __sh__ predefine (RetroEngine.hpp:
# 146-148), so struct layouts and device gating are identical to the core's -- no
# -DRETRO_PLATFORM is needed or wanted.
#
# Run inside the joengine-saturn Docker image:
#   docker run --rm -v "D:/sonicmaniasaturn":/work -w /work joengine-saturn:latest \
#       bash tools/_portspike/qa_p0_backend_contract.sh
#
# NOSHIM=1 forces the core-only baseline (skip the shims) to DEMONSTRATE the gate
# fires RED at 50 BACKEND -- the RED-first proof the gate catches an open contract.
#
# Exit 0 iff BACKEND == 0 AND ORPHAN == 0. Otherwise exit 1.
set -u

DIR=/work/tools/_portspike
CC=/work/jo-engine/Compiler/LINUX/bin/sh-none-elf-gcc-8.2.0
NEWLIB=/work/jo-engine/Compiler/WINDOWS/sh-elf/include
SHIM_INC=/work/tools/_portspike/shim
PLAT=/work/platform/Saturn
SRC=/work/rsdkv5-src/RSDKv5
DEPS=/work/rsdkv5-src/dependencies/all
COREOBJ=$DIR/_coreobj
SHIMOBJ=$DIR/_shimobj
LINKOUT=$DIR/_p0_link.elf
ERRLOG=$DIR/_p0_ld.txt

# The Saturn backend shim TUs that, together, must define EXACTLY the 50-symbol
# contract. Absent files are tolerated (RED-first state before they are written).
SHIM_TUS=(
  RenderDevice_Saturn.cpp
  AudioDevice_Saturn.cpp
  UserCore_Saturn.cpp
  FunctionTables_Saturn.cpp
  Video_Saturn.cpp
)

# IDENTICAL to build_core_target.sh CFLAGS (so the shims see the same preprocessor
# environment + struct layouts as the core they link against).
CFLAGS=(
  -x c++ -std=gnu++11 -m2 -O2 -c
  -include stdlib.h
  -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-builtin
  -DRETRO_USE_MOD_LOADER=0 -DRETRO_REVISION=2
  -DSCREEN_XMAX=320 -DSCREEN_YSIZE=224
  -I"$PLAT" -I"$SHIM_INC" -I"$SRC" -I"$DEPS" -I"$NEWLIB"
  -fmax-errors=8
)

echo "=============================================================="
echo "P0 gate: Saturn backend-contract closure (BACKEND 50 -> 0)"
echo "=============================================================="

# (1) Core objects (Probe 5 -- fresh, no stale-.o risk).
echo "[1/4] Compiling logic core (build_core_target.sh) ..."
if ! bash "$DIR/build_core_target.sh" >/dev/null 2>&1; then
  echo "RESULT: RED -- core did not codegen (Probe 5 failed); cannot contract-probe."
  exit 1
fi
ncore=$(ls "$COREOBJ"/*.o 2>/dev/null | wc -l | tr -d ' ')
echo "        $ncore core objects ready."

# (2) Compile whatever backend shim TUs exist. A missing shim leaves its part of the
#     contract open (RED). A shim that FAILS to compile (signature/type error) is RED.
echo "[2/4] Compiling Saturn backend shims ..."
rm -rf "$SHIMOBJ"; mkdir -p "$SHIMOBJ"
nshim=0; shim_fail=0
if [ "${NOSHIM:-0}" = "1" ]; then
  echo "        [NOSHIM] skipping shims -- core-only baseline (expect 50 BACKEND, RED)."
else
  for tu in "${SHIM_TUS[@]}"; do
    if [ ! -f "$PLAT/$tu" ]; then
      printf "  --    %-28s (absent -- contract part still open)\n" "$tu"
      continue
    fi
    out="$SHIMOBJ/${tu%.cpp}.o"
    err=$("$CC" "${CFLAGS[@]}" -o "$out" "$PLAT/$tu" 2>&1)
    if [ $? -eq 0 ] && [ -f "$out" ]; then
      printf "  OK    %-28s\n" "$tu"
      nshim=$((nshim+1))
    else
      printf "  FAIL  %-28s %s\n" "$tu" "$(echo "$err" | grep -E 'error:|fatal error:' | head -1)"
      shim_fail=$((shim_fail+1))
    fi
  done
fi
if [ "$shim_fail" -gt 0 ]; then
  echo "--------------------------------------------------------------"
  echo "RESULT: RED -- $shim_fail shim TU(s) failed to compile."
  echo "        A type/signature error means the shim cannot define its contract"
  echo "        symbol ABI-exactly. Fix the signature against the engine header."
  exit 1
fi
echo "        $nshim shim objects compiled."

# (3) Link core + shims, NO libc/libgcc/miniz; report every unresolved symbol.
echo "[3/4] Linking core + shims (no libc/libgcc/backend-from-src) ..."
rm -f "$LINKOUT" "$ERRLOG"
# nullglob so an EMPTY shim dir (RED-first state) drops the shim glob entirely
# instead of passing a literal "$SHIMOBJ/*.o" to gcc (which would fail file-open
# BEFORE symbol resolution -> zero "undefined reference" lines -> false GREEN).
shopt -s nullglob
LINK_OBJS=("$COREOBJ"/*.o "$SHIMOBJ"/*.o)
shopt -u nullglob
"$CC" -nostdlib -Wl,--unresolved-symbols=report-all -Wl,-e,0 \
      -o "$LINKOUT" "${LINK_OBJS[@]}" 2>"$ERRLOG" || true

UNDEF=$(grep "undefined reference to" "$ERRLOG" \
        | sed 's/.*undefined reference to .//' \
        | sed 's/.$//' \
        | sort -u)

# (4) Classify every undefined symbol (same 4 known-external classes as Probe 6).
classify() {
  case "$1" in
    __*) echo TOOLCHAIN; return;;
    "operator delete"*|"operator new"*) echo TOOLCHAIN; return;;
    memcpy|memset|memcmp|memmove|memchr|\
    strlen|strcmp|strncmp|strcpy|strncpy|strcat|strncat|strstr|strchr|strrchr|\
    malloc|calloc|realloc|free|\
    abs|labs|atoi|atol|atof|rand|srand|qsort|bsearch|time|\
    printf|fprintf|sprintf|snprintf|vsprintf|vsnprintf|vprintf|puts|putchar|\
    fopen|fclose|fread|fwrite|fseek|ftell|fflush|feof|ferror|fgets|fputs|fputc|\
    sin|sinf|cos|cosf|tan|tanf|asin|asinf|acos|acosf|atan|atanf|atan2|atan2f|\
    pow|powf|exp|expf|log|logf|sqrt|sqrtf|fabs|fabsf|floor|floorf|ceil|ceilf|fmod)
      echo LIBC; return;;
    mz_*|tinfl_*|tdefl_*) echo MINIZ; return;;
    # --- BACKEND: the 50-symbol Saturn device-backend contract (Probe 6) ----------
    RSDK::Draw*) echo BACKEND; return;;
    RSDK::RenderDeviceBase::*) echo BACKEND; return;;
    RSDK::AudioDevice::*) echo BACKEND; return;;
    RSDK::SKU::*) echo BACKEND; return;;
    RSDK::*FunctionTable) echo BACKEND; return;;
    RSDK::SetupFunctionTables*) echo BACKEND; return;;
    RSDK::ProcessVideo*) echo BACKEND; return;;
    RSDK::ClearStageSfx*|RSDK::LoadSfx*) echo BACKEND; return;;
    RSDK::*SettingsINI*) echo BACKEND; return;;
    RSDK::UpdateGameWindow*) echo BACKEND; return;;
    RSDK::cameras|RSDK::cameraCount|RSDK::currentScreen|RSDK::screens|\
    RSDK::drawGroups|RSDK::drawGroupNames|RSDK::gfxSurface|\
    RSDK::videoSettings|RSDK::changedVideoSettings|RSDK::channels|\
    RSDK::customSettings|RSDK::gameVerInfo|RSDK::shaderList)
      echo BACKEND; return;;
  esac
  echo ORPHAN
}

nB=0; nT=0; nL=0; nM=0; nO=0; backend_open=""; orphans=""
while IFS= read -r sym; do
  [ -z "$sym" ] && continue
  case "$(classify "$sym")" in
    BACKEND)   nB=$((nB+1)); backend_open="$backend_open    $sym
";;
    TOOLCHAIN) nT=$((nT+1));;
    LIBC)      nL=$((nL+1));;
    MINIZ)     nM=$((nM+1));;
    ORPHAN)    nO=$((nO+1)); orphans="$orphans  $sym
";;
  esac
done <<< "$UNDEF"

total=$((nB+nT+nL+nM+nO))
echo "[4/4] Classified $total undefined symbols after adding shims:"
echo "--------------------------------------------------------------"
printf "  BACKEND   (contract -- MUST be 0 now)      : %3d  (was 50)\n" "$nB"
printf "  TOOLCHAIN (libgcc + C++ ABI)               : %3d\n" "$nT"
printf "  LIBC      (newlib libc/libm)               : %3d\n" "$nL"
printf "  MINIZ     (bundled decompressor)           : %3d\n" "$nM"
printf "  ORPHAN    (must be 0)                       : %3d\n" "$nO"
echo "--------------------------------------------------------------"
if [ "$nB" -gt 0 ]; then
  echo "Contract symbols STILL UNDEFINED (shim missing or signature mismatch):"
  printf "%s" "$backend_open"
  echo "--------------------------------------------------------------"
fi
if [ "$nO" -gt 0 ]; then
  echo "ORPHAN symbols (referenced by core, defined nowhere):"
  printf "%s" "$orphans"
  echo "--------------------------------------------------------------"
fi

if [ "$nB" -eq 0 ] && [ "$nO" -eq 0 ]; then
  echo "RESULT: GREEN -- backend contract CLOSED. The 5 Saturn shim TUs define all"
  echo "        50 contract symbols ABI-exactly; the only remaining externals are the"
  echo "        toolchain runtime + libc + miniz (resolved at the real link, P2)."
  exit 0
else
  echo "RESULT: RED -- contract NOT closed (BACKEND=$nB, ORPHAN=$nO)."
  exit 1
fi
