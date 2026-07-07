/* Phase 2.4k -- StarPost.c
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_Global_StarPost.c`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Every function cites the decomp line range it mirrors. Saturn-side
 * adaptations are called out inline. Decomp file = StarPost.c:1-411.
 *
 * Translation notes:
 *
 *   PLAYER LOOP
 *     The decomp iterates PLAYER_COUNT (4) players. On Saturn there is one
 *     player (g_ghz_player_addr / g_ghz_player). foreach_active(Player) and
 *     RSDK_GET_ENTITY(SLOT_PLAYER1) both resolve to g_ghz_player.
 *     playerID is always 0 on Saturn.
 *
 *   Player_CheckCollisionTouch SUBSTITUTE
 *     The decomp uses Player_CheckCollisionTouch (a pure AABB overlap that
 *     does NOT snap the player). No such function exists on Saturn
 *     (Player.h only has Player_CheckCollisionBox which snaps). For StarPost
 *     touch detection we inline an AABB overlap test: compare the player's
 *     Q16.16 position against entity position + hitbox extents. No snap.
 *
 *   ANIMATORS
 *     The decomp drives three per-entity rsdk_animator_t fields (pole,
 *     ball, star). On Saturn the pole is static (anim 0 frame 0 always).
 *     The star frame is a simple index. Only the ball has a per-entity
 *     animator (self->ballAnimator) for correct per-entity spin cadence.
 *     The ball animator's frames ptr is wired to g_starpost_atlas scratch
 *     frames so rsdk_process_animation can walk the decomp per-frame
 *     durations.
 *
 *   globals->gameMode GUARDS
 *     No globals struct on Saturn. MODE_TIMEATTACK / MODE_COMPETITION never
 *     set. All guards collapse to "condition never true" and are omitted
 *     or noted as no-op.
 *
 *   SaveGame_SaveGameState / SetScene / Zone_StartFadeOut
 *     Blue Spheres not ported. CheckBonusStageEntry stubs these calls
 *     with no-ops so the orbit animation plays but no scene transition
 *     fires. Stubs are FIXME-marked for Phase Z.
 *
 *   API_UnlockAchievement
 *     No achievement system on Saturn. No-op stub.
 *
 *   RSDK.GetEntitySlot
 *     On Saturn playerID is always 0 (single player).
 *
 *   TMZ2Setup
 *     Always NULL/0 in GHZ; the "activate all prior posts" code always runs.
 *
 *   foreach_all(StarPost, starPost)
 *     Uses the Game.h foreach_all macro which calls rsdk_get_all_entity.
 */

#include "StarPost.h"
#include "../Common/Entities.h"
#include "../../../rsdk/entity_atlas.h"
#include "../../../rsdk/object.h"
#include "../../../rsdk/animation.h"
#include "../../../rsdk/math.h"
#include "../../../rsdk/drawing.h"

#include <jo/jo.h>
#include <string.h>

/* Screen half-dimensions (jo_sprite_draw3D centred origin). */
#define SCR_HALF_W 160
#define SCR_HALF_H 112

/* Atlas animation indices (decomp SetSpriteAnimation calls):
 *   anim 0 = pole (static, frame 0)
 *   anim 1 = ball idle (slow spin / static wait)
 *   anim 2 = ball activated / spinning
 *   anim 3 = stars (4 frames cycled by starFrameID)
 */
#define ANIM_POLE       0
#define ANIM_BALL_IDLE  1
#define ANIM_BALL_SPIN  2
#define ANIM_STARS      3

/* Z depth for draw (same drawGroup layer as other GHZ badniks). */
#define STARPOST_Z 145

/* One static object + one shared atlas for all StarPost instances. */
ObjectStarPost *StarPost;
entity_atlas_t  g_starpost_atlas;

/* -------------------------------------------------------------------------
 * Inline AABB touch test (replaces decomp Player_CheckCollisionTouch).
 *
 * Returns true when player's bounding box overlaps the given hitbox
 * centered at (ex_px, ey_px) in world pixel space. Player bounding box
 * is the standing Sonic hitbox: Player_FallbackHitbox {-10,-20,10,20}.
 * Pure overlap — no snap, no collision response.
 * -------------------------------------------------------------------------*/
static bool starpost_touch(player_t *p, int ex_px, int ey_px,
                           const hitbox_t *hb)
{
    /* Player center in pixels. */
    int px = p->position.x >> 16;
    int py = p->position.y >> 16;

    /* Player hitbox half-extents (standing Sonic). */
    int pl = px - 10;
    int pr = px + 10;
    int pt = py - 20;
    int pb = py + 20;

    /* Entity hitbox in pixels. */
    int el = ex_px + hb->left;
    int er = ex_px + hb->right;
    int et = ey_px + hb->top;
    int eb = ey_px + hb->bottom;

    return pr > el && pl < er && pb > et && pt < eb;
}

/* -------------------------------------------------------------------------
 * Wire the ball animator to point at g_starpost_atlas scratch frames for
 * the given anim_id so rsdk_process_animation can walk per-frame durations.
 * Called from Create and from CheckCollisions when switching ball anim.
 * -------------------------------------------------------------------------*/
static void wire_ball_animator(EntityStarPost *self, int anim_id)
{
    if (!g_starpost_atlas.ready) return;

    int first = entity_atlas_first_of_anim(&g_starpost_atlas, anim_id);
    if (first < 0) return;

    int n_frames = (anim_id + 1 < (int)g_starpost_atlas.anim_count)
        ? (entity_atlas_first_of_anim(&g_starpost_atlas, anim_id + 1) - first)
        : ((int)g_starpost_atlas.frame_total - first);
    if (n_frames <= 0) n_frames = 1;

    self->ballAnimator.frames          = &g_starpost_atlas.scratch_frames[first];
    self->ballAnimator.frame_count     = (int16_t)n_frames;
    self->ballAnimator.frame_id        = 0;
    self->ballAnimator.timer           = 0;
    self->ballAnimator.animation_id    = (int16_t)anim_id;
    self->ballAnimator.prev_animation_id = (int16_t)anim_id;
    self->ballAnimator.loop_index      = (uint8_t)g_starpost_atlas.anims[anim_id].loop_index;
    if (n_frames > 0)
        self->ballAnimator.frame_duration = g_starpost_atlas.duration[first];
    self->ballAnimID = (uint8_t)anim_id;
}

/* =========================================================================
 * Decomp StarPost_Update (StarPost.c:12-17)
 * =========================================================================*/
void StarPost_Update(void)
{
    RSDK_THIS(StarPost);
    StateMachine_Run(self->state);
}

/* =========================================================================
 * Decomp StarPost_LateUpdate / StarPost_StaticUpdate (StarPost.c:19-21)
 * =========================================================================*/
void StarPost_LateUpdate(void)  {}
void StarPost_StaticUpdate(void) {}

/* =========================================================================
 * Decomp StarPost_Draw (StarPost.c:23-44) -- NO-OP on Saturn.
 * The Saturn draw path is StarPost_draw_only called from
 * mania_ghz_draw_only; rsdk_object_draw_all is suppressed in the GHZ path.
 * =========================================================================*/
void StarPost_Draw(void) {}

/* =========================================================================
 * Decomp StarPost_Create (StarPost.c:46-76)
 * =========================================================================*/
void StarPost_Create(void *data)
{
    RSDK_THIS(StarPost);

    /* Decomp L50-52: destroy in time-attack / competition-vsRemove.
     * On Saturn: neither mode exists -- skip the destroy branch. */

    /* Decomp L54-74: editor guard, entity setup. */
    self->visible       = true;
    self->drawGroup     = 2;          /* Zone->objectDrawGroup[0] = 2 (Zone.c:184) */
    self->active        = ACTIVE_BOUNDS;
    self->updateRange.x = TO_FIXED(64);
    self->updateRange.y = TO_FIXED(64);
    self->state         = StarPost_State_Idle;
    self->angle         = 256;        /* decomp L61: initial angle = 0x100 */

    /* Decomp L64-72: set sprite animations.
     * On Saturn: wire the ball animator to the atlas instead of using
     * RSDK.SetSpriteAnimation (which requires a loaded aniFrames list). */
    if (self->interactedPlayers) {
        wire_ball_animator(self, ANIM_BALL_SPIN);
        self->ballAnimator.speed = 64;
    } else {
        wire_ball_animator(self, ANIM_BALL_IDLE);
    }

    /* Decomp L73-74: initialise ballPos to entity position. */
    self->ballPos.x = self->position.x;
    self->ballPos.y = self->position.y - TO_FIXED(24);

    (void)data;
}

/* =========================================================================
 * Decomp StarPost_StageLoad (StarPost.c:78-158)
 * =========================================================================*/
void StarPost_StageLoad(void)
{
    /* Decomp L80: RSDK.LoadSpriteAnimation("Global/StarPost.bin", ...).
     * On Saturn: atlas loaded via StarPost_load_assets at scene-load time.
     * StarPost->aniFrames is unused; leave 0. */

    /* Decomp L82-85: hitbox setup. */
    StarPost->hitbox.left   = -8;
    StarPost->hitbox.top    = -44;
    StarPost->hitbox.right  =  8;
    StarPost->hitbox.bottom = 20;

    /* Decomp L87: interactablePlayers = (1 << playerCount) - 1.
     * Saturn: single player, playerCount = 1 -> interactablePlayers = 1. */
    StarPost->interactablePlayers = 1;

    /* Decomp L91-141: restore saved player spawn position if postIDs set.
     * Ported for save-state restore (StarPost->postIDs[0] set by
     * CheckCollisions when the player tagged this post last run).
     * PLAYER_COUNT collapsed to 1 on Saturn. */
    {
        int p = 0;
        if (StarPost->postIDs[p]) {
            player_t *player = mania_is_ghz_active() ? g_ghz_player_addr : NULL;

            /* Activate all prior posts (TMZ2Setup always NULL in GHZ). */
            {
                int cursor = -1;
                EntityStarPost *starPost;
                EntityStarPost *savedStarPost = NULL;
                /* Resolve the entity at postIDs[p] so we can compare .id. */
                int cls = rsdk_object_find_class("StarPost");
                if (cls >= 0) {
                    for (uint16_t slot = 0; slot < RSDK_ENTITY_COUNT; ++slot) {
                        rsdk_entity_t *base = rsdk_entity_at(slot);
                        if (!base) continue;
                        if (base->class_id != (uint16_t)cls) continue;
                        if (base->active == ACTIVE_NEVER) continue;
                        EntityStarPost *sp = (EntityStarPost *)base;
                        /* postIDs stores the scene entity slot index. */
                        if (sp->id == (int32)StarPost->postIDs[p]) {
                            savedStarPost = sp;
                            break;
                        }
                    }
                    if (savedStarPost) {
                        cursor = -1;
                        foreach_all(StarPost, starPost) {
                            if (starPost->id < savedStarPost->id
                                && !starPost->interactedPlayers) {
                                starPost->interactedPlayers =
                                    StarPost->interactablePlayers;
                                wire_ball_animator(starPost, ANIM_BALL_SPIN);
                            }
                        }
                    }
                }
            }

            /* Decomp L107-140: restore timer + player position. */
            if (player) {
                player->position.x = StarPost->playerPositions[p].x;
                player->position.y = StarPost->playerPositions[p].y
                                     + TO_FIXED(16);
                player->direction  = StarPost->playerDirections[p];
            }

            /* Decomp L141: mark the saved post as interacted. */
            /* (savedStarPost lookup is re-done above; marking done there.) */
        }
    }

    /* Decomp L156-157: load SFX.
     * FIXME Phase Z: RSDK.GetSfx mapping -- stub 0 for now. */
    StarPost->sfxStarPost = 0;
    StarPost->sfxWarp     = 0;
}

/* =========================================================================
 * Decomp StarPost_CheckBonusStageEntry (StarPost.c:178-233)
 * =========================================================================*/
void StarPost_CheckBonusStageEntry(void)
{
    RSDK_THIS(StarPost);

    /* Decomp L182-185: orbit angle advance. */
    self->starAngleY = (self->starAngleY + 4) & 0x1FF;
    self->starAngleX = (self->starAngleX + 18) & 0x1FF;

    /* Decomp L187-190: radius pulse. */
    if (self->starTimer > 472)
        --self->starRadius;
    else if (self->starTimer < 0x80)
        ++self->starRadius;

    /* Decomp L192-196: timer advance; auto-clear at 600. */
    if (++self->starTimer == 600) {
        self->starTimer    = 0;
        self->bonusStageID = 0;
        self->active       = ACTIVE_BOUNDS;
    }

    /* Decomp L198: star frame driven by starAngleY >> 3. */
    self->starFrameID = (uint8_t)((self->starAngleY >> 3) & 3);

    /* Decomp L200-203: hitboxStars AABB for warp entry. */
    self->hitboxStars.left   = (int16_t)(-(self->starRadius >> 2));
    self->hitboxStars.top    = -48;
    self->hitboxStars.right  = (int16_t)(self->starRadius >> 2);
    self->hitboxStars.bottom = -40;

    /* Decomp L205-232: warp entry check at starTimer >= 60.
     * Blue Spheres not ported; FIXME Phase Z. */
    if (self->starTimer >= 60) {
        player_t *player = mania_is_ghz_active() ? g_ghz_player_addr : NULL;
        if (player) {
            int ex = self->position.x >> 16;
            int ey = self->position.y >> 16;
            if (starpost_touch(player, ex, ey, &self->hitboxStars)) {
                /* FIXME Phase Z:
                 *   SaveGame_SaveGameState();
                 *   SetScene("Blue Spheres", "");
                 *   Zone_StartFadeOut(10, 0xF0F0F0);
                 *   Music_Stop(); */
            }
        }
    }
}

/* =========================================================================
 * Decomp StarPost_CheckCollisions (StarPost.c:234-324)
 * =========================================================================*/
void StarPost_CheckCollisions(void)
{
    RSDK_THIS(StarPost);

    player_t *player = mania_is_ghz_active() ? g_ghz_player_addr : NULL;
    if (!player) return;

    int playerID = 0; /* Saturn: always 0 (single player). */
    if ((1 << playerID) & self->interactedPlayers) return;
    /* Decomp L241: !player->sidekick -- Saturn: no sidekick. */

    int ex = self->position.x >> 16;
    int ey = self->position.y >> 16;
    if (!starpost_touch(player, ex, ey, &StarPost->hitbox)) return;

    /* Decomp L243: enter Spinning state. */
    self->state = StarPost_State_Spinning;

    /* Decomp L244-252: activate prior posts (TMZ2Setup always NULL in GHZ). */
    {
        int cursor = -1;
        EntityStarPost *starPost;
        foreach_all(StarPost, starPost) {
            if (starPost->id < self->id && !starPost->interactedPlayers) {
                starPost->interactedPlayers = 1 << playerID;
                wire_ball_animator(starPost, ANIM_BALL_SPIN);
            }
        }
    }

    /* Decomp L254-257: record spawn data. */
    StarPost->postIDs[playerID]            = (uint16_t)self->id;
    StarPost->playerPositions[playerID].x  = self->position.x;
    StarPost->playerPositions[playerID].y  = self->position.y;
    StarPost->playerDirections[playerID]   = (uint8)self->direction;

    /* Decomp L258-262: store time (gameMode < MODE_TIMEATTACK always true
     * on Saturn since we never enter time attack). */
    /* FIXME Phase Z: StarPost->storedMS/Seconds/Minutes from SceneInfo. */

    /* Decomp L264-292: compute ball speed from player velocity. */
    {
        int32 playerVelocity = player->onGround ? player->groundVel
                                                : player->velocity.x;
        int32 ballSpeed = -12 * (playerVelocity >> 17);
        if (ballSpeed >= 0)
            ballSpeed += 32;
        else
            ballSpeed -= 32;

        if (!self->ballSpeed) {
            self->ballSpeed = ballSpeed;
        } else if (self->ballSpeed <= 0) {
            if (ballSpeed < self->ballSpeed)
                self->ballSpeed = ballSpeed;
            else if (ballSpeed > 0) {
                ballSpeed += self->ballSpeed;
                self->ballSpeed = ballSpeed;
            }
        } else {
            if (ballSpeed > self->ballSpeed)
                self->ballSpeed = ballSpeed;
            else if (ballSpeed < 0) {
                ballSpeed += self->ballSpeed;
                self->ballSpeed = ballSpeed;
            }
        }
    }

    self->timer = 0;

    /* Decomp L295-311: bonus stage activation if rings >= 25. */
    if (player->rings >= 25) {
        self->starTimer   = 0;
        self->starAngleY  = 0;
        self->starAngleX  = 0;
        self->starRadius  = 0;
        self->bonusStageID = (player->rings - 20) % 3 + 1;
    }

    /* Decomp L313-316: switch ball to spin anim (speed=0 until Spinning
     * state advances it). */
    if (!self->interactedPlayers) {
        wire_ball_animator(self, ANIM_BALL_SPIN);
        self->ballAnimator.speed = 0;
    }

    self->interactedPlayers |= (1 << playerID);
    self->active = ACTIVE_NORMAL;

    /* Decomp L320: RSDK.PlaySfx -- FIXME Phase Z. */
}

/* =========================================================================
 * Decomp StarPost_State_Idle (StarPost.c:325-336)
 * =========================================================================*/
void StarPost_State_Idle(void)
{
    RSDK_THIS(StarPost);

    if (self->interactedPlayers < StarPost->interactablePlayers)
        StarPost_CheckCollisions();

    if (self->bonusStageID > 0)
        StarPost_CheckBonusStageEntry();

    /* Decomp L335: RSDK.ProcessAnimation(&self->ballAnimator). */
    rsdk_process_animation(&self->ballAnimator);
}

/* =========================================================================
 * Decomp StarPost_State_Spinning (StarPost.c:337-390)
 * =========================================================================*/
void StarPost_State_Spinning(void)
{
    RSDK_THIS(StarPost);

    if (self->interactedPlayers < StarPost->interactablePlayers)
        StarPost_CheckCollisions();

    self->angle += self->ballSpeed;

    /* Decomp L345-346: achievement at timer==10.
     * FIXME Phase Z: API_UnlockAchievement. */

    bool32 isIdle = false;
    if (self->ballSpeed <= 0) {
        if (self->angle <= -0x300) {
            ++self->timer;
            self->angle += 0x400;
            self->ballSpeed += 8;
            if (self->ballSpeed > -32)
                self->ballSpeed = -32;
            isIdle = (self->ballSpeed == -32);
        }
    } else {
        if (self->angle >= 0x500) {
            ++self->timer;
            self->angle -= 0x400;
            self->ballSpeed -= 8;
            if (self->ballSpeed < 32)
                self->ballSpeed = 32;
            isIdle = (self->ballSpeed == 32);
        }
    }

    if (isIdle) {
        self->state              = StarPost_State_Idle;
        self->ballAnimator.speed = 64;
        self->ballSpeed          = 0;
        self->angle              = 0x100;
        if (!self->bonusStageID)
            self->active = ACTIVE_BOUNDS;
    }

    if (self->bonusStageID > 0)
        StarPost_CheckBonusStageEntry();

    rsdk_process_animation(&self->ballAnimator);
}

/* =========================================================================
 * Decomp StarPost_ResetStarPosts (StarPost.c:171-177)
 * =========================================================================*/
void StarPost_ResetStarPosts(void)
{
    int i;
    for (i = 0; i < STARPOST_PLAYER_COUNT; ++i)
        StarPost->postIDs[i] = 0;
    StarPost->storedMS      = 0;
    StarPost->storedSeconds = 0;
    StarPost->storedMinutes = 0;
}

/* =========================================================================
 * Decomp StarPost_Serialize (StarPost.c:405-410)
 * =========================================================================*/
void StarPost_Serialize(void)
{
    /* VAR_ENUM id, VAR_UINT8 direction, VAR_BOOL vsRemove.
     * Offsets handled by fill_starpost_attributes in scene.c. */
}

/* =========================================================================
 * Saturn-only: StarPost_load_assets
 * Called from entities_load_assets() in Entities.c at scene load time.
 * Loads cd/STARPOST.SP2 + cd/STARPOST.MET via entity_atlas_load.
 * =========================================================================*/
void StarPost_load_assets(void)
{
    if (entity_atlas_load(&g_starpost_atlas, "STARPOST"))
        entity_atlas_play(&g_starpost_atlas, ANIM_BALL_IDLE);
}

/* =========================================================================
 * Saturn-only: StarPost_draw_only
 * Called from mania_ghz_draw_only() in Game.c once per frame.
 * Iterates all StarPost RSDK slots and draws pole + ball + stars for each.
 * =========================================================================*/
void StarPost_draw_only(int cam_x, int cam_y)
{
    if (!g_starpost_atlas.ready) return;

    int cls = rsdk_object_find_class("StarPost");
    if (cls < 0) return;

    for (uint16_t slot = 0; slot < RSDK_ENTITY_COUNT; ++slot) {
        rsdk_entity_t *base = rsdk_entity_at(slot);
        if (!base) continue;
        if (base->class_id != (uint16_t)cls) continue;
        if (base->active == ACTIVE_NEVER) continue;

        EntityStarPost *self = (EntityStarPost *)base;

        int ex = self->position.x >> 16;
        int ey = self->position.y >> 16;

        /* Screen culling: skip if entirely off-screen. */
        int sx_base = ex - cam_x;
        int sy_base = ey - cam_y;
        if (sx_base < -64 || sx_base > 320 + 64) continue;
        if (sy_base < -128 || sy_base > 224 + 64) continue;

        /* --- Draw pole (anim 0, frame 0; always static). --- */
        {
            int sid = entity_atlas_sprite_at(&g_starpost_atlas,
                          entity_atlas_first_of_anim(&g_starpost_atlas,
                                                      ANIM_POLE));
            if (sid >= 0) {
                int pw, ph, ppx, ppy;
                entity_atlas_size_at(&g_starpost_atlas,
                    entity_atlas_first_of_anim(&g_starpost_atlas, ANIM_POLE),
                    &pw, &ph);
                entity_atlas_pivot_at(&g_starpost_atlas,
                    entity_atlas_first_of_anim(&g_starpost_atlas, ANIM_POLE),
                    &ppx, &ppy);
                int pole_sx = sx_base - ppx - SCR_HALF_W;
                int pole_sy = sy_base - ppy - SCR_HALF_H;
                jo_sprite_draw3D(sid, pole_sx, pole_sy, STARPOST_Z);
            }
        }

        /* --- Draw ball (anim 1 or 2 depending on ballAnimID). ---
         * Ball position from decomp Draw:
         *   ballPos.x = pos.x - 0x280 * Cos1024(angle)
         *   ballPos.y = pos.y - 0x280 * Sin1024(angle) - TO_FIXED(14)
         * Compute ballPos in Q16.16 then shift to pixels.
         */
        {
            int32 bx = self->position.x
                       - (int32)(0x280) * rsdk_cos1024(self->angle);
            int32 by = self->position.y
                       - (int32)(0x280) * rsdk_sin1024(self->angle)
                       - TO_FIXED(14);
            int ball_ex = bx >> 16;
            int ball_ey = by >> 16;
            int ball_sx = ball_ex - cam_x;
            int ball_sy = ball_ey - cam_y;

            /* Advance the per-entity ball animator. */
            int anim_id = (int)self->ballAnimID;
            if (anim_id < 1) anim_id = ANIM_BALL_IDLE;

            /* Sync the per-entity animator's frames ptr if needed. */
            if (!self->ballAnimator.frames)
                wire_ball_animator(self, anim_id);

            rsdk_process_animation(&self->ballAnimator);

            int fidx = entity_atlas_first_of_anim(&g_starpost_atlas, anim_id)
                       + self->ballAnimator.frame_id;
            int sid = entity_atlas_sprite_at(&g_starpost_atlas, fidx);
            if (sid >= 0) {
                int bpx, bpy;
                entity_atlas_pivot_at(&g_starpost_atlas, fidx, &bpx, &bpy);
                jo_sprite_draw3D(sid,
                                 ball_sx - bpx - SCR_HALF_W,
                                 ball_sy - bpy - SCR_HALF_H,
                                 STARPOST_Z);
            }
        }

        /* --- Draw stars (only when bonusStageID > 0). ---
         * Decomp Draw L34-43: 4 stars orbiting using Sin512/Cos512.
         *   drawPos.x = pos.x + ((Sin512(angleX) << 12) * starRadius >> 7)
         *   drawPos.y = (((amplitude * Sin512(angleX))
         *                + (Cos512(angleX) << 10)) * starRadius >> 7)
         *               + pos.y - TO_FIXED(50)
         * where amplitude = 3 * Sin512(starAngleY).
         */
        if (self->bonusStageID > 0) {
            int star_anim_first =
                entity_atlas_first_of_anim(&g_starpost_atlas, ANIM_STARS);
            if (star_anim_first >= 0) {
                int fidx_star = star_anim_first + ((int)self->starFrameID & 3);
                int sid = entity_atlas_sprite_at(&g_starpost_atlas, fidx_star);
                if (sid >= 0) {
                    int32 amplitude = 3 * rsdk_sin512(self->starAngleY);
                    int32 angleX = self->starAngleX;
                    int spx, spy;
                    entity_atlas_pivot_at(&g_starpost_atlas,
                                          fidx_star, &spx, &spy);
                    int i;
                    for (i = 0; i < 4; ++i) {
                        int32 sx32 = self->position.x
                            + (((int32)rsdk_sin512(angleX) << 12)
                               * (int32)self->starRadius >> 7);
                        int32 sy32 = (((amplitude
                                        * (int32)rsdk_sin512(angleX))
                                       + ((int32)rsdk_cos512(angleX) << 10))
                                      * (int32)self->starRadius >> 7)
                                     + self->position.y - TO_FIXED(50);
                        int star_wx = (int)(sx32 >> 16);
                        int star_wy = (int)(sy32 >> 16);
                        int star_sx = star_wx - cam_x - spx - SCR_HALF_W;
                        int star_sy = star_wy - cam_y - spy - SCR_HALF_H;
                        jo_sprite_draw3D(sid, star_sx, star_sy, STARPOST_Z);
                        angleX = (angleX + 128) & 0x1FF;
                    }
                }
            }
        }
    }
}
