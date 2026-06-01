#ifndef RSDK_MATH_H
#define RSDK_MATH_H

/* Phase 1.1 — Math module, Saturn port of RSDKv5/RSDK/Core/Math.
 *
 * Source contracts (read but not reproduced):
 *   tools/_decomp_raw/_RSDKv5_Core_Math.hpp:52-114   Sin/Cos/Tan/ASin/ACos
 *                                                    1024/512/256 accessors
 *   tools/_decomp_raw/_RSDKv5_Core_Math.hpp:118-142  Rand / RandSeeded LCG
 *   tools/_decomp_raw/_RSDKv5_Core_Math.cpp:49-107   CalculateTrigAngles —
 *                                                    LUT build at boot
 *   tools/_decomp_raw/_RSDKv5_Core_Math.cpp:109-136  ArcTanLookup
 *
 * The Saturn port:
 *   * Builds the LUTs at boot via rsdk_math_init() using libc sinf/cosf/
 *     tanf/asinf/acosf/atan2 from the SH-2 newlib that ships with the
 *     SaturnSDK toolchain.
 *   * RSDK angle units: 0x400 (1024) = 360 degrees for the 1024-LUT;
 *                       0x200  (512) = 360 degrees for the 512-LUT;
 *                       0x100  (256) = 360 degrees for the 256-LUT.
 *   * 1024-LUT outputs are 16.10 signed fixed (range [-0x400,+0x400]).
 *      512-LUT outputs are 16.9  signed fixed (range [-0x200,+0x200]).
 *      256-LUT outputs are 16.8  signed fixed (range [-0x80, +0x80 ]).
 *   * Decomp callers we care about for Phase 1.1:
 *       TitleBG_Scanline_Clouds       uses Sin1024
 *       TitleSetup_Draw_DrawRing      uses Sin512
 *
 * Saturn deviations: none. LUT shapes + range match upstream exactly so
 * decomp call-sites transfer 1:1. */

#include <stdint.h>

/* --- Angle-unit constants (mirror _RSDKv5_Core_Math.hpp:14-17) ------- */
#define RSDK_TO_FIXED(x)   ((int32_t)(x) << 16)
#define RSDK_FROM_FIXED(x) ((int32_t)(x) >> 16)

/* SGL's SL_DEF.H also defines MAX/MIN. We use rsdk_MAX/rsdk_MIN names to
 * avoid the redefine warning entirely. The legacy MIN/MAX aliases only
 * expand when nobody has defined them yet. */
#ifndef MIN
#define MIN(a, b)  ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b)  ((a) > (b) ? (a) : (b))
#endif
#ifndef CLAMP
#define CLAMP(v, lo, hi)  ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#endif
/* Saturn-safe aliases (no conflict with SGL). */
#define RSDK_MIN(a, b) ((a) < (b) ? (a) : (b))
#define RSDK_MAX(a, b) ((a) > (b) ? (a) : (b))

/* --- Public LUT pointers (filled by rsdk_math_init) ----------------- */
extern int32_t g_rsdk_sin1024[0x400];
extern int32_t g_rsdk_cos1024[0x400];
extern int32_t g_rsdk_tan1024[0x400];

extern int32_t g_rsdk_sin512[0x200];
extern int32_t g_rsdk_cos512[0x200];
extern int32_t g_rsdk_tan512[0x200];

extern int32_t g_rsdk_sin256[0x100];
extern int32_t g_rsdk_cos256[0x100];
extern int32_t g_rsdk_tan256[0x100];

extern uint8_t g_rsdk_arctan256[0x100 * 0x100];

extern uint32_t g_rsdk_rand_seed;

/* === Public API ===================================================== */

/* One-shot LUT builder. Call at boot. Idempotent — guarded by a static
 * flag so duplicate calls are safe. */
void rsdk_math_init(void);

/* Accessors (mirror _RSDKv5_Core_Math.hpp:52-110). */
static inline int32_t rsdk_sin1024(int32_t a) { return g_rsdk_sin1024[a & 0x3FF]; }
static inline int32_t rsdk_cos1024(int32_t a) { return g_rsdk_cos1024[a & 0x3FF]; }
static inline int32_t rsdk_tan1024(int32_t a) { return g_rsdk_tan1024[a & 0x3FF]; }

static inline int32_t rsdk_sin512(int32_t a) { return g_rsdk_sin512[a & 0x1FF]; }
static inline int32_t rsdk_cos512(int32_t a) { return g_rsdk_cos512[a & 0x1FF]; }
static inline int32_t rsdk_tan512(int32_t a) { return g_rsdk_tan512[a & 0x1FF]; }

static inline int32_t rsdk_sin256(int32_t a) { return g_rsdk_sin256[a & 0xFF]; }
static inline int32_t rsdk_cos256(int32_t a) { return g_rsdk_cos256[a & 0xFF]; }
static inline int32_t rsdk_tan256(int32_t a) { return g_rsdk_tan256[a & 0xFF]; }

/* ArcTan in the 0..255 angle space (decomp Math.cpp:109-136). */
uint8_t rsdk_arctan(int32_t x, int32_t y);

/* LCG random (decomp Math.hpp:118-128). */
int32_t rsdk_rand(int32_t mn, int32_t mx);
int32_t rsdk_rand_seeded(int32_t mn, int32_t mx, int32_t *seed);
static inline void rsdk_set_rand_seed(int32_t key) { g_rsdk_rand_seed = (uint32_t)key; }

#endif /* RSDK_MATH_H */
