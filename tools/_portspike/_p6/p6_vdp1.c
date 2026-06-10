/* ============================================================================
 * p6_vdp1.c -- P6.5b2 (Task #208): draw an ENGINE-loaded sprite frame on VDP1.
 *
 * jo-side half of the sprite proof (the engine half lives in p6_io_main.cpp's
 * P6_SCENE_TEST block; the two TUs cannot share headers -- jo.h's SGL C decls
 * clash with the engine's C++ namespace, same split as p6_gfs.c). The engine
 * loads Global/Ring.bin + Global/Items.gif from the ORIGINAL Data.rsdk with
 * its own LoadSpriteAnimation/LoadSpriteSheet/ImageGIF code; this TU only
 * takes the resulting frame RECT of indexed pixels and puts it on VDP1
 * through jo's PROVEN 8bpp sprite path (jo_sprite_add_8bits_image ->
 * __internal_jo_sprite_add(data, w, h, COL_256), sprites.c:237-247).
 *
 * Palette: COL_256 sprites read CRAM through jo's palette index. The engine's
 * stage palette already sits in CRAM bank 0 (NBG1's bank, written by
 * p6_vdp2.c); sprites get their OWN copy in bank 1 so jo's palette indexing
 * (1-based per the jo-cram-off-by-one rule for jo_create_palette_from; direct
 * bank writes used here) cannot disturb the proven NBG1 bank. The T2 pixel
 * gate arbitrates color correctness end-to-end.
 *
 * Width rule: VDP1/jo sprite widths MUST be a multiple of 8 (memory rule
 * jo-sgl-sprite-width-mult8-shear; sprites.c:212 truncates char size to
 * width & 0x1F8). Ring "Normal Ring" frame 0 is 16x16 -- already compliant;
 * the copy loop pads the static buffer width up to mult-8 regardless so any
 * frame rect stays safe.
 * ========================================================================== */
#include <jo/jo.h>

#define P6_SPR_MAXW 32
#define P6_SPR_MAXH 32

static unsigned char s_spr_buf[P6_SPR_MAXW * P6_SPR_MAXH];
static int s_spr_id = -1;
static int s_spr_w, s_spr_h;

/* One-time upload: copy the frame rect (sprX,sprY,w,h) out of the engine's
 * 8bpp sheet surface (lineSize = sheet width in bytes) into the padded
 * buffer, then hand it to jo's 8bpp VDP1 allocator. Returns the jo sprite id
 * (or -1). Also mirrors the 256-color stage palette into CRAM bank 1 for the
 * sprite (src = engine RGB565, same conversion as p6_vdp2.c CRAM bank 0). */
int p6_vdp1_ring_init(const unsigned char *sheetPixels, int sheetWidth,
                      int sprX, int sprY, int w, int h,
                      const unsigned short *pal565)
{
    volatile Uint16 *cram1 = (volatile Uint16 *)(0x25F00000 + 0x200);
    int x, y;
    int padw = (w + 7) & ~7;

    if (padw > P6_SPR_MAXW || h > P6_SPR_MAXH)
        return -1;

    for (y = 0; y < h; ++y) {
        const unsigned char *src = sheetPixels + (sprY + y) * sheetWidth + sprX;
        unsigned char *dst       = s_spr_buf + y * padw;
        for (x = 0; x < w; ++x) dst[x] = src[x];
        for (; x < padw; ++x) dst[x] = 0; /* transparent right-pad */
    }

    for (x = 0; x < 256; ++x) {
        unsigned short v  = pal565[x];
        unsigned short r5 = (v >> 11) & 0x1F;
        unsigned short g5 = ((v >> 5) & 0x3F) >> 1;
        unsigned short b5 = v & 0x1F;
        cram1[x] = (Uint16)(0x8000 | (b5 << 10) | (g5 << 5) | r5);
    }

    {
        jo_img_8bits img;
        img.width  = padw;
        img.height = h;
        img.data   = s_spr_buf;
        s_spr_id   = jo_sprite_add_8bits_image(&img);
        s_spr_w    = padw;
        s_spr_h    = h;
    }
    return s_spr_id;
}

/* Per-tick draw at a fixed SCREEN position (jo coords are screen-centered).
 * Called from the engine-side tick after ProcessAnimation. */
void p6_vdp1_ring_draw(int screenX, int screenY)
{
    if (s_spr_id < 0)
        return;
    jo_sprite_set_palette(1);
    jo_sprite_draw3D(s_spr_id, screenX - JO_TV_WIDTH_2, screenY - JO_TV_HEIGHT_2, 450);
}
