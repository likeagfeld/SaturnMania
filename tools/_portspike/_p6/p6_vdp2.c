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
    slPriorityNbg1(7);
    slBack1ColSet((void *)P6_VDP2_BAK, 0x8000); /* black backdrop = model's */
    slScrAutoDisp(NBG1ON | SPRON);
}

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

void p6_vdp2_present_ghz_camera(int layer, int scroll_x, int scroll_y,
                                const unsigned short *pal565,
                                unsigned int *out_pndhash, int *out_nblank)
{
    volatile Uint16 *cel  = (volatile Uint16 *)P6_VDP2_CEL;
    volatile Uint16 *map  = (volatile Uint16 *)P6_VDP2_MAP;
    volatile Uint16 *cram = (volatile Uint16 *)P6_VDP2_CRAM;
    static unsigned char used[1024]; /* file-static would collide with the
                                      * Title present's local; own copy */
    int t, c, x, y;

    SaturnLayout_Bind(0, layer); /* slot 0 = the W16 walk window */

    /* 1) Blank char = smallest tile index the 64x64 rect never references
     *    (same rule + same rect as the gate's offline model). */
    for (t = 0; t < 1024; ++t) used[t] = 0;
    for (y = 0; y < 64; ++y)
        for (x = 0; x < 64; ++x) {
            unsigned short e = SaturnLayout_GetTile(0, x, y);
            if (e != 0xFFFF)
                used[e & 0x3FF] = 1;
        }
    {
        int blank = 0;
        while (blank < 1024 && used[blank]) ++blank;
        volatile Uint16 *dst = cel + blank * 128;
        for (c = 0; c < 128; ++c) dst[c] = 0;

        /* 2) Map: identity 64x64 rect of 2-word PNDs (p6_vdp2.c:105-119
         *    formula -- fy bit11->31, fx bit10->30, charno = tile*8). */
        for (y = 0; y < 64; ++y) {
            for (x = 0; x < 64; ++x) {
                unsigned short e = SaturnLayout_GetTile(0, x, y);
                unsigned long pnd;
                if (e == 0xFFFF)
                    pnd = (unsigned long)blank * 8u;
                else
                    pnd = ((unsigned long)(e & 0x800) << 20)
                        | ((unsigned long)(e & 0x400) << 20)
                        | ((unsigned long)(e & 0x3FF) * 8u);
                int page = ((y >> 5) << 1) + (x >> 5);
                volatile Uint16 *p = map + page * 2048 + (((y & 31) << 5) + (x & 31)) * 2;
                p[0] = (Uint16)(pnd >> 16);
                p[1] = (Uint16)(pnd & 0xFFFF);
            }
        }
    }

    /* 3) CRAM bank 0 from the GHZ active palette (post-LoadSceneAssets
     *    fullPalette[0]; engine RGB565 -> Saturn BGR555, MSB set). */
    for (c = 0; c < 256; ++c) {
        unsigned short v = pal565[c];
        unsigned short r5 = (v >> 11) & 0x1F;
        unsigned short g5 = ((v >> 5) & 0x3F) >> 1;
        unsigned short b5 = v & 0x1F;
        cram[c] = (Uint16)(0x8000 | (b5 << 10) | (g5 << 5) | r5);
    }

    /* 4) Witness data: read-back hash over the map (the gate's S3 model is
     *    the same big-endian byte stream) + visible-window non-empty count
     *    (S4; window = scroll + 320x224, the CORE_DEFS screen). */
    {
        unsigned int h = 5381u;
        for (t = 0; t < 4096 * 2; ++t) { /* 8,192 words = 16,384 B */
            Uint16 w = map[t];
            h = ((h << 5) + h) ^ (unsigned int)(w >> 8);
            h = ((h << 5) + h) ^ (unsigned int)(w & 0xFF);
        }
        *out_pndhash = h;

        int n = 0;
        int tx0 = scroll_x >> 4, tx1 = (scroll_x + 320 - 1) >> 4;
        int ty0 = scroll_y >> 4, ty1 = (scroll_y + 224 - 1) >> 4;
        for (y = ty0; y <= ty1; ++y)
            for (x = tx0; x <= tx1; ++x)
                if (SaturnLayout_GetTile(0, x, y) != 0xFFFF)
                    ++n;
        *out_nblank = n;
    }

    /* 5) NBG1 config + camera-anchored scroll + display (same SGL sequence
     *    as the proven Title present part 4). */
    slCharNbg1(COL_TYPE_256, CHAR_SIZE_2x2);
    slPageNbg1((void *)P6_VDP2_CEL, 0, PNB_2WORD);
    slPlaneNbg1(PL_SIZE_2x2);
    slMapNbg1((void *)P6_VDP2_MAP, (void *)P6_VDP2_MAP,
              (void *)P6_VDP2_MAP, (void *)P6_VDP2_MAP);
    slScrPosNbg1(toFIXED(scroll_x), toFIXED(scroll_y));
    slPriorityNbg1(7);
    slBack1ColSet((void *)P6_VDP2_BAK, 0x8000);
    slScrAutoDisp(NBG1ON | SPRON);
}
