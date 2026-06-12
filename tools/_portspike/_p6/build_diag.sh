#!/usr/bin/env bash
# =============================================================================
# build_diag.sh -- the CANONICAL P6 diag-image build sequence (P6.7d.2+).
# Run INSIDE the Docker toolchain image:
#   MSYS_NO_PATHCONV=1 docker run --rm -v "D:/sonicmaniasaturn":/work -w /work \
#       joengine-saturn:latest bash tools/_portspike/_p6/build_diag.sh
#
# Encodes two binding build rules:
#   1. The flavor-switch trap (p6-5b3 memory): rm the shipping-flavored
#      src/main.o + game.elf/map so `make P6SCENE=1` cannot silently reuse
#      them (hybrid image, proof never runs).
#   2. The SGL work-area override (P6.7d.2): the ENGINE-SIZED parameter block
#      platform/Saturn/SaturnSGLArea.c replaces the stock SGLAREA.O via the
#      make command-line SYSOBJS definition (overrides the `+=` at
#      jo_engine_makefile:224 WITHOUT modifying the do-not-touch COMMON tree).
#      The SHIPPING build (verify_done / plain `make`) keeps the STOCK area --
#      the hand-port needs the 3D sortlist capacity.
# =============================================================================
set -eu

CC=/work/jo-engine/Compiler/LINUX/bin/sh-none-elf-gcc-8.2.0

echo "[1/4] proof pack (engine TUs, ld -r gc-pack) ..."
bash /work/tools/_portspike/_p6/build_p6scene_objs.sh > /dev/null

echo "[2/4] engine-sized SGL area block ..."
$CC -x c -std=gnu99 -m2 -O2 -fno-builtin \
    -I/work/jo-engine/Compiler/COMMON/SGL_302j/INC \
    -I/work/jo-engine/Compiler/WINDOWS/sh-elf/include \
    -c -o /work/platform/Saturn/SaturnSGLArea.o \
    /work/platform/Saturn/SaturnSGLArea.c

echo "[3/5] jo image (P6SCENE flavor, flavor-switch rm, SYSOBJS override) ..."
cd /work
# W11b: jo's malloc pool is flavor-dependent (P6SCENE = 8 KB, shipping =
# 256 KB; Makefile) and is a static array in jo_engine/core.c -- rm the
# object so a flavor switch can never reuse the wrong pool size (the
# jo-pool-stale-core-o-gotcha memory rule).
rm -f src/main.o jo-engine/jo_engine/core.o game.elf game.map
make P6SCENE=1 SYSOBJS=platform/Saturn/SaturnSGLArea.o

echo "[3b/5] Ring OVERLAY (P6.7d.3): fixed-base link vs game.elf -> cd/OVLRING.BIN ..."
LD=/work/jo-engine/Compiler/LINUX/sh-none-elf/bin/ld
OBJCOPY=/work/jo-engine/Compiler/LINUX/bin/sh-none-elf-objcopy
P6=/work/tools/_portspike/_p6
cd "$P6"   # ovl_ring.ld names input objects by basename
# game.elf is COFF-SH (sgl.linker OUTPUT_FORMAT) while the overlay objects
# are ELF: disambiguate per-input with -b (MEASURED: bare -R errors "file
# format is ambiguous: coff-sh coff-sh-small").
# BASENAMES REQUIRED (measured): ovl_ring.ld's `p6_ovl_ring.o(.text*)`
# pattern only FILTERS when it matches a command-line input by the same
# spelling; with absolute paths it becomes an extra INPUT statement and the
# object loads twice (multiple definition of p6_overlay_entry).
$LD -b coff-sh -R /work/game.elf -b elf32-sh \
    -T ovl_ring.ld -Map ovl_ring.map \
    p6_ovl_ring.o p6_ring2.o -o ovl_ring.elf
$OBJCOPY -O binary "$P6/ovl_ring.elf" /work/cd/OVLRING.BIN
ls -l /work/cd/OVLRING.BIN
cd /work

echo "[4/5] re-master the ISO with the overlay on disc ..."
rm -f game.iso
make P6SCENE=1 SYSOBJS=platform/Saturn/SaturnSGLArea.o

echo "[5/5] sanity: _end + engine-area symbols + overlay entry address ..."
grep " _end = " game.map
grep -m1 " MaxPolygons" game.map
grep -m1 "p6_overlay_entry" "$P6/ovl_ring.map" || true
echo "DONE [diag image built: game.iso/game.cue + cd/OVLRING.BIN]."
