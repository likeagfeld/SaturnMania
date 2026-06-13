#!/usr/bin/env bash
# =============================================================================
# build_shipping.sh -- P6.8 Step B (Task #211): build the LEAN ENGINE SHIPPING
# image. Identical to build_diag.sh EXCEPT the jo make knob is
# P6_ENGINE_SHIPPING=1 (not P6SCENE=1): main.c boots p6_engine_boot_and_run()
# straight into a continuously-running GHZ -- no diagnostic burst/proofs/Title
# reload/legacy Ring. The engine pack (p6_scene_pack.o) is the SAME object as
# the diag flavor; build_p6scene_objs.sh now -u-roots p6_engine_boot_and_run so
# the lean entry survives the pack gc. The Ring OVERLAY (OVLRING.BIN) is still
# loaded + registered by the shared masked load core (p6_scene_run step 1.5), so
# the overlay link stage is kept.
#
# Run INSIDE the Docker toolchain image:
#   MSYS_NO_PATHCONV=1 docker run --rm -v "D:/sonicmaniasaturn":/work -w /work \
#       joengine-saturn:latest bash tools/_portspike/_p6/build_shipping.sh
#
# The do-not-touch COMMON SGL tree is untouched; the engine-sized SGL work area
# (SaturnSGLArea.c) replaces stock SGLAREA.O via the make SYSOBJS override, same
# as the diag (the engine pack does not use the 3D sortlist capacity the stock
# area sizes for). The flavor-switch rm (p6-5b3 memory) clears the
# shipping-flavored intermediates so a knob switch cannot reuse a hybrid image.
# =============================================================================
set -eu

CC=/work/jo-engine/Compiler/LINUX/bin/sh-none-elf-gcc-8.2.0

echo "[1/5] proof pack (engine TUs, ld -r gc-pack; -u p6_engine_boot_and_run root) ..."
bash /work/tools/_portspike/_p6/build_p6scene_objs.sh > /dev/null

echo "[2/5] engine-sized SGL area block ..."
$CC -x c -std=gnu99 -m2 -O2 -fno-builtin \
    -I/work/jo-engine/Compiler/COMMON/SGL_302j/INC \
    -I/work/jo-engine/Compiler/WINDOWS/sh-elf/include \
    -c -o /work/platform/Saturn/SaturnSGLArea.o \
    /work/platform/Saturn/SaturnSGLArea.c

echo "[3/5] jo image (P6_ENGINE_SHIPPING flavor, flavor-switch rm, SYSOBJS override) ..."
cd /work
# jo's malloc pool is flavor-dependent (engine flavor = 32 KB, hand-port =
# 256 KB) and the jo-side P6 TUs compile under make with NO CCFLAGS dependency
# -- rm the flavor-sensitive intermediates so a knob switch can never reuse the
# wrong pool size / a stale p6_vdp1.o (jo-pool-stale-core-o-gotcha + the W12b
# hybrid-image rule).
rm -f src/main.o jo-engine/jo_engine/core.o game.elf game.map \
      tools/_portspike/_p6/p6_vdp1.o tools/_portspike/_p6/p6_snd.o
make P6_ENGINE_SHIPPING=1 SYSOBJS=platform/Saturn/SaturnSGLArea.o

echo "[3b/5] Ring OVERLAY (P6.7d.3): fixed-base link vs game.elf -> cd/OVLRING.BIN ..."
LD=/work/jo-engine/Compiler/LINUX/sh-none-elf/bin/ld
OBJCOPY=/work/jo-engine/Compiler/LINUX/bin/sh-none-elf-objcopy
P6=/work/tools/_portspike/_p6
cd "$P6"   # ovl_ring.ld names input objects by basename (the build_diag rule)
$LD -b elf32-sh -T ovl_ring.ld -Map ovl_ring.map \
    p6_ovl_ring.o p6_ring2.o \
    -b coff-sh -R /work/game.elf -o ovl_ring.elf
$OBJCOPY -O binary "$P6/ovl_ring.elf" /work/cd/OVLRING.BIN
ls -l /work/cd/OVLRING.BIN
cd /work

echo "[4/5] re-master the ISO with the overlay on disc ..."
rm -f game.iso
make P6_ENGINE_SHIPPING=1 SYSOBJS=platform/Saturn/SaturnSGLArea.o

echo "[5/5] sanity: _end + lean-boot entry + flavor flag + overlay entry ..."
grep " _end = " game.map
grep -m1 " _p6_engine_boot_and_run" game.map || true
grep -m1 " _p6_lean_boot" game.map || true
grep -m1 "p6_overlay_entry" "$P6/ovl_ring.map" || true
echo "DONE [shipping image built: game.iso/game.cue + cd/OVLRING.BIN]."
