/* ============================================================================
 * p6_vdp2.c -- P6.5b1 (Task #208): present the ENGINE-decoded Title "Island"
 * tile layer through VDP2 NBG1 cell mode. First engine-rendered pixels.
 *
 * Inputs are the engine's OWN data, produced by the unmodified chain proven
 * in P6.3/P6.4/P6.5a: tilesetPixels (LZW-decoded from the original
 * 16x16Tiles.gif inside Data.rsdk), tileLayers[3].layout (inflated from the
 * original Scene1.bin), fullPalette[0] (GIF palette through the engine's
 * rgb32To16 tables). Nothing here re-reads assets -- this is purely the
 * Saturn render backend for data already in engine memory.
 *
 * Register contracts (ST-058-R2, pinned 2026-06-10; mirrored by
 * tools/_portspike/qa_p6_vdp2.py which byte-checks VRAM/CRAM from a savestate
 * and SSIM-checks a frame capture against an offline software render):
 *  - 2-WORD pattern name data (slPageNbg1 type PNB_2WORD, SL_DEF.H:533).
 *    Charno unit is ALWAYS 0x20 bytes (ST-058 p.73): an 8bpp 16x16 tile
 *    (4 cells x 64 B = 256 B) spans 8 units, so charno = tile * 8 with cells
 *    based at VRAM A0 offset 0. 1-word/10-bit PND cannot address 1024 such
 *    tiles -- that is WHY 2-word is required. High word: bit15 V-flip,
 *    bit14 H-flip (= PND bits 31/30), palette bits 6-0 = 0 (CRAM bank 0).
 *  - 2x2-cell character order TL,TR,BL,BR; each cell is 8 rows of 8 bytes.
 *  - Map: 4 pages x 32x32 x 4 B at B0; page = (ty>=32)*2 + (tx>=32);
 *    in-page index = (ty&31)*32 + (tx&31). One PL_SIZE_2x2 plane covers the
 *    whole 1024x1024 px layer; all 4 map slots point at it (wrap).
 *  - CRAM bank 0: engine RGB565 -> Saturn 0x8000 | B5<<10 | G5<<5 | R5.
 *  - VDP2 VRAM/CRAM are written in 16/32-bit units only (no byte stores).
 *  - RSDK layout entry: idx = t & 0x3FF, fx = bit10, fy = bit11, empty =
 *    0xFFFF (src/rsdk/collision.c:327). Empty entries map to a BLANK char:
 *    the smallest tile index layer 3 never references, whose cell block is
 *    zeroed (tile 0 is NOT blank in this tileset -- measured).
 * ========================================================================== */
#include <SGL.H>

#define P6_VDP2_CEL  0x25E00000u /* A0+A1: 1024 tiles x 256 B = 256 KB        */
#define P6_VDP2_MAP  0x25E40000u /* B0: 4 pages x 32x32 x 4 B = 16 KB         */
#define P6_VDP2_CRAM 0x25F00000u /* color RAM bank 0                          */
#define P6_VDP2_BAK  0x25E7FFFEu /* back-color table: last word of B1 (unused) */
#define P6_SCROLL_X  320         /* densest 20x14 window of the Island layer  */
#define P6_SCROLL_Y  384         /*   (224/280 tiles non-empty, measured)     */

/* P6.7 W11b SPLIT (Task #226): tilesetPixels is now a LOAD-PHASE TRANSIENT
 * aliasing the WRAM-L entityList window (neither bank can hold a resident
 * 256 KB tileset at the GHZ entity scale -- measured, map v7 notes). The
 * cell+CRAM upload therefore runs BETWEEN LoadSceneFolder (GIF decode) and
 * LoadSceneAssets (entity placement clobbers the window); the layout-driven
 * half (blank-char pick + PND map + display) runs after LoadSceneAssets.
 * VDP2 VRAM bytes are IDENTICAL to the old single-shot present: the blank
 * char's cells are zeroed at present time (that tile is unreferenced by the
 * layout, so zeroing it after the verbatim upload is order-independent). */
void p6_vdp2_upload_cells(const unsigned char *tilesetPx)
{
    volatile Uint16 *cel = (volatile Uint16 *)P6_VDP2_CEL;
    int t, c, r;

    /* 1) Cells: RSDK 16x16 linear tiles -> 2x2-cell chars (TL,TR,BL,BR),
     *    16-bit stores, two pixels per word (high byte = left pixel). */
    for (t = 0; t < 1024; ++t) {
        const unsigned char *src = tilesetPx + t * 256;
        volatile Uint16 *dst     = cel + t * 128; /* 256 B = 128 words */
        for (c = 0; c < 4; ++c) {
            int cy0 = (c >> 1) * 8, cx0 = (c & 1) * 8;
            for (r = 0; r < 8; ++r) {
                const unsigned char *row = src + (cy0 + r) * 16 + cx0;
                *dst++ = (Uint16)((row[0] << 8) | row[1]);
                *dst++ = (Uint16)((row[2] << 8) | row[3]);
                *dst++ = (Uint16)((row[4] << 8) | row[5]);
                *dst++ = (Uint16)((row[6] << 8) | row[7]);
            }
        }
    }

    /* (3: CRAM moved into present_layout -- it depends only on fullPalette,
     * whose ACTIVE rows finalize in LoadSceneAssets:308-319, AFTER this
     * upload runs. MEASURED: uploading here grabbed the pre-merge palette
     * -- CRAM witnesses 0x8202/0x8390 vs the canonical 0xFA46/0xF3B4.) */
}

void p6_vdp2_present_layout(const unsigned short *layout, int wshift,
                            const unsigned short *pal565)
{
    volatile Uint16 *cel  = (volatile Uint16 *)P6_VDP2_CEL;
    volatile Uint16 *map  = (volatile Uint16 *)P6_VDP2_MAP;
    volatile Uint16 *cram = (volatile Uint16 *)P6_VDP2_CRAM;
    int t, c, x, y;

    /* Blank char = smallest tile index the layer never references (same rule
     * as the gate's offline model, so both sides pick the SAME slot). Its
     * cells are zeroed IN VDP2 VRAM -- safe post-upload, see split note. */
    static unsigned char used[1024];
    for (t = 0; t < 1024; ++t) used[t] = 0;
    for (t = 0; t < (1 << wshift) * 64; ++t) {
        unsigned short e = layout[t];
        if (e != 0xFFFF)
            used[e & 0x3FF] = 1;
    }
    int blank = 0;
    while (blank < 1024 && used[blank]) ++blank;
    {
        volatile Uint16 *dst = cel + blank * 128;
        for (c = 0; c < 128; ++c) dst[c] = 0;
    }

    /* 2) Map: 2-word PNDs. Big-endian bus: high word (flips) at the lower
     *    address, low word (charno = tile*8) at +2. */
    for (y = 0; y < 64; ++y) {
        for (x = 0; x < 64; ++x) {
            unsigned short e = layout[(y << wshift) + x];
            unsigned long pnd;
            if (e == 0xFFFF)
                pnd = (unsigned long)blank * 8u;
            else
                pnd = ((unsigned long)(e & 0x800) << 20)   /* fy bit11 -> 31 */
                    | ((unsigned long)(e & 0x400) << 20)   /* fx bit10 -> 30 */
                    | ((unsigned long)(e & 0x3FF) * 8u);
            int page = ((y >> 5) << 1) + (x >> 5);
            volatile Uint16 *p = map + page * 2048 + (((y & 31) << 5) + (x & 31)) * 2;
            p[0] = (Uint16)(pnd >> 16);
            p[1] = (Uint16)(pnd & 0xFFFF);
        }
    }

    /* 3) CRAM bank 0: engine RGB565 -> Saturn BGR555 (MSB set, jo-consistent).
     *    Runs post-LoadSceneAssets so the active palette rows are final. */
    for (c = 0; c < 256; ++c) {
        unsigned short v = pal565[c];
        unsigned short r5 = (v >> 11) & 0x1F;
        unsigned short g5 = ((v >> 5) & 0x3F) >> 1;
        unsigned short b5 = v & 0x1F;
        cram[c] = (Uint16)(0x8000 | (b5 << 10) | (g5 << 5) | r5);
    }

    /* 4) NBG1 configuration + display (SGL owns the cycle registers via
     *    slScrAutoDisp, same trust as the proven jo/scene_ghz NBG paths). */
    slCharNbg1(COL_TYPE_256, CHAR_SIZE_2x2);
    slPageNbg1((void *)P6_VDP2_CEL, 0, PNB_2WORD);
    slPlaneNbg1(PL_SIZE_2x2);
    slMapNbg1((void *)P6_VDP2_MAP, (void *)P6_VDP2_MAP,
              (void *)P6_VDP2_MAP, (void *)P6_VDP2_MAP);
    slScrPosNbg1(toFIXED(P6_SCROLL_X), toFIXED(P6_SCROLL_Y));
    slPriorityNbg1(1); /* FG plane BELOW the VDP1 sprites (HUD/Sonic/Tails at pri 7)
                        * so the characters render IN FRONT of the foreground plane
                        * (plants/totems), not behind it. No other VDP2 layer competes
                        * (BG parallax not yet drawn). Mania's player-between-FG-groups
                        * layering needs a behind/front FG split -- a later refinement. */
    slBack1ColSet((void *)P6_VDP2_BAK, 0x8000); /* black backdrop = model's */
    slScrAutoDisp(NBG1ON | SPRON);
}

/* Boot/load cover: blank ALL VDP2 scroll + sprite display so the multi-second
 * synchronous scene load shows a clean solid back-color instead of NBG1
 * displaying half-written VRAM (the red/green static the user reported between
 * boot and GHZ). Called from p6_load_phase_enter at the start of every load;
 * the first GHZ present (p6_vdp2_present_ghz_camera) re-arms NBG1ON|SPRON once
 * the scene's VRAM is fully written. */
void p6_vdp2_blank(void)
{
    slScrAutoDisp(0);
}

/* CP4c BLUE-SCREEN FIX (this session): arm the VDP1 SPRITE LAYER for a non-GHZ UI
 * scene (the Logos splash). MEASURED ROOT CAUSE of the uniform-blue splash: the
 * lean boot calls p6_vdp2_blank() (slScrAutoDisp(0)) during load, which disables
 * ALL VDP2 layers INCLUDING the VDP1 sprite layer (SPRON). The GHZ frame re-arms
 * NBG1ON|SPRON via p6_vdp2_present_ghz_camera, but the front-end frame
 * (p6_frontend_frame) does NO present (a UI scene has no FG plane), so SPRON
 * stayed OFF -- the UIPicture VDP1 sprites were drawn to the framebuffer
 * (MEASURED p6_w_vdp1_landed == draw_calls) but VDP2 never composited them
 * (MEASURED VDP2 BGON=0x0000, the sprite layer dark). This enables ONLY SPRON
 * (no NBG -- the UI scene draws no VDP2 cells) + a black backdrop, mirroring the
 * GHZ present's sprite-layer arm (p6_vdp2.c:471 slScrAutoDisp(NBG1ON|SPRON)).
 * Idempotent; the front-end frame calls it once the scene is armed. SGL owns the
 * sprite priority via the per-sprite slDispSprite Z (jo_sprite_draw3D), same as
 * the proven GHZ HUD/character path -- no PRISA write needed here.
 * Flag-gated (CP4c _end-leak FIX): the ONLY caller is p6_frontend_frame, itself
 * behind #if defined(P6_FRONTEND_LOGOS) -- so the DEFAULT (GHZ) build never
 * references it. Compiling it out (rather than leaning on --gc-sections at the
 * ld -r pack step) keeps the default p6_vdp2.o provably byte-identical. p6_vdp2.c
 * is compiled by build_p6scene_objs.sh (NOT jo-make), which threads
 * -DP6_FRONTEND_LOGOS into THIS TU's compile only in the front-end build, so the
 * definition is present for the front-end caller and absent in the default. */
#if defined(P6_FRONTEND_LOGOS)
void p6_vdp2_arm_sprites_only(void)
{
    slBack1ColSet((void *)P6_VDP2_BAK, 0x8000); /* black backdrop (model's) */
    slScrAutoDisp(SPRON);
}
#endif

/* ============================================================================
 * P6.7 W16 (Task #228): present the ENGINE-loaded GHZ1 FOREGROUND on NBG1,
 * anchored to the LIVE camera. Same VRAM/PND geometry as the Title present
 * above (cells A0+A1 charno=tile*8, 4-page 64x64-tile PL_SIZE_2x2 plane at
 * B0, 2-word PND, CRAM bank 0), but the layout source is the W11a/b
 * camera-local SLIDING-WINDOW accessor (SaturnLayout_GetTile -- GHZ layouts
 * are NOT RAM-resident; SaturnLayout.cpp) and the scroll registers come
 * from screens[0].position via slScrPosNbg1 (SL_DEF.H:1025; SGL transfers
 * the FIXED value to SCXIN1/SCYIN1 = VDP2 0x180080/0x180084, ST-058-R2
 * p.123 -- VDP2_Manual.txt:5406/5422).
 *
 * CONTRACT (W16): the camera rests bounds-clamped at (0, 780) after the 60
 * GHZ ticks (W15 measured, p6_w_scr_x/y) -- the whole 320x224 view lies
 * inside the plane's first 1024x1024 px, so the 64x64 PND rect is the
 * IDENTITY rect (plane row y = layer row y, rows 0..63). The moving-camera
 * page slide (vblank slDMACopy per memory rule saturn-vdp2-streaming-solved)
 * is the follow-on wave; this present is the parked post-tick anchor.
 *
 * Cells for THIS present are uploaded by p6_vdp2_upload_cells during the
 * GHZ load phase (between LoadSceneFolder's GIF decode and LoadSceneAssets'
 * entity placement -- the W11b tilesetPixels transient rule); this function
 * only zeroes the blank char (unreferenced by the rect, order-independent).
 *
 * Outputs for qa_p6_scroll.py: *out_pndhash = djb2-xor over the 16,384 map
 * bytes READ BACK from VDP2 VRAM (big-endian byte stream, proves the bytes
 * survived in VRAM); *out_nblank = count of non-empty layout words in the
 * visible scroll window (320x224 = the CORE_DEFS SCREEN_XMAX/SCREEN_YSIZE).
 * ========================================================================== */
extern void SaturnLayout_Bind(int slot, int layer);
extern unsigned short SaturnLayout_GetTile(int slot, int tx, int ty);

/* Perf Phase 2b (Task #211): sub-section attribution of the 850ms present.
 * VBLANK-MEASURED (overflow-immune) -- the present's heavy parts exceed the FRT
 * /32 78ms range. + the per-present SaturnLayout refill (zlib inflate) count.
 * Pack symbols (p6_vdp2.o is a pack member); the gate reads them by map name. */
extern volatile unsigned int p6_perf_vbl_count;       /* p6_perf.c true-60Hz tally */
extern int                   p6_w_lay_refills;          /* SaturnLayout.cpp cumulative (int32) */
__attribute__((used)) int p6_w_present_vbl_walk    = 0; /* blank-char GetTile walk */
__attribute__((used)) int p6_w_present_vbl_map     = 0; /* map build (GetTile + VDP2 writes) */
__attribute__((used)) int p6_w_present_vbl_hash    = 0; /* witness hash+count (DIAGNOSTIC) */
__attribute__((used)) int p6_w_present_refills     = 0; /* SaturnLayout inflates THIS present */

/* Perf Phase 2c (Task #211): the present's blank-char walk + 64x64 PND map +
 * the diagnostic hash are ALL derived from the STATIC GHZ layout (the present
 * reads a fixed top-left 64x64 via GetTile(0,x,y) -- camera-INDEPENDENT; the
 * hardware slScrPosNbg1 below does the camera). So that build is identical every
 * frame -- Phase 2b measured 6 redundant zlib inflates + ~820ms/frame of pure
 * waste. Build it ONCE (dirty), cache the hash/count, and per-frame run only the
 * CRAM upload (palette may cycle) + the hardware scroll. p6_vdp2_present_dirty is
 * re-armed on every GHZ (re)load (p6_ghz_arm_env). VDP2 map bytes are BYTE-
 * IDENTICAL to the per-frame rebuild (same static data) -- zero visual change. */
int p6_vdp2_present_dirty = 1;
static unsigned int s_present_pndhash = 0;
static int          s_present_nblank  = 0;

/* P6.8 W16-stream (Task #240): the present was a STATIC top-left 64x64 tile rect
 * built once and hardware-scrolled by slScrPosNbg1. The NBG1 plane is 64x64
 * tiles = 1024x1024 px and WRAPS; GHZ1 is thousands of px wide, so once the
 * camera scrolls past 1024 px the static rect shows nothing / the plane wraps
 * onto stale data ("missing grass blocks" + "all VDP2 goes glitchy when I move",
 * user 2026-06-14). Fix = a camera-ANCHORED streaming window: on every tile-
 * boundary crossing (camera tile origin ctx/cty changes) rewrite the visible
 * 320x224 region PLUS a 1-tile margin into the wrapping plane at (tx & 63,
 * ty & 63). The hardware slScrPosNbg1 still takes the FULL camera position --
 * the plane wraps mod 1024 px so the freshly-written cells line up seamlessly
 * no matter how far the camera has travelled. The rewrite is 23x17 cells which
 * fits inside ONE SaturnLayout 64x32 window (SaturnLayout.cpp:33-34), so it
 * costs at most one zlib refill per crossing (not per frame) -- it does NOT
 * reintroduce the per-frame full-plane GetTile walk Perf Phase 2c retired. */
static int          s_last_ctx = 0x7fffffff; /* camera tile origin at last build */
static int          s_last_cty = 0x7fffffff;
static int          s_blank_char = 0;

/* Streaming witnesses (read by qa_p6_fgstream.py from game.map). The self-check
 * walks the VISIBLE tile rect every present and compares each plane cell to the
 * layout GetTile -- this is the RED->GREEN signal: on the static build the
 * plane cannot match a camera that moved past 1024 px (visok_far stays 0); the
 * streaming build keeps it matched at any camera (visok_far -> 1). All visible
 * GetTile reads hit the same window the rewrite already loaded (visible 20x14
 * tiles fit one 64x32 window), so the check is array-read cheap, no refills. */
__attribute__((used)) int p6_w_fg_maxcamtx  = 0; /* max camera tile X seen (motion proof) */
__attribute__((used)) int p6_w_fg_visok      = 0; /* visible plane cells == layout this present */
__attribute__((used)) int p6_w_fg_visok_far  = 1; /* sticky: 0 if EVER mismatched at camtx>=64 */
__attribute__((used)) int p6_w_fg_crosses    = 0; /* cumulative map (re)builds: dirty + crossings */

/* Task #242 (user "chunks of grass missing while moving"): the present built the
 * VDP2 PND map with CPU stores DURING ACTIVE DISPLAY, which TEAR / land partially
 * (hand-port src/_archived/main_streaming_WORKING.c.bak:9-12; ST-210 SCU; memory
 * saturn-vdp2-streaming-solved). FIX (the proven in-repo src/rsdk/scene_ghz.c
 * ghz_fg_vblank pattern): build the 64x64 PND page in a CART (A-Bus) buffer, then
 * slDMAXCopy it to the VDP2 map IN THE VBLANK callback (reliable, tear-free).
 * ST-210: A-Bus READ -> B-Bus WRITE SCU-DMA is legal (only A-Bus write + VDP2
 * read + WRAM-L are barred). slDMAXCopy (NOT slDMACopy) per memory
 * sgl-audio-vs-scroll-cpu-dma-conflict (different SCU controller than audio CPU
 * DMA). P6_FG_PAGE is already a cache-through cart alias (0x227...), so the CPU
 * page writes and the DMA read are coherent without an extra |0x20000000. */
#define P6_FG_PAGE 0x227F0000u /* 4MB cart tail: 64x64x4 B = 16 KB (sheet store
                                * ends ~0x227E0A00; this 16 KB ends 0x227F4000) */
__attribute__((used)) int          p6_w_fg_dma     = 0; /* count of vblank page DMAs */
__attribute__((used)) volatile int p6_fg_dma_pending = 0; /* present->vblank handoff */
__attribute__((used)) volatile int p6_fg_scroll_x  = 0;   /* latest camera (vblank reads) */
__attribute__((used)) volatile int p6_fg_scroll_y  = 0;

/* Vblank callback: DMA the cart page to the VDP2 NBG1 map (only when it changed)
 * + arm the hardware scroll every vblank for smooth sub-tile panning even when
 * the game loop runs below 60 Hz. Registered via jo_core_add_vblank_callback. */
void p6_fg_vblank(void)
{
    if (p6_fg_dma_pending) {
        slDMAXCopy((void *)P6_FG_PAGE, (void *)P6_VDP2_MAP,
                   (Uint32)(4096u * 4u), Sinc_Dinc_Long); /* 4096 cells x 4 B */
        p6_fg_dma_pending = 0;
        ++p6_w_fg_dma;
    }
    slScrPosNbg1(toFIXED(p6_fg_scroll_x), toFIXED(p6_fg_scroll_y));
}

/* Task #242 ROOT CAUSE (MEASURED from extracted/Data/Stages/GHZ/Scene1.bin):
 * GHZ has TWO foreground layers -- "FG Low" (layer 3, 72,366 tiles) and
 * "FG High" (layer 4, 15,418 tiles). The present drew ONLY FG Low, so the
 * 11,321 cells that are EMPTY in FG Low but PRESENT in FG High (1,065 of them
 * in the visible grass band rows 44-64) rendered as transparent HOLES -- the
 * user's "chunks of grass missing". (NOT tearing: the vblank DMA ran, visok=1.)
 * FIX: composite FG High over empty FG-Low cells. The 4,097 BOTH-present cells
 * keep FG Low (a behind/in-front FG split that puts FG High in front of the
 * player is the later refinement; filling the holes is the user-visible fix). */
__attribute__((used)) int p6_w_fg_highfill = 0; /* FG-High composites this run (MEASURED) */

/* FG Low (slot 0) with a FG High (slot 1) fallback on empty cells. */
static unsigned short p6_fg_gettile(int x, int y, int *hf)
{
    unsigned short e = SaturnLayout_GetTile(0, x, y);
    if (e == 0xFFFF) {
        unsigned short eh = SaturnLayout_GetTile(1, x, y);
        if (eh != 0xFFFF) { e = eh; if (hf) ++*hf; }
    }
    return e;
}

/* PND word for a layout tile e (0xFFFF = empty -> blank char). Same packing as
 * the present map build (p6_vdp2.c: fy bit11->31, fx bit10->30, charno=tile*8). */
static unsigned long p6_pnd_for(unsigned short e, int blank)
{
    if (e == 0xFFFF)
        return (unsigned long)blank * 8u;
    return ((unsigned long)(e & 0x800) << 20)
         | ((unsigned long)(e & 0x400) << 20)
         | ((unsigned long)(e & 0x3FF) * 8u);
}

/* A1 (dual-SH2 STEP A, #246): split the FG present into the slave-MOVABLE
 * compute (PND-page rebuild + CRAM + self-check -- VRAM/CRAM DATA writes only,
 * no VDP register control) and the master-ONLY SGL register config. The compute
 * is what A2 forks to the slave; for A1 both still run on the master via
 * p6_vdp2_present_ghz_camera below (behavior-identical -- isolates the refactor
 * from the slave move so a gate regression is unambiguous). */
static void p6_present_compute(int layer, int scroll_x, int scroll_y,
                               const unsigned short *pal565,
                               unsigned int *out_pndhash, int *out_nblank)
{
    volatile Uint16 *cel  = (volatile Uint16 *)P6_VDP2_CEL;
    volatile Uint16 *map  = (volatile Uint16 *)P6_FG_PAGE; /* Task #242: build the
                              * PND page in the CART buffer (NOT VDP2) -- the
                              * vblank DMA (p6_fg_vblank) pushes it to VDP2 tear-
                              * free. Self-check reads this cart mirror too. */
    volatile Uint16 *cram = (volatile Uint16 *)P6_VDP2_CRAM;
    static unsigned char used[1024]; /* file-static would collide with the
                                      * Title present's local; own copy */
    int t, c, x, y;
    int ctx = scroll_x >> 4, cty = scroll_y >> 4;       /* camera tile origin */
    /* visible rect (20x14 tiles) plus a 1-tile margin all round = 23x17, which
     * fits one SaturnLayout 64x32 window so the rewrite is a single refill. */
    int vx0 = ctx - 1, vx1 = ctx + 21, vy0 = cty - 1, vy1 = cty + 15;
    int rebuild = p6_vdp2_present_dirty || ctx != s_last_ctx || cty != s_last_cty;
    unsigned int pv0, prf0 = (unsigned int)p6_w_lay_refills;

    if (rebuild) {
        if (p6_vdp2_present_dirty) {
            SaturnLayout_Bind(0, layer);     /* slot 0 = FG Low (layer 3) window */
            SaturnLayout_Bind(1, layer + 1); /* slot 1 = FG High (layer 4) window --
                                              * Task #242: composite over empty FG
                                              * Low cells. Bind ONLY on (re)load so
                                              * neither window is thrown away as the
                                              * camera pans. */
            /* BUG FIX (Task #241, user "blocks of grass missing"): empty
             * (0xFFFF) cells map to TILE 0, which is the RSDK canonical empty
             * tile -- VERIFIED transparent for GHZ (16x16Tiles.gif tile 0 = all
             * palette index 0). The previous code STOLE the smallest tile index
             * unused in the LOAD region and zeroed its CEL char; but the camera
             * streams into NEW regions where that index is a REAL tile, so its
             * (zeroed) char rendered as a transparent HOLE in the grass. Tile 0
             * is uploaded transparent by p6_vdp2_upload_cells and is never a
             * solid tile, so mapping empties to it needs NO char-stealing -- no
             * collision is possible level-wide. (The non-streaming
             * p6_vdp2_present_layout scans the WHOLE resident layout so its
             * stolen-blank stays safe; only the windowed streaming path broke.) */
            s_blank_char = 0;
            (void)used;
            /* Zero the whole cart page = tile 0 (transparent) everywhere; the
             * rebuild below drops real tiles into the visible+margin rect. The
             * page is cart garbage on first load, so this fill is mandatory. */
            for (t = 0; t < 4096 * 2; ++t) map[t] = 0;
            p6_w_present_vbl_walk = 0;
        } else {
            p6_w_present_vbl_walk = 0;
        }

        /* Rewrite the camera-anchored visible+margin rect into the wrapping
         * plane at (tx & 63, ty & 63). The hardware scroll uses the full camera
         * position, so these cells line up no matter how far we have travelled. */
        pv0 = p6_perf_vbl_count;
        {
            int hf = 0;
            for (y = vy0; y <= vy1; ++y) {
                int cyw = y & 63;
                for (x = vx0; x <= vx1; ++x) {
                    int cxw = x & 63;
                    unsigned short e = p6_fg_gettile(x, y, &hf); /* FG Low + FG High */
                    unsigned long pnd = p6_pnd_for(e, s_blank_char);
                    int page = ((cyw >> 5) << 1) + (cxw >> 5);
                    volatile Uint16 *p = map + page * 2048 + (((cyw & 31) << 5) + (cxw & 31)) * 2;
                    p[0] = (Uint16)(pnd >> 16);
                    p[1] = (Uint16)(pnd & 0xFFFF);
                }
            }
            p6_w_fg_highfill = hf;
        }
        p6_w_present_vbl_map = (int)(p6_perf_vbl_count - pv0);

        s_last_ctx = ctx;
        s_last_cty = cty;
        p6_vdp2_present_dirty = 0;
        ++p6_w_fg_crosses;
        p6_fg_dma_pending = 1; /* the cart page changed -> DMA it next vblank */
        p6_w_present_vbl_hash = 0;
        p6_w_present_refills  = (int)((unsigned int)p6_w_lay_refills - prf0);
    } else {
        /* Cached: no tile-boundary crossing this frame -- the plane already
         * holds the right cells; only the hardware scroll + CRAM run below. */
        p6_w_present_vbl_walk = 0;
        p6_w_present_vbl_map  = 0;
        p6_w_present_vbl_hash = 0;
        p6_w_present_refills  = 0;
    }

    /* Self-check witness (every present): compare each VISIBLE plane cell to the
     * live layout. All these GetTile reads hit the window the rewrite loaded, so
     * this is array-read cheap. visok_far stays 1 only while the plane matches
     * the layout at a camera that has moved past the 1024 px plane wrap. */
    {
        int tx0 = scroll_x >> 4, tx1 = (scroll_x + 320 - 1) >> 4;
        int ty0 = scroll_y >> 4, ty1 = (scroll_y + 224 - 1) >> 4;
        int ok = 1, n = 0;
        for (y = ty0; y <= ty1; ++y) {
            int cyw = y & 63;
            for (x = tx0; x <= tx1; ++x) {
                int cxw = x & 63;
                unsigned short e = p6_fg_gettile(x, y, 0); /* same FG Low+High */
                unsigned long pnd = p6_pnd_for(e, s_blank_char);
                int page = ((cyw >> 5) << 1) + (cxw >> 5);
                volatile Uint16 *p = map + page * 2048 + (((cyw & 31) << 5) + (cxw & 31)) * 2;
                if (p[0] != (Uint16)(pnd >> 16) || p[1] != (Uint16)(pnd & 0xFFFF))
                    ok = 0;
                if (e != 0xFFFF) ++n;
            }
        }
        /* out_pndhash retired (the streaming plane is camera-anchored, not a
         * fixed identity map); the visible-window non-blank count is the cheap
         * scroll-gate signal that survives. */
        s_present_pndhash = (unsigned int)n;
        s_present_nblank  = n;
        p6_w_fg_visok      = ok;
        if (ctx > p6_w_fg_maxcamtx) p6_w_fg_maxcamtx = ctx;
        if (ctx >= 64 && !ok) p6_w_fg_visok_far = 0;
    }
    *out_pndhash = s_present_pndhash;
    *out_nblank  = s_present_nblank;

    /* 3) CRAM bank 0 from the GHZ active palette (per-frame: the palette may
     *    cycle; only 256 writes, ~0 vbl -- engine RGB565 -> Saturn BGR555). */
    for (c = 0; c < 256; ++c) {
        unsigned short v = pal565[c];
        unsigned short r5 = (v >> 11) & 0x1F;
        unsigned short g5 = ((v >> 5) & 0x3F) >> 1;
        unsigned short b5 = v & 0x1F;
        cram[c] = (Uint16)(0x8000 | (b5 << 10) | (g5 << 5) | r5);
    }
}

/* Master-ONLY (ST-202 / dual-cpu-reference.md:142-148: only the master controls
 * VDP registers). NBG1 config + camera publish + display enable; runs on the
 * master after the slave present-compute joins (A2). */
static void p6_present_config(int scroll_x, int scroll_y)
{
    /* 5) NBG1 config + camera-anchored scroll + display (same SGL sequence
     *    as the proven Title present part 4). */
    slCharNbg1(COL_TYPE_256, CHAR_SIZE_2x2);
    slPageNbg1((void *)P6_VDP2_CEL, 0, PNB_2WORD);
    slPlaneNbg1(PL_SIZE_2x2);
    slMapNbg1((void *)P6_VDP2_MAP, (void *)P6_VDP2_MAP,
              (void *)P6_VDP2_MAP, (void *)P6_VDP2_MAP);
    /* Task #242: slScrPosNbg1 moved to p6_fg_vblank (runs at true 60 Hz for
     * smooth sub-tile panning); publish the latest camera for it to read. */
    p6_fg_scroll_x = scroll_x;
    p6_fg_scroll_y = scroll_y;
    slPriorityNbg1(1); /* FG plane BELOW the VDP1 sprites (HUD/Sonic/Tails at pri 7)
                        * so the characters render IN FRONT of the foreground plane
                        * (plants/totems), not behind it. No other VDP2 layer competes
                        * (BG parallax not yet drawn). Mania's player-between-FG-groups
                        * layering needs a behind/front FG split -- a later refinement. */
    slBack1ColSet((void *)P6_VDP2_BAK, 0x8000);
    slScrAutoDisp(NBG1ON | SPRON);
}

/* Public entry (the pack calls this extern "C"). A1: compute + config both on the
 * master, behavior-identical to the pre-split single function. A2 will fork
 * p6_present_compute to the slave between ProcessObjects and DrawLists, then call
 * p6_present_config here after the join (the register tail stays master-only). */
void p6_vdp2_present_ghz_camera(int layer, int scroll_x, int scroll_y,
                                const unsigned short *pal565,
                                unsigned int *out_pndhash, int *out_nblank)
{
    p6_present_compute(layer, scroll_x, scroll_y, pal565, out_pndhash, out_nblank);
    p6_present_config(scroll_x, scroll_y);
}

/* ============================================================================
 * A2 (dual-SH2 STEP A, #246): run p6_present_compute on the SLAVE SH-2, forked
 * mid-frame (after ProcessObjects, parallel with the master's DrawLists). The
 * pack calls p6_present_kick() right after ProcessObjects, then runs DrawLists,
 * then p6_present_join_config(). The 4.6ms present hides under DrawLists' 10.2ms
 * -> master frame 23.4 -> ~18.8ms (then STEP B's ~2ms cut crosses the cliff).
 *
 * Why mid-frame, not jo's frame-start auto fork-join: the present binds the
 * SaturnLayout slots 0/1 SHARED with the collision path in ProcessObjects (the
 * Task #237 GHZ2 fall-through). Forking at frame start would race collision on
 * those slots. After ProcessObjects collision is done; the slave present owns the
 * slots while master DrawLists (SaturnSheet, not SaturnLayout) runs in parallel,
 * and the inflate scratch is free (sheets resident -> DrawLists never inflates).
 *
 * COHERENCY (ST-202: no bus snooping): args master->slave via a CACHE-THROUGH
 * struct. Outputs: PND page = P6_FG_PAGE cache-through cart (DMA reads it); CRAM
 * = uncached I/O; the SaturnLayout window is slave-internal (collision refills
 * its OWN next frame). The master sees the slave's WRAM writes after
 * jo_core_wait_for_slave's slCashPurge at join. p6_fg_dma_pending (slave-set,
 * master-vblank-read) gets an explicit cache-through store at join time too.
 * ============================================================================ */
/* NB: 'pal' is an SGL.H macro (#define pal COL_32K) -- NEVER use it as an
 * identifier in an SGL-including TU; use palptr. core.h's jo_core_wait_for_slave
 * decl is not visible in this TU's include set, so prototype it explicitly. */
extern void jo_core_wait_for_slave(void);
extern void jo_core_exec_on_slave(void (*cb)(void));
static volatile struct { int layer, sx, sy; const unsigned short *palptr; } s_present_args;
extern int p6_w_slave_ticks; /* pack liveness witness */

/* Runs on the SLAVE (via slSlaveFunc). Reads args cache-through, runs compute,
 * then MIRRORS the master-visible outputs cache-through. The slave reads each
 * value back from its OWN cache (correct -- it just wrote it) and stores it
 * through the |0x20000000 alias so it reaches WRAM regardless of the SH-2 cache
 * write mode (write-back would otherwise leave the slave's stores in slave cache,
 * invisible to the master vblank + the savestate). Game-critical: p6_fg_dma_pending
 * (master vblank fires the FG DMA on it). Gate-critical: p6_w_fg_visok (the
 * FG-correctness witness). The present's persistent state (s_last_ctx etc.) is
 * slave-internal -- re-read by the slave next frame -- so it needs no mirror. */
static void p6_present_slave_entry(void)
{
    volatile int *a = (volatile int *)((unsigned int)&s_present_args | 0x20000000u);
    int layer = a[0], sx = a[1], sy = a[2];
    const unsigned short *palptr = (const unsigned short *)a[3];
    unsigned int hash; int nbl;
    p6_present_compute(layer, sx, sy, palptr, &hash, &nbl);
    *(volatile int *)((unsigned int)&p6_fg_dma_pending | 0x20000000u) = p6_fg_dma_pending;
    *(volatile int *)((unsigned int)&p6_w_fg_visok     | 0x20000000u) = p6_w_fg_visok;
    *(volatile int *)((unsigned int)&p6_w_fg_crosses   | 0x20000000u) = p6_w_fg_crosses;
    { volatile int *t = (volatile int *)((unsigned int)&p6_w_slave_ticks | 0x20000000u);
      *t = *t + 1; } /* liveness */
}

/* Pack-facing: publish args cache-through + fork the compute onto the slave. */
void p6_present_kick(int layer, int sx, int sy, const unsigned short *palptr)
{
    volatile int *a = (volatile int *)((unsigned int)&s_present_args | 0x20000000u);
    a[0] = layer; a[1] = sx; a[2] = sy; a[3] = (int)palptr;
    jo_core_exec_on_slave(p6_present_slave_entry);
}

/* Pack-facing: join the slave (slCashPurge -> master sees its WRAM writes), then
 * run the master-only NBG1 register config. Call AFTER the master's DrawLists. */
void p6_present_join_config(int sx, int sy)
{
    jo_core_wait_for_slave();
    p6_present_config(sx, sy);
}
