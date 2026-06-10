#!/usr/bin/env bash
# Probe 5 -- logic-core SH-2 compile target (engine true-port spike, Task #194).
#
# Compiles the RSDKv5 platform-INDEPENDENT logic core (NOT the render/audio/
# video/loader device backend, NOT Legacy, NOT per-platform device TUs) to real
# SH-2 object files on the jo-engine toolchain, proving the core codegens before
# any Saturn gameplay/render is wired.
#
# Run inside the joengine-saturn Docker image:
#   docker run --rm -v "D:/sonicmaniasaturn":/work -w /work joengine-saturn:latest \
#       bash tools/_portspike/build_core_target.sh
#
# Exit 0 only if EVERY core TU produced a valid SH-2 .o. Otherwise exit 1 and
# print the first error line of each failing TU (the measured gap to close).
set -u

CC=/work/jo-engine/Compiler/LINUX/bin/sh-none-elf-gcc-8.2.0
NEWLIB=/work/jo-engine/Compiler/WINDOWS/sh-elf/include
# NOTE: the jo-engine LINUX toolchain ships ONLY gcc + objcopy -- no nm/objdump/
# size/readelf. Object validity is therefore checked by reading the ELF header
# directly: magic 7f 45 4c 46, and e_machine (bytes 18-19, big-endian on the
# Saturn SH-2) == 0x002A == EM_SH. That proves a real SH-architecture relocatable
# was emitted -- a STRONGER check than "nm could read it".
is_sh_obj() {
  # $1 = path to candidate .o; returns 0 iff ELF32-BE relocatable for EM_SH.
  [ -f "$1" ] || return 1
  local b
  read -r -a b <<< "$(od -An -tx1 -N20 "$1" 2>/dev/null | tr '\n' ' ')"
  [ "${b[0]-}${b[1]-}${b[2]-}${b[3]-}" = "7f454c46" ] || return 1   # ELF magic
  [ "${b[18]-}${b[19]-}" = "002a" ] || return 1                      # EM_SH (big-endian)
  return 0
}
SHIM=/work/tools/_portspike/shim
PLAT=/work/platform/Saturn
SRC=/work/rsdkv5-src/RSDKv5
DEPS=/work/rsdkv5-src/dependencies/all
OBJ=/work/tools/_portspike/_coreobj
mkdir -p "$OBJ"

# The platform-independent logic core. Excluded by design (Saturn device
# backend / replaced / not used by Mania):
#   Graphics/Drawing.cpp  -> software rasterizer (Probe 3: infeasible; VDP1/VDP2)
#   Graphics/Video.cpp    -> theora FMV (Saturn Cinepak)
#   Audio/Audio.cpp       -> SCSP backend
#   Core/Link.cpp,ModAPI  -> mod/dynamic-library loader (no dlopen; MOD_LOADER=0)
#   User/**, **/Legacy/** -> services Dummy backend / non-RSDKv5-native
#   **/<Platform>/**      -> SDL2/DX/GL/Vulkan/NX/Steam device TUs
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

CFLAGS=(
  -x c++ -std=gnu++11 -m2 -O2 -c
  -include stdlib.h
  -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-builtin
  -DRETRO_USE_MOD_LOADER=0 -DRETRO_REVISION=2
  -DSCREEN_XMAX=320 -DSCREEN_YSIZE=224
  -I"$PLAT" -I"$SHIM" -I"$SRC" -I"$DEPS" -I"$NEWLIB"
  -fmax-errors=8
)

pass=0; fail=0; failed_list=""
echo "=============================================================="
echo "Probe 5: RSDKv5 logic-core -> SH-2 .o  (${#CORE_TUS[@]} TUs)"
echo "=============================================================="
for tu in "${CORE_TUS[@]}"; do
  base=$(echo "$tu" | tr '/' '_')
  out="$OBJ/${base%.cpp}.o"
  rm -f "$out"
  err=$("$CC" "${CFLAGS[@]}" -o "$out" "$SRC/RSDK/$tu" 2>&1)
  rc=$?
  if [ "$rc" -eq 0 ] && is_sh_obj "$out"; then
    nbytes=$(wc -c < "$out" 2>/dev/null | tr -d ' ')
    printf "  OK    %-34s (SH ELF, %s bytes)\n" "$tu" "$nbytes"
    pass=$((pass+1))
  else
    first=$(echo "$err" | grep -E "error:|fatal error:" | head -1)
    printf "  FAIL  %-34s %s\n" "$tu" "${first:-<no SH .o emitted>}"
    fail=$((fail+1))
    failed_list="$failed_list $tu"
  fi
done
echo "--------------------------------------------------------------"
echo "PASS=$pass  FAIL=$fail  / ${#CORE_TUS[@]}"
if [ "$fail" -eq 0 ]; then
  echo "RESULT: GREEN -- entire logic core codegens to SH-2 objects."
  exit 0
else
  echo "RESULT: RED -- failing TUs:$failed_list"
  exit 1
fi
