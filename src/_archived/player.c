/*
 * player.c - Classic Sonic ground/air physics state machine.
 *
 * Faithful to the Sonic Physics Guide model that Mania reproduces:
 *   - ground accel / decel(skid) / friction with a top speed
 *   - rolling with halved friction and no self-acceleration
 *   - air accel, gravity with terminal cap, air drag, and jump-cut
 *
 * On the flat bootstrap world (angle == 0) the ground projection reduces to
 * xsp == gsp and ysp == 0; the angle-aware terms are written out so Phase 3
 * collision can supply real tile angles with no change to this file's shape.
 */
#include <jo/jo.h>
#include "player.h"
#include "physics.h"

/* sign helper for Q16.16 values */
static jo_fixed fsign(jo_fixed v) { return (v > 0) - (v < 0); }

/* Q1.7 sin table for the Mania Q0.8 angle byte (0..255 = full circle).
 * Read as SIN8[(unsigned char)angle] returning -127..127 ~ -1.0..+1.0.
 * Generated from sin(i * 2*pi / 256) rounded to nearest integer * 127. */
static const signed char SIN8[256] = {
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

void player_init(player_t *p, character_id who, jo_fixed x, jo_fixed y)
{
    p->xpos = x;
    p->ypos = y;
    p->xsp = 0;
    p->ysp = 0;
    p->gsp = 0;
    p->angle = 0;
    p->grounded = false;
    p->rolling = false;
    p->jumping = false;
    p->facing_left = false;
    p->character = who;
    p->prev_jump = false;

    switch (who) {
        case CHAR_KNUCKLES: p->jump_force = PHYS_JUMP_KNUX;  break;
        default:            p->jump_force = PHYS_JUMP_SONIC; break;
    }
}

/* Apply ground friction toward zero by amount `frc`. */
static void apply_friction(player_t *p, jo_fixed frc)
{
    jo_fixed mag = JO_ABS(p->gsp);
    jo_fixed dec = (mag < frc) ? mag : frc;
    p->gsp -= fsign(p->gsp) * dec;
}

/* Mania running slope factor = 0.125 px/frame^2. In Q16.16: 0.125*65536 = 8192. */
#define SLOPE_FACTOR_RUN_Q16  8192

static void update_ground(player_t *p, bool left, bool right, bool down)
{
    /* Slope force: gsp -= slope_factor * sin(angle). Reduces gsp going
     * uphill (positive sin), increases it going downhill (negative sin).
     * SIN8 is Q1.7; (Q16.16 * Q1.7) >> 7 = Q16.16. */
    p->gsp -= (SLOPE_FACTOR_RUN_Q16 * (jo_fixed)SIN8[p->angle & 0xFF]) >> 7;

    if (p->rolling) {
        /* Rolling: only friction (halved) and skid-decel; no acceleration. */
        apply_friction(p, PHYS_ROLL_FRC);

        if (left && p->gsp > 0)  p->gsp -= PHYS_ROLL_DEC;
        if (right && p->gsp < 0) p->gsp += PHYS_ROLL_DEC;

        if (p->gsp >  PHYS_ROLL_TOP) p->gsp =  PHYS_ROLL_TOP;
        if (p->gsp < -PHYS_ROLL_TOP) p->gsp = -PHYS_ROLL_TOP;

        if (JO_ABS(p->gsp) < PHYS_UNROLL_MIN) p->rolling = false;
    } else {
        if (right) {
            if (p->gsp < 0) {                 /* skidding from leftward run */
                p->gsp += PHYS_DEC;
            } else if (p->gsp < PHYS_TOP) {
                p->gsp += PHYS_ACC;
                if (p->gsp > PHYS_TOP) p->gsp = PHYS_TOP;
            }
            p->facing_left = false;
        } else if (left) {
            if (p->gsp > 0) {                 /* skidding from rightward run */
                p->gsp -= PHYS_DEC;
            } else if (p->gsp > -PHYS_TOP) {
                p->gsp -= PHYS_ACC;
                if (p->gsp < -PHYS_TOP) p->gsp = -PHYS_TOP;
            }
            p->facing_left = true;
        } else {
            apply_friction(p, PHYS_FRC);
        }

        /* Crouch + speed -> begin a roll. */
        if (down && JO_ABS(p->gsp) >= PHYS_ROLL_MIN)
            p->rolling = true;
    }

    /* Project gsp onto the world axes along the surface tangent. For flat
     * ground (angle 0): cos(0)=1 -> xsp=gsp, sin(0)=0 -> ysp=0. For sloped
     * ground, xsp/ysp pick up the slope component so a jump launched here
     * goes perpendicular to the slope surface (Mania-faithful). */
    {
        int a  = p->angle & 0xFF;
        int sin_q7 = SIN8[a];                      /* sin(angle) in Q1.7 */
        int cos_q7 = SIN8[(a + 64) & 0xFF];        /* cos = sin shifted +90 deg */
        p->xsp =  (p->gsp * cos_q7) >> 7;
        p->ysp = -(p->gsp * sin_q7) >> 7;
    }
}

static void update_air(player_t *p, bool left, bool right)
{
    /* Air acceleration up to the same top speed. */
    if (right && p->xsp < PHYS_TOP) {
        p->xsp += PHYS_AIR_ACC;
        if (p->xsp > PHYS_TOP) p->xsp = PHYS_TOP;
        p->facing_left = false;
    } else if (left && p->xsp > -PHYS_TOP) {
        p->xsp -= PHYS_AIR_ACC;
        if (p->xsp < -PHYS_TOP) p->xsp = -PHYS_TOP;
        p->facing_left = true;
    }

    /* Air drag: bleed horizontal speed near the apex (-4 < ysp < 0). */
    if (p->ysp < 0 && p->ysp > SMS_FIXED(-4.0))
        p->xsp -= (p->xsp >> 5);

    /* Gravity with terminal velocity cap. */
    p->ysp += PHYS_GRAVITY;
    if (p->ysp > PHYS_TOP_Y) p->ysp = PHYS_TOP_Y;
}

/*
 * Per-column surface lookup. The GHZSURF.BIN entry for column x is 4 bytes:
 *   [0..1] big-endian u16 surface world-Y in pixels (0xFFFF = no floor / pit)
 *   [2]    Mania Q0.8 floor angle (0=flat, 64=right wall, 128=ceiling, 192=left
 *          wall; clockwise positive, so negative-signed = ground tilting up to
 *          the right -> uphill when facing right).
 *   [3]    flag byte (spikes / hazards -- future)
 * SH-2 is big-endian so we can read the u16 directly via a u16 pointer.
 */
int player_surface_y(const sms_world_t *w, int x_px)
{
    if (x_px < 0)               x_px = 0;
    if (x_px >= w->width_px)    x_px = w->width_px - 1;
    return (int) *((const unsigned short *)(w->raw + (x_px << 2)));
}

/* Return the Mania Q0.8 floor angle (unsigned 0..255) at world column x_px.
 * SIN8 lookup table lives at the top of this file (used by update_ground). */
int player_surface_angle(const sms_world_t *w, int x_px)
{
    if (x_px < 0)               x_px = 0;
    if (x_px >= w->width_px)    x_px = w->width_px - 1;
    return (int) w->raw[(x_px << 2) + 2];
}

/* Mania-faithful stair tolerance: a full 16-px tile step (1 tile up) is
 * walkable; only steps of more than 16 px count as walls. Same on the way
 * down -- a 16-px drop stays grounded, a bigger drop leaves you airborne
 * (cliff edge). The RSDK GHZ surface table has many 16-px features (grass
 * onto checkerboard dirt, dirt rises) that play as stairs, not walls. */
#define WALL_STEP_PX     17
#define CLIFF_DROP_PX    17

void player_update(player_t *p, const sms_world_t *w, bool autorun)
{
    bool left  = jo_is_pad1_key_pressed(JO_KEY_LEFT);
    bool right = jo_is_pad1_key_pressed(JO_KEY_RIGHT) || autorun;
    bool down  = jo_is_pad1_key_pressed(JO_KEY_DOWN);
    bool jump_held = jo_is_pad1_key_pressed(JO_KEY_A) ||
                     jo_is_pad1_key_pressed(JO_KEY_B) ||
                     jo_is_pad1_key_pressed(JO_KEY_C);
    bool jump_pressed = jump_held && !p->prev_jump;

    if (p->grounded) {
        /* Refresh the ground angle from the surface table at our current
         * column; update_ground / slope_force / projection all consume it. */
        p->angle = player_surface_angle(w, JO_FIXED_TO_INT(p->xpos));

        update_ground(p, left, right, down);

        if (jump_pressed) {
            /* Jump along the ground normal -- in terms of the floor's angle:
             *   xsp -= jump_force * sin(angle)
             *   ysp -= jump_force * cos(angle)
             * SIN8 is Q1.7; (Q16.16 * Q1.7) >> 7 = Q16.16. */
            int a      = p->angle & 0xFF;
            int sin_q7 = SIN8[a];
            int cos_q7 = SIN8[(a + 64) & 0xFF];
            p->xsp -= (p->jump_force * sin_q7) >> 7;
            p->ysp -= (p->jump_force * cos_q7) >> 7;
            p->grounded = false;
            p->jumping  = true;
            p->rolling  = false;
        }
    } else {
        update_air(p, left, right);

        /* Variable jump height: releasing early caps upward speed. */
        if (p->jumping && !jump_held && p->ysp < -PHYS_JUMP_CUT)
            p->ysp = -PHYS_JUMP_CUT;
    }

    /* --- Integrate horizontal position with wall block / cliff fall ---- */
    if (p->grounded) {
        /* Advance along the surface tangent. xsp = gsp*cos(angle) so this
         * naturally slows horizontal travel on steep slopes; the matching
         * vertical component is absorbed by the surface_y snap below. */
        jo_fixed new_x  = p->xpos + p->xsp;
        int      new_xi = JO_FIXED_TO_INT(new_x);
        if (new_xi < 0)                  new_xi = 0;
        if (new_xi >= w->width_px)       new_xi = w->width_px - 1;

        int cur_y = JO_FIXED_TO_INT(p->ypos);
        int new_y = player_surface_y(w, new_xi);

        if (new_y == SMS_NO_FLOOR) {
            /* Pit: leave the ground; xsp/ysp already carry the slope-projected
             * velocity from update_ground, so just clear grounded state. */
            p->xpos     = new_x;
            p->grounded = false;
            p->jumping  = false;
            p->angle    = 0;
        } else if (new_y < cur_y - WALL_STEP_PX) {
            /* Wall / steep upward step blocks horizontal motion. */
            p->gsp = 0;
            p->xsp = 0;
        } else if (new_y > cur_y + CLIFF_DROP_PX) {
            /* Cliff edge: leave the ground keeping current slope-projected
             * velocity. */
            p->xpos     = new_x;
            p->grounded = false;
            p->jumping  = false;
            p->angle    = 0;
        } else {
            /* Walkable: follow the ground surface (slopes / stairs). */
            p->xpos = new_x;
            p->ypos = JO_MULT_BY_65536(new_y);
        }
    } else {
        /* Airborne: integrate with gravity, check landing against surface. */
        p->xpos += p->xsp;
        p->ypos += p->ysp;

        int xi = JO_FIXED_TO_INT(p->xpos);
        if (xi < 0)                xi = 0;
        if (xi >= w->width_px)     xi = w->width_px - 1;
        int floor_y = player_surface_y(w, xi);

        if (floor_y != SMS_NO_FLOOR && p->ysp >= 0 &&
            JO_FIXED_TO_INT(p->ypos) >= floor_y) {
            p->ypos     = JO_MULT_BY_65536(floor_y);
            p->grounded = true;
            p->jumping  = false;
            p->gsp      = p->xsp;
            p->ysp      = 0;
        } else if (JO_FIXED_TO_INT(p->ypos) >= w->height_px) {
            /* Fell off the bottom of the level: clamp + respawn-ready. The
             * full respawn flow lives in the Phase 5 game shell; for now we
             * just stop the fall so the camera doesn't run off forever. */
            p->ypos = JO_MULT_BY_65536(w->height_px - 1);
            p->ysp  = 0;
        }
    }

    p->prev_jump = jump_held;
}
