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

#if defined(P6_FRONTEND_TITLE)
/* ============================================================================
 * CP5b.3 (Task #272): the Mania TITLE BACKDROP on VDP2 NBG1 cell-mode.
 *
 * The engine Title scene (P6_FRONTEND_TITLE) renders ONLY the VDP1 foreground
 * (SONIC MANIA logo + Sonic head/finger) on a BLACK VDP2 backdrop. This puts
 * the green floating ISLAND (the Sonic-head-shaped landmass in water, Title
 * Scene1.bin tileLayer 3) + the CLOUDS (tileLayer 2) behind Sonic.
 *
 * SOURCE = the engine's OWN decoded Title data (no extra asset, no boot delay):
 *   tilesetPx  = engine tilesetPixels (LZW-decoded Title/16x16Tiles.gif, 1024
 *                16x16 tiles), uploaded to NBG1 cells A0+A1 by p6_vdp2_upload_cells
 *                during the load (front-end-gated call added in p6_scene_load_and_arm).
 *   islandLay  = tileLayers[3].layout (64x64; the green island sits in plane
 *                tiles (24,24)-(40,40) = px (384,384)-(640,640) per the Scene1.bin
 *                parse -- the visible bbox is 256x256).
 *   cloudLay   = tileLayers[2].layout (16x16 = 256x256 px; white clouds on the
 *                magenta-key sky -- the cloud puffs are the non-empty cells).
 *
 * RENDER MODEL (one NBG1 PL_SIZE_2x2 1024x1024 plane, B0 map, the proven
 * p6_vdp2_present_layout geometry: cells A0, charno=tile*8, 2-word PND, CRAM
 * bank 0, 4 pages 32x32):
 *   - The ISLAND layer's 64x64 layout is written 1:1 into the plane (its bbox
 *     holds the island; the rest is empty -> blank/transparent -> back-color sky).
 *   - The CLOUDS layer (16x16) is TILED across the plane rows ABOVE the island
 *     bbox (plane tile rows 0..23) so the sky region behind/above Sonic shows
 *     cloud puffs instead of a flat color. Cloud empty cells (magenta key) ->
 *     blank char -> the sky-blue back-color shows through.
 *   - The back-color is set to Mania sky-blue (0x?? BGR555) so every transparent
 *     cell renders as sky, not black.
 *   - Scroll: the island bbox top-left (px 384,384) is parked so the island sits
 *     in the lower-center of the 320x240 view (island top near screen y~96) and
 *     the cloud/sky band fills the upper rows.
 *   - NBG1 priority 1 (BELOW the VDP1 sprites at pri 7) so the logo + Sonic stay
 *     composited IN FRONT of the backdrop. SPRON kept on (the FG sprites).
 *
 * Per the decomp (TitleBG_SetupFX) the island is a Mode-7 ROTATING ground and
 * the clouds a per-line scroll; on Saturn that maps to RBG0 + coefficient table
 * (ST-058-R2 §6.4 / DEMOCOEF) + line-scroll (§5.3). This first cut lands the
 * STATIC composited island+clouds (the recognizable Mania backdrop content);
 * the live Mode-7 rotation is reported as the remaining stretch (CP5b.4).
 *
 * Magenta-key handling: Title/16x16Tiles.gif uses palette index 0 = magenta
 * (255,0,255) as the transparency placeholder (MEASURED: BG.gif + tiles GCT
 * slot 0). The engine's fullPalette[0][0] carries whatever color index 0 maps
 * to; cell pixels with index 0 are TRANSPARENT on VDP2 (color-bank 0, the
 * standard VDP2 transparent-code-0 behavior, ST-058-R2 §10.2) so the back-color
 * shows through. So no extra keying is needed -- index-0 cells are see-through.
 * ========================================================================== */

/* Sky-blue back-color (Mania title sky). MEASURED from the Horizon layer render
 * (_dbg_horizon.png): the upper sky is ~(0,96,224) RGB. Saturn BGR555 MSB-set:
 * r5=0>>3=0, g5=96>>3=12, b5=224>>3=28 -> 0x8000 | (28<<10) | (12<<5) | 0 = 0xF180. */
#define P6_TITLE_SKY_COL  0xF180u

/* Park the island so its bbox (plane px 384..640) sits low-center in 320x240.
 * island top px 384 -> screen y ~96 => scroll_y = 384 - 96 = 288.
 * island center x 512 -> screen center x 160 => scroll_x = 512 - 160 = 352. */
#define P6_TITLE_SCROLL_X  352
#define P6_TITLE_SCROLL_Y  288

/* The island's visible content lives in plane tile rows >= 24 (px>=384). The
 * cloud band is tiled into plane tile rows [0,24) so the sky above the island
 * (screen rows above ~96) carries cloud puffs. */
#define P6_TITLE_ISLAND_TILE_TOP  24

void p6_vdp2_present_title_backdrop(const unsigned char *tilesetPx,
                                    const unsigned short *islandLay, int islandWShift,
                                    const unsigned short *cloudLay, int cloudWShift,
                                    const unsigned short *pal565)
{
    volatile Uint16 *cel  = (volatile Uint16 *)P6_VDP2_CEL;
    volatile Uint16 *map  = (volatile Uint16 *)P6_VDP2_MAP;
    volatile Uint16 *cram = (volatile Uint16 *)P6_VDP2_CRAM;
    int t, c, x, y;
    int icols = 1 << islandWShift;   /* island layer width in tiles (64) */
    int ccols = 1 << cloudWShift;    /* cloud  layer width in tiles (16) */
    int crows;                       /* cloud layer height in tiles (16) */

    /* 1) Cells are uploaded SEPARATELY (p6_vdp2_upload_cells) during the load
     *    gap -- BEFORE LoadSceneAssets' memset reclaims the transient
     *    tilesetPixels window. This function runs AFTER LoadSceneAssets (so the
     *    palette + layouts are final) and only builds the map + CRAM + display.
     *    tilesetPx is accepted for signature symmetry but not re-read here. */
    (void)tilesetPx;

    /* cloud layer rows = same as cols for the 16x16 Clouds layer; derive from a
     * conservative 16 (its on-disk ysize). The caller passes the wshift; assume
     * square (Clouds is 16x16). */
    crows = ccols;

    /* 2) Blank char = a tile index neither layer references, zeroed so empties
     *    render transparent. (Same rule as p6_vdp2_present_layout.) */
    static unsigned char used[1024];
    for (t = 0; t < 1024; ++t) used[t] = 0;
    for (t = 0; t < icols * icols; ++t) {            /* island is 64x64 square */
        unsigned short e = islandLay[t];
        if (e != 0xFFFF) used[e & 0x3FF] = 1;
    }
    for (t = 0; t < ccols * crows; ++t) {
        unsigned short e = cloudLay[t];
        if (e != 0xFFFF) used[e & 0x3FF] = 1;
    }
    int blank = 0;
    while (blank < 1024 && used[blank]) ++blank;
    {
        volatile Uint16 *dst = cel + blank * 128;
        for (c = 0; c < 128; ++c) dst[c] = 0;
    }

    /* 3) Build the 64x64 PND map (2-word PNDs; high word flips at lower addr,
     *    low word charno=tile*8 at +2 -- big-endian bus, as present_layout). For
     *    each plane tile (y,x): use the ISLAND cell if non-empty; ELSE in the top
     *    band (y < ISLAND_TILE_TOP) use the CLOUD cell (tiled mod cloud dims);
     *    ELSE blank (-> sky-blue back-color). */
    for (y = 0; y < 64; ++y) {
        for (x = 0; x < 64; ++x) {
            unsigned short e = islandLay[(y << islandWShift) + x];
            if (e == 0xFFFF && y < P6_TITLE_ISLAND_TILE_TOP) {
                /* Tile the cloud layer across the sky band. */
                int cx = x % ccols;
                int cy = y % crows;
                e = cloudLay[(cy << cloudWShift) + cx];
            }
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

    /* 4) CRAM bank 0: engine RGB565 -> Saturn BGR555 (MSB set). Same as the
     *    proven presents; the Title palette's grass-greens + sky + cloud-whites
     *    land here. */
    for (c = 0; c < 256; ++c) {
        unsigned short v = pal565[c];
        unsigned short r5 = (v >> 11) & 0x1F;
        unsigned short g5 = ((v >> 5) & 0x3F) >> 1;
        unsigned short b5 = v & 0x1F;
        cram[c] = (Uint16)(0x8000 | (b5 << 10) | (g5 << 5) | r5);
    }

    /* 5) NBG1 config + park scroll + display. Priority 1 (below VDP1 sprites at
     *    pri 7) so the logo + Sonic composite in front. SPRON kept (FG sprites).
     *    Sky-blue back-color so transparent cells render as sky, not black. */
    slCharNbg1(COL_TYPE_256, CHAR_SIZE_2x2);
    slPageNbg1((void *)P6_VDP2_CEL, 0, PNB_2WORD);
    slPlaneNbg1(PL_SIZE_2x2);
    slMapNbg1((void *)P6_VDP2_MAP, (void *)P6_VDP2_MAP,
              (void *)P6_VDP2_MAP, (void *)P6_VDP2_MAP);
    slScrPosNbg1(toFIXED(P6_TITLE_SCROLL_X), toFIXED(P6_TITLE_SCROLL_Y));
    slPriorityNbg1(1);
    /* CP5b.3 FIX (MEASURED): the front-end lean boot leaves the VDP1 sprite layer
     * priority below NBG1's opaque cells -> the island/cloud cells OCCLUDED the
     * logo emblem/wings/Sonic head (peach muzzle 3204->0, gold ring 462->0 MEASURED).
     * Raise ALL 8 sprite priority banks to 7 (> NBG1 pri 1) so EVERY VDP1 sprite --
     * regardless of which priority bank its color-bank bits select -- composites IN
     * FRONT of the backdrop. (jo_set_layer_priority sets all 8 banks, core.c:320-327;
     * the GHZ scene_ghz.c:598 sets only bank 0 because its sprites all use bank 0,
     * but the Title logo/Sonic sprites may use others -> set them all.) */
    slPrioritySpr0(7);
    slBack1ColSet((void *)P6_VDP2_BAK, P6_TITLE_SKY_COL); /* sky-blue backdrop */
    slScrAutoDisp(NBG1ON | SPRON);
}

/* ============================================================================
 * SUB2 (#276 clouds-coexist): restore the CLOUD backdrop on NBG1, reading from
 * VRAM bank B1, COEXISTING with the rotating RBG0 island (A0 char / A1 coeff /
 * B0 pattern-name).
 *
 * THE COEXISTENCE PROOF (ST-058-R2, re-read 2026-06-23): VDP2_Manual.txt:1030
 * "The normal scroll screen can be displayed simultaneously with ONE rotation
 * scroll screen." So NBG1 + RBG0 DO coexist (the prior memory's "they can't"
 * was a misreading). The real constraint (:6449-6452): "VRAM cycle pattern
 * register settings of the VRAM bank selected in RAM used for the rotational
 * scroll are IGNORED" -- so NBG1 cannot read from A0/A1/B0 (RBG0's banks). It
 * MUST read from B1. RAMCTL=0x1327 (jo core.c:213) low byte 0x27: bits7,6 (B1)
 * = 00 = "Not used as RBG0" (Table at :6437-6441) -> B1's cycle pattern IS
 * honored -> NBG1 can read its char + pattern-name from B1.
 *
 * B1 layout (0x25E60000..0x25E7FFFF, 128 KB; the back-color word at 0x25E7FFFE
 * is preserved): cloud CELLS compacted at 0x25E60000 (a RUNTIME REMAP -- the
 * cloud layer references only a subset of the 1024 tiles, so collect the
 * distinct tiles, copy their 256-B cell blocks compactly, and rewrite the map
 * charno to the compact index; this fits B1 even if all 256 cloud tiles are
 * distinct = 64 KB), cloud MAP at 0x25E78000 (one 32x32 page = 4 KB).
 *
 * NBG1 reads char + map from B1 only (slCharNbg1/slPageNbg1/slMapNbg1 all point
 * at B1); SGL's slScrAutoDisp(NBG1ON|RBG0ON|SPRON) schedules the NBG1 cycle in
 * B1 (the only normal-scroll-eligible bank). The clouds park in the UPPER band
 * (above the island), priority 1 (below RBG0 island pri 2, below VDP1 sprites
 * pri 7). Index-0 cloud cells are transparent -> the sky back-color shows.
 * ========================================================================== */
#define P6_CLOUD_CEL  0x25E60000u  /* B1: compacted cloud cells (<= 64 KB)       */
#define P6_CLOUD_MAP  0x25E78000u  /* B1: cloud pattern-name map (1 page, 4 KB)  */

/* ROOT-CAUSE FIX (#276 clouds, 2026-06-23, PROVEN via ST-058-R2 + LIBSGL disasm):
 * the VDP2 character number IS the VRAM byte address / 0x20 from the VRAM BASE
 * (0x25E00000), NOT bank-relative -- "The character number designates the address
 * of the character pattern (VRAM). The boundary ... is always 20H" (VDP2_Manual.txt
 * :3615-3617). There is NO per-bank character-base register in cell mode. The prior
 * code copied cloud cells to B1 (P6_CLOUD_CEL, VRAM-rel 0x60000) but wrote map
 * charno = slot*8 -> HW fetched char data at (slot*8)*0x20 = VRAM-rel ~0 = bank A0
 * (the island/blank cells), NOT B1 -> clouds blank (index-0 -> transparent sky).
 * The island RBG0 path works precisely BECAUSE its cells live in A0 (VRAM-rel 0)
 * so its charno=tile*8 is self-consistent; the clouds put cells in B1 but kept an
 * A0-relative charno -- the asymmetry IS the bug. FIX: add the B1 base charno so the
 * charno indexes B1: a 256-color tile is 256 B = 8 charno units (each 0x20 B), so
 * the B1 base (0x60000 B) = 0x60000/0x20 = 0x3000 charno units. Each cloud cell's
 * charno = P6_CLOUD_CHARNO_BASE + slot*8 -> char addr = 0x60000 + slot*256 = the
 * exact B1 byte where the cell was copied. Derived generically from P6_CLOUD_CEL so
 * it tracks the address. Fits the 2-word PND 15-bit charno (max 0x3000+255*8=0x37F8
 * < 0x8000, VDP2_Manual.txt:3401 Table 4.6). */
#define P6_CLOUD_CHARNO_BASE  (((P6_CLOUD_CEL) - 0x25E00000u) / 0x20u)  /* = 0x3000 */

/* SESSION 2026-06-23g ISSUE 2 FIX (clouds double-layer): the decomp clouds are an
 * UPPER-HALF band ONLY -- TitleBG_Scanline_Clouds (TitleBG.c:136-138) sets clip
 * y[0, SCREEN_YSIZE/2] = y[0,120]. The prior code tiled the 16x16 cloud layout
 * across the FULL 32x32 cloud map page -> the clouds filled the entire 1024x1024
 * NBG1 plane = a dense full-screen white-wisp field the user read as a "static
 * cloud background" (+ its slow scroll = the "moving layer" -> the double-layer
 * perception). With the cloud config scroll_y=0 (p6_frontend_frame:5648), plane
 * tile row N maps 1:1 to screen tile row N, so screen y<120 = plane tile rows 0..7.
 * Restrict cloud tiles to rows < P6_CLOUD_BAND_ROWS; the rest -> blank/transparent
 * -> the flat sky-blue back-color shows in the lower band (a single upper cloud
 * band, matching the decomp clip). 8 rows = screen y 0..127 (just past the y=120
 * clip, so the band's lower edge is off-screen-soft, not a hard tile cut). */
#define P6_CLOUD_BAND_ROWS  8u   /* plane tile rows 0..7 = screen y 0..127 (decomp y[0,120]) */

__attribute__((used)) int p6_w_title_clouds_armed = 0; /* 1 once clouds armed */
__attribute__((used)) int p6_w_title_clouds_ntiles = 0; /* distinct cloud tiles remapped */
/* NBG1 cloud char-base witness (#276 charno fix) -- NO new .bss: the resolved HW char
 * address for compact slot 1 = (P6_CLOUD_CHARNO_BASE + 8) * 0x20 is a COMPILE-TIME
 * CONSTANT (it depends only on P6_CLOUD_CEL), so it is folded into the existing
 * p6_w_title_clouds_cellsum's sibling region via the macro below -- the gate reads
 * P6_CLOUD_CHARNO_BASE from this source. GREEN target == ((BASE+8)*0x20) in
 * B1 [0x60000,0x80000); the pre-fix bug had charno=slot*8 -> 8*0x20=0x100 in bank A0
 * -> the B1-scheduled N1CG cycle fetched A0 (index-0 transparent) -> blank clouds. */
#define P6_CLOUD_CHARADDR_SLOT1  (((P6_CLOUD_CHARNO_BASE) + 8u) * 0x20u) /* = 0x60100 (in B1) */

/* MEASURED witness (#276 clouds, 2026-06-23): djb2 over the B1 cloud cell block
 * actually written. RED root cause was clouds_cellsum==compile-time-blank (all
 * zeros) because the cell-copy read tilesetPx AFTER LoadSceneAssets reclaimed it;
 * a non-trivial sum proves real cloud pixels reached B1. */
__attribute__((used)) int p6_w_title_clouds_cellsum = 0;

/* Build the clouds-only NBG1 on bank B1. cloudLay = tileLayers[2].layout (16x16).
 *
 * #276 ROOT-CAUSE FIX (MEASURED 2026-06-23 via a savestate VRAM peek: B1 cloud
 * cells @0x25E60000 were ALL ZERO while A0 island cells @0x25E00000 were valid):
 * the cloud cells are copied from the ALREADY-RESIDENT A0 VRAM cells (P6_VDP2_CEL,
 * uploaded by p6_vdp2_upload_cells), NOT from the volatile `tilesetPx` transient.
 * WHY: the Title scene enters via a FOLDER-RELOAD, so the load-gap clouds arm
 * (io_main.cpp:5059, gated `!p6_folderReload`) is SKIPPED, and the fallback arm
 * (io_main.cpp:5215) runs AFTER LoadSceneAssets memset-reclaimed tilesetPixels
 * (the W11b entityList-window alias) -> the cell-copy read zeros. The A0 cells
 * are uploaded by an UNGUARDED call (io_main.cpp:4466) so they are reliably
 * resident; tile i's 256-B block is packed identically at A0 + i*128 words, so a
 * VRAM->VRAM word copy reproduces the exact cloud cell bytes regardless of when
 * this arm runs or whether the transient survived. tilesetPx is now unused
 * (kept in the signature for call-site symmetry; the A0 source is canonical). */
void p6_vdp2_title_clouds_b1_arm(const unsigned char *tilesetPx,
                                 const unsigned short *cloudLay, int cloudWShift)
{
    volatile Uint16 *cel  = (volatile Uint16 *)P6_CLOUD_CEL;
    volatile Uint16 *map  = (volatile Uint16 *)P6_CLOUD_MAP;
    volatile Uint16 *a0   = (volatile Uint16 *)P6_VDP2_CEL; /* resident island+cloud cells */
    int ccols = 1 << cloudWShift;        /* cloud layer width in tiles (16) */
    int crows = ccols;                   /* Clouds is 16x16 square */
    int x, y, c, r;
    unsigned int csum = 5381u;
    (void)tilesetPx;                     /* A0 VRAM is the source now (see note above) */
    (void)r;

    /* 1) Collect the distinct cloud tile indices + assign each a COMPACT charno in
     *    B1. remap[tile] = compact slot (+1; 0 = unused). Slot 0 is the BLANK char
     *    (zeroed cells -> transparent) for empty (0xFFFF) cloud cells. */
    static unsigned short remap[1024];
    int i;
    for (i = 0; i < 1024; ++i) remap[i] = 0;
    int nslots = 1;                      /* slot 0 reserved = blank/transparent */
    for (i = 0; i < ccols * crows; ++i) {
        unsigned short e = cloudLay[i];
        if (e == 0xFFFF) continue;
        int tile = e & 0x3FF;
        if (!remap[tile]) {
            if (nslots < 256) remap[tile] = (unsigned short)nslots++;  /* cap 256 -> 64 KB */
        }
    }
    p6_w_title_clouds_ntiles = nslots - 1;

    /* 2) Zero slot 0 (blank/transparent) + copy each distinct cloud tile's 256-B
     *    cell block from the RESIDENT A0 cells (already packed 2x2-cell TL,TR,BL,BR,
     *    8bpp, 2 px/word by p6_vdp2_upload_cells) into its compact B1 slot. A pure
     *    VRAM->VRAM word copy: A0 block for tile i is at a0 + i*128, the B1 compact
     *    slot at cel + remap[i]*128. djb2 the bytes written -> clouds_cellsum
     *    witness (proves non-zero pixels reached B1). */
    {
        volatile Uint16 *dst0 = cel; /* slot 0 = blank */
        for (c = 0; c < 128; ++c) dst0[c] = 0;
    }
    for (i = 0; i < 1024; ++i) {
        if (!remap[i]) continue;
        volatile Uint16 *src = a0  + i * 128;             /* resident A0 cell block */
        volatile Uint16 *dst = cel + (int)remap[i] * 128; /* 256 B = 128 words */
        for (c = 0; c < 128; ++c) {
            Uint16 w = src[c];
            dst[c]   = w;
            csum = ((csum << 5) + csum) ^ (unsigned int)w;
        }
    }
    p6_w_title_clouds_cellsum = (int)csum;

    /* 3) Build the cloud pattern-name map (one 32x32-tile page; the 16x16 cloud
     *    layout tiled into it). 2-word PND: charno = P6_CLOUD_CHARNO_BASE + slot*8
     *    (8bpp 16x16 tile spans 8 charno units of 0x20 B; the B1 base = 0x3000 units
     *    so the HW char address resolves to B1 where the cells were copied -- see the
     *    P6_CLOUD_CHARNO_BASE root-cause note). Flips from the layout bits. Empty cells
     *    -> slot 0 (blank/transparent) = charno P6_CLOUD_CHARNO_BASE (B1 slot-0 zeros). */
    for (y = 0; y < 32; ++y) {
        for (x = 0; x < 32; ++x) {
            /* ISSUE 2 FIX: only the UPPER band rows carry clouds (decomp y[0,120]).
             * Rows >= P6_CLOUD_BAND_ROWS -> force empty so the lower band is flat sky
             * (no full-screen cloud field). */
            unsigned short e = (y < (int)P6_CLOUD_BAND_ROWS)
                             ? cloudLay[((y % crows) << cloudWShift) + (x % ccols)]
                             : 0xFFFF;
            unsigned long charno;
            if (e == 0xFFFF)
                charno = P6_CLOUD_CHARNO_BASE;          /* B1 slot 0 = zeroed/transparent */
            else
                charno = P6_CLOUD_CHARNO_BASE + (unsigned long)remap[e & 0x3FF] * 8u;
            unsigned long pnd = ((unsigned long)(e == 0xFFFF ? 0 : (e & 0x800)) << 20) /* fy bit11 -> 31 */
                              | ((unsigned long)(e == 0xFFFF ? 0 : (e & 0x400)) << 20) /* fx bit10 -> 30 */
                              | charno;
            volatile Uint16 *p = map + (((y & 31) << 5) + (x & 31)) * 2;
            p[0] = (Uint16)(pnd >> 16);
            p[1] = (Uint16)(pnd & 0xFFFF);
        }
    }
    /* (Char-base witness is the compile-time constant P6_CLOUD_CHARADDR_SLOT1 = 0x60100,
     * proven in-B1; no runtime store -> no new .bss. See the macro's note.) */
    p6_w_title_clouds_armed = 1;
}

/* Configure + park NBG1 clouds on B1. MUST be called each frame AFTER the RBG0
 * island frame (p6_vdp2_title_island_rbg0_frame) so NBG1's plane/map registers are
 * the LAST writes that survive into the vblank register DMA -- the island frame's
 * slScrAutoDisp(RBG0ON|NBG1ON|SPRON) + RBG0 plane setup otherwise leaves NBG1's
 * MPABN1 at its default (MEASURED 2026-06-23: MPABN1=0x0000 when this ran BEFORE
 * the island frame -> NBG1 plane base resolved off the cloud map -> blank).
 *
 * USE THE PROVEN GEOMETRY (PL_SIZE_2x2, 4 planes = same page) identical to the
 * flat backdrop p6_vdp2_present_title_backdrop that rendered NBG1 correctly; the
 * cloud map at P6_CLOUD_MAP is plane-aligned in B1 and all 4 plane pointers
 * replicate the one 32x32 cloud page across the 64x64 plane (the clouds tile). The
 * earlier PL_SIZE_1x1 variant was untested and resolved the wrong plane base.
 * NBG1 reads char + map from B1 ONLY (B1's cycle is honored -- not RBG0-claimed). */
void p6_vdp2_title_clouds_b1_config(int scroll_x, int scroll_y)
{
    /* CLOUD-FLICKER FIX (#276, user 2026-06-23 "clouds constantly flickering"):
     * the static NBG1 GEOMETRY (char/page/plane/map) is asserted ONCE, not every
     * frame. Re-writing the MPOFN/PLSZ/CHCTL + map registers every frame raced the
     * SGL vblank register-image DMA and dropped a chunk of cloud cell-fetches on
     * ~1/4 of frames -> the wisp-blink (qa_title_clouds_stable.py RED: 6/24 dips).
     * slScrAutoDisp does NOT touch CHCTL/MPOFN/PLSZ (only BGON + the cycle table),
     * so the geometry PERSISTS; only the SCROLL position changes per frame. */
    static int s_clouds_geom = 0;
    if (!s_clouds_geom) {
        slCharNbg1(COL_TYPE_256, CHAR_SIZE_2x2);
        slPageNbg1((void *)P6_CLOUD_CEL, 0, PNB_2WORD); /* cell base = B1 (RBG0 banks ignored) */
        slPlaneNbg1(PL_SIZE_2x2);                        /* proven geometry (= flat backdrop) */
        slMapNbg1((void *)P6_CLOUD_MAP, (void *)P6_CLOUD_MAP,
                  (void *)P6_CLOUD_MAP, (void *)P6_CLOUD_MAP); /* map base = B1, all 4 planes */
        s_clouds_geom = 1;
    }
    slScrPosNbg1(toFIXED(scroll_x), toFIXED(scroll_y)); /* scroll: the only per-frame NBG1 write */
    slPriorityNbg1(1);                               /* below RBG0 island (2) + sprites (7) */

    /* MPOFN (NBG1 map-offset) IS handled by slMapNbg1 -- DISASSEMBLY-PROVEN, do NOT
     * patch it (2026-06-23, sh-elf-objdump of LIBSGL sglB032.o _slMapNbg1):
     *   slMapNbg1 computes, for a 2-word/2x2-cell NBG1, the 9-bit map-select
     *   value = (planeAddr - 0x25E00000)/0x1000 and writes its low 6 bits to N1MPA
     *   (image+0x44) AND its high 3 bits to MPOFN N1 (image low-byte 0x060FFCFD,
     *   GBR+253). For P6_CLOUD_MAP=0x25E78000 -> sel9=0x78 -> N1MPA=0x38, MPOFN_N1=1
     *   -> plane base resolves to VRAM-rel 0x78000 = the cloud map. CROSS-CHECKED
     *   against the WORKING GHZ NBG1 (map 0x25E40000, MPOFN_N1 also auto-set by SGL,
     *   NO patch) -- if SGL didn't set MPOFN, GHZ (which needs N1=1 for its 0x40000
     *   map) would be blank too, and it renders. The SGL 144-byte register-image DMA
     *   (slInitSystem insc_01, size 0x90) carries image bytes 0x00..0x8F = MPOFN(0x3C)
     *   + N1MPAB(0x44) every flush, so the value persists to VDP2.
     *
     * The PRIOR manual patch wrote 0x060FFD20 = image+0x60 = MPABRB (Rotation
     * Parameter B plane-A/B map, VDP2_Manual.txt:4343) -- a register RBG0 (param A)
     * never uses, so it was a genuine NO-OP that touched NOTHING relevant. Removed.
     * The REAL clouds-blank cause was the cloud-cell CHARNO missing the B1 base
     * (P6_CLOUD_CHARNO_BASE; fixed in p6_vdp2_title_clouds_b1_arm), not the offset. */
}

/* CP5b.3: per-frame cloud drift. The decomp Scanline_Clouds rolls the cloud
 * band horizontally (sine-driven) each frame; TitleBG_StaticUpdate accumulates
 * TitleBG->timer += 0x8000 = +0.5 px/frame Y. This is the cheap Saturn analog:
 * scroll the WHOLE NBG1 plane slowly (the island rides along, which on a flat
 * plane reads as a gentle drift -- not the per-layer Mode-7/line-scroll, but it
 * animates the backdrop). Called each front-end frame; pure register write. */
void p6_vdp2_title_backdrop_scroll(int frame)
{
    /* CP5b.6 (#276): the island is now a ROTATING RBG0 plane, NOT a flat NBG1 cell
     * plane (the RBG0 island map lives in bank B0 per RAMCTL, the same bank NBG1's
     * map used -- so NBG1 can no longer render the backdrop; see p6_vdp2_title_island
     * _rbg0_frame). So this function NO LONGER arms NBG1 or the display flags --
     * p6_vdp2_title_island_rbg0_frame (called right after this) does the final
     * slScrAutoDisp(RBG0ON|SPRON) + back-color + sprite priority. This is kept only
     * as the (now no-op) per-frame hook; the sprite>backdrop priority is re-asserted
     * so a stray SGL call cannot drop the FG sprites in front of the island. */
    (void)frame;
    slPrioritySpr0(7);
}

/* ============================================================================
 * CP5b.6 (Task #276): the title island MODE-7 ROTATION via VDP2 RBG0 + a per-line
 * coefficient table. The decomp (TitleBG_Scanline_Island, TitleBG.c:159-178) draws
 * the island as a perspective-rotated GROUND (RSDK ScanlineInfo deform/position per
 * the engine's texel-walk DrawLayer): a per-line (deform.x/y) texture step + a
 * per-line (position.x/y) texture START, driven by TitleBG->angle (+1/frame, 10-bit).
 * The user's "flat island" complaint = we render it as a STATIC NBG1 cell plane.
 *
 * SATURN MAPPING (ST-058-R2 VDP2 §6, screen-coord formula :6343-6349):
 *   X = kx(Xsp + dX*Hcnt) + Xp ;  Y = ky(Ysp + dY*Hcnt) + Yp
 * RBG0 rotation-scroll with Rotation Parameter A + a per-line coefficient table
 * (Coefficient Data Mode 0: kx=ky=coeff[line], read 1 entry/line via K_LINE):
 *   - The coeff per line carries the decomp deform.x magnitude (= -cos*id>>7, the
 *     per-pixel texture X-step). dX=1.0 -> kx*dX*Hcnt = coeff*Hcnt = the per-pixel
 *     walk, mirroring the decomp lx += deform.x.
 *   - The RPT matrix + screen-start carry the rotation/translation so kx*Xsp + Xp
 *     reproduces the per-line texture origin (position.x).
 *
 * THE RPTA WIRE-UP (the prior blocker, now data-proven via LIBSGL disassembly +
 * savestate peeks):  SGL keeps a 144-byte VDP2-register MIRROR at WRAM 0x060FFCC0
 * (== VDP2 reg base 0x25F80000) and DMAs it to the registers every vblank in its
 * _BlankIn handler (LIBSGL sglI00.o:0x9e). The RPTA register (VDP2 offset 0xBC)
 * is mirrored at 0x060FFCC0+0xBC = 0x060FFD7C. MEASURED (p6_ghz.mcs, RBG0 off):
 * the SGL DEFAULT mirror already holds RPTA = 0x0001FF80 -> RPT base = 0x3FF00 ->
 * VRAM 0x05E3FF00 == the SGL RBG_PARA_ADR == our P6_RBG0_RPT. So RPTA is ALREADY
 * VALID + already points HERE -- we must NOT disturb it. The prior failures
 * (RPTA=0x92140 / 0x78000) came from CALLING slRparaInitSet / slScrMatSet, which
 * RELOCATE the RPT (and slScrMatSet zeroes our KAst + sets RotTransFlag bit0 ->
 * the vblank then DMAs SGL's zeroed WRAM ROTSCROLL OVER our VRAM RPT). So:
 *   - We write the RPT matrix + KAst + screen-start DIRECTLY to 0x05E3FF00.
 *   - We do NOT call slRparaInitSet (RPTA stays at the working SGL default 0x3FF00).
 *   - We do NOT call slScrMatSet/slScrMatConv/slZrotR (RotTransFlag 0x060FFCCC stays
 *     0 -- MEASURED -- so the vblank SKIPS its WRAM-ROTSCROLL->VRAM matrix DMA and
 *     leaves our manual RPT intact; the gate at sglI00.o:0xd0 tests RotTransFlag&1).
 *   - KTCTL is driven via the SGL API slKtableRA (it lands in the mirror + is DMA'd;
 *     MEASURED KTCTL=0x0061 lived when slKtableRA ran -- raw KTCTL writes get
 *     overwritten by the same vblank flush, so the API is mandatory here).
 * Net: zero SGL rotation-matrix calls; RBG0 enabled via slScrAutoDisp(RBG0ON) +
 * KTCTL via slKtableRA; everything else is our direct VRAM writes -> RPTA valid.
 *
 * The existing NBG1 sky+cloud+flat-island backdrop is KEPT (no regression). RBG0
 * draws the rotating island at a HIGHER VDP2 priority over the lower band; where
 * RBG0 samples a transparent (index-0) island texel, the NBG1 backdrop shows
 * through. The FG VDP1 sprites (logo/Sonic) stay at sprite priority 7 (in front).
 * ========================================================================== */

/* RBG0 VRAM banks -- DICTATED by the live RAMCTL (jo sets RAMCTL=0x1327 at boot,
 * core.c:213). The low byte 0x27 = the rotation-data-bank-select bits (ST-058-R2
 * RDBSA/RDBSB @ 18000EH, VDP2_Manual.txt:6432-6441):
 *   RDBSA bits1,0 (A0) = 0b11 -> A0 = RBG0 CHARACTER pattern  (island cells live here)
 *   RDBSA bits3,2 (A1) = 0b01 -> A1 = RBG0 COEFFICIENT table  (per-line kx)
 *   RDBSB bits5,4 (B0) = 0b10 -> B0 = RBG0 PATTERN-NAME table (the island map)
 *   RDBSB bits7,6 (B1) = 0b00 -> B1 = NOT used by RBG0
 * So the hardware reads the RBG0 island map from B0, the cells from A0, the coeff
 * from A1 -- these addresses are NOT free choices, they MUST match RAMCTL.
 *
 * ROOT CAUSE of the blank island (#276, MEASURED 2026-06-23 via the RAMCTL decode +
 * a savestate peek of the RBG0 map): the prior code wrote the island map to B1
 * (0x25E60000), but RAMCTL designates B0 for the RBG0 pattern-name read -- so the HW
 * read RBG0 PN from B0 (stale/unrelated data) and NEVER from B1. Moving the map to
 * B0 (the RAMCTL-designated RBG0-PN bank) is THE fix. (The savestate confirmed the
 * island layer references only tiles 5..428 -> all < 512 -> all cells fit A0 alone,
 * so the A1 coeff bank never collides with a referenced island cell.) */
#define P6_RBG0_RPT   0x25E3FF00u  /* = RBG_PARA_ADR (A1+0x1FF00); RPTA default points here */
#define P6_RBG0_KTBL  0x25E20000u  /* = KTBL0_RAM (A1); RDBSA1=01 -> A1 is the coeff bank      */
#define P6_RBG0_MAP   0x25E40000u  /* = RBG0_MAP (B0); RDBSB0=10 -> B0 is the RBG0 PN bank.
                                    * (The HW reads RBG0 pattern-name from B0, NOT B1.)        */
#define P6_RBG0_KAST  0x00008000u  /* KAst integer = (P6_RBG0_KTBL & 0x7FFFF) >> 2 (2-word=4H) */

/* The island band on screen. The decomp clips y[168,240) on its 240-line screen
 * (SetClipBounds 0,0,168,size.x,SCREEN_YSIZE=240); the perspective loop i=16..87
 * maps to scanlines 168..239 with the NEAR/foreground line i=87 at y239 = screen
 * bottom. BUT the Saturn ENGINE compiles SCREEN_YSIZE=224 (build_p6scene_objs.sh:95)
 * and VDP2 RBG0 renders 224 PHYSICAL scanlines -- so the decomp's coeff[224..239]
 * (band lines i=72..87, the near/foreground = the LARGEST head detail) are below
 * the visible screen and never read. THAT cut the head's foreground -> the user's
 * "appears small / only part of the head" symptom (#276, fresh-context root cause).
 * FIX: place the full 72-line band at the BOTTOM of the 224-line display --
 * foreground i=87 at y(152+71)=223 = screen bottom, horizon i=16 at y152. This is
 * the platform-correct equivalent of the decomp's y239-foreground placement.
 * MEASURED (sim_island_decomp.py render224 @224 lines): LINE0=168 = 5157 land px
 * (foreground CUT); LINE0=152 = 6653 (full island fits). */
#define P6_ISLAND_LINE0   152      /* 224-line display: band y[152,224) (decomp was 168 on 240) */
#define P6_ISLAND_NLINES  72       /* i in 16..88 -> 72 lines */

/* The texel the screen-center dot samples = the landmass CENTER. The decomp
 * (TitleBG.c:174-175) centers the rotation on texel 512.0 (the +0x2000000 term);
 * the island TileLayer's occupied bbox is x[384..639] y[384..639], center
 * (511,511) -- MEASURED via render_scene.py TileLayer 3 + sim_island_decomp.py.
 * So the screen-center MUST sample 512 = the head's geometric center. A prior 448
 * "green-bulk" value (a green-centroid gate over-fit) shifted sampling onto the
 * quills -> only a LEFT SLIVER of the head showed (the user's "1/4 island /
 * appears small"). sim_island_decomp.py proves the Saturn RBG0 formula at 512 ==
 * the decomp scanline band EXACTLY (band mean|diff| 0.00 at EVERY angle). */
#define P6_ISLAND_CTR_TEXEL  256u  /* head SHIFTED into page 0: new center = texel 256
                                    * (was decomp 512 = page boundary -> 1-of-4 quadrant) */

/* Witnesses (read by qa_title_island_rot.py from game.map). armed=1 once arm ran;
 * angle = the live TitleBG->angle the last frame built; kast = the RPT KAst integer
 * we wrote; coeff0 = the first island line's coeff word; rpta = the live RPTA reg
 * read back (the RED->GREEN signal -- valid == in-VRAM pointing at the RPT). */
__attribute__((used)) int p6_w_title_island_armed  = 0;
__attribute__((used)) int p6_w_title_island_angle  = -1;
__attribute__((used)) int p6_w_title_island_kast   = 0;
__attribute__((used)) int p6_w_title_island_coeff0 = 0;
__attribute__((used)) int p6_w_title_island_rpta   = 0; /* live RPTA reg (RPTAU<<16|RPTAL)<<1 */
__attribute__((used)) int p6_w_isl_tx = 0; /* MEASURE: HW screen-center sampled texel-X @ mid band line */
__attribute__((used)) int p6_w_isl_ty = 0; /* MEASURE: HW screen-center sampled texel-Y @ mid band line */

/* 16-page map for the RBG0 island plane (sl16MapRA). FILE-SCOPE so the per-frame
 * fn can RE-ASSERT it: slScrAutoDisp re-derives RBG0's plane config every frame
 * and resets it toward a single 512x512 page -> only the head's top-left quadrant
 * samples (the "1 of 4 pieces"). Populated once in the arm. */
static unsigned char p6_island_map16[16];

/* Build the RBG0 map for the island layer (64x64 tiles, 2-word PND), reusing the
 * A0 cells already uploaded by p6_vdp2_upload_cells. Empty cells -> blank char
 * (transparent -> NBG1 backdrop shows through). One PL_SIZE_2x2 plane (4 pages). */
static void p6_island_build_map(const unsigned short *islandLay, int islandWShift, int blank)
{
    volatile Uint16 *map = (volatile Uint16 *)P6_RBG0_MAP;
    int x, y;
    for (y = 0; y < 64; ++y) {
        for (x = 0; x < 64; ++x) {
            /* SHIFT the landmass into page 0 (#276 "1 of 4 pieces" ROBUST fix,
             * user "add the other 3 pieces"). MEASURED: 3 sl16MapRA builds changed
             * NOTHING -> the RBG0 plane is effectively a single 512x512 page. The
             * head is a 256x256 block at texels 384..639 (tiles 24..39) CENTERED on
             * texel 512 = the page boundary, so only its top-left quadrant (384..512)
             * lands in the one rendered page = the user's "1 of 4". The whole 256x256
             * head FITS in a 512x512 page -> shift the source read +16 tiles (+256
             * texels) so the head moves to tiles 8..23 (texels 128..384), entirely
             * inside page 0. The rotation re-centers on texel 256 (Mx/My base below).
             * Sidesteps the non-functional multi-page sl16MapRA path entirely. */
            int icols = 1 << islandWShift;
            unsigned short e = islandLay[(((y + 16) & (icols - 1)) << islandWShift) + ((x + 16) & (icols - 1))];
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
}

/* One-time RBG0 island arm. islandLay = tileLayers[3].layout (the green island).
 * Builds the map + the constant RPT fields (KAst/dKAst + viewpoint), enables the
 * coeff table control (slKtableRA), char/plane/map (slCharRbg0/slPlaneRA/sl1MapRA/
 * slOverRA), then RBG0ON. The per-line coeff + the angle-dependent matrix/start are
 * (re)written every frame by p6_vdp2_title_island_rbg0_frame. */
void p6_vdp2_title_island_rbg0_arm(const unsigned short *islandLay, int islandWShift)
{
    volatile unsigned long *rpt = (volatile unsigned long *)P6_RBG0_RPT;
    int i;

    /* Pick a blank char = a tile index the island layer never references; zero its
     * A0 cells so empties render transparent (same rule as the NBG1 present). */
    static unsigned char used[1024];
    for (i = 0; i < 1024; ++i) used[i] = 0;
    {
        int icols = 1 << islandWShift, t;
        for (t = 0; t < icols * icols; ++t) {
            unsigned short e = islandLay[t];
            if (e != 0xFFFF) used[e & 0x3FF] = 1;
        }
    }
    int blank = 0;
    while (blank < 1024 && used[blank]) ++blank;
    {
        volatile Uint16 *cel = (volatile Uint16 *)P6_VDP2_CEL;
        volatile Uint16 *dst = cel + blank * 128;
        for (i = 0; i < 128; ++i) dst[i] = 0;
    }

    p6_island_build_map(islandLay, islandWShift, blank);

    /* RPT constant fields (ST-058 Fig 6.3, ROTSCROLL layout SL_DEF.H:480-512). The
     * 0x60-byte RPT is laid out as: XST(+0) YST(+4) ZST(+8) dXST(+0xC) dYST(+0x10)
     * DX(+0x14) DY(+0x18) MATA(+0x1C)..MATF(+0x34) ... KAST(+0x54) dKAST(+0x58)
     * dKAx(+0x5C). (SGL's ROTSCROLL struct order; the VRAM RPT it DMAs matches.)
     * Set the angle-INDEPENDENT fields here; the per-frame fn fills the matrix +
     * screen-start. KAst = coeff base in KAst units; dKAst = +4H/line (2-word,
     * per-line advance); dKAx = 0 (no per-dot advance). Viewpoint/center = 0. */
    rpt[0x08 / 4] = 0;                 /* ZST   */
    rpt[0x0C / 4] = 0;                 /* dXST  (per-line Xst increment; we rewrite Xst/line via the matrix) */
    rpt[0x10 / 4] = 0;                 /* dYST  */
    rpt[0x14 / 4] = 0x00010000u;       /* DX = 1.0 (16.16): kx*DX*Hcnt = coeff*Hcnt = per-pixel walk */
    rpt[0x18 / 4] = 0;                 /* DY = 0 (horizontal scan only steps X; Y per line via Yst) */
    /* KAST: integer part @ +0x54 (HIGH 16b), fractional @ +0x56 (LOW 16b)
     * (ST-058 Fig 6.3). KAst_int = 0x8000 = (KTBL & 0x7FFFF)>>2 (2-word coeff,
     * LSB unit = 4H). So the 32-bit word is (0x8000 << 16). The gate peeks the
     * 16-bit HIGH half @ +0x54 and expects 0x8000. */
    rpt[0x54 / 4] = (P6_RBG0_KAST << 16);  /* KAST int 0x8000 in high half | 0 frac */
    /* dKAST = ONE 2-word coeff entry per screen line. ROOT-CAUSE FIX (#276, MEASURED
     * via the P6_TITLE_ISLAND_STATIC bisect screenshot): the prior 0x00040000 = integer
     * part 4 = FOUR entries/line, so screen line L read coeff[L*4]. The island band's
     * coeff[168..239] was therefore consumed by screen lines 42..60 (a small dark block
     * at screen y~42, MEASURED in _isl_pixel_shot), and screen lines 168..239 (the
     * island band) read uninitialised coeff[672..956] -> transparent -> blank island.
     * Per ST-058 Table 6.3 the 2-word coeff integer LSB = 4H (4 bytes = ONE entry), and
     * the dKAst integer part = entries-per-line (DEMOCOEF BumpCoeff uses 0xa0<<16 = 160
     * entries/line for its 1-coeff-per-2-pixels row). For 1 entry/line: dKAst = 1<<16. */
    rpt[0x58 / 4] = 0x00010000u;       /* dKAST = 1 entry/line (the 2-word stride) */
    rpt[0x5C / 4] = 0;                 /* dKAx  = 0 (per-line coeff, not per-dot) */
    p6_w_title_island_kast = (int)(P6_RBG0_KAST & 0xFFFF);

    /* SGL coeff-table control (lands in the mirror, DMA'd each vblank -- the proven
     * KTCTL path). K_LINE = 1 coeff/line; K_2WORD; K_ON; K_MODE1 = the coeff supplies
     * kx ONLY (ky from the RPT ky field) so the per-line kx perspective-foreshortens the
     * HORIZONTAL while the vertical stays a clean linear sweep (see _frame). MEASURED:
     * mode 0 (kx=ky) drove the sampled texel-Y off the island -> blank. */
    slKtableRA((void *)P6_RBG0_KTBL, K_LINE | K_2WORD | K_ON | K_MODE1);
    /* RBG0 cell/plane/map: 256-color 1x1-cell chars, the A0 island cells (charno =
     * tile*8 via the PND), one PL_SIZE_2x2 plane, the B1 map, SINGLE over-mode (the
     * single island maps once -- outside the plane shows the NBG1 backdrop; the
     * Phase 1.31 tile-repeat-seam fix, slOverRA(3) per ST-058 RAOVR=3). */
    slCharRbg0(COL_TYPE_256, CHAR_SIZE_2x2);
    slPageRbg0((void *)P6_VDP2_CEL, 0, PNB_2WORD);
    slPlaneRA(PL_SIZE_2x2);
    /* THE "1 of 4 pieces" FIX (#276, user-flagged 2026-06-23): the Sonic-head
     * island is a 1024x1024 texture = a 2x2 grid of 512x512 RBG0 pages (the 4
     * quadrants the user counted). p6_island_build_map writes all 4 quadrants to
     * VRAM pages P6_RBG0_MAP + {0,1,2,3}*0x1000 (VERIFIED present: page0/1/2/3
     * landmass charno 263/271/385/393). BUT sl1MapRA() points ALL plane slots at
     * page 0 -> only the top-left quadrant renders (the 3 missing pieces).
     * FIX = sl16MapRA() with the 4 distinct page numbers. Per SGL SCROLL.TXT +
     * SGLFAQ_F.TXT 2-6: page numbers are counted in units of 0x800 from VRAM
     * START (NOT 0,4,8 -- the documented sl16MapRA failure mode), and the 16
     * entries form a 4x4 grid ABCD/EFGH/IJKL/MNOP. Our page size = 32x32 chars *
     * 4 B (2-word PND, 2x2-cell char) = 0x1000 B = 2 units, base off P6_RBG0_MAP.
     * Tile the 2x2 texture across the 4x4 grid so the FULL head renders + wraps
     * cleanly regardless of how PLSZ consumes the grid. */
    {
        unsigned long b = (P6_RBG0_MAP - 0x25E00000u) / 0x800u; /* MAPOFFSET = 128 */
        unsigned char q0 = (unsigned char)(b + 0), q1 = (unsigned char)(b + 2);
        unsigned char q2 = (unsigned char)(b + 4), q3 = (unsigned char)(b + 6);
        p6_island_map16[0]=q0; p6_island_map16[1]=q1; p6_island_map16[2]=q0; p6_island_map16[3]=q1;   /* ABCD: TL TR TL TR */
        p6_island_map16[4]=q2; p6_island_map16[5]=q3; p6_island_map16[6]=q2; p6_island_map16[7]=q3;   /* EFGH: BL BR BL BR */
        p6_island_map16[8]=q0; p6_island_map16[9]=q1; p6_island_map16[10]=q0; p6_island_map16[11]=q1; /* IJKL */
        p6_island_map16[12]=q2; p6_island_map16[13]=q3; p6_island_map16[14]=q2; p6_island_map16[15]=q3; /* MNOP */
    }
    sl16MapRA(p6_island_map16);
    slOverRA(3);
    slPriorityRbg0(2);  /* RBG0 island ABOVE NBG1 backdrop (pri 1), BELOW VDP1 sprites (7) */

    p6_w_title_island_armed = 1;
}

/* Per-frame: rebuild the per-line coefficient table + the angle-dependent RPT
 * matrix/screen-start from the live TitleBG->angle. VERBATIM the decomp
 * TitleBG_Scanline_Island math: the caller passes sine = Sin1024(-angle)>>2 and
 * cosine = Cos1024(-angle)>>2 (computed engine-side via RSDK::Sin/Cos1024 so the
 * trig table is the engine's own). For each island line i in [16,88):
 *   id = 0xA00000/(8*i); sin = sine*id; cos = cosine*id;
 *   deform.x = -cos>>7 ; deform.y = sin>>7
 *   position.x = sin - 160*deform.x - 0xA000*sine   + 0x2000000
 *   position.y = cos - 160*deform.y - 0xA000*cosine + 0x2000000
 * The coeff word per screen line = deform.x (the per-pixel X step; matches the
 * qa_title_island_rot.py contract). The RPT screen-start Xst/Yst carries the
 * line-0 origin; per-line position variation rides the coeff*matrix product. We
 * also re-arm KTCTL + re-assert RBG0ON each frame (idempotent) so a stray SGL
 * scroll call cannot drop the coeff/display. NO slScrMatSet (RotTransFlag stays 0
 * -> the vblank never DMAs over this RPT). */
void p6_vdp2_title_island_rbg0_frame(int angle, int sine, int cosine)
{
    volatile unsigned long *rpt   = (volatile unsigned long *)P6_RBG0_RPT;
    volatile long          *coeff = (volatile long *)P6_RBG0_KTBL;
    int i;

#if defined(P6_TITLE_ISLAND_STATIC)
    /* BISECT PROOF (P6_TITLE_ISLAND_STATIC): write a CONSTANT identity coeff (kx=ky=1.0,
     * 2-word = 0x00010000 = FIXED1, per DEMOCOEF MAIN.C CurvedCoeff:273) + an IDENTITY
     * matrix (A=E=1.0, B=D=0, no rotation) + Xst/Yst at the island bbox TOP-LEFT (384,384).
     * If a STILL, unrotated island APPEARS -> every VDP2 RBG0 register/bank/cell/map is
     * PROVEN correct and ONLY the rotation+coeff MATH remains (the live-rotation path
     * stuffs a raw deform_x that does NOT honor the 2-word coeff bit layout, ST-058 §6.4).
     * If STILL blank with kx=1.0+identity -> a deeper char-base/transparent-palette issue. */
    (void)angle; (void)sine; (void)cosine;
    for (i = (int)P6_ISLAND_LINE0; i < (int)(P6_ISLAND_LINE0 + P6_ISLAND_NLINES); ++i)
        coeff[i] = (long)0x00010000;            /* kx=ky=1.0 every line */
    rpt[0x00 / 4] = (unsigned long)((long)384 << 16); /* Xst = 384.0 texel (island bbox left) */
    rpt[0x04 / 4] = (unsigned long)((long)384 << 16); /* Yst = 384.0 texel (island bbox top)  */
    rpt[0x08 / 4] = 0;                          /* Zst */
    rpt[0x0C / 4] = 0;                          /* dXst */
    rpt[0x10 / 4] = 0;                          /* dYst */
    rpt[0x14 / 4] = 0x00010000u;               /* dX = 1.0 (one texel per screen dot) */
    rpt[0x18 / 4] = 0;                          /* dY */
    rpt[0x1C / 4] = 0x00010000u;               /* A = 1.0 */
    rpt[0x20 / 4] = 0;                          /* B = 0 */
    rpt[0x24 / 4] = 0;                          /* C = 0 */
    rpt[0x28 / 4] = 0;                          /* D = 0 */
    rpt[0x2C / 4] = 0x00010000u;               /* E = 1.0 */
    rpt[0x30 / 4] = 0;                          /* F = 0 */
    rpt[0x54 / 4] = (P6_RBG0_KAST << 16);
    slKtableRA((void *)P6_RBG0_KTBL, K_LINE | K_2WORD | K_ON | K_MODE0);
    {
        volatile unsigned char *rotTransFlag = (volatile unsigned char *)0x060FFCCCu;
        *rotTransFlag &= (unsigned char)~0x01u;
    }
    slBack1ColSet((void *)P6_VDP2_BAK, P6_TITLE_SKY_COL);
    slPrioritySpr0(7);
    slScrAutoDisp(RBG0ON | SPRON);
    slPriorityRbg0(2);
    p6_w_title_island_coeff0 = (int)coeff[P6_ISLAND_LINE0];
    p6_w_title_island_angle  = 0;
    return;
#endif
    /* ===================================================================== *
     * SUB1 (#276 all-angle): FULL 2D-ROTATION Mode-7 -- coeff = per-line depth
     * (Mode 0, kx=ky), rotation in the RPT matrix. This DROPS the Mode-1 kx-only
     * decomposition (which only foreshortened the HORIZONTAL and so DEGENERATED at
     * cos~=0, where deform.x->0 collapsed the band to vertical streaks/blank).
     *
     * THE EXACT DECOMPOSITION (ST-058-R2 §6.3 screen-coord formula,
     * VDP2_Manual.txt:6343-6353, all viewpoint/center 0):
     *   X = kx*(Xsp + dX_eff*Hcnt) + Xp,  dX_eff = A*DX + B*DY
     *   Y = ky*(Ysp + dY_eff*Hcnt) + Yp,  dY_eff = D*DX + E*DY
     * With DX=1.0, DY=0 -> dX_eff = A, dY_eff = D. So the PER-DOT texel step is
     * (kx*A, ky*D). The decomp (TitleBG_Scanline_Island:172-173) wants the per-dot
     * step = (deform.x, deform.y) = (-(cos*id)>>7, (sin*id)>>7), where
     * id=0xA00000/(8*i), cos=cosine*id... no: cosine=Cos1024(-angle)>>2 (1.0==256),
     * deform.x = -(cosine*id)>>7, deform.y = (sine*id)>>7.
     *
     * MATCH (EXACT, verified arithmetically for angle 0, i=16):
     *   kx = ky = 2*id   (positive per-line PERSPECTIVE DEPTH; 256>>7 = 2)
     *   A = -cos_true = -(cosine<<8)   (16.16; cosine/256 -> <<8 makes it 16.16)
     *   D =  sin_true =  (sine<<8)
     *   => per-dot-X = kx*A>>16 = (2*id)*(-(cosine<<8))>>16 = -(cosine*id)>>7 = deform.x  EXACT
     *      per-dot-Y = ky*D>>16 = (2*id)*( (sine<<8))>>16 =  (sine*id)>>7  = deform.y  EXACT
     * When cos~=0 the X-step vanishes but deform.y (driven by sin~=1) carries the
     * motion -> the island STAYS present + rotating at EVERY angle. This is the whole
     * point of restoring the matrix rotation + Mode 0.
     *
     * B,E are the rotation partners for the DOWN-SCREEN (Vcnt, via Yst+dYst) axis,
     * which is perpendicular to the across-dot axis: B = -sin_true, E = -cos_true
     * (the [[-cos -sin][sin -cos]] reflection-rotation the decomp's (deform.x basis
     * = (-cos,sin), down-screen basis = (-sin,-cos)) implies). The down-screen sweep
     * advances the START via dYst=1.0 texel/line through the matrix; Xst/Yst place the
     * line-0 origin; Mx/My carry the texel-center constant (post-kx, NOT scaled by kx
     * -- the decomp position's +0x2000000 = texel 512.0 center term, ST-058 Xp=Cx+Mx).
     * Xst/Yst/Mx/My are TUNED from MEASURED pixels (2-4 builds OK per the methodology).
     *
     * Coeff is SIGN-MAGNITUDE 2-word (ST-058 Fig 6.7): bit31=transparency, bit23=sign,
     * bits22-16 = 7-bit int, bits15-0 = frac. 2*id is POSITIVE in [~0.46,2.5] -> bit31
     * clear (visible) + fits the 7-bit int. Non-island lines = 0x80000000 (transparent
     * -> FG/sky show). dKAst=1 entry/line so screen line L reads coeff[L]. */
    for (i = 0; i < 240; ++i)
        coeff[i] = (long)0x80000000;            /* transparency bit set (non-island lines) */
    for (i = 16; i < 88; ++i) {
        int idv = 0xA00000 / (8 * i);
        long kx = (long)(2 * idv);             /* per-line perspective depth (16.16, positive) */
        if (kx > 0x007FFFFFL) kx = 0x007FFFFFL; /* clamp to 7 int + 16 frac (transp clear) */
        if (kx < 0x00001000L) kx = 0x00001000L; /* floor ~1/16 texel: never a zero-scale line */
        coeff[P6_ISLAND_LINE0 + (i - 16)] = kx; /* kx=ky for this island screen line (Mode 0) */
    }
    {
        /* True cos/sin in 16.16 (cosine/sine carry 1.0==256, so <<8 -> 1.0==0x10000). */
        const long cosF = (long)cosine << 8;      /* cos_true in 16.16 */
        const long sinF = (long)sine   << 8;      /* sin_true in 16.16 */
        /* EXACT decomp depth model (#276 all-angle, the principled fix): the decomp depth
         * is id = 0xA00000/(8*i) -- HYPERBOLIC in the line i, NOT linear. A linear dYst
         * per-line start sweep CANNOT reproduce it. Instead set dXst=dYst=0 (the texel-
         * space START is the SAME point for every line) and let kx=2*id (already the
         * coeff) provide the HYPERBOLIC scaling for BOTH the per-dot step AND the per-line
         * start (X = kx*(A*Xst+B*Yst+A*sx)+Mx -- the whole Xsp rides kx). Then:
         *   start id-part = kx*(A*Xst+B*Yst) = 2*id*(A*Xst+B*Yst)  -- hyperbolic like the
         *     decomp's sine*id / -160*deform.x terms (both ~ id).
         *   per-dot = kx*A*sx = deform.x*sx  (EXACT).
         * Xst is the texel-space start offset (TUNED so the island bbox texels 384..640
         * fill the band); Mx/My carry the angle-dependent VIEWPOINT counter-shift the
         * decomp uses to RE-CENTER the island as it rotates (position.x const term
         * -0xA000*sine + 0x2000000; -0xA000*cosine + 0x2000000 for Y). */
        /* PROVEN-BEST baseline (MEASURED 8/24 visible + frame-20 grass strip): STATIC
         * Mx=My=512, dYst=1.0, Xst=-160, Yst=-200. Every angle-dependent Mx counter-shift
         * trial made coverage WORSE (0-5/24) -- so the swing IS the correct Mode-7 motion;
         * the gap is purely that the island's visible window is contiguous (~8 frames) and
         * needs WIDENING. The witnesses below report the SCREEN-CENTER sampled texel at the
         * horizon/mid/bottom band lines so the placement can be derived from MEASURED data
         * (peek p6_w_isl_tx_* at a blank vs a visible frame) instead of blind tuning. */
        /* DECOMP-EXACT pivot (#276, DERIVED -- replaces the prior hand-TUNED Xst/Yst/Mx
         * that gave only 8/24 frames + the wrong rotation center the user flagged).
         * Matching the HW screen formula  X[sx,i] = kx[i]*(A*Xst + B*Yst + A*sx) + Mx
         * (kx[i]=2*id[i], A=-cos, B=-sin) term-by-term to the decomp
         * TitleBG_Scanline_Island position.x[i] = id*(sine+1.25*cosine) - 0xA000*sine
         * + 0x2000000 (and the Y analogue) yields, for EVERY angle, a CONSTANT texel
         * start + an ANGLE-DEPENDENT center:
         *   2*(A*Xst + B*Yst) == (sine + 1.25*cosine)  ->  -c*Xst - s*Yst == 128s + 160c
         *     ->  Xst = -160.0 , Yst = -128.0 (constants), dXst = dYst = 0
         *   Mx  = 0x2000000 - 0xA000*sine     (the decomp -0xA000*sine + 512.0 viewpoint term)
         *   My  = 0x2000000 - 0xA000*cosine
         * The prior "angle counter-shift HURT" note was because the counter-shift was
         * applied WRONG; the EXACT term is -0xA000*sin (X) / -0xA000*cos (Y) -- the decomp's
         * own per-angle re-centering, which is what makes the island spin about the right
         * pivot AND keeps the full landmass in the band (fixes "missing tiles"). */
        const long dYst = 0;                        /* depth rides kx (2*id), NOT a linear sweep */
        const long Xst  = (long)(-160) << 16;       /* decomp-exact texel start (was tuned -200) */
        const long Yst  = (long)(-128) << 16;       /* decomp-exact (was tuned -240)             */
        /* DECOMP-EXACT viewpoint center (#276 FULL-ISLAND FIX, 2026-06-23 fresh
         * context). The decomp TitleBG_Scanline_Island (TitleBG.c:174-175) is:
         *   position.x = sin - center.x*deform.x - 0xA000*sine   + 0x2000000
         *   position.y = cos - center.x*deform.y - 0xA000*cosine + 0x2000000
         * Matching the Saturn HW screen formula term-by-term (the -160/+160 deform
         * terms cancel at screen-center) yields EXACTLY:
         *   Mx = 0x2000000 - 0xA000*sine   ;  My = 0x2000000 - 0xA000*cosine
         * (0x2000000 = texel 512.0 = the head's geometric center; the island layer's
         * occupied bbox is x[384..639] y[384..639] center (511,511), MEASURED via
         * render_scene.py TileLayer 3). sim_island_decomp.py renders the Saturn
         * formula with THESE params vs the decomp scanline formula: band mean|diff|
         * = 0.00 at EVERY angle -> byte-identical, the FULL Sonic-head fills+spins.
         * The prior 448 center + halved 0x5000 My-swing were a green-centroid-gate
         * over-fit that biased sampling onto the quills (left sliver) + shrank the
         * vertical sweep -> the "island appears small / only 1/4 / one eye" symptom.
         * Restore decomp-exact (the WHOLE head, centered, per TitleBG.c). */
        const long Mx = (long)((unsigned long)P6_ISLAND_CTR_TEXEL << 16) - (long)0xA000 * sine;
        const long My = 0x01000000L - (long)0xA000 * cosine;  /* texel-256 center (head shifted into page0) + FULL 0xA000 swing */
        /* MEASUREMENT witnesses: compute the HW screen-center (sx=160) sampled texel for
         * the MID band line (kx ~ 2*id at i=42) so I can read WHERE the sampling lands.
         * texel_X = (kx*(A*Xst + B*Yst + A*160) + Mx) >> 16 (integer texel). Do the 16.16
         * multiplies the way the HW does (>>16 per multiply). cosF/sinF are 16.16. */
        {
            long idmid = 0xA00000L / (8 * 42);     /* mid-band id */
            long kxm   = 2 * idmid;                /* mid-band kx (16.16) */
            long Ax = -cosF, Bx = -sinF, Dx = sinF, Ex = -cosF;
            /* Xsp_center = A*Xst + B*Yst + A*160 ; all 16.16, products >>16 */
            long xsp = ((Ax * (Xst >> 16)) + (Bx * (Yst >> 16))) + (Ax * 160);
            long ysp = ((Dx * (Xst >> 16)) + (Ex * (Yst >> 16))) + (Dx * 160);
            long tx  = (((long long)kxm * xsp) >> 16) + Mx;
            long ty  = (((long long)kxm * ysp) >> 16) + My;
            p6_w_isl_tx = (int)(tx >> 16);         /* sampled texel-X (integer) at mid line */
            p6_w_isl_ty = (int)(ty >> 16);         /* sampled texel-Y (integer) at mid line */
        }
        /* RPT field byte-offsets (ST-058-R2 Fig 6.2/6.3 + DEMOCOEF vdp2RotParam
         * UTIL/VDP2.H:189-191 -- Fixed32 fields: Xst..f at 0x00..0x30, then Uint16
         * Px..dummy2 at 0x34..0x43, then Mx 0x44, My 0x48, kx 0x4C, ky 0x50, KAst 0x54). */
        rpt[0x00 / 4] = (unsigned long)Xst;        /* Xst (texel-space, scaled by kx) */
        rpt[0x04 / 4] = (unsigned long)Yst;        /* Yst (texel-space, scaled by kx) */
        rpt[0x08 / 4] = 0;                          /* Zst */
        rpt[0x0C / 4] = 0;                          /* dXst = 0 (depth via kx, not linear) */
        rpt[0x10 / 4] = (unsigned long)dYst;       /* dYst = 0 (depth via kx, not linear) */
        rpt[0x14 / 4] = 0x00010000u;               /* DX = 1.0 texel/dot */
        rpt[0x18 / 4] = 0;                          /* DY = 0 */
        rpt[0x1C / 4] = (unsigned long)(-cosF);    /* A = -cos  -> per-dot-X = kx*A = deform.x */
        rpt[0x20 / 4] = (unsigned long)(-sinF);    /* B = -sin */
        rpt[0x24 / 4] = 0;                          /* C = 0 */
        rpt[0x28 / 4] = (unsigned long)( sinF);    /* D =  sin  -> per-dot-Y = ky*D = deform.y */
        rpt[0x2C / 4] = (unsigned long)(-cosF);    /* E = -cos */
        rpt[0x30 / 4] = 0;                          /* F = 0 */
        rpt[0x34 / 4] = 0;                          /* Px=0, Py=0 (Uint16 pair) */
        rpt[0x38 / 4] = 0;                          /* Pz=0, dummy1 */
        rpt[0x3C / 4] = 0;                          /* Cx=0, Cy=0 */
        rpt[0x40 / 4] = 0;                          /* Cz=0, dummy2 */
        rpt[0x44 / 4] = (unsigned long)Mx;         /* Mx (post-kx X: 512 - 0xA000*sine) */
        rpt[0x48 / 4] = (unsigned long)My;         /* My (post-kx Y: 512 - 0xA000*cosine) */
        rpt[0x4C / 4] = 0x00010000u;               /* kx field (Mode 0 reads coeff, this is the RPT fallback) */
        rpt[0x50 / 4] = 0x00010000u;               /* ky field (Mode 0 reads coeff) */
        p6_w_title_island_coeff0 = (int)coeff[P6_ISLAND_LINE0];
        p6_w_title_island_kast   = (int)((cosF >> 16) & 0xFFFF); /* witness: A=-cos hi half */
    }

    /* Re-assert coeff control + KAst each frame (defensive). KAst integer in the HIGH
     * 16b (+0x54). K_MODE0 = coeff supplies BOTH kx and ky (the per-line depth); the
     * rotation lives in the RPT matrix above, so the band stays present at all angles. */
    rpt[0x54 / 4] = (P6_RBG0_KAST << 16);
    slKtableRA((void *)P6_RBG0_KTBL, K_LINE | K_2WORD | K_ON | K_MODE0);

    /* CRITICAL (MEASURED 2026-06-23): the SGL vblank _BlankIn handler (LIBSGL
     * sglI00.o:0xca-0xe8) DMAs its WRAM ROTSCROLL_A (0x060FFE1C) OVER our VRAM RPT
     * whenever BGON bit4/5 (RBG0ON) is set AND RotTransFlag (0x060FFCCC) bit0 is set
     * -- which clobbered our manual matrix/Xst (peeked Xst became SGL's 0xB0=176 +
     * an identity matrix instead of our 0x02000000 + rotation). slKtableRA/slPlaneRA/
     * etc. evidently set RotTransFlag. So we CLEAR RotTransFlag bit0 AFTER our RPT
     * writes each frame -> the vblank skips its WRAM->VRAM RPT DMA and our matrix +
     * screen-start survive. (The 144-byte register-mirror DMA that carries RPTA is a
     * SEPARATE gate -- it still runs, keeping RPTA valid at the SGL default 0x3FF00.) */
    /* (RotTransFlag clear is now the VERY LAST op -- it was here, but slScrAutoDisp +
     * slPriorityRbg0 below RE-SET it, so clearing it here left it SET at vblank on some
     * frames. Moved to the STABILITY FIX block at the end of this function.) */
    /* Display flags: RBG0 (rotating island) + NBG1 (clouds on B1) + sprites
     * (logo/Sonic). SUB2 (#276) RESTORES NBG1: ST-058-R2 :1030 "the normal scroll
     * screen can be displayed simultaneously with ONE rotation scroll screen." The
     * prior "NBG1 cannot coexist" claim was a MISREADING of :6449 -- that rule only
     * says NBG1 cannot read from a RBG0-CLAIMED bank (A0/A1/B0). NBG1 reads its
     * clouds from B1, which RAMCTL (0x1327, bits7,6=00) leaves NON-RBG0 -> B1's VRAM
     * cycle is honored -> NBG1 renders. (p6_vdp2_title_clouds_b1_config points NBG1
     * at B1; the arm copied the cloud cells + map there.) A/B knobs: P6_TITLE_NORBG0
     * = island off (sprites+clouds), P6_TITLE_NOCLOUDS = clouds off (island only). */
    slBack1ColSet((void *)P6_VDP2_BAK, P6_TITLE_SKY_COL);
    slPrioritySpr0(7);
#if defined(P6_TITLE_NORBG0)
    slScrAutoDisp(NBG1ON | SPRON);
#elif defined(P6_TITLE_NOCLOUDS)
    slScrAutoDisp(RBG0ON | SPRON);
#else
    slScrAutoDisp(RBG0ON | NBG1ON | SPRON);
#endif
    /* MEASURED 2026-06-23: the RBG0 priority register (PRIR, VDP2 0x25F80100,
     * mirror 0x060FFDC0) read back 0 = "layer not displayed" even with RBG0ON set,
     * because slScrAutoDisp / the arm-once slPriorityRbg0 did not leave it latched.
     * Re-assert the RBG0 priority EACH frame AFTER slScrAutoDisp so RBG0 composites
     * (pri 2 = above NBG1 pri 1, below VDP1 sprites pri 7). Without this the matrix +
     * coeff + RPTA are all correct but nothing draws (PRIR=0). */
    slPriorityRbg0(2);

    /* "1 of 4 pieces" FIX, per-frame half (#276, user "add the other 3 pieces"):
     * RE-ASSERT the 2x2 (1024x1024) plane + the 4-page sl16MapRA map EVERY frame.
     * slScrAutoDisp (above) re-derives RBG0's plane config and resets it toward a
     * single 512x512 page, so the arm-once sl16MapRA was being clobbered every frame
     * -> only the head's top-left quadrant (texels 384..512) sampled, the other 3
     * quadrants (512..640, in pages 1/2/3) never read. (Same per-frame re-assertion
     * the RPT/coeff need.) Kept BEFORE the RotTransFlag clear so that stays last. */
    slPlaneRA(PL_SIZE_2x2);
    sl16MapRA(p6_island_map16);
    slOverRA(3);

    /* STABILITY FIX (#276, MEASURED garbled-Sonic intermittent corruption -- user-flagged
     * "Sonic in the ring derezzes"): clear RotTransFlag bit0 as the ABSOLUTE LAST op.
     * slScrAutoDisp + slPriorityRbg0 (above) RE-SET it (same as slKtableRA), so clearing
     * it earlier left it SET at the vblank on the frames where _BlankIn lands after these
     * calls -> the SGL ISR DMAs its identity RPT over ours -> the island band/coeff
     * mis-indexes -> horizontal-stripe garbage rendered over Sonic in the screen center.
     * Clearing AFTER every SGL call this frame suppresses that DMA every frame. */
    {
        volatile unsigned char *rotTransFlag = (volatile unsigned char *)0x060FFCCCu;
        *rotTransFlag &= (unsigned char)~0x01u;
    }

    /* Read back the LIVE RPTA register (VDP2 0x25F800BC/BE) -> the RED->GREEN proof. */
    {
        volatile unsigned short *rptau = (volatile unsigned short *)0x25F800BCu;
        volatile unsigned short *rptal = (volatile unsigned short *)0x25F800BEu;
        unsigned int rpta = (((unsigned int)(*rptau & 0x7) << 16)
                             | (unsigned int)(*rptal & 0xFFFE)) << 1;
        p6_w_title_island_rpta = (int)rpta;
    }
    p6_w_title_island_angle = angle & 0x3FF;
}
#endif /* P6_FRONTEND_TITLE */

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
