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
/* W12b scale-safety: rect-cache exhaustion / oversize-frame drops.
 * Task #241: with LRU eviction this is now ONLY oversize (>64x64) or banded-
 * fetch-fail; a normal cache miss on a full cache EVICTS instead of dropping,
 * so a BOUND sprite rect never drops -> the player no longer blinks. */
__attribute__((used)) int p6_w_vdp1_drops = 0;
/* Task #241: LRU evictions. A cache MISS on a full cache reuses the
 * least-recently-used slot's VRAM in place via jo_sprite_replace (sprites.c:143)
 * instead of dropping the blit. >0 proves the eviction path is live; the working
 * set per frame (~20 distinct rects) is far below P6_VDP1_NSLOTS, so LRU keeps
 * the hot frames resident and only cold rects churn. */
__attribute__((used)) int p6_w_vdp1_evicts = 0;
/* W14: jo_sprite_add_8bits_image failures (the silent no-drop exit). */
__attribute__((used)) int p6_w_vdp1_joaddfail = 0;
/* W18 (Task #227, qa_p6_entdraw.py): the UNBOUND-SURFACE silent drop. A
 * DrawSprite blit whose surface never bound (handle < 0) returned early
 * WITHOUT counting (the dominant ~5944 unrendered ring/entity blits/run);
 * landed = blits that reached a valid VDP1 slot (handle >= 0, slot cached).
 * Both counted across BOTH blit arms so the gate witnesses the bind+blit. */
__attribute__((used)) int p6_w_vdp1_handle_drops = 0;
__attribute__((used)) int p6_w_vdp1_landed       = 0;
/* W18: the LAST handle passed to a dropped blit (== -1 for unbound surface;
 * identifies whether the drop was unbound vs slot-cache exhaustion). */
__attribute__((used)) int p6_w_vdp1_lastdrop_h   = -2;

/* STEP B (#246/#243): per-frame VDP1 workload, to localise the VDP1-draw
 * bottleneck (A2 showed VDP1 74% BUSY at compute-done). VDP1 rasterises every
 * pixel of a sprite's bbox (transparent texels are READ then skipped), so the
 * cost ~ total drawn pixel area. EVERY sprite is staged into a FIXED 64x64 box
 * (p6_vdp1.c:241 img.width/height = P6_SPR_MAXW/H) -> a 16x16 ring rasterises
 * 4096 px (16x overdraw). These accumulate in the blit arms + reset each frame
 * (p6_vdp1_perf_reset from p6_ghz_frame) so the capture holds the last frame's
 * totals. boxpx = the VDP1 fill workload AS DRAWN; contentpx = the ideal if
 * sprites were drawn at content size -> contentpx/boxpx = the overdraw waste. */
__attribute__((used)) int p6_w_vdp1_cmds      = 0; /* landed sprite cmds this frame */
__attribute__((used)) int p6_w_vdp1_boxpx     = 0; /* sum of 64x64 per cmd (as drawn) */
__attribute__((used)) int p6_w_vdp1_contentpx = 0; /* sum of w*h per cmd (ideal) */
__attribute__((used)) int p6_w_vdp1_maxw      = 0; /* widest single sprite this frame */
__attribute__((used)) int p6_w_vdp1_maxh      = 0; /* tallest single sprite this frame */

__attribute__((used)) void p6_vdp1_perf_reset(void) /* called at p6_ghz_frame top */
{
    p6_w_vdp1_cmds = 0; p6_w_vdp1_boxpx = 0; p6_w_vdp1_contentpx = 0;
    p6_w_vdp1_maxw = 0; p6_w_vdp1_maxh = 0;
}

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
    int lastUse;      /* Task #241: LRU stamp (s_useclock at last hit/fill) */
} s_slots[P6_VDP1_NSLOTS];
/* Task #241: monotonic LRU clock; the slot with the smallest lastUse is the
 * eviction victim when the cache is full and a new rect misses. */
static int s_useclock = 0;

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
        s_useclock = 0;
        for (i = 0; i < P6_VDP1_NSLOTS; ++i) {
            s_slots[i].jo_id   = -1;
            s_slots[i].sheet   = -1;
            s_slots[i].lastUse = 0;
        }
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
        s_useclock = 0;
        for (i = 0; i < P6_VDP1_NSLOTS; ++i) {
            s_slots[i].jo_id   = -1;
            s_slots[i].sheet   = -1;
            s_slots[i].lastUse = 0;
        }
    }
    s_sheets[s_sheet_count].px      = 0;
    s_sheets[s_sheet_count].w       = sheetWidth;
    s_sheets[s_sheet_count].shtSlot = shtSlot;
    return s_sheet_count++;
}

/* Find (or upload) the VDP1 residency slot for a (sheet, rect).
 *
 * Task #241 (was the "characters blink in and out" bug -- MEASURED: a saturated
 * 40-slot fill-once cache dropped 1,333 bound rects/run): the cache is now LRU,
 * not fill-once. A HIT touches the slot's LRU stamp. A MISS stages the rect into
 * a FIXED P6_SPR_MAXW x P6_SPR_MAXH (64x64) box (content top-left, transparent
 * pad) and either (a) cold-fills a new jo sprite while the cache is below
 * capacity, or (b) EVICTS the least-recently-used slot and overwrites its VDP1
 * VRAM in place via jo_sprite_replace (sprites.c:143 -- which requires identical
 * dimensions, hence the fixed 64x64 slot). A BOUND rect therefore ALWAYS gets a
 * slot; p6_w_vdp1_drops now only fires on an oversize frame (>64x64) or a banded-
 * fetch failure. The per-frame working set (~20 distinct rects) is far below
 * P6_VDP1_NSLOTS, so the player's hot frames stay resident and only cold rects
 * churn through the victim slot. jo_sprite_add still runs at most NSLOTS times
 * total (cold-fill only), so the #189 sprite-table overflow class cannot recur. */
static int p6_slot_for(int sheet, int sx, int sy, int w, int h)
{
    int i, x, y, victim;
    const unsigned char *srcPx;
    int srcStride;
    jo_img_8bits img;

    for (i = 0; i < p6_w_vdp1_slots; ++i) {
        if (s_slots[i].sheet == sheet &&
            s_slots[i].sx == sx && s_slots[i].sy == sy &&
            s_slots[i].w == w && s_slots[i].h == h) {
            s_slots[i].lastUse = ++s_useclock; /* LRU touch on hit */
            return i;
        }
    }
    /* A fixed 64x64 slot cannot hold an oversize frame -> genuine drop. */
    if (w > P6_SPR_MAXW || h > P6_SPR_MAXH) {
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

    /* Stage into a FIXED 64x64 box: content top-left, the rest transparent
     * (palette index 0 -- VDP1 sprite transparent-pixel processing skips it).
     * jo_sprite_replace re-DMAs exactly width*height bytes, so the staged box
     * dimensions MUST equal the pre-allocated slot's (64x64). */
    for (y = 0; y < P6_SPR_MAXH; ++y) {
        unsigned char *dst = s_stage + y * P6_SPR_MAXW;
        if (y < h) {
            const unsigned char *src = srcPx + y * srcStride;
            for (x = 0; x < w; ++x) dst[x] = src[x];
            for (; x < P6_SPR_MAXW; ++x) dst[x] = 0;
        }
        else {
            for (x = 0; x < P6_SPR_MAXW; ++x) dst[x] = 0;
        }
    }
    img.width  = P6_SPR_MAXW;
    img.height = P6_SPR_MAXH;
    img.data   = s_stage;

    if (p6_w_vdp1_slots < P6_VDP1_NSLOTS) {
        /* Cold-fill: allocate a fresh fixed-size jo sprite. */
        int id = jo_sprite_add_8bits_image(&img);
        if (id < 0) {
            ++p6_w_vdp1_joaddfail; /* W14: the silent no-drop exit */
            return -1;
        }
        victim = p6_w_vdp1_slots++;
        s_slots[victim].jo_id = id;
    }
    else {
        /* Cache full: evict the least-recently-used slot, reuse its VRAM. */
        int oldest = s_slots[0].lastUse;
        victim = 0;
        for (i = 1; i < P6_VDP1_NSLOTS; ++i) {
            if (s_slots[i].lastUse < oldest) {
                oldest = s_slots[i].lastUse;
                victim = i;
            }
        }
        if (s_slots[victim].jo_id < 0) { /* defensive: never cold-filled */
            ++p6_w_vdp1_drops;
            return -1;
        }
        jo_sprite_replace(&img, s_slots[victim].jo_id);
        ++p6_w_vdp1_evicts;
    }

    s_slots[victim].sheet   = sheet;
    s_slots[victim].sx      = sx;
    s_slots[victim].sy      = sy;
    s_slots[victim].w       = w;
    s_slots[victim].h       = h;
    s_slots[victim].lastUse = ++s_useclock;
    p6_w_vdp1_lastkey       = (sx << 20) | (sy << 12) | (w << 6) | h;
    return victim;
}

/* Draw a sheet rect with its TOP-LEFT at engine screen px (x,y) -- the
 * coordinate DrawSpriteFlipped receives (Drawing.cpp:2785: pos + pivot).
 * jo_sprite_draw3D positions the sprite CENTER in screen-centered coords;
 * the slot is a fixed P6_SPR_MAXW x P6_SPR_MAXH box with content in the
 * top-left corner, so centering on the box (offset 32) keeps the content at
 * engine [x, x+w) x [y, y+h). */
void p6_vdp1_blit(int sheet, int x, int y, int w, int h, int sx, int sy)
{
    int slot;

    if (sheet < 0 || sheet >= s_sheet_count) {
        ++p6_w_vdp1_handle_drops; /* W18: unbound-surface silent drop */
        p6_w_vdp1_lastdrop_h = sheet;
        return;
    }
    slot = p6_slot_for(sheet, sx, sy, w, h);
    if (slot < 0)
        return;

    ++p6_w_vdp1_landed; /* W18: a blit that reached a valid VDP1 slot */
    /* STEP B: per-frame VDP1 workload (box-as-drawn vs content-ideal). */
    ++p6_w_vdp1_cmds; p6_w_vdp1_boxpx += P6_SPR_MAXW * P6_SPR_MAXH;
    p6_w_vdp1_contentpx += w * h;
    if (w > p6_w_vdp1_maxw) p6_w_vdp1_maxw = w;
    if (h > p6_w_vdp1_maxh) p6_w_vdp1_maxh = h;
    jo_sprite_set_palette(1);
    /* Task #241: the slot is a fixed P6_SPR_MAXW x P6_SPR_MAXH box with content
     * in the top-left corner; the box CENTER sits at content-top-left + 32, so
     * placing the center there lands the content at engine top-left (x,y). */
    jo_sprite_draw3D(s_slots[slot].jo_id,
                     x + P6_SPR_MAXW / 2 - JO_TV_WIDTH_2,
                     y + P6_SPR_MAXH / 2 - JO_TV_HEIGHT_2, 450);
}

/* W14c (Task #227): flipped draw -- the DrawSprite FX_FLIP arm. VDP1 HF/VF
 * (CMDCTRL Dir bits, ST-013-R3 sec 5.5.4) mirror the PIXELS inside the
 * part's bbox; jo exposes them as the h/v flip attribute toggles
 * (sprites.h:292-312). The caller passes the RSDK world TOP-LEFT already
 * flip-adjusted (Drawing.cpp:2796-2808: x - width - pivotX for FLIP_X).
 *
 * Task #241: the slot is now a FIXED 64x64 box (content top-left). VDP1 HF
 * mirrors the WHOLE box around its center, moving content from columns [0,w)
 * to [64-w,64); VF mirrors rows [0,h) to [64-h,64). To keep the (flipped)
 * content top-left at engine (x,y), the box origin compensates by (64-w) in X
 * when flipped (and (64-h) in Y) -- which reduces to the symmetric center
 * formula below (32 == P6_SPR_MAXW/2 == P6_SPR_MAXH/2). */
void p6_vdp1_blit_flipped(int sheet, int x, int y, int w, int h, int sx, int sy,
                          int flipX, int flipY)
{
    int slot;

    if (sheet < 0 || sheet >= s_sheet_count) {
        ++p6_w_vdp1_handle_drops; /* W18: unbound-surface silent drop */
        p6_w_vdp1_lastdrop_h = sheet;
        return;
    }
    slot = p6_slot_for(sheet, sx, sy, w, h);
    if (slot < 0)
        return;

    ++p6_w_vdp1_landed; /* W18: a blit that reached a valid VDP1 slot */
    ++p6_w_vdp1_cmds; p6_w_vdp1_boxpx += P6_SPR_MAXW * P6_SPR_MAXH;
    p6_w_vdp1_contentpx += w * h;
    if (w > p6_w_vdp1_maxw) p6_w_vdp1_maxw = w;
    if (h > p6_w_vdp1_maxh) p6_w_vdp1_maxh = h;
    jo_sprite_set_palette(1);
    if (flipX)
        jo_sprite_enable_horizontal_flip();
    if (flipY)
        jo_sprite_enable_vertical_flip();
    jo_sprite_draw3D(s_slots[slot].jo_id,
                     (flipX ? x + w - P6_SPR_MAXW / 2 : x + P6_SPR_MAXW / 2) - JO_TV_WIDTH_2,
                     (flipY ? y + h - P6_SPR_MAXH / 2 : y + P6_SPR_MAXH / 2) - JO_TV_HEIGHT_2, 450);
    if (flipX)
        jo_sprite_disable_horizontal_flip();
    if (flipY)
        jo_sprite_disable_vertical_flip();
}
