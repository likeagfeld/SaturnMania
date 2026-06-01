#ifndef RSDK_SCENE_GHZ_H
#define RSDK_SCENE_GHZ_H

/* Phase 2.1 — engine-layer Green Hill Zone bring-up.
 *
 * This is the Saturn-hardware-facing bring-up of the GHZ Act 1 visible
 * playfield: VDP2 NBG1 cell-mode streaming foreground + VDP2 NBG2 sky
 * parallax. It is intentionally NOT part of the Mania-side game-logic
 * port (`src/mania/Objects/GHZ/GHZSetup.c`) — that file mirrors the
 * decomp's per-entity logic; THIS module is the engine-compat layer
 * that translates the decomp's TileLayer + draw concepts to Saturn-
 * native VDP2 cell-mode + slDMAXCopy.
 *
 * Authoritative sources (per CLAUDE.md §4.0 step 1):
 *   - ST-058-R2-060194.pdf (VDP2 NBG cell-mode + cycle pattern + line
 *     scroll table format)
 *   - ST-097-R5-072694.pdf (SCU DMA cache-through-alias rules)
 *   - ST-238-R1-051795.pdf (SGL slMapNbg1/slCharNbg1/slPlaneNbg1/
 *     slScrPosNbg1/2/slScrAutoDisp)
 *   - src/_archived/main.c.v01-handrolled:200-368, 240-269, 1937 —
 *     the working FG-stream + sky setup we port from
 *
 * Memory budget:
 *   - g_ghz_page[FG_PAGE_LEN] = 4096 * u16 = 8 KB Work RAM
 *   - palette structs + pointers + scalars ~64 B
 *   - sky palette struct = 16 B
 *   total: ~8.1 KB new BSS post-2.1 (still 68 KB margin under SGL work
 *   area boundary 0x060C0000). */

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* === FG streaming page geometry (mirrors archived design) ============ */

#define GHZ_FG_PAGE_CELLS     64                       /* cells per page row/col */
#define GHZ_FG_PAGE_LEN       (GHZ_FG_PAGE_CELLS * GHZ_FG_PAGE_CELLS)
#define GHZ_FG_WIN_TILES      32                       /* 32 tiles = 64 cells = 1 page */

/* === Camera Y constants (archived main.c.v01-handrolled:211) ========= */

/* GHZ Act 1's opening grass sits ~world tile-row 45 (720 px). Phase 2.1
 * pins the camera vertically at this Y so the visible page shows
 * grass-line terrain; Phase 2.2 (Player) replaces this with per-frame
 * vertical follow. */
#define GHZ_CAM_Y_PX_DEFAULT  720

/* === Public API ====================================================== */

/* Initialise / load the GHZ Act 1 foreground (NBG1 cell-mode).
 *
 * Side effects:
 *   - Loads `cd/GHZ<act>FG.CEL/PAT/TMP/PAL` via jo_fs_read_file.
 *   - Allocates the 8 KB Work-RAM `g_ghz_page[]` buffer (BSS, not heap).
 *   - Calls `jo_vdp2_set_nbg1_8bits_image` to configure cell-mode + alloc
 *     cell bank A0 / page bank B0 in VDP2 VRAM.
 *   - Sets `slPriorityNbg1(6)` so NBG1 sits between sprite priority and
 *     NBG2 sky.
 *   - DMAs the resident cell bank into VRAM via slDMACopy (one-shot;
 *     not the per-frame stream).
 *   - Resets g_ghz_cam_x to 0, g_ghz_cam_y to GHZ_CAM_Y_PX_DEFAULT.
 *
 * Returns true on success, false if any asset load failed (caller may
 * then leave NBG1 hidden). */
bool ghz_setup_foreground(int act);

/* Initialise / load the GHZ Act 1 sky (NBG2 cell-mode bitmap).
 *
 * Performs the archived "drop NBG2ON → rewrite cell+map → re-enable"
 * sequence per src/_archived/main.c.v01-handrolled:329-368 (BIPLANE
 * sample reference: NOV96_DTS/EXAMPLES/SGL/BIPLANE/MAIN.C:191-206).
 *
 * - Loads `cd/GHZ<act>SKY.DAT/PAL`.
 * - Drops NBG2ON via slScrAutoDisp(NBG1ON).
 * - Calls jo_vdp2_set_nbg2_8bits_image.
 * - Re-issues plane config (slPlaneNbg2 / slCharNbg2).
 * - Sets slPriorityNbg2(1) so sky sits behind NBG1 ground.
 * - Re-enables NBG1 + NBG2 atomically.
 *
 * Returns true on success. */
bool ghz_setup_sky(int act);

/* Per-frame Work-RAM page rebuild + V-blank DMA upload to VRAM.
 *
 * Build the current camera window's 64x64 page in g_ghz_page[], then
 * DMA it to nbg1_map via slDMAXCopy (SCU DMA cache-through alias —
 * required per memory/sgl-audio-vs-scroll-cpu-dma-conflict.md so we
 * don't share the CPU DMA controller with SGL's sound subsystem).
 *
 * Register via `jo_core_add_vblank_callback(ghz_fg_vblank)` AFTER
 * `ghz_setup_foreground` completes.
 *
 * Phase 2.1: g_ghz_cam_x is fixed at 0, g_ghz_cam_y at the default. A
 * tiny auto-scroll demo can drive g_ghz_cam_x to prove the streaming
 * works without a Player; toggle GHZ_AUTO_SCROLL below. Phase 2.2
 * replaces this with Player-driven camera follow.
 *
 * fg_vblank pushes the just-built page to VRAM. fg_build_page (called
 * from the main thread before slSynch) rebuilds it. The two functions
 * are deliberately separated to keep V-blank work bounded — building
 * the page during V-blank would overflow the V-blank budget. */
void ghz_fg_build_page(void);
void ghz_fg_vblank(void);

/* Phase 2.3j (2026-05-28) — `ghz_is_active()` REMOVED. Synchronous
 * scene-load means callers know GHZ is active by virtue of the title
 * state machine having advanced past TS_TRANSITION_TO_GHZ. Use
 * `s_ts_state == TS_GHZ_ACTIVE` (Game.c) or `mania_is_ghz_active()`
 * (mania/Game.h) instead. */

/* Phase 2.3j — synchronous-load tick counter (gate-evidence symbol).
 * Incremented once per `mania_ghz_tick_and_draw` call. Used by
 * tools/qa_phase2_3j_sync_load_gate.py P3 predicate. NOT in the gating
 * path — pure measurement. */
extern volatile unsigned int g_ghz_active_tick_counter;

/* Phase 2.3k-mid (2026-05-28) — sub-asset load-failure bitmask.
 * Bits: 0=FG.TMP, 1=FG.PAL, 2=FG.CEL, 3=FG.PAT, 4=SKY.PAL, 5=SKY.DAT.
 * Stays 0 only on a fully successful GHZ load. Peek via savestate at
 * BSS address from game.map. See scene_ghz.c for bit assignments. */
extern volatile unsigned int g_ghz_load_error_code;

/* Camera accessors. Phase 2.1: read-only at the page-rebuild level
 * (camera is internally fixed); Phase 2.2 exposes setters. */
int ghz_camera_x(void);
int ghz_camera_y(void);

/* Phase 2.2 — Player-driven camera. Game.c calls this once per frame
 * after Player_Tick. The next ghz_fg_build_page() builds the page for
 * the new (cam_x, cam_y), and the next ghz_fg_vblank() pushes it +
 * updates the NBG1 scroll position. Both x and y are integer pixels
 * (NOT Q16.16); caller passes JO_FIXED_TO_INT(player.xpos) etc.
 *
 * Coordinates clamped to [0, world_w - SCR_W] x [0, world_h - SCR_H]
 * inside this function so the caller doesn't have to. */
void ghz_set_camera(int cam_x_px, int cam_y_px);

/* World size in pixels (level width * 16, level height * 16). Phase
 * 2.2's Player + camera math needs this for clamping. Defaults to 0
 * pre-load; returns nonzero only after ghz_setup_foreground succeeds. */
int  ghz_world_width_px(void);
int  ghz_world_height_px(void);

/* Phase 2.1 sky parallax — apply a static horizontal offset to NBG2.
 * Phase 2.2: drives the 1/4-speed sky scroll for the current camera
 * via slScrPosNbg2. A full line-scroll table for vertical depth-of-
 * field is documented as a Phase 2.3 cosmetic follow-up. */
void ghz_sky_scroll(int x_world, int y_world);

/* Phase 2.3b — diagnostic SPCTL snapshot accessors. Capture VDP2 SPCTL
 * (0x25F800E0) at three points across the GHZ bring-up to bisect the
 * GHZ-state sprite-invisibility blocker per docs/COMPREHENSIVE_PLAN.md
 * §12.3b. Cite: ST-058-R2 §SPCTL register; jo/sega_saturn.h:357. */
unsigned short ghz_diag_spctl_pre_nbg1(void);
unsigned short ghz_diag_spctl_post_nbg1(void);
unsigned short ghz_diag_spctl_post_sky(void);

#ifdef __cplusplus
}
#endif

#endif /* RSDK_SCENE_GHZ_H */
