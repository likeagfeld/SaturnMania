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
/* CP5b.1 (Task #268): the TITLE logo frames are taller than the Logos splash --
 * the EMBLEM (anim 0) is 144x144, Ribbon Center 176x52 -> needs a 192x160 box.
 * CP5b.2 (Task #269): TitleSonic (the ring-center head, Title/Sonic.bin) is WIDER
 * still -- MEASURED (convert_ring_sprite.parse_spr): anim 0 "Sonic" (the head/body
 * pop-up, 49 frames) sweeps up to 241x137 (f30 = 241 wide; f15/f17 = 137 tall;
 * the leaning mid-poses are wide), anim 1 "Finger Wave" (12 frames) up to 50x63.
 * OVERALL MAX frame (logo emblem + sonic head) = 241x137. The CP5b.1 192x160 box
 * would DROP the 241-wide head (p6_slot_for: `w > P6_SPR_MAXW` -> oversize drop ->
 * the head never blits -> black ring interior, the CP5b.1 logo class). So the TITLE
 * flavor grows to a 248x160 box (241->248 mult-8 width per jo-sgl-sprite-width-mult8-
 * shear; 137 fits the 160 that already covered the 144 emblem). A single VDP1 sprite
 * supports 504(W,mult-8)x255(H) per ST-013-R3 sec 6.6 (CMDSIZE), so 241x137 fits ONE
 * sprite each. 10 * 248*160 = 396,800 px < JO_VDP1_USER_AREA_SIZE (0x71D38 = 466,232
 * B). NSLOTS 8->10: the Title working set is now ~6 logo pieces + the electricity ring
 * + the Sonic head + finger (~9 distinct rects); 10 keeps them resident with no
 * eviction (the LRU path still handles any overflow gracefully per
 * ghz-vdp1-sprite-residency-lru). The DEFAULT (GHZ) build is BYTE-IDENTICAL (64x64x40);
 * a Logos-only build keeps 192x96. */
#define P6_SPR_MAXW     248 /* Title: widest frame 241 (sonic head) -> mult-8 pad 248 */
#define P6_SPR_MAXH     160 /* Title: tallest frame 144 (emblem) / 137 (head) -> 160 */
/* Phase 2 (Task #279): with the content-size N-bucket below, s_slots[] is now ONLY
 * bucket 3 -- the 248x160 catch-all for w>160 frames, which is JUST the Sonic body
 * (241x137; every other title sprite + the <=160-wide ring frames route to the
 * smaller buckets). 4 slots cover the body's play-once twirl (LRU). Shrinking 10->4
 * frees 6*sizeof(P6Vdp1Slot)=168 B of .bss so the N-bucket arrays (s_buck0/1/2) are a
 * NET-NEGATIVE WRAM-H change -- the +120 B naive version tripped the #228 boot trap
 * (master PC 0x06000956), the title flavor's ceiling being far tighter than GHZ's. */
#define P6_VDP1_NSLOTS  4   /* Title BIG bucket (Sonic body catch-all); buckets carry the rest */
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

typedef struct {
    int sheet;        /* W12b: bind handle joins the cache key */
    int sx, sy, w, h; /* cache key: sheet rect */
    int jo_id;        /* jo sprite id of the uploaded rect */
    int lastUse;      /* Task #241: LRU stamp (s_useclock at last hit/fill) */
} P6Vdp1Slot;
static P6Vdp1Slot s_slots[P6_VDP1_NSLOTS]; /* LARGE box: P6_SPR_MAXW x P6_SPR_MAXH */
#if defined(P6_FRONTEND_TITLE)
/* CP5b.7 -> Phase 2 (Task #279): CONTENT-SIZE N-BUCKET VDP1 pool.
 *
 * MEASURED ROOT CAUSE (qa_p6_perf --scene title, per-section FRT): the title is
 * COMPUTE-bound, not VDP1-fill-bound -- DrawLists 42.3 ms dominates the 48.9 ms
 * master frame; jo-body/slSynch wait = 0.00 ms (the master never waits on VDP1).
 * The DrawLists cost is the per-sprite STAGING (a CPU byte-copy of the fixed box
 * into s_stage) + jo_sprite_replace DMA on every cache MISS. Every sprite paid the
 * one 248x160 box (39,680 B) regardless of content, so a churning animation
 * (ribbon-wave tails, electricity ring, unfurl) re-staged 39,680 B per cycle frame.
 * The earlier "VDP1-fill-bound" read of the P6_TITLE_NODRAW A/B was wrong: skipping
 * the emit also skips this CPU staging. CP5b.7's single 64x64 second pool (8.6 ->
 * 13.3 fps) cut it for tiny sprites; Phase 2 generalises to N content-size buckets
 * so EVERY sprite stages only the smallest box that holds it.
 *
 * Bucket boundaries from the MEASURED Title frame dims (parse_spr on
 * extracted/Data/Sprites/Title/{Logo,Sonic,Electricity}.bin):
 *   b0 64x80   : finger 50x63, ribbon-WAVE tails 56x72, copyright 45x8, small ring
 *   b1 192x64  : ribbon-center 176x52, wordmark 137x46, ring-bottom 120x25,
 *                press-start 174x14
 *   b2 160x160 : emblem 144x144, ribbon-UNFURL 118x85, mid electricity-ring frames
 *   b3 248x160 : Sonic BODY 241x137 + CATCH-ALL (w>160). Box UNCHANGED from the old
 *                large pool, so nothing that drew before can newly drop (every ring
 *                frame is <=160 wide; >160-tall frames already dropped at 248x160).
 * Smallest-first: a sprite takes the first bucket whose box holds BOTH (w,h). Each
 * bucket is UNIFORM-box so jo_sprite_replace's same-size in-place LRU reuse holds
 * (jo's VDP1 allocator is append-only -- mixing sizes in one pool would leak). VRAM
 * (1 B/px, 8bpp paletted): 6*5,120 + 6*12,288 + 6*25,600 + 4*39,680 = 416,768 B <
 * JO_VDP1_USER_AREA_SIZE 466,232 (49,464 B margin). b3 reuses the existing s_slots[]
 * (n_max capped at 4) + the p6_w_vdp1_slots coldn, so the GHZ/Logos builds (no
 * P6_FRONTEND_TITLE) keep the single-pool path byte-identical. */
#define P6_NB 4
static P6Vdp1Slot s_buck0[6];                    /* 64x80   */
static P6Vdp1Slot s_buck1[6];                    /* 192x64  */
static P6Vdp1Slot s_buck2[6];                    /* 160x160 */
/* bucket 3 (248x160 catch-all) reuses s_slots[] + p6_w_vdp1_slots. */
static int s_buck0n = 0, s_buck1n = 0, s_buck2n = 0;
static const struct { int bw, bh; } P6_BUCK[P6_NB] = {
    { 64, 80 }, { 192, 64 }, { 160, 160 }, { P6_SPR_MAXW, P6_SPR_MAXH }
};
/* smallest-first bucket select; shared by the router AND the stride cull so the
 * box used for placement and the box used for the off-screen-wrap check agree. */
static int p6_bucket_for(int w, int h)
{
    int i;
    for (i = 0; i < P6_NB; ++i)
        if (w <= P6_BUCK[i].bw && h <= P6_BUCK[i].bh)
            return i;
    return -1;
}

/* CP5b.7 content-size step (Task #277): EAGER PRE-ALLOCATION of every bucket slot
 * at its full BOX size, ONCE, before any draw.
 *
 * THE FILL FIX: the N-bucket above cut the single-box waste (3.47 -> 0.92 screens)
 * but each sprite still drew its WHOLE bucket box -- MEASURED 39% of the on-hardware
 * VDP1 fill is transparent padding (e.g. ring-bottom 120x25 in a 192x64 box). VDP1
 * rasterises every pixel of a sprite's CMDSIZE bbox (transparent texels read+skipped),
 * so the fill cost == the drawn CMDSIZE area. jo registers each sprite's CMDSIZE in
 * __jo_sprite_def[id] {width=Hsize, height=Vsize, adr=CGadr, size=HVsize} -- the SGL
 * TEXTURE table fed to slInitSystem (core.c:192 casts __jo_sprite_def to TEXTURE*).
 * slDispSprite (jo SGL path, sprites.c:447, scale 1.0) draws the sprite at that
 * registered hardware size. So to draw a sprite at CONTENT (w mult-8, h) instead of
 * the box, p6_title_restage_content (below) DMAs the content packed at content-width
 * STRIDE into the slot's reserved VRAM and OVERWRITES the slot's __jo_sprite_def
 * width/height/size to content -- exactly the TEXDEF a content-sized add would produce
 * (ST-238-R1 sec, TEXDEF(h,v,presize) HVsize = ((h&0x1f8)<<5)|v == jo
 * __internal_jo_sprite_add:212). VDP1 then rasterises ONLY the content rows/cols.
 *
 * WHY EAGER PRE-ALLOC: jo's VDP1 allocator (__jo_get_next_sprite_address,
 * sprites.c:74) is APPEND-ONLY and computes the NEXT sprite's VRAM address from the
 * PREVIOUS sprite's __jo_sprite_def width*height. If a cold-fill shrank a slot's
 * width/height to content, the next cold-fill would place its sprite into this slot's
 * box tail (overlap) -- and a later eviction restaging a LARGER content into this slot
 * would corrupt the neighbour. Reserving EVERY slot at the BOX footprint up front
 * (contiguously, all jo_sprite_add calls done before any restage) makes the allocator
 * never run again for the buckets, so freely mutating each slot's width/height/size to
 * content per (re)stage is safe -- the reserved box region (boxw*boxh) always holds the
 * content (content fits its bucket). This is jo-pool-stale-core-o-gotcha-clean (no
 * post-init jo_sprite_add) and #189-clean (__jo_sprite_id is bounded by the 22 slots).
 * TITLE flavor only; the GHZ/Logos p6_vdp1.o is byte-identical (this block is #if'd). */
typedef struct { P6Vdp1Slot *slots; int n; int bw, bh; } P6Bucket;
static P6Bucket s_buckets[P6_NB];
static int s_buckets_prealloc = 0;
/* Defined below the s_stage/s_fetch cart-buffer declarations (they DMA through s_stage):
 *   p6_title_alloc_box     -- reserve one box-sized jo sprite (permanent slot VRAM).
 *   p6_title_restage_content -- DMA content packed at content-width stride + set the
 *                              slot's __jo_sprite_def CMDSIZE to content (the fill fix). */
static int p6_title_alloc_box(int bw, int bh);
static int p6_title_restage_content(int jo_id, const unsigned char *srcPx,
                                    int srcStride, int w, int h);
#endif
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
/* CP5b.1 (Task #268): the 192x160 TITLE box made each buffer 30,720 B at 0x226E0000.
 * CP5b.2 (Task #269): the 248x160 box makes each buffer P6_SPR_MAXW*P6_SPR_MAXH =
 * 248*160 = 39,680 B (0x9B00); 79,360 B total (8bpp, 1 byte/px -- these are PADDED
 * paletted upload copies, NOT 2 bytes/px). KEPT at the CP5b.1 0x226E0000 window:
 * TSONIC.SHT is staged BANDED but NOT made resident (the 1024-wide MakeResident
 * boundary-case HANGS -- see p6_io_main.cpp's TSONIC stage block), so the cart
 * resident-sheet store stays at its 11-sheet high-water ~0x225E8000, well BELOW these
 * buffers; and the front-end band store is relocated DOWN to 0x22720000 (SaturnSheet.
 * cpp), so 0x226E0000 is clear of it too. s_stage @ 0x226E0000 (0xA000 reserved,
 * covers 39,680), s_fetch @ 0x226EA000 (0xA000) -> ends 0x226F4000, below the GFS
 * windows (0x22700000) and the resident-store END (also 0x22700000) -- disjoint.
 * Cache-through alias (written by SH-2, DMA'd into VDP1 VRAM by jo; same producer/
 * consumer property as the GHZ staging copies -> no coherency purge). */
static unsigned char *const s_stage = (unsigned char *)0x226E0000u; /* 39,680 B */
static unsigned char *const s_fetch = (unsigned char *)0x226EA000u; /* 39,680 B */
#elif defined(P6_FRONTEND_LOGOS)
static unsigned char *const s_stage = (unsigned char *)0x226E0000u; /* 18,432 B */
static unsigned char *const s_fetch = (unsigned char *)0x226E4800u; /* 18,432 B */
#else
static unsigned char s_stage[P6_SPR_MAXW * P6_SPR_MAXH]; /* padded upload copy */
static unsigned char s_fetch[P6_SPR_MAXW * P6_SPR_MAXH]; /* banded-miss fetch */
#endif

#if defined(P6_FRONTEND_TITLE)
/* CP5b.7 content-size (#277): allocate one box-sized jo sprite from a zeroed staging
 * buffer; returns the jo id (the slot's PERMANENT VRAM reservation) or -1. Called only
 * by p6_title_ensure_prealloc -- after prealloc the buckets never jo_sprite_add again,
 * so the append-only allocator (sprites.c:74) runs a fixed 22 times total. */
static int p6_title_alloc_box(int bw, int bh)
{
    jo_img_8bits img;
    int n = bw * bh, i;
    for (i = 0; i < n; ++i) s_stage[i] = 0;   /* transparent box */
    img.width = bw; img.height = bh; img.data = s_stage;
    return jo_sprite_add_8bits_image(&img);
}

/* Restage a slot's jo sprite to draw at CONTENT size: pack the content rows at content-
 * width (mult-8) STRIDE into s_stage, DMA into the slot's reserved VRAM, and set the
 * slot's __jo_sprite_def {width=Hsize, height=Vsize, size=HVsize} to the content TEXDEF.
 * The slot's adr (CGadr, VRAM base) is unchanged -- the box reservation (boxw*boxh) holds
 * the smaller content (content fits its bucket). VDP1 then rasterises ONLY content rows/
 * cols (the 39% box padding is gone). Returns the mult-8 padded width (the drawn width). */
static int p6_title_restage_content(int jo_id, const unsigned char *srcPx,
                                    int srcStride, int w, int h)
{
    int pw = (w + 7) & ~7;          /* content width padded to the VDP1 mult-8 unit */
    int x, y;
    for (y = 0; y < h; ++y) {
        unsigned char *dst = s_stage + y * pw;
        const unsigned char *src = srcPx + y * srcStride;
        for (x = 0; x < w; ++x) dst[x] = src[x];
        for (; x < pw; ++x) dst[x] = 0;        /* mult-8 right pad transparent */
    }
    __jo_sprite_def[jo_id].width  = (unsigned short)pw;
    __jo_sprite_def[jo_id].height = (unsigned short)h;
    /* HVsize TEXDEF (== jo __internal_jo_sprite_add:212 / SGL ST-238-R1 TEXDEF macro):
     * the hardware CMDSIZE VDP1 rasterises. pw is mult-8 so (pw & 0x1f8) == pw (pw<=504). */
    __jo_sprite_def[jo_id].size   = (unsigned short)(JO_MULT_BY_32(pw & 0x1f8) | (h & 0xff));
    /* DMA the content-packed bytes (pw*h, 8bpp = 1 B/px) into the slot's reserved VRAM
     * base (mirrors jo_sprite_replace's copy, sprites.c:172-174, COL_256). */
    jo_dma_copy(s_stage,
                (void *)(JO_VDP1_VRAM + JO_MULT_BY_8(__jo_sprite_def[jo_id].adr)),
                (unsigned int)(pw * h));
    return pw;
}

/* =============================================================================
 * TASK 2 (this session): RSDK::FillScreen on Saturn -- the title INTRO fade/flash.
 *
 * The decomp TitleSetup_Draw_FadeBlack calls RSDK.FillScreen(0x000000, timer,
 * timer-128, timer-256) (black fade-in, timer 1024->0) and Draw_Flash calls
 * RSDK.FillScreen(0xF0F0F0, timer, timer-128, timer-256) (white flash, timer
 * 0x300->0). The PC FillScreen (Drawing.cpp:586) does a per-channel alpha blend
 * of `color` over the software framebuffer -- which on Saturn is the 1-element
 * frameBuffer[1] stub (Drawing.hpp:118) = a NO-OP, so the title pops in with no
 * intro. This is the Saturn implementation: a FULL-SCREEN VDP1 sprite of the
 * fill colour composited ON TOP of the title sprites + VDP2 backdrop.
 *
 * MECHANISM (ST-013-R3 VDP1 + jo SGL sprite path):
 *  - A 16x16 SOLID-colour RGB555 sprite (jo_sprite_add COL_32K; MSB=0x8000 set
 *    so every texel is opaque-visible) is allocated ONCE per colour (black +
 *    white cover both decomp callers). jo's append-only allocator (sprites.c:74)
 *    runs these 2 jo_sprite_add calls at most once each (#189-safe).
 *  - Drawn via jo_sprite_draw3D at Z=450 (the SAME Z the title content draws at,
 *    p6_title_blit:908 -> ~1:1 screen scale) with UNIFORM scale 20.0 -> 320x320,
 *    centred at (0,0) -> covers the whole 320x240 frame. Uniform scale takes the
 *    slDispSprite path (sprites.c:447), avoiding the slDispSpriteHV null-angle bug
 *    (sprites.c:444-445). FillScreen is called from TitleSetup's Draw (drawGroup
 *    12 = the LAST drawGroup), so this command is appended AFTER every other title
 *    sprite -> VDP1 painter's order puts it on top regardless of Z.
 *  - OPACITY ramp from the decomp's timer-derived alphas (CLAMP 0..255 each, the
 *    Drawing.cpp:588-590 clamp). avg = (aR+aG+aB)/3:
 *      avg >= 170 -> OPAQUE (CL_Replace, default) = solid black / solid white
 *                    (the fade-in start frames + the flash peak = the gate's
 *                    "near-black early frames" + "bright flash frame").
 *      avg in [1,170) -> HALF-TRANSPARENT (VDP1 CL_Half via
 *                    jo_sprite_enable_half_transparency, SL_DEF.H:193) = the fade
 *                    transition (50% blend toward the revealing content).
 *      sum <= 0 -> draw NOTHING (fully revealed -> the logo shows through).
 *    VDP1 half-transparency is a fixed 50% blend (it cannot do arbitrary per-
 *    channel alpha), so this is a faithful 3-level quantisation of the PC ramp --
 *    opaque -> 50% -> clear -- which is exactly what the intro pixel-gate asserts
 *    (black -> fade -> reveal, with a white flash before the logo).
 *
 * FRONT-END-ONLY (the whole helper is #if defined(P6_FRONTEND_TITLE)); the GHZ
 * flavor's FillScreen stays the p6_stubs.cpp no-op (it has no intro). The two
 * 16x16 sprites cost 2*512 B of VDP1 VRAM, allocated lazily on first fade frame.
 * ========================================================================== */
static int            s_fillSprBlack = -2; /* -2 = not yet attempted */
static int            s_fillSprWhite = -2;
static unsigned short s_fillPx[16 * 16];

static int p6_fill_make(unsigned short rgb555)
{
    jo_img img;
    int i;
    for (i = 0; i < 16 * 16; ++i)
        s_fillPx[i] = rgb555; /* MSB(0x8000) already set by the caller -> opaque */
    img.width  = 16;
    img.height = 16;
    img.data   = s_fillPx;
    return jo_sprite_add(&img); /* COL_32K RGB direct-colour sprite */
}

__attribute__((used)) void p6_fillscreen_saturn(unsigned int color, int aR, int aG, int aB)
{
    int sum, avg, spr;

    if (aR < 0) aR = 0; else if (aR > 255) aR = 255;
    if (aG < 0) aG = 0; else if (aG > 255) aG = 255;
    if (aB < 0) aB = 0; else if (aB > 255) aB = 255;
    sum = aR + aG + aB;
    if (sum <= 0)
        return; /* fully transparent -> the content shows through (decomp early-out) */

    /* Lazy-allocate the two solid sprites (once each). 0x8000 = RGB555 visible bit. */
    if (s_fillSprWhite == -2)
        s_fillSprWhite = p6_fill_make((unsigned short)(0x8000 | 0x7FFF)); /* white */
    if (s_fillSprBlack == -2)
        s_fillSprBlack = p6_fill_make((unsigned short)(0x8000 | 0x0000)); /* black */

    /* Colour select: FadeBlack=0x000000, Flash=0xF0F0F0 -- threshold the red byte. */
    spr = (((color >> 16) & 0xFF) > 0x80) ? s_fillSprWhite : s_fillSprBlack;
    if (spr < 0)
        return; /* alloc failed (VRAM full) -> skip rather than draw garbage */

    avg = sum / 3;
    /* 16x16 sprite * uniform 20.0 = 320x320 -> covers the 320x240 frame, centred. */
    jo_sprite_change_sprite_scale(20.0f);
    if (avg < 170)
        jo_sprite_enable_half_transparency(); /* mid-fade: VDP1 CL_Half 50% blend */
    /* avg >= 170 -> opaque (CL_Replace, the default). */
    jo_sprite_draw3D(spr, 0, 0, 450);
    jo_sprite_disable_half_transparency();
    jo_sprite_restore_sprite_scale();
}
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

#if defined(P6_FRONTEND_TITLE)
static int p6_title_ensure_prealloc(void); /* fwd: eager bucket VRAM reservation */
/* CP5b.7 content-size (#277): the TITLE first-bind init -- mirror the sprite palette
 * then reserve the bucket slots ONCE via the eager prealloc (NOT the per-bind jo_id=-1
 * reset, which would orphan the permanent VRAM reservations and leak jo's append-only
 * allocator). TITLE flavor only; the GHZ/Logos binds keep their verbatim inline reset
 * below (#if'd) so the GHZ p6_vdp1.o stays byte-identical. */
#define P6_VDP1_FIRST_BIND_INIT(pal) do {        \
        p6_pal_mirror(pal);                       \
        s_useclock = 0;                           \
        p6_title_ensure_prealloc();               \
    } while (0)
#else
#define P6_VDP1_FIRST_BIND_INIT(pal) do {        \
        int i;                                    \
        p6_pal_mirror(pal);                       \
        p6_w_vdp1_slots = 0;                      \
        s_useclock = 0;                           \
        for (i = 0; i < P6_VDP1_NSLOTS; ++i) {    \
            s_slots[i].jo_id   = -1;              \
            s_slots[i].sheet   = -1;              \
            s_slots[i].lastUse = 0;               \
        }                                         \
    } while (0)
#endif

/* Bind a RESIDENT engine surface. Returns the sheet handle (or -1). */
int p6_vdp1_sheet_bind(const unsigned char *sheetPixels, int sheetWidth,
                       const unsigned short *pal565)
{
    if (s_sheet_count >= P6_VDP1_NSHEETS)
        return -1;
    if (s_sheet_count == 0)
        P6_VDP1_FIRST_BIND_INIT(pal565);
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
    if (s_sheet_count == 0)
        P6_VDP1_FIRST_BIND_INIT(pal565);
    s_sheets[s_sheet_count].px      = 0;
    s_sheets[s_sheet_count].w       = sheetWidth;
    s_sheets[s_sheet_count].shtSlot = shtSlot;
    return s_sheet_count++;
}

#if defined(P6_FRONTEND_CHAIN)
/* CP5c (Task #270) CRAM-PALETTE FIX -- MEASURED root cause of the wrong Title
 * colors after the Logos->Title chain transition (savestate _title_chain.mcs vs
 * the direct-boot golden _title_sonic.mcs): the engine's active Title palette in
 * fullPalette[0] (WorkRAM-L 0x002FAC00) was BYTE-IDENTICAL in both states, but
 * VDP2 CRAM bank 1 (the SPRITE palette VDP1 reads, 0x05F00200) differed in 144 of
 * 256 entries -- the chain's bank 1 held the stale LOGOS palette. CAUSE:
 * p6_pal_mirror (the only writer of CRAM bank 1) runs ONLY when s_sheet_count==0
 * (the very first bind of the WHOLE run). On the direct Title boot, Title is the
 * first scene -> its first bind has s_sheet_count==0 -> the Title palette mirrors
 * correctly. In the CHAIN, the LOGOS scene binds FIRST (s_sheet_count 0->1, mirrors
 * Logos's palette), so when the Logos->Title fire re-binds Title's surfaces
 * s_sheet_count is already >=1 -> p6_pal_mirror is NEVER re-run -> bank 1 keeps the
 * Logos palette -> every Title sprite hue-shifts (the SAME #250-class "loaded once"
 * guard the VDP1-handle-table reset already fixed for GEOMETRY -- this is its CRAM
 * twin, in a SEPARATE static this TU owns).
 *
 * FIX: the front-end CHAIN calls this on the Logos->Title fire (p6_io_main.cpp,
 * alongside the p6_vdp1HandleBySurface[] reset it already does). Resetting
 * s_sheet_count to 0 makes the Title's FIRST re-bind satisfy s_sheet_count==0 ->
 * p6_pal_mirror re-runs with Title's fullPalette[0] -> CRAM bank 1 carries the
 * correct Title sprite palette. The slot rect-cache + sheet table are cleared too
 * (the surfaces re-bind fresh from index 0, matching the handle-table reset). The
 * GHZ same-folder reload never reaches this (front-end CHAIN only); the default
 * GHZ build does not compile it (byte-identical). */
void p6_vdp1_frontend_pal_reset(void)
{
    s_sheet_count   = 0;
    s_useclock      = 0;
#if defined(P6_FRONTEND_TITLE)
    /* CP5b.7 content-size (#277): the bucket slots are PERMANENTLY VRAM-reserved by the
     * eager prealloc -- NEVER reset their jo_id (re-allocating would leak jo's append-only
     * VDP1 allocator on every chain transition). Only clear the rect KEYS so each surface
     * re-stages content fresh after the Logos->Title re-bind; keep s_buckets_prealloc so
     * the allocator is not re-run. (CHAIN implies TITLE.) */
    {
        P6Vdp1Slot *bk[P6_NB];
        int bn, j, n;
        bk[0] = s_buck0; bk[1] = s_buck1; bk[2] = s_buck2; bk[3] = s_slots;
        for (bn = 0; bn < P6_NB; ++bn) {
            n = (bn == 3) ? P6_VDP1_NSLOTS : 6;
            for (j = 0; j < n; ++j) {
                bk[bn][j].sheet   = -1;
                bk[bn][j].sx = bk[bn][j].sy = bk[bn][j].w = bk[bn][j].h = -1;
                bk[bn][j].lastUse = 0;
            }
        }
        /* keep p6_w_vdp1_slots / s_buck*n at the prealloc'd "full" marks */
    }
#else
    {
        int i;
        p6_w_vdp1_slots = 0;
        for (i = 0; i < P6_VDP1_NSLOTS; ++i) {
            s_slots[i].jo_id   = -1;
            s_slots[i].sheet   = -1;
            s_slots[i].lastUse = 0;
        }
    }
#endif
}
#endif

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
/* CP5b.7: the slot lookup/stage/upload, PARAMETERIZED by the pool + its box size
 * so the same proven LRU logic serves BOTH the large (248x160) and the small
 * (64x64) title pools. Returns the slot INDEX into `slots` (or -1). Each pool is
 * uniform-box, so jo_sprite_replace's same-size in-place VRAM reuse is preserved
 * (no append-only leak). Body is the verbatim pre-CP5b.7 p6_slot_for with
 * s_slots->slots, p6_w_vdp1_slots->*coldn, P6_VDP1_NSLOTS->n_max, P6_SPR_MAXW/H->
 * boxw/boxh. */
static int p6_pool_for(P6Vdp1Slot *slots, int n_max, int *coldn,
                       int boxw, int boxh, int sheet, int sx, int sy, int w, int h)
{
    int i, x, y, victim;
    const unsigned char *srcPx;
    int srcStride;
    jo_img_8bits img;

#if defined(P6_FRONTEND_LOGOS)
    p6_w_vdp1_lastwh = (w << 16) | (h & 0xFFFF);
#endif
    for (i = 0; i < *coldn; ++i) {
        if (slots[i].sheet == sheet &&
            slots[i].sx == sx && slots[i].sy == sy &&
            slots[i].w == w && slots[i].h == h) {
            slots[i].lastUse = ++s_useclock; /* LRU touch on hit */
            return i;
        }
    }
    /* A fixed boxw x boxh slot cannot hold an oversize frame. */
    if (w > boxw || h > boxh) {
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

    /* Stage into the FIXED boxw x boxh box: content top-left, the rest transparent
     * (palette index 0 -- VDP1 sprite transparent-pixel processing skips it).
     * jo_sprite_replace re-DMAs exactly boxw*boxh bytes, so the staged box
     * dimensions MUST equal the pre-allocated slot's (boxw x boxh -- guaranteed:
     * same pool = same box). s_stage is 248x160 = big enough for either box. */
    for (y = 0; y < boxh; ++y) {
        unsigned char *dst = s_stage + y * boxw;
        if (y < h) {
            const unsigned char *src = srcPx + y * srcStride;
            for (x = 0; x < w; ++x) dst[x] = src[x];
            for (; x < boxw; ++x) dst[x] = 0;
        }
        else {
            for (x = 0; x < boxw; ++x) dst[x] = 0;
        }
    }
    img.width  = boxw;
    img.height = boxh;
    img.data   = s_stage;

    if (*coldn < n_max) {
        /* Cold-fill: allocate a fresh fixed-size jo sprite. */
        int id = jo_sprite_add_8bits_image(&img);
        if (id < 0) {
            ++p6_w_vdp1_joaddfail; /* W14: the silent no-drop exit */
#if defined(P6_FRONTEND_LOGOS)
            p6_w_vdp1_dropreason = 3;
#endif
            return -1;
        }
        victim = (*coldn)++;
        slots[victim].jo_id = id;
    }
    else {
        /* Cache full: evict the least-recently-used slot, reuse its VRAM. */
        int oldest = slots[0].lastUse;
        victim = 0;
        for (i = 1; i < n_max; ++i) {
            if (slots[i].lastUse < oldest) {
                oldest = slots[i].lastUse;
                victim = i;
            }
        }
        if (slots[victim].jo_id < 0) { /* defensive: never cold-filled */
            ++p6_w_vdp1_drops;
            return -1;
        }
        jo_sprite_replace(&img, slots[victim].jo_id);
        ++p6_w_vdp1_evicts;
    }

    slots[victim].sheet   = sheet;
    slots[victim].sx      = sx;
    slots[victim].sy      = sy;
    slots[victim].w       = w;
    slots[victim].h       = h;
    slots[victim].lastUse = ++s_useclock;
    p6_w_vdp1_lastkey     = (sx << 20) | (sy << 12) | (w << 6) | h;
    return victim;
}

#if defined(P6_FRONTEND_TITLE)
/* CP5b.7 content-size step (Task #277): the TITLE content-tight pool. Every bucket
 * slot is pre-reserved at box size (p6_title_ensure_prealloc), so this never calls
 * jo_sprite_add -- a MISS LRU-evicts a victim slot and RESTAGES it at CONTENT size via
 * p6_title_restage_content (DMA content-packed + set the slot's CMDSIZE to content).
 * VDP1 then rasterises ONLY the sprite's content (w mult-8, h) -- the 39%-padding the
 * box-draw paid is gone. Returns the slot index into b->slots (or -1); sets *out_pw to
 * the drawn mult-8 width (the blit centres on (pw,h), not the box). The HIT path is
 * unchanged (the slot already carries its content TEXDEF from its last restage). */
static int p6_title_ensure_prealloc(void);
static int p6_title_pool_for(P6Bucket *b, int sheet, int sx, int sy, int w, int h,
                             int *out_pw, int *out_ph)
{
    int i, victim;
    const unsigned char *srcPx;
    int srcStride, pw;

    if (!p6_title_ensure_prealloc()) { ++p6_w_vdp1_drops; return -1; }

    for (i = 0; i < b->n; ++i) {            /* HIT: same rect already content-staged */
        if (b->slots[i].sheet == sheet &&
            b->slots[i].sx == sx && b->slots[i].sy == sy &&
            b->slots[i].w == w && b->slots[i].h == h) {
            b->slots[i].lastUse = ++s_useclock;
            *out_pw = (w + 7) & ~7; *out_ph = h;
            return i;
        }
    }
    if (w > b->bw || h > b->bh) { ++p6_w_vdp1_drops; return -1; } /* oversize for bucket */

    if (s_sheets[sheet].px) {
        srcPx     = s_sheets[sheet].px + sy * s_sheets[sheet].w + sx;
        srcStride = s_sheets[sheet].w;
    }
    else {
        if (!s_fetchFn
            || !s_fetchFn(s_sheets[sheet].shtSlot, sx, sy, w, h, s_fetch)) {
            ++p6_w_vdp1_drops;
            return -1;
        }
        srcPx     = s_fetch;
        srcStride = w;
    }

    /* LRU victim among this bucket's pre-allocated slots (all jo_id >= 0). */
    {
        int oldest = b->slots[0].lastUse;
        victim = 0;
        for (i = 1; i < b->n; ++i)
            if (b->slots[i].lastUse < oldest) { oldest = b->slots[i].lastUse; victim = i; }
    }
    if (b->slots[victim].jo_id < 0) { ++p6_w_vdp1_drops; return -1; } /* prealloc failed */
    if (b->slots[victim].sheet >= 0) ++p6_w_vdp1_evicts;             /* reuse of a live slot */

    pw = p6_title_restage_content(b->slots[victim].jo_id, srcPx, srcStride, w, h);

    b->slots[victim].sheet   = sheet;
    b->slots[victim].sx      = sx;
    b->slots[victim].sy      = sy;
    b->slots[victim].w       = w;
    b->slots[victim].h       = h;
    b->slots[victim].lastUse = ++s_useclock;
    p6_w_vdp1_lastkey        = (sx << 20) | (sy << 12) | (w << 6) | h;
    *out_pw = pw; *out_ph = h;
    return victim;
}

/* Reserve every bucket slot's VRAM (box footprint) exactly once, before any draw.
 * Returns 1 on success. Builds s_buckets[] (binding each P6Bucket to its slot array +
 * box dims) and content-stages NOTHING yet (the slots start empty: sheet=-1). After
 * this runs, jo_sprite_add is never called again for the buckets (see the eager-
 * prealloc rationale above), so per-(re)stage __jo_sprite_def mutation is safe. */
static int p6_title_ensure_prealloc(void)
{
    int bi, si, id;
    P6Vdp1Slot *arr[P6_NB];
    int cnt[P6_NB];

    if (s_buckets_prealloc) return 1;
    arr[0] = s_buck0; arr[1] = s_buck1; arr[2] = s_buck2; arr[3] = s_slots;
    cnt[0] = 6; cnt[1] = 6; cnt[2] = 6; cnt[3] = P6_VDP1_NSLOTS; /* P6_VDP1_NSLOTS==4 */
    for (bi = 0; bi < P6_NB; ++bi) {
        s_buckets[bi].slots = arr[bi];
        s_buckets[bi].n     = cnt[bi];
        s_buckets[bi].bw    = P6_BUCK[bi].bw;
        s_buckets[bi].bh    = P6_BUCK[bi].bh;
        for (si = 0; si < cnt[bi]; ++si) {
            id = p6_title_alloc_box(P6_BUCK[bi].bw, P6_BUCK[bi].bh);
            if (id < 0) { ++p6_w_vdp1_joaddfail; return 0; }
            arr[bi][si].jo_id   = id;
            arr[bi][si].sheet   = -1;     /* empty: no rect staged yet */
            arr[bi][si].sx = arr[bi][si].sy = arr[bi][si].w = arr[bi][si].h = -1;
            arr[bi][si].lastUse = 0;
        }
    }
    /* p6_w_vdp1_slots tracks bucket-3 occupancy in the witnesses; the slots are now all
     * reserved, so mark it full (the LRU victim path -- not cold-fill -- serves it). */
    s_buck0n = 6; s_buck1n = 6; s_buck2n = 6; p6_w_vdp1_slots = P6_VDP1_NSLOTS;
    s_buckets_prealloc = 1;
    return 1;
}
#endif

/* CP5b.7 ROUTER: pick the SMALLEST box pool that holds (w,h). Returns the jo
 * sprite ID (NOT a slot index) + sets s_last_box_w/h for the blit's box-center
 * placement. TITLE only -- the GHZ/Logos build has a single pool (byte-identical:
 * the small-pool branch is #if'd out and the large call mirrors the old code). */
static int s_last_box_w = P6_SPR_MAXW, s_last_box_h = P6_SPR_MAXH;
static int p6_slot_for(int sheet, int sx, int sy, int w, int h)
{
    int s;
#if defined(P6_FRONTEND_TITLE)
    /* Phase 2 + content-size (#277): route to the smallest bucket, then DRAW at content
     * size. s_last_box_w/h become the drawn (mult-8 w, h) so the blit centres the
     * content -- NOT the box -- and the fill witnesses sum the real CMDSIZE area. */
    int bk = p6_bucket_for(w, h);
    int pw = (w + 7) & ~7, ph = h;
    if (bk < 0) { ++p6_w_vdp1_drops; return -1; } /* oversize (w>248 or h>160) */
    s = p6_title_pool_for(&s_buckets[bk], sheet, sx, sy, w, h, &pw, &ph);
    if (s < 0) return -1;
    s_last_box_w = pw; s_last_box_h = ph;
    return s_buckets[bk].slots[s].jo_id;
#else
    s = p6_pool_for(s_slots, P6_VDP1_NSLOTS, &p6_w_vdp1_slots,
                    P6_SPR_MAXW, P6_SPR_MAXH, sheet, sx, sy, w, h);
    if (s < 0) return -1;
    s_last_box_w = P6_SPR_MAXW;
    s_last_box_h = P6_SPR_MAXH;
    return s_slots[s].jo_id;
#endif
}

#if defined(P6_FRONTEND_TITLE)
/* CP5b.4 (Task #272): VDP1 half-transparency for the TitleBG INK_BLEND (Mountain2)
 * + INK_ADD (Reflection/WaterSparkle, alpha 0x80) sprites. jo's effect bits OR into
 * cmd->pmod (sprites.c:363: pmod = 0x0080 | effect); effect 0x3 == SGL CL_Trans
 * translucent color-calc (SL_DEF.H:194; ST-013-R3 sec 5.5.4 PMOD bits 2:0).
 * HARDWARE TRUTH (REPORTED): VDP1 PMOD half-transparency blends with what is
 * already in the VDP1 FRAMEBUFFER (other sprites), NOT the VDP2 backdrop -- so a
 * mountain's translucency over the VDP2 island is NOT reproduced by PMOD alone
 * (that needs VDP2 color-calc CCRTL on the sprite layer, ST-058-R2 sec 12). Set
 * before the blit, clear after (sticky jo attribute -- same pattern as the flips).
 * Title flavor only (GHZ p6_vdp1.o byte-identical). */
__attribute__((used)) int p6_w_ink_half_blits = 0;
void p6_vdp1_set_ink(int half)
{
    if (half) { jo_sprite_enable_half_transparency(); ++p6_w_ink_half_blits; }
    else      { jo_sprite_disable_half_transparency(); }
}
#endif

#if defined(P6_FRONTEND_TITLE)
/* EDGE-GLITCH FIX (this session): the TitleBG parallax band (MountainTop1/2,
 * Reflection, WaterSparkle -- Title/BG.gif, 176-192 px wide) is scrolled +
 * horizontally wrapped by the verbatim decomp TitleBG_Update
 * (position.x -= 0x10000; if (position.x < -0x800000) position.x += 0x3000000)
 * for a band that spans a WIDER PC screen. On Saturn's 320 px screen those wide
 * sprites land (MEASURED via the per-blit ring on a settled-title savestate:
 * x=283 content [283,475], and x=-77 content [-77,115]) so their drawn box
 * extends far past both screen edges. The Saturn VDP1 path stages each sprite
 * into a FIXED P6_SPR_MAXW(248)-wide box (content at the box top-left) and
 * slDispSprite-places the box CENTER at framebuffer x + 124. For an off-screen
 * sprite the box CROSSES the 512 px VDP1 framebuffer LINE STRIDE (e.g. x=283 ->
 * box [283,531]; 531 > 512), and the part past 512 WRAPS to the next line's
 * left columns -- the "fragment at the opposite edge". The PC engine never sees
 * this because DrawSpriteFlipped (Drawing.cpp:2882-2905) clips the sprite to
 * currentScreen->clipBound_* PER PIXEL; the Saturn VDP1 has no such per-pixel
 * clip on a normal-sprite command. MIRROR that clip here: clip the source rect
 * to the on-screen span [0, JO_TV_WIDTH) so only the visible columns are staged
 * + drawn and the box can never cross the framebuffer boundary. Title flavor
 * only (default GHZ p6_vdp1.o stays byte-identical -- the GHZ object set draws
 * within-screen and never triggers this).
 *
 * The VDP1 stages every sprite into a FIXED P6_SPR_MAXW-wide box (content at the
 * box top-left) and slDispSprite-places the box CENTER at framebuffer x + 124, so
 * the box spans framebuffer [x, x + P6_SPR_MAXW]. When a sprite is positioned so
 * its box crosses the 320-mode VDP1 framebuffer LINE STRIDE (512 px) -- box-left
 * < 0 (a sprite scrolled off the LEFT) or box-right > 512 (off the RIGHT) -- the
 * crossing columns WRAP to the opposite edge as a visible "duplicate fragment".
 * MEASURED root cause of the title edge glitch: the verbatim decomp TitleBG_Update
 * scrolls + horizontally wraps the TitleBG parallax band (MountainTop1/2, Reflection,
 * WaterSparkle) for a WIDER PC screen; on Saturn's 320 px screen those wide sprites
 * land off both edges and their box crosses the stride.
 *
 * The engine clips partly-off-screen sprites per-pixel (DrawSpriteFlipped clipBound),
 * but the Saturn VDP1 normal-sprite command has no per-pixel clip, and re-staging a
 * clipped sub-rect would thrash the 10-slot LRU cache (the per-scroll-position rects
 * explode the key space -> evictions -> stale-slot garbage). So instead CULL any
 * sprite whose fixed box would cross the stride. The culled content is only the few
 * pixels of the DISTANT-mountain band right at the screen edge -- imperceptible vs a
 * wrapped duplicate, and the cull touches neither the source rect nor the cache.
 * Returns 1 to draw (box fully in [0,512)), 0 to cull. Title flavor only (GHZ
 * p6_vdp1.o is byte-identical -- its object set draws within-screen). */
#define P6_VDP1_FB_STRIDE 512  /* 320-mode VDP1 framebuffer line width (px) */
static int p6_box_in_stride(int x, int flipX, int w, int h)
{
    int box_left;
    /* Phase 2: the box width is the ROUTED content-size bucket -- MUST match the
     * p6_slot_for routing exactly, or a sprite near the right edge is wrongly
     * culled/passed (box-right = x+boxw vs the 512 px stride). p6_bucket_for(-1)
     * (oversize) -> the big box; p6_slot_for drops it anyway. */
    int b    = p6_bucket_for(w, h);
    int boxw = (b < 0) ? P6_SPR_MAXW : P6_BUCK[b].bw;
    /* Box-left in the framebuffer for the content-at-box-left staging:
     *   FLIP_NONE: box center FB = x + boxw/2  -> box-left = x.
     *   FLIP_X:    box center FB = x + w - boxw/2 + JO_TV_WIDTH_2 ... reduces
     *              to box-left = x + w - boxw. */
    box_left = flipX ? (x + w - boxw) : x;
    return (box_left >= 0 && box_left + boxw <= P6_VDP1_FB_STRIDE);
}
#endif

/* Draw a sheet rect with its TOP-LEFT at engine screen px (x,y) -- the
 * coordinate DrawSpriteFlipped receives (Drawing.cpp:2785: pos + pivot).
 * jo_sprite_draw3D positions the sprite CENTER in screen-centered coords;
 * the slot is a fixed P6_SPR_MAXW x P6_SPR_MAXH box with content in the
 * top-left corner, so centering on the box (offset 32) keeps the content at
 * engine [x, x+w) x [y, y+h). */
void p6_vdp1_blit(int sheet, int x, int y, int w, int h, int sx, int sy)
{
    int jid;

    if (sheet < 0 || sheet >= s_sheet_count) {
        ++p6_w_vdp1_handle_drops; /* W18: unbound-surface silent drop */
        p6_w_vdp1_lastdrop_h = sheet;
        return;
    }
#if defined(P6_FRONTEND_TITLE)
    /* EDGE-GLITCH FIX: cull a sprite whose ROUTED box would cross the 512 px VDP1
     * framebuffer line stride (the off-screen wrap). See p6_box_in_stride. */
    if (!p6_box_in_stride(x, 0, w, h))
        return;
#endif
    /* CP5b.7: p6_slot_for routes to the small/large pool by (w,h) and returns the
     * jo sprite id + sets s_last_box_w/h (the chosen box). */
    jid = p6_slot_for(sheet, sx, sy, w, h);
    if (jid < 0)
        return;

    ++p6_w_vdp1_landed; /* W18: a blit that reached a valid VDP1 slot */
    /* STEP B: per-frame VDP1 workload (ROUTED box-as-drawn vs content-ideal). */
    ++p6_w_vdp1_cmds; p6_w_vdp1_boxpx += s_last_box_w * s_last_box_h;
    p6_w_vdp1_contentpx += w * h;
    if (w > p6_w_vdp1_maxw) p6_w_vdp1_maxw = w;
    if (h > p6_w_vdp1_maxh) p6_w_vdp1_maxh = h;
    jo_sprite_set_palette(1);
    /* Task #241 + CP5b.7: the slot is a fixed s_last_box_w x s_last_box_h box with
     * content in the top-left corner; the box CENTER sits at content-top-left +
     * box/2, so placing the center there lands the content at engine top-left (x,y). */
    jo_sprite_draw3D(jid,
                     x + s_last_box_w / 2 - JO_TV_WIDTH_2,
                     y + s_last_box_h / 2 - JO_TV_HEIGHT_2, 450);
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
    int jid;

    if (sheet < 0 || sheet >= s_sheet_count) {
        ++p6_w_vdp1_handle_drops; /* W18: unbound-surface silent drop */
        p6_w_vdp1_lastdrop_h = sheet;
        return;
    }
#if defined(P6_FRONTEND_TITLE)
    /* EDGE-GLITCH FIX (this is the REAL Saturn draw path -- p6_draw_flipped always
     * calls THIS, never p6_vdp1_blit). CULL a sprite whose ROUTED box would cross
     * the 512 px VDP1 framebuffer line stride: the verbatim decomp TitleBG_Update
     * scrolls + wraps the TitleBG parallax band for a wider PC screen, so on
     * Saturn's 320 px screen those wide sprites land off both edges (MEASURED via
     * the per-blit ring: x=-67 left, x=293 right) and their box crosses the stride
     * -> the crossing columns WRAP to the opposite edge as the visible fragment.
     * Culling the box-crossing sprite drops only the distant-mountain band's few
     * edge pixels (imperceptible) and -- unlike re-staging a clipped sub-rect --
     * touches neither the source rect nor the LRU cache (which a per-scroll-position
     * rect would thrash into stale-slot garbage). See p6_box_in_stride. The centred
     * FG sprites (EMBLEM/RIBBON/Sonic/logo, MEASURED box within [0,512)) are never
     * culled. */
    if (!p6_box_in_stride(x, flipX, w, h))
        return;
#endif
    /* CP5b.7: routed jo id + s_last_box_w/h (small or large pool). */
    jid = p6_slot_for(sheet, sx, sy, w, h);
    if (jid < 0)
        return;

    ++p6_w_vdp1_landed; /* W18: a blit that reached a valid VDP1 slot */
    ++p6_w_vdp1_cmds; p6_w_vdp1_boxpx += s_last_box_w * s_last_box_h;
    p6_w_vdp1_contentpx += w * h;
    if (w > p6_w_vdp1_maxw) p6_w_vdp1_maxw = w;
    if (h > p6_w_vdp1_maxh) p6_w_vdp1_maxh = h;
    jo_sprite_set_palette(1);
    if (flipX)
        jo_sprite_enable_horizontal_flip();
    if (flipY)
        jo_sprite_enable_vertical_flip();
    jo_sprite_draw3D(jid,
                     (flipX ? x + w - s_last_box_w / 2 : x + s_last_box_w / 2) - JO_TV_WIDTH_2,
                     (flipY ? y + h - s_last_box_h / 2 : y + s_last_box_h / 2) - JO_TV_HEIGHT_2, 450);
    if (flipX)
        jo_sprite_disable_horizontal_flip();
    if (flipY)
        jo_sprite_disable_vertical_flip();
}
