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

/* CP4c BLUE-SCREEN FIX (this session): the VDP1 slot cache stages every sprite
 * into a FIXED box (content top-left, transparent pad). The GHZ gameplay set
 * (Ring/Player/badniks) fits 64x64, but the FRONT-END Logos splash frames are
 * large UI images -- MEASURED from Logos/Logos.bin: Sega 187x58, CW 150x85,
 * HC 92x89, PWG 147x71 (every one > 64 in W and/or H). With a 64x64 box, every
 * Logos blit hit `w > P6_SPR_MAXW || h > P6_SPR_MAXH` in p6_slot_for and DROPPED
 * (MEASURED p6_w_vdp1_drops == draw_calls, vdp1_landed == 0 -> uniform-blue
 * screen). A single VDP1 sprite supports up to 504(W,mult-8) x 255(H) per
 * ST-013-R3 sec 6.6 (CMDSIZE), so the 187x89 max fits ONE sprite each. The
 * FRONT-END flavor therefore uses a 192x96 box (covers the largest frame,
 * width mult-8 per jo-sgl-sprite-width-mult8-shear) with only 8 slots (the
 * Logos working set is the 4 logo frames + a margin; far below 8 -> the LRU
 * eviction path never fires). 8 * 192*96 = 147,456 px < JO_VDP1_USER_AREA_SIZE
 * (0x71D38 = 466,232 B). The DEFAULT (GHZ) build is BYTE-IDENTICAL (64x64x40);
 * only -DP6_FRONTEND_LOGOS enlarges the box. The two staging buffers are also
 * relocated off .bss to a verified-free cart window in the FRONT-END build
 * (192*96*2 = 36 KB would breach the ~1.2 KB WRAM-H headroom under ANIMPAK);
 * the GHZ build keeps them as the original 64x64 .bss arrays. */
#if defined(P6_FRONTEND_TITLE)
/* CP5b.1 (Task #268): the TITLE logo frames are TALLER than the Logos splash
 * frames -- MEASURED from Title/Logo.bin (convert_ring_sprite.parse_spr): the
 * EMBLEM (anim 0 "Logo Wings") is 144x144, the Ribbon Center (anim 3) is 176x52,
 * the Game Title (anim 4) 137x46, the Ribbon Wave (anim 2, shown after FlashIn)
 * 56x72. MAX frame = 176x144. The CP4b 192x96 Logos box would DROP the 144-tall
 * emblem (p6_slot_for: `h > P6_SPR_MAXH` -> oversize drop -> the ring never blits
 * -> the iconic SONIC-MANIA emblem missing). So the TITLE flavor uses a 192x160
 * box (176->192 mult-8 width per jo-sgl-sprite-width-mult8-shear; 144->160 height).
 * A single VDP1 sprite supports 504(W,mult-8)x255(H) per ST-013-R3 sec 6.6, so
 * 176x144 fits ONE sprite each. 8 * 192*160 = 245,760 px < JO_VDP1_USER_AREA_SIZE
 * (0x71D38 = 466,232 B). Same 8 slots (the Title working set = ~6 logo pieces +
 * the electricity ring, below 8 -> no eviction). The DEFAULT (GHZ) build is
 * BYTE-IDENTICAL (64x64x40); a Logos-only build keeps 192x96. */
#define P6_SPR_MAXW     192 /* Title: widest frame 176 -> mult-8 pad 192 */
#define P6_SPR_MAXH     160 /* Title: tallest frame 144 (emblem) -> 160 */
#define P6_VDP1_NSLOTS  8   /* Title working set = ~6 logo pieces + ring; no eviction */
#elif defined(P6_FRONTEND_LOGOS)
#define P6_SPR_MAXW     192 /* Logos: widest frame 187 -> mult-8 pad 192 */
#define P6_SPR_MAXH     96  /* Logos: tallest frame 89 -> 96 */
#define P6_VDP1_NSLOTS  8   /* Logos working set = 4 logo frames; no eviction */
#else
#define P6_SPR_MAXW     64 /* W12b: Player frames (Ring needed 32) */
#define P6_SPR_MAXH     64
#define P6_VDP1_NSLOTS  40 /* Ring 16 + Player working set; eviction = a
                            * declared later closer -- overflow DROPS and
                            * counts (p6_w_vdp1_drops), never overwrites */
#endif
/* GHZ1 parity P2 (#181/#247): staged sheets bind here -- SONIC1/2/3, ITEMS,
 * DISPLAY, SHIELDS, TAILS1, GLOBJ, GHZOBJ (GHZ/Objects.gif, the bridge planks +
 * badniks + GHZ content objects), and the Batch 2 effect sheets EXPLODE
 * (Global/Explosions.gif) + ANIMALS (Global/Animals.gif). Must stay >= the actual
 * bind DEMAND: each surface that binds (a staged .SHT via the banded path, OR an
 * unstaged sheet that LoadSpriteSheet gave resident pixels) consumes exactly one
 * bind-table entry. MEASURED root cause of the bridge not drawing (#181): at 8,
 * the 9th bind (GHZOBJ) hit s_sheet_count>=NSHEETS and returned -1 (handle -1 ->
 * plank blits dropped). BADNIK-VIS (2026-06-18): the SAME class re-bit -- Batch 2's
 * Explosions/Animals decoded to resident pixels (Sprite.cpp:994) + bound via the
 * pixels path, so bind_demand=11 > the 9 slots and BOTH surf 13 (Explosions) AND
 * surf 16 (GHZ/Objects.gif = badniks+bridge+SpikeLog) bound -1 -> invisible
 * (MEASURED bind_log16). The GHZ1 scene's bind DEMAND is 11 (the 9 banded .SHT +
 * 2 resident-PIXEL engine/player surfaces, bind_log16 surf 7 + surf 15). Sized to
 * 12 = 11 measured + 1 margin so GHZ/Objects.gif (surf 16, last by index) binds.
 * (12 * 12 B = 144 B .bss; int8 handle table holds 0..11.) NOTE: growing this and
 * SATURNSHEET_SLOTS TOGETHER tripped the #228 orphan-.bss overlap (GFS GfsMng ptr
 * corruption -> boot trap PC=0x06000956); keep the .bss delta minimal -- raise only
 * this table (+24 B), leave SATURNSHEET_SLOTS and the staged-sheet set unchanged. */
#define P6_VDP1_NSHEETS 12

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
/* CP4c BLUE-SCREEN diag: WHICH drop branch in p6_slot_for fired last, + the
 * last attempted fetch's (slot,w,h). 1=oversize(w/h>box) 2=no-fetch-fn-or-
 * fetch-failed 3=jo_sprite_add failed. lastfetch packs (shtSlot<<24)|(w<<12)|h ;
 * fetchret = the s_fetchFn return (1 ok / 0 fail). FRONT-END ONLY so the GHZ
 * hot-path p6_slot_for (60 fps-sensitive) stays byte-identical. */
#if defined(P6_FRONTEND_LOGOS)
__attribute__((used)) int p6_w_vdp1_dropreason = 0;
__attribute__((used)) int p6_w_vdp1_lastfetch  = 0;
__attribute__((used)) int p6_w_vdp1_fetchret   = -1;
__attribute__((used)) int p6_w_vdp1_lastwh     = 0; /* (w<<16)|h of the last slot_for call */
#endif
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

/* CP4c BLUE-SCREEN FIX: in the FRONT-END flavor the 192x96 box makes these two
 * buffers 18,432 B each (36 KB total) -- relocate them to a VERIFIED-FREE cart
 * window so .bss (and thus _end vs ANIMPAK) is unchanged. 0x226E0000 is past the
 * camera-local pool / DORM / editableVarList cart structures (highest is
 * 0x226D4000) and well before the GFS windows (0x22700000); the FRONT-END build
 * never loads GHZ so those pool structures are inert anyway, but this address is
 * disjoint regardless. Cache-through alias (0x226E....) -- written by the SH-2,
 * read by jo's DMA into VDP1 VRAM; no coherency purge needed (the existing GHZ
 * staging copies have the same producer/consumer and rely on the same property).
 * The DEFAULT (GHZ) build keeps the original 64x64 .bss arrays (byte-identical). */
#if defined(P6_FRONTEND_TITLE)
/* CP5b.1 (Task #268): the 192x160 TITLE box makes each buffer 30,720 B (0x7800);
 * 61,440 B total -- relocate them to the SAME verified-free cart window the Logos
 * flavor used (0x226E0000, past the highest cart structure 0x226D4000, before the
 * GFS windows 0x22700000). s_fetch follows s_stage by 0x7800; both end at
 * 0x226EF000, 0x11000 (68 KB) below the GFS windows -- disjoint. Cache-through alias
 * (written by SH-2, DMA'd into VDP1 VRAM by jo; same producer/consumer property as
 * the GHZ staging copies -> no coherency purge). The front-end build never loads
 * GHZ so the pool structures at 0x226D4000 are inert regardless. */
static unsigned char *const s_stage = (unsigned char *)0x226E0000u; /* 30,720 B */
static unsigned char *const s_fetch = (unsigned char *)0x226E7800u; /* 30,720 B */
#elif defined(P6_FRONTEND_LOGOS)
static unsigned char *const s_stage = (unsigned char *)0x226E0000u; /* 18,432 B */
static unsigned char *const s_fetch = (unsigned char *)0x226E4800u; /* 18,432 B */
#else
static unsigned char s_stage[P6_SPR_MAXW * P6_SPR_MAXH]; /* padded upload copy */
static unsigned char s_fetch[P6_SPR_MAXW * P6_SPR_MAXH]; /* banded-miss fetch */
#endif

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

#if defined(P6_FRONTEND_LOGOS)
    p6_w_vdp1_lastwh = (w << 16) | (h & 0xFFFF);
#endif
    for (i = 0; i < p6_w_vdp1_slots; ++i) {
        if (s_slots[i].sheet == sheet &&
            s_slots[i].sx == sx && s_slots[i].sy == sy &&
            s_slots[i].w == w && s_slots[i].h == h) {
            s_slots[i].lastUse = ++s_useclock; /* LRU touch on hit */
            return i;
        }
    }
    /* A fixed P6_SPR_MAXW x P6_SPR_MAXH slot cannot hold an oversize frame. */
    if (w > P6_SPR_MAXW || h > P6_SPR_MAXH) {
        ++p6_w_vdp1_drops;
#if defined(P6_FRONTEND_LOGOS)
        p6_w_vdp1_dropreason = 1;
#endif
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
#if defined(P6_FRONTEND_LOGOS)
        p6_w_vdp1_lastfetch = ((s_sheets[sheet].shtSlot & 0xFF) << 24)
                            | ((w & 0xFFF) << 12) | (h & 0xFFF);
        p6_w_vdp1_fetchret = s_fetchFn
            ? s_fetchFn(s_sheets[sheet].shtSlot, sx, sy, w, h, s_fetch) : -2;
        if (p6_w_vdp1_fetchret <= 0) {
            ++p6_w_vdp1_drops;
            p6_w_vdp1_dropreason = 2;
            return -1;
        }
#else
        if (!s_fetchFn
            || !s_fetchFn(s_sheets[sheet].shtSlot, sx, sy, w, h, s_fetch)) {
            ++p6_w_vdp1_drops;
            return -1;
        }
#endif
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
#if defined(P6_FRONTEND_LOGOS)
            p6_w_vdp1_dropreason = 3;
#endif
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
