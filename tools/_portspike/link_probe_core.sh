#!/usr/bin/env bash
# Probe 6 -- logic-core link self-consistency probe (engine true-port spike, Task #194/#198).
#
# Probe 5 (build_core_target.sh) proved the 16 platform-INDEPENDENT logic-core
# TUs each CODEGEN to a valid SH-2 ELF .o. That is necessary but not sufficient:
# a TU can compile yet reference a symbol that exists NOWHERE in the engine (a
# typo, a misplaced #if, a half-ported call). This probe closes that gap.
#
# It links all 16 core .o together with NO libraries and NO device backend, then
# enumerates every UNDEFINED symbol the link reports (the jo-engine LINUX
# toolchain ships no nm/objdump, so undefined refs are harvested from ld's own
# "undefined reference to `X'" diagnostics -- a STRONGER check than nm because it
# only lists symbols actually ODR-used by emitted core code).
#
# Each undefined symbol MUST fall into exactly one of four KNOWN-external classes:
#   BACKEND   RSDK:: symbols defined in the device-backend TUs deliberately
#             EXCLUDED from the core set (Drawing.cpp software raster, Video.cpp
#             theora, Audio.cpp SCSP, RenderDevice*, User/SKU services, Link.cpp
#             function tables, settings INI). This enumerated set IS the spike's
#             deliverable: the exact Saturn device-backend API a true port must
#             implement. (Verified e.g. RSDK::cameraCount -> Graphics/Drawing.cpp:156.)
#   TOOLCHAIN libgcc soft-float / integer / SH-2 shift helpers (__adddf3,
#             __divsf3, __ashrsi3, __ashiftrt_r4_*, __lshrsi3_r0 ...) + C++ ABI
#             (operator new/delete). Provided by libgcc.a at final link.
#   LIBC      newlib libc/libm (memcpy, strlen, malloc, sinf, pow ...).
#   MINIZ     the bundled decompressor in rsdkv5-src/dependencies/all (mz_*).
#
# A symbol matching NONE of those four is an ORPHAN: the core references code
# that is not a known external and not in the core -> the logic core is NOT
# self-consistent. Any orphan => RED.
#
# Run inside the joengine-saturn Docker image:
#   docker run --rm -v "D:/sonicmaniasaturn":/work -w /work joengine-saturn:latest \
#       bash tools/_portspike/link_probe_core.sh
#
# SELFTEST=1 injects one synthetic orphan to PROVE the gate can fire RED:
#   docker run ... -e SELFTEST=1 ... bash tools/_portspike/link_probe_core.sh
#
# Exit 0 iff every undefined symbol is a known external (0 orphans). Else exit 1.
set -u

DIR=/work/tools/_portspike
CC=/work/jo-engine/Compiler/LINUX/bin/sh-none-elf-gcc-8.2.0
OBJ=$DIR/_coreobj
LINKOUT=$DIR/_linkprobe.elf
ERRLOG=$DIR/_linkprobe_ld.txt

echo "=============================================================="
echo "Probe 6: RSDKv5 logic-core link self-consistency"
echo "=============================================================="

# (1) Re-run Probe 5 so we link FRESH .o (no stale-object risk -- the
#     jo-pool-stale-core-o lesson). build_core_target.sh exits non-zero unless
#     all 16 TUs produced a valid SH-2 .o, leaving them in $OBJ.
echo "[1/3] Compiling logic core (delegating to build_core_target.sh) ..."
if ! bash "$DIR/build_core_target.sh" >/dev/null 2>&1; then
  echo "RESULT: RED -- core did not codegen (Probe 5 failed); cannot link-probe."
  exit 1
fi
nobj=$(ls "$OBJ"/*.o 2>/dev/null | wc -l | tr -d ' ')
echo "        $nobj core objects ready."

# (2) Link with NO libs, NO backend; report (not error-stop on) every unresolved
#     symbol. A dummy entry (-e 0) keeps ld from inventing an _start reference.
echo "[2/3] Linking core-only (no libc, no libgcc, no device backend) ..."
rm -f "$LINKOUT" "$ERRLOG"
"$CC" -nostdlib -Wl,--unresolved-symbols=report-all -Wl,-e,0 \
      -o "$LINKOUT" "$OBJ"/*.o 2>"$ERRLOG" || true

# Harvest undefined symbol names from ld diagnostics. The message is:
#   <obj>:(.text+0xNN): undefined reference to `NAME'
# The leading '.*undefined reference to .' eats through the opening backtick
# (matched as '.', so no literal backtick needs quoting in this script); the
# trailing 's/.$//' drops the closing quote. sort -u dedups multi-TU refs.
UNDEF=$(grep "undefined reference to" "$ERRLOG" \
        | sed 's/.*undefined reference to .//' \
        | sed 's/.$//' \
        | sort -u)

if [ "${SELFTEST:-0}" = "1" ]; then
  UNDEF="$UNDEF
RSDK::__synthetic_orphan_probe()"
  echo "        [SELFTEST] injected 1 synthetic orphan symbol."
fi

# (3) Classify every undefined symbol. ORPHAN = matches none of the 4 classes.
classify() {
  case "$1" in
    # --- TOOLCHAIN: libgcc helpers + C++ ABI new/delete -------------------
    __*) echo TOOLCHAIN; return;;
    "operator delete"*|"operator new"*) echo TOOLCHAIN; return;;
    # --- LIBC: newlib libc + libm ----------------------------------------
    memcpy|memset|memcmp|memmove|memchr|\
    strlen|strcmp|strncmp|strcpy|strncpy|strcat|strncat|strstr|strchr|strrchr|\
    malloc|calloc|realloc|free|\
    abs|labs|atoi|atol|atof|rand|srand|qsort|bsearch|time|\
    printf|fprintf|sprintf|snprintf|vsprintf|vsnprintf|vprintf|puts|putchar|\
    fopen|fclose|fread|fwrite|fseek|ftell|fflush|feof|ferror|fgets|fputs|fputc|\
    sin|sinf|cos|cosf|tan|tanf|asin|asinf|acos|acosf|atan|atanf|atan2|atan2f|\
    pow|powf|exp|expf|log|logf|sqrt|sqrtf|fabs|fabsf|floor|floorf|ceil|ceilf|fmod)
      echo LIBC; return;;
    # --- MINIZ: bundled decompressor (dependencies/all) ------------------
    mz_*|tinfl_*|tdefl_*) echo MINIZ; return;;
    # --- BACKEND: the Saturn device-backend contract (excluded TUs) -------
    #     This enumerated set IS the spike deliverable. Each entry is a symbol
    #     a core TU ODR-uses that is defined in a TU excluded by design.
    RSDK::Draw*) echo BACKEND; return;;                          # Drawing.cpp (sw raster)
    RSDK::RenderDeviceBase::*) echo BACKEND; return;;            # RenderDevice backend
    RSDK::AudioDevice::*) echo BACKEND; return;;                 # Audio.cpp (SCSP)
    RSDK::SKU::*) echo BACKEND; return;;                         # User/* services
    RSDK::*FunctionTable) echo BACKEND; return;;                 # Link.cpp API/RSDK tables
    RSDK::SetupFunctionTables*) echo BACKEND; return;;           # Link.cpp
    RSDK::ProcessVideo*) echo BACKEND; return;;                  # Video.cpp (theora)
    RSDK::ClearStageSfx*|RSDK::LoadSfx*) echo BACKEND; return;;  # Audio.cpp
    RSDK::*SettingsINI*) echo BACKEND; return;;                  # settings/RenderDevice
    RSDK::UpdateGameWindow*) echo BACKEND; return;;              # RenderDevice
    # render/scene globals all defined in Graphics/Drawing.cpp (verified):
    RSDK::cameras|RSDK::cameraCount|RSDK::currentScreen|RSDK::screens|\
    RSDK::drawGroups|RSDK::drawGroupNames|RSDK::gfxSurface|\
    RSDK::videoSettings|RSDK::changedVideoSettings|RSDK::channels|\
    RSDK::customSettings|RSDK::gameVerInfo|RSDK::shaderList)
      echo BACKEND; return;;
  esac
  echo ORPHAN
}

nB=0; nT=0; nL=0; nM=0; nO=0; orphans=""; backend_contract=""
while IFS= read -r sym; do
  [ -z "$sym" ] && continue
  case "$(classify "$sym")" in
    BACKEND)   nB=$((nB+1)); backend_contract="$backend_contract$sym
";;
    TOOLCHAIN) nT=$((nT+1));;
    LIBC)      nL=$((nL+1));;
    MINIZ)     nM=$((nM+1));;
    ORPHAN)    nO=$((nO+1)); orphans="$orphans  $sym
";;
  esac
done <<< "$UNDEF"

total=$((nB+nT+nL+nM+nO))
echo "[3/3] Classified $total undefined symbols:"
echo "--------------------------------------------------------------"
printf "  BACKEND   (Saturn device-backend contract) : %3d\n" "$nB"
printf "  TOOLCHAIN (libgcc + C++ ABI)               : %3d\n" "$nT"
printf "  LIBC      (newlib libc/libm)               : %3d\n" "$nL"
printf "  MINIZ     (bundled decompressor)           : %3d\n" "$nM"
printf "  ORPHAN    (must be 0)                       : %3d\n" "$nO"
echo "--------------------------------------------------------------"
echo "Saturn device-backend API contract (BACKEND symbols the port must implement):"
printf "%s" "$backend_contract" | sed 's/^/    /'
echo "--------------------------------------------------------------"
if [ "$nO" -eq 0 ]; then
  echo "RESULT: GREEN -- every undefined symbol is a known external."
  echo "        The logic core is link-self-consistent: it references only the"
  echo "        device backend + toolchain runtime + libc, never missing code."
  exit 0
else
  echo "RESULT: RED -- $nO orphan symbol(s) (referenced by core, defined nowhere):"
  printf "%s" "$orphans"
  exit 1
fi
