/*
 * player.h - Player entity: state, physics, per-character tuning.
 *
 * Positions and velocities are Jo Engine Q16.16 fixed point, in world pixels.
 * Ground speed (gsp) drives movement while grounded; xsp/ysp drive it in air.
 * angle is the ground angle in Jo's 0-359 degree space (0 = flat). Until Phase 3
 * collision supplies real tile angles, angle stays 0 (flat bootstrap world).
 */
#ifndef SMS_PLAYER_H
#define SMS_PLAYER_H

#include <jo/jo.h>

typedef enum {
    CHAR_SONIC = 0,
    CHAR_TAILS,
    CHAR_KNUCKLES,
    CHAR_MIGHTY,
    CHAR_RAY
} character_id;

typedef struct {
    /* kinematics (Q16.16 world pixels) */
    jo_fixed xpos, ypos;     /* position                                   */
    jo_fixed xsp, ysp;       /* air velocity components                    */
    jo_fixed gsp;            /* ground speed (along the ground angle)       */
    int      angle;          /* ground angle, degrees (0 = flat)           */

    /* state flags */
    bool     grounded;       /* touching the floor this frame              */
    bool     rolling;        /* in a roll/jump ball                        */
    bool     jumping;        /* airborne due to a jump (enables jump-cut)   */
    bool     facing_left;    /* sprite/orientation direction               */

    /* per-character tuning */
    character_id character;
    jo_fixed jump_force;     /* selected from PHYS_JUMP_*                   */

    /* input edge tracking */
    bool     prev_jump;      /* jump button held last frame                */
} player_t;

/*
 * sms_world_t - lookup of the per-world-column collision surface table
 * (cd/GHZSURF.BIN; see tools/build_collision.py). One 4-byte entry per pixel
 * column: big-endian u16 surface world-Y + u8 angle + u8 flag.
 *   width_px  = level width in pixels (16384 for GHZ Act 1)
 *   height_px = level height in pixels (used to clamp falls)
 */
typedef struct {
    const unsigned char *raw;
    int                  width_px;
    int                  height_px;
} sms_world_t;

#define SMS_NO_FLOOR 0xFFFF

/* Initialise a player at a world position with a character's tuning. */
void player_init(player_t *p, character_id who, jo_fixed x, jo_fixed y);

/* Read the topmost solid surface Y at world column x_px (clamped to level).
 * Returns SMS_NO_FLOOR for pit columns. */
int  player_surface_y(const sms_world_t *w, int x_px);

/*
 * Advance one 60 Hz step. Reads controller 1 directly via Jo Engine.
 *   w        - world collision table (per-column surface Y from TileConfig.bin)
 *   autorun  - forces rightward input (demo); real controller input overrides
 */
void player_update(player_t *p, const sms_world_t *w, bool autorun);

#endif /* SMS_PLAYER_H */
