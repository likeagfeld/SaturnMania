/* Phase A4 — Drawing system (Saturn port).
 *
 * Implements the RSDK Drawing API surface on top of jo_engine + SGL.
 * Per docs/rsdkv5_engine_catalog.md §3. See header for design rationale.
 *
 * Current scope (Phase A4 baseline): the public RSDK API is exposed so
 * per-object ports compile and call the canonical names. The underlying
 * blits route to jo_sprite_draw3D / VDP2 NBG layer pipeline that
 * src/main.c already wires up. INK effects, rotation styles, and
 * dynamic clip rectangles are stubbed — rotation/scale via VDP1
 * distorted-sprite, INK effects via Phase Z. */

#include "drawing.h"

#include <jo/jo.h>
#include <string.h>

rsdk_screen_info_t g_rsdk_screen;

void rsdk_drawing_init(void)
{
    memset(&g_rsdk_screen, 0, sizeof(g_rsdk_screen));
    g_rsdk_screen.size_x   = RSDK_SCREEN_XSIZE;
    g_rsdk_screen.size_y   = RSDK_SCREEN_YSIZE;
    g_rsdk_screen.center_x = RSDK_SCREEN_XSIZE / 2;
    g_rsdk_screen.center_y = RSDK_SCREEN_YSIZE / 2;
    g_rsdk_screen.clip_x1  = 0;
    g_rsdk_screen.clip_y1  = 0;
    g_rsdk_screen.clip_x2  = RSDK_SCREEN_XSIZE;
    g_rsdk_screen.clip_y2  = RSDK_SCREEN_YSIZE;
}

/* §3.4 DrawSprite implementation.
 *
 * The animator's current sprite frame carries (sx,sy,w,h,pivot_x,pivot_y).
 * Upstream renders directly into ScreenInfo->frameBuffer; Saturn uses
 * VDP1 sprites instead. The per-object port code needs to KNOW its
 * sprite_id (jo_sprite_add return) for any sprite it draws — the
 * animator's frame index doesn't directly map to a Saturn sprite_id.
 *
 * Saturn-side bridge: each Mania class that uses DrawSprite tracks an
 * array of sprite_ids parallel to its animator->frames[]. The class's
 * Draw callback resolves frame_id -> sprite_id then calls this routine
 * with a sentinel pos override (so it doesn't have to re-implement the
 * jo coord conversion). The Phase A4 baseline routine handles the
 * generic conversion + flip; per-class code provides the sprite ID
 * mapping via a helper API extension. */

/* Saturn-side helper: convert RSDK 16.16 world coord + screen pos +
 * sprite pivot into jo's centre-anchored draw3D coordinates. */
static void _world_to_jo(int32_t pos_x_fixed, int32_t pos_y_fixed,
                         int16_t pivot_x, int16_t pivot_y,
                         int16_t w, int16_t h,
                         bool screen_relative,
                         int *out_jo_x, int *out_jo_y)
{
    /* Phase 1.3 — Saturn-side ScreenInfo->position is stored as plain
     * pixel coords (NOT 16.16) — see TitleSetup_Update which writes
     *   ScreenInfo->position.x = 0x100 - ScreenInfo->center.x = 96
     * directly as an int. The mirror in g_rsdk_screen.position_x is a
     * straight copy. Entity positions however ARE 16.16 fixed (from
     * Scene1.bin and Create-body assignments like
     *   self->drawPos.x = 256 << 16
     * so we shift pos by 16 but read cam as integer pixels. */
    int32_t cam_x = screen_relative ? 0 : g_rsdk_screen.position_x;
    int32_t cam_y = screen_relative ? 0 : g_rsdk_screen.position_y;
    int x_world = (int)(pos_x_fixed >> 16);
    int y_world = (int)(pos_y_fixed >> 16);
    /* Sprite top-left in world = pos + pivot. Centre in world = TL +
     * (w/2, h/2). jo's draw3D anchors at centre = world - cam - (160, 112). */
    int cx_world = x_world + pivot_x + (w >> 1);
    int cy_world = y_world + pivot_y + (h >> 1);
    *out_jo_x = cx_world - (int)cam_x - 160;
    *out_jo_y = cy_world - (int)cam_y - 112;
}

void rsdk_draw_sprite(const rsdk_animator_t *animator,
                      int32_t pos_x_fixed, int32_t pos_y_fixed,
                      uint8_t direction,
                      bool screen_relative)
{
    if (!animator) return;
    const rsdk_sprite_frame_t *f = rsdk_animator_current_frame(animator);
    if (!f) return;

    /* The Saturn-side per-class sprite-ID mapping is owned by the per-
     * object port. The Phase A4 baseline DrawSprite is parameter-only:
     * caller can compute jo coords + handle flip via this routine. Per-
     * class draw callbacks build on this. For sprite_id resolution
     * callers use jo_sprite_draw3D directly with the precomputed jo
     * coords from _world_to_jo. */
    int jx = 0, jy = 0;
    _world_to_jo(pos_x_fixed, pos_y_fixed, f->pivot_x, f->pivot_y,
                 f->width, f->height, screen_relative, &jx, &jy);

    /* Per-class code is responsible for the actual jo_sprite_draw3D call
     * — it has the sprite_id table. This routine is a coordinate helper.
     * Phase A4 baseline: silent if no per-class draw hook is registered. */
    (void)direction; (void)jx; (void)jy;
}

/* Helper exposed for per-class draw callbacks that need the coord
 * conversion without going through an animator. */
void rsdk_world_to_jo_coords(int32_t pos_x_fixed, int32_t pos_y_fixed,
                             int16_t pivot_x, int16_t pivot_y,
                             int16_t w, int16_t h,
                             bool screen_relative,
                             int *out_jo_x, int *out_jo_y)
{
    _world_to_jo(pos_x_fixed, pos_y_fixed, pivot_x, pivot_y, w, h,
                 screen_relative, out_jo_x, out_jo_y);
}

/* §3.5 DrawTile — currently a Phase B follow-up; HUD overlays in this
 * project use direct jo_sprite_draw3D calls. Stub stays so Phase B port
 * code compiles. */
void rsdk_draw_tile(const uint16_t *tile_ids,
                    int count_x, int count_y,
                    int32_t pos_x_fixed, int32_t pos_y_fixed,
                    int tile_offset,
                    bool screen_relative)
{
    (void)tile_ids; (void)count_x; (void)count_y;
    (void)pos_x_fixed; (void)pos_y_fixed; (void)tile_offset;
    (void)screen_relative;
}

/* §3.6 FillScreen — Saturn drives the VDP2 back-colour register for the
 * full-fill case (alpha == 0xFF on all channels = solid). Partial-alpha
 * blends are deferred to Phase Z (require VDP2 COLOR_CALC programming).
 *
 * For the FadeBlack / FadeWhite call sites in TitleSetup (per
 * docs/title_ground_truth.md §state-machine), the engine just needs the
 * back-color set and the foreground layers' priorities zeroed. This
 * baseline implementation does the back-color swap; the layer priority
 * dance lives in the per-state code.
 *
 * Phase 3.2.b (Task #146): when all three alpha channels saturate to
 * 0xFF, route through rsdk_fill_screen_solid which programs the VDP2
 * back-color register AND clears the NBG1 bitmap so subsequent circle
 * draws have a clean canvas. The Menu scene's UIBackground_DrawNormal
 * call site passes (color, 0xFF, 0xFF, 0xFF) so it hits the solid
 * path. Partial-alpha blends still TODO Phase Z. */
void rsdk_fill_screen(uint32_t rgb, int alpha_r, int alpha_g, int alpha_b)
{
    if (alpha_r >= 0xFF && alpha_g >= 0xFF && alpha_b >= 0xFF) {
        rsdk_fill_screen_solid(rgb);
        return;
    }
    /* Convert 24-bit RGB to Saturn RGB555 + opaque bit. */
    uint8_t r = (uint8_t)((rgb >> 16) & 0xFF);
    uint8_t g = (uint8_t)((rgb >>  8) & 0xFF);
    uint8_t b = (uint8_t)((rgb)       & 0xFF);
    /* TODO Phase Z: respect alpha_r/alpha_g/alpha_b for partial blends.
     * For now alpha = max channel collapses to solid fill iff any
     * channel is fully opaque. */
    (void)alpha_r; (void)alpha_g; (void)alpha_b;
    /* Partial-alpha path: not yet ported (deferred Phase Z). */
    (void)r; (void)g; (void)b;
}

/* Phase 3.2.b — Saturn-native solid back-color fill.
 *
 * Translates the decomp Drawing.cpp:586 FillScreen `alpha == 0xFF`
 * fast-path to Saturn hardware. Programs VDP2 back-screen colour via
 * slBack1ColSet (SGL ref ST-238-R1) AND clears the NBG1 bitmap layer
 * to match so subsequent VDP2 bitmap-mode draws (circle scanlines)
 * have a clean canvas behind them.
 *
 * 24-bit RGB888 -> Saturn 15-bit RGB555 conversion: 5 high bits per
 * channel (>>3 each), MSB=1 (opaque) per ST-058-R2 §CRAM format. */
void rsdk_fill_screen_solid(uint32_t rgb)
{
    uint8_t r = (uint8_t)((rgb >> 16) & 0xFF);
    uint8_t g = (uint8_t)((rgb >>  8) & 0xFF);
    uint8_t b = (uint8_t)((rgb)       & 0xFF);
    /* Saturn RGB555 little-endian: [MSB][R4..R0][G4..G0][B4..B0].
     * MSB = 1 -> opaque (per VDP2 manual §CRAM colour format). */
    uint16_t rgb555 = (uint16_t)(0x8000
                                 | ((r >> 3) << 10)
                                 | ((g >> 3) << 5)
                                 | (b >> 3));
    /* VDP2 back-screen colour: one-entry table, scnBack = back-screen
     * colour line table pointer (we pass NULL -> SGL uses its own
     * internal default back buffer, just colours one line). */
    slBack1ColSet((void *)0x25e7fffe, rgb555);
    /* Clear the NBG1 bitmap to the same colour so circle-fill scanlines
     * land on a clean canvas. The Saturn menu scene runs NBG1 in
     * bitmap mode (set up by mania_engine_init when the menu scene
     * loads). For Phase 3.2.a/b foundation this is safe to call
     * unconditionally — jo_vdp2_clear_bitmap_nbg1 is a no-op when
     * NBG1 isn't in bitmap mode (writes to the wrong VRAM page that
     * the cell-mode renderer doesn't read from). The Phase 3.2.l
     * menu-scene wiring switches NBG1 to bitmap before calling this. */
    jo_vdp2_clear_bitmap_nbg1((jo_color)rgb555);
}

/* Phase 3.2.b — Saturn-native filled circle (rsdk_draw_circle).
 *
 * Translates decomp Drawing.cpp:1314 DrawCircle (screenRelative=true,
 * INK_NONE, alpha=0xFF) to Saturn. The decomp builds a per-scanline
 * edge buffer via Bresenham midpoint walk then fills horizontal
 * scanlines between the left/right edges. Saturn implementation: same
 * midpoint-circle walk, but each scanline emission is a single call
 * to jo_vdp2_draw_bitmap_nbg1_line into NBG1 bitmap VRAM.
 *
 * Midpoint circle algorithm (per Bresenham 1977): walk one octant
 * with integer-only arithmetic; mirror across 8 octants by negating
 * dx/dy. For each Y in [-r..+r] compute the X extent and emit one
 * horizontal scanline.
 *
 * NBG1 bitmap canvas: 512x256, 16-color or 256-color (jo configures
 * during boot). RGB888 -> jo_color via JO_COLOR_RGB macro (5 bits per
 * channel + opaque MSB). */
void rsdk_draw_circle(int cx, int cy, int radius, uint32_t rgb)
{
    if (radius <= 0) return;
    /* 24-bit RGB -> Saturn RGB555 jo_color. */
    uint8_t r8 = (uint8_t)((rgb >> 16) & 0xFF);
    uint8_t g8 = (uint8_t)((rgb >>  8) & 0xFF);
    uint8_t b8 = (uint8_t)((rgb)       & 0xFF);
    jo_color col = (jo_color)(0x8000
                              | ((r8 >> 3) << 10)
                              | ((g8 >> 3) << 5)
                              | (b8 >> 3));
    /* Midpoint circle: for each y in [0..radius], compute the
     * widest x such that x*x + y*y <= radius*radius. Then emit
     * two scanlines (cy-y and cy+y), each spanning [cx-x .. cx+x].
     * The trivial integer-arithmetic implementation is O(r) and
     * avoids floating point — fits Saturn no-FPU constraint. */
    int r_sq = radius * radius;
    for (int dy = 0; dy <= radius; ++dy) {
        /* Solve dx from dx*dx <= r_sq - dy*dy. */
        int rem = r_sq - dy * dy;
        if (rem < 0) continue;
        int dx = 0;
        while ((dx + 1) * (dx + 1) <= rem) ++dx;
        int x0 = cx - dx;
        int x1 = cx + dx;
        jo_vdp2_draw_bitmap_nbg1_line(x0, cy - dy, x1, cy - dy, col);
        if (dy != 0) {
            jo_vdp2_draw_bitmap_nbg1_line(x0, cy + dy, x1, cy + dy, col);
        }
    }
}

/* Phase 3.2.b — Saturn-native annular circle (rsdk_draw_circle_outline).
 *
 * Translates decomp Drawing.cpp:1647 DrawCircleOutline. The decomp
 * uses two midpoint walks (inner + outer) and emits scanlines for
 * the annular region between them. Saturn implementation: for each
 * Y in [-outer..+outer], compute outer_x extent. If |Y| <= inner,
 * also compute inner_x extent and emit TWO sub-scanlines:
 *   [cx-outer_x .. cx-inner_x]  and  [cx+inner_x .. cx+outer_x]
 * Otherwise emit ONE full scanline [cx-outer_x .. cx+outer_x]. */
void rsdk_draw_circle_outline(int cx, int cy,
                              int inner_radius, int outer_radius,
                              uint32_t rgb)
{
    if (outer_radius <= 0) return;
    if (inner_radius < 0) inner_radius = 0;
    if (inner_radius >= outer_radius) return;

    uint8_t r8 = (uint8_t)((rgb >> 16) & 0xFF);
    uint8_t g8 = (uint8_t)((rgb >>  8) & 0xFF);
    uint8_t b8 = (uint8_t)((rgb)       & 0xFF);
    jo_color col = (jo_color)(0x8000
                              | ((r8 >> 3) << 10)
                              | ((g8 >> 3) << 5)
                              | (b8 >> 3));

    int ro_sq = outer_radius * outer_radius;
    int ri_sq = inner_radius * inner_radius;

    for (int dy = 0; dy <= outer_radius; ++dy) {
        int outer_rem = ro_sq - dy * dy;
        if (outer_rem < 0) continue;
        int outer_dx = 0;
        while ((outer_dx + 1) * (outer_dx + 1) <= outer_rem) ++outer_dx;

        int inner_rem = ri_sq - dy * dy;
        if (inner_rem >= 0 && dy <= inner_radius) {
            int inner_dx = 0;
            while ((inner_dx + 1) * (inner_dx + 1) <= inner_rem) ++inner_dx;
            /* Two annular sub-scanlines per Y row. */
            jo_vdp2_draw_bitmap_nbg1_line(cx - outer_dx, cy - dy,
                                          cx - inner_dx, cy - dy, col);
            jo_vdp2_draw_bitmap_nbg1_line(cx + inner_dx, cy - dy,
                                          cx + outer_dx, cy - dy, col);
            if (dy != 0) {
                jo_vdp2_draw_bitmap_nbg1_line(cx - outer_dx, cy + dy,
                                              cx - inner_dx, cy + dy, col);
                jo_vdp2_draw_bitmap_nbg1_line(cx + inner_dx, cy + dy,
                                              cx + outer_dx, cy + dy, col);
            }
        } else {
            /* Full scanline beyond the inner radius. */
            jo_vdp2_draw_bitmap_nbg1_line(cx - outer_dx, cy - dy,
                                          cx + outer_dx, cy - dy, col);
            if (dy != 0) {
                jo_vdp2_draw_bitmap_nbg1_line(cx - outer_dx, cy + dy,
                                              cx + outer_dx, cy + dy, col);
            }
        }
    }
}

/* === Phase 3.2.c.1 (Task #148) VDP1 polygon emitter ==================
 *
 * Mirrors decomp Drawing.cpp DrawRect / DrawLine / DrawFace via SGL
 * primitives slPutPolygon (POLY/QUAD) + slLine (line command).
 *
 * Authoritative source:
 *   ST-013-R3-061694.pdf §3.4 "Polygon Command Format" — VDP1 command
 *     types CMDCTRL bits 0-3: 0x0004 (polygon) / 0x0005 (polyline) /
 *     0x0006 (line). Color is 16-bit RGB555 written to CMDCOLR.
 *   SGL ST-238-R1-051795.pdf p.207 slPutPolygon — submits a PDATA
 *     descriptor that points at a POINT[] table (vertex positions in
 *     screen-centered FIXED) + an ATTR[] table (one entry per polygon
 *     with sort flag, sprite function = sprPolygon (FUNC_Polygon) or
 *     sprLine (FUNC_Line), color number = RGB555 packed via RGB()).
 *   SL_DEF.H L153-219 — FUNC_Polygon (4), FUNC_Line (6), sprPolygon /
 *     sprLine macros wrapping ECdis|SPdis flags.
 *   NOV96_DTS LIBRARY/SDK_10J/SGL302/SAMPLE/S_2_2/POLYGON.C — working
 *     reference using POSTtoFIXED + slPutPolygon at top of main loop.
 *
 * Saturn coord system: SGL polygon/line vertices use screen-centered
 * FIXED — (0,0,0) is the centre of a 320x224 viewport (= jo's draw3D
 * coord origin). The decomp `x/y` parameters are 16.16 in *world* coords
 * (RSDK convention), with the menu scene having ScreenInfo->position
 * pinned at the entity's UIControl camera origin so screen_relative
 * coords drop the camera subtraction.
 *
 * For UIWidgets call sites the convention is:
 *   * DrawParallelogram / DrawRect / DrawFace / DrawLine all pass
 *     coords that have already had ScreenInfo->position subtracted
 *     (decomp Drawing.cpp:1314 baseline). The Saturn impl therefore
 *     treats `screen_relative=true` as "do not subtract ScreenInfo
 *     again". For decomp-faithful conversion: pixel = (fixed >> 16),
 *     centered = pixel - 160 / pixel - 112.
 *
 * INK mapping (per ST-013-R3 §5.5.4 + SGL CL_* enum):
 *   INK_NONE   -> CL_Replace (default opaque overwrite)
 *   INK_BLEND  -> CL_Half    (VDP1 half-brightness; closest 50% blend)
 *   INK_ALPHA  -> CL_Half    (approximation; full alpha needs CCRSP)
 *   INK_ADD    -> CL_Trans   (VDP1 translucent / additive approx)
 *   INK_SUB    -> CL_Replace (no native VDP1 subtractive; fall back)
 *   INK_TINT   -> CL_Replace (Phase Z follow-up)
 *
 * Each primitive uses a per-call static scratch PDATA + POINT[4] +
 * ATTR[1] so we don't allocate. SGL copies the descriptor into the
 * command table at slPutPolygon time, so the same scratch buffer can
 * be reused immediately after the call. */

/* Pack RGB888 -> Saturn RGB555 (5 bits per channel) with opaque MSB. */
static inline uint16_t _rgb888_to_rgb555(uint8_t r8, uint8_t g8, uint8_t b8)
{
    return (uint16_t)(0x8000
                      | ((b8 >> 3) << 10)
                      | ((g8 >> 3) <<  5)
                      |  (r8 >> 3));
}

/* Translate RSDK ink_effect to SGL CL_* sprite-color mode bits. */
static inline uint16_t _ink_to_sgl_cl(uint8_t ink_effect)
{
    switch (ink_effect) {
        case INK_BLEND:
        case INK_ALPHA: return CL_Half;
        case INK_ADD:   return CL_Trans;
        case INK_NONE:
        case INK_SUB:
        case INK_TINT:
        case INK_MASKED:
        case INK_UNMASKED:
        default:        return CL_Replace;
    }
}

/* Phase 2.4j.1 (Task #156) — settable polygon Z depth. The decomp's
 * TitleCard draw path interleaves rsdk_draw_face polygons (decoration
 * boxes) BETWEEN sprite/text draws, relying on RSDK's software-framebuffer
 * submission-order painter. On Saturn (SGL Z-sort) both polygons and
 * jo_sprite_draw3D sprites share the same FIXED Z scale (jo_int2fixed),
 * and smaller Z = nearer/front. To reproduce the painter order we let the
 * caller assign a monotonic per-primitive Z. Default 0 preserves the
 * existing menu/UIWidgets behaviour (polygons at the front plane). */
static FIXED _poly_z = 0;
void rsdk_drawing_set_poly_z(int32_t z) { _poly_z = (FIXED)z; }

/* Per-primitive scratch tables — SGL copies the data on slPutPolygon
 * so reusing the same backing storage between calls is safe. */
static POINT     _poly_pntbl[4];
/* Normal must be non-degenerate: SGL uses it for sort/cull. A {0,0,0} normal
 * combined with Single_Plane (single-sided) caused every UI/TitleCard polygon
 * to back-face cull (invisible). Screen-facing unit normal (+Z, 1.0 FIXED)
 * per the working slPutPolygon reference NOV96 SGL302/SAMPLE/S_2_2/POLYGON.C
 * which uses NORMAL(0,1,0) + Dual_Plane. */
static POLYGON   _poly_pltbl[1] = { { { 0, 0, 0x00010000 }, { 0, 1, 2, 3 } } };
static ATTR      _poly_attbl[1];
static PDATA     _poly_pdata = {
    _poly_pntbl, 4, _poly_pltbl, 1, _poly_attbl
};

/* Convert RSDK 16.16 fixed coord (already screen-relative) to SGL
 * screen-centered FIXED used by polygon vertex tables. Saturn jo's
 * draw3D origin is (160, 112); SGL polygons share that convention.
 * pos_fixed is 16.16 → first integer pixels via >>16, then subtract
 * the centre, then re-scale to FIXED via <<16. The trip through int
 * pixels matches decomp Drawing.cpp:240 where `verts[i].x -= sx` /
 * `verts[i].y -= sy` operates in 16.16 then renderer truncates. */
static inline FIXED _fixed_to_sgl_x(int32_t pos_fixed, bool screen_relative)
{
    int32_t cam_x = screen_relative ? 0 : g_rsdk_screen.position_x;
    int32_t px    = (pos_fixed >> 16) - cam_x - (RSDK_SCREEN_XSIZE >> 1);
    return (FIXED)(px << 16);
}
static inline FIXED _fixed_to_sgl_y(int32_t pos_fixed, bool screen_relative)
{
    int32_t cam_y = screen_relative ? 0 : g_rsdk_screen.position_y;
    int32_t py    = (pos_fixed >> 16) - cam_y - (RSDK_SCREEN_YSIZE >> 1);
    return (FIXED)(py << 16);
}

/* Emit a 4-vertex polygon via slPutPolygon (decomp DrawRect / DrawFace
 * 4-vertex / DrawParallelogram backing path). The verts[] are already
 * in SGL screen-centered FIXED. */
static void _emit_polygon4(FIXED x0, FIXED y0,
                           FIXED x1, FIXED y1,
                           FIXED x2, FIXED y2,
                           FIXED x3, FIXED y3,
                           uint8_t r8, uint8_t g8, uint8_t b8,
                           uint8_t ink_effect)
{
    _poly_pntbl[0][X] = x0; _poly_pntbl[0][Y] = y0; _poly_pntbl[0][Z] = _poly_z;
    _poly_pntbl[1][X] = x1; _poly_pntbl[1][Y] = y1; _poly_pntbl[1][Z] = _poly_z;
    _poly_pntbl[2][X] = x2; _poly_pntbl[2][Y] = y2; _poly_pntbl[2][Z] = _poly_z;
    _poly_pntbl[3][X] = x3; _poly_pntbl[3][Y] = y3; _poly_pntbl[3][Z] = _poly_z;

    uint16_t colno = _rgb888_to_rgb555(r8, g8, b8);
    uint16_t cl    = _ink_to_sgl_cl(ink_effect);

    /* SGL polygon ATTR field encoding (per jo-engine/jo_engine/3d.c
     * L385-387 reference + SL_DEF.H L232-240 ATTR struct + L379-384
     * SORT enum + L386-389 Single_Plane / Dual_Plane enum):
     *   flag  = Dual_Plane (double-sided; disables back-face culling so a
     *           screen-facing 2D UI quad always renders regardless of vertex
     *           winding. Single_Plane culled every UI/TitleCard polygon -- the
     *           first runtime-confirmed polygon use, the act-intro card, showed
     *           nothing. Working ref NOV96 SGL302/SAMPLE/S_2_2 uses Dual_Plane.)
     *   sort  = SORT_CEN | ((sprPolygon >> 16) & 0x1c) | No_Option
     *   texno = No_Texture
     *   atrb  = (CL32KRGB | No_Gouraud | cl_mode | MESHoff) |
     *           ((sprPolygon >> 24) & 0xc0)
     *   colno = packed RGB555 (CL32KRGB direct-color mode)
     *   gstb  = 0 (no Gouraud)
     *   dir   = sprPolygon & 0x3f
     *
     * The bit-shifts decompose sprPolygon's packed flag set (FUNC_Polygon
     * + ECdis | SPdis flags shifted into the upper byte) into the
     * sort/atrb/dir fields per SGL's expected layout. Without these
     * shifts the polygon submits as garbage VDP1 commands. */
    _poly_attbl[0].flag  = Dual_Plane;
    _poly_attbl[0].sort  = (uint16_t)(SORT_CEN |
                                      ((sprPolygon >> 16) & 0x1c) |
                                      No_Option);
    _poly_attbl[0].texno = No_Texture;
    _poly_attbl[0].atrb  = (uint16_t)((CL32KRGB | No_Gouraud | cl | MESHoff) |
                                      ((sprPolygon >> 24) & 0xc0));
    _poly_attbl[0].colno = colno;
    _poly_attbl[0].gstb  = 0;
    _poly_attbl[0].dir   = (uint16_t)(sprPolygon & 0x3f);

    /* slPutPolygon submits the PDATA into the VDP1 command list for
     * this frame; SGL will issue the drawing pass on slSynch(). */
    slPutPolygon(&_poly_pdata);
}

void rsdk_draw_rect(int32_t x_fixed, int32_t y_fixed,
                    int32_t w_fixed, int32_t h_fixed,
                    uint32_t rgb, uint8_t alpha,
                    uint8_t ink_effect, bool screen_relative)
{
    (void)alpha; /* INK_NONE path ignores alpha; partial alpha = Phase Z */
    if (w_fixed <= 0 || h_fixed <= 0) return;

    uint8_t r8 = (uint8_t)((rgb >> 16) & 0xFF);
    uint8_t g8 = (uint8_t)((rgb >>  8) & 0xFF);
    uint8_t b8 = (uint8_t)((rgb)       & 0xFF);

    FIXED x0 = _fixed_to_sgl_x(x_fixed,            screen_relative);
    FIXED y0 = _fixed_to_sgl_y(y_fixed,            screen_relative);
    FIXED x1 = _fixed_to_sgl_x(x_fixed + w_fixed,  screen_relative);
    FIXED y1 = y0;
    FIXED x2 = x1;
    FIXED y2 = _fixed_to_sgl_y(y_fixed + h_fixed,  screen_relative);
    FIXED x3 = x0;
    FIXED y3 = y2;

    _emit_polygon4(x0, y0, x1, y1, x2, y2, x3, y3,
                   r8, g8, b8, ink_effect);
}

void rsdk_draw_line(int32_t x1_fixed, int32_t y1_fixed,
                    int32_t x2_fixed, int32_t y2_fixed,
                    uint32_t rgb, uint8_t alpha,
                    uint8_t ink_effect, bool screen_relative)
{
    (void)alpha;

    uint8_t r8 = (uint8_t)((rgb >> 16) & 0xFF);
    uint8_t g8 = (uint8_t)((rgb >>  8) & 0xFF);
    uint8_t b8 = (uint8_t)((rgb)       & 0xFF);

    /* SGL line command: emitted via slPutPolygon with `dir = sprLine`
     * (FUNC_Line per SL_DEF.H L219). Only verts[0] and verts[1] are
     * read; verts[2]/[3] are set to verts[1] as a precaution. */
    FIXED x0 = _fixed_to_sgl_x(x1_fixed, screen_relative);
    FIXED y0 = _fixed_to_sgl_y(y1_fixed, screen_relative);
    FIXED x1 = _fixed_to_sgl_x(x2_fixed, screen_relative);
    FIXED y1 = _fixed_to_sgl_y(y2_fixed, screen_relative);

    _poly_pntbl[0][X] = x0; _poly_pntbl[0][Y] = y0; _poly_pntbl[0][Z] = _poly_z;
    _poly_pntbl[1][X] = x1; _poly_pntbl[1][Y] = y1; _poly_pntbl[1][Z] = _poly_z;
    _poly_pntbl[2][X] = x1; _poly_pntbl[2][Y] = y1; _poly_pntbl[2][Z] = _poly_z;
    _poly_pntbl[3][X] = x1; _poly_pntbl[3][Y] = y1; _poly_pntbl[3][Z] = _poly_z;

    uint16_t colno = _rgb888_to_rgb555(r8, g8, b8);
    uint16_t cl    = _ink_to_sgl_cl(ink_effect);

    /* SGL line ATTR field encoding (same decomposition pattern as
     * _emit_polygon4 above, just substituting sprLine for sprPolygon).
     * sprLine = FUNC_Line | (ECdis|SPdis << 24) per SL_DEF.H L219. */
    _poly_attbl[0].flag  = Dual_Plane;
    _poly_attbl[0].sort  = (uint16_t)(SORT_CEN |
                                      ((sprLine >> 16) & 0x1c) |
                                      No_Option);
    _poly_attbl[0].texno = No_Texture;
    _poly_attbl[0].atrb  = (uint16_t)((CL32KRGB | No_Gouraud | cl | MESHoff) |
                                      ((sprLine >> 24) & 0xc0));
    _poly_attbl[0].colno = colno;
    _poly_attbl[0].gstb  = 0;
    _poly_attbl[0].dir   = (uint16_t)(sprLine & 0x3f);

    slPutPolygon(&_poly_pdata);
}

void rsdk_draw_face(const rsdk_face_vertex_t *verts, int count,
                    uint8_t r, uint8_t g, uint8_t b,
                    uint8_t alpha, uint8_t ink_effect)
{
    (void)alpha;
    if (!verts || count < 3 || count > 4) return;

    /* The decomp DrawFace contract (UIWidgets.c L160 / L198 / L244) is
     * screen_relative — call sites already subtract ScreenInfo->position
     * before invoking. So we treat verts as already-screen-relative. */
    FIXED v[4][2];
    v[0][0] = _fixed_to_sgl_x(verts[0].x, true);
    v[0][1] = _fixed_to_sgl_y(verts[0].y, true);
    v[1][0] = _fixed_to_sgl_x(verts[1].x, true);
    v[1][1] = _fixed_to_sgl_y(verts[1].y, true);
    v[2][0] = _fixed_to_sgl_x(verts[2].x, true);
    v[2][1] = _fixed_to_sgl_y(verts[2].y, true);
    if (count == 4) {
        v[3][0] = _fixed_to_sgl_x(verts[3].x, true);
        v[3][1] = _fixed_to_sgl_y(verts[3].y, true);
    } else {
        /* Triangle: duplicate the last vertex to form a degenerate quad.
         * VDP1 polygon command requires 4 vertices; degenerate edge has
         * zero length so the third + fourth vertices coincide. */
        v[3][0] = v[2][0];
        v[3][1] = v[2][1];
    }

    _emit_polygon4(v[0][0], v[0][1],
                   v[1][0], v[1][1],
                   v[2][0], v[2][1],
                   v[3][0], v[3][1],
                   r, g, b, ink_effect);
}

/* §3.7 SetClipBounds — update the active screen's clip rectangle. */
void rsdk_set_clip_bounds(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > RSDK_SCREEN_XSIZE) x2 = RSDK_SCREEN_XSIZE;
    if (y2 > RSDK_SCREEN_YSIZE) y2 = RSDK_SCREEN_YSIZE;
    g_rsdk_screen.clip_x1 = x1;
    g_rsdk_screen.clip_y1 = y1;
    g_rsdk_screen.clip_x2 = x2;
    g_rsdk_screen.clip_y2 = y2;
}

/* Phase 1.2 — Saturn-side sprite-callback registry.
 *
 * Decomp DrawSprite calls don't know about Saturn jo sprite IDs; the mania
 * port (src/mania/Game.c) installs a callback that resolves
 * (list_id, anim_id, frame_id) to the actual Saturn sprite. When the
 * callback is NULL, DrawSprite is a no-op (Phase 1.1 baseline behavior).
 *
 * INK_BLEND / INK_ADD recipe on Saturn (the callback handles it):
 *   * INK_BLEND  — VDP1 color-calc with 50% blend ratio. Set the VDP1 CMDCTRL
 *                  PMOD field bit 6=0 (transparent-pixel-enable) and bit 5=1
 *                  (half-transparent processing). VDP2 must also have
 *                  CCRSP set so the sprite-color-RAM area allows blend.
 *                  Spec: VDP1 Manual §5.5.4 "Sprite Draw Mode" + VDP2 Manual
 *                  §6.4 "Color Calculation". Phase Z follow-up: program the
 *                  CCRSP register at boot from the Saturn HAL.
 *   * INK_ADD    — VDP1 ECdis (bit 7) + VDP1 sprite-color half-add.
 *                  Approximation: route to additive blending via VDP1's
 *                  half-transparent flag where the source is colour-only.
 *   * INK_MASKED / UNMASKED — VDP2 sprite priority masking; out of scope
 *                  for Phase 1.2 (no Title element uses MASKED meaningfully).
 *
 * For Phase 1.2 the callback receives the ink_effect tag and decides per-
 * sprite how to dispatch (different SPR_ATTR.atrb bits per ink mode). The
 * Title scene's TitleBG MOUNTAIN2 (INK_BLEND), REFLECTION + WATERSPARKLE
 * (INK_ADD) call sites all surface through this path. */

static rsdk_sprite_draw_cb_fn s_sprite_cb = NULL;

void rsdk_drawing_set_sprite_callback(rsdk_sprite_draw_cb_fn cb)
{
    s_sprite_cb = cb;
}

void rsdk_draw_sprite_ex(const rsdk_animator_t *animator,
                         int32_t pos_x_fixed, int32_t pos_y_fixed,
                         uint8_t direction, uint8_t ink_effect,
                         int32_t alpha, uint8_t draw_group,
                         bool screen_relative)
{
    if (!animator) return;
    const rsdk_sprite_frame_t *f = rsdk_animator_current_frame(animator);
    if (!f) return;
    int jx = 0, jy = 0;
    _world_to_jo(pos_x_fixed, pos_y_fixed, f->pivot_x, f->pivot_y,
                 (int16_t)f->width, (int16_t)f->height,
                 screen_relative, &jx, &jy);
    /* Cull obviously offscreen sprites — keeps the callback path cheap when
     * a Title entity scrolls outside the viewport (e.g. WINGSHINE bouncing
     * down past the bottom edge). Threshold: pivot-centered bbox does not
     * intersect a [-256..+256] x [-160..+160] envelope (jo center-anchored).
     * This is intentionally generous; tighter culling lives in the callback
     * itself if needed. */
    int half_w = (int)f->width  >> 1;
    int half_h = (int)f->height >> 1;
    if (jx + half_w < -RSDK_SCREEN_XSIZE / 2 - half_w) return;
    if (jx - half_w >  RSDK_SCREEN_XSIZE / 2 + half_w) return;
    if (jy + half_h < -RSDK_SCREEN_YSIZE / 2 - half_h) return;
    if (jy - half_h >  RSDK_SCREEN_YSIZE / 2 + half_h) return;
    /* Map drawGroup to a z hint (higher drawGroup = drawn on top = smaller
     * jo Z value since lower jo z is closer to camera in SGL convention).
     * Phase 1.2 picks the simple range [-200 .. +200] across draw groups
     * 0..15 so groups stay separated; per-entity zdepth-within-group is
     * applied in rsdk_object_draw_all's per-group sort. */
    int z = 200 - (int)draw_group * 25;
    if (s_sprite_cb) {
        /* Phase 1.23 GAP A — list_id is now stored on the animator at
         * rsdk_set_sprite_animation time and propagated through the draw
         * callback. Removes the §11.28 round-2 `s_active_list_id` global
         * hack which broke under entity-interleaved draws. */
        s_sprite_cb((int)animator->list_id,
                    (int)animator->animation_id,
                    (int)animator->frame_id,
                    direction, ink_effect, alpha,
                    jx, jy, f, z);
    }
}
