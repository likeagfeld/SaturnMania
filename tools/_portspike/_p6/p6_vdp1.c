/* ============================================================================
 * p6_vdp1.c -- P6.5b2/b3 (Task #208): VDP1 half of the engine render backend.
 *
 * jo-side TU (the engine half lives in p6_io_main.cpp's P6_SCENE_TEST block;
 * the two TUs cannot share headers -- jo.h's SGL C decls clash with the
 * engine's C++ namespace, same split as p6_gfs.c). Compiled INSIDE the jo
 * make (Makefile P6SCENE SRCS +=) so jo.h struct layouts match the project's
 * jo configuration flags exactly.
 *
 * P6.5b3 shape: the Saturn RSDK::DrawSprite backend (p6_io_main.cpp, a
 * mechanical mirror of engine Drawing.cpp:2670-2686 + the FX_NONE arm :2785)
 * calls p6_vdp1_blit(topleftX, topleftY, w, h, sprX, sprY) per draw. Each
 * DISTINCT sheet rect is uploaded to VDP1 exactly once through jo's PROVEN
 * 8bpp path (jo_sprite_add_8bits_image -> __internal_jo_sprite_add(data, w,
 * h, COL_256), sprites.c:237-247) and cached by (sx,sy,w,h); subsequent
 * blits of the same rect only emit the draw command. A per-tick
 * jo_sprite_add would grow __jo_sprite_id without bound -- the #189
 * sprite-table overflow class (jo has NO release-build bounds check).
 * p6_w_vdp1_slots witnesses the cache population (qa_p6_draw.py D5 expects
 * EXACTLY the 16 distinct Ring anim-0 rects after the animator has cycled).
 *
 * Palette: COL_256 sprites read CRAM through jo's palette index. The engine's
 * stage palette already sits in CRAM bank 0 (NBG1's bank, written by
 * p6_vdp2.c); sprites get their OWN copy in bank 1 so jo's palette indexing
 * cannot disturb the proven NBG1 bank. The pixel gates arbitrate color
 * correctness end-to-end.
 *
 * Width rule: VDP1/jo sprite widths MUST be a multiple of 8 (memory rule
 * jo-sgl-sprite-width-mult8-shear; sprites.c:212 truncates char size to
 * width & 0x1F8). The staging copy pads every rect width up to mult-8 with
 * transparent right-columns; content stays left-aligned, and the draw centers
 * on the PADDED width so the content occupies [x, x+w) exactly.
 * ========================================================================== */
#include <jo/jo.h>

#define P6_SPR_MAXW     64 /* W12b: Player frames (Ring needed 32) */
#define P6_SPR_MAXH     64
#define P6_VDP1_NSLOTS  40 /* Ring 16 + Player working set; eviction = a
                            * declared later closer -- overflow DROPS and
                            * counts (p6_w_vdp1_drops), never overwrites */
#define P6_VDP1_NSHEETS 8

/* qa_p6_draw.py D5 witness: number of distinct rects resident on VDP1.
 * __attribute__((used)) defeats LTO name-collapse so the gate can locate it
 * in game.map (entity-atlas-loader-pattern memory rule). */
__attribute__((used)) int p6_w_vdp1_slots = 0;
/* P6.7a diagnostic: the LAST-uploaded cache key, packed
 * (sx<<20)|(sy<<12)|(w<<6)|h -- identifies an unexpected 17th rect. */
__attribute__((used)) int p6_w_vdp1_lastkey = 0;
/* W12b scale-safety: rect-cache exhaustion / oversize-frame drops. */
__attribute__((used)) int p6_w_vdp1_drops = 0;

/* W12b: MULTI-SHEET bind table. A handle indexes this table; resident
 * sheets carry their engine surface pixels, banded sheets carry the
 * SaturnSheet store slot (pixels fetched per cache miss). */
/* W12b ROOT-CAUSED (bisect A/A1/A2, 2026-06-11): a STATIC reference from
 * this jo-side (LTO) TU to the pack symbol SaturnSheet_FetchRect re-shapes
 * the mixed LTO/non-LTO link on GCC 8.2 and crashes the GFS pack open
 * (PC 0x06000956, PR in GFS_Tell) -- with the refactor otherwise identical
 * and the reference absent, the boot is GREEN. The fetch therefore arrives
 * as a RUNTIME FUNCTION POINTER set by the pack side (p6_io_main step 1.7),
 * which already references jo symbols in the proven direction. */
static int (*s_fetchFn)(int slot, int sx, int sy, int w, int h,
                        unsigned char *dst) = 0;
void p6_vdp1_set_fetch(int (*fn)(int, int, int, int, int, unsigned char *))
{
    s_fetchFn = fn;
}
static struct {
    const unsigned char *px; /* resident surface pixels, or NULL if banded */
    int w;                   /* sheet width (row stride) */
    int shtSlot;             /* SaturnSheet slot for banded sheets (px==NULL) */
} s_sheets[P6_VDP1_NSHEETS];
static int s_sheet_count = 0;

static struct {
    int sheet;        /* W12b: bind handle joins the cache key */
    int sx, sy, w, h; /* cache key: sheet rect */
    int jo_id;        /* jo sprite id of the uploaded rect */
} s_slots[P6_VDP1_NSLOTS];

static unsigned char s_stage[P6_SPR_MAXW * P6_SPR_MAXH]; /* padded upload copy */
static unsigned char s_fetch[P6_SPR_MAXW * P6_SPR_MAXH]; /* banded-miss fetch */

/* Mirror the 256-color stage palette into CRAM bank 1 once (engine RGB565,
 * same conversion as p6_vdp2.c bank 0). All Mania global sprites share the
 * stage palette, so the first bind owns the bank. */
static void p6_pal_mirror(const unsigned short *pal565)
{
    volatile Uint16 *cram1 = (volatile Uint16 *)(0x25F00000 + 0x200);
    int i;
    for (i = 0; i < 256; ++i) {
        unsigned short v  = pal565[i];
        unsigned short r5 = (v >> 11) & 0x1F;
        unsigned short g5 = ((v >> 5) & 0x3F) >> 1;
        unsigned short b5 = v & 0x1F;
        cram1[i] = (Uint16)(0x8000 | (b5 << 10) | (g5 << 5) | r5);
    }
}

/* Bind a RESIDENT engine surface. Returns the sheet handle (or -1). */
int p6_vdp1_sheet_bind(const unsigned char *sheetPixels, int sheetWidth,
                       const unsigned short *pal565)
{
    if (s_sheet_count >= P6_VDP1_NSHEETS)
        return -1;
    if (s_sheet_count == 0) {
        int i;
        p6_pal_mirror(pal565);
        p6_w_vdp1_slots = 0;
        for (i = 0; i < P6_VDP1_NSLOTS; ++i)
            s_slots[i].jo_id = -1;
    }
    s_sheets[s_sheet_count].px      = sheetPixels;
    s_sheets[s_sheet_count].w       = sheetWidth;
    s_sheets[s_sheet_count].shtSlot = -1;
    return s_sheet_count++;
}

/* W12b: bind a BANDED sheet (no resident pixels -- SaturnSheet store slot).
 * Same handle space as resident binds. */
int p6_vdp1_sheet_bind_banded(int shtSlot, int sheetWidth,
                              const unsigned short *pal565)
{
    if (s_sheet_count >= P6_VDP1_NSHEETS || shtSlot < 0)
        return -1;
    if (s_sheet_count == 0) {
        int i;
        p6_pal_mirror(pal565);
        p6_w_vdp1_slots = 0;
        for (i = 0; i < P6_VDP1_NSLOTS; ++i)
            s_slots[i].jo_id = -1;
    }
    s_sheets[s_sheet_count].px      = 0;
    s_sheets[s_sheet_count].w       = sheetWidth;
    s_sheets[s_sheet_count].shtSlot = shtSlot;
    return s_sheet_count++;
}

/* Find (or upload) the VDP1 residency slot for a (sheet, rect). */
static int p6_slot_for(int sheet, int sx, int sy, int w, int h)
{
    int i, x, y, padw;
    const unsigned char *srcPx;
    int srcStride;

    for (i = 0; i < p6_w_vdp1_slots; ++i) {
        if (s_slots[i].sheet == sheet &&
            s_slots[i].sx == sx && s_slots[i].sy == sy &&
            s_slots[i].w == w && s_slots[i].h == h)
            return i;
    }
    padw = (w + 7) & ~7;
    if (p6_w_vdp1_slots >= P6_VDP1_NSLOTS
        || padw > P6_SPR_MAXW || h > P6_SPR_MAXH) {
        ++p6_w_vdp1_drops;
        return -1;
    }

    if (s_sheets[sheet].px) {
        srcPx     = s_sheets[sheet].px + sy * s_sheets[sheet].w + sx;
        srcStride = s_sheets[sheet].w;
    }
    else {
        /* W12b banded miss: fetch the rect rows from the VDP2 band store
         * through the runtime pointer (see root-cause note above). s_fetch
         * holds the bare rect (stride w); the CACHE KEY keeps sheet sx/sy. */
        if (!s_fetchFn
            || !s_fetchFn(s_sheets[sheet].shtSlot, sx, sy, w, h, s_fetch)) {
            ++p6_w_vdp1_drops;
            return -1;
        }
        srcPx     = s_fetch;
        srcStride = w;
    }

    for (y = 0; y < h; ++y) {
        const unsigned char *src = srcPx + y * srcStride;
        unsigned char *dst       = s_stage + y * padw;
        for (x = 0; x < w; ++x) dst[x] = src[x];
        for (; x < padw; ++x) dst[x] = 0; /* transparent right-pad */
    }

    {
        jo_img_8bits img;
        int id;
        img.width  = padw;
        img.height = h;
        img.data   = s_stage;
        id = jo_sprite_add_8bits_image(&img);
        if (id < 0)
            return -1;
        i                = p6_w_vdp1_slots++;
        s_slots[i].sheet = sheet;
        s_slots[i].sx    = sx;
        s_slots[i].sy    = sy;
        s_slots[i].w     = w;
        s_slots[i].h     = h;
        s_slots[i].jo_id = id;
        p6_w_vdp1_lastkey = (sx << 20) | (sy << 12) | (w << 6) | h;
        return i;
    }
}

/* Draw a sheet rect with its TOP-LEFT at engine screen px (x,y) -- the
 * coordinate DrawSpriteFlipped receives (Drawing.cpp:2785: pos + pivot).
 * jo_sprite_draw3D positions the sprite CENTER in screen-centered coords;
 * centering on the PADDED width keeps the content at [x, x+w). */
void p6_vdp1_blit(int sheet, int x, int y, int w, int h, int sx, int sy)
{
    int slot, padw;

    if (sheet < 0 || sheet >= s_sheet_count)
        return;
    slot = p6_slot_for(sheet, sx, sy, w, h);
    if (slot < 0)
        return;

    padw = (w + 7) & ~7;
    jo_sprite_set_palette(1);
    jo_sprite_draw3D(s_slots[slot].jo_id,
                     x + padw / 2 - JO_TV_WIDTH_2,
                     y + h / 2 - JO_TV_HEIGHT_2, 450);
}
