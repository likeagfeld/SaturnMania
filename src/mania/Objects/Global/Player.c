/* Phase 2.2 — Saturn-side Player implementation.
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Global_Player.c`.
 * See Player.h for the scope rationale (which decomp behaviors are in vs
 * deferred), and docs/COMPREHENSIVE_PLAN.md §12.2 for the Phase 2.2 design.
 *
 * The implementation is a translation of the decomp's state machine into
 * Saturn-native Q16.16 arithmetic with per-column surface-table collision.
 * Each non-trivial code block cites the source `Player.c:LL-LL` range it
 * implements. */

#include "Player.h"
#include "../../../rsdk/collision.h"   /* #180 step 4b - tile-backed surface query */
#include <string.h>

/* === Q1.7 SIN table for Mania's Q0.8 angle byte =========================
 *
 * Decomp uses RSDK.Sin256(angle) returning the 8.8-fixed sin (Player.c:3089:
 * `groundVel += RSDK.Sin256(self->angle) << 13 >> 8`). The Saturn port
 * uses a precomputed signed-8-bit table so the multiply fits in a single
 * SH-2 `muls.w` per slope-projection. Read as `SIN8[(uint8_t)angle]`
 * returning -127..+127 ≈ -1.0..+1.0.
 *
 * SAME table as src/_archived/player.c:23-40 — generator
 * `sin(i * 2*pi / 256) * 127` rounded to nearest. Mania's RSDK.Sin256
 * scales by 256; we scale by 128 and `>> 7` instead of `>> 8` at the use
 * site. The two `<< 13 >> 8` / `<< 13 >> 7` shifts yield the same Q16.16
 * slope-force magnitude. */
static const int8_t SIN8[256] = {
       0,    3,    6,    9,   12,   16,   19,   22,   25,   28,   31,   34,   37,   40,   43,   46,
      49,   51,   54,   57,   60,   63,   65,   68,   71,   73,   76,   78,   81,   83,   85,   88,
      90,   92,   94,   96,   98,  100,  102,  104,  106,  107,  109,  111,  112,  113,  115,  116,
     117,  118,  120,  121,  122,  122,  123,  124,  125,  125,  126,  126,  126,  127,  127,  127,
     127,  127,  127,  127,  126,  126,  126,  125,  125,  124,  123,  122,  122,  121,  120,  118,
     117,  116,  115,  113,  112,  111,  109,  107,  106,  104,  102,  100,   98,   96,   94,   92,
      90,   88,   85,   83,   81,   78,   76,   73,   71,   68,   65,   63,   60,   57,   54,   51,
      49,   46,   43,   40,   37,   34,   31,   28,   25,   22,   19,   16,   12,    9,    6,    3,
       0,   -3,   -6,   -9,  -12,  -16,  -19,  -22,  -25,  -28,  -31,  -34,  -37,  -40,  -43,  -46,
     -49,  -51,  -54,  -57,  -60,  -63,  -65,  -68,  -71,  -73,  -76,  -78,  -81,  -83,  -85,  -88,
     -90,  -92,  -94,  -96,  -98, -100, -102, -104, -106, -107, -109, -111, -112, -113, -115, -116,
    -117, -118, -120, -121, -122, -122, -123, -124, -125, -125, -126, -126, -126, -127, -127, -127,
    -127, -127, -127, -127, -126, -126, -126, -125, -125, -124, -123, -122, -122, -121, -120, -118,
    -117, -116, -115, -113, -112, -111, -109, -107, -106, -104, -102, -100,  -98,  -96,  -94,  -92,
     -90,  -88,  -85,  -83,  -81,  -78,  -76,  -73,  -71,  -68,  -65,  -63,  -60,  -57,  -54,  -51,
     -49,  -46,  -43,  -40,  -37,  -34,  -31,  -28,  -25,  -22,  -19,  -16,  -12,   -9,   -6,   -3,
};

int8_t Player_Sin8(unsigned char a) { return SIN8[a]; }

/* === Phase 2.4b — Decomp-exact Player_CheckCollisionBox =============== *
 *
 * Mechanical port of:
 *   - tools/_decomp_raw/SonicMania_Objects_Global_Player.c:2267-2341
 *     (Player_CheckCollisionBox)
 *   - rsdkv5-src/RSDKv5/RSDK/Scene/Collision.cpp:276-487
 *     (CheckObjectCollisionBox with setValues=true)
 *
 * The decomp performs the test in two halves:
 *   1. Horizontal (LEFT/RIGHT) overlap using non-incremented top/bottom.
 *   2. Vertical (TOP/BOTTOM) overlap using non-incremented left/right
 *      but with otherHitbox.top++ / bottom-- (one-pixel inset to bias
 *      vertical-vs-horizontal tie-breaks toward vertical contact).
 * Then picks the dominant side via (cx*cx >= cy*cy).
 *
 * Saturn-port deviations from the decomp:
 *   - The decomp's `thisEntity` is the box, `otherEntity` is the
 *     player. Saturn-port: box_x_px/box_y_px (integer px) are the box
 *     position, player_t *p is the player.
 *   - The decomp's entity positions are Q16.16 (TO_FIXED). Saturn
 *     entities store integer pixels in the .BIN tables, so we promote
 *     box position to Q16.16 once at function entry, then mirror the
 *     decomp arithmetic exactly.
 *   - The decomp flips hitbox edges when entity->direction has FLIP_X
 *     / FLIP_Y. Saturn-side entities currently don't carry a direction
 *     field (Phase 2.4 deferral; Motobug uses vx_dir not the decomp's
 *     FLIP_X drawFX). We do NOT do the hitbox flip; the boxes used by
 *     itembox/spring/motobug are all symmetric so the result is
 *     unchanged.
 *   - The decomp also handles invertGravity (RETRO_REV0U) and
 *     onGround/collisionMode/groundVel state propagation. Phase 2.4b
 *     ports the core position-snap + velocity-zero contract;
 *     groundVel is mirrored from xsp via Saturn's existing p->gsp
 *     update path.
 *   - Phase 2.2 player_t doesn't have `collisionPlane` or `outerbox`
 *     (decomp Player.c:2275). Saturn-port uses Player_FallbackHitbox
 *     unconditionally (matches the decomp's NULL-Animator fallback
 *     path at Player.c:2247).
 *
 * The setValues=true mutation contract (per Collision.cpp:385-460):
 *   C_TOP   -> ypos = collideY; if ysp > 0 then ysp = 0;
 *              if !onGround && ysp >= 0 latch onGround, gsp=xsp, angle=0
 *   C_LEFT  -> xpos = collideX; if effective velX > 0 then xsp=0, gsp=0
 *   C_RIGHT -> xpos = collideX; if effective velX < 0 then xsp=0, gsp=0
 *   C_BOTTOM-> ypos = collideY; if ysp < 0 then ysp = 0
 */
hitbox_t Player_FallbackHitbox = { -10, -20, 10, 20 };

int Player_CheckCollisionBox(player_t *p, int box_x_px, int box_y_px,
                             const hitbox_t *entityHitbox)
{
    /* Decomp Collision.cpp:278-279 — NULL guard. */
    if (!p || !entityHitbox) return C_NONE;

    /* Decomp Player.c:2275 — Phase 2.2 fallback (no Animator, no outerbox). */
    const hitbox_t *playerHitbox = &Player_FallbackHitbox;

    /* Decomp Collision.cpp:284-285 — collide points default to the
     * player's current position; on side-resolve they shift to the
     * snap position. */
    int32_t collideX = p->xpos;
    int32_t collideY = p->ypos;

    /* Decomp Collision.cpp:307-310 — integer-pixel projections.
     * Saturn-side box position is already integer px; player position
     * is Q16.16 -> >>16. */
    int32_t thisIX  = box_x_px;
    int32_t thisIY  = box_y_px;
    int32_t otherIX = p->xpos >> 16;
    int32_t otherIY = p->ypos >> 16;

    /* The decomp mutates otherHitbox in place (top++ / bottom--, then
     * later top--/bottom++). Saturn-side keeps a mutable LOCAL copy
     * so the caller's hitbox table is never touched. */
    int16_t ph_left   = playerHitbox->left;
    int16_t ph_top    = playerHitbox->top;
    int16_t ph_right  = playerHitbox->right;
    int16_t ph_bottom = playerHitbox->bottom;

    int16_t th_left   = entityHitbox->left;
    int16_t th_top    = entityHitbox->top;
    int16_t th_right  = entityHitbox->right;
    int16_t th_bottom = entityHitbox->bottom;

    int collisionSideH = C_NONE;
    int collisionSideV = C_NONE;

    /* === Horizontal half (Collision.cpp:312-328) =====================
     * Per decomp the otherHitbox is inset top++/bottom-- BEFORE the
     * horizontal test. */
    ph_top++;
    ph_bottom--;

    /* Decomp Collision.cpp:315: `if (otherIX <= (this.right + this.left + 2*thisIX) >> 1)`.
     * Player is left-of-box-midline -> test for C_LEFT contact. */
    if (otherIX <= (th_right + th_left + 2 * thisIX) >> 1) {
        if (otherIX + ph_right >= thisIX + th_left &&
            thisIY + th_top   <  otherIY + ph_bottom &&
            thisIY + th_bottom > otherIY + ph_top) {
            collisionSideH = C_LEFT;
            /* Decomp Collision.cpp:319: collideX = thisEntity->position.x +
             *                            TO_FIXED(thisHitbox->left -
             *                                     otherHitbox->right).
             * Box position is integer px on Saturn -> promote via <<16. */
            collideX = ((int32_t)box_x_px << 16) +
                       (((int32_t)(th_left - ph_right)) << 16);
        }
    }
    else {
        if (otherIX + ph_left < thisIX + th_right &&
            thisIY + th_top   <  otherIY + ph_bottom &&
            thisIY + th_bottom > otherIY + ph_top) {
            collisionSideH = C_RIGHT;
            collideX = ((int32_t)box_x_px << 16) +
                       (((int32_t)(th_right - ph_left)) << 16);
        }
    }

    /* === Vertical half (Collision.cpp:330-349) =======================
     * Per decomp the otherHitbox is now restored
     * (left++/top--/right--/bottom++) BEFORE the vertical test. */
    ph_left++;
    ph_top--;
    ph_right--;
    ph_bottom++;

    if (otherIY < (th_top + th_bottom + 2 * thisIY) >> 1) {
        if (otherIY + ph_bottom >= thisIY + th_top &&
            thisIX + th_left   <  otherIX + ph_right &&
            thisIX + th_right  >  otherIX + ph_left) {
            collisionSideV = C_TOP;
            collideY = ((int32_t)box_y_px << 16) +
                       (((int32_t)(th_top - ph_bottom)) << 16);
        }
    }
    else {
        if (otherIY + ph_top < thisIY + th_bottom &&
            thisIX + th_left < otherIX + ph_right) {
            if (otherIX + ph_left < thisIX + th_right) {
                collisionSideV = C_BOTTOM;
                collideY = ((int32_t)box_y_px << 16) +
                           (((int32_t)(th_bottom - ph_top)) << 16);
            }
        }
    }

    /* === Side selection (Collision.cpp:374-383) ======================
     * cx = abs displacement to horizontal-snap; cy = abs displacement
     * to vertical-snap. Larger cx (further to push back) means
     * horizontal direction dominated by vertical; pick vertical when
     * cx*cx >= cy*cy (and either V hit, or H didn't hit), else H. */
    int side = C_NONE;
    int32_t cx = (collideX - p->xpos) >> 16;
    int32_t cy = (collideY - p->ypos) >> 16;
    if ((cx * cx >= cy * cy && (collisionSideV || !collisionSideH)) ||
        (!collisionSideH && collisionSideV)) {
        side = collisionSideV;
    } else {
        side = collisionSideH;
    }

    /* === setValues=true mutation (Collision.cpp:385-460) =============
     * The decomp Player_CheckCollisionBox always passes setValues=true
     * (Player.c:2277). The mutations:
     *   C_TOP    -> position.y = collideY; clamp velocity.y >= 0 -> 0;
     *               latch onGround + gsp inherit when landing.
     *   C_LEFT   -> position.x = collideX; if effective velX > 0
     *               then xsp = 0, gsp = 0.
     *   C_RIGHT  -> position.x = collideX; if effective velX < 0
     *               then xsp = 0, gsp = 0.
     *   C_BOTTOM -> position.y = collideY; clamp velocity.y <= 0 -> 0. */
    switch (side) {
        default:
        case C_NONE:
            break;

        case C_TOP: {
            /* Decomp Collision.cpp:391-407 */
            p->ypos = collideY;
            if (p->ysp > 0) p->ysp = 0;
            if (!p->onGround && p->ysp >= 0) {
                p->gsp      = p->xsp;
                p->angle    = 0x00;
                p->onGround = true;
                p->jumping  = false;
                p->applyJumpCap = false;
            }
            /* Decomp Player.c:2281-2314 — C_TOP also clears controlLock
             * and latches collisionMode = CMODE_FLOOR. Saturn player_t
             * doesn't carry controlLock or collisionMode (Phase 2.2
             * scope), so those fields are skipped. */
            break;
        }

        case C_LEFT: {
            /* Decomp Collision.cpp:410-425 */
            p->xpos = collideX;
            int32_t velX = p->xsp;
            if (p->onGround) {
                /* collisionMode == CMODE_ROOF inversion (Phase 2.2
                 * doesn't track CMODE_ROOF -> use groundVel directly). */
                velX = p->gsp;
            }
            if (velX > 0) {
                p->xsp = 0;
                p->gsp = 0;
            }
            /* Decomp Player.c:2317-2331 — C_LEFT spindash quirk
             * (groundVel = -0x8000 when player->left && onGround).
             * Phase 2.2 player_t doesn't track per-frame input flags;
             * the caller's player_update_ground already sets gsp from
             * input, so re-applying the -0x8000 nudge here would
             * double-count. Skipped per Saturn-port deviation note. */
            break;
        }

        case C_RIGHT: {
            /* Decomp Collision.cpp:427-442 */
            p->xpos = collideX;
            int32_t velX = p->xsp;
            if (p->onGround) {
                velX = p->gsp;
            }
            if (velX < 0) {
                p->xsp = 0;
                p->gsp = 0;
            }
            break;
        }

        case C_BOTTOM: {
            /* Decomp Collision.cpp:444-448 */
            p->ypos = collideY;
            if (p->ysp < 0) p->ysp = 0;
            break;
        }
    }

    return side;
}

/* === Mania-faithful physics constants (per decomp Player.h:281-285,
 *     sonicPhysicsTable, default-chunk entry 0 = Sonic / no shoes /
 *     no super / on land) =========================================== */

/* sonicPhysicsTable[0]..[7] from decomp Player.h:282 (!MANIA_USE_PLUS).
 * These match archived physics.h *except* gravity (archived 0x38000 was
 * the Sonic 1/2 Genesis value; Mania uses 0x5800 per Player.c:2769
 * UpdatePhysicsState). */
#define P_TOP_SPEED        0x60000   /* 6.0   px/fr */
#define P_ACCELERATION     0xC00     /* 0.046875       */
#define P_DECELERATION     0xC00     /* sonicPhysicsTable[1] >> 0 (no super) */
#define P_AIR_ACCEL        0x1800    /* 0.09375        */
#define P_SKID_SPEED       0x8000    /* 0.5            */
#define P_ROLLING_FRIC     0x600     /* 0.0234375      */
#define P_ROLLING_DECEL    0x2000    /* decomp UpdatePhysicsState (Player.c:2795) */
#define P_JUMP_STRENGTH    0x68000   /* 6.5            */
#define P_JUMP_CAP         (-0x40000)/* -4.0           */
#define P_GRAVITY          0x5800    /* 0.34375 — decomp Mania (NOT 0x38000) */

/* Slope-factor (running). Decomp Player.c:3089: `groundVel += RSDK.Sin256(
 * angle) << 13 >> 8`. With our Q1.7 SIN8 the equivalent is `<< 13 >> 7`
 * (one bit less right-shift because the table is half-scale). The
 * resulting Q16.16 slope-force magnitude is identical to the decomp's. */
#define SLOPE_SHIFT_DOWN_RUN  7    /* SIN8 Q1.7 -> Q16.16 via << 13 >> 7 */
#define SLOPE_SHIFT_DOWN_ROLL 7    /* same scale for rolling slope force */

/* Stair / wall tolerance — Mania decomp uses 6-sensor probe heights to
 * compute these; the Saturn per-column surface table reduces it to a
 * step-up / step-down threshold check. 16 px = 1 tile is walkable
 * (matches the GHZ grass-onto-dirt stair pattern); >16 = wall.
 * Archived src/_archived/player.c:183-184 — kept identical. */
#define WALL_STEP_PX     17
#define CLIFF_DROP_PX    17

/* abs helper */
static int32_t iabs(int32_t v) { return v < 0 ? -v : v; }

/* === Init ============================================================ */

void Player_Init(player_t *p, player_character_t who,
                 int32_t xpos_fixed, int32_t ypos_fixed)
{
    /* Bring up like decomp Player_Create (Player.c:542-690) reduced to the
     * Phase 2.2 active fields. */
    memset(p, 0, sizeof(*p));

    p->xpos = xpos_fixed;
    p->ypos = ypos_fixed;
    p->characterID = who;

    /* UpdatePhysicsState (Player.c:2747-2813) on a fresh land-Sonic. We
     * skip the per-mode (underwater / super / speed-shoes) branches per
     * Phase 2.2 deferral list and load entry 0 directly. */
    p->topSpeed         = P_TOP_SPEED;
    p->acceleration     = P_ACCELERATION;
    p->deceleration     = P_DECELERATION;
    p->airAcceleration  = P_AIR_ACCEL;
    p->skidSpeed        = P_SKID_SPEED;
    p->rollingFriction  = P_ROLLING_FRIC;
    p->rollingDeceleration = P_ROLLING_DECEL;
    p->jumpStrength     = P_JUMP_STRENGTH;
    p->jumpCap          = P_JUMP_CAP;
    p->gravityStrength  = P_GRAVITY;

    /* FR-1 — animation state. UpdatePhysicsState (Player.c:686-688) seeds the
     * three velocity thresholds; Player_HandleGroundAnimation mutates them
     * with hysteresis thereafter. Start on ANI_IDLE with the MET-default
     * speed sentinel. */
    p->animationID     = ANI_IDLE;
    p->prevAnimationID = ANI_IDLE;
    p->animFrame       = 0;
    p->animTimer       = 0;
    p->animSpeed       = PLAYER_ANIM_SPEED_DEFAULT;
    p->animFrameCount  = 0;
    p->minJogVelocity  = 0x40000;
    p->minRunVelocity  = 0x60000;
    p->minDashVelocity = 0xC0000;

    /* On land, expected onGround=true is set BY THE CALLER once a surface-
     * snap has been performed (see Game.c, where the camera y-clamp drops
     * Sonic onto the surface table's column-0 surfY immediately after
     * Player_Init). */
}

/* === Surface lookups (#180 step 4b — tile-backed) ===================== *
 *
 * Repointed from the old per-column cd/GHZ1SURF.BIN heightmap onto the
 * streamed tile window: Player_SurfaceY/Angle now query
 * rsdk_collision_column_floor (src/rsdk/collision.c), the LINE-FOR-LINE
 * mirror of tools/qa_ghz_fall_through_gate.py oracle_floor. The fall-through
 * gate proved the heightmap dropped Sonic through 2384 real-floor columns
 * while this tile model drops him through 0. The integrate-then-snap state
 * machine below is UNCHANGED — only the surface DATA SOURCE swapped.
 *
 * w->raw is the resident GHZ1MASK.BIN blob (non-NULL load sentinel); a NULL
 * raw means the collision bind failed -> graceful-degrade to "no floor"
 * (Sonic floats at fixed Y) exactly as the Phase 2.2 heightmap path did.
 * The plane-A/B raw_alt/active_path selector is retired here: the tile model
 * resolves plane 0 directly, and GHZ1's B path was degenerate (raw_alt==raw)
 * so no behavior is lost. */

int Player_SurfaceY(const sms_world_t *w, int x_px)
{
    if (!w->raw) return SMS_NO_FLOOR;
    if (x_px < 0)            x_px = 0;
    if (x_px >= w->width_px) x_px = w->width_px - 1;
    return rsdk_collision_column_floor(x_px, 0);
}

int Player_SurfaceAngle(const sms_world_t *w, int x_px)
{
    int32_t angle = 0;
    if (!w->raw) return 0;
    if (x_px < 0)            x_px = 0;
    if (x_px >= w->width_px) x_px = w->width_px - 1;
    (void) rsdk_collision_column_floor(x_px, &angle);
    return (int) angle;
}

int Player_SurfaceFlag(const sms_world_t *w, int x_px)
{
    (void) w; (void) x_px;
    return 0;   /* vestigial: the GHZ1SURF flag byte retired with the heightmap */
}

/* === HandleGroundMovement (Player.c:3081-3206) ======================== *
 *
 * The decomp branches on `controlLock` (skid auto-recovery), then handles
 * left/right input vs current groundVel sign with the canonical Sonic
 * accel/decel/skid logic, then applies the slope force.
 *
 * Saturn port: omits the autoScrollSpeed / collisionMode / invertGravity
 * branches (all Phase 2.3+ features). Includes the slope force + the
 * "no input" friction branch. */
static void player_update_ground(player_t *p, bool left, bool right, bool down)
{
    (void)down;

    /* left/right input branches — decomp Player.c:3104-3146. The skidding=24
     * trigger (decomp L3110-3113 / L3132-3134) drives the SKID / SKID_TURN
     * animation in Player_HandleGroundAnimation. The decomp guards the
     * trigger with `!collisionMode && !Zone->autoScrollSpeed`; on GHZ flat
     * ground collisionMode==0 (floor) and autoScrollSpeed==0, so those are
     * constant-true here and only the |gsp| > 0x40000 threshold gates. */
    if (left) {
        if (p->gsp > -p->topSpeed) {
            if (p->gsp <= 0) {
                /* Accelerate leftward. */
                p->gsp -= p->acceleration;
            } else {
                /* Skidding from rightward run — decel by skidSpeed. */
                if (p->gsp > 0x40000) {
                    p->facing_left = false;   /* decomp FLIP_NONE */
                    p->skidding    = 24;
                }
                if (p->gsp < p->skidSpeed)
                    p->gsp = -iabs(p->skidSpeed);
                else
                    p->gsp -= p->skidSpeed;
            }
        }
        /* decomp Player.c:3122 — facing flips to FLIP_X only once gsp is no
         * longer positive AND the skid-turn countdown has elapsed. */
        if (p->gsp <= 0 && p->skidding < 1) p->facing_left = true;
    }

    if (right) {
        if (p->gsp < p->topSpeed) {
            if (p->gsp >= 0) {
                p->gsp += p->acceleration;
            } else {
                /* Skidding from leftward run. */
                if (p->gsp < -0x40000) {
                    p->facing_left = true;    /* decomp FLIP_X */
                    p->skidding    = 24;
                }
                if (p->gsp > -p->skidSpeed)
                    p->gsp = iabs(p->skidSpeed);
                else
                    p->gsp += p->skidSpeed;
            }
        }
        /* decomp Player.c:3144 — facing flips to FLIP_NONE only once gsp is
         * no longer negative AND the skid-turn countdown has elapsed. */
        if (p->gsp >= 0 && p->skidding < 1) p->facing_left = false;
    }

    /* Slope force + no-input deceleration — decomp Player.c:3148-3194.
     * If pressing in a direction, add slope force every frame. If no
     * direction held, apply deceleration toward zero AND add slope force
     * only when |gsp| > 0x2000 (the decomp threshold prevents jitter on
     * flat ground when stationary). */
    if (left || right) {
        /* Decomp Player.c:3150: `groundVel += RSDK.Sin256(angle) << 13 >> 8`.
         * SIN8 is Q1.7 (half-scale) so we shift one bit less. */
        p->gsp += ((int32_t)SIN8[p->angle & 0xFF] << 13) >> SLOPE_SHIFT_DOWN_RUN;
    } else {
        /* Decomp Player.c:3167-3178 — deceleration toward zero. */
        if (p->gsp <= 0) {
            p->gsp += p->deceleration;
            if (p->gsp > 0) p->gsp = 0;
        } else {
            p->gsp -= p->deceleration;
            if (p->gsp < 0) p->gsp = 0;
        }
        /* Decomp Player.c:3181: slope force only when |gsp| > 0x2000. */
        if (p->gsp > 0x2000 || p->gsp < -0x2000) {
            p->gsp += ((int32_t)SIN8[p->angle & 0xFF] << 13) >> SLOPE_SHIFT_DOWN_RUN;
        }
    }

    /* Project gsp onto world axes along the surface tangent. Decomp does
     * this implicitly via `velocity.x = groundVel * cos(angle) >> 8` in
     * ProcessObjectMovement when it commits the new position; on flat
     * ground (angle 0): cos=127 -> xsp≈gsp, sin=0 -> ysp=0. Phase 2.2
     * computes it explicitly here so the surface-snap step below can
     * integrate cleanly. */
    {
        int a      = p->angle & 0xFF;
        int sin_q7 = SIN8[a];
        int cos_q7 = SIN8[(a + 64) & 0xFF];   /* cos = sin shifted +90° */
        p->xsp =  (p->gsp * cos_q7) >> 7;
        p->ysp = -(p->gsp * sin_q7) >> 7;
    }
}

/* === HandleAirMovement (Player.c:3255-3293, HandleAirFriction +
 *     gravity) =========================================================
 *
 * Decomp splits this into HandleAirMovement + HandleAirFriction; both
 * are called from State_Air. Saturn collapses them: gravity + jump-cap +
 * air friction + air acceleration in one routine. */
static void player_update_air(player_t *p, bool left, bool right, bool jump_held)
{
    /* Decomp Player.c:3261: `velocity.y += gravityStrength`. */
    p->ysp += p->gravityStrength;
    if (p->ysp > PLAYER_FIXED(16.0)) p->ysp = PLAYER_FIXED(16.0);  /* terminal */

    /* Decomp Player.c:3263-3266 — variable jump height. If jumping and
     * the player released the button before reaching jumpCap, snap ysp
     * to jumpCap (== negative-4 px/fr) and bleed horizontal speed. */
    if (p->jumping && p->applyJumpCap && !jump_held && p->ysp < p->jumpCap) {
        p->xsp -= (p->xsp >> 5);
        p->ysp = p->jumpCap;
    }

    /* HandleAirFriction (Player.c:3273-3293).
     * Bleed horizontal speed near the apex (-4 < ysp < 0). */
    if (p->ysp > PLAYER_FIXED(-4.0) && p->ysp < 0)
        p->xsp -= (p->xsp >> 5);

    /* Air accel toward topSpeed. */
    if (right) {
        if (p->xsp < p->topSpeed) p->xsp += p->airAcceleration;
        if (p->xsp > p->topSpeed) p->xsp = p->topSpeed;
        p->facing_left = false;
    }
    if (left) {
        if (p->xsp > -p->topSpeed) p->xsp -= p->airAcceleration;
        if (p->xsp < -p->topSpeed) p->xsp = -p->topSpeed;
        p->facing_left = true;
    }
}

/* === FR-1 animation system =========================================== *
 *
 * Mechanical port of the decomp's animator-driving handlers. Player.c owns
 * the animationID + animSpeed decisions (the decomp's
 * `RSDK.SetSpriteAnimation` call sites + Player_HandleGroundAnimation +
 * Player_HandleIdleAnimation + the State_Air anim switch); src/rsdk/
 * player_atlas owns the MET-driven frame walk + LWRAM/VDP1 residency and
 * writes the resident frame index back into p->animFrame / p->animFrameCount
 * each tick (so the handlers can read frameID/frameCount without an atlas
 * dependency).
 *
 * player_set_anim mirrors RSDK.SetSpriteAnimation(forceApply=false): if the
 * requested anim is already current it is a no-op (frame is retained);
 * otherwise animationID/animFrame are reset and animSpeed is set to the
 * "use MET default" sentinel (-1). State code that needs a speed override
 * assigns p->animSpeed AFTER the call, exactly as the decomp assigns
 * animator.speed after SetSpriteAnimation. PLAYER_ANIM_SPEED_DEFAULT (-1) is
 * defined in Player.h so Player_Init can seed animSpeed before this point. */

static void player_set_anim(player_t *p, int ani, int frame)
{
    if (p->animationID != ani) {
        p->animationID = ani;
        p->animFrame   = frame;
        p->animTimer   = 0;
        p->animSpeed   = PLAYER_ANIM_SPEED_DEFAULT;
    }
}

/* Keep-frame variant — mirrors the SetSpriteAnimation sites that pass
 * `self->animator.frameID` (carry the current frame across the anim swap;
 * decomp Player.c:2976, 3905, 3921). */
static void player_set_anim_keepframe(player_t *p, int ani)
{
    if (p->animationID != ani) {
        p->animationID = ani;
        p->animTimer   = 0;
        p->animSpeed   = PLAYER_ANIM_SPEED_DEFAULT;
    }
}

/* === Player_HandleIdleAnimation (Player.c:2818-2854, ID_SONIC) ========= *
 *
 * timer < 240 -> ANI_IDLE; >= 240 -> ANI_BORED_1 (looping; resets timer to
 * 0 when frameID reaches 41). The decomp's timer==720 -> ANI_BORED_2 second
 * fidget is a DROPPED anim (build_entity_atlas.py keep/drop list — rare
 * 720-tick / 12-second idle, 175 KB VDP1+LWRAM cost). The Saturn port keeps
 * cycling ANI_BORED_1 past 720 instead of switching to the unresident
 * BORED_2; surfaced at the FR-1 checkpoint. */
static void Player_HandleIdleAnimation(player_t *p)
{
    if (p->timer != 720) {
        if (p->timer < 240) {
            p->timer++;
            player_set_anim(p, ANI_IDLE, 0);
        }
        else {
            p->timer++;
            if (p->animationID == ANI_BORED_1) {
                if (p->animFrame == 41)
                    p->timer = 0;
            }
            else {
                player_set_anim(p, ANI_BORED_1, 0);
            }
        }
    }
    else {
        /* decomp switches to ANI_BORED_2 here; dropped -> recycle BORED_1. */
        if (p->animationID != ANI_BORED_1)
            player_set_anim(p, ANI_BORED_1, 0);
        p->timer = 0;
    }
}

/* === Player_HandleGroundAnimation (Player.c:2917-3080) ================= *
 *
 * Mechanical port. The skid block (skidding > 0) selects ANI_SKID /
 * ANI_SKID_TURN; the walk block selects WALK/JOG/RUN/DASH by the
 * minJog/minRun/minDash velocity thresholds (with the decomp's hysteresis
 * reassignment of those thresholds); PUSH when pushing >= 3.
 *
 * Reductions, each cited:
 *   - The skid SFX (sfxSkidding) + Dust trail spawn (Player.c:2942-2944)
 *     are audio/effect-system concerns deferred with their systems.
 *   - The flailing/balance branch (Player.c:3016-3059) needs the 5-sensor
 *     ObjectTileGrip model; the Saturn per-column surface table has no
 *     flailing signal, so ANI_BALANCE_1/2 are DROPPED and the "not
 *     balancing" default path (Player_HandleIdleAnimation) is always taken.
 *   - ANI_OUTTA_HERE + Player_State_OuttaHere are unported; the
 *     72000000-tick (~333 h) threshold is unreachable in normal play, so the
 *     outtaHereTimer increment is kept but its trigger is inert. */
static void Player_HandleGroundAnimation(player_t *p)
{
    if (p->skidding > 0) {
        if (p->animationID != ANI_SKID) {
            if (p->animationID == ANI_SKID_TURN) {
                if (p->animFrame == p->animFrameCount - 1) {
                    p->facing_left = !p->facing_left;   /* direction ^= FLIP_X */
                    p->skidding    = 1;
                    player_set_anim(p, ANI_WALK, 0);
                }
            }
            else {
                player_set_anim(p, ANI_SKID, 0);
                if (iabs(p->gsp) >= 0x60000) {
                    if (iabs(p->gsp) >= 0xA0000)
                        p->animSpeed = 64;
                    else
                        p->animSpeed = 144;
                }
                else {
                    p->skidding -= 8;
                }
                /* sfxSkidding + Dust_State_DustTrail deferred. */
            }
        }
        else {
            int32_t spd = p->animSpeed;
            if (p->facing_left) {
                if (p->gsp >= 0)
                    player_set_anim(p, ANI_SKID_TURN, 0);
            }
            else if (p->gsp <= 0) {
                player_set_anim(p, ANI_SKID_TURN, 0);
            }
            p->animSpeed = spd;
        }

        --p->skidding;
    }
    else {
        if (p->pushing > -3 && p->pushing < 3) {
            if (p->gsp || (p->angle >= 0x20 && p->angle <= 0xE0)) {
                p->timer          = 0;
                p->outtaHereTimer = 0;

                int32_t velocity = iabs(p->gsp);
                if (velocity < p->minJogVelocity) {
                    if (p->animationID == ANI_JOG) {
                        if (p->animFrame == 9)
                            player_set_anim(p, ANI_WALK, 9);
                    }
                    else if (p->animationID == ANI_AIR_WALK) {
                        player_set_anim(p, ANI_WALK, p->animFrame);
                    }
                    else {
                        player_set_anim(p, ANI_WALK, 0);
                    }
                    p->animSpeed     = (velocity >> 12) + 48;
                    p->minJogVelocity = 0x40000;
                }
                else if (velocity < p->minRunVelocity) {
                    if (p->animationID != ANI_WALK || p->animFrame == 3)
                        player_set_anim(p, ANI_JOG, 0);
                    p->animSpeed     = (velocity >> 12) + 0x40;
                    p->minJogVelocity = 0x38000;
                    p->minRunVelocity = 0x60000;
                }
                else if (velocity < p->minDashVelocity) {
                    if (p->animationID == ANI_DASH || p->animationID == ANI_RUN)
                        player_set_anim(p, ANI_RUN, 0);
                    else
                        player_set_anim(p, ANI_RUN, 1);

                    int32_t s = (velocity >> 12) + 0x60;
                    p->animSpeed      = (s < 0x200) ? s : 0x200;
                    p->minRunVelocity = 0x58000;
                    p->minDashVelocity = 0xC0000;
                }
                else {
                    if (p->animationID == ANI_DASH || p->animationID == ANI_RUN)
                        player_set_anim(p, ANI_DASH, 0);
                    else
                        player_set_anim(p, ANI_DASH, 1);
                    p->minDashVelocity = 0xB8000;
                }
            }
            else {
                p->minJogVelocity  = 0x40000;
                p->minRunVelocity  = 0x60000;
                p->minDashVelocity = 0xC0000;

                /* flailing/balance detection dropped (see header) -> idle. */
                Player_HandleIdleAnimation(p);

                /* outtaHereTimer increment kept; trigger inert (see header). */
                p->outtaHereTimer++;
            }
        }
        else {
            if (p->pushing < -3) p->pushing = -3;
            else if (p->pushing > 3) p->pushing = 3;
            player_set_anim(p, ANI_PUSH, 0);
        }
    }
}

/* === Action_Jump (Player.c:3295-3329) ================================= *
 *
 * Decomp launches the jump along the ground normal:
 *   jumpForce  = gravityStrength + jumpStrength
 *   velocity.x = (groundVel * cos(angle) + jumpForce * sin(angle)) >> 8
 *   velocity.y = (groundVel * sin(angle) - jumpForce * cos(angle)) >> 8
 *
 * Saturn-port: identical math with the Q1.7 SIN8 (>>7 vs decomp >>8). */
static void player_action_jump(player_t *p)
{
    int a       = p->angle & 0xFF;
    int sin_q7  = SIN8[a];
    int cos_q7  = SIN8[(a + 64) & 0xFF];
    int32_t jumpForce = p->gravityStrength + p->jumpStrength;

    p->xsp = (p->gsp * cos_q7 + jumpForce * sin_q7) >> 7;
    p->ysp = (p->gsp * sin_q7 - jumpForce * cos_q7) >> 7;

    /* decomp Player.c:3312-3319 — ANI_JUMP (the ball) + speed ramp. The Tails
     * fixed-120 branch is character-gated; base Sonic uses the |gsp| ramp. */
    player_set_anim(p, ANI_JUMP, 0);
    {
        int32_t s = ((iabs(p->gsp) * 0xF0) / 0x60000) + 0x30;
        p->animSpeed = (s > 0xF0) ? 0xF0 : s;
    }

    p->onGround     = false;
    p->jumping      = true;
    p->applyJumpCap = true;
    p->angle        = 0;
    p->skidding     = 0;                  /* decomp Action_Jump (Player.c:3323) */
    p->jumpAbilityState = 1;              /* decomp Action_Jump: arm (Player.c:3325) */
    p->state        = PLAYER_STATE_AIR;   /* decomp Action_Jump: state=Air */
    /* Decomp also resets collisionMode/skidding and plays sfxJump — Phase 2.2
     * plays jump SFX from the caller (Game.c) so the cd/JUMPSFX.PCM load lives
     * near the other audio init. */
}

/* === Action_Roll (Player.c:3330-3340) ================================ *
 *
 * Decomp sets ANI_JUMP, zeroes pushing, switches state to Player_State_Roll,
 * and (on CMODE_FLOOR) nudges position.y by jumpOffset. The Saturn integrate
 * step re-snaps ypos to the surface table every grounded tick, so the
 * jumpOffset nudge is moot here (it would be overwritten the same frame) and
 * is intentionally omitted; the state + pushing reset are the load-bearing
 * parts. Animation (ANI_JUMP) lands with the player animation system in a
 * later increment. */
static void Player_Action_Roll(player_t *p)
{
    /* decomp Player.c:3334 — ANI_JUMP (the ball; roll shares the jump-ball
     * sprite). State_Roll ramps animSpeed from |gsp| each grounded tick. */
    player_set_anim(p, ANI_JUMP, 0);
    p->pushing = 0;
    p->state   = PLAYER_STATE_ROLL;
}

/* === HandleRollDeceleration (Player.c:3466-3556, CMODE_FLOOR branch) == *
 *
 * Decomp: reverse-press adds rollingDeceleration toward zero; otherwise
 * rollingFriction bleeds |groundVel| toward zero; slope force adds
 * 0x1400 (with-slope) / 0x5000 (against-slope) * Sin256(angle) >> 8, capped
 * at +/-0x120000; a sign reversal parks at groundVel=0 and returns to
 * State_Ground.
 *
 * Saturn port: the decomp's `0x1400 * Sin256(angle) >> 8` uses the full-scale
 * Sin256 table (~256*sin). The Saturn SIN8 table is Q1.7 (~127*sin ~= half
 * Sin256), so the equivalent slope force is `0x1400 * SIN8[angle] >> 7` (one
 * less right-shift) — identical magnitude. The sign of SIN8[angle] matches
 * Sin256(angle), so the with/against-slope branch keys on it directly. Only
 * the CMODE_FLOOR case of the decomp switch is ported (the Saturn collision
 * model is always floor-relative; LWALL/RWALL/ROOF/TubeRoll are out of
 * Phase 2.5.1 scope). */
static void Player_HandleRollDeceleration(player_t *p, bool left, bool right)
{
    int32_t initialVel = p->gsp;
    int     a      = p->angle & 0xFF;
    int     sin_q7 = SIN8[a];

    if (right && p->gsp < 0)
        p->gsp += p->rollingDeceleration;
    if (left && p->gsp > 0)
        p->gsp -= p->rollingDeceleration;

    if (p->gsp) {
        if (p->gsp < 0) {
            p->gsp += p->rollingFriction;
            if (sin_q7 >= 0)
                p->gsp += (0x1400 * sin_q7) >> 7;
            else
                p->gsp += (0x5000 * sin_q7) >> 7;
            if (p->gsp < -0x120000)
                p->gsp = -0x120000;
        } else {
            p->gsp -= p->rollingFriction;
            if (sin_q7 <= 0)
                p->gsp += (0x1400 * sin_q7) >> 7;
            else
                p->gsp += (0x5000 * sin_q7) >> 7;
            if (p->gsp > 0x120000)
                p->gsp = 0x120000;
        }
    } else {
        p->gsp += (0x5000 * sin_q7) >> 7;
    }

    /* CMODE_FLOOR reversal -> park + return to Ground (Player.c:3516-3519). */
    if ((p->gsp >= 0 && initialVel <= 0) || (p->gsp <= 0 && initialVel >= 0)) {
        p->gsp   = 0;
        p->state = PLAYER_STATE_GROUND;
    }
}

/* === State_Roll (Player.c:3932-3958) ================================= *
 *
 * Decomp: HandleGroundRotation (Saturn refreshes p->angle from the surface
 * table in Player_Tick before this runs), HandleRollDeceleration, clears
 * applyJumpCap, then on jumpPress -> Action_Jump. The animator-speed ramp
 * (Player.c:3947-3951) is a draw-side concern deferred with the player
 * animation system. Saturn projects the post-decel groundVel onto xsp/ysp so
 * the grounded integrate step (which reads xsp) advances the roll — mirrors
 * ProcessObjectMovement's `velocity.x = groundVel * cos >> 8`. */
static void Player_State_Roll(player_t *p, bool left, bool right, bool jump_press)
{
    Player_HandleRollDeceleration(p, left, right);

    p->applyJumpCap = false;

    /* decomp Player.c:3944-3951 — grounded roll animator-speed ramp (ANI_JUMP
     * ball spins faster the quicker you roll), cap 0xF0. */
    {
        int32_t s = ((iabs(p->gsp) * 0xF0) / 0x60000) + 0x30;
        p->animSpeed = (s > 0xF0) ? 0xF0 : s;
    }

    /* Project groundVel onto world axes along the surface tangent (same shape
     * as player_update_ground, Player.c port). */
    {
        int a      = p->angle & 0xFF;
        int sin_q7 = SIN8[a];
        int cos_q7 = SIN8[(a + 64) & 0xFF];
        p->xsp =  (p->gsp * cos_q7) >> 7;
        p->ysp = -(p->gsp * sin_q7) >> 7;
    }

    if (jump_press)
        player_action_jump(p);
}

/* === Action_Spindash (Player.c:3341-3355) ============================= *
 *
 * Decomp spawns a Dust entity (Dust_State_SpinDash), sets ANI_SPINDASH,
 * switches state to Player_State_Spindash, zeroes abilityTimer +
 * spindashCharge, and plays sfxCharge. The Saturn port carries the
 * load-bearing state transition + counter resets; the Dust effect, the
 * spindash animation, and the charge SFX land with the player animation /
 * SFX-channel systems in a later increment (same deferral as Action_Roll). */
static void Player_Action_Spindash(player_t *p)
{
    /* decomp Player.c:3350 — ANI_SPINDASH (forceApply=true). The Dust
     * spin-charge effect + sfxCharge land with the effect/SFX systems. */
    p->animationID = ANI_SPINDASH;
    p->animFrame   = 0;
    p->animTimer   = 0;
    p->animSpeed   = PLAYER_ANIM_SPEED_DEFAULT;

    p->state          = PLAYER_STATE_SPINDASH;
    p->abilityTimer   = 0;
    p->spindashCharge = 0;
}

/* === State_Crouch (Player.c:4082-4129) ================================ *
 *
 * Decomp: forces left=right=false, runs HandleGroundMovement + Gravity_False,
 * sets nextAirState=Air. With DOWN held: plays ANI_CROUCH (settle on frame 4),
 * counts `timer` to 60 then pans camera lookPos.y toward +/-96, and on
 * jumpPress launches Action_Spindash. Without DOWN: resumes the crouch anim
 * (speed=128) and returns to State_Ground once the anim reverses to frame 0
 * (or left/right is pressed), with jumpPress -> Action_Jump.
 *
 * Saturn port: the animator-frame settle (frameID==4 / frameID==0) is a
 * draw-side concern deferred with the player animation system, so the
 * un-crouch keys directly on releasing DOWN (or pressing left/right) — the
 * faithful state-level reduction. The camera lookPos pan lands in 2.5.3
 * (LookUp + camera-pan); 2.5.2 advances the `timer` hold counter that gates
 * it. left/right are forced false before HandleGroundMovement so groundVel
 * friction-decays to rest, exactly as the decomp. */
static void Player_State_Crouch(player_t *p, bool left, bool right, bool down,
                                bool jump_press)
{
    /* Decomp Player.c:4089-4092 — left/right forced false, then ground move. */
    player_update_ground(p, false, false, false);

    /* Decomp sets nextAirState = Player_State_Air (Player.c:4096); the Saturn
     * integrate step transitions to AIR directly on a cliff/pit, so the
     * nextAirState indirection is elided. */

    if (down) {
        /* Decomp Player.c:4099-4101 — ANI_CROUCH (frame 1); settle (speed 0)
         * once the anim reaches frame 4. */
        player_set_anim(p, ANI_CROUCH, 1);
        if (p->animFrame == 4)
            p->animSpeed = 0;

        /* Decomp Player.c:4103-4115 — 60-tick hold before the look-down pan. */
        if (p->timer < 60)
            p->timer++;
        /* else: camera->lookPos.y += 2 (cap 96) — wired in 2.5.3 camera-pan. */

        if (jump_press)
            Player_Action_Spindash(p);
    } else {
        /* Decomp Player.c:4122-4128 — resume the crouch anim (speed 128) and
         * un-crouch back to Ground. The Saturn state-level reduction keys the
         * exit on releasing DOWN (the decomp keys on the anim reversing to
         * frame 0); the speed bump still drives the stand-up frames the tick
         * before the state swap. */
        p->animSpeed = 128;
        p->state = PLAYER_STATE_GROUND;
        if (jump_press)
            player_action_jump(p);
    }
    (void)left; (void)right;
}

/* === State_Spindash (Player.c:4131-4188) ============================== *
 *
 * Decomp: each jumpPress while charging adds 0x20000 to abilityTimer (cap
 * 0x90000) and bumps spindashCharge (0..12, the sfx-pitch index); with no
 * jumpPress abilityTimer bleeds down by `abilityTimer >> 5`. When DOWN is
 * released the charge converts to launch ground speed
 * `(((uint32)abilityTimer >> 1) & 0x7FFF8000) + 0x80000` (super adds 0xB0000,
 * deferred to 2.5.9), signed by facing, and the player drops into
 * Player_State_Roll.
 *
 * Saturn port: the chargeSpeeds[] SFX-pitch table + SetChannelAttributes, the
 * ANI_SPINDASH/ANI_JUMP animations, the Dust effect, and the camera
 * scrollDelay/FollowY (Player.c:4161-4164) are draw/SFX/camera-side concerns
 * deferred with their respective systems. The collisionMode jumpOffset nudge
 * (Player.c:4182-4183) is moot under the Saturn surface re-snap, identical to
 * the Action_Roll deferral. On release the launched groundVel is projected
 * onto xsp/ysp immediately (mirrors Player_State_Roll) so the grounded
 * integrate advances the very same frame. */
static void Player_State_Spindash(player_t *p, bool down, bool jump_press)
{
    if (jump_press) {
        p->abilityTimer += 0x20000;
        if (p->abilityTimer > 0x90000)
            p->abilityTimer = 0x90000;
        if (p->spindashCharge < 12)
            p->spindashCharge++;
        if (p->spindashCharge < 0)
            p->spindashCharge = 0;
        /* ANI_SPINDASH + sfxCharge pitch (chargeSpeeds[spindashCharge]) —
         * deferred with the player animation / SFX-channel systems. */
    } else {
        p->abilityTimer -= p->abilityTimer >> 5;
    }

    /* Pin the player horizontally while charging (decomp holds position; its
     * ProcessObjectMovement derives velocity from a near-zero groundVel). */
    p->xsp = 0;
    p->ysp = 0;

    if (!down) {
        /* Decomp Player.c:4166-4186 — release into a roll. */
        int32_t vel = (int32_t)((((uint32_t)p->abilityTimer >> 1) & 0x7FFF8000)
                                + 0x80000);
        /* super adds 0xB0000 (Player.c:4168) — deferred to 2.5.9. */
        p->gsp     = p->facing_left ? -vel : vel;
        p->pushing = 0;
        p->state   = PLAYER_STATE_ROLL;

        /* Project the launch speed onto world axes along the surface tangent
         * (same shape as Player_State_Roll) so the grounded integrate moves
         * this frame. */
        {
            int a      = p->angle & 0xFF;
            int sin_q7 = SIN8[a];
            int cos_q7 = SIN8[(a + 64) & 0xFF];
            p->xsp =  (p->gsp * cos_q7) >> 7;
            p->ysp = -(p->gsp * sin_q7) >> 7;
        }
    }
}

/* === State_LookUp (Player.c:4026-4081) ================================ *
 *
 * Decomp: Gravity_False, nextAirState=Air. With UP held: forces left=right=
 * false, HandleGroundMovement, plays ANI_LOOK_UP (settle frame 5), counts
 * `timer` to 60 then pans camera lookPos.y toward -96 at 2/frame (the
 * invertGravity branch pans toward +96, unused for base Sonic); jumpPress
 * runs statePeelout if set else Action_Jump. Without UP: resumes the anim
 * (speed=64) and returns to State_Ground once the anim reverses to frame 0
 * (or left/right is pressed), with jumpPress -> Action_Jump.
 *
 * Saturn port: the camera is owned by Game.c, so lookPos rides on player_t
 * and Game.c's camera-follow folds it into cam_y; the recenter-toward-0 when
 * leaving LookUp/Crouch (decomp Player_Update Player.c:35-40) lands in
 * Player_Tick below. The animator-frame settle (frameID==0) is a draw-side
 * concern, so the un-look-up keys directly on releasing UP (or pressing
 * left/right) — the faithful state-level reduction, mirroring Crouch.
 *
 * statePeelout (Player.c:4062) is a medal-mod ability (Encore/medal Sonic);
 * `self->statePeelout` is unset for base Mania Sonic, so the Saturn port
 * takes the Action_Jump branch unconditionally. Peelout is surfaced as an
 * optional default-OFF follow-on, not silently dropped. */
static void Player_State_LookUp(player_t *p, bool left, bool right, bool up,
                                bool jump_press)
{
    if (up) {
        /* Decomp Player.c:4037-4041 — left/right forced false, then ground move. */
        player_update_ground(p, false, false, false);

        /* Decomp Player.c:4043-4045 — ANI_LOOK_UP (frame 1); settle (speed 0)
         * once the anim reaches frame 5. */
        player_set_anim(p, ANI_LOOK_UP, 1);
        if (p->animFrame == 5)
            p->animSpeed = 0;

        /* Decomp Player.c:4047-4058 — 60-tick hold, then pan lookPos to -96. */
        if (p->timer < 60) {
            p->timer++;
        } else {
            if (p->lookPos > -96)
                p->lookPos -= 2;
        }

        if (jump_press)
            player_action_jump(p);  /* statePeelout default-OFF for base Sonic */
    } else {
        /* Decomp Player.c:4071-4079 — resume movement (anim speed 64); left/
         * right (or the anim reversing to frame 0) returns to Ground. Saturn
         * keys the exit on releasing UP, the state-level reduction of the
         * frameID settle. */
        player_update_ground(p, left, right, false);
        p->animSpeed = 64;
        p->state = PLAYER_STATE_GROUND;
        if (jump_press)
            player_action_jump(p);
    }
}

/* === State_Ground (Player.c:3801-3870) ================================ *
 *
 * Decomp dispatches to HandleGroundMovement, HandleGroundAnimation,
 * checks the jump button, the down+|gsp|>=minRollVel roll-init, and the
 * up/down crouch/look. Phase 2.5.2 adds the at-rest crouch-init (down on
 * flat ground -> Crouch); Phase 2.5.3 adds the at-rest lookup-init (up on
 * flat ground -> LookUp). */
static void player_state_ground(player_t *p, bool left, bool right, bool down,
                                bool up, bool jump_press)
{
    player_update_ground(p, left, right, down);

    /* decomp Player.c:3804 — HandleGroundAnimation runs every grounded tick
     * (after HandleGroundMovement, before the jump/roll/crouch checks),
     * selecting IDLE/WALK/JOG/RUN/DASH/SKID/PUSH from groundVel + skidding +
     * pushing. This is what makes Sonic visibly walk/run/skid/idle. */
    Player_HandleGroundAnimation(p);

    if (jump_press) {
        player_action_jump(p);
        p->timer = 0;                 /* decomp Player.c:3846 */
    } else if (p->gsp) {
        /* Roll-init — decomp Player.c:3849-3854. minRollVel is 0x11000 when
         * entering from Crouch and 0x8800 from a run. In practice this runs
         * only with state==GROUND (the switch dispatches Crouch to
         * Player_State_Crouch), so the run threshold applies; the ternary
         * mirrors the decomp exactly. DOWN held, neither LEFT nor RIGHT,
         * at/above minRollVel -> roll. */
        int32_t minRollVel = (p->state == PLAYER_STATE_CROUCH) ? 0x11000 : 0x8800;
        if (iabs(p->gsp) >= minRollVel && !left && !right && down) {
            Player_Action_Roll(p);
        }
    } else {
        /* At-rest crouch/lookup init — decomp Player.c:3856-3866. Flat-ground
         * guard: angle < 0x20 || angle > 0xE0 (collisionMode always floor on
         * Saturn). DOWN -> Crouch; UP -> LookUp (2.5.3). */
        int ang = p->angle & 0xFF;
        if (ang < 0x20 || ang > 0xE0) {
            if (up) {
                /* decomp Player.c:3857-3861 — ANI_LOOK_UP, timer=0, LookUp. */
                p->timer = 0;
                p->state = PLAYER_STATE_LOOKUP;
            } else if (down) {
                p->timer = 0;
                p->state = PLAYER_STATE_CROUCH;
            }
        }
    }
}

/* === JumpAbility_Sonic (Player.c:6114-6216, base-Sonic dropdash) ====== *
 *
 * The decomp's stateAbility is invoked from State_Air (Player.c:3914-3917)
 * once the jump animation is active AND velocity.y has reached the jump cap
 * (apex). For base Mania Sonic with no shield and no medal mods the flow is:
 *   - Action_Jump set jumpAbilityState = 1.
 *   - dropdashDisabled = (jumpAbilityState <= 1).
 *   - jumpAbilityState == 1 + jumpPress + SHIELD_NONE:
 *         jumpAbilityState = (~(medalMods & 0xFF) >> 3) & 2.
 *         medalMods == 0 for base Sonic -> (~0 >> 3) & 2 == 2 (dropdash on).
 *         The decomp `return`s on this frame (the arm frame).
 *   - thereafter, !dropdashDisabled && jumpHold -> ++jumpAbilityState; at
 *         >= 22 -> state = Player_State_DropDash, ANI_DROPDASH.
 * Shield branches (SHIELD_BUBBLE/FIRE/LIGHTNING) + the invincibleTimer/
 * insta-shield + super TryTransform paths are 2.5.7/2.5.9 scope and are
 * intentionally absent here; base Sonic always takes the SHIELD_NONE arm. */
static void Player_JumpAbility_Sonic(player_t *p, bool jump_press)
{
    bool dropdash_disabled = (p->jumpAbilityState <= 1);

    if (p->jumpAbilityState == 1) {
        if (jump_press) {
            /* SHIELD_NONE: (~(medalMods & 0xFF) >> 3) & 2 with medalMods=0 =>
             * 2 (dropdash enabled). Decomp returns on the arm frame. */
            p->jumpAbilityState = 2;
        }
        return;
    }

    if (!dropdash_disabled && p->jumpHold) {
        if (++p->jumpAbilityState >= 22) {
            p->state = PLAYER_STATE_DROPDASH;
            /* decomp Player.c:6212 — ANI_DROPDASH (the charged spin). sfxDropdash
             * lands with the audio system. */
            player_set_anim(p, ANI_DROPDASH, 0);
        }
    }
}

/* === State_Air (Player.c:3871-3931) =================================== *
 *
 * Decomp does HandleAirFriction + HandleAirMovement, then if onGround
 * (set by ProcessObjectMovement post-physics) transitions back to
 * State_Ground. Saturn-side the surface-snap below handles the onGround
 * latch.
 *
 * Phase 2.5.4 — invoke the jump ability. The decomp gates the stateAbility
 * call on animationID == ANI_JUMP && velocity.y >= jumpCap (Player.c:3914-
 * 3917): only after a genuine jump (not a spring/hurt launch) and only once
 * the upward burst has decayed to the jump cap (apex region). The Saturn
 * model has no per-frame animationID, so `jumping` (set true only by
 * player_action_jump) stands in for ANI_JUMP, and `ysp >= jumpCap` mirrors
 * the velocity gate (jumpCap is negative, so this is true from the apex on). */
static void player_state_air(player_t *p, bool left, bool right,
                             bool jump_held, bool jump_press)
{
    player_update_air(p, left, right, jump_held);

    /* decomp Player.c:3900-3929 — air-anim switch. Now that the Saturn player
     * carries animationID, the JumpAbility (dropdash) gate keys on
     * animationID == ANI_JUMP && velocity.y >= jumpCap exactly as the decomp
     * (replaces the Phase 2.5.4 `jumping`-bool stand-in). The spring-twirl
     * pre-block (Player.c:3889-3898) is for DROPPED spring anims and is
     * omitted. */
    switch (p->animationID) {
        case ANI_IDLE:
        case ANI_WALK:
            if (p->animSpeed < 64)
                p->animSpeed = 64;
            player_set_anim_keepframe(p, ANI_AIR_WALK);
            break;

        case ANI_LOOK_UP:
        case ANI_CROUCH:
        case ANI_SKID_TURN:
        case ANI_JOG:
            player_set_anim(p, ANI_AIR_WALK, 0);
            break;

        case ANI_JUMP:
            if (p->ysp >= p->jumpCap)
                Player_JumpAbility_Sonic(p, jump_press);
            break;

        case ANI_SKID:
            if (p->skidding <= 0)
                player_set_anim_keepframe(p, ANI_AIR_WALK);
            else
                p->skidding--;
            break;

        case ANI_SPINDASH:
            player_set_anim(p, ANI_JUMP, 0);
            break;

        default: break;
    }
}

/* === State_DropDash (Player.c:4455-4543, base-Sonic onGround launch) == *
 *
 * On landing (onGround) the dropdash launches into a roll: base vel 0x80000,
 * cap 0xC0000 (the super 0xC0000/0xD0000 + Camera_ShakeScreen branch is 2.5.9
 * scope). The launch direction keys on facing + current horizontal velocity:
 *   facing right: velocity.x >= 0 -> groundVel = vel + (groundVel>>2) cap
 *       +cap; else if angle -> vel + (groundVel>>1); else -> vel.
 *   facing left : mirror with negative sign.
 * Then pushing=0, state=Roll. While still airborne with jumpHold held the
 * decomp keeps air friction/movement running (+ a Dust/anim ramp deferred
 * here); releasing jumpHold mid-air cancels: jumpAbilityState=0, state=Air.
 * The onGround landing-latch is handled by the integrate step (which leaves
 * state==DROPDASH so this onGround branch fires the frame after touchdown). */
static void Player_State_DropDash(player_t *p, bool left, bool right,
                                  bool jump_held)
{
    if (p->onGround) {
        int32_t vel = 0x80000;
        int32_t cap = 0xC0000;

        if (right) p->facing_left = false;
        if (left)  p->facing_left = true;

        if (p->facing_left) {
            if (p->xsp <= 0) {
                p->gsp = (p->gsp >> 2) - vel;
                if (p->gsp < -cap) p->gsp = -cap;
            } else if (p->angle) {
                p->gsp = (p->gsp >> 1) - vel;
            } else {
                p->gsp = -vel;
            }
        } else {
            if (p->xsp >= 0) {
                p->gsp = vel + (p->gsp >> 2);
                if (p->gsp > cap) p->gsp = cap;
            } else if (p->angle) {
                p->gsp = vel + (p->gsp >> 1);
            } else {
                p->gsp = vel;
            }
        }

        p->pushing = 0;
        p->state   = PLAYER_STATE_ROLL;
    } else if (jump_held) {
        /* Continue the dropdash charge in the air (air friction + movement;
         * the animator-speed ramp + Dust spawn are deferred). */
        player_update_air(p, left, right, jump_held);
    } else {
        /* jumpHold released before landing -> cancel back to normal air. */
        p->jumpAbilityState = 0;
        p->state            = PLAYER_STATE_AIR;
    }
}

/* === Player_Tick — Update body + ProcessObjectMovement collision ====== *
 *
 * Decomp Player_Update (Player.c:14-152) dispatches the StateMachine
 * then calls RSDK.ProcessObjectMovement which probes the 6 sensors and
 * commits the new position. Saturn port collapses to: state machine →
 * per-column surface-snap with wall/cliff thresholds.
 *
 * Phase 2.4g.3 — `__attribute__((used))`: Player_Tick has a single caller
 * (Game.c mania_tick), so GCC 8.2 whole-program LTO fully inlines it and
 * drops the named symbol from game.map. The two-plane bridge selection
 * (active_path = collisionPlane) lives inside this function, so the
 * 2.4g.3 gate (qa_phase2_4g3_gate.py P2.2c) must be able to locate it by
 * name. `used` keeps a stable named symbol — same LTO defeat as
 * InvisibleBlock.c's QA landmarks (memory/sync-load + entity-atlas rules). */
__attribute__((used)) void Player_Tick(player_t *p, const sms_world_t *w,
                 bool left, bool right, bool down, bool up,
                 bool jump_held)
{
    /* Jump button edge detect — needed for Action_Jump trigger. */
    bool jump_press = jump_held && !p->prev_jump;

    /* Phase 2.5.4 — latch the held jump button for the dropdash charge ramp.
     * Decomp Player_Input_P1 mirrors jumpHoldButton into entity->jumpHold each
     * frame (Player.c:6421); JumpAbility_Sonic / State_DropDash read it. */
    p->jumpHold = jump_held;

    /* Phase 2.4g.3 — two-plane bridge. Select the collision-plane table
     * BEFORE any surface probe this step. p->collisionPlane is written by
     * PlaneSwitch_CheckCollisions (PlaneSwitch.c:94,103) as the player
     * crosses a switch marker. `w` is const in the probe signatures (they
     * only read); active_path is the one mutable selector, set here once
     * per tick. Cast is local + benign — sms_world_t lives in Game.c BSS,
     * not ROM. */
    ((sms_world_t *)w)->active_path = (int)(p->collisionPlane & 1);

    /* Camera lookPos recenter — decomp Player_Update (Player.c:35-40). When
     * the player is NOT actively panning (state != LookUp && != Crouch), the
     * look offset bleeds back toward 0 at 2/frame so the viewport recenters.
     * Runs before the state machine, mirroring the decomp order; p->state
     * here reflects last frame's state (same as the decomp's self->state). */
    if (p->state != PLAYER_STATE_LOOKUP && p->state != PLAYER_STATE_CROUCH) {
        if (p->lookPos > 0)
            p->lookPos -= 2;
        else if (p->lookPos < 0)
            p->lookPos += 2;
    }

    /* State dispatch — Phase 2.5.1 mirrors the decomp's `StateMachine
     * self->state` selector (Player_Update -> StateMachine_Run) with an
     * explicit switch. Ground/Air bodies are byte-identical to Phase 2.2;
     * Roll is the new case. onGround remains the integrate selector below and
     * is kept in sync with state (cliff/pit -> AIR, land -> GROUND). */
    switch (p->state) {
        case PLAYER_STATE_ROLL:
            /* HandleGroundRotation: refresh the ground angle from the surface
             * column before the roll-decel curve consumes it. */
            p->angle = Player_SurfaceAngle(w, p->xpos >> 16);
            Player_State_Roll(p, left, right, jump_press);
            break;

        case PLAYER_STATE_AIR:
            player_state_air(p, left, right, jump_held, jump_press);
            break;

        case PLAYER_STATE_DROPDASH:
            /* Airborne charge or the onGround launch-into-roll. No ground-angle
             * refresh: while airborne the angle is unused, and on the landing
             * frame the launch reads gsp/facing only (Player.c:4455-4543). */
            Player_State_DropDash(p, left, right, jump_held);
            break;

        case PLAYER_STATE_CROUCH:
            /* At-rest; refresh the ground angle (HandleGroundMovement consumes
             * the slope force as groundVel friction-decays to zero). */
            p->angle = Player_SurfaceAngle(w, p->xpos >> 16);
            Player_State_Crouch(p, left, right, down, jump_press);
            break;

        case PLAYER_STATE_SPINDASH:
            /* Charging in place; refresh the ground angle so the release
             * launch is projected onto the correct surface tangent. */
            p->angle = Player_SurfaceAngle(w, p->xpos >> 16);
            Player_State_Spindash(p, down, jump_press);
            break;

        case PLAYER_STATE_LOOKUP:
            /* At-rest; refresh the ground angle (HandleGroundMovement
             * consumes the slope force as groundVel friction-decays). */
            p->angle = Player_SurfaceAngle(w, p->xpos >> 16);
            Player_State_LookUp(p, left, right, up, jump_press);
            break;

        case PLAYER_STATE_GROUND:
        default:
            /* Refresh ground angle from the column we're over BEFORE running
             * the state — slope force consumes it. */
            p->angle = Player_SurfaceAngle(w, p->xpos >> 16);
            player_state_ground(p, left, right, down, up, jump_press);
            break;
    }

    /* === Integrate position + ProcessObjectMovement-equivalent =========
     *
     * Decomp's ProcessObjectMovement does sub-pixel stepping + 6-sensor
     * probes. Phase 2.2 does the simpler archived integrate-then-snap:
     *   - grounded: advance xpos by xsp, snap ypos to surface
     *     unless wall (step >17 up) or cliff (step >17 down).
     *   - airborne: integrate xsp/ysp, snap to surface on downward
     *     intersection.
     *
     * Citation: archived src/_archived/player.c:225-288 (verbatim shape).
     */
    if (p->onGround) {
        int32_t new_x  = p->xpos + p->xsp;
        int     new_xi = new_x >> 16;
        if (new_xi < 0)              new_xi = 0;
        if (new_xi >= w->width_px)   new_xi = w->width_px - 1;

        int cur_y = p->ypos >> 16;
        int new_y = Player_SurfaceY(w, new_xi);

        if (new_y == SMS_NO_FLOOR) {
            /* Pit (none in GHZ Act 1 per inspection, but the code path
             * is needed for GHZ Act 2 + later zones). Drop off. */
            p->xpos    = new_x;
            p->onGround= false;
            p->jumping = false;
            p->angle   = 0;
            p->state   = PLAYER_STATE_AIR;
        } else if (new_y < cur_y - WALL_STEP_PX) {
            /* Wall / steep step blocks horizontal motion (decomp
             * pushing=1 + groundVel=0; we just zero the velocity). */
            p->gsp = 0;
            p->xsp = 0;
        } else if (new_y > cur_y + CLIFF_DROP_PX) {
            /* Cliff edge — leave the ground with current slope-projected
             * velocity (Mania-faithful: gsp carries through to air xsp). */
            p->xpos    = new_x;
            p->onGround= false;
            p->jumping = false;
            p->angle   = 0;
            p->state   = PLAYER_STATE_AIR;
        } else {
            /* Walkable: follow the surface. */
            p->xpos = new_x;
            p->ypos = ((int32_t)new_y) << 16;
        }
    } else {
        /* Airborne integrate. */
        p->xpos += p->xsp;
        p->ypos += p->ysp;

        int xi = p->xpos >> 16;
        if (xi < 0)              xi = 0;
        if (xi >= w->width_px)   xi = w->width_px - 1;
        int floor_y = Player_SurfaceY(w, xi);

        if (floor_y != SMS_NO_FLOOR && p->ysp >= 0 &&
            (p->ypos >> 16) >= floor_y) {
            /* Land — decomp transitions State_Air → State_Ground when
             * onGround latches. groundVel inherits xsp. */
            p->ypos       = ((int32_t)floor_y) << 16;
            p->onGround   = true;
            p->jumping    = false;
            p->applyJumpCap = false;
            p->gsp        = p->xsp;
            p->ysp        = 0;
            /* Land -> Ground (decomp). Exception: a DROPDASH that touches down
             * must keep its state for one more tick so Player_State_DropDash's
             * onGround branch fires the launch-into-roll next frame (decomp
             * State_DropDash, Player.c:4458-4540 — the onGround launch runs in
             * the dropdash state itself, not in State_Ground). */
            if (p->state != PLAYER_STATE_DROPDASH)
                p->state  = PLAYER_STATE_GROUND;
        } else if ((p->ypos >> 16) >= w->height_px) {
            /* Fell off the bottom of the level. Phase 2.2 just clamps;
             * Phase 2.3 will add death state + respawn. */
            p->ypos = ((int32_t)(w->height_px - 1)) << 16;
            p->ysp  = 0;
        }
    }

    p->prev_jump = jump_held;
}
