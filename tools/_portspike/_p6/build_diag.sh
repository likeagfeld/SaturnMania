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
# P6.8 Step F.1: diag-only injected trigger. Default = the F.2 GHZ2 act-advance
# (P6_XTEST). The three diag variants are MUTUALLY EXCLUSIVE (pick exactly one):
#   P6_WARP_BRIDGE=1 -- #181: pin the player onto the first GHZ1 bridge (planks).
#   P6_WARP=1        -- F.2-followup: warp past the GHZ1 signpost (act-clear chain).
#   (neither)        -- F.2 GHZ2 act-advance inject (default).
# Pass via `docker run -e P6_WARP_BRIDGE=1 ...`. ${VAR:-} keeps set -u happy.
if [ -n "${P6_WARP_BRIDGE:-}" ]; then
  P6_WARP_BRIDGE=1 bash /work/tools/_portspike/_p6/build_p6scene_objs.sh > /dev/null
elif [ -n "${P6_WARP:-}" ]; then
  P6_WARP=1 bash /work/tools/_portspike/_p6/build_p6scene_objs.sh > /dev/null
else
  P6_XTEST=1 bash /work/tools/_portspike/_p6/build_p6scene_objs.sh > /dev/null
fi

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
# W12b post-mortem (Task #227, 2026-06-11): the jo-side P6 TUs compile
# under make with NO dependency on CCFLAGS -- a define/flag change (the
# P6BISECT cycle) reused a stale p6_vdp1.o and produced HYBRID images
# that falsified the whole bisect chain. rm them unconditionally.
rm -f src/main.o jo-engine/jo_engine/core.o game.elf game.map \
      tools/_portspike/_p6/p6_vdp1.o tools/_portspike/_p6/p6_snd.o
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
# Player-wave fix (Task #227, 2026-06-12): the overlay objects MUST precede
# the -R import. game.elf now EXPORTS Ring_* (p6_closure_edge.c stubs for the
# pack-side Player references); with -R first, the overlay's own
# Ring_Draw_Normal/State fns bound to the PACK STUB addresses (MEASURED:
# ovl_ring.map Ring_Draw_Normal=0x060192fc = stub; p6_w_edge_calls=3,448,
# edge_last=28 -> Ring draws became stub calls, qa_p6_obj O2 draws=0).
# Objects-first makes local definitions win; -R then fills only what is
# still undefined (the intended import direction).
# O1 (Task #254): GHZ multi-class overlay (matches build_shipping.sh + the updated
# ovl_ring.ld, which now expects p6_ovl_ghz.o first at the window base). Spring
# moved out of the pack, so it MUST link here too or diag loses Spring.
$LD -b elf32-sh -T ovl_ring.ld -Map ovl_ring.map \
    p6_ovl_ghz.o p6_ring2.o Game_Spring.o Game_Bridge.o Game_PlaneSwitch.o Game_SpikeLog.o \
    -b coff-sh -R /work/game.elf -o ovl_ring.elf
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
