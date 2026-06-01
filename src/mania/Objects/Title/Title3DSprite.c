/* Phase 1.32 — Title3DSprite: software-projected billboard formation.
 *
 * Decomp source of truth:
 *   tools/_decomp_raw/SonicMania_Objects_Title_Title3DSprite.c
 *
 * Per the decomp the title scene contains 58 Title3DSprite entities
 * (verified via tools/parse_title_entities.py against
 *  extracted/Data/Stages/Title/Scene1.bin) positioned in 3D-world space
 * around the orbit centre (0x2000000, 0x2000000) = (512, 512) world units.
 *
 * Per-frame:
 *   relativePos.x = (-((y>>8) * Sin1024(angle)) - ((x>>8) * Cos1024(angle))) >> 10
 *   relativePos.y = (+((y>>8) * Cos1024(angle)) - ((x>>8) * Sin1024(angle))) >> 10
 *   zdepth        = relativePos.y
 *
 * Per-draw:
 *   depth   = zdepth + baseDepth        (baseDepth = 0xA000)
 *   if depth >= 0x100:
 *     scale = MIN(0x18000 * islandSize / depth, 0x200)   (islandSize = 0x90)
 *     drawPos.x = islandSize * relativePos.x / depth + ScreenInfo.center.x
 *     drawPos.y = islandSize * height / depth + 152      (height = 0x2800)
 *     DrawSprite(useDelta=true)         <- scale is applied via animator drawFX
 *
 * On Create:
 *   position.x -= 0x2000000              (translate orbit centre to origin)
 *   position.y -= 0x2000000
 *
 * Saturn port deviations (documented):
 *   1. The decomp's 58-entity ECS pass walks via foreach_active; the
 *      Saturn side parses Scene1.bin directly into a static array at
 *      boot so we don't pay ECS overhead per frame for what's a fixed
 *      formation. Coords + frame come from
 *      tools/parse_title_entities.py output (verified slot 16 = MountainL
 *      at world (544,444), slot 21 = Bush at (408,493), etc.).
 *   2. Per-frame scaling (Phase 1.32b LANDED): the decomp uses RSDK
 *      DrawSprite useDelta=true which feeds the entity's scale.x/scale.y
 *      computed at Title3DSprite_Draw:32 into the GFX pipeline. On Saturn
 *      we feed the equivalent uniform H/V scale into slDispSprite via
 *      pos[S] (SGL FIXED Q16.16). The RSDK Q9.7 scale (0x200 = 1.0) is
 *      converted to Saturn FIXED by `(scale_q207 << 9)` (Q9.7 << 9 = Q16.16
 *      with 1.0 = 0x10000). See title3d_bb_draw_frame_scaled in
 *      TitleAssets.c for the SL_DEF.H:93 + ST-238-R1 p.65 citations.
 *   3. Drawing uses 4-bpp Color Bank 16 path (title3d_bb_draw_frame in
 *      TitleAssets.c) so the 5 billboard sprites share the same VDP1+CRAM
 *      infrastructure as TSONIC + ELECTRA.
 *   4. Z assignment: Saturn perspective sort uses smaller Z = closer
 *      (SGL ST-238-R1). Per-entity Z is derived from depth so closer
 *      billboards (smaller depth) get smaller Z. Range chosen so the
 *      formation sits BEHIND the title logo (z=180+ for logo elements)
 *      but in front of the RBG0 backdrop. */

#include "Title3DSprite.h"
#include "TitleAssets.h"
#include "../../../rsdk/animation.h"
#include "../../../rsdk/drawing.h"
#include "../../../rsdk/scene.h"
#include "../../../rsdk/object.h"
#include "../../../rsdk/math.h"

#include <stdint.h>
#include <string.h>

/* === 58-entity formation table — sourced from Scene1.bin via
 *     `python tools/parse_title_entities.py`. Each entry is the WORLD
 *     position in RSDK fixed-point (Q16.16) BEFORE the Create-time
 *     -0x2000000 origin shift. The runtime applies the shift once at
 *     boot in title3d_init_entities() so subsequent ticks see entity-
 *     centred (x, y) just like the decomp does after its Create. */

typedef struct {
    int32_t world_x;    /* Q16.16 world position (post -0x2000000 shift) */
    int32_t world_y;
    uint8_t frame;      /* 0..4 = MountainL/M/S/Tree/Bush                 */
} t3d_entity_seed_t;

/* Slot ordering matches Scene1.bin entity table; coordinates are the
 * RSDK on-disk integer position multiplied by 0x10000 (Q16.16) BEFORE
 * the Create-time origin shift of -0x2000000 (= -512 world units). */
#define WX(px) ((int32_t)((px) * 0x10000) - 0x2000000)
#define WY(py) ((int32_t)((py) * 0x10000) - 0x2000000)
#define E(px, py, fr) { WX(px), WY(py), (uint8_t)(fr) }

static const t3d_entity_seed_t s_t3d_seeds[] = {
    E(544, 444, 0), E(508, 588, 1), E(516, 580, 2), E(528, 524, 3),
    E(410, 498, 4), E(408, 493, 4), E(406, 486, 4), E(402, 478, 4),
    E(410, 474, 4), E(418, 474, 4), E(426, 478, 4), E(410, 482, 4),
    E(414, 490, 4), E(418, 482, 4), E(418, 494, 2), E(422, 486, 2),
    E(430, 478, 2), E(456, 480, 3), E(492, 456, 3), E(500, 468, 4),
    E(508, 460, 4), E(518, 455, 4), E(528, 452, 4), E(532, 464, 4),
    E(534, 475, 4), E(532, 484, 4), E(516, 464, 1), E(524, 472, 1),
    E(512, 476, 1), E(540, 504, 3), E(548, 540, 3), E(556, 452, 0),
    E(568, 444, 0), E(572, 452, 1), E(580, 444, 1), E(532, 436, 1),
    E(520, 432, 2), E(524, 440, 2), E(564, 460, 2), E(556, 464, 2),
    E(580, 504, 0), E(592, 512, 0), E(568, 504, 1), E(580, 516, 1),
    E(604, 512, 1), E(612, 516, 2), E(572, 524, 2), E(568, 512, 3),
    E(600, 520, 3), E(588, 448, 3), E(576, 568, 0), E(588, 572, 1),
    E(576, 576, 1), E(596, 576, 2), E(560, 576, 3), E(564, 552, 2),
    E(568, 540, 2), E(500, 588, 2),
};

#define T3D_ENTITY_COUNT ((int)(sizeof(s_t3d_seeds) / sizeof(s_t3d_seeds[0])))

/* Runtime per-entity state. Computed each Update from `(world_x, world_y,
 * TitleBG->angle)`. */
typedef struct {
    int32_t rel_x;       /* relativePos.x (signed; Q16-ish per decomp)    */
    int32_t rel_y;       /* relativePos.y                                 */
    int32_t zdepth;      /* = rel_y (decomp Title3DSprite.c:19)           */
    uint8_t frame;       /* 0..4 billboard variant                         */
} t3d_entity_state_t;

static t3d_entity_state_t s_t3d_state[T3D_ENTITY_COUNT];

/* TitleBG->angle — rotation driver. Decomp TitleBG_StaticUpdate (line 39-40)
 * increments by 1 each tick and masks to 10 bits (0..1023). On Saturn we
 * own this counter; Title3DSprite_Tick_All() advances it. */
int g_title_bg_angle = 0;

/* `ObjectTitle3DSprite *Title3DSprite` is defined in src/mania/Game.c
 * (line ~73) so that rsdk_object_register_ex can install the static_vars
 * pointer via `(void **)&Title3DSprite`. Don't redefine here. */

/* Decomp Title3DSprite_StageLoad:57-64. Saturn-side this just installs the
 * decomp's class constants into the static_vars block — `Title3DSprite`
 * points at the object storage allocated by rsdk_object_register_ex. */
void Title3DSprite_StageLoad(void)
{
    (void)rsdk_load_sprite_animation("Title/Background.bin", -1);

    if (Title3DSprite) {
        /* Phase 1.38 REVERTED 2026-05-28 — tuned constants
         * (islandSize=0x120, baseDepth=0x5000, scale cap 0x300) produced
         * full-screen blue-with-green-line blackout = Phase 2.3e SGL
         * sortlist overflow class. Same failure mode as Phase 1.37 VDP1
         * sprite attempt. Restored decomp defaults. */
        Title3DSprite->islandSize = 0x90;
        Title3DSprite->height     = 0x2800;
        Title3DSprite->baseDepth  = 0xA000;
    }
}

/* Decomp Title3DSprite_Create:42-55. Saturn-side keeps the entity as
 * ACTIVE_NEVER because the formation is driven by the static seed table +
 * Title3DSprite_Tick_All / Title3DSprite_Draw_All, NOT the ECS draw pass.
 * Marking ACTIVE_NEVER skips the per-entity Update/Draw callbacks (which
 * are no-ops on Saturn — see below) so we don't pay the ECS overhead. */
void Title3DSprite_Create(void *data)
{
    (void)data;
    RSDK_THIS(Title3DSprite);
    if (!SceneInfo->inEditor) {
        self->active    = ACTIVE_NEVER;
        self->visible   = false;
        self->drawGroup = 2;
        self->drawFX    = FX_NONE;
    }
}

/* The decomp's Update/Draw run per-entity; the Saturn build batches all
 * 58 via Title3DSprite_Tick_All + Title3DSprite_Draw_All so the per-entity
 * callbacks are no-ops. */
void Title3DSprite_Update(void)       { /* batched into Tick_All  */ }
void Title3DSprite_LateUpdate(void)   { /* decomp no-op           */ }
void Title3DSprite_StaticUpdate(void) { /* decomp no-op           */ }
void Title3DSprite_Draw(void)         { /* batched into Draw_All  */ }

/* === Saturn batched API ============================================== */

/* One-shot init: seed s_t3d_state[i].frame from the static seed table.
 * Called once after title_assets_load() succeeds. The rel_x/rel_y/zdepth
 * fields are computed on every Tick. */
static int s_t3d_inited = 0;

static void title3d_init_state(void)
{
    if (s_t3d_inited) return;
    for (int i = 0; i < T3D_ENTITY_COUNT; ++i) {
        s_t3d_state[i].frame  = s_t3d_seeds[i].frame;
        s_t3d_state[i].rel_x  = 0;
        s_t3d_state[i].rel_y  = 0;
        s_t3d_state[i].zdepth = 0;
    }
    s_t3d_inited = 1;
}

/* Per-frame batch update for all 58 entities. Ports the decomp's
 * Title3DSprite_Update math line-for-line:
 *
 *   self->relativePos.x = (-((y >> 8) * Sin1024(angle))
 *                          - ((x >> 8) * Cos1024(angle))) >> 10;
 *   self->relativePos.y = (+((y >> 8) * Cos1024(angle))
 *                          - ((x >> 8) * Sin1024(angle))) >> 10;
 *   self->zdepth = self->relativePos.y;
 *
 * Also advances g_title_bg_angle (decomp TitleBG_StaticUpdate:39-40). */
void Title3DSprite_Tick_All(void)
{
    if (!g_title3d_loaded) return;
    title3d_init_state();

    /* Decomp TitleBG_StaticUpdate:39-40 — ++angle; angle &= 0x3FF. */
    g_title_bg_angle = (g_title_bg_angle + 1) & 0x3FF;
    int32_t s = rsdk_sin1024(g_title_bg_angle);
    int32_t c = rsdk_cos1024(g_title_bg_angle);

    for (int i = 0; i < T3D_ENTITY_COUNT; ++i) {
        int32_t x = s_t3d_seeds[i].world_x;
        int32_t y = s_t3d_seeds[i].world_y;
        int32_t xs = x >> 8;
        int32_t ys = y >> 8;
        s_t3d_state[i].rel_x = (-(ys * s) - (xs * c)) >> 10;
        s_t3d_state[i].rel_y = ( (ys * c) - (xs * s)) >> 10;
        s_t3d_state[i].zdepth = s_t3d_state[i].rel_y;
    }
}

/* Per-frame batch draw. Ports the decomp's Title3DSprite_Draw math:
 *
 *   depth = zdepth + baseDepth;
 *   if (depth && depth >= 0x100) {
 *       scale = MIN(0x18000 * islandSize / depth, 0x200);
 *       drawPos.x = islandSize * relativePos.x / depth + ScreenInfo->center.x;
 *       drawPos.y = islandSize * height / depth + 152;
 *       DrawSprite(&self->animator, &drawPos, true);
 *   }
 *
 * Constants per StageLoad:57-64: islandSize=0x90, height=0x2800, baseDepth=0xA000.
 * ScreenInfo->center.x = 160 (320/2) on Saturn (Game.c:450).
 *
 * Saturn deviation 2 (documented above): constant-scale first cut — scale
 * computation is preserved as a comment + locally bound `scale` for the
 * follow-on iteration. The position math is the full decomp formula.
 *
 * Z mapping: per-entity Z = 195 + (depth >> 12) clamped to [185, 220]. Smaller
 * depth (closer to viewer) -> smaller Z (drawn IN FRONT on Saturn perspective
 * sort per SGL ST-238-R1). The base 195 keeps the formation BEHIND the
 * GAMETITLE (z=175) + RIBBON (z=180/185) + RINGBOT (z=195) + EMBLEM (z=200)
 * but in front of the rotating RBG0 backdrop. */
void Title3DSprite_Draw_All(void)
{
    if (!g_title3d_loaded) return;
    if (!Title3DSprite) return;

    int32_t islandSize = Title3DSprite->islandSize;  /* 0x90 */
    int32_t height     = Title3DSprite->height;      /* 0x2800 */
    int32_t baseDepth  = Title3DSprite->baseDepth;   /* 0xA000 */
    const int center_x = 160;     /* ScreenInfo->center.x (320/2)            */

    for (int i = 0; i < T3D_ENTITY_COUNT; ++i) {
        int32_t depth = s_t3d_state[i].zdepth + baseDepth;
        if (depth < 0x100) continue;

        /* Phase 1.32b — decomp Title3DSprite_Draw:32 scale math, verbatim:
         *   self->scale.x = MIN(0x18000 * islandSize / depth, 0x200);
         *   self->scale.y = self->scale.x;
         * RSDK scale is Q9.7 (0x200 = 1.0). Convert to Saturn FIXED Q16.16
         * via `<< 9` (Q9.7 << 9 = Q16.16; 0x200 << 9 = 0x40000 = 4.0... wait)
         *
         * RSDK convention: scale.x = 0x200 means 100% (per DrawSpriteFlipped
         * in _RSDKv5_Graphics_Drawing.cpp). Saturn slDispSprite pos[S] is
         * FIXED Q16.16 where 0x10000 = 100%. So the conversion is:
         *   hv_scale_q16 = scale_q207 * (0x10000 / 0x200) = scale_q207 << 7.
         * (0x200 -> 0x10000 is a 0x80 = 1<<7 multiplier, NOT << 9.)         */
        int32_t scale_q207 = (0x18000 * islandSize) / depth;
        if (scale_q207 > 0x200) scale_q207 = 0x200;
        int32_t hv_scale   = scale_q207 << 7;       /* Q9.7 -> Q16.16        */

        int32_t world_dx = (islandSize * s_t3d_state[i].rel_x) / depth;
        int32_t world_dy = (islandSize * height               ) / depth;

        int screen_x = (int)(world_dx + center_x);
        int screen_y = (int)(world_dy + 152);

        /* Convert RSDK screen-space (origin top-left) to jo canvas-centred
         * coords (origin centre). jo_x = screen_x - 160, jo_y = screen_y - 112.
         * jo's sprite-draw places the sprite CENTRE at (jo_x, jo_y); the .ATL
         * frame's RSDK pivot (px, py) shifts the canvas centre by (px+w/2,
         * py+h/2) from the entity origin, matching the FLIP_NONE branch of
         * RSDK DrawSprite. */
        int frame = s_t3d_state[i].frame;
        if (frame < 0) frame = 0;
        if (frame >= TITLE3D_BB_MAX_FRAMES) frame = TITLE3D_BB_MAX_FRAMES - 1;

        int fw = g_title3d_bb_frames[frame].width;
        int fh = g_title3d_bb_frames[frame].height;
        int px = g_title3d_bb_frames[frame].pivot_x;
        int py = g_title3d_bb_frames[frame].pivot_y;
        int canvas_cx = px + (fw >> 1);
        int canvas_cy = py + (fh >> 1);

        int jo_x = (screen_x + canvas_cx) - 160;
        int jo_y = (screen_y + canvas_cy) - 112;

        /* Per-entity Z: deeper (larger depth) draws further back. Map
         * (depth >> 12) into a small Z spread, then offset to land behind
         * the title logo. Range [185, 220] keeps us inside the same SGL
         * perspective bucket as the rest of the title scene. */
        int z = 200 + (depth >> 14);
        if (z < 185) z = 185;
        if (z > 220) z = 220;

        title3d_bb_draw_frame_scaled(frame, jo_x, jo_y, z, hv_scale);
    }
}
