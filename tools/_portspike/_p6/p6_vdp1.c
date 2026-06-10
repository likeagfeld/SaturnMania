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

#define P6_SPR_MAXW     32
#define P6_SPR_MAXH     32
#define P6_VDP1_NSLOTS  24 /* >= 16 distinct Ring anim-0 rects, small bound */

/* qa_p6_draw.py D5 witness: number of distinct rects resident on VDP1.
 * __attribute__((used)) defeats LTO name-collapse so the gate can locate it
 * in game.map (entity-atlas-loader-pattern memory rule). */
__attribute__((used)) int p6_w_vdp1_slots = 0;
/* P6.7a diagnostic: the LAST-uploaded cache key, packed
 * (sx<<20)|(sy<<12)|(w<<6)|h -- identifies an unexpected 17th rect. */
__attribute__((used)) int p6_w_vdp1_lastkey = 0;

static const unsigned char *s_sheet_px = 0; /* bound engine surface (indexed 8bpp) */
static int                  s_sheet_w  = 0;

static struct {
    int sx, sy, w, h; /* cache key: sheet rect */
    int jo_id;        /* jo sprite id of the uploaded rect */
} s_slots[P6_VDP1_NSLOTS];

static unsigned char s_stage[P6_SPR_MAXW * P6_SPR_MAXH]; /* reusable copy buffer */

/* Bind the engine's sheet surface + mirror the 256-color stage palette into
 * CRAM bank 1 (src = engine RGB565, same conversion as p6_vdp2.c bank 0).
 * Called once from the run body after LoadSpriteSheet succeeds. */
int p6_vdp1_sheet_bind(const unsigned char *sheetPixels, int sheetWidth,
                       const unsigned short *pal565)
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

    s_sheet_px      = sheetPixels;
    s_sheet_w       = sheetWidth;
    p6_w_vdp1_slots = 0;
    for (i = 0; i < P6_VDP1_NSLOTS; ++i)
        s_slots[i].jo_id = -1;
    return 0;
}

/* Find (or upload) the VDP1 residency slot for a sheet rect. */
static int p6_slot_for(int sx, int sy, int w, int h)
{
    int i, x, y, padw;

    for (i = 0; i < p6_w_vdp1_slots; ++i) {
        if (s_slots[i].sx == sx && s_slots[i].sy == sy &&
            s_slots[i].w == w && s_slots[i].h == h)
            return i;
    }
    if (p6_w_vdp1_slots >= P6_VDP1_NSLOTS)
        return -1;

    padw = (w + 7) & ~7;
    if (padw > P6_SPR_MAXW || h > P6_SPR_MAXH)
        return -1;

    for (y = 0; y < h; ++y) {
        const unsigned char *src = s_sheet_px + (sy + y) * s_sheet_w + sx;
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
        i              = p6_w_vdp1_slots++;
        s_slots[i].sx  = sx;
        s_slots[i].sy  = sy;
        s_slots[i].w   = w;
        s_slots[i].h   = h;
        s_slots[i].jo_id = id;
        p6_w_vdp1_lastkey = (sx << 20) | (sy << 12) | (w << 6) | h;
        return i;
    }
}

/* Draw a sheet rect with its TOP-LEFT at engine screen px (x,y) -- the
 * coordinate DrawSpriteFlipped receives (Drawing.cpp:2785: pos + pivot).
 * jo_sprite_draw3D positions the sprite CENTER in screen-centered coords;
 * centering on the PADDED width keeps the content at [x, x+w). */
void p6_vdp1_blit(int x, int y, int w, int h, int sx, int sy)
{
    int slot, padw;

    if (!s_sheet_px)
        return;
    slot = p6_slot_for(sx, sy, w, h);
    if (slot < 0)
        return;

    padw = (w + 7) & ~7;
    jo_sprite_set_palette(1);
    jo_sprite_draw3D(s_slots[slot].jo_id,
                     x + padw / 2 - JO_TV_WIDTH_2,
                     y + h / 2 - JO_TV_HEIGHT_2, 450);
}
