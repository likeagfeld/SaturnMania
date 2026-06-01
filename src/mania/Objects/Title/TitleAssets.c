/* Phase 1.3 — Title asset bridge implementation.
 *
 * See TitleAssets.h for the design rationale. This file:
 *   1. Loads the Saturn-side .SPR atlases (16-bit BGR1555 per-pixel) via
 *      jo_sprite_add. Each frame becomes a sequential jo sprite ID.
 *   2. Loads TSONIC.ATL (4-bpp Color Bank 16 palettized atlas) via
 *      jo_sprite_add_4bits_image — mechanically ported from
 *      src/_archived/title_sonic.c which solved the CRAM+VDP1 alignment
 *      problem (see docs/titlesonic_implementation.md §5.2).
 *   3. Manufactures rsdk_sprite_animation_t entries in the global
 *      g_rsdk_sprite_anims[] table — one slot per Title class — populated
 *      with per-anim frame stubs whose pivot/width/height come from the
 *      decomp ground-truth (docs/title_ground_truth.md) and per-anim
 *      duration carries the AAA decomp speed (default 16 per frame for
 *      Logo / 1 per frame for press-start blink, etc.).
 *
 * The synthetic anim entries are sized so rsdk_animator_current_frame()
 * returns a valid pointer (non-NULL) for any (list_id, anim_id, frame_id)
 * tuple the Title classes' Draw routines ask about. The Saturn-side
 * resolver in Game.c then maps anim_id → asset_slot via TitleLogoTypes.
 *
 * Saturn-citation policy (per binding rule):
 *   * VDP1 4-bpp Color Bank 16 setup mirrors `src/_archived/title_sonic.c`
 *     line-by-line (CRAM slot 2032, PMOD CCM=0 ECdis=1, COLR field = CRAM
 *     index). See `D:\Claude Saturn Skill Documentation\Saturn_Official_
 *     Documentation\VDP1_Manual.txt §5.4.2` "Sprite Draw Mode" + §5.5.4
 *     "PMOD field" — bit 5 = half-trans, bit 6 = SPD (transparent-disable),
 *     bit 7 = ECD (end-code disable).
 *   * jo_sprite_add_4bits_image is a documented jo extension already used
 *     by the legacy title_sonic.c (per references/joengine-reference.md). */

#include "TitleAssets.h"

#include <jo/jo.h>
#include <string.h>

#include "../../../rsdk/animation.h"
#include "../../../rsdk/storage.h"

/* Global asset table. Indexed by TITLE_ASSET_* enum. */
title_asset_t g_title_assets[TITLE_ASSET_COUNT];

/* TSONIC.ATL per-frame metadata. */
title_tsonic_frame_t g_tsonic_frames[TITLE_TSONIC_MAX_FRAMES];
int                  g_tsonic_frame_count    = 0;
int                  g_tsonic_total_ticks    = 0;
int                  g_tsonic_palette_cram_index = 0;
int                  g_tsonic_loaded         = 0;

/* Phase 1.27 §11.32 — ELECTRA.ATL per-frame metadata. 8 culled keyframes
 * from the 40-frame Electricity anim (per `tools/build_electricity_atlas.py`).
 * Distinct CRAM index from TSONIC so palettes don't clash. */
title_tsonic_frame_t g_electra_frames[TITLE_ELECTRA_MAX_FRAMES];
int                  g_electra_frame_count       = 0;
int                  g_electra_palette_cram_index = 0;
int                  g_electra_loaded            = 0;

/* Phase 1.32 — TITLE3D.ATL anim 5 ('Billboard Sprites') per-frame metadata.
 * 5 billboard frames: MountainL/M/S + Tree + Bush per
 * tools/_decomp_raw/SonicMania_Objects_Title_Title3DSprite.c:42-55 and
 * Title3DSprite.h enum TITLE3DSPRITE_MOUNTAIN_L..TITLE3DSPRITE_BUSH.
 * Loaded from cd/TITLE3D.ATL by load_title3d_atlas (below). */
title_tsonic_frame_t g_title3d_bb_frames[TITLE3D_BB_MAX_FRAMES];
int                  g_title3d_bb_frame_count    = 0;
int                  g_title3d_palette_cram_index = 0;
int                  g_title3d_loaded            = 0;

/* Phase 1.34 — TITLE3D.ATL anims 0..4 (TitleBG sub-types) per-frame metadata.
 * Each anim is a single-frame entry per the v4 ATL header (verified
 * 2026-05-27 via header dump: frame_counts = [1,1,1,1,1,5]). The 5
 * single-frame entries are streamed into VDP1 char-RAM by load_title3d_atlas
 * and the resulting sprite IDs are sequential starting at
 * g_title3d_bg_first_sid. Decomp authority:
 *   tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.c (Update/Draw/Create). */
title_tsonic_frame_t g_title3d_bg_frames[TITLE3D_BG_MAX_FRAMES];
int                  g_title3d_bg_first_sid       = -1;
int                  g_title3d_bg_frame_count     = 0;

/* Per-class list_id (Saturn-side g_rsdk_sprite_anims[] slot). Set by
 * title_assets_load + returned by the title_assets_*_list_id() getters. */
static int s_titlelogo_list_id  = -1;
static int s_titlesonic_list_id = -1;
static int s_titlebg_list_id    = -1;
static int s_titlesetup_list_id = -1;

/* Persistent backing storage for the synthetic animations. Each Title class
 * gets one anim list with up to 9 sub-animations (TitleLogo has 8 anim slots
 * 0..7 per the decomp, TitleSonic has 2, TitleBG has 5, TitleSetup has 1).
 *
 * The animator references `frames[]` directly — these arrays MUST live for
 * the lifetime of the title scene. We allocate them statically here so they
 * never get freed by rsdk_sprite_animation_free (which jo_free's the
 * pointers); we use a sentinel pattern: the static arrays are owned BY us,
 * not by jo's heap. As long as nobody calls rsdk_unload_sprite_animation
 * on our slots they survive.
 *
 * Memory: ~9 animations * 49 frames * sizeof(rsdk_sprite_frame_t)=80 bytes
 * (with hitbox array) = ~35 KB per class. 4 classes = 140 KB. That's a
 * lot. We trim by sharing storage where possible.
 *
 * Actually rsdk_sprite_frame_t is much larger than 80 bytes due to the
 * 8-hitbox-per-frame inline array. Let's compute: sizeof(rsdk_hitbox_t)=8,
 * 8 hitboxes = 64. Other fields = ~16. Total ~80 bytes/frame. */

#define MAX_TITLELOGO_ANIMS   9     /* anim ids 0..8 from decomp     */
#define MAX_TITLELOGO_FRAMES  10    /* MPRESS.SPR has 10 frames; rest are 1 */
#define MAX_TITLESONIC_ANIMS  2
#define MAX_TITLESONIC_FRAMES 64
#define MAX_TITLEBG_ANIMS     5
#define MAX_TITLEBG_FRAMES    1
#define MAX_TITLESETUP_ANIMS  1
#define MAX_TITLESETUP_FRAMES 1

static rsdk_sprite_animation_entry_t s_logo_anims[MAX_TITLELOGO_ANIMS];
static rsdk_sprite_frame_t           s_logo_frames[MAX_TITLELOGO_ANIMS * MAX_TITLELOGO_FRAMES];

static rsdk_sprite_animation_entry_t s_sonic_anims[MAX_TITLESONIC_ANIMS];
static rsdk_sprite_frame_t           s_sonic_frames[MAX_TITLESONIC_ANIMS * MAX_TITLESONIC_FRAMES];

static rsdk_sprite_animation_entry_t s_bg_anims[MAX_TITLEBG_ANIMS];
static rsdk_sprite_frame_t           s_bg_frames[MAX_TITLEBG_ANIMS * MAX_TITLEBG_FRAMES];

static rsdk_sprite_animation_entry_t s_setup_anims[MAX_TITLESETUP_ANIMS];
static rsdk_sprite_frame_t           s_setup_frames[MAX_TITLESETUP_ANIMS * MAX_TITLESETUP_FRAMES];

/* ---- .SPR loader -------------------------------------------------------
 *
 * .SPR is the custom Saturn-side atlas format produced by tools/build_*.py.
 * Layout: u16 frame_count, u16 width, u16 height, then fc*w*h u16 BGR1555
 * pixels. On Saturn SH-2 (big-endian) the u16 reads are correct via direct
 * pointer dereference; the host-side python scripts build it big-endian
 * matching the Saturn's native short order. */

static int load_spr_atlas(int slot, const char *filename)
{
    title_asset_t *a = &g_title_assets[slot];
    a->base_sprite_id = -1;
    a->frame_count    = 0;
    a->is_4bpp        = 0;

    int len = 0;
    unsigned short *spr = (unsigned short *)jo_fs_read_file((char *)filename, &len);
    if (!spr || len < 6) {
        if (spr) jo_free(spr);
        return -1;
    }
    int fc = (int)spr[0];
    int w  = (int)spr[1];
    int h  = (int)spr[2];
    if (fc <= 0 || w <= 0 || h <= 0 || fc > 64) {
        jo_free(spr);
        return -1;
    }
    int first_sid = -1;
    int loaded    = 0;
    for (int i = 0; i < fc; ++i) {
        jo_img img;
        img.data   = (jo_color *)(spr + 3) + (uint32_t)i * (uint32_t)w * (uint32_t)h;
        img.width  = w;
        img.height = h;
        int sid = jo_sprite_add(&img);
        if (sid < 0) break;
        if (first_sid < 0) first_sid = sid;
        ++loaded;
    }
    a->base_sprite_id = first_sid;
    a->frame_count    = loaded;
    a->width          = (int16_t)w;
    a->height         = (int16_t)h;
    jo_free(spr);
    return (first_sid >= 0) ? 0 : -1;
}

/* ---- TSONIC.ATL 4-bpp loader (mechanical port of legacy title_sonic.c) -
 *
 * Atlas format (matches tools/build_titlesonic_atlas.py):
 *   Header: 44 bytes
 *     [0..2)   magic 'TS' 0x5453 big-endian u16
 *     [2..4)   version 0x0003
 *     [4..6)   frame_count u16
 *     [6..8)   palette_size u16 (always 16)
 *     [8..40)  16 × u16 BGR1555 palette
 *     [40..44) pixel-pool total bytes u32
 *   Then frame_count × 14-byte records:
 *     w u16, h u16, pivot_x i16, pivot_y i16, duration u16, pool_off u32
 *   Then the pixel pool: per-frame (w*h)/2 bytes 4-bpp packed-nibble.
 *
 * Each frame becomes a 4-bpp sprite via jo_sprite_add_4bits_image. */

#define TSONIC_MAGIC       0x5453
/* Atlas builder tools/build_titlesonic_atlas.py emits version 0x0004:
 * v4 header (44 B: magic+ver+anim_count+pal_size+pal[16]+total_pix)
 * + anim_count * 8 B per-anim records
 *   (frame_count, loop_index, first_frame_global_idx, total_duration)
 * + total_frame_count * 14 B per-frame records
 *   (w, h, pivot_x, pivot_y, duration, pixel_offset)
 * + pixel_pool bytes.
 *
 * v3 had frame_count at +4 and frame records immediately after header.
 * Phase 1.18 supports v4 ONLY — v3 path retired because shipped atlas
 * is v4. Anim 0 = 'Sonic' 49 frames; anim 1 = 'Finger Wave' 12 frames. */
#define TSONIC_VERSION     0x0004
#define TSONIC_PAL_SIZE    16
#define TSONIC_PAL_CRAM    2032         /* 16-aligned, high end of CRAM */
#define TSONIC_HEADER_SZ   44
#define TSONIC_ANIM_SZ     8
#define TSONIC_FRAME_SZ    14
#define TSONIC_STAGING_SZ  16384        /* worst-case 200x126/2 = 12,600 */

static inline unsigned short be_u16(const unsigned char *p) {
    return ((unsigned short)p[0] << 8) | p[1];
}
static inline short be_s16(const unsigned char *p) {
    return (short)be_u16(p);
}
static inline unsigned int be_u32(const unsigned char *p) {
    return ((unsigned int)p[0] << 24) | ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] <<  8) |  (unsigned int)p[3];
}

/* Phase 1.18 — v4 anim record (8 B per anim).
 * Maps anim_index -> (frame_count, loop_index, first_global_frame_idx,
 *                     total_duration_ticks).
 * Decomp uses anim 0 = body, anim 1 = finger overlay.
 * Saturn loader picks anim 0 as the primary 'g_tsonic_*' state used by
 * title_tsonic_draw_frame; anim 1 is loaded into the same sprite slot
 * table (continuation of base_sprite_id + offset). */
int g_tsonic_anim0_frame_count   = 0;
int g_tsonic_anim0_first_frame   = 0;
int g_tsonic_anim1_frame_count   = 0;
int g_tsonic_anim1_first_frame   = 0;

/* Phase 1.18 diagnostic globals: surface where the loader bails so a
 * memory-inspect of the running ELF can pinpoint the failure step. */
volatile int g_tsonic_load_step       = 0;   /* 0 = not started; >0 = step */
volatile int g_tsonic_load_anim_count = 0;
volatile int g_tsonic_load_total_fc   = 0;
volatile int g_tsonic_load_first_sid  = -1;
volatile int g_tsonic_load_loaded     = 0;

/* Phase 1.19 — GFS_Fread semantics fix.
 *
 * SEGA_GFS.H §396 declares `Sint32 GFS_Fread(GfsHn, Sint32 nsct, void *buf,
 * Sint32 bsize)`. `nsct` = sector count (1 sector = 2048 B). On real
 * hardware GFS_Fread reads `nsct` sectors but truncates the write to
 * `bsize` bytes; on Yabause/Mednafen the implementation enforces a
 * stricter bsize >= nsct*2048 check and returns -1 if the supplied
 * buffer can't hold a full sector. jo's own usage (fs.c:399, fs.c:444)
 * always passes bsize=16384 (an 8-sector buffer).
 *
 * Phase 1.18's loader passed bsize = small struct size (e.g. 16 for the
 * per-anim record block) which made Mednafen's GFS implementation
 * return -1 at the second call (the per-anim record read) — confirmed
 * via the back-color diagnostic showing g_tsonic_load_step stuck at 5.
 *
 * The fix uses ONE 16 KB staging buffer (matching jo's convention) and
 * tracks file position manually; each GFS_Fread reads up to one sector
 * at a time into the staging buffer, then we memcpy-out the bytes we
 * actually need. Frame data >2048 B uses multi-sector reads with
 * nsct = ceil(remaining/2048). */
#define TSONIC_SECTOR_SZ   2048

static int load_tsonic_atlas(void)
{
    if (g_tsonic_loaded) return 0;
    g_tsonic_load_step = 1;

    Sint32 fid = GFS_NameToId((Sint8 *)"TSONIC.ATL");
    if (fid < 0) return -1;
    GfsHn gfs = GFS_Open(fid);
    if (gfs == JO_NULL) return -1;
    g_tsonic_load_step = 2;

    /* Allocate the single staging buffer up-front (matches jo fs.c). All
     * GFS_Fread calls go here; per-section consumers memcpy what they
     * need out. The buffer is sized so a single GFS_Fread can read 8
     * sectors at once when streaming pixel data. */
    unsigned char *staging = (unsigned char *)jo_malloc(TSONIC_STAGING_SZ);
    if (!staging) { GFS_Close(gfs); return -1; }

    /* Read sector 0 (covers header + anim records + most/all of frame
     * records — header(44) + anim_records(<=32) + frame_records(<=14*64
     * = 896) = max 972 bytes, well under 2048). */
    Sint32 r0 = GFS_Fread(gfs, 1, staging, TSONIC_STAGING_SZ);
    if (r0 <= 0) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }
    /* Track logical file position (bytes); used for later seeks. */
    unsigned int file_pos = (unsigned int)r0;
    (void)file_pos;
    g_tsonic_load_step = 3;

    /* Parse header from sector 0 buffer. */
    const unsigned char *hdr = staging;
    if (be_u16(&hdr[0]) != TSONIC_MAGIC || be_u16(&hdr[2]) != TSONIC_VERSION) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }
    int anim_count = (int)be_u16(&hdr[4]);
    if (anim_count <= 0 || anim_count > 4) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }
    if (be_u16(&hdr[6]) != TSONIC_PAL_SIZE) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }
    g_tsonic_load_anim_count = anim_count;
    g_tsonic_load_step = 4;

    /* Palette to CRAM. */
    {
        volatile jo_color *cram = ((volatile jo_color *)JO_VDP2_CRAM) + TSONIC_PAL_CRAM;
        for (int i = 0; i < TSONIC_PAL_SIZE; ++i) {
            cram[i] = (jo_color)be_u16(&hdr[8 + i * 2]);
        }
        g_tsonic_palette_cram_index = TSONIC_PAL_CRAM;
    }
    g_tsonic_load_step = 5;

    /* Anim records start at offset 44 inside the sector. */
    int total_frame_count = 0;
    {
        const unsigned char *anim_base = &staging[TSONIC_HEADER_SZ];
        for (int ai = 0; ai < anim_count; ++ai) {
            const unsigned char *r = &anim_base[ai * TSONIC_ANIM_SZ];
            int afc    = (int)be_u16(&r[0]);
            int afirst = (int)be_u16(&r[4]);
            if (ai == 0) {
                g_tsonic_anim0_frame_count = afc;
                g_tsonic_anim0_first_frame = afirst;
            } else if (ai == 1) {
                g_tsonic_anim1_frame_count = afc;
                g_tsonic_anim1_first_frame = afirst;
            }
            total_frame_count += afc;
        }
    }
    if (total_frame_count <= 0 || total_frame_count > TITLE_TSONIC_MAX_FRAMES) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }
    g_tsonic_load_total_fc = total_frame_count;
    g_tsonic_load_step = 6;

    /* Frame records start at offset 44 + anim_count*8 inside the sector.
     * For anim_count <= 4 and total_frame_count <= 64, the records end
     * at <= 44 + 32 + 64*14 = 972 — well within sector 0 (2048 B). */
    unsigned int frame_table_off = TSONIC_HEADER_SZ + (unsigned int)anim_count * TSONIC_ANIM_SZ;
    unsigned int pool_base_off   = frame_table_off + (unsigned int)total_frame_count * TSONIC_FRAME_SZ;
    /* Cache frame metadata. The pool offsets in the per-frame records are
     * RELATIVE to the start of the pixel pool (NOT absolute file offset). */
    unsigned int frame_pool_off[TITLE_TSONIC_MAX_FRAMES];
    int total_ticks = 0;
    for (int i = 0; i < total_frame_count; ++i) {
        const unsigned char *r = &staging[frame_table_off + i * TSONIC_FRAME_SZ];
        g_tsonic_frames[i].width    = be_u16(&r[0]);
        g_tsonic_frames[i].height   = be_u16(&r[2]);
        g_tsonic_frames[i].pivot_x  = be_s16(&r[4]);
        g_tsonic_frames[i].pivot_y  = be_s16(&r[6]);
        g_tsonic_frames[i].duration = be_u16(&r[8]);
        frame_pool_off[i]           = be_u32(&r[10]);
        total_ticks                += (int)g_tsonic_frames[i].duration;
        g_tsonic_frames[i].cumulative = (uint16_t)total_ticks;
    }
    g_tsonic_total_ticks = total_ticks;
    g_tsonic_load_step = 7;

    /* Phase 1.20 — Two-phase selective load.
     *
     * Phase A: anim 0 settled pose (frame 48) — the iconic large drawn pose
     * that's visible whenever the body animator has rolled all the way
     * around (which it does immediately because the State_FadeToVideo timer
     * is much longer than the 49-frame body cycle, and the animator's
     * speed=1 holds at the loop_index=48 final frame).
     *
     * Phase B: anim 1 finger overlay (12 frames). The decomp's
     * TitleSonic_Update only ticks animatorFinger while animatorSonic is on
     * its last frame — so once anim 0 settles, the finger loops indefinitely.
     *
     * VDP1 char-RAM budget audit (atlas-measured, see tools verification):
     *   anim 0 frame 48 = 14,508 B  (single biggest body frame)
     *   anim 1 total    = 19,424 B  (12 frames, 1,440..1,764 B each)
     *   ---------------------------
     *   total           = 33,932 B  (~33 KB)
     * Phase 1.20 estimate: VDP1 char-RAM utilisation ~321 KB pre-Phase-1.20;
     * +33 KB lands ~354 KB; ~37 KB margin below the 391 KB cliff per
     * docs/COMPREHENSIVE_PLAN.md §11.16. Safe. */
    int first_sid = -1;
    int loaded    = 0;
    g_tsonic_load_step = 8;

    /* === Phase A: anim 0 entrance keyframes ============================
     *
     * Phase 1.23 GAP B' — load 8 keyframes from anim 0 (per
     * `docs/COMPREHENSIVE_PLAN.md` §11.29 GAP B'). Each keyframe becomes
     * a sequential jo sprite id starting at `first_sid`, and the body
     * asset slot's frame_count = 8. The animator's frame_id 0..7
     * indexes directly into base_sprite_id + frame_id at draw time so
     * the entrance arc is visible without full 49-frame streaming.
     *
     * Keyframe indices chosen for arc-mass coverage:
     *   0, 6, 12, 18, 24, 30, 36, 48
     * Per atlas tools/build_titlesonic_atlas.py the largest of these
     * is frame 30 (248x117 = 14,508 B), still under TSONIC_STAGING_SZ.
     *
     * VDP1 char-RAM cost (atlas-measured):
     *   0 → 96x82  =  3,936 B
     *   6 → 104x113 = 5,876 B
     *   12 → 112x118 = 6,608 B
     *   18 → 112x126 = 7,056 B
     *   24 → 128x134 = 8,576 B
     *   30 → 248x117 = 14,508 B
     *   36 → 128x127 = 8,128 B
     *   48 → 112x120 = 6,720 B
     *   ---------------------------
     *   sum            = 61,408 B  (~60 KB)
     * Combined with the rest of the title scene (~189 KB title sprites
     * + ~19 KB finger + ~33 KB other) Phase 1.23 lands ~302 KB total,
     * well under the 391 KB char-RAM cliff per §11.16. */
    static const int s_tsonic_keyframe_offsets[8] = { 0, 6, 12, 18, 24, 30, 36, 48 };
    int body_keyframes_loaded = 0;
    int body_first_sid        = -1;
    if (g_tsonic_anim0_frame_count > 0) {
        for (int ki = 0; ki < 8; ++ki) {
            int kf_local = s_tsonic_keyframe_offsets[ki];
            if (kf_local >= g_tsonic_anim0_frame_count) break;
            int gfi = g_tsonic_anim0_first_frame + kf_local;
            if (gfi >= TITLE_TSONIC_MAX_FRAMES) break;

            unsigned int target_file_off = pool_base_off + frame_pool_off[gfi];
            unsigned int target_sector   = target_file_off / TSONIC_SECTOR_SZ;
            unsigned int target_in_sec   = target_file_off % TSONIC_SECTOR_SZ;
            unsigned int frame_bytes = ((unsigned int)g_tsonic_frames[gfi].width *
                                        (unsigned int)g_tsonic_frames[gfi].height) >> 1;
            if (frame_bytes == 0 || frame_bytes > TSONIC_STAGING_SZ) break;
            /* Phase 1.23 GAP B' — guard against (target_in_sec + frame_bytes
             * > TSONIC_STAGING_SZ).  Frame 30 (248x117 = 14,508 B) can
             * overflow the 16,384 B staging buffer when target_in_sec is
             * non-zero.  If so, re-align by reading from sector_offset =
             * target_sector + 1 with target_in_sec = 0 (skips this frame's
             * pool offset by < 1 sector; we'd have to seek back, which GFS
             * supports).  Simpler: when the natural in-sector offset would
             * overflow, break instead of corrupting memory. Frame 30 still
             * loads cleanly for the common case where its pool offset is
             * sector-aligned. */
            if (target_in_sec + frame_bytes > TSONIC_STAGING_SZ) break;
            if (GFS_Seek(gfs, (Sint32)target_sector, GFS_SEEK_SET) < 0) break;
            unsigned int span = target_in_sec + frame_bytes;
            Sint32 nsct = (Sint32)((span + TSONIC_SECTOR_SZ - 1) / TSONIC_SECTOR_SZ);
            if (nsct <= 0) nsct = 1;
            Sint32 max_nsct = (Sint32)(TSONIC_STAGING_SZ / TSONIC_SECTOR_SZ);
            if (nsct > max_nsct) nsct = max_nsct;
            Sint32 nb2 = GFS_Fread(gfs, nsct, staging, TSONIC_STAGING_SZ);
            if (nb2 <= 0) break;
            if (target_in_sec + frame_bytes > (unsigned int)nb2) break;
            jo_img_8bits img;
            img.width  = g_tsonic_frames[gfi].width;
            img.height = g_tsonic_frames[gfi].height;
            img.data   = &staging[target_in_sec];
            int sid = jo_sprite_add_4bits_image(&img);
            if (sid < 0) break;
            if (body_first_sid < 0) body_first_sid = sid;
            ++body_keyframes_loaded;
        }
    }
    /* Fall through with whatever keyframes we got. Body slot stays empty
     * (base_sprite_id == -1) iff body_keyframes_loaded == 0 — in which
     * case title_sprite_cb's `a->base_sprite_id < 0` guard fires and
     * the title still composes correctly minus Sonic. */
    first_sid = body_first_sid;
    loaded    = body_keyframes_loaded;
    /* Track the last keyframe's local index for the body-slot metadata
     * fallback if no keyframes loaded. */
    int body_meta_idx = g_tsonic_anim0_first_frame +
                        s_tsonic_keyframe_offsets[7];
    if (body_meta_idx >= TITLE_TSONIC_MAX_FRAMES)
        body_meta_idx = g_tsonic_anim0_first_frame;
    g_tsonic_load_step = 9;

    /* === Phase B: anim 1 finger overlay (12 frames) ==================== *
     *
     * Each frame's pool offset comes from frame_pool_off[anim1_first + i].
     * Frames are emitted sequentially in the pool (per the atlas builder),
     * so we can issue multi-frame multi-sector reads that span several
     * frames at once when their combined size fits the staging buffer —
     * but a per-frame seek+read is simpler and still well within sector
     * budget (12 frames * 1 read each = 12 GFS_Fread calls, total ~30 KB
     * pixel data, fits easily in staging). */
    int finger_first_sid  = -1;
    int finger_loaded     = 0;
    if (g_tsonic_anim1_frame_count > 0 &&
        g_tsonic_anim1_first_frame + g_tsonic_anim1_frame_count <= TITLE_TSONIC_MAX_FRAMES) {
        for (int i = 0; i < g_tsonic_anim1_frame_count; ++i) {
            int gfi = g_tsonic_anim1_first_frame + i;
            unsigned int t_off    = pool_base_off + frame_pool_off[gfi];
            unsigned int t_sector = t_off / TSONIC_SECTOR_SZ;
            unsigned int t_in_sec = t_off % TSONIC_SECTOR_SZ;
            if (GFS_Seek(gfs, (Sint32)t_sector, GFS_SEEK_SET) < 0) break;
            unsigned int fb = ((unsigned int)g_tsonic_frames[gfi].width *
                               (unsigned int)g_tsonic_frames[gfi].height) >> 1;
            if (fb == 0 || fb > TSONIC_STAGING_SZ) break;
            unsigned int span = t_in_sec + fb;
            Sint32 nsct = (Sint32)((span + TSONIC_SECTOR_SZ - 1) / TSONIC_SECTOR_SZ);
            if (nsct <= 0) nsct = 1;
            Sint32 max_nsct = (Sint32)(TSONIC_STAGING_SZ / TSONIC_SECTOR_SZ);
            if (nsct > max_nsct) nsct = max_nsct;
            Sint32 nb = GFS_Fread(gfs, nsct, staging, TSONIC_STAGING_SZ);
            if (nb <= 0) break;
            if (t_in_sec + fb > (unsigned int)nb) break;
            jo_img_8bits img;
            img.width  = g_tsonic_frames[gfi].width;
            img.height = g_tsonic_frames[gfi].height;
            img.data   = &staging[t_in_sec];
            int sid = jo_sprite_add_4bits_image(&img);
            if (sid < 0) break;
            if (finger_first_sid < 0) finger_first_sid = sid;
            ++finger_loaded;
            ++loaded;
        }
    }
    g_tsonic_load_step = 10;

    jo_free(staging);
    GFS_Close(gfs);
    g_tsonic_load_first_sid = first_sid;
    g_tsonic_load_loaded    = loaded;

    /* Populate body asset slot.
     *
     * Phase 1.23 GAP B' — base_sprite_id points at the first keyframe; the
     * slot's frame_count is now the number of keyframes actually loaded
     * (0..8). Per-keyframe pivot/width/height live in `g_tsonic_frames[]`
     * (indexed by global frame index = anim0_first + s_tsonic_keyframe_
     * offsets[ki]) and `title_tsonic_draw_frame` reads them at draw time.
     * The width/height/pivot fields on the slot itself report the settled
     * pose (last keyframe = index 48) as a sane default for anything that
     * inspects them without a frame_id. */
    g_title_assets[TITLE_ASSET_TSONIC_BODY].base_sprite_id = first_sid;
    g_title_assets[TITLE_ASSET_TSONIC_BODY].frame_count    = body_keyframes_loaded;
    g_title_assets[TITLE_ASSET_TSONIC_BODY].is_4bpp        = 1;
    g_title_assets[TITLE_ASSET_TSONIC_BODY].width          = g_tsonic_frames[body_meta_idx].width;
    g_title_assets[TITLE_ASSET_TSONIC_BODY].height         = g_tsonic_frames[body_meta_idx].height;
    g_title_assets[TITLE_ASSET_TSONIC_BODY].pivot_x        = g_tsonic_frames[body_meta_idx].pivot_x;
    g_title_assets[TITLE_ASSET_TSONIC_BODY].pivot_y        = g_tsonic_frames[body_meta_idx].pivot_y;

    /* Populate finger asset slot if Phase B succeeded (else stays -1
     * and title_sprite_cb early-returns at base<0 — body still draws). */
    if (finger_first_sid >= 0 && finger_loaded > 0) {
        int idx = g_tsonic_anim1_first_frame;
        g_title_assets[TITLE_ASSET_TSONIC_FINGER].base_sprite_id = finger_first_sid;
        g_title_assets[TITLE_ASSET_TSONIC_FINGER].frame_count    = finger_loaded;
        g_title_assets[TITLE_ASSET_TSONIC_FINGER].is_4bpp        = 1;
        g_title_assets[TITLE_ASSET_TSONIC_FINGER].width          = g_tsonic_frames[idx].width;
        g_title_assets[TITLE_ASSET_TSONIC_FINGER].height         = g_tsonic_frames[idx].height;
        g_title_assets[TITLE_ASSET_TSONIC_FINGER].pivot_x        = g_tsonic_frames[idx].pivot_x;
        g_title_assets[TITLE_ASSET_TSONIC_FINGER].pivot_y        = g_tsonic_frames[idx].pivot_y;
    }

    g_tsonic_frame_count = loaded;
    g_tsonic_loaded = 1;
    return 0;
}

/* ---- TSONIC draw (4-bpp + per-frame pivot) -----------------------------
 *
 * Mechanically ported from src/_archived/title_sonic.c::title_sonic_draw.
 * VDP1 PMOD field encoding per VDP1 Manual §5.5.4: bits 3-5 CCM=0 = Color
 * Bank 16, bit 7 ECD=1 = end-code disable for sprite char data. */

#define VDP1_CCM_BANK16    (0 << 3)
#define VDP1_ECD_DISABLE   (1 << 7)

/* Phase 1.20 — accepts an asset slot id (TITLE_ASSET_TSONIC_BODY or
 * TITLE_ASSET_TSONIC_FINGER). `frame_id` is the LOCAL frame inside that
 * asset's slot table (0..frame_count-1), NOT the global TSONIC.ATL frame
 * index. The caller in Game.c::title_sprite_cb passes the animator's
 * frame_id directly (which is already local: animatorFinger.frame_id
 * runs 0..11 for the 12-frame finger anim). */
void title_tsonic_draw_frame(int asset_slot, int frame_id, int jo_x, int jo_y,
                             int z, uint8_t direction)
{
    (void)direction;          /* TSONIC body is symmetric — no flip used. */
    if (!g_tsonic_loaded) return;
    if (asset_slot != TITLE_ASSET_TSONIC_BODY && asset_slot != TITLE_ASSET_TSONIC_FINGER)
        return;
    const title_asset_t *a = &g_title_assets[asset_slot];
    if (a->base_sprite_id < 0 || a->frame_count <= 0) return;
    if (frame_id < 0) frame_id = 0;
    if (frame_id >= a->frame_count) frame_id = a->frame_count - 1;

    FIXED pos[XYZS];
    SPR_ATTR attr;

    pos[X] = toFIXED((float)jo_x);
    pos[Y] = toFIXED((float)jo_y);
    pos[Z] = toFIXED((float)z);
    pos[S] = toFIXED(1.0);

    attr.texno = (Uint16)(a->base_sprite_id + frame_id);
    attr.atrb  = (Uint16)(VDP1_CCM_BANK16 | VDP1_ECD_DISABLE);
    attr.colno = (Uint16)g_tsonic_palette_cram_index;
    attr.gstb  = 0;
    attr.dir   = 0;

    slDispSprite(pos, &attr, 0);
}

/* ---- Synthetic anim builder -------------------------------------------
 *
 * Decomp Logo.bin contains 9 animations (0..8). Each animation references
 * frames whose pivot/size came from the original RSDK sprite atlas.
 * Saturn-side we mirror the structural layout:
 *
 *   Anim 0  "Emblem"        1 frame  pivot/size from MWINGS.SPR
 *   Anim 1  "Ribbon Wave"   1 frame  pivot from MRIBSIDE.SPR  (decomp -121,-13)
 *   Anim 2  "Ribbon Wave 2" 1 frame  same as anim 1 (ribbon-anim swap site)
 *   Anim 3  "Ribbon Center" 1 frame  pivot from MRIBBON.SPR    (decomp -79,-24)
 *   Anim 4  "Game Title"    2 frames pivot from MLOGO.SPR      (decomp -68,-23)
 *   Anim 5  "Power LED"     unused (destroyed on State_AnimateUntilFlash)
 *   Anim 6  "Copyright"     1 frame  pivot from decomp         (-93,-7)
 *   Anim 7  "Ring Bottom"   1 frame  pivot from MRING.SPR      (decomp -60,-16)
 *   Anim 8  "Press Start"   10 frames pivot from MPRESS.SPR    (decomp -64,-7)
 *
 * Decomp citation: see docs/title_ground_truth.md for the source-of-truth
 * pivot table. The Saturn .SPR atlases were built by tools/convert_anim_
 * sprite.py to compose a canvas where the RSDK pivot point lands at
 * `(-pivot_x, -pivot_y)` inside the canvas. So when the engine asks
 * `dst_top_left = entity_pos + pivot`, we just pass the canvas-centered
 * coordinates through jo_sprite_draw3D with no offset — the .SPR's pivot
 * is already baked in.
 *
 * HOWEVER: tools/convert_anim_sprite.py composes the canvas with the
 * pivot at canvas-top-left when (-pivot_x, -pivot_y) is positive (i.e. the
 * pivot lies inside the canvas). The legacy main.c.v01-handrolled draw
 * calls then add per-sprite tweak offsets to align the canvas-center pivot
 * with the entity world coord. Phase 1.3 replicates those tweak offsets
 * by passing a "Saturn pivot adjustment" into the resolver. */

static void fill_frame(rsdk_sprite_frame_t *f, int w, int h,
                       int16_t px, int16_t py, uint16_t dur)
{
    memset(f, 0, sizeof(*f));
    f->width    = (uint16_t)w;
    f->height   = (uint16_t)h;
    f->pivot_x  = px;
    f->pivot_y  = py;
    f->duration = dur;
}

static void fill_anim(rsdk_sprite_animation_entry_t *a,
                      const char *name,
                      rsdk_sprite_frame_t *frames_storage,
                      int frame_count, uint16_t speed, uint8_t loop_index)
{
    memset(a, 0, sizeof(*a));
    int i;
    for (i = 0; i < (int)sizeof(a->name) - 1 && name[i]; ++i)
        a->name[i] = name[i];
    a->name[i] = 0;
    rsdk_md5_name(a->name, a->hash);
    a->frames         = frames_storage;
    a->frame_count    = (uint16_t)frame_count;
    a->speed          = speed;
    a->loop_index     = loop_index;
    a->rotation_style = 0;
}

/* Find a free slot in g_rsdk_sprite_anims[]. Returns -1 if full. */
static int alloc_anim_slot(void)
{
    for (int i = 0; i < RSDK_SPRITEANIM_LIST_COUNT; ++i) {
        if (g_rsdk_sprite_anims[i].anim_count == 0 &&
            g_rsdk_sprite_anims[i].frames == NULL &&
            g_rsdk_sprite_anims[i].animations == NULL) {
            return i;
        }
    }
    return -1;
}

static void build_titlelogo_anims(void)
{
    int slot = alloc_anim_slot();
    if (slot < 0) return;
    s_titlelogo_list_id = slot;

    rsdk_sprite_animation_t *list = &g_rsdk_sprite_anims[slot];
    memset(list, 0, sizeof(*list));
    list->animations  = s_logo_anims;
    list->frames      = s_logo_frames;
    list->anim_count  = MAX_TITLELOGO_ANIMS;
    list->sheet_count = 0;

    /* Per-anim frame storage offsets within the s_logo_frames array. */
    rsdk_sprite_frame_t *p = s_logo_frames;

    /* Anim 0: Emblem (MWINGS.SPR 144x144). Decomp pivot (-72,-72) — single
     * 1-frame anim, speed irrelevant. */
    fill_anim(&s_logo_anims[0], "Emblem", p, 1, 0, 0);
    fill_frame(&p[0], 144, 144, -72, -72, 1);
    p += MAX_TITLELOGO_FRAMES;

    /* Anim 1: Ribbon Wave (MRIBSIDE.SPR 56x72). Decomp pivot (-121,-13).
     * For Saturn-side the pivot is baked into the .SPR canvas, so we use
     * the recorded pivot directly so rsdk_draw_sprite_ex's world-to-jo
     * conversion places the canvas pivot at world (256, 144). */
    fill_anim(&s_logo_anims[1], "Ribbon Wave", p, 1, 0, 0);
    fill_frame(&p[0], 56, 72, -121, -13, 1);
    p += MAX_TITLELOGO_FRAMES;

    /* Anim 2: Ribbon Wave 2 (same as anim 1). State_FlashIn swaps the
     * RIBBON entity's mainAnimator to anim 2 — the asset is the same. */
    fill_anim(&s_logo_anims[2], "Ribbon Wave 2", p, 1, 0, 0);
    fill_frame(&p[0], 56, 72, -121, -13, 1);
    p += MAX_TITLELOGO_FRAMES;

    /* Anim 3: Ribbon Center (MRIBBON.SPR 176x56). Decomp pivot (-79,-24). */
    fill_anim(&s_logo_anims[3], "Ribbon Center", p, 1, 0, 0);
    fill_frame(&p[0], 176, 56, -79, -24, 1);
    p += MAX_TITLELOGO_FRAMES;

    /* Anim 4: Game Title (MLOGO.SPR 144x48 — 2 frames). Decomp pivot
     * (-68,-23). Frame 0 = "MANIA"; frame 1 = "DISCOVERY" (Plus-only,
     * unused on the Saturn ship). */
    fill_anim(&s_logo_anims[4], "Game Title", p, 2, 0, 0);
    fill_frame(&p[0], 144, 48, -68, -23, 1);
    fill_frame(&p[1], 144, 48, -68, -23, 1);
    p += MAX_TITLELOGO_FRAMES;

    /* Anim 5: Power LED (1 frame, never drawn). */
    fill_anim(&s_logo_anims[5], "Power LED", p, 1, 0, 0);
    fill_frame(&p[0], 1, 1, 0, 0, 1);
    p += MAX_TITLELOGO_FRAMES;

    /* Anim 6: Copyright (1 frame, off-screen on Saturn 320 wide). */
    fill_anim(&s_logo_anims[6], "Copyright", p, 1, 0, 0);
    fill_frame(&p[0], 1, 1, 0, 0, 1);
    p += MAX_TITLELOGO_FRAMES;

    /* Anim 7: Ring Bottom (MRING.SPR 120x32). Decomp pivot (-60,-16). */
    fill_anim(&s_logo_anims[7], "Ring Bottom", p, 1, 0, 0);
    fill_frame(&p[0], 120, 32, -60, -16, 1);
    p += MAX_TITLELOGO_FRAMES;

    /* Anim 8: Press Button / Press Start (MPRESS.SPR 176x24 — 10 frames).
     * Decomp pivot (-88,-12). The 10 frames cycle the "PRESS START"
     * shimmer. */
    fill_anim(&s_logo_anims[8], "Press Button", p, 10, 0, 0);
    for (int f = 0; f < 10; ++f) {
        fill_frame(&p[f], 176, 24, -88, -12, 1);
    }
}

static void build_titlesonic_anims(void)
{
    int slot = alloc_anim_slot();
    if (slot < 0) return;
    s_titlesonic_list_id = slot;

    rsdk_sprite_animation_t *list = &g_rsdk_sprite_anims[slot];
    memset(list, 0, sizeof(*list));
    list->animations  = s_sonic_anims;
    list->frames      = s_sonic_frames;
    list->anim_count  = MAX_TITLESONIC_ANIMS;
    list->sheet_count = 0;

    /* Anim 0: Sonic body. Phase 1.23 GAP B' — 8 keyframes loaded from
     * anim-0 at offsets 0/6/12/18/24/30/36/48. The per-keyframe pivots
     * come from the corresponding global frame in g_tsonic_frames[] so
     * rsdk_draw_sprite_ex's world→jo conversion applies the right
     * canvas-centring at each tick of the entrance arc.
     *
     * Frame durations are reported as 8 ticks per keyframe (chosen so
     * 8 keyframes * 8 ticks = 64 ticks ≈ the decomp's 49-frame * ~1.3
     * cumulative cadence; the State_FlashIn-to-State_SetupLogo window
     * is 0x300 ticks = 768 game-ticks per decomp:209, well over the
     * full body cycle). loop_index = 7 freezes on the settled (last)
     * keyframe so TitleSonic_Update's `frame_id == frame_count-1` check
     * fires once the body settles, gating the finger animator's advance
     * — exactly the decomp behaviour at TitleSonic.c:18-19. */
    static const int s_tsonic_kf_offsets[8] = { 0, 6, 12, 18, 24, 30, 36, 48 };
    if (g_tsonic_loaded &&
        g_title_assets[TITLE_ASSET_TSONIC_BODY].base_sprite_id >= 0) {
        int body_fc = g_title_assets[TITLE_ASSET_TSONIC_BODY].frame_count;
        if (body_fc <= 0) body_fc = 1;
        if (body_fc > 8) body_fc = 8;
        if (body_fc > MAX_TITLESONIC_FRAMES) body_fc = MAX_TITLESONIC_FRAMES;
        int loop_idx = body_fc - 1;
        fill_anim(&s_sonic_anims[0], "Sonic", s_sonic_frames, body_fc,
                  1, (uint8_t)loop_idx);
        for (int i = 0; i < body_fc; ++i) {
            int gfi = g_tsonic_anim0_first_frame + s_tsonic_kf_offsets[i];
            if (gfi >= TITLE_TSONIC_MAX_FRAMES) gfi = g_tsonic_anim0_first_frame;
            fill_frame(&s_sonic_frames[i],
                       g_tsonic_frames[gfi].width,
                       g_tsonic_frames[gfi].height,
                       g_tsonic_frames[gfi].pivot_x,
                       g_tsonic_frames[gfi].pivot_y,
                       8);   /* per-keyframe duration in ticks */
        }
    } else {
        fill_anim(&s_sonic_anims[0], "Sonic", s_sonic_frames, 1, 1, 0);
        fill_frame(&s_sonic_frames[0], 110, 120, -50, -91, 1);
    }

    /* Anim 1: Finger Wave. Phase 1.20 — load the 12-frame metadata from
     * the TSONIC.ATL anim-1 record range. Decomp Sonic.bin sets
     * speed=1 loop_index=0 (loop back to the first finger frame after
     * the 12-frame cycle completes). Per-frame durations vary 1..4 ticks
     * per the atlas builder; we mirror them faithfully so the wave has
     * the correct cadence (slow-fast-slow as the finger curls/extends). */
    if (g_tsonic_loaded &&
        g_tsonic_anim1_frame_count > 0 &&
        g_title_assets[TITLE_ASSET_TSONIC_FINGER].base_sprite_id >= 0) {
        int finger_fc = g_title_assets[TITLE_ASSET_TSONIC_FINGER].frame_count;
        if (finger_fc > MAX_TITLESONIC_FRAMES) finger_fc = MAX_TITLESONIC_FRAMES;
        fill_anim(&s_sonic_anims[1], "Finger Wave",
                  &s_sonic_frames[MAX_TITLESONIC_FRAMES],
                  finger_fc, 1, 0);
        for (int i = 0; i < finger_fc; ++i) {
            int gfi = g_tsonic_anim1_first_frame + i;
            fill_frame(&s_sonic_frames[MAX_TITLESONIC_FRAMES + i],
                       g_tsonic_frames[gfi].width,
                       g_tsonic_frames[gfi].height,
                       g_tsonic_frames[gfi].pivot_x,
                       g_tsonic_frames[gfi].pivot_y,
                       g_tsonic_frames[gfi].duration);
        }
    } else {
        /* Stub fallback: 1-frame 1x1 anim so the animator ticks without
         * drawing visible garbage. */
        fill_anim(&s_sonic_anims[1], "Finger Wave",
                  &s_sonic_frames[MAX_TITLESONIC_FRAMES], 1, 0, 0);
        fill_frame(&s_sonic_frames[MAX_TITLESONIC_FRAMES], 1, 1, 0, 0, 1);
    }
}

static void build_titlebg_anims(void)
{
    int slot = alloc_anim_slot();
    if (slot < 0) return;
    s_titlebg_list_id = slot;

    rsdk_sprite_animation_t *list = &g_rsdk_sprite_anims[slot];
    memset(list, 0, sizeof(*list));
    list->animations  = s_bg_anims;
    list->frames      = s_bg_frames;
    list->anim_count  = MAX_TITLEBG_ANIMS;
    list->sheet_count = 0;

    /* All 5 sub-types map to a stub 1x1 frame for now; the actual BG
     * compositing lives on the NBG2 backdrop (TITLE.DAT), not VDP1 sprites.
     * Saturn-side INK_BLEND/INK_ADD for the upstream MOUNTAIN2/REFLECTION/
     * WATERSPARKLE layers requires VDP1 PMOD bit 5 + CCRSP setup which is
     * Phase 2 work. */
    static const char *names[5] = {
        "Mountain1", "Mountain2", "Reflection", "Water Sparkle", "Wing Shine"
    };
    for (int i = 0; i < MAX_TITLEBG_ANIMS; ++i) {
        fill_anim(&s_bg_anims[i], names[i], &s_bg_frames[i], 1, 0, 0);
        fill_frame(&s_bg_frames[i], 1, 1, 0, 0, 1);
    }
}

static void build_titlesetup_anims(void)
{
    int slot = alloc_anim_slot();
    if (slot < 0) return;
    s_titlesetup_list_id = slot;

    rsdk_sprite_animation_t *list = &g_rsdk_sprite_anims[slot];
    memset(list, 0, sizeof(*list));
    list->animations  = s_setup_anims;
    list->frames      = s_setup_frames;
    list->anim_count  = MAX_TITLESETUP_ANIMS;
    list->sheet_count = 0;

    /* Electricity arc — 1-frame stub (no Saturn-side asset shipped). The
     * state machine completes the arc by checking frame_count==0 →
     * auto-advance (see TitleSetup_State_AnimateUntilFlash). */
    fill_anim(&s_setup_anims[0], "Electricity", s_setup_frames, 1, 0, 0);
    fill_frame(&s_setup_frames[0], 1, 1, 0, 0, 1);
}

/* ---- ELECTRA.ATL 4-bpp loader (Phase 1.27 §11.32) ---------------------
 *
 * Same v4 file format as TSONIC.ATL (modelled on `load_tsonic_atlas`
 * above). Single-anim atlas, 8 culled keyframes built by
 * `tools/build_electricity_atlas.py`. Each keyframe becomes a sequential
 * jo sprite ID via `jo_sprite_add_4bits_image`.
 *
 * CRAM strategy: TSONIC.ATL parks its 16-color palette at CRAM index 2032
 * (high end). Electricity uses a DIFFERENT palette (cyan/white arc vs.
 * Sonic body palette), so it needs its own 16-color band. Park it 16
 * slots below TSONIC at CRAM index 2016 to avoid collision with the
 * VDP2 backgrounds + TSONIC. See `TSONIC_PAL_CRAM = 2032`.
 *
 * VDP1 PMOD/COLR fields per ST-013-R3 §5.5.4 mirror the TSONIC path:
 * CCM=0 (Color Bank 16), ECdis=1 (end-code disable), COLR = CRAM index. */

#define ELECTRA_MAGIC       0x5453    /* shared 'TS' family magic            */
#define ELECTRA_VERSION     0x0004    /* same v4 layout as TSONIC            */
#define ELECTRA_PAL_SIZE    16
#define ELECTRA_PAL_CRAM    2016      /* 16-aligned, below TSONIC's 2032     */
#define ELECTRA_HEADER_SZ   44
#define ELECTRA_ANIM_SZ     8
#define ELECTRA_FRAME_SZ    14
#define ELECTRA_STAGING_SZ  16384

static int load_electricity_atlas(void)
{
    if (g_electra_loaded) return 0;

    Sint32 fid = GFS_NameToId((Sint8 *)"ELECTRA.ATL");
    if (fid < 0) return -1;
    GfsHn gfs = GFS_Open(fid);
    if (gfs == JO_NULL) return -1;

    unsigned char *staging = (unsigned char *)jo_malloc(ELECTRA_STAGING_SZ);
    if (!staging) { GFS_Close(gfs); return -1; }

    /* Read sector 0 (header + 1 anim record + 8 frame records all fit
     * in 44 + 8 + 8*14 = 164 bytes, well within 2048). */
    Sint32 r0 = GFS_Fread(gfs, 1, staging, ELECTRA_STAGING_SZ);
    if (r0 <= 0) { jo_free(staging); GFS_Close(gfs); return -1; }

    const unsigned char *hdr = staging;
    if (be_u16(&hdr[0]) != ELECTRA_MAGIC ||
        be_u16(&hdr[2]) != ELECTRA_VERSION) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }
    int anim_count = (int)be_u16(&hdr[4]);
    if (anim_count != 1) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }
    if (be_u16(&hdr[6]) != ELECTRA_PAL_SIZE) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }

    /* Palette to its own CRAM band. */
    {
        volatile jo_color *cram = ((volatile jo_color *)JO_VDP2_CRAM) + ELECTRA_PAL_CRAM;
        for (int i = 0; i < ELECTRA_PAL_SIZE; ++i) {
            cram[i] = (jo_color)be_u16(&hdr[8 + i * 2]);
        }
        g_electra_palette_cram_index = ELECTRA_PAL_CRAM;
    }

    /* Anim record at offset 44. */
    const unsigned char *anim_rec = &staging[ELECTRA_HEADER_SZ];
    int total_frame_count = (int)be_u16(&anim_rec[0]);
    if (total_frame_count <= 0 || total_frame_count > TITLE_ELECTRA_MAX_FRAMES) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }

    /* Frame records start at offset 44 + 8 = 52. */
    unsigned int frame_table_off = ELECTRA_HEADER_SZ + ELECTRA_ANIM_SZ;
    unsigned int pool_base_off   = frame_table_off +
                                   (unsigned int)total_frame_count * ELECTRA_FRAME_SZ;
    unsigned int frame_pool_off[TITLE_ELECTRA_MAX_FRAMES];
    for (int i = 0; i < total_frame_count; ++i) {
        const unsigned char *r = &staging[frame_table_off + i * ELECTRA_FRAME_SZ];
        g_electra_frames[i].width    = be_u16(&r[0]);
        g_electra_frames[i].height   = be_u16(&r[2]);
        g_electra_frames[i].pivot_x  = be_s16(&r[4]);
        g_electra_frames[i].pivot_y  = be_s16(&r[6]);
        g_electra_frames[i].duration = be_u16(&r[8]);
        frame_pool_off[i]            = be_u32(&r[10]);
        g_electra_frames[i].cumulative = 0;
    }

    /* Stream each keyframe into VDP1 char-RAM via jo_sprite_add_4bits_image.
     * Same per-frame seek+read pattern as the TSONIC anim 1 finger loader. */
    int first_sid = -1;
    int loaded    = 0;
    for (int i = 0; i < total_frame_count; ++i) {
        unsigned int t_off    = pool_base_off + frame_pool_off[i];
        unsigned int t_sector = t_off / TSONIC_SECTOR_SZ;
        unsigned int t_in_sec = t_off % TSONIC_SECTOR_SZ;
        if (GFS_Seek(gfs, (Sint32)t_sector, GFS_SEEK_SET) < 0) break;
        unsigned int fb = ((unsigned int)g_electra_frames[i].width *
                           (unsigned int)g_electra_frames[i].height) >> 1;
        if (fb == 0 || fb > ELECTRA_STAGING_SZ) break;
        unsigned int span = t_in_sec + fb;
        Sint32 nsct = (Sint32)((span + TSONIC_SECTOR_SZ - 1) / TSONIC_SECTOR_SZ);
        if (nsct <= 0) nsct = 1;
        Sint32 max_nsct = (Sint32)(ELECTRA_STAGING_SZ / TSONIC_SECTOR_SZ);
        if (nsct > max_nsct) nsct = max_nsct;
        Sint32 nb = GFS_Fread(gfs, nsct, staging, ELECTRA_STAGING_SZ);
        if (nb <= 0) break;
        if (t_in_sec + fb > (unsigned int)nb) break;
        jo_img_8bits img;
        img.width  = g_electra_frames[i].width;
        img.height = g_electra_frames[i].height;
        img.data   = &staging[t_in_sec];
        int sid = jo_sprite_add_4bits_image(&img);
        if (sid < 0) break;
        if (first_sid < 0) first_sid = sid;
        ++loaded;
    }

    jo_free(staging);
    GFS_Close(gfs);

    if (first_sid < 0 || loaded <= 0) return -1;

    /* Populate asset slot. */
    g_title_assets[TITLE_ASSET_ELECTRICITY].base_sprite_id = first_sid;
    g_title_assets[TITLE_ASSET_ELECTRICITY].frame_count    = loaded;
    g_title_assets[TITLE_ASSET_ELECTRICITY].is_4bpp        = 1;
    g_title_assets[TITLE_ASSET_ELECTRICITY].width          = g_electra_frames[0].width;
    g_title_assets[TITLE_ASSET_ELECTRICITY].height         = g_electra_frames[0].height;
    g_title_assets[TITLE_ASSET_ELECTRICITY].pivot_x        = g_electra_frames[0].pivot_x;
    g_title_assets[TITLE_ASSET_ELECTRICITY].pivot_y        = g_electra_frames[0].pivot_y;

    g_electra_frame_count = loaded;
    g_electra_loaded = 1;
    return 0;
}

/* Draw a single electricity keyframe. Mirrors `title_tsonic_draw_frame`
 * but reads from the ELECTRA palette CRAM band + per-frame metadata.
 *
 * Per decomp `Draw_DrawRing:393-402` the title scene issues TWO of these
 * each tick (FLIP_NONE then FLIP_X) at world (256, 108). The caller is
 * responsible for passing both directions; this routine handles only one
 * draw call. The `direction` byte maps to VDP1 PMOD HF/VF bits per
 * ST-013-R3 §5.5.4 — but SGL's slDispSprite uses attr.dir which we set
 * via the standard SPR_*_FLIP attribute pattern. */
void title_electra_draw_frame(int frame_id, int jo_x, int jo_y, int z,
                              uint8_t direction)
{
    if (!g_electra_loaded) return;
    const title_asset_t *a = &g_title_assets[TITLE_ASSET_ELECTRICITY];
    if (a->base_sprite_id < 0 || a->frame_count <= 0) return;
    if (frame_id < 0) frame_id = 0;
    if (frame_id >= a->frame_count) frame_id = a->frame_count - 1;

    FIXED pos[XYZS];
    SPR_ATTR attr;

    pos[X] = toFIXED((float)jo_x);
    pos[Y] = toFIXED((float)jo_y);
    pos[Z] = toFIXED((float)z);
    pos[S] = toFIXED(1.0);

    attr.texno = (Uint16)(a->base_sprite_id + frame_id);
    attr.atrb  = (Uint16)(VDP1_CCM_BANK16 | VDP1_ECD_DISABLE);
    attr.colno = (Uint16)g_electra_palette_cram_index;
    attr.gstb  = 0;
    /* direction: bit 0 = FLIP_X (per RSDK constant), VDP1 CMDCTRL bit 4 = HF.
     * SGL `attr.dir` field is the upper byte of CMDCTRL; HF = 0x10. */
    attr.dir   = (direction & 0x01) ? 0x10 : 0x00;

    slDispSprite(pos, &attr, 0);
}

/* ---- TITLE3D.ATL 4-bpp loader (Phase 1.32) -----------------------------
 *
 * Same v4 file format as TSONIC.ATL and ELECTRA.ATL. Built by
 * `tools/build_title3d_atlas.py` from
 * `extracted/Data/Sprites/Title/Background.bin` (6 anims, 10 frames total).
 * The loader streams ALL 10 frames into VDP1 char-RAM but the Saturn-side
 * Title3DSprite class only references anim 5 ('Billboard Sprites', 5 frames
 * spanning MountainL/M/S + Tree + Bush) per decomp Title3DSprite.c:47
 * `RSDK.SetSpriteAnimation(aniFrames, 5, &self->animator, true, self->frame)`.
 *
 * The 5 billboard frames are mirrored into `g_title3d_bb_frames[]` and the
 * asset slot TITLE_ASSET_TITLE3D_BB stores `base_sprite_id` pointing at the
 * first of those 5 sequential jo sprite IDs. The other 5 anims (TitleBG
 * mountain/reflection/sparkle/wingshine) are loaded into char-RAM too so the
 * sprite IDs are valid for a future Phase Z wiring but are not consumed
 * this iteration.
 *
 * CRAM: TITLE3D parks its 16-color palette at CRAM index 2000, 16 slots
 * below ELECTRA (2016) and 32 below TSONIC (2032). 4-bpp Color Bank 16
 * convention (CCM=0, ECdis=1) shared with TSONIC + ELECTRA. */

#define TITLE3D_MAGIC       0x5453    /* shared 'TS' family magic         */
#define TITLE3D_VERSION     0x0004    /* same v4 layout                   */
#define TITLE3D_PAL_SIZE    16
#define TITLE3D_PAL_CRAM    2000      /* 16 below ELECTRA's 2016          */
#define TITLE3D_HEADER_SZ   44
#define TITLE3D_ANIM_SZ     8
#define TITLE3D_FRAME_SZ    14
#define TITLE3D_STAGING_SZ  16384
#define TITLE3D_BB_ANIM_INDEX 5       /* decomp anim 5 = Billboard Sprites */
#define TITLE3D_MAX_TOTAL_FRAMES 16   /* atlas has 10; cap for safety      */

static int load_title3d_atlas(void)
{
    if (g_title3d_loaded) return 0;

    Sint32 fid = GFS_NameToId((Sint8 *)"TITLE3D.ATL");
    if (fid < 0) return -1;
    GfsHn gfs = GFS_Open(fid);
    if (gfs == JO_NULL) return -1;

    unsigned char *staging = (unsigned char *)jo_malloc(TITLE3D_STAGING_SZ);
    if (!staging) { GFS_Close(gfs); return -1; }

    /* Sector 0 holds header(44) + 6 anim records(48) + 10 frame records(140)
     * = 232 B — well under 2048. */
    Sint32 r0 = GFS_Fread(gfs, 1, staging, TITLE3D_STAGING_SZ);
    if (r0 <= 0) { jo_free(staging); GFS_Close(gfs); return -1; }

    const unsigned char *hdr = staging;
    if (be_u16(&hdr[0]) != TITLE3D_MAGIC ||
        be_u16(&hdr[2]) != TITLE3D_VERSION) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }
    int anim_count = (int)be_u16(&hdr[4]);
    if (anim_count <= 0 || anim_count > 8) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }
    if (be_u16(&hdr[6]) != TITLE3D_PAL_SIZE) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }

    /* Palette to its own CRAM band. */
    {
        volatile jo_color *cram = ((volatile jo_color *)JO_VDP2_CRAM) + TITLE3D_PAL_CRAM;
        for (int i = 0; i < TITLE3D_PAL_SIZE; ++i) {
            cram[i] = (jo_color)be_u16(&hdr[8 + i * 2]);
        }
        g_title3d_palette_cram_index = TITLE3D_PAL_CRAM;
    }

    /* Walk anim records to compute total_frame_count and locate anim 5's
     * first_global_idx. */
    int total_frame_count = 0;
    int bb_first_global   = 0;
    int bb_frame_count    = 0;
    {
        const unsigned char *anim_base = &staging[TITLE3D_HEADER_SZ];
        for (int ai = 0; ai < anim_count; ++ai) {
            const unsigned char *r = &anim_base[ai * TITLE3D_ANIM_SZ];
            int afc    = (int)be_u16(&r[0]);
            int afirst = (int)be_u16(&r[4]);
            if (ai == TITLE3D_BB_ANIM_INDEX) {
                bb_first_global = afirst;
                bb_frame_count  = afc;
            }
            total_frame_count += afc;
        }
    }
    if (total_frame_count <= 0 || total_frame_count > TITLE3D_MAX_TOTAL_FRAMES) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }
    if (bb_frame_count <= 0 || bb_frame_count > TITLE3D_BB_MAX_FRAMES) {
        jo_free(staging); GFS_Close(gfs); return -1;
    }

    /* Frame records start at offset 44 + anim_count*8. */
    unsigned int frame_table_off = TITLE3D_HEADER_SZ +
                                   (unsigned int)anim_count * TITLE3D_ANIM_SZ;
    unsigned int pool_base_off   = frame_table_off +
                                   (unsigned int)total_frame_count * TITLE3D_FRAME_SZ;

    /* Cache per-frame metadata for all 10 frames in a local scratch array,
     * then mirror the 5 billboard frames into g_title3d_bb_frames. */
    struct { uint16_t w, h; int16_t px, py; uint16_t dur; uint32_t pool_off; }
        frames[TITLE3D_MAX_TOTAL_FRAMES];
    for (int i = 0; i < total_frame_count; ++i) {
        const unsigned char *r = &staging[frame_table_off + i * TITLE3D_FRAME_SZ];
        frames[i].w        = be_u16(&r[0]);
        frames[i].h        = be_u16(&r[2]);
        frames[i].px       = be_s16(&r[4]);
        frames[i].py       = be_s16(&r[6]);
        frames[i].dur      = be_u16(&r[8]);
        frames[i].pool_off = be_u32(&r[10]);
    }

    /* Stream each frame's pixels into VDP1 char-RAM. We load ALL frames so
     * the sprite IDs are contiguous; the 5 billboard frames feed Title3DSprite
     * and the 5 anim-0..4 single-frame entries (Phase 1.34) feed TitleBG. */
    int first_sid_all = -1;
    int bb_first_sid  = -1;
    int bg_first_sid  = -1;
    int loaded        = 0;
    /* Phase 1.34 — `bg_first_global` is the global frame index of the first
     * TitleBG sub-type (anim 0 frame 0 = Mountain1). Since anims 0..4 are
     * single-frame each (verified via `python tools/build_title3d_atlas.py`
     * + ATL dump 2026-05-27), the global frame indices for the 5 TitleBG
     * sub-types are 0..4 in order. */
    const int bg_first_global = 0;
    const int bg_frame_count  = TITLE3D_BG_MAX_FRAMES;
    for (int i = 0; i < total_frame_count; ++i) {
        if (frames[i].w == 0 || frames[i].h == 0) continue;
        unsigned int t_off    = pool_base_off + frames[i].pool_off;
        unsigned int t_sector = t_off / TSONIC_SECTOR_SZ;
        unsigned int t_in_sec = t_off % TSONIC_SECTOR_SZ;
        if (GFS_Seek(gfs, (Sint32)t_sector, GFS_SEEK_SET) < 0) break;
        unsigned int fb = ((unsigned int)frames[i].w *
                           (unsigned int)frames[i].h) >> 1;
        if (fb == 0 || fb > TITLE3D_STAGING_SZ) break;
        unsigned int span = t_in_sec + fb;
        Sint32 nsct = (Sint32)((span + TSONIC_SECTOR_SZ - 1) / TSONIC_SECTOR_SZ);
        if (nsct <= 0) nsct = 1;
        Sint32 max_nsct = (Sint32)(TITLE3D_STAGING_SZ / TSONIC_SECTOR_SZ);
        if (nsct > max_nsct) nsct = max_nsct;
        Sint32 nb = GFS_Fread(gfs, nsct, staging, TITLE3D_STAGING_SZ);
        if (nb <= 0) break;
        if (t_in_sec + fb > (unsigned int)nb) break;
        jo_img_8bits img;
        img.width  = frames[i].w;
        img.height = frames[i].h;
        img.data   = &staging[t_in_sec];
        int sid = jo_sprite_add_4bits_image(&img);
        if (sid < 0) break;
        if (first_sid_all < 0) first_sid_all = sid;
        /* When this frame is one of the 5 billboard frames, record the
         * sprite ID and mirror metadata. */
        if (i >= bb_first_global && i < bb_first_global + bb_frame_count) {
            int local = i - bb_first_global;
            if (local < TITLE3D_BB_MAX_FRAMES) {
                g_title3d_bb_frames[local].width    = frames[i].w;
                g_title3d_bb_frames[local].height   = frames[i].h;
                g_title3d_bb_frames[local].pivot_x  = frames[i].px;
                g_title3d_bb_frames[local].pivot_y  = frames[i].py;
                g_title3d_bb_frames[local].duration = frames[i].dur;
                g_title3d_bb_frames[local].cumulative = 0;
                if (bb_first_sid < 0) bb_first_sid = sid;
            }
        }
        /* Phase 1.34 — TitleBG anim 0..4 single-frame entries. */
        if (i >= bg_first_global && i < bg_first_global + bg_frame_count) {
            int local = i - bg_first_global;
            if (local < TITLE3D_BG_MAX_FRAMES) {
                g_title3d_bg_frames[local].width    = frames[i].w;
                g_title3d_bg_frames[local].height   = frames[i].h;
                g_title3d_bg_frames[local].pivot_x  = frames[i].px;
                g_title3d_bg_frames[local].pivot_y  = frames[i].py;
                g_title3d_bg_frames[local].duration = frames[i].dur;
                g_title3d_bg_frames[local].cumulative = 0;
                if (bg_first_sid < 0) bg_first_sid = sid;
            }
        }
        ++loaded;
    }

    jo_free(staging);
    GFS_Close(gfs);

    if (bb_first_sid < 0) return -1;

    /* Populate billboard asset slot. */
    g_title_assets[TITLE_ASSET_TITLE3D_BB].base_sprite_id = bb_first_sid;
    g_title_assets[TITLE_ASSET_TITLE3D_BB].frame_count    = bb_frame_count;
    g_title_assets[TITLE_ASSET_TITLE3D_BB].is_4bpp        = 1;
    g_title_assets[TITLE_ASSET_TITLE3D_BB].width          = g_title3d_bb_frames[0].width;
    g_title_assets[TITLE_ASSET_TITLE3D_BB].height         = g_title3d_bb_frames[0].height;
    g_title_assets[TITLE_ASSET_TITLE3D_BB].pivot_x        = g_title3d_bb_frames[0].pivot_x;
    g_title_assets[TITLE_ASSET_TITLE3D_BB].pivot_y        = g_title3d_bb_frames[0].pivot_y;

    g_title3d_bb_frame_count = bb_frame_count;
    /* Phase 1.34 — record the per-anim base sid for TitleBG (anims 0..4). */
    g_title3d_bg_first_sid   = bg_first_sid;
    g_title3d_bg_frame_count = (bg_first_sid >= 0) ? bg_frame_count : 0;
    g_title3d_loaded = 1;
    (void)first_sid_all;
    (void)loaded;
    return 0;
}

/* Phase 1.32 — draw a single billboard frame (4-bpp Color Bank 16).
 * Mirrors `title_electra_draw_frame` but reads from the TITLE3D palette
 * CRAM band + per-frame metadata. No flip — the 5 billboard frames are
 * symmetric per decomp Title3DSprite_Draw which calls DrawSprite without
 * any FLIP_* direction. */
void title3d_bb_draw_frame(int frame_id, int jo_x, int jo_y, int z)
{
    if (!g_title3d_loaded) return;
    const title_asset_t *a = &g_title_assets[TITLE_ASSET_TITLE3D_BB];
    if (a->base_sprite_id < 0 || a->frame_count <= 0) return;
    if (frame_id < 0) frame_id = 0;
    if (frame_id >= a->frame_count) frame_id = a->frame_count - 1;

    FIXED pos[XYZS];
    SPR_ATTR attr;

    pos[X] = toFIXED((float)jo_x);
    pos[Y] = toFIXED((float)jo_y);
    pos[Z] = toFIXED((float)z);
    pos[S] = toFIXED(1.0);

    attr.texno = (Uint16)(a->base_sprite_id + frame_id);
    attr.atrb  = (Uint16)(VDP1_CCM_BANK16 | VDP1_ECD_DISABLE);
    attr.colno = (Uint16)g_title3d_palette_cram_index;
    attr.gstb  = 0;
    attr.dir   = 0;

    slDispSprite(pos, &attr, 0);
}

/* Phase 1.32b — same draw path as title3d_bb_draw_frame, but `hv_scale`
 * (Saturn FIXED Q16.16) replaces the constant toFIXED(1.0) at pos[S].
 *
 * SGL slDispSprite contract (ST-238-R1 page 65, SL_DEF.H:911):
 *   void slDispSprite(FIXED *pos, SPR_ATTR *attr, ANGLE Zrot);
 *   pos[X=0, Y=1, Z=2, S=3] with S = uniform H/V scale in FIXED.
 *   "If a negative value is input for the scale, calculate the scale
 *    according to the Z position, multiply it by the complement..."
 *   We pass POSITIVE FIXED so the engine uses our explicit scale and
 *   does NOT auto-derive from Z.
 *
 * SL_DEF.H:93 enum `S=XYZ=3, Sh=S, Sv=XYZS=4` confirms pos[3] is the
 * uniform scale slot for slDispSprite (the slDispSpriteHV variant uses
 * a 5-element pos[X,Y,Z,Sh,Sv] for asymmetric scale). Our depth scale
 * is uniform (Title3DSprite_Draw:33 sets scale.y = scale.x), so the
 * 4-element slDispSprite path is correct.
 *
 * Verified against jo-engine/jo_engine/sprites.c:443-447: jo's draw path
 * picks slDispSprite over slDispSpriteHV whenever sgl_pos[3]==sgl_pos[4],
 * which is exactly our case. */
void title3d_bb_draw_frame_scaled(int frame_id, int jo_x, int jo_y, int z,
                                  int hv_scale)
{
    if (!g_title3d_loaded) return;
    const title_asset_t *a = &g_title_assets[TITLE_ASSET_TITLE3D_BB];
    if (a->base_sprite_id < 0 || a->frame_count <= 0) return;
    if (frame_id < 0) frame_id = 0;
    if (frame_id >= a->frame_count) frame_id = a->frame_count - 1;

    FIXED pos[XYZS];
    SPR_ATTR attr;

    pos[X] = toFIXED((float)jo_x);
    pos[Y] = toFIXED((float)jo_y);
    pos[Z] = toFIXED((float)z);
    pos[S] = (FIXED)hv_scale;       /* per-entity depth scale (Q16.16) */

    attr.texno = (Uint16)(a->base_sprite_id + frame_id);
    attr.atrb  = (Uint16)(VDP1_CCM_BANK16 | VDP1_ECD_DISABLE);
    attr.colno = (Uint16)g_title3d_palette_cram_index;
    attr.gstb  = 0;
    attr.dir   = 0;

    slDispSprite(pos, &attr, 0);
}

/* Phase 1.34 — draw a single TitleBG sub-type frame (4-bpp Color Bank 16).
 * Mirrors title3d_bb_draw_frame but indexed by anim 0..4 (TitleBG types
 * MOUNTAIN1/MOUNTAIN2/REFLECTION/WATERSPARKLE/WINGSHINE). All 5 anims
 * have a single frame each, so anim_id == sprite-id-offset from
 * g_title3d_bg_first_sid.
 *
 * Decomp TitleBG_Draw (TitleBG.c:48-54) calls
 *   RSDK.DrawSprite(&self->animator, NULL, false);
 * with self->drawFX = FX_FLIP but self->direction never assigned in
 * Create/Update — so direction is the default 0 (FLIP_NONE). We expose
 * `direction` here to keep the contract aligned with title_electra_draw_frame
 * in case a future caller needs the FLIP_X bit (RSDK FLIP_X = 1 -> VDP1
 * PMOD HF bit 4 = 0x10 per ST-013-R3 §5.5.4).
 *
 * Phase 1.34c — `half_transparency` parameter folds in the VDP1
 * half-transparent Color-Calc mode (CL_Trans = 3 in PMOD bits 2:0
 * per ST-013-R3 §5.5.4 + SGL SL_DEF.H:194). Decomp TitleBG_Create
 * (TitleBG.c:67-83) sets per-type inkEffect:
 *   MOUNTAIN1                       -> opaque  (default INK_NONE)
 *   MOUNTAIN2                       -> INK_BLEND  -> CL_Trans
 *   REFLECTION + WATERSPARKLE       -> INK_ADD    -> CL_Trans (Saturn
 *                                      VDP1 has no additive primitive
 *                                      in a single pass; half-transparent
 *                                      is the canonical approximation)
 *   WINGSHINE                       -> INK_MASKED -> CL_Trans (likewise;
 *                                      true mask-blend needs a 2-pass
 *                                      stencil which is Phase Z work)
 *
 * NOTE: per ST-013-R3 §5.5.4 (color calculation conditions) VDP1
 * half-transparency requires the framebuffer destination pixel to have
 * MSB=1. Our framebuffer comes from the VDP2 backdrop + RBG0 + NBG1/2
 * composite path; VDP2 layers in RGB555 cell-mode write MSB=1 by
 * default (sprite-type 8 register / SPCTL settings established by
 * jo_core_init's slInitSystem in 320x224 mode). CL_Trans correctly
 * blends the sprite pixel 50/50 with whatever underlies it. */
void title3d_bg_draw_frame(int anim_id, int jo_x, int jo_y, int z,
                           uint8_t direction, int half_transparency)
{
    if (!g_title3d_loaded) return;
    if (g_title3d_bg_first_sid < 0) return;
    if (g_title3d_bg_frame_count <= 0) return;
    if (anim_id < 0) anim_id = 0;
    if (anim_id >= g_title3d_bg_frame_count) anim_id = g_title3d_bg_frame_count - 1;

    FIXED pos[XYZS];
    SPR_ATTR attr;

    pos[X] = toFIXED((float)jo_x);
    pos[Y] = toFIXED((float)jo_y);
    pos[Z] = toFIXED((float)z);
    pos[S] = toFIXED(1.0);

    /* Base PMOD: Color Bank 16 (CCM=0 at bits 5:3 = CL16Bnk) +
     * End-Code Disable (bit 7). Phase 1.34c adds CL_Trans (= 3 in
     * bits 2:0 = Color-Calc field per SGL SL_DEF.H:194) for the
     * MOUNTAIN2/REFLECTION/WATERSPARKLE/WINGSHINE half-transparent
     * draws. CL_Replace (= 0) is the implicit opaque default for
     * MOUNTAIN1. */
    Uint16 atrb = (Uint16)(VDP1_CCM_BANK16 | VDP1_ECD_DISABLE);
    if (half_transparency) {
        atrb = (Uint16)(atrb | CL_Trans);
    }

    attr.texno = (Uint16)(g_title3d_bg_first_sid + anim_id);
    attr.atrb  = atrb;
    attr.colno = (Uint16)g_title3d_palette_cram_index;
    attr.gstb  = 0;
    attr.dir   = (direction & 0x01) ? 0x10 : 0x00;

    slDispSprite(pos, &attr, 0);
}

/* Phase 1.37 — same path as title3d_bg_draw_frame, but with non-uniform
 * H/V scaling via slDispSpriteHV (5-element pos [X, Y, Z, Sh, Sv]) per
 * SGL ST-238-R1 page 65 + SL_DEF.H:93 enum (Sh = S = 3, Sv = XYZS = 4) +
 * SL_DEF.H:912 prototype.  Used by the central island silhouette which
 * scales Mountain1 (176x16 source) into a substantial bottom-anchored
 * silhouette.
 *
 * Mountain1's source aspect (11:1) means uniform scaling cannot fill a
 * roughly square viewport region; non-uniform H/V scale is required.
 *
 * jo's path (sprites.c:444-445) confirms slDispSpriteHV with angle=1
 * SGL bug-workaround (engine doesn't draw properly at angle 0).  We
 * mirror that exact convention.
 *
 * `hv_scale_x` / `hv_scale_y` are Saturn FIXED (Q16.16) where 0x10000 =
 * 1.0x (source size). */
void title3d_bg_draw_frame_hv(int anim_id, int jo_x, int jo_y, int z,
                              int hv_scale_x, int hv_scale_y,
                              uint8_t direction, int half_transparency)
{
    if (!g_title3d_loaded) return;
    if (g_title3d_bg_first_sid < 0) return;
    if (g_title3d_bg_frame_count <= 0) return;
    if (anim_id < 0) anim_id = 0;
    if (anim_id >= g_title3d_bg_frame_count) anim_id = g_title3d_bg_frame_count - 1;

    FIXED pos[XYZSS];     /* 5-element: X, Y, Z, Sh, Sv per SL_DEF.H:93 */
    SPR_ATTR attr;

    pos[X]  = toFIXED((float)jo_x);
    pos[Y]  = toFIXED((float)jo_y);
    pos[Z]  = toFIXED((float)z);
    pos[Sh] = (FIXED)hv_scale_x;
    pos[Sv] = (FIXED)hv_scale_y;

    Uint16 atrb = (Uint16)(VDP1_CCM_BANK16 | VDP1_ECD_DISABLE);
    if (half_transparency) {
        atrb = (Uint16)(atrb | CL_Trans);
    }

    attr.texno = (Uint16)(g_title3d_bg_first_sid + anim_id);
    attr.atrb  = atrb;
    attr.colno = (Uint16)g_title3d_palette_cram_index;
    attr.gstb  = 0;
    attr.dir   = (direction & 0x01) ? 0x10 : 0x00;

    /* Pass non-zero angle (1) per jo sprites.c:445 SGL bug workaround. */
    slDispSpriteHV(pos, &attr, 1);
}

/* ---- Public entry ----------------------------------------------------- */

void title_assets_load(void)
{
    for (int i = 0; i < TITLE_ASSET_COUNT; ++i) {
        memset(&g_title_assets[i], 0, sizeof(g_title_assets[i]));
        g_title_assets[i].base_sprite_id = -1;
    }

    /* Load the .SPR atlases — each one populates a TITLE_ASSET_* slot with
     * the resulting jo sprite_id table. */
    load_spr_atlas(TITLE_ASSET_TLOGO_EMBLEM,     "MWINGS.SPR");
    load_spr_atlas(TITLE_ASSET_TLOGO_RIBSIDE,    "MRIBSIDE.SPR");
    load_spr_atlas(TITLE_ASSET_TLOGO_GAMETITLE,  "MLOGO.SPR");
    load_spr_atlas(TITLE_ASSET_TLOGO_RIBCENTER,  "MRIBBON.SPR");
    load_spr_atlas(TITLE_ASSET_TLOGO_RINGBOT,    "MRING.SPR");
    load_spr_atlas(TITLE_ASSET_TLOGO_PRESSSTART, "MPRESS.SPR");

    /* Load TSONIC.ATL — soft-fail if missing. */
    (void)load_tsonic_atlas();

    /* Phase 1.27 §11.32 — Load ELECTRA.ATL (electricity pre-flash arc).
     * Soft-fail if missing: the existing State_AnimateUntilFlash fallback
     * at TitleSetup.c:170-175 auto-advances when frame_count < 32, so the
     * scene still progresses past the arc state. */
    (void)load_electricity_atlas();

    /* Phase 1.32 — Load TITLE3D.ATL (Title3DSprite billboard family).
     * 5 billboard frames (MountainL/M/S + Tree + Bush) consumed by
     * Title3DSprite_Draw_All. Soft-fail if missing — Title3DSprite_Draw_All
     * early-returns on !g_title3d_loaded so the title scene still renders
     * minus the orbiting island formation. */
    (void)load_title3d_atlas();

    /* Manufacture synthetic anim lists so the per-class StageLoad ->
     * LoadSpriteAnimation gets a real list_id with valid frame tables. */
    build_titlelogo_anims();
    build_titlesonic_anims();
    build_titlebg_anims();
    build_titlesetup_anims();
}

int title_assets_titlelogo_list_id(void)  { return s_titlelogo_list_id; }
int title_assets_titlesonic_list_id(void) { return s_titlesonic_list_id; }
int title_assets_titlebg_list_id(void)    { return s_titlebg_list_id; }
int title_assets_titlesetup_list_id(void) { return s_titlesetup_list_id; }
