#!/usr/bin/env bash
# P1 gate -- TRUEPORT codegen into the shipping build (engine true-port pivot, #200).
#
# Compiles, with the SHIPPING Saturn toolchain + Probe-5 flags, the COMPLETE
# true-port object set the real Saturn link (P2) will consume:
#   - the 16 platform-INDEPENDENT RSDKv5 logic-core TUs  (Probe 5, proven .o)
#   - the  5 Saturn backend shim TUs                     (P0, contract-closing .o)
# = 21 SH-2 relocatables in ONE output tree (_trueport_obj/), produced by the
# `trueport` Makefile target. This is the P1 deliverable: the shipping build
# system can emit the true-port objects behind TRUEPORT=1, with the default
# `make` byte-identical (the trueport target is inert unless selected).
#
# Run inside the joengine-saturn Docker image, EITHER directly:
#   docker run --rm -v "D:/sonicmaniasaturn":/work -w /work joengine-saturn:latest \
#       bash tools/_portspike/build_trueport.sh
# OR via the Makefile flag (the canonical shipping-build invocation):
#   docker run --rm -v "D:/sonicmaniasaturn":/work -w /work joengine-saturn:latest \
#       make TRUEPORT=1
#
# Exit 0 iff ALL 21 TUs emit a valid SH-2 ELF relocatable (16 core + 5 shim).
# Otherwise exit 1 and name every failing TU (the measured gap to close).
#
# Scope note: P1 is CODEGEN ONLY. No link, no libc/libgcc/miniz resolution --
# that is P2 (real Saturn link). This gate asserts 21/21 .o, nothing more.
set -u

CC=/work/jo-engine/Compiler/LINUX/bin/sh-none-elf-gcc-8.2.0
NEWLIB=/work/jo-engine/Compiler/WINDOWS/sh-elf/include
SHIM_INC=/work/tools/_portspike/shim
PLAT=/work/platform/Saturn
SRC=/work/rsdkv5-src/RSDKv5
DEPS=/work/rsdkv5-src/dependencies/all
OBJ=/work/tools/_portspike/_trueport_obj
EVID=/work/tools/_portspike/_p1_trueport.txt

# Object validity is checked by reading the ELF header directly (the jo LINUX
# toolchain ships ONLY gcc + objcopy -- no nm/objdump/readelf): magic
# 7f 45 4c 46, and e_machine (bytes 18-19, big-endian) == 0x002A == EM_SH.
# That proves a real SH-architecture relocatable was emitted.
is_sh_obj() {
  [ -f "$1" ] || return 1
  local b
  read -r -a b <<< "$(od -An -tx1 -N20 "$1" 2>/dev/null | tr '\n' ' ')"
  [ "${b[0]-}${b[1]-}${b[2]-}${b[3]-}" = "7f454c46" ] || return 1   # ELF magic
  [ "${b[18]-}${b[19]-}" = "002a" ] || return 1                      # EM_SH (big-endian)
  return 0
}

# The 16 platform-independent logic-core TUs (relative to $SRC/RSDK).
CORE_TUS=(
  Core/Math.cpp
  Core/Reader.cpp
  Storage/Storage.cpp
  Storage/Text.cpp
  Graphics/Animation.cpp
  Graphics/Palette.cpp
  Graphics/Scene3D.cpp
  Graphics/Sprite.cpp
  Scene/Object.cpp
  Scene/Scene.cpp
  Scene/Collision.cpp
  Scene/Objects/DefaultObject.cpp
  Scene/Objects/DevOutput.cpp
  Input/Input.cpp
  Dev/Debug.cpp
  Core/RetroEngine.cpp
)

# The 5 Saturn backend shim TUs (relative to $PLAT) -- the P0 contract closers.
SHIM_TUS=(
  RenderDevice_Saturn.cpp
  AudioDevice_Saturn.cpp
  UserCore_Saturn.cpp
  FunctionTables_Saturn.cpp
  Video_Saturn.cpp
)

# IDENTICAL to build_core_target.sh + qa_p0_backend_contract.sh CFLAGS, so every
# TU sees the same preprocessor environment + struct layouts. RETRO_PLATFORM
# auto-resolves to RETRO_SATURN via the compiler's __sh__ predefine
# (RetroEngine.hpp:146-148) -- no -DRETRO_PLATFORM is needed or wanted.
CFLAGS=(
  -x c++ -std=gnu++11 -m2 -O2 -c
  -include stdlib.h
  -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-builtin
  -DRETRO_USE_MOD_LOADER=0 -DRETRO_REVISION=2
  -DSCREEN_XMAX=320 -DSCREEN_YSIZE=224
  -I"$PLAT" -I"$SHIM_INC" -I"$SRC" -I"$DEPS" -I"$NEWLIB"
  -fmax-errors=8
)

rm -rf "$OBJ"; mkdir -p "$OBJ"
: > "$EVID"
emit() { echo "$1" | tee -a "$EVID"; }

emit "=============================================================="
emit "P1 gate: TRUEPORT codegen (16 core + 5 shim -> 21 SH-2 .o)"
emit "=============================================================="

core_pass=0; core_fail=0; shim_pass=0; shim_fail=0; failed_list=""

compile_one() {
  # $1 = source path, $2 = output .o path, $3 = human label
  local src="$1" out="$2" label="$3" err rc
  rm -f "$out"
  err=$("$CC" "${CFLAGS[@]}" -o "$out" "$src" 2>&1); rc=$?
  if [ "$rc" -eq 0 ] && is_sh_obj "$out"; then
    local nbytes; nbytes=$(wc -c < "$out" 2>/dev/null | tr -d ' ')
    emit "$(printf '  OK    %-36s (SH ELF, %s bytes)' "$label" "$nbytes")"
    return 0
  else
    local first; first=$(echo "$err" | grep -E "error:|fatal error:" | head -1)
    emit "$(printf '  FAIL  %-36s %s' "$label" "${first:-<no SH .o emitted>}")"
    failed_list="$failed_list $label"
    return 1
  fi
}

emit "[1/2] Logic core (16 TUs) ..."
for tu in "${CORE_TUS[@]}"; do
  base=$(echo "$tu" | tr '/' '_')
  if compile_one "$SRC/RSDK/$tu" "$OBJ/${base%.cpp}.o" "core/$tu"; then
    core_pass=$((core_pass+1))
  else
    core_fail=$((core_fail+1))
  fi
done

emit "[2/2] Saturn backend shims (5 TUs) ..."
for tu in "${SHIM_TUS[@]}"; do
  if [ ! -f "$PLAT/$tu" ]; then
    emit "$(printf '  FAIL  %-36s <shim source absent>' "shim/$tu")"
    shim_fail=$((shim_fail+1)); failed_list="$failed_list shim/$tu"
    continue
  fi
  if compile_one "$PLAT/$tu" "$OBJ/${tu%.cpp}.o" "shim/$tu"; then
    shim_pass=$((shim_pass+1))
  else
    shim_fail=$((shim_fail+1))
  fi
done

total_pass=$((core_pass+shim_pass))
total_fail=$((core_fail+shim_fail))
emit "--------------------------------------------------------------"
emit "$(printf '  CORE  : %2d/16   SHIM : %2d/5   TOTAL : %2d/21' "$core_pass" "$shim_pass" "$total_pass")"
emit "--------------------------------------------------------------"

if [ "$total_fail" -eq 0 ] && [ "$core_pass" -eq 16 ] && [ "$shim_pass" -eq 5 ]; then
  emit "RESULT: GREEN -- all 21 true-port TUs codegen to SH-2 objects in"
  emit "        _trueport_obj/. The shipping build system emits the true-port"
  emit "        object set behind TRUEPORT=1; default 'make' is unaffected."
  emit "        Next: P2 links these 21 + libgcc + newlib + miniz (real Saturn link)."
  exit 0
else
  emit "RESULT: RED -- failing TUs:$failed_list"
  exit 1
fi
