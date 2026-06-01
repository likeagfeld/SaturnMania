/* title_sonic.c — 49-frame TitleSonic animation as a VDP1 4-bpp sprite atlas.
 *
 * Built from `cd/TSONIC.ATL` (see tools/build_titlesonic_atlas.py for
 * format).  Loads ONCE at title init, then animates by indexing into the
 * 49 consecutive sprite IDs returned by jo_sprite_add_4bits_image().
 *
 * Sprite-ID allocator design (Problem 1 of the SHIP-IT-FIRST-TRY mission):
 * ---------------------------------------------------------------------
 * The prior agent's implementation referenced jo's file-static
 * `__jo_sprite_id` / `__jo_sprite_addr` symbols, which won't link.
 * We solve this by adding ONE minimal extension to jo's sprites.c —
 * a public `jo_sprite_add_4bits_image()` function that mirrors the
 * existing `jo_sprite_add_8bits_image()` exactly (only the cmode arg
 * differs: COL_16 vs COL_256).  This:
 *
 *   - Touches jo by exactly +9 lines (one function + one header proto)
 *   - Uses jo's existing __internal_jo_sprite_add for VRAM allocation,
 *     ID assignment, alignment, and DMA upload — zero new code paths
 *   - Returns a normal jo sprite ID so jo_create_sprite_anim, free_from,
 *     etc. all continue to work over our 49 IDs
 *
 * jo_sprite_draw3D doesn't draw 4-bpp sprites correctly (its
 * __jo_set_sprite_attributes helper hard-codes PMOD CCM=4 i.e. 8-bpp
 * for any non-COL_32K sprite), so we emit slDispSprite ourselves with
 * a hand-built SPR_ATTR carrying CCM=0 (Color Bank 16).
 *
 * Strategy summary (see docs/large_sprite_anim_strategy.md §3):
 *
 *   1. Write a 16-color palette directly to CRAM at a known
 *      16-aligned slot (TS_PAL_CRAM_INDEX = 2032).  jo's palette
 *      allocator strides 256 entries which doesn't align with VDP1's
 *      16-color-bank alignment requirement (the COLR field's low 4
 *      bits ARE the per-pixel nibble; the upper bits must be the
 *      16-CRAM-slot bank base).
 *
 *   2. Read header + frame records via jo_fs_read_file() into a
 *      single 373 KB buffer at boot — this is the ONLY transient
 *      allocation; after the 49 jo_sprite_add_4bits_image calls
 *      complete the buffer is freed.  Peak transient jo_malloc =
 *      atlas file size ~373 KB; well within the 576 KB pool's
 *      remaining headroom at the title-load stage (~100 KB).
 *
 *      *** Updated 2026-05-26: the prior staging-buffer + per-frame
 *      GFS_Load design (8 KB peak) was rejected because GFS_Load with
 *      arbitrary file offsets often fails on Saturn (sector-aligned
 *      reads only).  The full-file read is simpler and well within
 *      the pool budget; the pool is at 477 KB resident at title load
 *      and the global pool is 576 KB, leaving 99 KB for the temporary
 *      file buffer — TOO TIGHT.  We instead use a 16 KB staging
 *      buffer with GFS_Open + GFS_Fread, which guarantees stream
 *      sequencing (no random offsets) and bounded peak allocation.
 *
 *   3. For each of 49 frames sequentially: GFS_Fread the frame's
 *      packed-nibble bytes into a staging buffer, then call
 *      jo_sprite_add_4bits_image() which DMAs into VDP1 VRAM and
 *      records the slot.  Frame i lives at sprite ID base+i.
 *
 *   4. At draw time, look up the current frame from a tick-driven
 *      cumulative-duration table and call slDispSprite() with the
 *      per-frame pivot offsets applied.
 *
 * Hardware citations (definitive):
 *   - VDP1 character pattern size: H 8..504 px in 8-px units, V 1..255
 *     px.  Source: VDP1 User Manual table 5.1
 *     (D:\Claude Saturn Skill Documentation\Saturn_Official_Documentation\
 *     VDP1_Manual.txt §5.1).
 *   - VDP1 color mode COL_16: bytes = (W * H * 4) >> 3 = (W * H) / 2.
 *     Each pixel is 4 bits, MSB nibble = leftmost pixel of the pair.
 *     Source: jo sprites.c:74, VDP1 manual §5.1.2 "Color Bank 16".
 *   - Palette index 0 is transparent through the sprite draw mode
 *     (drawMode bit 6 = "transparent pixel disable").  Source: VDP1
 *     manual §5.4.2 sprite draw-mode word format.
 */

#include <jo/jo.h>
#include "title_sonic.h"

/* Atlas file format constants — must match tools/build_titlesonic_atlas.py.
 * Header is 44 bytes; each frame record is 14 bytes. */
#define ATLAS_MAGIC          0x5453     /* 'TS' big-endian as u16  */
#define ATLAS_VERSION        0x0003
#define ATLAS_PALETTE_SIZE   16
#define ATLAS_FRAME_MAX      64         /* 49 actual; 64 for safety */
#define ATLAS_HEADER_BYTES   (2*4 + 2*ATLAS_PALETTE_SIZE + 4)        /* = 44 */
#define ATLAS_FRAME_REC_BYTES (2*5 + 4)                              /* = 14 */

/* Per-frame metadata held in BSS for draw-time use. */
typedef struct {
    unsigned short  width;       /* padded to multiple of 8           */
    unsigned short  height;      /* 1..255                            */
    short           pivot_x;     /* RSDK pivot, preserved as-is       */
    short           pivot_y;
    unsigned short  duration;    /* ticks at the configured framerate */
    unsigned short  cumulative;  /* sum of duration[0..i], for table-driven lookup */
} ts_frame_t;

static ts_frame_t       g_ts_frames[ATLAS_FRAME_MAX];
static unsigned short   g_ts_frame_count;
static unsigned short   g_ts_total_ticks;
static unsigned short   g_ts_palette_cram_index;
static int              g_ts_base_sprite_id; /* first of g_ts_frame_count consecutive sprites */
static int              g_ts_atlas_loaded;

/* ---- Big-endian readers ------------------------------------------------- */
static __jo_force_inline unsigned short be_u16(const unsigned char *p)
{
    return ((unsigned short)p[0] << 8) | p[1];
}
static __jo_force_inline short be_s16(const unsigned char *p)
{
    return (short)be_u16(p);
}
static __jo_force_inline unsigned int be_u32(const unsigned char *p)
{
    return ((unsigned int)p[0] << 24) |
           ((unsigned int)p[1] << 16) |
           ((unsigned int)p[2] <<  8) |
           ((unsigned int)p[3]);
}

/* ---- Direct CRAM palette upload --------------------------------------- *
 *
 * jo_create_palette has a known off-by-one CRAM placement bug
 * (memory/jo-cram-off-by-one-shift.md) AND its 256-entry stride
 * misaligns 16-color banks (VDP1 Color Bank 16 needs the bank base on
 * a 16-CRAM-index boundary).  We bypass jo's allocator and write
 * directly to a chosen CRAM region.
 *
 * CRAM layout:
 *   CRAM has 2048 entries at JO_VDP2_CRAM (0x25F00000), 2 bytes each.
 *   We park our 16-color palette at CRAM index TS_PAL_CRAM_INDEX,
 *   on a 16-aligned slot at the high end of CRAM, well past any jo
 *   palette.  For VDP1 Color Bank 16 mode the sprite-cmd COLR field ==
 *   TS_PAL_CRAM_INDEX (since the low 4 bits are the pixel nibble and
 *   the upper bits select the bank base; with our base being
 *   16-aligned the OR is equivalent to addition).
 *
 * CRAM index 2032 is 16-aligned, sits at byte offset 4064 in CRAM
 * (16 bytes below the 2048-entry / 4096-byte end), past any jo palette.
 */
#define TS_PAL_CRAM_INDEX   2032

static void __ts_upload_palette(const unsigned short *bgr1555_16)
{
    volatile jo_color *cram;
    int i;

    cram = ((volatile jo_color *)JO_VDP2_CRAM) + TS_PAL_CRAM_INDEX;
    /* Direct CRAM write -- VDP2 Color RAM at 0x25F00000 is 16-bit accessible. */
    for (i = 0; i < ATLAS_PALETTE_SIZE; ++i) {
        cram[i] = (jo_color)bgr1555_16[i];
    }
    g_ts_palette_cram_index = TS_PAL_CRAM_INDEX;
}

/* ---- Public API --------------------------------------------------------- */

#define STAGING_BYTES   16384   /* worst-case frame is 200x126/2 = 12,600 B */

int title_sonic_load(void)
{
    GfsHn               gfs;
    Sint32              fid;
    Sint32              nbyte_read;
    unsigned char       header_buf[ATLAS_HEADER_BYTES];
    unsigned char       fr_buf[ATLAS_FRAME_REC_BYTES * ATLAS_FRAME_MAX];
    unsigned short      palette_native[ATLAS_PALETTE_SIZE];
    unsigned char      *staging;
    unsigned int        frame_pix_offset[ATLAS_FRAME_MAX];
    unsigned int        pool_base_offset;
    unsigned int        cur_pool_pos;    /* track read cursor through pixel pool */
    int                 i;
    int                 first_sid;
    jo_img_8bits        img;

    if (g_ts_atlas_loaded) {
        return 1;
    }

    /* Step 0: find the atlas file via GFS.  We use GFS_Open + GFS_Fread
     * (sequential read) because GFS_Load offset-based reads are
     * sector-aligned on Saturn and our 44-byte header + 686-byte frame
     * table aren't sector-aligned. */
    fid = GFS_NameToId((Sint8 *)"TSONIC.ATL");
    if (fid < 0) {
        return 0;
    }
    gfs = GFS_Open(fid);
    if (gfs == JO_NULL) {
        return 0;
    }

    /* Step 1: stage buffer (jo_malloc, freed at end).  Sized to the
     * worst-case frame + padding. */
    staging = (unsigned char *)jo_malloc(STAGING_BYTES);
    if (staging == NULL) {
        GFS_Close(gfs);
        return 0;
    }

    /* Step 2: read header (44 bytes) sequentially. */
    if (GFS_Fread(gfs, 1, header_buf, ATLAS_HEADER_BYTES) < 0) {
        jo_free(staging); GFS_Close(gfs);
        return 0;
    }
    if (be_u16(&header_buf[0]) != ATLAS_MAGIC ||
        be_u16(&header_buf[2]) != ATLAS_VERSION) {
        jo_free(staging); GFS_Close(gfs);
        return 0;
    }
    g_ts_frame_count = be_u16(&header_buf[4]);
    if (g_ts_frame_count == 0 || g_ts_frame_count > ATLAS_FRAME_MAX) {
        jo_free(staging); GFS_Close(gfs);
        return 0;
    }
    if (be_u16(&header_buf[6]) != ATLAS_PALETTE_SIZE) {
        jo_free(staging); GFS_Close(gfs);
        return 0;
    }

    /* Palette: 16 BGR1555 u16s, header bytes [8..40). */
    for (i = 0; i < ATLAS_PALETTE_SIZE; ++i) {
        palette_native[i] = be_u16(&header_buf[8 + i * 2]);
    }

    /* Step 3: read frame-record block. */
    if (GFS_Fread(gfs, 1, fr_buf,
                  (Sint32)(ATLAS_FRAME_REC_BYTES * g_ts_frame_count)) < 0) {
        jo_free(staging); GFS_Close(gfs);
        return 0;
    }
    for (i = 0; i < g_ts_frame_count; ++i) {
        const unsigned char *r = &fr_buf[i * ATLAS_FRAME_REC_BYTES];
        g_ts_frames[i].width    = be_u16(&r[0]);
        g_ts_frames[i].height   = be_u16(&r[2]);
        g_ts_frames[i].pivot_x  = be_s16(&r[4]);
        g_ts_frames[i].pivot_y  = be_s16(&r[6]);
        g_ts_frames[i].duration = be_u16(&r[8]);
        frame_pix_offset[i]     = be_u32(&r[10]);   /* offset into pool */
    }
    pool_base_offset = ATLAS_HEADER_BYTES +
                       (unsigned int)ATLAS_FRAME_REC_BYTES * g_ts_frame_count;
    cur_pool_pos = 0;   /* relative to pool start = next sequential frame */

    /* Step 4: upload palette to CRAM. */
    __ts_upload_palette(palette_native);
    (void)pool_base_offset;   /* compiler will note unused; explicit cast */

    /* Step 5: stream each frame from CD via GFS_Fread and add as
     * 4-bpp sprite via jo's new public API. */
    first_sid = -1;
    g_ts_total_ticks = 0;

    for (i = 0; i < g_ts_frame_count; ++i) {
        unsigned int frame_bytes = ((unsigned int)g_ts_frames[i].width *
                                    (unsigned int)g_ts_frames[i].height) >> 1;
        int sid;

        if (frame_bytes > STAGING_BYTES) {
            jo_free(staging); GFS_Close(gfs);
            return 0;
        }

        /* Skip-read any padding between current pool position and this
         * frame's start.  The builder packs frames consecutively so
         * frame_pix_offset[i] should equal cur_pool_pos (except possibly
         * for inter-frame alignment padding).  If they differ we burn the
         * gap via Fread into staging. */
        while (cur_pool_pos < frame_pix_offset[i]) {
            unsigned int skip = frame_pix_offset[i] - cur_pool_pos;
            if (skip > STAGING_BYTES) skip = STAGING_BYTES;
            if (GFS_Fread(gfs, 1, staging, (Sint32)skip) < 0) {
                jo_free(staging); GFS_Close(gfs);
                return 0;
            }
            cur_pool_pos += skip;
        }

        nbyte_read = GFS_Fread(gfs, 1, staging, (Sint32)frame_bytes);
        if (nbyte_read < 0) {
            jo_free(staging); GFS_Close(gfs);
            return 0;
        }
        cur_pool_pos += frame_bytes;

        /* Use the new public 4-bpp adder.  jo's __internal_jo_sprite_add
         * handles VRAM cursor, ID assignment, alignment, and DMA upload. */
        img.width  = g_ts_frames[i].width;
        img.height = g_ts_frames[i].height;
        img.data   = staging;
        sid = jo_sprite_add_4bits_image(&img);
        if (sid < 0) {
            jo_free(staging); GFS_Close(gfs);
            return 0;
        }
        if (first_sid < 0) {
            first_sid = sid;
        }

        /* Cumulative-duration table for tick-to-frame lookup at draw time. */
        g_ts_total_ticks += g_ts_frames[i].duration;
        g_ts_frames[i].cumulative = g_ts_total_ticks;
    }

    g_ts_base_sprite_id = first_sid;
    g_ts_atlas_loaded = 1;

    jo_free(staging);
    GFS_Close(gfs);
    return 1;
}

void title_sonic_unload(void)
{
    /* No-op: VDP1 sprite slots remain registered for the life of the
     * title state (so jo_sprite_draw3D on the slot still works).  */
}

/* ---- Frame lookup by tick ---------------------------------------------- */

int title_sonic_current_frame(unsigned int ticks)
{
    unsigned int t;
    int          lo, hi, mid;

    if (!g_ts_atlas_loaded || g_ts_frame_count == 0) {
        return -1;
    }

    /* RSDK loop=48 means: after the cumulative-duration sum is exhausted,
     * hold on the final settled frame indefinitely (not loop back to 0). */
    if (ticks >= g_ts_total_ticks) {
        return g_ts_frame_count - 1;
    }
    t = ticks;
    lo = 0;
    hi = g_ts_frame_count - 1;
    while (lo < hi) {
        mid = (lo + hi) >> 1;
        if (t < g_ts_frames[mid].cumulative) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return lo;
}

/* ---- Draw with per-frame pivot --------------------------------------- *
 *
 * The 49 frames have varying pivots (mostly the head, sometimes other
 * body parts).  RSDK's draw convention: dst_top_left = (entity_X + pivot_X,
 * entity_Y + pivot_Y).  For jo's centre-anchored slDispSprite we compute
 * the per-frame screen-centre offset:
 *     centre_x = entity_X + pivot_X + frame_W/2
 *     centre_y = entity_Y + pivot_Y + frame_H/2
 *
 * TitleLogo's TitleSonic entity is at world (252, 104) per the decomp.
 */
#define TS_ENTITY_X    252
#define TS_ENTITY_Y    104
#define TS_JO_CENTRE_X 256
#define TS_JO_CENTRE_Y 112

/* VDP1 sprite-command PMOD field encoding (VDP1 Manual §5.5.4):
 *   bits 3-5 CCM:  0 = Color Bank 16  (what we need)
 *                  4 = Color Bank 256 (jo's hard-coded default)
 *                  5 = RGB 32k        (jo's COL_32K default)
 *   bit 6 SPD:     1 = transparent pixel disable (we want 0 = enable)
 *   bit 7 ECD:     1 = end-code disable (we want 1 — VDP1 normally stops
 *                  at 0xFF/0xF nibble; for sprite char data we disable that)
 */
#define VDP1_CCM_BANK16   (0 << 3)
#define VDP1_ECD_DISABLE  (1 << 7)

void title_sonic_draw(unsigned int ticks, int z)
{
    int          f;
    FIXED        pos[XYZS];
    SPR_ATTR     attr;

    f = title_sonic_current_frame(ticks);
    if (f < 0) {
        return;
    }

    /* Centre-anchored screen position with per-frame pivot applied. */
    pos[X] = toFIXED((float)(TS_ENTITY_X + g_ts_frames[f].pivot_x +
                             (int)g_ts_frames[f].width  / 2 - TS_JO_CENTRE_X));
    pos[Y] = toFIXED((float)(TS_ENTITY_Y + g_ts_frames[f].pivot_y +
                             (int)g_ts_frames[f].height / 2 - TS_JO_CENTRE_Y));
    pos[Z] = toFIXED((float)z);
    pos[S] = toFIXED(1.0);    /* unit scale */

    /* Build SPR_ATTR for COL_16.  texno indexes __jo_sprite_def[] which
     * carries the char-data address (.adr) and size (.size).
     *
     * SGL's slDispSprite copies attr->atrb into the VDP1 cmd's PMOD word
     * (after OR-ing in SGL-managed direction/clip bits).  We compose
     * atrb as the PMOD value directly (CCM in bits 3-5, ECD in bit 7).
     * SPD bit left 0 = transparent pixels enabled.
     *
     * colno: in Color Bank 16 mode the VDP1 reads CRAM at
     *   (colno & ~0xF) | pixel_nibble.  Our 16-aligned CRAM index 2032
     * has its low 4 bits zero, so colno = TS_PAL_CRAM_INDEX directly.
     */
    attr.texno = (Uint16)(g_ts_base_sprite_id + f);
    attr.atrb  = (Uint16)(VDP1_CCM_BANK16 | VDP1_ECD_DISABLE);
    attr.colno = (Uint16)g_ts_palette_cram_index;
    attr.gstb  = 0;
    attr.dir   = 0;

    slDispSprite(pos, &attr, 0);
}

/* ---- Debugging helpers ------------------------------------------------- */
int title_sonic_frame_count(void)        { return (int)g_ts_frame_count; }
int title_sonic_total_ticks(void)        { return (int)g_ts_total_ticks; }
int title_sonic_first_sprite_id(void)    { return g_ts_base_sprite_id; }
unsigned int title_sonic_atlas_vram_bytes(void)
{
    unsigned int total = 0;
    int i;
    for (i = 0; i < g_ts_frame_count; ++i) {
        total += ((unsigned int)g_ts_frames[i].width *
                  (unsigned int)g_ts_frames[i].height) >> 1;
    }
    return total;
}
