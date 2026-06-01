/*
 * physics.h - Classic Sonic physics constants (Genesis / Mania-faithful).
 *
 * Values are the canonical Sonic Physics Guide constants, expressed here in
 * pixels-per-frame and converted to Jo Engine Q16.16 fixed point via JO_FIXED.
 * Mania reproduces these exact Genesis values, so they are the correct target.
 *
 * All of these fractions are exactly representable in binary, so JO_FIXED()
 * incurs zero rounding error:
 *   0.046875 = 3/64,  0.09375 = 6/64,  0.21875 = 14/64,  6.5 = 13/2.
 */
#ifndef SMS_PHYSICS_H
#define SMS_PHYSICS_H

#include <jo/jo.h>

/*
 * Compile-time pixels -> Jo Q16.16 fixed point. This Jo Engine build has no
 * JO_FIXED() macro (it provides JO_INT_TO_FIXED for ints and the runtime inline
 * jo_float2fixed); SMS_FIXED folds a fractional literal to an exact integer
 * constant at compile time. All constants below are binary-exact, so no rounding.
 */
#define SMS_FIXED(x)    ((jo_fixed)((double)(x) * 65536.0))

/* --- Ground movement --------------------------------------------------- */
#define PHYS_ACC        SMS_FIXED(0.046875)  /* ground acceleration / frame  */
#define PHYS_DEC        SMS_FIXED(0.5)       /* skid deceleration            */
#define PHYS_FRC        SMS_FIXED(0.046875)  /* ground friction (= ACC)      */
#define PHYS_TOP        SMS_FIXED(6.0)       /* max ground walk/run speed    */

/* --- Air movement ------------------------------------------------------ */
#define PHYS_AIR_ACC    SMS_FIXED(0.09375)   /* air acceleration (= 2*ACC)   */
#define PHYS_GRAVITY    SMS_FIXED(0.21875)   /* downward accel in air        */
#define PHYS_TOP_Y      SMS_FIXED(16.0)      /* terminal fall speed (cap)    */

/* --- Jump -------------------------------------------------------------- */
#define PHYS_JUMP_SONIC SMS_FIXED(6.5)       /* initial jump impulse (Sonic) */
#define PHYS_JUMP_KNUX  SMS_FIXED(6.0)       /* Knuckles' shorter jump       */
#define PHYS_JUMP_CUT   SMS_FIXED(4.0)       /* variable-height release cap  */

/* --- Rolling ----------------------------------------------------------- */
#define PHYS_ROLL_FRC   SMS_FIXED(0.0234375) /* rolling friction (= ACC/2)   */
#define PHYS_ROLL_DEC   SMS_FIXED(0.125)     /* rolling skid decel           */
#define PHYS_ROLL_TOP   SMS_FIXED(16.0)      /* max rolling speed            */
#define PHYS_ROLL_MIN   SMS_FIXED(1.03125)   /* min gsp to start a roll      */
#define PHYS_UNROLL_MIN SMS_FIXED(0.5)       /* gsp below which roll ends    */

#endif /* SMS_PHYSICS_H */
