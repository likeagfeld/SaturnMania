/*
 * intro_main.c -- Phase 1.21 B1 Cinepak intro binary (two-binary chain
 * Path B).
 *
 * STATUS: Phase 1.21 B1 (bounded). This file is the SECOND ELF in the
 * two-binary chain. It is built by `make -f Makefile.intro intro` and
 * the produced raw binary is written to cd/INTRO.BIN (NOT cd/0.bin).
 * cd/0.bin remains the GAME.BIN produced by the default `make` flow.
 *
 * Build profile (see Makefile.intro for the full flag set):
 *   JO_COMPILE_WITH_VIDEO_MODULE = 1   (turns on jo_video_init + LIBCPK/LIBSND
 *                                       linkage in jo_engine_makefile)
 *   JO_COMPILE_WITH_AUDIO_MODULE = 0   (mandatory; the audio module's
 *                                       slInitSound(4-byte fake map) +
 *                                       slCDDAOn(127,127,0,0) collide with
 *                                       Cinepak's PCM path -- see
 *                                       docs/cinepak_blackboot_solved.md
 *                                       sec.3 for the verified collision).
 *
 * What this binary does:
 *   1. jo_core_init() -- the engine init also runs jo_video_init() under the
 *      VIDEO_MODULE=1 profile, which calls CPK_Init() + registers CPK_VblIn
 *      into jo's vblank callback list (jo-engine/jo_engine/video.c
 *      lines 280-297).
 *   2. jo_add_memory_zone(LWRAM, 256 KB) -- adds 256 KB of Work-RAM-Low
 *      to the jo heap so Cinepak's ~520 KB transient allocations (PCM /
 *      RING / WORK / MOVIE / DECODE buffers per video.c:145-214) don't
 *      blow the default 384 KB pool. Mirrors jo's Samples/demo-video
 *      main.c:29-30,59 verbatim.
 *   3. jo_video_open_file("INTRO.CPK") -- opens the FILM container,
 *      preloads the header, configures the decode buffer.
 *   4. jo_video_play(JO_NULL) -- starts playback; the __jo_internal_cpk
 *      callback ticks CPK_Task every frame from the jo_core_run main
 *      loop. JO_NULL = no on-stop callback (B1 scope; in B2 this
 *      becomes the chain-trampoline trigger).
 *   5. jo_core_run() -- enters the engine main loop which drives the
 *      registered vblank callbacks (CPK_VblIn + __jo_internal_cpk) so
 *      playback completes naturally. After Cinepak returns we sit
 *      idle. The chain-load to GAME.BIN is Phase 1.22+ work.
 *
 * For B1 the user can hand-promote intro.bin to cd/0.bin (manual copy,
 * rebuild ISO) to verify the Cinepak plays at all BEFORE the chain
 * mechanism lands. After the hand-promote test, restore cd/0.bin =
 * cd/GAME.BIN so the repo's default state is unregressed.
 *
 * Hard rule: this file must NOT pull in any of the Mania-object sources
 * (Game.c, Title*.c, Player.c, etc). The intro binary is intentionally
 * minimal to keep its BSS footprint small and to avoid the audio module
 * conflict (Mania objects assume AUDIO=1).
 */

#include <jo/jo.h>

/* Work-RAM-Low expansion for Cinepak's transient buffer footprint.
 * Address + size copied verbatim from jo-engine/Samples/demo - video/
 * main.c:29-30. The 256 KB is enough headroom for the 520 KB peak
 * (the 384 KB jo pool absorbs the rest). */
#define INTRO_LWRAM_BASE   ((unsigned char *)0x00200000)
#define INTRO_LWRAM_SIZE   (0x00040000)   /* 256 KB */

/* Per-frame draw callback. Cinepak's per-vblank CPK_VblIn updates the
 * decode buffer + maintains the sprite handle returned by
 * jo_get_video_sprite(), but the sprite is NOT auto-drawn -- the caller
 * must explicitly issue a jo_sprite_draw3D every frame. This is the
 * pattern from jo-engine/Samples/demo - video/main.c:32-38: without it
 * Cinepak decodes silently and the screen stays black. The B1
 * hand-promote test surfaced this on first capture (all frames black
 * post-BIOS); fixed by adding this callback + jo_core_add_callback below.
 *
 * Coordinates (0, 0, 170): centred at viewport origin (0,0) with Z=170
 * (in front of the back-color plane). Identical placement to the jo
 * demo. */
static void intro_draw(void)
{
    jo_sprite_draw3D(jo_get_video_sprite(), 0, 0, 170);
}

void jo_main(void)
{
    /* Engine bring-up. With VIDEO_MODULE=1 this also calls jo_video_init()
     * which runs CPK_Init() + registers CPK_VblIn into the jo vblank list. */
    jo_core_init(JO_COLOR_Black);

    /* Add LWRAM (256 KB) to jo's malloc pool BEFORE jo_video_play allocates
     * the CPK transient buffers. */
    jo_add_memory_zone(INTRO_LWRAM_BASE, INTRO_LWRAM_SIZE);

    /* Open the FILM container. jo_video_open_file preloads the header
     * and configures the decode buffer (jo-engine/jo_engine/video.c
     * lines 179-220). Returns false on failure (missing file, bad
     * magic, width not multiple of 8, etc). For B1 we don't have a
     * graceful failure path -- if INTRO.CPK is broken the binary will
     * just idle on a black screen, which is acceptable for the
     * hand-promote test (Gate 11 in verify_done.ps1 already validates
     * the FILM container shape offline).
     *
     * Per jo-engine/Samples/demo - video/main.c:60, jo_video_play MUST
     * only be called when jo_video_open_file returns true; if open
     * fails (e.g. INTRO.CPK absent) calling play without an open file
     * dereferences uninitialised state. */
    if (jo_video_open_file("INTRO.CPK"))
    {
        /* Fire-and-forget playback. The internal __jo_internal_cpk
         * callback registered by jo_video_play drives CPK_Task from the
         * vblank chain during jo_core_run below. JO_NULL = no on-stopped
         * callback (B1 scope; in B2 this becomes the chain-trampoline
         * trigger). */
        jo_video_play(JO_NULL);
    }

    /* Per-frame draw: actually places the decoded Cinepak sprite onto
     * the framebuffer via jo_sprite_draw3D(jo_get_video_sprite(), ...).
     * Without this, Cinepak decodes silently and the screen stays
     * black. See jo-engine/Samples/demo - video/main.c:62. */
    jo_core_add_callback(intro_draw);

    /* Phase 1.21 B1: no chain. After Cinepak returns, sit idle until
     * Phase 1.22 lands the GFS-based chain-load trampoline. The
     * jo_core_run main loop services the registered vblank callbacks
     * (CPK_VblIn + __jo_internal_cpk) so playback completes naturally. */
    jo_core_run();
}
