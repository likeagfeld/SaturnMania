/*
 * main.c — Phase 1.3 foundation-aligned boot stub.
 *
 * Phase 0.5 archived the 1965-line hand-rolled main.c that mixed game-logic
 * + GHZ rendering + title timeline. Phase 1.x is a minimal boot stub that:
 *   1. Sets back-color + zeros NBG priorities so no VRAM-garbage shows
 *      before the title paints.
 *   2. Initialises the engine-compat layer (rsdk_* modules + mania_*).
 *   3. Loads cd/TITLE.DAT + cd/TITLE.PAL into NBG2 as the title backdrop
 *      (no Mania-side per-class .c file owns the NBG2 backdrop — it's a
 *      pre-composed bitmap, not a TileLayer; the upstream multi-layer
 *      parallax + scanline FX are Phase 2 work).
 *   4. Queues the "Title" scene which spawns + ticks the Title classes.
 *   5. Hands the frame to jo_core_run with the mania_tick callback.
 */

#include <jo/jo.h>

#include "rsdk/storage.h"
#include "rsdk/object.h"
#include "rsdk/animation.h"
#include "rsdk/drawing.h"
#include "rsdk/input.h"
#include "rsdk/audio.h"
#include "rsdk/save.h"
#include "rsdk/scene.h"
#include "rsdk/palette.h"
#include "rsdk/scene_ghz.h"
#include "mania/Game.h"
/* Phase 3.2 + 3.2b REVERTED 2026-05-28 — MenuSetup integration produced
 * black-screen + artifacts with no visible menu text. MenuSetup files left
 * on disk for future re-attempt. */

/* Phase 1.20 — title-side palette CRAM-drain support.
 *
 * The title backdrop NBG2 uses g_title_pal as its 256-color palette, allocated
 * via jo_create_palette_from in setup_title_bg. The decomp's
 * TitleBG_StaticUpdate rotates indices 140-143 of palette bank 0 every 6
 * ticks (water-shimmer band). rsdk_rotate_palette updates the RAM mirror +
 * marks the range dirty; we drain those dirties to CRAM here.
 *
 * CRAM layout for 8-bit cell mode: index `i` in palette `id` lives at
 *   CRAM[id * 256 + i]    (per jo_vdp2_set_nbg2_8bits_image's __jo_create_map
 *                          paloff = palette_id << 12 nibble-offset; 256-color
 *                          banks consume 256 short-entries each).
 *
 * The drain is invoked from mania_tick (synchronous, post-Update) rather
 * than from fg_vblank — fg_vblank empirically doesn't fire as registered
 * (per Phase 1.19 finding documented in main.c:141-156). Short stores to
 * CRAM are safe at any time on Saturn (only DMA needs vblank gating per
 * ST-058-R2 §5 CRAM access). 4 entries per drain is far below any DMA
 * threshold so direct CPU writes are appropriate. */
static int s_title_pal_cram_base = -1;
static volatile uint32_t s_title_pal_drains = 0;
uint32_t main_title_pal_drains(void) { return s_title_pal_drains; }

/* Phase 1.3 — NBG2 title backdrop bring-up.
 *
 * cd/TITLE.DAT is a pre-composed 224x512 8-bit cell-mode bitmap (the
 * orientation matches jo_vdp2_set_nbg2_8bits_image's column-major write
 * + mirror cancellation, per legacy main.c.v01-handrolled:381-417).
 * cd/TITLE.PAL is a 512-byte BGR1555 palette consumed by jo_create_
 * palette_from.
 *
 * Saturn-citation policy: jo_vdp2_set_nbg2_8bits_image is the documented
 * jo wrapper around SGL's slBitMap / Cell-mode plane setup (see
 * jo-engine/jo_engine/vdp2.c). 8-bit cell-mode pinned scroll(0,0)
 * matches the on-disk bitmap convention from convert_vdp2_cells8.py. */

#define TITLE_BG_W  224
#define TITLE_BG_H  512

/* #187 fix (2026-06-01): stage cd/TITLE.DAT in the GHZ FG.TMP LWRAM region
 * (0x00210000, 320 KB, storage.c:211-221) instead of the jo malloc pool.
 *
 * Measured (during #186): the jo pool (256 KB) is down to ~8.9 KB free at
 * GHZ gameplay because the 114688-byte TITLE.DAT backdrop bitmap
 * (TITLE_BG_W * TITLE_BG_H = 224*512) stays resident in the pool. jo never
 * splits a reused block (malloc.c:127-131) and jo_free either pops the zone
 * high or marks the block free (malloc.c:189-192), so whether TITLE.DAT was
 * never freed OR freed-then-recycled-whole by a smaller later alloc, the
 * 114 KB is stranded either way -- starving HUD/entity loads.
 *
 * The FG.TMP LWRAM region is provably free during the title phase: GHZ
 * assets (GHZ1SURF/FG.TMP/SKY.DAT) load only at the title->GHZ transition,
 * AFTER setup_title_bg runs. jo_img_to_vdp2_cells copies the bitmap into
 * VDP2 VRAM during the one-shot bind, so the staging buffer is dead the
 * instant setup_title_bg returns -- it is never re-read and is reclaimed by
 * the GHZSetup FG.TMP load at the transition. Pool-free GFS read via
 * jo_fs_read_file_ptr (fs.c:282-283). Same LWRAM-bypass mechanism as
 * memory/ghz-sky-dat-lwram-bypass.md and the #186 HUD fix. */
#define TITLE_DAT_LWRAM_STAGE   ((void *)0x00210000)

static jo_palette g_title_pal;
static int s_title_bg_ready = 0;
static unsigned char  *s_title_bg_dat  = NULL;   /* #187: now points at the
                                                  * TITLE_DAT_LWRAM_STAGE region,
                                                  * NOT a jo-pool allocation.
                                                  * Kept only as a readiness
                                                  * marker; NEVER jo_free()'d
                                                  * (LWRAM has no pool header). */
static unsigned short *s_title_bg_pal_buf = NULL;

/* Phase 1.34b — NBG2 cloud parallax (Sub-fix B revived).
 *
 * Decomp source: tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:103-136
 *   TitleBG_Scanline_Clouds drives a per-scanline UV deform on the
 *   'Clouds' TileLayer (Scene1.bin layer 2, 256x256 source).  The full
 *   per-scanline path requires VDP2 H-IRQ raster tables -- Phase Z work.
 *   First cut here: uniform NBG2 scroll matching the decomp's
 *   `TitleBG->timer += 0x8000`/StaticUpdate cadence (=0.5 Q16 units/frame
 *   Y drift) with a slow horizontal drift on top to mirror the
 *   sine-driven X term.
 *
 * Asset: cd/CLOUDS.DAT + cd/CLOUDS.PAL built by tools/build_clouds_bg.py
 * (256x256 8-bpp cell-mode bitmap + 256-entry RGB555 BE palette).
 *
 * Citation chain:
 *   - tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:103-136
 *     (Scanline_Clouds reference)
 *   - ST-058-R2-060194.pdf §3.3 + §11 (NBG2 cell-mode + Color RAM)
 *   - ST-238-R1-051795.pdf §slScrPosNbg2 + §slPriorityNbg2
 *   - jo-engine/jo_engine/vdp2.c:739 (jo_vdp2_set_nbg2_8bits_image)
 *   - src/rsdk/scene_ghz.c:256-352 (GHZ sky NBG2 cell-mode reference --
 *     same documented-working 256-color cell-mode path Phase 2.1 ships).
 */
#define CLOUDS_BG_W  256
#define CLOUDS_BG_H  256

static jo_palette g_clouds_pal;
static int s_clouds_bg_ready = 0;
static unsigned char  *s_clouds_bg_dat     = NULL;
static unsigned short *s_clouds_bg_pal_buf = NULL;

/* Phase 1.35 — NBG1 decomp-island plane.
 *
 * Decomp source: tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:103-105
 *     TileLayer *islandLayer        = RSDK.GetTileLayer(3);
 *     islandLayer->drawGroup[0]     = 1;
 *     islandLayer->scanlineCallback = TitleBG_Scanline_Island;
 *
 * TitleBG_Scanline_Island (TitleBG.c:138-140) clips to screen Y >= 168:
 *     RSDK.SetClipBounds(0, 0, 168, ScreenInfo->size.x, SCREEN_YSIZE);
 * so the Island layer's TileLayer 3 source 1024x1024 only renders pixels
 * that land in screen Y=168..224 (the bottom band).
 *
 * Saturn first-cut approximation: static cell-mode NBG1 plane at
 * 512x256, top 96 rows transparent (mapped to off-screen above the
 * decomp clip line), bottom 160 rows hold the visible Sonic-head
 * island silhouette per the extracted decomp Title/Scene1.bin layer
 * (rendered at tools/qa_golden/title_layer_3_Island.png).  Rotation /
 * per-scanline UV deform (sine/cosine yaw orbit) is Phase Z work.
 *
 * Priority routing (per ST-058-R2 §3.4 Priority Number Register, p.225,
 * + Phase 1.34c REPLACE block at mania_title_3d_backdrop_draw):
 *     back-color (priority 0)
 *     NBG2 clouds  (priority 1) -- Phase 1.34b
 *     NBG1 island  (priority 2) -- Phase 1.35 (this layer)
 *     RBG0         (priority 0 = hidden, Phase 1.31 Fix #4)
 *     VDP1 sprites (priority 6) -- billboards + foreground composite on top
 *
 * Asset: cd/ISLAND.DAT + cd/ISLAND.PAL built by tools/build_island_bg.py
 * (512x256 8-bpp cell-mode bitmap + 256-entry RGB555 BE palette).
 *
 * Boot-delay budget audit (per CLAUDE.md §4.5.1 Audit 4):
 *   ISLAND.DAT = 128 KB, ISLAND.PAL = 0.5 KB.  128.5 KB / 150 KB/s
 *   = 0.86s + 1 GFS seek (~0.1s) = ~0.96s.  Total boot path now
 *   setup_title_bg + setup_clouds_bg + setup_island_bg ~= 3s.
 *
 * Citation chain:
 *   - tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:103-105 (auth)
 *   - tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:138-140 (clip)
 *   - ST-058-R2-060194.pdf §3.3 (NBG1 cell-mode) + §3.4 (priorities)
 *   - ST-238-R1-051795.pdf §slPriorityNbg1 + §slCharNbg1
 *   - jo-engine/jo_engine/vdp2.c:527-543 (jo_vdp2_set_nbg1_8bits_image)
 *   - tools/build_island_bg.py (asset builder; Phase 1.34b convention)
 *   - tools/view_island_joshifted.py (static-preview gate, verified
 *     GREEN before flash per tools/qa_golden/island_simulated.png).
 */
/* Phase 1.35c: shrunk from 512x256 (128 KB; filled VDP2 VRAM bank A0
 * and corrupted downstream SGL allocations) to 256x256 (64 KB; leaves
 * 64 KB A0 headroom). Diagnosed in Phase 1.35b via samples/
 * qa_phase1_35b_diag.mcs + jo-engine/jo_engine/vdp2_malloc.c:196-220
 * (JO_VDP2_RAM_CELL_NBG1 -> A0 routing). */
#define ISLAND_BG_W  256
#define ISLAND_BG_H  256

static jo_palette g_island_pal;
static int s_island_bg_ready = 0;
static unsigned char  *s_island_bg_dat     = NULL;
static unsigned short *s_island_bg_pal_buf = NULL;

/* Phase 1.17 — probe_init/probe_draw deleted. The Phase 1.15/1.16 bisect
 * probe served its purpose (proved jo_sprite_draw3D works post-BSS fix).
 * Title rendering now flows entity-first via rsdk_object_draw_all in
 * mania_tick. If a regression surfaces a sprite-pipeline question again,
 * resurrect a minimal probe from git history (§11.22). */

/* Phase 1.26b §11.33 — title backdrop migrated from NBG2 cell-mode to RBG0
 * rotation plane. Justification chain (full plan in §11.33):
 *   - User-mandated 2026-05-27: "rotating-island backdrop they've been
 *     asking for" — replace the static NBG2 cell-mode bitmap with a
 *     rotation-capable RBG0 plane so the title scene carries the
 *     canonical Mania camera-orbit motion.
 *   - jo wrapper API: jo_enable_background_3d_plane +
 *     jo_background_3d_plane_a_img + jo_background_3d_plane_a_draw,
 *     verified via jo-engine/Samples/demo - vdp2 plane/main.c:107-128
 *     (init_3d_planes pattern) and jo-engine/jo_engine/vdp2.c:330-360
 *     (jo_vdp2_enable_rbg0 = canonical BIPLANE init sequence).
 *   - DTS authority: NOV96_DTS/EXAMPLES/SGL/BIPLANE/MAIN.C:187-213 +
 *     SL_DEF.H:546 (RBG0ON bit) + ST-058-R2 §RBG. The jo wrapper does
 *     slRparaInitSet + slMakeKtable + slCharRbg0(COL_TYPE_256,
 *     CHAR_SIZE_1x1) + slPlaneRA(PL_SIZE_1x1) + sl1MapRA + slOverRA +
 *     slKtableRA(K_FIX|K_DOT|K_2WORD|K_ON|K_LINECOL) + slRparaMode(
 *     K_CHANGE) + RBG0ON in screen_flags. Per-frame: slCurRpara(RA) +
 *     slScrMatConv + slScrMatSet.
 *   - VDP2 VRAM bank routing (jo-engine/jo_engine/vdp2_malloc.c:175,200):
 *     RBG0 cell → A0 (114,688 B fits in 128 KB), RBG0 map → B0 (8192 B),
 *     K-table + R-table statically reserved at A1 base (130,688 B
 *     reserved unconditionally regardless of RBG0 usage). No BSS impact.
 *   - JO_COMPILE_WITH_PSEUDO_MODE7_MODULE flag NOT required for RBG0:
 *     the demo - vdp2 plane reference makefile uses
 *     JO_COMPILE_WITH_PSEUDO_MODE7_MODULE = 0 and exercises the same
 *     jo_enable_background_3d_plane API. The mode7 flag gates the
 *     SEPARATE software-rendered floor module (jo_mode7 /
 *     jo_do_mode7_floor).
 *   - NBG2 is hidden via slPriorityNbg2(0) after RBG0 takes over the
 *     backdrop slot. */
static void setup_title_bg(void)
{
    /* Phase 1.31 Fix #3 (retry, 2026-05-27): REVERTED to baseline.
     *
     * Attempted the +2 byte-offset rebuild (cd/TITLE3D.DAT + .PAL via
     * tools/build_title_island_bg.py with disk slot (1+i) holding
     * quant color i and on-disk pixel bytes in range 2..255).  The
     * static palette simulation gate (tools/qa_phase1_31_static_
     * palette_sim.py) passed S1/S2/S3 on the rebuilt asset and the
     * simulated PNG (tools/qa_golden/title3d_simulated.png) looked
     * like an authentic Mania backdrop.
     *
     * On Saturn capture (frames 60-89 of the 90-frame 3 fps qa_diag
     * sweep), the RBG0 plane STILL rendered as a pure-green
     * (~0,248,0) flood despite all on-disk arithmetic matching the
     * derived CRAM-shift convention.  The pre-quantise composite
     * and the simulated render both agree the asset is a valid
     * blue-sky + green-island backdrop, so the failure is not in
     * the build tool — it's in the Saturn-side palette/cell-mode
     * mapping that this work did not characterise correctly.
     *
     * Per the binding rule "DO NOT FLASH to ISO if the static preview
     * shows wrong colors", I'd already cleared that gate; the
     * Saturn-side failure now invokes the parallel rule "If the +2
     * offset ALSO produces wrong colors, REVERT cleanly".
     *
     * REVERT: read the May-26 baseline TITLE.PAL + TITLE.DAT
     * (orange-dirt-and-green island, neon greens at slots 130/131
     * but no pure-green flood).  TITLE3D.* files remain on disk for
     * Fix #3 re-attempt; src/main.c does not consume them.
     *
     * Open question for next iteration: WHY does the +2 offset
     * still flood?  Hypothesis: the printf-reserved CRAM[257] slot
     * (= disk-PAL[0]) isn't the only leak — there may be a second
     * jo-reserved slot or a wider CRAM-bank shift that the empirical
     * "byte V renders disk[V-1]" rule doesn't fully capture for
     * RBG0 (as opposed to NBG2).  Phase 1.26b switched the backdrop
     * from NBG2 cell-mode to RBG0 rotation-plane; jo's __jo_create_map
     * (vdp2.c:240-266) was originally written for NBG paths.  The
     * pure-green RBG0 flood could indicate the rotation-plane
     * pattern-name table is using a different palette_id field
     * width than the cell-mode plane assumes — needs a savestate
     * peek of CRAM[256..512] + RBG0 pattern-name word inspection
     * (per the Mednafen savestate harness reference card,
     * tools/README_debugger.md) before the next code change.
     *
     * Citations: this attempt's trace lives in HANDOFF.md Phase 1.31
     * Fix #3 retry; the static-gate pass + Saturn-flood mismatch
     * is the data anchor for the next investigation. */
    int len_pal = 0, len_dat = 0;
    unsigned short *tpal = (unsigned short *)jo_fs_read_file("TITLE.PAL", &len_pal);
    if (!tpal || len_pal < 2 * 256) {
        if (tpal) jo_free(tpal);
        return;
    }
    jo_create_palette_from(&g_title_pal, tpal, 256);
    /* tpal pointer ownership is transferred to jo's palette table — do
     * NOT free here (same convention as the legacy main.c). Captured
     * for Phase 2.1's title->GHZ transition free path. */
    s_title_bg_pal_buf = tpal;

    /* #187: read into the LWRAM staging region, NOT the jo pool. dat is an
     * LWRAM pointer; it must never be jo_free'd (see TITLE_DAT_LWRAM_STAGE
     * note above and mania_free_title_bg_buffers). */
    unsigned char *dat = (unsigned char *)jo_fs_read_file_ptr(
        "TITLE.DAT", TITLE_DAT_LWRAM_STAGE, &len_dat);
    if (!dat || len_dat < TITLE_BG_W * TITLE_BG_H) {
        return;
    }
    static jo_img_8bits img;
    img.data   = dat;
    img.width  = TITLE_BG_W;
    img.height = TITLE_BG_H;

    /* Phase 1.26b §11.33 — enable RBG0 rotation plane and bind the
     * 224x512 Sonic-island bitmap as plane A. Args:
     *   - JO_COLOR_RGB(96, 128, 224) matches jo_core_init's back-color
     *     (sky-blue) so any RBG0 over-mode-single transparent fringe
     *     remains visually consistent with the prior NBG2 pin-(0,0)
     *     edge behavior.
     *   - palette_id = g_title_pal.id (the same 256-color palette
     *     loaded from cd/TITLE.PAL).
     *   - repeat = false  (Phase 1.31 Fix #1, 2026-05-27):  jo's
     *     jo_vdp2_set_rbg0_plane_a_8bits_image at vdp2.c:299 maps
     *     `repeat` -> slOverRA(RBG0_OVER_MODE_REPEAT=0 if true,
     *     else RBG0_OVER_MODE_SINGLE=3).  Per ST-058-R2 §RBG (VDP2
     *     manual, OVRA bits) mode 3 = "display screen inside-plane
     *     only; outside = back-color".  With our 224x512 image and
     *     1x1 plane (PL_SIZE_1x1 = 512x512), mode REPEAT (formerly
     *     here) caused jo's __jo_create_map (vdp2.c:240-266) to bake
     *     a horizontal tile-wrap into the 64x64 page map at every
     *     28 cells (224 px), producing visible seams every 224 px
     *     of world-x.  Switching to mode SINGLE keeps the map data
     *     intact but the OVR register now tells VDP2 not to repeat
     *     the plane outside its bounds; combined with the user's
     *     authorisation to remove the cross-plane tiling (this is
     *     the first of four title-backdrop fixes — pixel-content
     *     and palette artifacts are addressed in Fix #2/#3), this
     *     is the smallest-possible change with the maximum
     *     measurable visual impact.  Gate
     *     tools/qa_phase1_31_tile_seam_gate.py asserts the change
     *     RED->GREEN.
     *   - vertical_flip = true: matches the prior
     *     jo_vdp2_set_nbg2_8bits_image(..., true) call's vertical_flip
     *     argument so the GIF→DAT orientation is preserved (without
     *     this flip the island would render upside-down because
     *     jo_img_to_vdp2_cells reads column-major top-down). */
    jo_enable_background_3d_plane(JO_COLOR_RGB(96, 128, 224));
    jo_background_3d_plane_a_img(&img, g_title_pal.id, false, true);
    /* Phase 1.31 Fix #3 (LANDED 2026-05-27 — final).
     *
     * RBG0's CRAM read region routing.  Per ST-058-R2 §11 (Color RAM
     * Address Offset Register, p.217) the 3-bit R0CAOS field at CRAOFB
     * bits 2..0 (Saturn addr 0x05F800E6) selects which 256-slot bank
     * of CRAM RBG0's 8-bit pixel bytes index into:
     *   slot_used = (R0CAOS << 8) | pixel_byte
     * After jo_core_init the register reads 0x0000 so R0CAOS = 0 and
     * RBG0 pulls from CRAM[0..255] — that's the printf-font region
     * (jo-engine/jo_engine/vdp2_malloc.c:60 reserves the first
     * 256-slot bank +1 transparent slot for printf/internal use).
     * jo's first user palette (the one loaded here for the title
     * island) lands at CRAM[257..512], so with R0CAOS=0 RBG0 reads
     * mostly-zero CRAM and the picture comes out as a neon-green
     * flood — pre-fix savestate (samples/qa_phase1_31_fix3_diag.mcs):
     * 15 nonzero slots in CRAM[0..255] vs 158 in CRAM[257..512].
     *
     * Setting R0CAOS=1 routes the read to CRAM[256..511] which is
     * exactly where jo's allocator placed g_title_pal.  Slot 256 is
     * jo's reserved transparent slot; the asset palette occupies
     * slots 257..511; the pre-shift-up-by-1 convention from
     * memory/jo-cram-off-by-one-shift.md aligns the on-disk PAL
     * byte V with the in-CRAM slot 256+V.
     *
     * Two attempts to write the bit from inside setup_title_bg
     * (slColRAMOffsetRbg0(1) and a direct CRAOFB store) both failed:
     * the post-init savestate kept showing CRAOFB.R0CAOS=0.  SGL
     * caches scroll-screen config in its internal screen_flags table
     * and re-applies it to the VDP2 register window on every slSynch
     * — the same clobber pattern that necessitated the per-frame
     * slScrAutoDisp / slPriorityRbg0 REPLACE inside
     * mania_title_3d_backdrop_draw (see §11.33c).
     *
     * Fix: drop the one-shot write here entirely and instead include
     * slColRAMOffsetRbg0(1) in mania_title_3d_backdrop_draw's
     * per-frame REPLACE set.  That function already runs every tick
     * AFTER jo+SGL init has fully settled and ALREADY re-applies
     * slScrAutoDisp/slPriorityRbg0 for exactly this clobber reason;
     * adding one more short register store is free.  Simpler than the
     * jo_core_add_callback one-shot path because it reuses the
     * proven REPLACE site rather than introducing a new self-
     * unregistering callback.
     *
     * Gate: tools/qa_phase1_31_rbg0_diag_gate.py P5 (RED on baseline,
     * GREEN after this fix). */
    /* Place RBG0 below the VDP1 sprite priority (default 6) so the logo,
     * Sonic, banners, and finger-wave composite on top of the rotating
     * backdrop — same priority slot the NBG2 path used (5). */
    slPriorityRbg0(5);
    /* NBG2 is no longer the backdrop; hide it explicitly (the
     * Phase 1.3 boot stub set NBG2 priority unconditionally; we override
     * to 0 here so the dummy NBG2 setup from main_jo_init can't show
     * through during RBG0 rendering). */
    slPriorityNbg2(0);

    /* Phase 1.20 CRAM-base capture — palette mirror drains are stubbed
     * per §11.29 GAP E1 (mania_title_palette_drain returns without
     * writing CRAM) so this captured value is presently inert, but the
     * field is kept populated so a future re-enable of the drain path
     * doesn't have to re-derive it. */
    s_title_pal_cram_base = g_title_pal.id * 256;
    s_title_bg_ready = 1;
    /* Capture dat pointer for Phase 2.1 transition-time free. The pixels
     * have been COPIED into VDP2 VRAM by jo_img_to_vdp2_cells (see
     * jo-engine/jo_engine/vdp2.c:294-310), so the Work-RAM source can be
     * safely freed once we no longer need to re-issue the bitmap. */
    s_title_bg_dat = dat;
}

/* Phase 2.1 — free the title backdrop's Work-RAM source buffers. Called
 * from the title->GHZ transition in src/mania/Game.c to recover ~112 KB
 * of jo pool space for the GHZ FG.TMP (262 KB) allocation. The VDP2
 * VRAM contents are unaffected — only the Work-RAM source pointers are
 * freed. NBG2 retains its cell-mode bitmap until the GHZ sky setup
 * rewrites it.
 *
 * Exposed via the mania-side header path because Game.c is the
 * transition driver. */
void mania_free_title_bg_buffers(void)
{
    /* Phase 1.26b §11.33 — flip RBG0 off so VDP2 VRAM bank A0 (RBG0 cell)
     * and B0 (RBG0 map) are no longer contested when setup_ghz allocates
     * NBG1 FG cell (90 KB into A0). Per jo-engine/jo_engine/vdp2.c:362-379
     * jo_vdp2_disable_rbg0 clears RBG0ON in screen_flags, calls
     * slRparaMode(K_OFF), and frees the K-table + R-table dynamic slots
     * (which are themselves in the A1 statically-reserved region — the
     * free just clears jo's tracker so a future enable reallocates from
     * the same VDP2 VRAM region).
     *
     * The RBG0 cell + map data in A0/B0 remain in their static slots
     * (jo's allocator keeps the rbg0_cell_a / rbg0_map_a pointers alive
     * across the disable so a future plane-A image reuse can rebind);
     * for the title→GHZ single-shot transition we don't re-enter RBG0,
     * so the A0/B0 space remains conceptually held — but jo's plane-A
     * cell allocator routes JO_VDP2_RAM_CELL_NBG1 to A0 too (per
     * vdp2_malloc.c:196-220), and segment reuse with is_free=false would
     * starve the NBG1 allocation. So we explicitly free both:
     *
     *   - rbg0_cell_a / rbg0_map_a are STATIC inside jo's vdp2.c so we
     *     cannot reach them directly; instead jo's
     *     jo_vdp2_set_nbg1_8bits_image (GHZ-side, called from scene_ghz)
     *     will REUSE the freed segments because it allocates from A0
     *     too. We pre-free here by invoking the public jo_vdp2_free of
     *     the disable_rbg0 K/R tables, then leave the cell/map in place
     *     — A0's 128 KB minus the held 114 KB RBG0 cell leaves only
     *     14 KB for NBG1's 90 KB. Insufficient.
     *
     *   - Mitigation (BINDING per Phase 2.1 §11.33 VRAM audit): the
     *     simplest correct path is to call jo_vdp2_set_rbg0_plane_a_8bits
     *     _image with a tiny 8x8 dummy image BEFORE disable, which
     *     triggers jo's internal `jo_vdp2_free(rbg0_cell_a)` branch at
     *     vdp2.c:300-302, freeing the 114 KB A0 slot. We then disable
     *     RBG0. The 8 B tiny image is harmless (it's overwritten by the
     *     disable's RBG0OFF flip before the next vblank). */
    static unsigned char s_rbg0_disable_dummy_data[64];
    static jo_img_8bits  s_rbg0_disable_dummy_img;
    s_rbg0_disable_dummy_img.data   = s_rbg0_disable_dummy_data;
    s_rbg0_disable_dummy_img.width  = 8;
    s_rbg0_disable_dummy_img.height = 8;
    if (s_title_bg_ready) {
        /* Replace plane-A cell with 8x8 dummy (triggers jo_vdp2_free of
         * the 114 KB A0 slot per vdp2.c:300-302). */
        jo_background_3d_plane_a_img(&s_rbg0_disable_dummy_img,
                                     g_title_pal.id, true, false);
        /* Flip RBG0OFF + free K/R tables. */
        jo_disable_background_3d_plane(JO_COLOR_RGB(96, 128, 224));
    }
    /* #187: s_title_bg_dat points at TITLE_DAT_LWRAM_STAGE (0x00210000), NOT
     * a jo-pool allocation -- jo_free would treat the LWRAM byte at stage-8
     * as a jo_memory_block header and corrupt the zone. The LWRAM staging
     * buffer needs no free (it is reclaimed by the GHZ FG.TMP load at this
     * same transition); just drop the readiness marker. The 114 KB it used
     * to strand in the pool is now never in the pool at all. */
    s_title_bg_dat = NULL;
    if (s_title_bg_pal_buf) {
        jo_free(s_title_bg_pal_buf);
        s_title_bg_pal_buf = NULL;
    }
    s_title_bg_ready = 0;
    s_title_pal_cram_base = -1;
}

/* Phase 1.34b — NBG2 cloud parallax setup (Sub-fix B revived).
 *
 * Loads cd/CLOUDS.DAT + cd/CLOUDS.PAL (256x256 8-bpp cell-mode + 256-entry
 * RGB555 BE palette) into VDP2 NBG2 via jo_vdp2_set_nbg2_8bits_image.
 * NBG2 is mapped at priority 1 (back-screen layer, identical to the GHZ
 * sky pattern in src/rsdk/scene_ghz.c:319-321 -- the canonical
 * documented-working cell-mode-NBG2 path on this project).  VDP1 sprite
 * priority 7 + Title3DSprite billboards at default jo sprite priority
 * (6 by default; see Phase 1.32 Title3DSprite cluster) composite ON TOP
 * of the cloud plane because:
 *   - sprite priority 7 > NBG2 priority 1 -> sprites win
 *   - billboards at sprite-priority 6 also > NBG2 priority 1 -> win
 *   - RBG0 is hidden (Phase 1.31 Fix #4 Sub-fix A, slPriorityRbg0(0))
 *     so it doesn't compete with NBG2 for the back-screen layer slot.
 *
 * Per ST-058-R2 §3.4 (Priority Number Register, p.225) layer priority
 * controls the composite order; the cloud plane sits behind all VDP1
 * output and the (currently disabled) RBG0 island plane.
 *
 * Asset budget audit (per CLAUDE.md §4.5.1 Audit 4):
 *   CLOUDS.DAT = 64 KB, CLOUDS.PAL = 0.5 KB.  64.5 KB / 150 KB/s
 *   = 0.43s + 1 GFS seek (~0.1s) = ~0.53s.  Boot-path budget under 5s.
 *
 * Citation chain:
 *   - tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:103-136
 *     (Scanline_Clouds source -- decomp authority for the cloud band)
 *   - ST-058-R2-060194.pdf §3.3 (NBG2 cell-mode) + §3.4 (priorities)
 *   - ST-238-R1-051795.pdf §slPriorityNbg2 + §slScrPosNbg2
 *   - jo-engine/jo_engine/vdp2.c:739-748 (jo_vdp2_set_nbg2_8bits_image)
 *   - src/rsdk/scene_ghz.c:256-352 (GHZ sky NBG2 reference; verified
 *     working pattern). */
static void setup_clouds_bg(void)
{
    int len_pal = 0, len_dat = 0;
    unsigned short *cpal = (unsigned short *)jo_fs_read_file("CLOUDS.PAL",
                                                              &len_pal);
    if (!cpal || len_pal < 2 * 256) {
        if (cpal) jo_free(cpal);
        return;
    }
    jo_create_palette_from(&g_clouds_pal, cpal, 256);
    /* Pointer ownership transferred to jo palette table; held for the
     * title->GHZ transition teardown path in mania_free_title_bg_buffers. */
    s_clouds_bg_pal_buf = cpal;

    unsigned char *dat = (unsigned char *)jo_fs_read_file("CLOUDS.DAT",
                                                           &len_dat);
    if (!dat || len_dat < CLOUDS_BG_W * CLOUDS_BG_H) {
        if (dat) jo_free(dat);
        return;
    }

    static jo_img_8bits img;
    img.data   = dat;
    img.width  = CLOUDS_BG_W;
    img.height = CLOUDS_BG_H;

    /* Wipe NBG2 scroll before the cell-mode upload so the just-loaded
     * 256x256 bitmap lines up at column 0 (mirrors scene_ghz.c:284). */
    slScrPosNbg2(0, 0);

    /* Per scene_ghz.c:286-300 the NBG2 cell-mode rewrite is a REPLACE
     * across slScrAutoDisp, and the bitmask must include EVERY currently-
     * enabled layer.  At boot time (called from jo_main right after
     * setup_title_bg and the NBG1 dummy seed) the active layers are
     * NBG1 + the implicit sprite layer.  The jo wrapper will append
     * NBG2ON internally when `enabled=true` is passed; we then issue
     * a follow-up slScrAutoDisp with the full title-state mask so the
     * RBG0 dummy (priority 0 / hidden) + sprite layer stay enabled. */
    jo_vdp2_set_nbg2_8bits_image(&img, g_clouds_pal.id,
                                 /*vertical_flip=*/false,
                                 /*enabled=*/true);

    /* Re-issue plane config (BIPLANE NOV96 sample §sample setup pattern;
     * also mirrored at scene_ghz.c:307-308).  jo's
     * jo_vdp2_set_nbg2_8bits_image already calls slPlaneNbg2 + slCharNbg2
     * internally via __jo_set_nbg2_8bits_image (vdp2.c:678-686), but
     * re-issuing is safe and keeps the post-call state explicit. */
    slPlaneNbg2(PL_SIZE_1x1);
    slCharNbg2(COL_TYPE_256, CHAR_SIZE_1x1);

    s_clouds_bg_dat = dat;
    s_clouds_bg_ready = 1;
}

/* Phase 1.34b — mirror mania_free_title_bg_buffers's pool-recovery path
 * for the clouds asset.  Called from the title->GHZ transition driver. */
void mania_free_clouds_bg_buffers(void)
{
    if (s_clouds_bg_dat) {
        jo_free(s_clouds_bg_dat);
        s_clouds_bg_dat = NULL;
    }
    if (s_clouds_bg_pal_buf) {
        jo_free(s_clouds_bg_pal_buf);
        s_clouds_bg_pal_buf = NULL;
    }
    s_clouds_bg_ready = 0;
}

/* Phase 1.35 — NBG1 decomp-island plane setup.
 *
 * Loads cd/ISLAND.DAT + cd/ISLAND.PAL (512x256 8-bpp cell-mode + 256-entry
 * RGB555 BE palette) into VDP2 NBG1 via jo_vdp2_set_nbg1_8bits_image
 * (jo-engine/jo_engine/vdp2.c:527).
 *
 * Mirrors setup_clouds_bg's proven Phase 1.34b GREEN flow exactly, with
 * NBG1 substituted for NBG2.  The NBG1 wrapper internally calls
 * slPlaneNbg1(PL_SIZE_1x1) + slCharNbg1(COL_TYPE_256, CHAR_SIZE_1x1) +
 * slPageNbg1 + jo_img_to_vdp2_cells + ADD_FLAG(screen_flags, NBG1ON) +
 * slScrAutoDisp(screen_flags).
 *
 * Note on the jo_main NBG1-dummy seed at lines ~1017-1030: that placed
 * an 8x8 zero-palette image into NBG1 at jo_main boot time to ensure
 * the cycle-pattern was initialised before VDP1 sprite rendering came
 * online (Phase 1.3 § lineage).  We REPLACE that dummy with the real
 * island bitmap here.  jo_vdp2_set_nbg1_8bits_image's
 * `if (nbg1_cell != JO_NULL) jo_vdp2_free(nbg1_cell);` branch at
 * vdp2.c:530-531 handles the reclamation cleanly.
 *
 * Citation: tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:103-105
 *           jo-engine/jo_engine/vdp2.c:527-543 (NBG1 wrapper)
 *           src/main.c::setup_clouds_bg (Phase 1.34b GREEN reference). */
__attribute__((unused)) static void setup_island_bg(void)
{
    int len_pal = 0, len_dat = 0;
    unsigned short *ipal = (unsigned short *)jo_fs_read_file("ISLAND.PAL",
                                                              &len_pal);
    if (!ipal || len_pal < 2 * 256) {
        if (ipal) jo_free(ipal);
        return;
    }
    jo_create_palette_from(&g_island_pal, ipal, 256);
    /* Pointer ownership transferred to jo palette table; held for the
     * title->GHZ transition teardown path. */
    s_island_bg_pal_buf = ipal;

    unsigned char *dat = (unsigned char *)jo_fs_read_file("ISLAND.DAT",
                                                           &len_dat);
    if (!dat || len_dat < ISLAND_BG_W * ISLAND_BG_H) {
        if (dat) jo_free(dat);
        return;
    }

    static jo_img_8bits img;
    img.data   = dat;
    img.width  = ISLAND_BG_W;
    img.height = ISLAND_BG_H;

    /* Reset NBG1 scroll before the cell-mode swap so the just-loaded
     * 512x256 bitmap lines up at col 0.  Mirrors setup_clouds_bg's
     * pre-call slScrPosNbg2(0,0) (which itself mirrors
     * src/rsdk/scene_ghz.c:284). */
    slScrPosNbg1(0, 0);

    /* The jo NBG1 wrapper signature differs from the NBG2 wrapper: it
     * has NO `enabled` parameter (vdp2.c:527).  It unconditionally
     * appends NBG1ON to screen_flags and calls slScrAutoDisp.  The
     * per-frame REPLACE block in mania_title_3d_backdrop_draw already
     * re-applies slScrAutoDisp(NBG1ON | NBG2ON | RBG0ON | SPRON), so
     * the boot-time scroll-flag write here will be overwritten on the
     * first slSynch -- which is correct: that REPLACE block is the
     * single source of truth for screen_flags during the title state. */
    jo_vdp2_set_nbg1_8bits_image(&img, g_island_pal.id,
                                 /*vertical_flip=*/false);

    /* Re-issue plane config (BIPLANE NOV96 sample pattern; mirrors
     * setup_clouds_bg + scene_ghz.c:307-308). */
    slPlaneNbg1(PL_SIZE_1x1);
    slCharNbg1(COL_TYPE_256, CHAR_SIZE_1x1);

    s_island_bg_dat = dat;
    s_island_bg_ready = 1;
}

/* Phase 1.35 — mirror mania_free_clouds_bg_buffers's pool-recovery
 * path for the island asset.  Invoked from the title->GHZ transition
 * driver (same site as mania_free_clouds_bg_buffers). */
void mania_free_island_bg_buffers(void)
{
    if (s_island_bg_dat) {
        jo_free(s_island_bg_dat);
        s_island_bg_dat = NULL;
    }
    if (s_island_bg_pal_buf) {
        jo_free(s_island_bg_pal_buf);
        s_island_bg_pal_buf = NULL;
    }
    s_island_bg_ready = 0;
}

/* Phase 1.35 — expose the readiness flag for the per-frame priority
 * REPLACE block in mania_title_3d_backdrop_draw. */
int main_island_bg_ready(void) { return s_island_bg_ready; }

/* Phase 1.34b — per-frame NBG2 scroll tick.
 *
 * Called from mania_title_3d_backdrop_draw (already runs every tick in
 * the title state via the mania_tick title branch).  Drives uniform
 * X + Y scroll to approximate the decomp's Scanline_Clouds drift:
 *   - decomp TitleBG.c:70   `TitleBG->timer += 0x8000;` /tick
 *   - decomp TitleBG.c:122  `position.y = TitleBG->timer + 2*cos`
 *   - decomp TitleBG.c:121  `position.x = sin - center.x * (-cos >> 7)`
 *
 * With static angle=0 (Scanline_Clouds passes 0 to Sin256/Cos256),
 * cos=256 and sin=0 for every scanline.  The per-scanline `2*cos*id`
 * varies the scroll by Y -- Phase Z work.  For the first cut a uniform
 * Y scroll proportional to timer (0x4000 fixed/tick = 0.25 px/frame
 * in Q16.16 -- a gentle atmospheric drift) gives the same downward
 * cue without the runaway monotonic accumulation of the prior
 * 0x8000-per-frame attempt.
 *
 * X scroll: 1 fixed-unit/frame (matches scene_ghz.c parallax-rate
 * baseline; visually subtle at 256-wide tile-repeat).
 *
 * Phase 1.34c — bounded modulo wrap.  The previous mask
 * `s_clouds_scroll_y &= 0x7FFFFFFF` only prevented int31 sign-flip and
 * let the scroll accumulate for ~36 hours before wrapping; visually
 * the clouds dragged off-screen forever.  Per ST-058-R2 §6 (NBG2
 * scroll register SCYIN2 / SCYDN2) the VDP2 wraps cell-mode scroll
 * positions modulo the plane size automatically AT THE HARDWARE
 * LEVEL, but the visible drift only feels "cyclic" if our software
 * counter mirrors that wrap so the accumulated Q16.16 value stays
 * small.  Wrap both axes modulo the cloud bitmap size
 * (CLOUDS_BG_W = CLOUDS_BG_H = 256 px) so the scroll cycles cleanly
 * every ~1024 frames at 0.25 px/frame.
 *
 * SGL slScrPosNbg2 contract: arguments are Q16.16 FIXED (per
 * ST-238-R1 §slScrPosNbg2 / SGL302/INC/SL_DEF.H).  toFIXED(1.0)
 * = JO_MULT_BY_65536(1) = 0x10000. */
static void clouds_bg_tick(void)
{
    static int s_clouds_scroll_x = 0;  /* Q16.16 */
    static int s_clouds_scroll_y = 0;  /* Q16.16 */

    /* Wrap modulus = CLOUDS_BG_W/H << 16 (Q16.16 fixed-point).  Per
     * ST-058-R2 §6.5 cell-mode scroll wraps automatically at the
     * plane boundary; we wrap our software counter at the same
     * boundary so the visible drift cycles instead of growing
     * monotonically forever. */
    const int wrap_x_q16 = (int)(CLOUDS_BG_W << 16);
    const int wrap_y_q16 = (int)(CLOUDS_BG_H << 16);

    if (!s_clouds_bg_ready) return;

    /* Phase 1.40b (user direction 2026-05-28): user clarified "why is
     * the background still vertically scrolling" — the vertical scroll
     * was the WRONG part, not the desired behavior. Static clouds: X
     * and Y both zero. ST-058-R2 §6.5 cell-mode tolerates fixed
     * scroll position. */
    s_clouds_scroll_x = 0;
    s_clouds_scroll_y = 0;
    (void)wrap_x_q16; (void)wrap_y_q16;

    slScrPosNbg2(s_clouds_scroll_x, s_clouds_scroll_y);
}

/* Phase 1.26b §11.33 — per-frame RBG0 title backdrop rotation driver.
 *
 * Called from src/mania/Game.c::mania_tick in the title state branch
 * BEFORE title_direct_draw so VDP1 sprite output composites on top.
 * Skipped when s_title_bg_ready is 0 (RBG0 setup failed or has been
 * torn down by mania_free_title_bg_buffers).
 *
 * Implementation note (BINDING per §11.33 pre-code audit):
 *   This project's Makefile sets JO_COMPILE_WITH_3D_MODULE = 0 (the
 *   ELF doesn't need jo's 3D mesh path; jo_sprite_draw3D lives in
 *   sprites.h independently per the Makefile comment at line 26).
 *   The jo_3d_* helpers in jo/3d.h are gated by
 *   JO_COMPILE_WITH_3D_SUPPORT and would be invisible here.
 *
 *   We therefore call SGL directly. jo_3d_push_matrix/...rotate.../
 *   ...translate.../jo_3d_pop_matrix are pure thin wrappers over
 *   slPushMatrix / slRotX / slRotY / slRotZ / slTranslate /
 *   slPopMatrix (jo-engine/jo_engine/jo/3d.h:613-725 — each helper
 *   is a single `__jo_force_inline` over the SGL primitive). The
 *   draw call jo_background_3d_plane_a_draw(true) expands to
 *   `slCurRpara(RA); slScrMatConv(); slScrMatSet();`
 *   (jo/vdp2.h:403-406) and is reachable via background.h because
 *   it only requires JO_COMPILE_USING_SGL (which IS enabled).
 *
 * Citation chain:
 *   - NOV96_DTS/EXAMPLES/SGL/BIPLANE/MAIN.C:317-337 (main_map) —
 *     canonical per-frame RBG0 rotation: slPushMatrix +
 *     set_matrix(pos, ang) [= slRotX/Y/Z + slTranslate] +
 *     slCurRpara(RA) + slScrMatConv + slScrMatSet + slPopMatrix.
 *   - jo-engine/Samples/demo - vdp2 plane/main.c:80-98 — same
 *     pattern via jo_3d_* helpers (jo wrapper exercise).
 *   - jo-engine/jo_engine/jo/vdp2.h:403-406 — confirmed the jo
 *     wrapper expansion.
 *   - SGL RADtoANG (NOV96_DTS/LIBRARY/SDK_10J/SGL302/INC/SL_DEF.H)
 *     converts a `float` radian to the `ANGLE` (Sint16) fixed-point
 *     unit consumed by slRotY.
 *   - SGL toFIXED (SL_DEF.H) converts a `float` to the `FIXED`
 *     (Sint32 16.16) unit consumed by slTranslate.
 *   - Yaw rate: 0.5 deg/tick @ 60 Hz = 30 deg/sec = 12-second
 *     orbit. Slow enough to read as a gentle camera drift; ~7.5 px
 *     of perimeter motion per tick on a 512-px-radius plane — well
 *     above the 1.5-px inter-frame mean-delta floor used by Gate
 *     V1.26b.
 *   - Z translation = 256.0f keeps the plane at the default RBG0
 *     viewing distance (jo's enable_rbg0 path sets up the rotation
 *     parameter table for the standard "ground at z=0 viewed from
 *     z>0" affine). */
void mania_title_3d_backdrop_draw(void)
{
    static float s_title_bg_yaw_deg = 0.0f;
    float yaw_rad;

    if (!s_title_bg_ready) {
        return;
    }

    /* Phase 1.26c §11.33c — RED-gate-driven fix.
     *
     * Diagnostic evidence (tools/qa_rbg0_rotation_gate.py against
     * samples/qa_phase1_26c_t0.mcs):
     *   BGON @ 0x25F80020 = 0x0002 -- only NBG1ON set; R0ON (bit 4) NOT
     *   set despite setup_title_bg calling jo_enable_background_3d_plane.
     *   PRINA = 0x0000 / PRINB = 0x0000 -- ALL NBG priorities cleared
     *   after setup_title_bg completed (the slPriorityNbg2(5) and
     *   slPriorityRbg0(5) we set didn't fully survive).
     *   PRIR  = 0x0004 -- our slPriorityRbg0(5) was clobbered down to 4.
     *   Matrix block at RPTA-pointed VRAM is IDENTICAL between two
     *   states 2s apart -- slScrMatSet pipeline isn't reaching VDP2
     *   VRAM, even though this draw function IS called every tick
     *   from mania_tick.
     *
     * Root cause: SGL's slInitSystem (called by jo_core_init) and/or
     * slSynch on the first frame REPLACE the scroll-screen enable
     * mask + priorities with their internal defaults.  The jo-engine
     * sets screen_flags via JO_ADD_FLAG and calls slScrAutoDisp ONCE
     * at jo_vdp2_enable_rbg0 time; that one-shot write is lost.
     *
     * Fix (REPLACE semantics per src/rsdk/scene_ghz.c:297 binding
     * pattern + DTS NOV96 BIPLANE/MAIN.C:212):
     *   - Re-apply slScrAutoDisp(NBG1ON | RBG0ON | SPRON) every frame
     *     so BGON's R0ON bit stays set regardless of any downstream
     *     clobber.  NBG1ON kept for backdrop fallback (was already in
     *     the post-jo-init mask); SPRON for VDP1 sprite output.
     *   - Re-apply slPriorityRbg0(5) every frame so PRIR doesn't drift
     *     back to 0 (transparent) or to a different value.
     *
     * This matches the same per-frame REPLACE pattern proven correct
     * for the GHZ scene at scene_ghz.c:300/325.  No extra cost: both
     * calls are short register writes.
     *
     * DTS citations:
     *   - ST-058-R2 §11.1 (Priority Number Register, p.225) -- PRIR
     *     bits 2-0 = R0PRIN; value 0 = transparent/hidden.
     *   - ST-058-R2 §6.2 (BGON, p.180-181) -- R0ON bit 4 enables RBG0
     *     scroll.
     *   - NOV96_DTS/EXAMPLES/SGL/BIPLANE/MAIN.C:212 -- canonical
     *     slScrAutoDisp(NBG0ON | RBG0ON) per-frame enable pattern.
     */
    /* Phase 1.31 Fix #4 REVISED (2026-05-27, Task #106): SUB-FIX A —
     * hide RBG0 entirely.  User-mandated strategic pivot after the
     * matrix-stretch + W0-clip iterations both regressed the visible
     * composite.  The Phase 1.32/1.32b VDP1 billboards alone form the
     * visible island (Title3DSprite software 3D projection); the
     * full-screen RBG0 rotating bitmap is the cosmetic the user
     * dislikes and is being eliminated.
     *
     * Change: slPriorityRbg0(5) -> slPriorityRbg0(0).  Per ST-058-R2
     * §11.1 (Priority Number Register, p.225) priority 0 = layer
     * transparent / not displayed; the SGL helper writes PRIR.R0PRIN
     * bits 2..0 (Saturn register 0x05F800FC) and updates SGL's
     * internal mirror so the REPLACE-on-slSynch behaviour stays
     * consistent.  BGON.R0ON stays asserted via the unchanged
     * slScrAutoDisp call below (so the RBG0 pipeline is still
     * "enabled" but contributes 0 priority = no pixels emitted).
     *
     * Keeping the rest of the REPLACE block (slScrAutoDisp,
     * slColRAMOffsetRbg0(1)) harmless when priority=0 — the writes
     * are short register stores; no rendering side effect.  The
     * downstream slPushMatrix/slRotX/slRotY/slTranslate/slCurRpara
     * matrix work also becomes a no-op visually but stays cheap
     * (rotation parameter table writes only execute when RBG0 is
     * actually being sampled).
     *
     * Gate: tools/qa_phase1_31_fix4_pivot_gate.py P1 (PRIR.R0PRIN==0)
     * + P2 (top-1/3 ROI free of neon-green flood).
     *
     * Phase 3.2b REVERTED 2026-05-28 — NBG0 dynamic mask + slPriorityNbg0
     * lift produced black-screen + artifacts in menu state. Restored to
     * pre-Phase-3.2b static mask. Menu integration deferred. */
    slScrAutoDisp(NBG1ON | NBG2ON | RBG0ON | SPRON);
    slPriorityRbg0(0);
    /* Phase 1.35 — NBG1 decomp-island priority re-apply.
     *
     * Mirrors the slPriorityRbg0/slPriorityNbg2 per-frame REPLACE pattern.
     * SGL's slSynch caches scroll-screen priorities in its internal state
     * and re-applies them every vblank, so boot-time-only writes drift.
     * Re-applying slPriorityNbg1(2) per-frame keeps the island plane
     * stable at priority 2 (above clouds=1, below VDP1 sprites=6).
     *
     * Citation: ST-058-R2 §11.1 (Priority Number Register, PRINA p.225,
     * N1PRIN bits 4:6); same clobber-pattern as Phase 1.34c
     * slPriorityNbg2(1) re-apply below. */
    /* Phase 1.35c REVERTED 2026-05-28 (Task #114) -- per-frame REPLACE
     * disabled again. The 256x256 ISLAND.DAT cleared the A0-exhaustion
     * mode from Phase 1.35 v1, but exposed a NEW failure (orange border
     * + 32% blue contamination of the MANIA wordmark ROI per
     * tools/qa_phase1_35_island_gate.py P6/P7/P8). See setup_island_bg
     * site comment for the full measured handoff to Phase 1.35d. */
    /* Phase 1.35d 2026-05-28 (Task #114) -- per-frame REPLACE disabled
     * again. See setup_island_bg call-site comment for the full register-
     * diff diagnosis; the per-frame priority chase produced visible
     * MANIA-wordmark blue bleed because the boot-time PRINA wipe takes
     * effect on frame 0 while this REPLACE re-fires on frame 1+. */
    /* if (main_island_bg_ready()) { slPriorityNbg1(2); } */
    /* Phase 1.34b — NBG2 cloud parallax priority + per-frame scroll.
     *
     * NBG2 priority = 1 (back-screen layer below sprites + RBG0).
     * Mirrors the GHZ sky pattern at src/rsdk/scene_ghz.c:320 which
     * is the documented-working 256-color cell-mode NBG2 path.
     *
     * VDP1 sprite priority = 7 (Phase 2.3 fix; see scene_ghz.c:321)
     * so the Title3DSprite billboards + TitleBG sprite entities
     * composite ON TOP of the cloud plane.  Per ST-058-R2 §3.4
     * Priority Number Register (p.225) higher priority = drawn on
     * top.  Priority 0 = layer hidden.
     *
     * Re-applied every frame for the same SGL-clobber-on-slSynch
     * reason as slPriorityRbg0 above. */
    if (s_clouds_bg_ready) {
        slPriorityNbg2(1);
        clouds_bg_tick();
    }
    /* Phase 1.31 Fix #3 (2026-05-27): per-frame CRAOFB.R0CAOS = 1.
     *
     * RBG0 reads CRAM[R0CAOS*256 .. R0CAOS*256+255] per ST-058-R2
     * §11 p.217 (Color RAM Address Offset Register, bits 2..0 at
     * 0x05F800E6).  jo allocates user palette 0 at CRAM[257..512]
     * (jo-engine/jo_engine/vdp2_malloc.c:60 reserves CRAM[0..256]
     * for printf font + transparent slot), so R0CAOS=1 routes RBG0
     * into the asset-palette region.
     *
     * Two prior attempts placed slColRAMOffsetRbg0(1) inside
     * setup_title_bg.  Both failed: the post-init savestate
     * (samples/qa_phase1_31_fix3_diag.mcs) still shows R0CAOS=0
     * because SGL's slSynch re-applies its cached scroll-screen
     * state to VDP2 every vblank, overwriting the one-shot write.
     * The REPLACE block above already handles BGON.R0ON, PRIR.R0PRIN
     * (per §11.33c diagnosis); CRAOFB.R0CAOS belongs in the same
     * per-frame REPLACE for the same reason.
     *
     * SGL helper slColRAMOffsetRbg0 (SGL302/INC/SL_DEF.H:1081) writes
     * the register AND updates SGL's internal cache, so this single
     * call keeps both the VDP2 hardware view and SGL's mirror
     * consistent across the next slSynch.  Cost: one short register
     * store + one byte cache update per frame; below the
     * measurement floor.
     *
     * Gate: tools/qa_phase1_31_rbg0_diag_gate.py P5 RED -> GREEN. */
    slColRAMOffsetRbg0(1);

    /* Phase 1.31 Fix #4a v2 (2026-05-27, Task #106): REVERTED — W0 ALONE
     * (no matrix change) also produces the back-color flood failure mode.
     *
     * What was tried:
     *   slScrWindow0(0, 80, 319, 239);
     *   slScrWindowMode(scnRBG0, win0_IN);
     * with the existing slRotX(90)/Z=256 matrix UNCHANGED.
     *
     * Gate result: qa_phase1_31_fix4a_clip_gate.py P1..P6 ALL GREEN
     * against samples/qa_phase1_31_fix4a_post.mcs (SaveFrame=28):
     *   WPSY0=0x50 (=80)  WPEY0=0xef (=239)  WCTLC=0x0003 (R0W0E=1 R0W0A=1)
     *   WPSX0=0x000 (=0)  WPEX0=0x27e (=319)
     *
     * Visual result: REGRESSION confirmed identical to v1 attempt.
     * 90-frame 3 fps capture qa_phase1_31_fix4a_{60,70,85}.png:
     *   - Frame 60 (pre-island): solid sky-blue back-color full screen (OK).
     *   - Frame 70/85 (settled): neon-green flood covers ~95% of screen.
     *     RBG0 island visible only as a thin vertical sliver near the
     *     left edge (Y≈160..220 region). VDP1 sprites (Sonic head,
     *     wings, SONIC+MANIA wordmark, ribbons) composite on top of the
     *     green flood unchanged.
     *
     * Diagnosis (revised from v1 hand-off — matrix is NOT the cause):
     *   The W0 + R0W0A=1 + jo_enable_background_3d_plane combo causes
     *   RBG0 to suppress rendering outside the projected pixel area of
     *   the rotation-parameter-A pose AND simultaneously the projected
     *   pose's bbox apparently shrinks to a sliver (NOT to the bottom
     *   2/3 the W0 rectangle defines). The slOverRA(SINGLE) configured
     *   by Fix #1 (rsdk/scene_ghz.c: repeat=false) means OUT-of-bitmap
     *   regions display back-color. With W0 enabled and R0W0A=1, the
     *   INTERSECTION of (inside-W0) and (rotation-A projected bbox) is
     *   the rendered region; everything outside that becomes back-color.
     *   The visible sliver = the intersection. The neon green is the
     *   transient k-table-write back-color leaking through Fix #3's
     *   patched CRAM region (Fix #3 fixed R0CAOS=1 routing but did NOT
     *   fix the underlying back-color word at 0x05E3FFFE that holds
     *   0x83E0 = neon green).
     *
     *   The W0 clip is semantically incompatible with the current
     *   jo_enable_background_3d_plane + slOverRA(SINGLE) configuration
     *   because both APIs assume the entire visible frame is the
     *   sampling domain. Restricting the sampling domain to a window
     *   collapses the projected-pose region instead of letting the
     *   plane render full-screen and clipping the OUTPUT.
     *
     * Bisect needed (NEXT iteration, Fix #4a v3):
     *   Hypothesis A: W0 must be combined with slOverRA(REPEAT) (Fix
     *     #1 reverse) — the seam regression was the cost of getting
     *     SINGLE. If REPEAT + W0 still produces flood, eliminate W0.
     *   Hypothesis B: The W0 clip semantics for RBG0 require a coefficient
     *     table change (slCoefTable) — when SGL projects the plane
     *     through W0, it samples K-coefficients per scanline and emits
     *     transparent for skipped lines. The K-table SGL builds via
     *     slRparaInit/slScrMatSet assumes full-screen; truncating to a
     *     band may need a custom K-table.
     *   Hypothesis C: VDP2 W0 + RBG0 is not the right tool for
     *     scanline-band clipping. Use VDP2 line-window (slScrLineWindow0)
     *     with a per-scanline mask table that's wide in Y=80..239 and
     *     zero-width elsewhere. Per ST-058-R2 §8.1 p.183 line-windows
     *     give per-scanline control over window edges and are the
     *     mechanism for "render only band Y..Y+N" patterns.
     *
     * Per binding rule "if visual produces a regression, REVERT":
     * pulling the W0 clip; back-color flood is worse than the unclipped
     * full-screen rotating island. Reporting honestly. Task #106 stays
     * in_progress; v3 iteration must investigate hypothesis A/B/C before
     * landing a clip.
     *
     * Captured artifacts:
     *   - samples/qa_phase1_31_fix4a_post.mcs (post-fix savestate)
     *   - qa_phase1_31_fix4a_{60,70,85}.png (3-frame regression evidence)
     *
     * Citation chain (unchanged from v2 attempt):
     *   - ST-058-R2 §8.1 p.181 (WPSX0/WPSY0/WPEX0/WPEY0)
     *   - ST-058-R2 §8.1 p.183 (line window — Fix #4a v3 hypothesis C)
     *   - ST-058-R2 §8.1 p.195 (WCTLC R0W0E + R0W0A semantics)
     *   - SGL302/INC/SL_DEF.H:678 win0_IN=0x03, :651 scnRBG0=5
     *   - SGL302/INC/SL_DEF.H:1052 slScrWindow0, :1056 slScrWindowMode
     *   - SGL302/INC/SL_DEF.H:1054 slScrLineWindow0 (hypothesis C tool)
     */

    /* Phase 1.31 Fix #4a v1 (2026-05-27): ATTEMPTED + REVERTED.
     *
     * Tried VDP2 W0 + slScrWindowMode(scnRBG0, win0_IN) to clip RBG0
     * to bottom 2/3 (pixel Y=80..239). The gate
     * qa_phase1_31_fix4a_clip_gate.py confirmed all 6 register
     * predicates GREEN (WPSY0=80, WPEY0=239, WCTLC.R0W0E=1,
     * WCTLC.R0W0A=1 per "transparent process window" semantics from
     * ST-058-R2 §8.1 p.195). BUT visual capture frame 70 showed the
     * ENTIRE screen as neon green (back-screen color flood, not
     * partial bottom-2/3 island). The island disappeared completely
     * from visible output — the W0 clip + matrix-change combination
     * suppresses RBG0 rendering instead of restricting it.
     *
     * Suspected root cause for the gap between register-GREEN and
     * visual-RED: the matrix change (slRotX 90->60 + Y translate +40
     * + Z translate 256->128) PLUS the W0 clip + R0W0A=1 may push
     * the rotation-parameter-A pose outside the bitmap's projected
     * pixel region so RBG0 emits the over-mode-SINGLE back-color
     * everywhere. The back-color word at VDP2 VRAM 0x05E3FFFE happens
     * to contain 0x83E0 (neon green) — likely a transient K-table
     * write per Phase 1.31 Fix #3 retry notes (the same VRAM region
     * is reused by jo's RBG0 K-table allocator).
     *
     * Per binding rule "if visual produces a regression, REVERT":
     * the user's mandate to STOP and report on regressions to the
     * foreground composite takes precedence over landing the W0 clip
     * in isolation. The W0 register contract is correct (gate proves
     * it) — but the matrix-stretch path needs more diagnostic work
     * before re-attempting.
     *
     * Follow-up work for the next iteration:
     *   1. Capture pre-clip + post-clip VDP2 VRAM at RPTA base to see
     *      what rotation parameters slScrMatSet writes when the new
     *      matrix is in effect.
     *   2. Bisect: try W0 clip WITHOUT matrix change (keep slRotX(90),
     *      Z=256) to see if the clip alone produces a half-screen
     *      island visible in the bottom 2/3 (proves clip is non-
     *      destructive). Then bisect matrix change without W0 clip
     *      to see what the matrix-only change looks like full-screen.
     *   3. Investigate the back-color VRAM 0x05E3FFFE neon-green
     *      transient — this is a Fix #3 P4-class regression that
     *      will surface in any region that exposes back-screen.
     *
     * For now, the per-frame REPLACE block in this function remains
     * scoped to just slScrAutoDisp / slPriorityRbg0 / slColRAMOffsetRbg0
     * (Fix #3 work). The W0 clip is OUT pending the matrix bisect. */

    s_title_bg_yaw_deg += 0.5f;
    if (s_title_bg_yaw_deg >= 360.0f) {
        s_title_bg_yaw_deg -= 360.0f;
    }
    /* JO_DEG_TO_RAD lives in jo/math.h (no module gate). */
    yaw_rad = JO_DEG_TO_RAD(s_title_bg_yaw_deg);

    slPushMatrix();
    {
        /* Floor-tilt: lay the RBG0 plane flat as a "ground" perpendicular
         * to the camera-up axis.  Matches demo - vdp2 plane/main.c:141
         * (rot.rx = JO_DEG_TO_RAD(90.0)) and MMM-NETLINK main.c:6577 floor
         * setup.  Without this 90-deg X-rotation slRotY rolls the plane
         * around its vertical center axis (i.e. spins a wall-poster), and
         * the visible inter-frame delta is approximately zero on a 224x512
         * mostly-symmetric bitmap.  With rx=90deg the plane becomes the
         * floor and slRotY rotates the FLOOR yaw -- producing visible
         * texture motion across the screen as the camera "looks down" at
         * a slowly orbiting ground.
         *
         * Phase 1.31 Fix #4a REVERTED here too: tried slRotX(60) +
         * slTranslate(0,40,128) for stretch-and-center-pivot per
         * user mandate, paired with W0 clip above. Visual capture
         * frame 70 showed entire screen as back-color flood (island
         * not visible). Matrix-change bisect needed before re-attempt;
         * see the REVERTED comment block above for follow-up plan. */
        slRotX(RADtoANG(JO_DEG_TO_RAD(90.0f)));
        slRotY(RADtoANG(yaw_rad));
        slTranslate(toFIXED(0.0f), toFIXED(0.0f), toFIXED(256.0f));
        /* jo_background_3d_plane_a_draw(true) expansion: */
        slCurRpara(RA);
        slScrMatConv();
        slScrMatSet();
    }
    slPopMatrix();
}

/* Phase 1.20 — drain palette-mirror dirties to title backdrop CRAM.
 *
 * Called from mania_tick (title branch) every tick. rsdk_palette_consume
 * _dirty returns one range per dirty bank; we only honour bank 0 (the
 * title backdrop palette) — the other 7 banks are not currently bound
 * to any visible Saturn palette and their dirty-marks are discarded.
 * (The dirty bits get cleared as a side-effect of consume_dirty.)
 *
 * Per DTS96 ST-058-R2 §5 (VDP2 CRAM access): CPU short stores to CRAM
 * are valid at any time; only DMA must be vblank-gated. 4 entries per
 * drain = 8 bytes — direct CPU writes are correct.
 *
 * Memory rule applied: memory/sgl-audio-vs-scroll-cpu-dma-conflict.md
 * gates SCU DMA + audio coexistence; since we use direct CPU stores
 * (not slDMACopy/slDMAXCopy) the audio-DMA conflict cannot fire here. */
void mania_title_palette_drain(void)
{
    /* Phase 1.23 GAP E1 — bank-isolated drain ATTEMPTED but reverted.
     *
     * The Phase 1.23 capture (qa_phase1_23_seq_80.png) showed that even
     * the narrow indices-140..143 write produces the same vertical-stripe
     * NBG2 corruption that the unrestricted Phase 1.20 drain caused. The
     * root cause is structural: the NBG2 TITLE.DAT pre-composed backdrop
     * uses the SAME 256-entry CRAM bank as the decomp's bank-0 RGB565
     * mirror, and indices 140-143 in that bank are NOT the water-shimmer
     * row on the Saturn-side pre-composed backdrop — they're part of the
     * mountain/foliage gradient used by the upper-half NBG2 cells. The
     * RSDK rotation cycles their VALUES rather than rotating which CRAM
     * indices the cells point to (since the cells are baked into the
     * pre-composed bitmap), so any value mutation at those CRAM slots
     * corrupts the bitmap regardless of how narrow the write window is.
     *
     * The correct fix is structural: allocate a separate sub-palette
     * range for the water-shimmer that doesn't overlap the backdrop's
     * static cells, then re-target rsdk_rotate_palette's CRAM writes
     * to that sub-range. That's Phase 1.24+ work (needs a water-shimmer
     * sub-palette built from the decomp Title/Background.bin palette).
     *
     * For Phase 1.23: drain the dirty mask without writing CRAM so the
     * dirty accumulator doesn't grow unbounded. The visible water-shimmer
     * cosmetic is deferred. */
    if (s_title_pal_cram_base < 0) return;
    rsdk_palette_range_t ranges[RSDK_PALETTE_BANK_COUNT];
    (void)rsdk_palette_consume_dirty(ranges, RSDK_PALETTE_BANK_COUNT);
}

/* Phase 1.2 — V-blank palette CRAM upload.
 *
 * Decomp call sites that mutate the RGB565 mirror (RotatePalette,
 * SetPaletteEntry, SetLimitedFade) leave a dirty-range bitmap that
 * rsdk_palette_consume_dirty drains in V-blank. The actual CRAM DMA is
 * 16-bit short writes — Saturn CRAM at JO_VDP2_CRAM is short-accessible.
 *
 * Phase 1.3 — instrumented with a write-count global so verify_done can
 * confirm V-blank palette traffic is landing (Gate V1 hook). */

static volatile uint32_t s_palette_writes_count = 0;
uint32_t main_palette_writes_count(void) { return s_palette_writes_count; }

static void fg_vblank(void)
{
    /* Phase 1.16 — stubbed pending Phase 1.17 conversion to slDMAXCopy
     * per memory/sgl-audio-vs-scroll-cpu-dma-conflict.md. Title scene
     * does not exercise dynamic palette CRAM mutations, so the dirty
     * drain is not required for the current scope.
     *
     * Phase 1.19 confirmed empirically: this V-blank callback DOES NOT
     * fire (the back-color stays at jo_core_init's value when the
     * encoding is placed here). The shadowed encoding in
     * mania/Game.c::mania_tick is the working diagnostic path. The
     * vblank callback registration is presumably overridden by SGL's
     * own vblank handler; investigation deferred to a later phase.
     *
     * Phase 2.1 update: the empirical finding above means we cannot
     * register ghz_fg_vblank as a jo-engine vblank callback and expect
     * it to fire on its own. Instead, we invoke it from mania_tick
     * synchronously (which is itself driven from jo_core_run's main
     * loop). The "vblank" naming is preserved for clarity vs. the
     * archived build, but the call site is the active-tick. This is
     * acceptable because slDMAXCopy is asynchronous (SCU DMA starts
     * + returns immediately) and the SH-2 cache-through alias makes
     * the data coherent regardless of when the DMA completes. */
    (void)s_palette_writes_count;
    /* Phase 2.3j: ghz_is_active() removed. mania_is_ghz_active() checks
     * the title state machine which is the new readiness signal. */
    if (mania_is_ghz_active()) {
        ghz_fg_vblank();
    }
}

#ifdef P6IO_HOOK
/* P6.2 Path A (Task #206): host the engine file-I/O proof in this PROVEN jo boot.
 * jo_core_init has already run slInitSystem (core.c) + CDC_CdInit (audio.c:110) +
 * jo_fs_init/GFS_Init with the root dir loaded (core.c:388 -> fs.c:115), so jo's
 * GFS is live. p6_io_run() (tools/_portspike/_p6/p6_io_main.cpp, lean, NO
 * P6_IO_TEST) calls the UNMODIFIED engine RSDK::LoadFile("P6IO.BIN") through the
 * Saturn GFS FileIO backend (p6_gfs.c) and records the gate witnesses (p6_w_io_*)
 * in WRAM-H .bss. Linked + hooked only when `make P6IO=1`; the shipping `make`
 * leaves jo_main byte-identical. Gate: tools/_portspike/qa_p6_io.py. */
extern void p6_io_run(void);
#endif

#ifdef P6SCENE_HOOK
/* P6.3 Path A (Task #207): host the engine LoadScene proof in this PROVEN jo
 * boot. Same contract as the P6IO hook above (jo's GFS is live after
 * jo_core_init), but the body drives the UNMODIFIED engine chain InitStorage()
 * -> LoadSceneAssets() against the on-disc "Data/Stages/Title/Scene1.bin"
 * (cd/SCENE1.BIN) and copies the parsed entity coords into WRAM-H witnesses
 * (p6_w_scene_*). Engine pools + entity/layer arrays live in WRAM-L, which is
 * untouched at this point in boot (first game writer is TITLE.DAT staging at
 * 0x210000, after this returns). Linked + hooked only when `make P6SCENE=1`.
 * Gate: tools/_portspike/qa_p6_scene.py. */
extern void p6_scene_run(void);
extern void p6_scene_tick(void); /* P6.5b2: ProcessAnimation + VDP1 ring re-draw */
#endif

void jo_main(void)
{
    jo_core_init(JO_COLOR_RGB(96, 128, 224));

#ifdef P6IO_HOOK
    /* Engine LoadFile proof, once, on jo's live GFS (witnesses persist in BSS). */
    p6_io_run();
#endif

#ifdef P6SCENE_HOOK
    /* Engine LoadScene proof, once, on jo's live GFS (witnesses persist in BSS). */
    p6_scene_run();

    /* P6.5b1 (Task #208): the proof now PRESENTS the engine-decoded Island
     * layer on NBG1. Park the diag build here -- the hand-port title init
     * below would slScrAutoDisp-REPLACE the layer away (scene_ghz.c:574
     * semantics). jo_core_run with no callbacks idles on slSynch, keeping the
     * engine-rendered frame on screen for qa_p6_vdp2.py's capture tier. The
     * shipping `make` (P6SCENE unset) compiles none of this.
     * P6.5b2: one callback ticks the engine's Ring animator + re-emits the
     * VDP1 sprite each frame (SGL command lists are per-frame). */
    jo_core_add_callback(p6_scene_tick);
    jo_core_run();
    return;
#endif

    /* Hide ALL NBGs immediately so any in-VRAM garbage from the cold-boot
     * VRAM state doesn't render before setup_title_bg writes the
     * backdrop. */
    slPriorityNbg0(0);
    slPriorityNbg1(0);
    slPriorityNbg2(0);
    slPriorityNbg3(0);

    /* Phase 1.1 — engine-compat layer bring-up. */
    rsdk_object_init();
    rsdk_drawing_init();
    rsdk_input_init();
    rsdk_audio_init();
    rsdk_save_init();
    mania_engine_init();

    /* Phase 1.8 Delta 7 RESULT: RULED OUT. Skipping both NBG helper calls
     * + leaving all NBG priorities at 0 (invisible) did NOT make Probe B
     * visible against the bare sky-blue back-color (qa_probe_delta7.png:
     * solid back-color, no red square). The compositor was NOT occluding
     * sprites via NBG priority; VDP1 itself is producing nothing. The
     * NBG calls below are RESTORED so the build retains the backdrop
     * infrastructure for the Phase 1.9 deeper-VDP1-pipeline investigation.
     * See docs/COMPREHENSIVE_PLAN.md §11.13 + §11.14. */

    /* Phase 1.3 — NBG1 cycle-pattern init (restored after Phase 1.8 Delta 7
     * test). Phase 1.5 already ruled out removing this as a fix for the
     * VDP1 sprite-output suppression; kept to mirror the archived build's
     * NBG1 cell-mode setup (src/_archived/main.c.v01-handrolled:1937). */
    {
        static unsigned char s_nbg1_dummy_data[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        static jo_img_8bits  s_nbg1_dummy_img;
        static jo_palette    s_nbg1_dummy_pal;
        static unsigned short s_nbg1_dummy_pal_data[16];
        for (int i = 0; i < 16; ++i) s_nbg1_dummy_pal_data[i] = 0;
        jo_create_palette_from(&s_nbg1_dummy_pal, s_nbg1_dummy_pal_data, 16);
        s_nbg1_dummy_img.data   = s_nbg1_dummy_data;
        s_nbg1_dummy_img.width  = 8;
        s_nbg1_dummy_img.height = 8;
        jo_vdp2_set_nbg1_8bits_image(&s_nbg1_dummy_img,
                                     s_nbg1_dummy_pal.id, false);
        slPriorityNbg1(0);     /* hide NBG1 (config done, layer off) */
    }

    /* Phase 1.3 — load TITLE.DAT NBG2 backdrop. Per ST-058-R2 p. 227-228 +
     * jo init (core.c:281) PRINB low-byte = 7, so NBG2 defaults to
     * priority 7 (above default sprite priority 6 from PRISA bits 2-0).
     * The archived working build (main.c.v01-handrolled:1947) drops
     * NBG2 to priority 5 unconditionally after setup_title_bg so VDP1
     * sprites composite on top. We mirror that here. If
     * setup_title_bg fails (file missing), NBG2 is also forced to 0
     * so it can't show garbage from the default priority-7 enable. */
    setup_title_bg();
    /* Phase 1.34b — NBG2 cloud parallax setup AFTER setup_title_bg.
     *
     * setup_title_bg historically owned the NBG2 layer (Phase 1.3
     * pre-composed title backdrop) until Phase 1.26b migrated the
     * backdrop to RBG0; the slPriorityNbg2(0) hide below was the
     * residual cleanup.  Phase 1.34b takes NBG2 over for the cloud
     * band, so we now load CLOUDS.DAT here and set NBG2 priority
     * via the per-frame REPLACE block in mania_title_3d_backdrop_draw
     * (slPriorityNbg2(1) -- back-screen layer below VDP1 sprites). */
    setup_clouds_bg();
    if (s_clouds_bg_ready) {
        slPriorityNbg2(1);   /* clouds at back-screen priority */
    } else if (s_title_bg_ready) {
        /* Legacy fall-back path (CLOUDS.DAT missing): retain the
         * Phase 1.3 priority-5 NBG2 enable so title-backdrop infra
         * remains visible.  Should never fire on a clean build that
         * shipped CLOUDS.DAT. */
        slPriorityNbg2(5);   /* below default sprite priority 6 */
    } else {
        slPriorityNbg2(0);   /* hide if both setups failed */
    }

    /* Phase 1.35 — NBG1 decomp-island plane setup AFTER setup_clouds_bg.
     *
     * setup_island_bg loads the extracted decomp Title TileLayer 3
     * (per tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c:103-105
     * + tools/extract_title_island.py) into NBG1 as a 512x256 cell-mode
     * 8-bpp bitmap.  Replaces the Phase 1.3 NBG1-dummy seed above
     * (the dummy is reclaimed by jo_vdp2_set_nbg1_8bits_image's
     * jo_vdp2_free(nbg1_cell) branch at vdp2.c:530-531).
     *
     * If ISLAND.DAT is missing, NBG1 retains the dummy 8x8 zero plane
     * (priority 0, invisible) and the title scene falls back to the
     * Phase 1.34c rendering with no island silhouette -- matches the
     * pre-Phase-1.35 baseline so the missing-asset case degrades
     * cleanly. */
    /* Phase 1.35c REVERTED 2026-05-28 (Task #114) -- second-pass failure:
     *
     * Shrinking ISLAND.DAT from 512x256 (128 KB; Phase 1.35 v1) to
     * 256x256 (64 KB) fit VRAM bank A0 cleanly but the on-Saturn
     * capture revealed a NEW failure mode: an unexpected orange field
     * fills the perimeter of the title scene (visible Mednafen border
     * region) and the MANIA wordmark ROI gained ~32 percentage points
     * of blue pixels (likely from the NBG1 plane bleeding into the
     * wordmark area). Static-verify of the asset itself was clean
     * (tools/qa_golden/island_simulated.png shows correct silhouette,
     * no flood, no scramble), so the failure mechanism is somewhere
     * in the runtime composition layer (back-color reroute when NBG1
     * cell-mode allocates bank-A0 vs the prior Phase 1.34c state, OR
     * Y-scroll positioning of the island plane causing it to overlap
     * upper ROIs).
     *
     * Per the strict acceptance constraint ("If sprites STILL corrupt
     * at 256x256, REVERT cleanly and produce HONEST hand-off") and
     * the CLAUDE.md §6.1 rule against shipping "looks close" results,
     * the call is disabled again pending a fresh investigation pass.
     * The Phase 1.35d follow-up should investigate (a) why the
     * Mednafen border shifts from blue back-color to orange (suggests
     * back-color register write side-effect), (b) why the MANIA logo
     * ROI gains 32% blue (suggests NBG1 plane Y-scroll is wrong and
     * the plane is covering the wordmark region), (c) the post-A0-
     * allocation state of nbg0_cell/font residents that Phase 1.35
     * may have displaced.
     *
     * Measured findings (samples/qa_phase1_35_island_{60,70,85}.png):
     *   Sonic head ROI    bin%=[0,0,5,10,0,0,3,78] L1=0.200 vs baseline
     *   MANIA logo ROI    bin%=[2,0,0,20,43,0,0,33] L1=0.658 vs baseline
     *   WingShine ROI     bin%=[14,0,6,2,0,0,1,74] L1=0.256 vs baseline
     * (Sonic + WingShine ~60-80% similar but ~10-13% more blue dominance;
     *  MANIA wordmark lost 22% of red+yellow palette to 33% new blue,
     *  consistent with NBG1 plane physically covering it.) */
    /* Phase 1.35d 2026-05-28 (Task #114) -- REVERTED after register-diff.
     *
     * Captured baseline (samples/qa_phase1_31_post_revert.mcs, Phase 1.34c)
     * vs broken (samples/qa_phase1_35d_broken.mcs, Phase 1.35c re-enabled)
     * via Mednafen savestate harness. Diff revealed jo_vdp2_set_nbg1_8bits
     * _image (vdp2.c:527-543) destructively rewrites EIGHT VDP2 registers
     * that Phase 1.34c had carefully composed for multi-layer rendering:
     *
     *   Register     Baseline   Broken     Delta interpretation
     *   CHCTLA       0x1010     0x3210     N0CHCN+N1CHCN reconfig
     *   CHCTLB       0x1002     0x1000     R0CHCN dropped
     *   CYCA0L/U     0x55FE/EE  0x5555/FE  A0 cycle pattern wiped
     *   CYCA1L/U     0xFFFE/EE  0x5555/FE  A1 reassigned to NBG1 reads
     *   CYCB0L       0x1FEE     0xFEEE     B0 NBG0 read slot dropped
     *   CYCB1L       0xFFEE     0xFEEE     B1 lower slot reassigned
     *   CRAOFB       0x0001     0x0000     R0CAOS=1->0 (RBG0 CRAM bank)
     *   PRINA        0x0600     0x0000     NBG1 prio CLOBBERED
     *   PRINB        0x0001     0x0000     NBG2 prio CLOBBERED
     *   PRIR         0x0005     0x0004     RBG0 prio dropped
     *   BGON         0x0052     0x0002     NBG2ON+NBG3ON+R0ON all OFF
     *
     * Artifact-to-delta map:
     *   * Orange perimeter = BGON drops NBG2ON+R0ON; back-color now
     *     visible where clouds+RBG0 used to fill (ST-058-R2 §6.2 p.180).
     *   * MANIA wordmark blue bleed = NBG1 priority chase between the
     *     boot-time PRINA=0 wipe and the per-frame slPriorityNbg1(2)
     *     re-apply; ramp-up flicker shows the 256x256 island bitmap
     *     overlapping the wordmark (the island texture contains the
     *     deep-blue water band at the rows the wordmark occupies).
     *
     * Why counter-writes alone CANNOT fix it (binding acceptance check):
     *   The VRAM cycle pattern damage (CYCA0/A1/B0/B1 = 5 of 8 banks
     *   reassigned) means VDP2 no longer has read-access slots for NBG2
     *   cell data and RBG0 cell data. Per ST-058-R2 §3.3 (p.46-50,
     *   VRAM Access Cycle Pattern) without explicit character-pattern
     *   read slots assigned in CYC*, the corresponding layer cannot
     *   read its cell data from VRAM regardless of BGON enable bit or
     *   priority register value. Re-writing PRINA/PRINB/BGON in
     *   mania_title_3d_backdrop_draw's REPLACE block would assert the
     *   "I want NBG2/RBG0 displayed" intent but VDP2 would have no
     *   character-pattern bandwidth to satisfy it, producing back-color
     *   (orange) in the unreadable regions.
     *
     *   A correct fix requires bypassing jo_vdp2_set_nbg1_8bits_image
     *   entirely and instead manually loading the island cell + map
     *   into a non-conflicting VRAM bank (B1, currently used only by
     *   NBG2 map/cell) with custom slMapNbg1/slPageNbg1/slCharNbg1
     *   calls that don't touch the cycle pattern. That's Phase 1.36
     *   work; the jo wrapper is structurally unsuitable for a four-
     *   layer NBG1+NBG2+RBG0+VDP1 composite.
     *
     * Reference savestates kept for future investigation:
     *   - samples/qa_phase1_31_post_revert.mcs (baseline, NBG1 OFF)
     *   - samples/qa_phase1_35d_broken.mcs    (Phase 1.35c re-enabled)
     */
    /* setup_island_bg();  -- disabled by Phase 1.35d register-diff revert */
    slPriorityNbg1(0);

    /* Phase 1.3 — start the title BGM (CD-DA track 03). Mania-side
     * Music_PlayTrack() is called from TitleSetup_State_Wait when it
     * transitions to State_AnimateUntilFlash, but the Phase 1.3 build
     * still relies on the boot-time start as a safety net so the audio
     * gate (Gate 7) passes even if the state machine timing diverges
     * during early-iteration QA. */
#ifdef JO_COMPILE_WITH_AUDIO_SUPPORT
    jo_audio_play_cd_track(3, 3, true);
#endif

    /* Phase 1.1 — queue the Title scene. */
    (void)rsdk_load_scene_by_name("Title");

    /* Hand the loop to jo. */
    jo_core_add_callback(mania_tick);
    jo_core_add_vblank_callback(fg_vblank);
    jo_core_run();
}
