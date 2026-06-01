/* ---------------------------------------------------------------------
 * Phase 2.4j.1 (Task #156) — TitleCard (act-intro card) port.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_Global_TitleCard.c` (955 lines)
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Bridge-model (Task #155 precedent, src/mania/Objects/GHZ/Bridge.c):
 * the class is registered via rsdk_object_register_ex("TitleCard") so the
 * TitleCard_* callbacks land in game.map, BUT no TitleCard slot entity is
 * ever created — the single module-static g_titlecard is driven by the
 * bespoke titlecard_tick() + titlecard_draw_only() pair called from
 * Game.c. The RSDK callbacks (TitleCard_Update/_Draw/_Create/_StageLoad)
 * are therefore inert stubs (never invoked via rsdk_object_tick); the real
 * state/draw dispatch lives in titlecard_tick/titlecard_draw_only.
 *
 * Saturn-fit deviations vs decomp (documented at each site):
 *   - globals->atlEnabled / globals->suppressTitlecard do not exist on the
 *     non-Plus Saturn build; both are FALSE, so every
 *     `!globals->atlEnabled && !globals->suppressTitlecard` guard collapses
 *     to "always taken". The suppress / ATL branches are dropped.
 *   - ENGINESTATE_PAUSED/REGULAR map to g_titlecard_active (1/0); Game.c
 *     reads it to freeze the Player tick during the PAUSED hold.
 *   - EntityTitleCard (TitleCard.h) is a bespoke POD with NO rsdk_entity_t
 *     base, so the decomp's self->active/visible/drawGroup/drawFX/scale/
 *     position references have no Saturn field and are dropped.
 *   - Act-number FX_SCALE (decomp Draw_ShowTitleCard L820-833) has no jo
 *     per-sprite-scale draw3D equivalent; the plate + digits draw at full
 *     size once actNumScale > 0 (the scale-in easing is omitted). Faithful
 *     final pose; only the grow-in tween is lost.
 *   - Zone_ApplyWorldBounds / TitleCard_HandleCamera are no-ops on the
 *     single-camera Saturn surface.
 *   - Glyph draw routes through the shared g_titlecard_atlas (SPR2+MET, all
 *     frames per memory/entity-atlas-must-ship-all-frames.md) using the
 *     by-frame accessors instead of spriteAnimationList[aniFrames]; the text
 *     trio (rsdk_set_sprite_string/_get_string_width/_draw_text) is the
 *     animation.c Saturn mirror of Animation.cpp:179-231 + Drawing.cpp
 *     DrawString.
 *
 * Z-ordering: the decomp relies on an RSDK software-framebuffer painter
 * (submission order: earlier = behind). On SGL Z-sort we reproduce that by
 * assigning a MONOTONIC DESCENDING Z per primitive in submission order
 * (smaller Z = nearer/front per jo convention). Polygons go through
 * rsdk_drawing_set_poly_z(z<<16), text through rsdk_set_text_depth(z),
 * sprites through jo_sprite_draw3D(...,z). Both depth setters are reset to
 * their defaults at the end of titlecard_draw_only.
 * ------------------------------------------------------------------- */

#include "TitleCard.h"
#include "../../../rsdk/entity_atlas.h"
#include "../../../rsdk/object.h"
#include "../../../rsdk/animation.h"
#include "../../../rsdk/drawing.h"
#include "../../../rsdk/string.h"

#include <jo/jo.h>
#include <string.h>

/* TO_FIXED comes from drawing.h (== ((int32_t)(n) << 16)). */

/* Non-Plus TitleCard colors (decomp L678-682, MANIA_USE_PLUS=FALSE). */
#define TC_ORANGE 0xF08C18
#define TC_GREEN  0x60C0A0
#define TC_RED    0xF05030
#define TC_BLUE   0x4060B0
#define TC_YELLOW 0xF0C800

/* Atlas anim slots (decomp Create L72-75 SetSpriteAnimation indices). */
#define TC_ANIM_DECORATION 0   /* frame 0 = act-number plate, 1 = "Sonic Mania" */
#define TC_ANIM_NAME       1   /* zone-name font (27 glyphs A-Z + space)         */
#define TC_ANIM_ZONE       2   /* "ZONE" word, 4 glyphs                          */
#define TC_ANIM_ACTNUM     3   /* act digits, 3 frames                           */

/* Z bands. CRITICAL Saturn constraint (Phase 2.4j.1, root-caused 2026-05-29 via
 * a 3-point Z-sweep, tcv4/tcv5/tcv6 captures): both the polygon path
 * (rsdk_draw_rect / rsdk_draw_face -> slPutPolygon) and the sprite path
 * (jo_sprite_draw3D) are PERSPECTIVE projectors -- a world quad at depth Z
 * projects to a fraction of the playfield = factor(Z). Measured directly by
 * drawing a full-screen (0..320 x 0..224) yellow rect at three depths and
 * reading the rendered coverage:
 *     Z=245 -> 0.607 of playfield     (tcv4_28, 34.9% of the window)
 *     Z=165 -> 0.90                   (tcv5_26, 73.6%)
 *     Z=153 -> 0.97                   (tcv6_24..28, 85.0%, stable)
 * The ratio 0.90/0.607 = 1.483 == 245/165 confirms pure 1/Z scaling, so
 *     factor(Z) = 148.7 / Z   (the "1:1" plane, factor 1.0, is Z ~= 149).
 * No drop occurs at any of these depths -- a full-screen quad at Z=153
 * projects to screen corners [5,315] x [3,221], comfortably inside the SGL
 * default window slWindow(0,0,319,223). The card is pinned at Z=153: factor
 * 0.97 fills ~97% of each axis with a ~3-5px margin to the window edge, which
 * is the largest safe coverage that keeps every corner inside the window
 * (drawing wider risks an overhang -> whole-quad drop). The card is a
 * full-screen overlay drawn while the engine is PAUSED (g_titlecard_active):
 * mania_ghz_draw_only suppresses ALL gameplay VDP1 sprites (Sonic + entities +
 * HUD), so the card's own back-to-front SUBMISSION ORDER (BG -> strips ->
 * decor -> text) handles internal layering (SGL SORT_CEN is stable at
 * equal/near-equal center-Z), and GHZ scenery is VDP2 NBG (always behind all
 * VDP1) so the card occludes it automatically. Sprite/text Z is kept a hair
 * NEARER than the poly plane so glyphs/decor draw in front of the BG fill.
 * Global DS is untouched. */
#define TCZ_POLY_PLANE 153    /* factor 0.97 (~97% coverage), corners inside  */
#define TCZ_BG_BLACK   TCZ_POLY_PLANE
#define TCZ_BG_BLUE    TCZ_POLY_PLANE
#define TCZ_BG_RED     TCZ_POLY_PLANE
#define TCZ_BG_ORANGE  TCZ_POLY_PLANE
#define TCZ_BG_GREEN   TCZ_POLY_PLANE
#define TCZ_BG_YELLOW  TCZ_POLY_PLANE
#define TCZ_STRIPS     TCZ_POLY_PLANE
#define TCZ_DECORBOX1  TCZ_POLY_PLANE
#define TCZ_DECORBOX2  TCZ_POLY_PLANE
#define TCZ_DECOR_SPR  152    /* sprite/text, a hair in front of the BG plane*/
#define TCZ_ACTNUM     151
#define TCZ_ZONE_LTR   150
#define TCZ_NAME_TEXT  149

/* SGL default-window safe envelope (Phase 2.4j.1, doc-grounded by
 * ST-237-R1-051795 §"Clipping" p.4-18 + p.4-23 default window). No code calls
 * slWindow, so the SGL default window slWindow(0,0,319,223) clips every
 * slPutPolygon, and ST-237 §4.18 (HTML extract line 4380) notes a whole quad is
 * DROPPED when its projected corners overhang the window. At the card plane
 * Z=153 (measured factor 0.97, see TCZ_POLY_PLANE note) a full-screen world
 * rect 0..320 x 0..224 projects to screen [5,315] x [3,221] -- already inside
 * the window, so the Z choice alone prevents the drop. This envelope is a
 * modest symmetric inset (about center 160,112) applied to axis-aligned BG
 * fills in tc_draw_rect_px: at Z=153 [8,312] x [6,218] world projects to screen
 * ~[12,308] x [9,215] (~85% coverage, measured tcv6), leaving a clean ~3-5px
 * margin so nothing rides the window edge. It mirrors the decomp software
 * framebuffer, which clips fills to the screen rather than dropping them. */
#define TC_SAFE_X0 8
#define TC_SAFE_Y0 6
#define TC_SAFE_X1 312
#define TC_SAFE_Y1 218

/* === Module-static state (Bridge-model single instance) ============== */

EntityTitleCard g_titlecard;
static ObjectTitleCard g_titlecard_obj;
ObjectTitleCard *TitleCard = &g_titlecard_obj;

/* TRUE while the engine should be PAUSED (decomp ENGINESTATE_PAUSED). */
volatile int g_titlecard_active = 0;

/* Scratch per-glyph pixel Y-offset for rsdk_draw_text (decomp passes
 * Vector2 charPos with only .y set; the text trio wants int16 PIXELS). */
static int16_t s_char_off_y[20];

/* === Internal sprite-draw helper (DrawSprite analogue) ===============
 * Draw atlas (anim_id, frame_id) at the given screen-relative FIXED pos. */
static void tc_draw_sprite(int anim_id, int frame_id,
                           int32_t pos_x_fixed, int32_t pos_y_fixed, int z)
{
    if (!g_titlecard_atlas.ready)
        return;

    int first = entity_atlas_first_of_anim(&g_titlecard_atlas, anim_id);
    if (first < 0)
        return;

    int idx = first + frame_id;
    int sid = entity_atlas_sprite_at(&g_titlecard_atlas, idx);
    if (sid < 0)
        return;

    int px, py, w, h;
    entity_atlas_pivot_at(&g_titlecard_atlas, idx, &px, &py);
    entity_atlas_size_at(&g_titlecard_atlas, idx, &w, &h);
    if (w <= 0) w = 16;
    if (h <= 0) h = 16;

    int jx, jy;
    rsdk_world_to_jo_coords(pos_x_fixed, pos_y_fixed,
                            (int16_t)px, (int16_t)py,
                            (int16_t)w, (int16_t)h,
                            true, &jx, &jy);
    jo_sprite_draw3D(sid, jx, jy, z);
}

/* DrawFace analogue: tc_vec2[4] is layout-identical to rsdk_face_vertex_t[4]
 * (two int32_t x,y) so the strip/decor vertex arrays cast directly. */
static void tc_draw_face(const tc_vec2 *verts, uint32_t rgb, int z)
{
    rsdk_drawing_set_poly_z(TO_FIXED(z));
    rsdk_draw_face((const rsdk_face_vertex_t *)verts, 4,
                   (uint8_t)((rgb >> 16) & 0xFF),
                   (uint8_t)((rgb >> 8) & 0xFF),
                   (uint8_t)((rgb >> 0) & 0xFF),
                   0xFF, INK_NONE);
}

static void tc_draw_rect_px(int x, int y, int w, int h, uint32_t rgb, int z)
{
    /* Clip the requested fill to the SGL default-window safe envelope
     * (TC_SAFE_*, see the doc-grounded note at the constant definitions). SGL
     * drops an entire slPutPolygon quad whose projected corners overhang the
     * default window slWindow(0,0,319,223); a full-screen world rect projects
     * to screen -6..326 (overhang) and vanishes, while the proven-rendering
     * blue strip projects to 2..318 (inside). Intersecting every fill with
     * [8,312]x[6,218] world px keeps the projected quad inside the window
     * (~97% coverage) so it renders. Mirrors the decomp software framebuffer,
     * which clips fills to the screen. */
    int x0 = x       < TC_SAFE_X0 ? TC_SAFE_X0 : x;
    int y0 = y       < TC_SAFE_Y0 ? TC_SAFE_Y0 : y;
    int x1 = (x + w) > TC_SAFE_X1 ? TC_SAFE_X1 : (x + w);
    int y1 = (y + h) > TC_SAFE_Y1 ? TC_SAFE_Y1 : (y + h);
    if (x1 <= x0 || y1 <= y0)
        return;

    rsdk_face_vertex_t v[4];
    v[0].x = TO_FIXED(x0);  v[0].y = TO_FIXED(y0);
    v[1].x = TO_FIXED(x1);  v[1].y = TO_FIXED(y0);
    v[2].x = TO_FIXED(x1);  v[2].y = TO_FIXED(y1);
    v[3].x = TO_FIXED(x0);  v[3].y = TO_FIXED(y1);
    rsdk_drawing_set_poly_z(TO_FIXED(z));
    rsdk_draw_face(v, 4,
                   (uint8_t)((rgb >> 16) & 0xFF),
                   (uint8_t)((rgb >> 8) & 0xFF),
                   (uint8_t)((rgb >> 0) & 0xFF),
                   0xFF, INK_NONE);
}

/* Full-screen BG fill as a stack of short horizontal bands. Root-caused
 * 2026-05-29 (tcfix capture, frame 21/22 + SlideIn green-band comparison): a
 * SINGLE full-HEIGHT axis-aligned slPutPolygon quad at the card plane is
 * dropped by SGL once the card's strips/decor are also submitted, even though
 * (a) a lone full-screen quad renders (probe, 85% coverage), (b) four diagonal
 * strips at the same Z all render, and (c) the SlideIn full-WIDTH but
 * partial-HEIGHT growing bands render. The discriminator is full vertical
 * extent of an axis-aligned quad. Painting the fill as <=48px-tall bands (each
 * the proven partial-height geometry) reproduces the decomp's full-screen
 * RSDK.DrawRect(colors[4]) without the drop. */
static void tc_fill_bg(uint32_t rgb, int z)
{
    int y = TC_SAFE_Y0;
    while (y < TC_SAFE_Y1) {
        int bh = TC_SAFE_Y1 - y;
        if (bh > 48) bh = 48;
        tc_draw_rect_px(TC_SAFE_X0, y, TC_SAFE_X1 - TC_SAFE_X0, bh, rgb, z);
        y += bh;
    }
}

/* === Ported helpers (decomp names preserved) ========================= */

void TitleCard_SetupVertices(void)
{
    EntityTitleCard *self = &g_titlecard;

    self->vertMovePos[0].x = TO_FIXED(240);
    self->vertMovePos[0].y = TO_FIXED(496);
    self->vertMovePos[1].x = TO_FIXED(752);
    self->vertMovePos[1].y = TO_FIXED(1008);

    self->vertTargetPos[0].x = TO_FIXED(0);
    self->vertTargetPos[0].y = TO_FIXED(138);
    self->vertTargetPos[1].x = TO_FIXED(74);
    self->vertTargetPos[1].y = TO_FIXED(112);

    if (self->titleCardWord2 > 0) {
        self->word1DecorVerts[0].x = -self->word1Width;
        self->word1DecorVerts[0].y = TO_FIXED(82);
        self->word1DecorVerts[1].x = TO_FIXED(0);
        self->word1DecorVerts[1].y = TO_FIXED(82);
        self->word1DecorVerts[2].x = TO_FIXED(0);
        self->word1DecorVerts[2].y = TO_FIXED(98);
        self->word1DecorVerts[3].x = -self->word1Width;
        self->word1DecorVerts[3].y = TO_FIXED(98);
    }

    self->word2DecorVerts[0].x = -self->word2Width;
    self->word2DecorVerts[0].y = TO_FIXED(186);
    self->word2DecorVerts[1].x = TO_FIXED(0);
    self->word2DecorVerts[1].y = TO_FIXED(186);
    self->word2DecorVerts[2].x = TO_FIXED(0);
    self->word2DecorVerts[2].y = TO_FIXED(202);
    self->word2DecorVerts[3].x = -self->word2Width;
    self->word2DecorVerts[3].y = TO_FIXED(202);

    self->zoneDecorVerts[0].x = TO_FIXED(g_rsdk_screen.size_x);
    self->zoneDecorVerts[0].y = TO_FIXED(154);
    self->zoneDecorVerts[1].x = TO_FIXED(120) + self->zoneDecorVerts[0].x;
    self->zoneDecorVerts[1].y = TO_FIXED(154);
    self->zoneDecorVerts[2].x = TO_FIXED(120) + self->zoneDecorVerts[0].x;
    self->zoneDecorVerts[2].y = TO_FIXED(162);
    self->zoneDecorVerts[3].x = self->zoneDecorVerts[0].x;
    self->zoneDecorVerts[3].y = TO_FIXED(162);

    self->stripVertsBlue[0].x = self->stripPos[0];
    self->stripVertsBlue[0].y = TO_FIXED(240);
    self->stripVertsBlue[1].x = TO_FIXED(64) + self->stripVertsBlue[0].x;
    self->stripVertsBlue[1].y = TO_FIXED(240);
    self->stripVertsBlue[2].x = TO_FIXED(304) + self->stripVertsBlue[0].x;
    self->stripVertsBlue[2].y = TO_FIXED(240);
    self->stripVertsBlue[3].x = TO_FIXED(240) + self->stripVertsBlue[0].x;
    self->stripVertsBlue[3].y = TO_FIXED(240);

    self->stripVertsRed[0].x = self->stripPos[1];
    self->stripVertsRed[0].y = TO_FIXED(240);
    self->stripVertsRed[1].x = TO_FIXED(128) + self->stripVertsRed[0].x;
    self->stripVertsRed[1].y = TO_FIXED(240);
    self->stripVertsRed[2].x = TO_FIXED(230) + self->stripVertsRed[0].x;
    self->stripVertsRed[2].y = TO_FIXED(240);
    self->stripVertsRed[3].x = TO_FIXED(102) + self->stripVertsRed[0].x;
    self->stripVertsRed[3].y = TO_FIXED(240);

    self->stripVertsOrange[0].x = self->stripPos[2];
    self->stripVertsOrange[0].y = TO_FIXED(240);
    self->stripVertsOrange[1].x = TO_FIXED(240) + self->stripVertsOrange[0].x;
    self->stripVertsOrange[1].y = TO_FIXED(240);
    self->stripVertsOrange[2].x = TO_FIXED(262) + self->stripVertsOrange[0].x;
    self->stripVertsOrange[2].y = TO_FIXED(240);
    self->stripVertsOrange[3].x = TO_FIXED(166) + self->stripVertsOrange[0].x;
    self->stripVertsOrange[3].y = TO_FIXED(240);

    self->stripVertsGreen[0].x = self->stripPos[3];
    self->stripVertsGreen[0].y = TO_FIXED(240);
    self->stripVertsGreen[1].x = TO_FIXED(32) + self->stripVertsGreen[0].x;
    self->stripVertsGreen[1].y = TO_FIXED(240);
    self->stripVertsGreen[2].x = TO_FIXED(160) + self->stripVertsGreen[0].x;
    self->stripVertsGreen[2].y = TO_FIXED(240);
    self->stripVertsGreen[3].x = TO_FIXED(128) + self->stripVertsGreen[0].x;
    self->stripVertsGreen[3].y = TO_FIXED(240);

    /* Outer edges built at the TC_SAFE_* envelope (not 0 / size_x / 240) so
     * the SlideAway curtain quads project inside the SGL default window and
     * render -- same fix as the BG fills (see tc_draw_rect_px note). Inner
     * edges follow the blue-strip midpoints (already inside the envelope). */
    self->bgLCurtainVerts[0].x = TO_FIXED(TC_SAFE_X0);
    self->bgLCurtainVerts[0].y = TO_FIXED(TC_SAFE_Y0);
    self->bgLCurtainVerts[1].x = (self->stripVertsBlue[1].x + self->stripVertsBlue[0].x) >> 1;
    self->bgLCurtainVerts[1].y = TO_FIXED(TC_SAFE_Y0);
    self->bgLCurtainVerts[2].x = (self->stripVertsBlue[3].x + self->stripVertsBlue[2].x) >> 1;
    self->bgLCurtainVerts[2].y = TO_FIXED(TC_SAFE_Y1);
    self->bgLCurtainVerts[3].x = TO_FIXED(TC_SAFE_X0);
    self->bgLCurtainVerts[3].y = TO_FIXED(TC_SAFE_Y1);

    self->bgRCurtainVerts[0].x = (self->stripVertsBlue[1].x + self->stripVertsBlue[0].x) >> 1;
    self->bgRCurtainVerts[0].y = TO_FIXED(TC_SAFE_Y0);
    self->bgRCurtainVerts[1].x = TO_FIXED(TC_SAFE_X1);
    self->bgRCurtainVerts[1].y = TO_FIXED(TC_SAFE_Y0);
    self->bgRCurtainVerts[2].x = TO_FIXED(TC_SAFE_X1);
    self->bgRCurtainVerts[2].y = TO_FIXED(TC_SAFE_Y1);
    self->bgRCurtainVerts[3].x = (self->stripVertsBlue[3].x + self->stripVertsBlue[2].x) >> 1;
    self->bgRCurtainVerts[3].y = TO_FIXED(TC_SAFE_Y1);
}

void TitleCard_SetupTitleWords(void)
{
    EntityTitleCard *self = &g_titlecard;
    int center_x = g_rsdk_screen.center_x;

    if (!self->zoneName.chars)
        rsdk_init_string(&self->zoneName, "UNTITLED", 0);

    rsdk_set_sprite_string(&g_titlecard_atlas, TC_ANIM_NAME, &self->zoneName);

    int32_t offset = TO_FIXED(40);
    for (int32_t c= 0; c < self->zoneName.length && c < 20; ++c) {
        self->charPos[c].y = offset;
        self->charVel[c]   = -TO_FIXED(8);
        offset += TO_FIXED(16);
    }

    for (int32_t i= 0; i < 4; ++i) {
        self->zoneCharPos[i] = ((2 - self->zoneName.length) << 19) - ((i * 2) << 19);
        self->zoneCharVel[i] = TO_FIXED(4);
    }

    for (int32_t c= 0; c < self->zoneName.length; ++c) {
        if (self->zoneName.chars[c] == (uint16_t)-1)
            self->titleCardWord2 = c + 1;
    }

    if (self->titleCardWord2) {
        self->word1Width = TO_FIXED(rsdk_get_string_width(&g_titlecard_atlas, TC_ANIM_NAME,
                                    &self->zoneName, 0, self->titleCardWord2 - 1, 1) + 24);
        self->word2Width = TO_FIXED(rsdk_get_string_width(&g_titlecard_atlas, TC_ANIM_NAME,
                                    &self->zoneName, self->titleCardWord2, 0, 1) + 24);
    }
    else {
        self->word2Width = TO_FIXED(rsdk_get_string_width(&g_titlecard_atlas, TC_ANIM_NAME,
                                    &self->zoneName, 0, 0, 1) + 24);
    }

    self->zoneXPos  = TO_FIXED(center_x - ((center_x - 160) >> 3) + 72);
    self->word2XPos = TO_FIXED(center_x - ((center_x - 160) >> 3) + 72);

    if (self->word2Width < TO_FIXED(128))
        self->word2XPos -= TO_FIXED(40);

    self->word1XPos = self->word1Width - self->word2Width + self->word2XPos - TO_FIXED(32);
}

void TitleCard_HandleWordMovement(void)
{
    EntityTitleCard *self = &g_titlecard;

    if (self->titleCardWord2 > 0) {
        self->word1DecorVerts[1].x -= TO_FIXED(32);
        if (self->word1DecorVerts[1].x < self->word1XPos - TO_FIXED(16))
            self->word1DecorVerts[1].x = self->word1XPos - TO_FIXED(16);
        self->word1DecorVerts[0].x = self->word1DecorVerts[1].x - self->word1Width;

        self->word1DecorVerts[2].x -= TO_FIXED(32);
        if (self->word1DecorVerts[2].x < self->word1XPos)
            self->word1DecorVerts[2].x = self->word1XPos;
        self->word1DecorVerts[3].x = self->word1DecorVerts[2].x - self->word1Width;
    }

    self->word2DecorVerts[1].x -= TO_FIXED(32);
    if (self->word2DecorVerts[1].x < self->word2XPos - TO_FIXED(16))
        self->word2DecorVerts[1].x = self->word2XPos - TO_FIXED(16);
    self->word2DecorVerts[0].x = self->word2DecorVerts[1].x - self->word2Width;

    self->word2DecorVerts[2].x -= TO_FIXED(32);
    if (self->word2DecorVerts[2].x < self->word2XPos)
        self->word2DecorVerts[2].x = self->word2XPos;
    self->word2DecorVerts[3].x = self->word2DecorVerts[2].x - self->word2Width;

    self->zoneDecorVerts[1].x += TO_FIXED(32);
    if (self->zoneDecorVerts[1].x > self->zoneXPos - TO_FIXED(8))
        self->zoneDecorVerts[1].x = self->zoneXPos - TO_FIXED(8);
    self->zoneDecorVerts[0].x = self->zoneDecorVerts[1].x - TO_FIXED(120);

    self->zoneDecorVerts[2].x += TO_FIXED(32);
    if (self->zoneDecorVerts[2].x > self->zoneXPos)
        self->zoneDecorVerts[2].x = self->zoneXPos;
    self->zoneDecorVerts[3].x = self->zoneDecorVerts[2].x - TO_FIXED(120);

    if (self->decorationPos.y < TO_FIXED(12)) {
        self->decorationPos.x += TO_FIXED(2);
        self->decorationPos.y += TO_FIXED(2);
    }
}

void TitleCard_HandleZoneCharMovement(void)
{
    EntityTitleCard *self = &g_titlecard;

    for (int32_t c= 0; c < self->zoneName.length && c < 20; ++c) {
        if (self->charPos[c].y < 0)
            self->charVel[c] += 0x28000;

        self->charPos[c].y += self->charVel[c];
        if (self->charPos[c].y > 0 && self->charVel[c] > 0)
            self->charPos[c].y = 0;
    }

    for (int32_t i= 0; i < 4; ++i) {
        if (self->zoneCharPos[i] > 0)
            self->zoneCharVel[i] -= 0x14000;

        self->zoneCharPos[i] += self->zoneCharVel[i];
        if (self->zoneCharPos[i] < 0 && self->zoneCharVel[i] < 0)
            self->zoneCharPos[i] = 0;
    }
}

void TitleCard_HandleCamera(void)
{
    /* Saturn-fit: single fixed camera; decomp zeroes player->camera->offset.y
     * here. No-op. */
}

/* === States (decomp TitleCard_State_*) =============================== */

void TitleCard_State_SetupBGElements(void)
{
    EntityTitleCard *self = &g_titlecard;

    /* decomp Zone_ApplyWorldBounds() — no-op on Saturn. */

    /* decomp SetEngineState(PAUSED) (gated on !atlEnabled && !suppress, both
     * FALSE on Saturn -> always paused). */
    g_titlecard_active = 1;

    self->timer += 24;
    if (self->timer >= 512) {
        self->word1DecorVerts[0].y += TO_FIXED(32);
        self->word1DecorVerts[1].y += TO_FIXED(32);
        self->word1DecorVerts[2].y += TO_FIXED(32);
        self->word1DecorVerts[3].y += TO_FIXED(32);

        self->word2DecorVerts[0].y -= TO_FIXED(32);
        self->word2DecorVerts[1].y -= TO_FIXED(32);
        self->word2DecorVerts[2].y -= TO_FIXED(32);
        self->word2DecorVerts[3].y -= TO_FIXED(32);

        self->zoneDecorVerts[0].y += TO_FIXED(32);
        self->zoneDecorVerts[1].y += TO_FIXED(32);
        self->zoneDecorVerts[2].y += TO_FIXED(32);
        self->zoneDecorVerts[3].y += TO_FIXED(32);

        self->state = TC_STATE_OPENINGBG;
    }

    self->word1DecorVerts[0].x += TO_FIXED(40);
    self->word1DecorVerts[1].x += TO_FIXED(40);
    self->word1DecorVerts[2].x += TO_FIXED(40);
    self->word1DecorVerts[3].x += TO_FIXED(40);

    self->word2DecorVerts[0].x += TO_FIXED(40);
    self->word2DecorVerts[1].x += TO_FIXED(40);
    self->word2DecorVerts[2].x += TO_FIXED(40);
    self->word2DecorVerts[3].x += TO_FIXED(40);

    self->zoneDecorVerts[0].x -= TO_FIXED(40);
    self->zoneDecorVerts[1].x -= TO_FIXED(40);
    self->zoneDecorVerts[2].x -= TO_FIXED(40);
    self->zoneDecorVerts[3].x -= TO_FIXED(40);
}

void TitleCard_State_OpeningBG(void)
{
    EntityTitleCard *self = &g_titlecard;

    if (self->timer >= 1024) {
        self->state     = TC_STATE_ENTERTITLE;
        self->stateDraw = TC_DRAW_SHOWCARD;
    }
    else {
        self->timer += 32;
    }

    TitleCard_HandleWordMovement();
}

void TitleCard_State_EnterTitle(void)
{
    EntityTitleCard *self = &g_titlecard;

    self->vertMovePos[0].x += (self->vertTargetPos[0].x - self->vertMovePos[0].x - TO_FIXED(16)) / 6;
    if (self->vertMovePos[0].x < self->vertTargetPos[0].x)
        self->vertMovePos[0].x = self->vertTargetPos[0].x;

    self->vertMovePos[0].y += (self->vertTargetPos[0].y - self->vertMovePos[0].y - TO_FIXED(16)) / 6;
    if (self->vertMovePos[0].y < self->vertTargetPos[0].y)
        self->vertMovePos[0].y = self->vertTargetPos[0].y;

    self->vertMovePos[1].x += (self->vertTargetPos[1].x - self->vertMovePos[1].x - TO_FIXED(16)) / 6;
    if (self->vertMovePos[1].x < self->vertTargetPos[1].x)
        self->vertMovePos[1].x = self->vertTargetPos[1].x;

    self->vertMovePos[1].y += (self->vertTargetPos[1].y - self->vertMovePos[1].y - TO_FIXED(16)) / 6;
    if (self->vertMovePos[1].y < self->vertTargetPos[1].y)
        self->vertMovePos[1].y = self->vertTargetPos[1].y;

    self->stripVertsBlue[0].x = (self->vertMovePos[0].x - TO_FIXED(240)) + self->stripVertsBlue[3].x;
    self->stripVertsBlue[0].y = self->vertMovePos[0].x;
    self->stripVertsBlue[1].x = (self->vertMovePos[0].x - TO_FIXED(240)) + self->stripVertsBlue[2].x;
    self->stripVertsBlue[1].y = self->vertMovePos[0].x;

    self->stripVertsRed[0].x = (self->vertMovePos[0].y - TO_FIXED(240)) + self->stripVertsRed[3].x;
    self->stripVertsRed[0].y = self->vertMovePos[0].y;
    self->stripVertsRed[1].x = (self->vertMovePos[0].y - TO_FIXED(240)) + self->stripVertsRed[2].x;
    self->stripVertsRed[1].y = self->vertMovePos[0].y;

    self->stripVertsOrange[0].x = (self->vertMovePos[1].x - TO_FIXED(240)) + self->stripVertsOrange[3].x;
    self->stripVertsOrange[0].y = self->vertMovePos[1].x;
    self->stripVertsOrange[1].x = (self->vertMovePos[1].x - TO_FIXED(240)) + self->stripVertsOrange[2].x;
    self->stripVertsOrange[1].y = self->vertMovePos[1].x;

    self->stripVertsGreen[0].x = (self->vertMovePos[1].y - TO_FIXED(240)) + self->stripVertsGreen[3].x;
    self->stripVertsGreen[0].y = self->vertMovePos[1].y;
    self->stripVertsGreen[1].x = (self->vertMovePos[1].y - TO_FIXED(240)) + self->stripVertsGreen[2].x;
    self->stripVertsGreen[1].y = self->vertMovePos[1].y;

    TitleCard_HandleWordMovement();
    TitleCard_HandleZoneCharMovement();

    if (self->actNumScale < 0x300)
        self->actNumScale += 0x40;

    if (!self->zoneCharPos[3] && self->zoneCharVel[3] < 0)
        self->state = TC_STATE_SHOWING;
}

void TitleCard_State_ShowingTitle(void)
{
    EntityTitleCard *self = &g_titlecard;

    TitleCard_HandleCamera();

    if (self->actionTimer >= 60) {
        self->actionTimer = 0;
        self->state       = TC_STATE_SLIDEAWAY;
        self->stateDraw   = TC_DRAW_SLIDEAWAY;
        /* decomp SetEngineState(REGULAR) — unfreeze the Player tick. */
        g_titlecard_active = 0;
    }
    else {
        self->actionTimer++;
        /* decomp actionTimer==16 ATL camera handoff omitted (no ATL on
         * Saturn; single camera). */
    }
}

void TitleCard_State_SlideAway(void)
{
    EntityTitleCard *self = &g_titlecard;

    int32_t speed = ++self->actionTimer << 18;
    self->stripVertsGreen[0].x -= speed;
    self->stripVertsGreen[0].y -= speed;
    self->stripVertsGreen[1].x -= speed;
    self->stripVertsGreen[1].y -= speed;
    self->stripVertsGreen[2].x -= speed;
    self->stripVertsGreen[2].y -= speed;
    self->stripVertsGreen[3].x -= speed;
    self->stripVertsGreen[3].y -= speed;

    if (self->actionTimer > 6) {
        speed = (self->actionTimer - 6) << 18;
        self->stripVertsOrange[0].x -= speed;
        self->stripVertsOrange[0].y -= speed;
        self->stripVertsOrange[1].x -= speed;
        self->stripVertsOrange[1].y -= speed;
        self->stripVertsOrange[2].x -= speed;
        self->stripVertsOrange[2].y -= speed;
        self->stripVertsOrange[3].x -= speed;
        self->stripVertsOrange[3].y -= speed;
        self->decorationPos.x += speed;
        self->decorationPos.y += speed;
    }

    if (self->actionTimer > 12) {
        speed = (self->actionTimer - 12) << 18;
        self->stripVertsRed[0].x -= speed;
        self->stripVertsRed[0].y -= speed;
        self->stripVertsRed[1].x -= speed;
        self->stripVertsRed[1].y -= speed;
        self->stripVertsRed[2].x -= speed;
        self->stripVertsRed[2].y -= speed;
        self->stripVertsRed[3].x -= speed;
        self->stripVertsRed[3].y -= speed;
    }

    if (self->actionTimer > 18) {
        speed = (self->actionTimer - 12) << 18;
        self->stripVertsBlue[0].x -= speed;
        self->stripVertsBlue[0].y -= speed;
        self->stripVertsBlue[1].x -= speed;
        self->stripVertsBlue[1].y -= speed;
        self->stripVertsBlue[2].x -= speed;
        self->stripVertsBlue[2].y -= speed;
        self->stripVertsBlue[3].x -= speed;
        self->stripVertsBlue[3].y -= speed;
    }

    if (self->actionTimer > 4) {
        speed = (self->actionTimer - 4) << 17;

        self->bgLCurtainVerts[0].x -= speed;
        self->bgLCurtainVerts[1].x -= speed;
        self->bgLCurtainVerts[2].x -= speed;
        self->bgLCurtainVerts[3].x -= speed;

        self->bgRCurtainVerts[0].x += speed;
        self->bgRCurtainVerts[1].x += speed;
        self->bgRCurtainVerts[2].x += speed;
        self->bgRCurtainVerts[3].x += speed;
    }

    if (self->actionTimer > 60) {
        speed = TO_FIXED(32);

        self->zoneXPos -= speed;
        self->word1XPos -= speed;
        self->word2XPos += speed;
        self->actNumPos.x += speed;
        self->actNumPos.y += speed;

        self->word1DecorVerts[0].x -= speed;
        self->word1DecorVerts[1].x -= speed;
        self->word1DecorVerts[2].x -= speed;
        self->word1DecorVerts[3].x -= speed;

        self->word2DecorVerts[0].x += speed;
        self->word2DecorVerts[1].x += speed;
        self->word2DecorVerts[2].x += speed;
        self->word2DecorVerts[3].x += speed;

        self->zoneDecorVerts[0].x -= speed;
        self->zoneDecorVerts[1].x -= speed;
        self->zoneDecorVerts[2].x -= speed;
        self->zoneDecorVerts[3].x -= speed;
    }

    /* decomp actionTimer==60 SceneInfo->timeEnabled = true (gameplay timer
     * start) — handled by the engine state going REGULAR already. */

    if (self->actionTimer > 80) {
        /* decomp globals cleanup + destroyEntity(self). */
        self->state     = TC_STATE_DONE;
        self->stateDraw = TC_DRAW_NONE;
    }
}

void TitleCard_State_Supressed(void)
{
    EntityTitleCard *self = &g_titlecard;

    TitleCard_HandleCamera();
    /* decomp SetEngineState(REGULAR). */
    g_titlecard_active = 0;
    self->state     = TC_STATE_DONE;
    self->stateDraw  = TC_DRAW_NONE;
}

/* === Draw states (decomp TitleCard_Draw_*) =========================== */

void TitleCard_Draw_SlideIn(void)
{
    EntityTitleCard *self = &g_titlecard;
    int cy = g_rsdk_screen.center_y;
    int sx = g_rsdk_screen.size_x;
    int sy = g_rsdk_screen.size_y;

    /* The big ol' BG (decomp L686-714; atlEnabled/suppress always FALSE). */
    if (self->timer < 256)
        tc_draw_rect_px(0, 0, sx, sy, 0x000000, TCZ_BG_BLACK);

    int height = self->timer;       /* Blue */
    if (self->timer < 512)
        tc_draw_rect_px(0, cy - (height >> 1), sx, height, TC_BLUE, TCZ_BG_BLUE);

    height = self->timer - 128;     /* Red */
    if (self->timer > 128 && self->timer < 640)
        tc_draw_rect_px(0, cy - (height >> 1), sx, height, TC_RED, TCZ_BG_RED);

    height = self->timer - 256;     /* Orange */
    if (self->timer > 256 && self->timer < 768)
        tc_draw_rect_px(0, cy - (height >> 1), sx, height, TC_ORANGE, TCZ_BG_ORANGE);

    height = self->timer - 384;     /* Green */
    if (self->timer > 384 && self->timer < 896)
        tc_draw_rect_px(0, cy - (height >> 1), sx, height, TC_GREEN, TCZ_BG_GREEN);

    height = self->timer - 512;     /* Yellow */
    if (self->timer > 512)
        tc_draw_rect_px(0, cy - (height >> 1), sx, height, TC_YELLOW, TCZ_BG_YELLOW);

    /* BG thingos. */
    if (self->titleCardWord2 > 0)
        tc_draw_face(self->word1DecorVerts, 0x000000, TCZ_DECORBOX1);
    tc_draw_face(self->word2DecorVerts, 0x000000, TCZ_DECORBOX1);
    tc_draw_face(self->zoneDecorVerts, 0xF0F0F0, TCZ_DECORBOX2);

    /* Act Number "decoration" plate frame 1 ("Sonic Mania"). */
    tc_draw_sprite(TC_ANIM_DECORATION, 1, self->decorationPos.x, self->decorationPos.y, TCZ_DECOR_SPR);
}

void TitleCard_Draw_ShowTitleCard(void)
{
    EntityTitleCard *self = &g_titlecard;
    int sx = g_rsdk_screen.size_x;
    int sy = g_rsdk_screen.size_y;

    /* Yellow BG (decomp L753-755). The decomp gates this on
     * !globals->atlEnabled && !globals->suppressTitlecard; on Saturn neither
     * global exists (no ATL/encore filter; suppression is the SUPRESSED
     * stateDraw, which never routes here), so it is unconditional. This
     * full-screen fill was MISSING from the original port -- without it the
     * showing-state card had no background and GHZ scenery showed through the
     * strip gaps. Painted as <=48px bands via tc_fill_bg: a single full-HEIGHT
     * axis-aligned quad at the card plane is dropped by SGL once the strips/
     * decor are co-submitted (see tc_fill_bg root-cause comment). */
    tc_fill_bg(TC_YELLOW, TCZ_BG_YELLOW);

    /* Strips (decomp L757-771). */
    if (self->vertMovePos[1].x < TO_FIXED(240))
        tc_draw_face(self->stripVertsOrange, TC_ORANGE, TCZ_STRIPS);
    if (self->vertMovePos[1].y < TO_FIXED(240))
        tc_draw_face(self->stripVertsGreen, TC_GREEN, TCZ_STRIPS);
    if (self->vertMovePos[0].y < TO_FIXED(240))
        tc_draw_face(self->stripVertsRed, TC_RED, TCZ_STRIPS);
    if (self->vertMovePos[0].x < TO_FIXED(240))
        tc_draw_face(self->stripVertsBlue, TC_BLUE, TCZ_STRIPS);

    /* "Sonic Mania" (decoration frame 1). */
    tc_draw_sprite(TC_ANIM_DECORATION, 1, self->decorationPos.x, self->decorationPos.y, TCZ_DECOR_SPR);

    /* BG thingos. */
    if (self->titleCardWord2 > 0)
        tc_draw_face(self->word1DecorVerts, 0x000000, TCZ_DECORBOX1);
    tc_draw_face(self->word2DecorVerts, 0x000000, TCZ_DECORBOX1);
    tc_draw_face(self->zoneDecorVerts, 0xF0F0F0, TCZ_DECORBOX2);

    /* "ZONE" (decomp L791-799; clip 0,170..size,YSIZE). */
    rsdk_set_clip_bounds(0, 170, sx, sy);
    for (int32_t i= 0; i < 4; ++i) {
        int32_t dy = TO_FIXED(186) + self->zoneCharPos[i];
        tc_draw_sprite(TC_ANIM_ZONE, i, self->zoneXPos, dy, TCZ_ZONE_LTR);
    }

    /* TitleCard word 1 (only when titleCardWord2 > 0; not taken for the
     * single-word "GREEN HILL"). */
    if (self->titleCardWord2 > 0) {
        rsdk_set_clip_bounds(0, 0, sx, 130);
        for (int32_t c= 0; c < self->zoneName.length && c < 20; ++c)
            s_char_off_y[c] = (int16_t)(self->charPos[c].y >> 16);
        rsdk_set_text_depth(TCZ_NAME_TEXT);
        rsdk_draw_text(&g_titlecard_atlas, TC_ANIM_NAME,
                       self->word1XPos - TO_FIXED(20), TO_FIXED(114),
                       &self->zoneName, 0, self->titleCardWord2,
                       RSDK_ALIGN_CENTER, 1, NULL, s_char_off_y, 1);
    }

    /* TitleCard word 2 / whole zone name (decomp L810-813). */
    rsdk_set_clip_bounds(0, 0, sx, 170);
    for (int32_t c= 0; c < self->zoneName.length && c < 20; ++c)
        s_char_off_y[c] = (int16_t)(self->charPos[c].y >> 16);
    rsdk_set_text_depth(TCZ_NAME_TEXT);
    rsdk_draw_text(&g_titlecard_atlas, TC_ANIM_NAME,
                   self->word2XPos - TO_FIXED(20), TO_FIXED(154),
                   &self->zoneName, self->titleCardWord2, 0,
                   RSDK_ALIGN_CENTER, 1, NULL, s_char_off_y, 1);

    rsdk_set_clip_bounds(0, 0, sx, sy);

    /* Act Number (decomp L818-835). FX_SCALE grow-in omitted — draw at full
     * size once actNumScale > 0 (Saturn-fit: no jo per-sprite scale draw). */
    if (self->actID != 3 && self->actNumScale > 0) {
        tc_draw_sprite(TC_ANIM_DECORATION, 0, self->actNumPos.x, self->actNumPos.y, TCZ_ACTNUM);
        tc_draw_sprite(TC_ANIM_ACTNUM, self->actID, self->actNumPos.x, self->actNumPos.y, TCZ_ACTNUM);
    }
}

void TitleCard_Draw_SlideAway(void)
{
    EntityTitleCard *self = &g_titlecard;

    /* Yellow BG curtain "opening" (decomp L857-861). */
    tc_draw_face(self->bgLCurtainVerts, TC_YELLOW, TCZ_BG_YELLOW);
    tc_draw_face(self->bgRCurtainVerts, TC_YELLOW, TCZ_BG_YELLOW);

    /* Strips. */
    if (self->vertMovePos[1].x < TO_FIXED(240))
        tc_draw_face(self->stripVertsOrange, TC_ORANGE, TCZ_STRIPS);
    if (self->vertMovePos[1].y < TO_FIXED(240))
        tc_draw_face(self->stripVertsGreen, TC_GREEN, TCZ_STRIPS);
    if (self->vertMovePos[0].y < TO_FIXED(240))
        tc_draw_face(self->stripVertsRed, TC_RED, TCZ_STRIPS);
    if (self->vertMovePos[0].x < TO_FIXED(240))
        tc_draw_face(self->stripVertsBlue, TC_BLUE, TCZ_STRIPS);

    /* "Sonic Mania". */
    tc_draw_sprite(TC_ANIM_DECORATION, 1, self->decorationPos.x, self->decorationPos.y, TCZ_DECOR_SPR);

    /* Act Number (decomp L890-898; plain, no FX_SCALE). */
    if (self->actID != 3 && self->actNumScale > 0) {
        tc_draw_sprite(TC_ANIM_DECORATION, 0, self->actNumPos.x, self->actNumPos.y, TCZ_ACTNUM);
        tc_draw_sprite(TC_ANIM_ACTNUM, self->actID, self->actNumPos.x, self->actNumPos.y, TCZ_ACTNUM);
    }

    /* BG thingos. */
    if (self->titleCardWord2 > 0)
        tc_draw_face(self->word1DecorVerts, 0x000000, TCZ_DECORBOX1);
    tc_draw_face(self->word2DecorVerts, 0x000000, TCZ_DECORBOX1);
    tc_draw_face(self->zoneDecorVerts, 0xF0F0F0, TCZ_DECORBOX2);

    /* "ZONE" (decomp L907-914; no zoneCharPos offset, no clip). */
    for (int32_t i= 0; i < 4; ++i)
        tc_draw_sprite(TC_ANIM_ZONE, i, self->zoneXPos, TO_FIXED(186), TCZ_ZONE_LTR);

    /* Word 1 (only when titleCardWord2 > 0). */
    if (self->titleCardWord2 > 0) {
        rsdk_set_text_depth(TCZ_NAME_TEXT);
        rsdk_draw_text(&g_titlecard_atlas, TC_ANIM_NAME,
                       self->word1XPos - TO_FIXED(20), TO_FIXED(114),
                       &self->zoneName, 0, self->titleCardWord2,
                       RSDK_ALIGN_CENTER, 1, NULL, NULL, 1);
    }

    /* Word 2 / whole zone name (decomp L924-926; char_off NULL here). */
    rsdk_set_text_depth(TCZ_NAME_TEXT);
    rsdk_draw_text(&g_titlecard_atlas, TC_ANIM_NAME,
                   self->word2XPos - TO_FIXED(20), TO_FIXED(154),
                   &self->zoneName, self->titleCardWord2, 0,
                   RSDK_ALIGN_CENTER, 1, NULL, NULL, 1);
}

/* === RSDK class callbacks (inert under the Bridge-model; no TitleCard
 *     slot entity is ever created, so these are never invoked via
 *     rsdk_object_tick. Defined + `used` for the gate P1 symbol set +
 *     register_ex). ==================================================== */

__attribute__((used)) void TitleCard_Update(void) {}
__attribute__((used)) void TitleCard_LateUpdate(void) {}
__attribute__((used)) void TitleCard_StaticUpdate(void) {}
__attribute__((used)) void TitleCard_Draw(void) {}
__attribute__((used)) void TitleCard_Create(void *data) { (void)data; }
__attribute__((used)) void TitleCard_StageLoad(void) {}

/* === Saturn-fit drive surface ======================================== */

/* Phase 2.4j.2 -- LWRAM scratch for the TITLCARD.SP2/.MET read. The card
 * loads mid-GHZ, when jo's 256 KB malloc pool is already saturated by the
 * FG.* residue + entity SPRs; jo_fs_read_file's internal jo_malloc(50721)
 * then fails (GFS *finds* the file, fid=97, but the buffer alloc returns 0 --
 * proven via the 2.4j.2 diagnostic latch dbg[1]=97, dbg[3]=0). This is the
 * same jo-pool-exhaustion bug class as FG.TMP/SKY.DAT, so we apply the same
 * fix: read straight into a reserved LWRAM region via entity_atlas_load_ex
 * (jo_fs_read_file_ptr buffer path, no jo_malloc).
 *
 * LWRAM map (Work RAM-L 0x00200000..0x002FFFFF, 1 MB; see scene_ghz.c L75-77
 * + memory/ghz-sky-dat-lwram-bypass.md; ST-097-R5-072694.pdf S2.1):
 *   0x00200000..0x0020FFFF (64 KB)  GHZ1SURF.BIN
 *   0x00210000..0x0025FFFF (320 KB) GHZ?FG.TMP
 *   0x00260000..0x00277FFF (96 KB)  GHZ?SKY.DAT
 *   0x00278000..0x00287FFF (64 KB)  TITLCARD scratch  <-- this region
 * 64 KB covers the 50720-byte SP2 with margin; the ~500-byte MET reuses it. */
#define TITLCARD_SCRATCH_LWRAM_ADDR ((void *)0x00278000)
#define TITLCARD_SCRATCH_LWRAM_SIZE 0x10000

bool titlecard_load_assets(void)
{
    if (g_titlecard_atlas.ready)
        return true;
    /* 8.3-compliant base: SGL GFS fname[] is GFS_FNAME_LEN=12
     * (SEGA_GFS.H:37). "TITLECARD.SP2" (13 chars) overflows it so
     * GFS_NameToId never matches and jo_fs_read_file returns NULL.
     * "TITLCARD.SP2" = 12 chars fits. (Phase 2.4j.2) */
    if (entity_atlas_load_ex(&g_titlecard_atlas, "TITLCARD",
                             TITLCARD_SCRATCH_LWRAM_ADDR,
                             TITLCARD_SCRATCH_LWRAM_SIZE)) {
        entity_atlas_play(&g_titlecard_atlas, 0);
        return true;
    }
    return false;
}

void titlecard_spawn(const char *zone_name, uint8_t actID)
{
    EntityTitleCard *self = &g_titlecard;

    memset(self, 0, sizeof(*self));

    /* decomp Create init (L34-90, Plus branches omitted). */
    self->actID = actID;
    if (self->actID > TC_ACT_NONE)
        self->actID = TC_ACT_NONE;

    rsdk_init_string(&self->zoneName, zone_name, 0);

    self->state     = TC_STATE_SETUPBG;
    self->stateDraw = TC_DRAW_SLIDEIN;

    int center_x = g_rsdk_screen.center_x;
    self->stripPos[0] = TO_FIXED(center_x - 152);
    self->stripPos[1] = TO_FIXED(center_x - 152);
    self->stripPos[2] = TO_FIXED(center_x - 160);
    self->stripPos[3] = TO_FIXED(center_x + 20);

    TitleCard_SetupTitleWords();
    TitleCard_SetupVertices();

    self->decorationPos.y = -TO_FIXED(52);
    self->decorationPos.x = TO_FIXED(g_rsdk_screen.size_x - 160);

    self->actNumPos.y = TO_FIXED(168);
    self->actNumPos.x = TO_FIXED(center_x + 106);
    self->actNumScale = -0x400;

    if (self->word2XPos - self->word2Width < TO_FIXED(16)) {
        int32_t dist = (self->word2XPos - self->word2Width) - TO_FIXED(16);
        self->word1XPos -= dist;
        self->zoneXPos -= dist;
        self->actNumPos.x -= dist;
        self->word2XPos = self->word2XPos - dist;
    }

    /* PAUSED hold begins immediately (engine is frozen while the card
     * slides in). Cleared at ShowingTitle -> SlideAway. */
    g_titlecard_active = 1;
}

void titlecard_tick(void)
{
    EntityTitleCard *self = &g_titlecard;

    switch (self->state) {
        case TC_STATE_SETUPBG:   TitleCard_State_SetupBGElements(); break;
        case TC_STATE_OPENINGBG: TitleCard_State_OpeningBG();       break;
        case TC_STATE_ENTERTITLE:TitleCard_State_EnterTitle();      break;
        case TC_STATE_SHOWING:   TitleCard_State_ShowingTitle();    break;
        case TC_STATE_SLIDEAWAY: TitleCard_State_SlideAway();       break;
        case TC_STATE_SUPRESSED: TitleCard_State_Supressed();       break;
        case TC_STATE_DONE:
        default:                                                    break;
    }
}

void titlecard_draw_only(void)
{
    EntityTitleCard *self = &g_titlecard;

    if (self->state == TC_STATE_DONE)
        return;

    switch (self->stateDraw) {
        case TC_DRAW_SLIDEIN:   TitleCard_Draw_SlideIn();      break;
        case TC_DRAW_SHOWCARD:  TitleCard_Draw_ShowTitleCard();break;
        case TC_DRAW_SLIDEAWAY: TitleCard_Draw_SlideAway();    break;
        case TC_DRAW_NONE:
        default:                                               break;
    }

    /* Reset the shared depth setters to their defaults (other draw paths
     * — menus/UIWidgets/HUD — expect poly_z 0 + text_depth 140). */
    rsdk_drawing_set_poly_z(0);
    rsdk_set_text_depth(140);
}
