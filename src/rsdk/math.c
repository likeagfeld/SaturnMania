/* Phase 1.1 — Math module (Saturn implementation).
 *
 * Port reference (read but not reproduced — API contract per the header):
 *   tools/_decomp_raw/_RSDKv5_Core_Math.cpp:49-107  CalculateTrigAngles
 *   tools/_decomp_raw/_RSDKv5_Core_Math.cpp:109-136 ArcTanLookup
 *   tools/_decomp_raw/_RSDKv5_Core_Math.hpp:118-128 LCG Rand
 *
 * Saturn deviations from upstream:
 *   * Upstream uses time(NULL) to seed; Saturn has no wall clock so the
 *     boot seed defaults to 0. Callers explicitly seed via
 *     rsdk_set_rand_seed when they need it.
 *   * SH-2 has no libm with the joengine toolchain config (-nostdlib).
 *     The boot-time LUT build needs sin/cos/tan/atan2 ONLY for filling
 *     the integer LUTs — once filled, the runtime engine uses the LUTs
 *     directly via integer indexing. We provide local polynomial
 *     approximations below; the LUT entries are then quantized to int32
 *     so the accuracy floor is the LUT scale (1/1024).
 *
 * Memory budget:
 *   sin/cos/tan 1024 = 3 * 0x400 * 4 = 12 KB
 *   sin/cos/tan  512 = 3 * 0x200 * 4 =  6 KB
 *   sin/cos/tan  256 = 3 * 0x100 * 4 =  3 KB
 *   arctan       256x256 = 64 KB
 *   ~85 KB total in .bss. */

#include "math.h"
#include <string.h>

#ifndef RSDK_PI
#define RSDK_PI       3.1415927f
#endif
#define RSDK_TWO_PI   (RSDK_PI * 2.0f)
#define RSDK_HALF_PI  (RSDK_PI * 0.5f)

/* === Local libm-free trig (boot-time use only) ====================== */

static float lm_fmodf(float a, float b)
{
    if (b == 0.0f) return 0.0f;
    float q = a / b;
    int32_t qi = (int32_t)q;
    return a - (float)qi * b;
}

static float lm_sinf(float x)
{
    /* Reduce x into [-pi, pi], then symmetry-fold to [-pi/2, pi/2]. */
    x = lm_fmodf(x, RSDK_TWO_PI);
    if (x >  RSDK_PI) x -= RSDK_TWO_PI;
    if (x < -RSDK_PI) x += RSDK_TWO_PI;
    if (x >  RSDK_HALF_PI) x =  RSDK_PI - x;
    if (x < -RSDK_HALF_PI) x = -RSDK_PI - x;
    /* Maclaurin polynomial; converges well on [-pi/2, pi/2]. */
    float x2 = x * x;
    float result = x;
    float term   = x;
    term = -term * x2 / (2.0f * 3.0f);   result += term;
    term = -term * x2 / (4.0f * 5.0f);   result += term;
    term = -term * x2 / (6.0f * 7.0f);   result += term;
    term = -term * x2 / (8.0f * 9.0f);   result += term;
    term = -term * x2 / (10.0f * 11.0f); result += term;
    return result;
}

static float lm_cosf(float x) { return lm_sinf(x + RSDK_HALF_PI); }

static float lm_tanf(float x)
{
    float c = lm_cosf(x);
    if (c == 0.0f) return 1.0e30f;
    return lm_sinf(x) / c;
}

static float lm_atanf(float x)
{
    int neg = 0;
    if (x < 0.0f) { neg = 1; x = -x; }
    int reciprocal = 0;
    if (x > 1.0f) { x = 1.0f / x; reciprocal = 1; }
    /* Hastings-style polynomial on [0,1]; error ~1e-4 — fine for LUT. */
    float x2 = x * x;
    float r = ((( 0.0805374449538f * x2
                - 0.138776856032f) * x2
                + 0.199777106478f) * x2
                - 0.333329491539f) * x2 * x + x;
    if (reciprocal) r = RSDK_HALF_PI - r;
    if (neg) r = -r;
    return r;
}

static float lm_atan2f(float y, float x)
{
    if (x == 0.0f) {
        if (y > 0.0f) return  RSDK_HALF_PI;
        if (y < 0.0f) return -RSDK_HALF_PI;
        return 0.0f;
    }
    float a = lm_atanf(y / x);
    if (x < 0.0f) {
        if (y >= 0.0f) return a + RSDK_PI;
        return a - RSDK_PI;
    }
    return a;
}

/* === Global LUTs ==================================================== */

int32_t g_rsdk_sin1024[0x400];
int32_t g_rsdk_cos1024[0x400];
int32_t g_rsdk_tan1024[0x400];

int32_t g_rsdk_sin512[0x200];
int32_t g_rsdk_cos512[0x200];
int32_t g_rsdk_tan512[0x200];

int32_t g_rsdk_sin256[0x100];
int32_t g_rsdk_cos256[0x100];
int32_t g_rsdk_tan256[0x100];

uint8_t g_rsdk_arctan256[0x100 * 0x100];

uint32_t g_rsdk_rand_seed = 0;

static int s_math_inited = 0;

void rsdk_math_init(void)
{
    if (s_math_inited) return;
    s_math_inited = 1;

    int i;
    for (i = 0; i < 0x400; ++i) {
        float a = ((float)i / 512.0f) * RSDK_PI;
        g_rsdk_sin1024[i] = (int32_t)(lm_sinf(a) * 1024.0f);
        g_rsdk_cos1024[i] = (int32_t)(lm_cosf(a) * 1024.0f);
        g_rsdk_tan1024[i] = (int32_t)(lm_tanf(a) * 1024.0f);
    }
    /* Exact-value fixup at the four cardinal angles. */
    g_rsdk_cos1024[0x000] =  0x400;
    g_rsdk_cos1024[0x100] =  0;
    g_rsdk_cos1024[0x200] = -0x400;
    g_rsdk_cos1024[0x300] =  0;
    g_rsdk_sin1024[0x000] =  0;
    g_rsdk_sin1024[0x100] =  0x400;
    g_rsdk_sin1024[0x200] =  0;
    g_rsdk_sin1024[0x300] = -0x400;

    for (i = 0; i < 0x200; ++i) {
        float a = ((float)i / 256.0f) * RSDK_PI;
        g_rsdk_sin512[i] = (int32_t)(lm_sinf(a) * 512.0f);
        g_rsdk_cos512[i] = (int32_t)(lm_cosf(a) * 512.0f);
        g_rsdk_tan512[i] = (int32_t)(lm_tanf(a) * 512.0f);
    }
    g_rsdk_cos512[0x00]  =  0x200;
    g_rsdk_cos512[0x80]  =  0;
    g_rsdk_cos512[0x100] = -0x200;
    g_rsdk_cos512[0x180] =  0;
    g_rsdk_sin512[0x00]  =  0;
    g_rsdk_sin512[0x80]  =  0x200;
    g_rsdk_sin512[0x100] =  0;
    g_rsdk_sin512[0x180] = -0x200;

    for (i = 0; i < 0x100; ++i) {
        g_rsdk_sin256[i] = g_rsdk_sin512[i * 2] >> 1;
        g_rsdk_cos256[i] = g_rsdk_cos512[i * 2] >> 1;
        g_rsdk_tan256[i] = g_rsdk_tan512[i * 2] >> 1;
    }

    for (int y = 0; y < 0x100; ++y) {
        for (int x = 0; x < 0x100; ++x) {
            /* 40.743664 = 256 / (2*pi) — converts radians to the 0..255
             * angle space used by the upstream ArcTanLookup contract. */
            float a = lm_atan2f((float)y, (float)x) * 40.743664f;
            g_rsdk_arctan256[(y << 8) | x] = (uint8_t)(int32_t)a;
        }
    }
}

/* Port of _RSDKv5_Core_Math.cpp:109-136. Quadrant fold-back + LUT index. */
uint8_t rsdk_arctan(int32_t X, int32_t Y)
{
    int32_t x = X < 0 ? -X : X;
    int32_t y = Y < 0 ? -Y : Y;
    if (x <= y) {
        while (y > 0xFF) { x >>= 4; y >>= 4; }
    } else {
        while (x > 0xFF) { x >>= 4; y >>= 4; }
    }
    uint8_t base = g_rsdk_arctan256[(x << 8) | y];
    if (X <= 0) {
        if (Y <= 0) return (uint8_t)(base + 0x80);
        return (uint8_t)(0x80 - base);
    }
    if (Y <= 0) return (uint8_t)(-(int8_t)base);
    return base;
}

/* Port of _RSDKv5_Core_Math.hpp:118-128. LCG with three-step mix —
 * preserve constants for replay determinism. */
int32_t rsdk_rand(int32_t mn, int32_t mx)
{
    uint32_t s1 = g_rsdk_rand_seed * 0x41C64E6Du + 0x3039u;
    uint32_t s2 = s1                * 0x41C64E6Du + 0x3039u;
    g_rsdk_rand_seed = s2           * 0x41C64E6Du + 0x3039u;
    int32_t res = (int32_t)((((s1 >> 16) & 0x7FF) << 10 ^ ((s2 >> 16) & 0x7FF)) << 10
                            ^ ((g_rsdk_rand_seed >> 16) & 0x7FF));
    if (mn < mx) return mn + res % (mx - mn);
    if (mn == mx) return mn;
    return mx + res % (mn - mx);
}

int32_t rsdk_rand_seeded(int32_t mn, int32_t mx, int32_t *seed)
{
    if (!seed) return 0;
    uint32_t s  = (uint32_t)*seed;
    uint32_t s1 = s  * 0x41C64E6Du + 0x3039u;
    uint32_t s2 = s1 * 0x41C64E6Du + 0x3039u;
    s           = s2 * 0x41C64E6Du + 0x3039u;
    *seed = (int32_t)s;
    int32_t res = (int32_t)((((s1 >> 16) & 0x7FF) << 10 ^ ((s2 >> 16) & 0x7FF)) << 10
                            ^ ((s >> 16) & 0x7FF));
    if (mn < mx) return mn + res % (mx - mn);
    if (mn == mx) return mn;
    return mx + res % (mn - mx);
}
