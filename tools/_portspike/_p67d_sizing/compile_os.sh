#!/usr/bin/env bash
# P6.7d: -Os recompile of the full verbatim object set (code-density measure)
# in the SHIPPING configuration: -DGAME_VERSION=3 (VER_103 -> MANIA_USE_PLUS=0,
# matching the 1.03 Data.rsdk) WITHOUT the MANIA_PREPLUS knob (which would also
# force RETRO_REVISION=1 against our REV02 engine -- the W9 coupling; the
# direct GAME_VERSION define keeps REVISION=2). MEASURED 2026-06-11: Game.h's
# default is VER_106 (Plus), so the -O2 census numbers are the Plus build --
# valid only as conservative upper bounds.
set -u
CC=/work/jo-engine/Compiler/LINUX/bin/sh-none-elf-gcc-8.2.0
INC=/work/tools/_portspike/_p67d_sizing/include
OUT=/work/tools/_portspike/_p67d_sizing/obj_os
mkdir -p "$OUT"
pass=0; fail=0
for c in /work/tools/_decomp_raw/SonicMania_Objects_*.c; do
  b=$(basename "$c" .c)
  if $CC -x c -std=gnu11 -m2 -Os -fno-builtin -ffunction-sections -fdata-sections \
       -DRETRO_REVISION=2 -DGAME_VERSION=3 \
       -I"$INC" -I/work/jo-engine/Compiler/WINDOWS/sh-elf/include \
       -c -o "$OUT/$b.o" "$c" 2>/dev/null; then
    pass=$((pass+1))
  else
    fail=$((fail+1))
  fi
done
echo "OS-CENSUS compiled=$pass failed=$fail"
