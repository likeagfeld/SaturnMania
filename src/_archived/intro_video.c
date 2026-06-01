/* intro_video.c — Mania.ogv intro playback.
 *
 * Current status (2026-05-26): EMPTY STUB. Cinepak integration deferred
 * pending data-driven research on the additional pre-init that CPK_Init
 * requires beyond what jo_video_init provides.
 *
 * Path-A investigation (data-driven from authoritative DTS96 SGL Cinepak
 * samples + docs/cinepak_integration_spec.md):
 *   - jo's video module (jo-engine/jo_engine/video.c) DOES call CPK_Init
 *     and register CPK_VblIn V-blank callback in jo_video_init().
 *   - Setting JO_COMPILE_WITH_VIDEO_MODULE=1 in the Makefile enables
 *     this auto-init path.
 *   - BUT verified 2026-05-26: with the flag set, Saturn locks to black
 *     at boot. The cinepak_integration_spec.md §4 note about adding
 *     `jo_add_memory_zone(LWRAM, 0x40000)` for Cinepak's ~520 KB
 *     transient allocations suggests heap exhaustion is one factor.
 *   - LIBCPK.A is still linked here as the bisect-1 baseline (link OK,
 *     no CPK_* refs = no runtime touch); this matches the build before
 *     this attempt and keeps all 9 verify_done gates green.
 *
 * Next steps when resumed:
 *   1. Add LWRAM malloc zone via jo_add_memory_zone before jo_video_init
 *   2. Verify Saturn boot + title state still work with VIDEO_MODULE=1
 *   3. Then transcode Mania.ogv -> INTRO.CPK via tools/ogv_to_cpk.py
 *   4. Call jo_video_open_file("INTRO.CPK") + jo_video_play from
 *      intro_video_play() with slSynch outer loop. */

#include <jo/jo.h>

/* LIBCPK.A's audio path references SBL sound symbols that jo doesn't
 * provide when JO_COMPILE_WITH_VIDEO_MODULE=0. The stub linker-resolves
 * the references so a clean ELF can still be produced while we keep the
 * library in the link line for the bisect baseline. */
void SND_Init(void)        {}
void SND_ChangeMap(void)   {}
void SND_StartSeq(void)    {}
void SND_StopSeq(void)     {}
void SND_StartPcm(void)    {}
void SND_StopPcm(void)     {}
void SND_Status(void)      {}
void SND_VolMap(void)      {}
void SND_PauseSeq(void)    {}

int intro_video_play(const char *cpk_filename)
{
    (void)cpk_filename;
    return 0;
}
