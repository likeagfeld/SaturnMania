/* title_sonic.h — public API for the 49-frame TitleSonic animation.
 *
 * Build prerequisite: cd/TSONIC.ATL produced by
 * tools/build_titlesonic_atlas.py.  Format details in that script.
 *
 * Memory cost: ~373 KB in VDP1 VRAM (one-time at load); peak ~8 KB
 * jo_malloc during the boot-time atlas streaming (freed at end of
 * load).  Zero per-frame CD I/O after load.
 */
#ifndef SRC_TITLE_SONIC_H
#define SRC_TITLE_SONIC_H

/* Boot-time load.  Streams the atlas from cd/TSONIC.ATL via raw GFS
 * (not jo_fs_read_file) so the full 373 KB never lives in jo_malloc.
 * Returns 1 on success, 0 on any failure (file missing, malformed,
 * over budget). */
int title_sonic_load(void);

/* Mark atlas as releasable.  Currently a no-op; the VDP1 sprite slots
 * remain registered for the life of the title state. */
void title_sonic_unload(void);

/* Tick-driven frame lookup.  Implements the RSDK loopIndex=48 behavior:
 * after the cumulative-duration sum is exhausted, returns the final
 * (settled) frame indefinitely.  Returns -1 if atlas not loaded. */
int title_sonic_current_frame(unsigned int ticks);

/* Draw the current frame at the TitleSonic entity position (world
 * coords (252, 104) per Mania decomp), applying the per-frame pivot
 * from the .bin so each frame lands on the right pose-anchor pixel.
 * `z` is the same jo Z-index convention used elsewhere in main.c. */
void title_sonic_draw(unsigned int ticks, int z);

/* Diagnostics (never required by main.c, useful in qa scripts). */
int          title_sonic_frame_count(void);
int          title_sonic_total_ticks(void);
int          title_sonic_first_sprite_id(void);
unsigned int title_sonic_atlas_vram_bytes(void);

#endif /* !SRC_TITLE_SONIC_H */
