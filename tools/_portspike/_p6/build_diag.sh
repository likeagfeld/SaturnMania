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

echo "[3/4] jo image (P6SCENE flavor, flavor-switch rm, SYSOBJS override) ..."
cd /work
rm -f src/main.o game.elf game.map
make P6SCENE=1 SYSOBJS=platform/Saturn/SaturnSGLArea.o

echo "[4/4] sanity: _end + engine-area symbols ..."
grep " _end = " game.map
grep -m1 " MaxPolygons" game.map
echo "DONE [diag image built: game.iso/game.cue]."
