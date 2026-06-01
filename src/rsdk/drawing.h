#ifndef RSDK_DRAWING_H
#define RSDK_DRAWING_H

/* Phase A4 — Drawing system, Saturn port of RSDKv5/RSDK/Graphics/Drawing.
 *
 * Per docs/rsdkv5_engine_catalog.md §3 + BIBLE.md Phase A row A4.
 *
 * Authoritative source (cite by §section when touching this code):
 *   §3.1 Screen / surface constants (Drawing.hpp)
 *   §3.2 Flip + ink enums (Drawing.hpp:27-37)
 *   §3.3 Layer compositor (Drawing.cpp)
 *   §3.4 DrawSprite (Drawing.cpp)
 *   §3.5 DrawTile (Drawing.cpp)
 *   §3.6 FillScreen (Drawing.cpp ≈L1273)
 *   §3.7 SetClipBounds (Drawing.cpp)
 *
 * Saturn-port deviations from upstream:
 *   * `frameBuffer` is NOT a per-Screen byte array on Saturn — output goes
 *     to VDP1 sprite framebuffer (16-bit RGB555) via SGL primitives, and
 *     to VDP2 NBG layers via the layer-compositor pipeline. The
 *     `ScreenInfo` struct keeps just the public fields (position/size/
 *     center/clipBound_*) for upstream code compatibility.
 *   * INK_BLEND/ALPHA/ADD/SUB/TINT route through Saturn's COLOR_CALC
 *     hardware (50/50 colour calculation) when supported; SUB and TINT
 *     fall back to nearest-equivalent palette LUTs. See Phase Z
 *     migration list for full-fidelity INK effect rebuilds.
 *   * Rotation (ROTSTYLE_FULL) uses VDP1 distorted-sprite or pre-baked
 *     rotated frames; ROTSTYLE_45/90/180DEG snap to fixed VDP1 sprite
 *     transforms.
 *   * `DrawLayerHScroll`/`VScroll`/`Basic` map to VDP2 NBG cell-scroll
 *     with line-scroll table for parallax. `DrawLayerRotozoom` requires
 *     RBG0 (Phase Z deferred).
 *   * Per-Screen + SHADER_COUNT shaders are PC-only -- omitted. */

#include <stdint.h>
#include <stdbool.h>
#include "storage.h"      /* sprite frame struct */
#include "animation.h"    /* animator struct      */

/* --- §3.1 constants --------------------------------------------------- */

#define RSDK_DRAWGROUP_COUNT       16
#define RSDK_SURFACE_COUNT         0x40
#define RSDK_SCREEN_XSIZE          320     /* Saturn native NTSC visible */
#define RSDK_SCREEN_YSIZE          224

/* --- §3.1b Fixed-point helper (decomp Game.h TO_FIXED) --------------- *
 * The Sonic Mania decomp uses TO_FIXED(n) == ((n) << 16) to promote an
 * integer pixel/tile count to a Q16.16 world coordinate (Game.h upstream;
 * e.g. CollapsingPlatform.c:138 updateRange.x = TO_FIXED(128)). Provided
 * here, guarded, so the per-object ports (ForceSpin/BreakableWall/
 * SpinBooster/CollapsingPlatform/Bridge) can use the decomp idiom
 * verbatim. */
#ifndef TO_FIXED
#define TO_FIXED(n) ((int32_t)(n) << 16)
#endif

/* --- §3.2 Flip + ink enums ------------------------------------------- */

enum {
    FLIP_NONE = 0,
    FLIP_X    = 1,
    FLIP_Y    = 2,
    FLIP_XY   = 3
};

enum {
    INK_NONE     = 0,
    INK_BLEND    = 1,
    INK_ALPHA    = 2,
    INK_ADD      = 3,
    INK_SUB      = 4,
    INK_TINT     = 5,
    INK_MASKED   = 6,
    INK_UNMASKED = 7
};

/* --- ScreenInfo (Drawing.hpp:68-79) --- *
 * Saturn keeps public fields only. The framebuffer pointer is replaced by
 * a Saturn-VDP1 backing that the engine doesn't address directly. */

typedef struct {
    int32_t  position_x;            /* world-coord scroll origin (fixed)  */
    int32_t  position_y;
    int32_t  size_x, size_y;        /* viewport pixel size (320, 224)     */
    int32_t  center_x, center_y;    /* size/2 — used by entity X->screen  */
    int32_t  clip_x1, clip_y1;
    int32_t  clip_x2, clip_y2;
    int32_t  water_draw_pos;        /* HCZ/MSZ water line                  */
} rsdk_screen_info_t;

extern rsdk_screen_info_t g_rsdk_screen;        /* the single active screen */

/* === Public API ===================================================== */

/* Initialise the drawing subsystem. Sets up ScreenInfo defaults (320x224
 * native NTSC viewport, center 160x112, full clipBound). Call once at
 * boot after jo_core_init. */
void rsdk_drawing_init(void);

/* §3.4 DrawSprite — render the animator's current frame at world `pos`.
 * If `screen_relative` is true, `pos` is in screen coords (cam not
 * applied). Honours entity flip via `direction` (FLIP_NONE/X/Y/XY).
 * jo_x/jo_y wrappers: Saturn jo_sprite_draw3D uses centre-anchored
 * coords (0,0 = screen centre). This routine handles the conversion. */
void rsdk_draw_sprite(const rsdk_animator_t *animator,
                      int32_t pos_x_fixed, int32_t pos_y_fixed,
                      uint8_t direction,
                      bool screen_relative);

/* §3.5 DrawTile — draw a block of 16x16 tile IDs at a screen position.
 * `tile_ids` is an array of `count_x * count_y` uint16 entries (each
 * encoding tile index + flip flags in the standard RSDK layout). Used
 * by HUD overlays + transition wipes. */
void rsdk_draw_tile(const uint16_t *tile_ids,
                    int count_x, int count_y,
                    int32_t pos_x_fixed, int32_t pos_y_fixed,
                    int tile_offset,
                    bool screen_relative);

/* §3.6 FillScreen — blend the framebuffer toward (r,g,b) by per-channel
 * alpha. Used for Draw_FadeBlack / Draw_FadeWhite / flashbulb effects.
 * Saturn implementation drives VDP2 colour-calc; on alpha=0 path becomes
 * a back-color full-screen fill via slBack1ColSet. */
void rsdk_fill_screen(uint32_t rgb, int alpha_r, int alpha_g, int alpha_b);

/* §3.7 SetClipBounds — clip subsequent draw calls to the rectangle.
 * Coords are screen-pixel ints. Clamped to viewport size. */
void rsdk_set_clip_bounds(int32_t x1, int32_t y1, int32_t x2, int32_t y2);

/* Phase 3.2.b (Task #146) — solid full-screen back-colour fill.
 * Mirrors decomp Drawing.cpp:586 FillScreen contract for the
 * full-opacity case (alpha == 0xFF on all channels).
 *
 * Saturn implementation:
 *   * Programs the VDP2 back-screen colour register via slBack1ColSet
 *     (SGL ref ST-238-R1 §back-colour). This produces a guaranteed
 *     full-screen solid fill behind every layer with zero VRAM
 *     pressure.
 *   * ALSO clears the NBG1 bitmap-mode layer to the same colour via
 *     jo_vdp2_clear_bitmap_nbg1, so subsequent rsdk_draw_circle /
 *     rsdk_draw_circle_outline calls render into a clean canvas.
 *
 * `rgb` is 24-bit (0x00RRGGBB) per upstream Drawing.cpp convention. */
void rsdk_fill_screen_solid(uint32_t rgb);

/* Phase 3.2.b — filled circle. Mirrors decomp Drawing.cpp:1314
 * DrawCircle contract (alpha+ink ignored for Saturn baseline — only
 * INK_NONE with alpha=0xFF is implemented).
 *
 * Saturn implementation: midpoint-circle scanline fill into NBG1
 * bitmap via jo_vdp2_draw_bitmap_nbg1_line (one horizontal line per
 * Y row, Bresenham endpoints).
 *
 * `cx`, `cy`, `radius` are SCREEN-pixel integers; the entire
 * RSDK-side fixed-point conversion + screen-relative offset is
 * already applied by the caller (UIBackground_DrawNormal passes
 * ScreenInfo->center.x etc. which are already pixel ints). */
void rsdk_draw_circle(int cx, int cy, int radius, uint32_t rgb);

/* Phase 3.2.b — annular (outline) circle. Mirrors decomp
 * Drawing.cpp:1647 DrawCircleOutline.
 *
 * Saturn implementation: difference-of-two-midpoint-circles — for
 * each Y row in [-outer..+outer], fill the two horizontal segments
 * [-outer.x .. -inner.x] and [+inner.x .. +outer.x]. When the row is
 * outside the inner radius (|y| > inner), fill the full [-outer.x ..
 * +outer.x] span. */
void rsdk_draw_circle_outline(int cx, int cy,
                              int inner_radius, int outer_radius,
                              uint32_t rgb);

/* Phase 3.2.c.1 (Task #148) — VDP1 polygon + line + face primitives.
 *
 * These mirror the decomp Drawing.cpp APIs used by UIWidgets +
 * UIButton + UISubHeading + UIDialog + every menu widget that paints
 * its own background/outline rather than blitting a sprite:
 *   - rsdk_draw_rect      — solid axis-aligned filled rectangle
 *                           (decomp Drawing.cpp DrawRect — VDP1
 *                            polygon command 4 vertices, PMOD bit 3
 *                            FUNC_Polygon per ST-013-R3 §3.2 Table 3.4).
 *   - rsdk_draw_line      — single-pixel line between two points
 *                           (decomp Drawing.cpp DrawLine — VDP1 line
 *                            command, FUNC_Line per SL_DEF.H L157).
 *   - rsdk_draw_face      — convex polygon with 3 or 4 vertices
 *                           (decomp Drawing.cpp DrawFace — VDP1
 *                            polygon command; UIWidgets uses 3-vertex
 *                            triangles + 4-vertex parallelograms).
 *
 * Coordinate convention: ALL coordinates are 16.16 fixed-point in the
 * same convention as the decomp call sites (UIWidgets_DrawParallelogram
 * etc.). The Saturn implementation converts to SGL POINT[XYZ] (signed
 * pixel int) by >>16 and centers about (0,0) since SGL polygon
 * coordinates are screen-centered (160,112 = origin per Saturn 320x224).
 *
 * Color convention: RGB888 packed (decomp `color` typedef) — high byte
 * is R, mid G, low B per upstream Drawing.cpp. The Saturn implementation
 * converts to RGB555 + MSBon (opaque) per ST-013-R3 §4.4.
 *
 * Authoritative source:
 *   ST-013-R3-061694.pdf §3.2 "Sprite/Polygon Draw Commands"
 *   ST-013-R3-061694.pdf §3.4 "Polygon Command Format" (4-vertex polygon
 *                              command 0x0004, line command 0x0006)
 *   SGL ST-238-R1-051795.pdf slPutPolygon / slLine + sprPolygon /
 *                              sprLine macros (SL_DEF.H L213-219).
 *   NOV96_DTS LIBRARY/SDK_10J/SGL302/SAMPLE/S_4_3_x POLYGON.C -- working
 *     slPutPolygon example with ATTR + PDATA + POINT tables.
 *
 * Saturn-side decisions:
 *   * INK / alpha effects are honored where the SGL API supports them
 *     (INK_BLEND -> CL_Half via ATTR atrb, INK_ADD -> CL_Trans). Other
 *     modes fall back to opaque.
 *   * 4-vertex polygons go through slPutPolygon directly; 3-vertex
 *     triangles are emitted as a degenerate quad (verts[2] == verts[3])
 *     which is the standard SGL idiom for triangles per SAMPLE/S_2_2.
 *   * Z-depth defaults to drawGroup-2 mapping (UIWidgets calls render
 *     at drawGroup 2 -- see UIButton_Create decomp L101). */
void rsdk_draw_rect(int32_t x_fixed, int32_t y_fixed,
                    int32_t w_fixed, int32_t h_fixed,
                    uint32_t rgb, uint8_t alpha,
                    uint8_t ink_effect, bool screen_relative);

void rsdk_draw_line(int32_t x1_fixed, int32_t y1_fixed,
                    int32_t x2_fixed, int32_t y2_fixed,
                    uint32_t rgb, uint8_t alpha,
                    uint8_t ink_effect, bool screen_relative);

/* rsdk_draw_face — convex polygon with `count` vertices (3 or 4).
 * `verts` is an array of {x,y} pairs in 16.16 fixed. Saturn impl
 * collapses 3-vertex triangles by duplicating the last vertex. */
typedef struct {
    int32_t x;
    int32_t y;
} rsdk_face_vertex_t;

void rsdk_draw_face(const rsdk_face_vertex_t *verts, int count,
                    uint8_t r, uint8_t g, uint8_t b,
                    uint8_t alpha, uint8_t ink_effect);

/* Phase 2.4j.1 (Task #156) — set the SGL Z depth used by ALL subsequent
 * rsdk_draw_rect / rsdk_draw_line / rsdk_draw_face polygon submissions.
 * `z` is SGL FIXED (the same scale jo_sprite_draw3D uses internally via
 * jo_int2fixed, i.e. pixel-Z << 16); SMALLER Z = nearer/front. Default 0.
 *
 * TitleCard's draw path interleaves polygons (decoration boxes) between
 * sprite/text draws and relies on RSDK's submission-order painter; on
 * Saturn we reproduce that by assigning a monotonic descending Z per
 * primitive. Callers that don't care about polygon depth (menus,
 * UIWidgets) leave it at 0. Always reset to 0 when done. */
void rsdk_drawing_set_poly_z(int32_t z);

/* Phase 1.2 — Saturn-side per-(list_id, anim_id, frame_id, direction, ink)
 * sprite-mapping callback. Decomp DrawSprite calls don't directly know about
 * Saturn jo sprite IDs; the mania port registers a callback that resolves
 * the animator's current frame to a Saturn draw operation.
 *
 * The callback receives:
 *   list_id           — sprite-animation list slot (from rsdk_load_sprite_animation)
 *   anim_id           — animator->animation_id
 *   frame_id          — animator->frame_id
 *   direction         — FLIP_NONE/X/Y/XY
 *   ink_effect        — INK_NONE/BLEND/ALPHA/ADD/SUB/TINT/MASKED/UNMASKED
 *   alpha             — 0..255
 *   jo_x, jo_y        — jo's centre-anchored screen coords (already
 *                       computed: world + pivot + W/2 - cam - 160)
 *   frame             — pointer to the resolved frame struct (pivot/size/etc.)
 *   z                 — entity drawGroup expressed as a Z value for ordering
 *
 * Return value: ignored. Callback is responsible for issuing the actual
 * jo_sprite_draw3D / slDispSprite call. NULL callback = no-op DrawSprite.
 *
 * The Mania port's mania_engine_init() registers a Saturn-side resolver
 * that maps (list_id, anim_id, frame_id) -> the correct jo sprite_id from
 * the pre-loaded .SPR/.ATL atlases shipped in cd/. */
typedef void (*rsdk_sprite_draw_cb_fn)(int list_id, int anim_id, int frame_id,
                                       uint8_t direction, uint8_t ink_effect,
                                       int32_t alpha,
                                       int jo_x, int jo_y,
                                       const rsdk_sprite_frame_t *frame,
                                       int z);

void rsdk_drawing_set_sprite_callback(rsdk_sprite_draw_cb_fn cb);

/* Phase 1.2 — Saturn-side coord helper. Per-class draw callbacks that
 * route directly to Saturn primitives need this to compute jo coords. */
void rsdk_world_to_jo_coords(int32_t pos_x_fixed, int32_t pos_y_fixed,
                             int16_t pivot_x, int16_t pivot_y,
                             int16_t w, int16_t h,
                             bool screen_relative,
                             int *out_jo_x, int *out_jo_y);

/* Phase 1.2 — extended DrawSprite that takes a Mania-side animator + the
 * caller-supplied direction/inkEffect/alpha/drawGroup explicitly. The
 * per-class draw callback prepares these from `self->direction` etc.
 * Caller passes:
 *   animator           — populated by SetSpriteAnimation
 *   pos_x_fixed/y      — 16.16 world (or screen if screen_relative)
 *   direction          — FLIP_NONE/X/Y/XY
 *   ink_effect         — INK_NONE/BLEND/ADD/MASKED/UNMASKED/...
 *   alpha              — 0..255
 *   draw_group         — for z-ordering hint
 *   screen_relative    — true to interpret pos as screen coords */
void rsdk_draw_sprite_ex(const rsdk_animator_t *animator,
                         int32_t pos_x_fixed, int32_t pos_y_fixed,
                         uint8_t direction, uint8_t ink_effect,
                         int32_t alpha, uint8_t draw_group,
                         bool screen_relative);

#endif /* RSDK_DRAWING_H */
