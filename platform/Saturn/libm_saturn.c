// =============================================================================
// libm_saturn.c -- freestanding libm for the RSDKv5 SH-2 true-port (P2, #201).
//
// WHY THIS FILE EXISTS
// --------------------
// The jo Saturn toolchain ships NO libm.a (verified: `sh-none-elf-gcc -m2
// -print-file-name=libm.a` returns the bare string "libm.a" == NOT FOUND, while
// libc.a/libnosys.a/libgcc.a all resolve to real absolute paths). newlib's
// libc.a does NOT fold in the transcendental math either -- after a real link
// the 9 math symbols below remain undefined. They are referenced by exactly two
// core TUs:
//
//   Storage/Text.cpp:33-47  calcKs(): pow(2,32), fabs(sin(1+i)) for i=0..63 to
//                           BUILD THE MD5 K-CONSTANT TABLE AT RUNTIME. These
//                           MUST equal the canonical MD5 constants
//                           (0xd76aa478, 0xe8c7b756, ...) or every asset/object
//                           name md5() (Text.cpp:56+) diverges from the baked
//                           hashes in the data files. => FULL double precision
//                           required (sin / pow / fabs).
//
//   Core/Math.cpp:49-107    CalculateTrigAngles(): sinf/cosf/tanf/asinf/acosf
//                           and atan2 build the trig LOOKUP TABLES. These are
//                           float-truncated ((int32)(sinf(..)*1024.f) etc.) and
//                           the exact quadrant points are hard-set afterward
//                           (cos1024[0]=0x400, sin1024[0x100]=0x400, ...), so
//                           approximation error << 1 LSB of the int table is
//                           tolerable here -- the lower-precision regime.
//
// The double path therefore sets the bar: it must reproduce the canonical MD5
// table bit-exactly. The implementation is transcribed from fdlibm (the SunPro
// public-domain reference that newlib/glibc/musl all derive from) -- porting
// known-good code, not guessing.
//
// ENDIANNESS PORTABILITY
// ----------------------
// fdlibm's __HI/__LO macros index the double as two 32-bit words, which is
// word-ORDER dependent (breaks across LE host / BE SH-2). This file instead
// reinterprets the double through `union{double d; unsigned long long u;}` and
// extracts the IEEE-754 high/low words by SHIFTING the 64-bit value. Because the
// double and the uint64 share the same byte-endianness on any given target, the
// logical bit positions (sign=63, exp=62..52, ...) are identical on x86 (LE) and
// SH-2 (BE). Result: the host build (gcc -m64, SSE2 IEEE double) and the SH-2
// soft-double build produce BIT-IDENTICAL output -> the host gate
// (qa_p2_libm_verify.py) can prove the MD5 table is correct before SH-2 runs.
//
// PUBLIC SYMBOLS (exactly the 9 the core references, C linkage / unmangled):
//   double fabs(double); double sin(double); double cos(double);
//   double pow(double,double); double atan2(double,double);
//   float sinf(float); float cosf(float); float tanf(float);
//   float asinf(float); float acosf(float);
// =============================================================================

// IEEE-754 double bit access, endianness-portable (see header comment).
typedef union {
    double             d;
    unsigned long long u;
} __du;

static int __hi(double x)
{
    __du t;
    t.d = x;
    return (int)(unsigned)(t.u >> 32);
}

static unsigned __lo(double x)
{
    __du t;
    t.d = x;
    return (unsigned)(t.u & 0xffffffffu);
}

static double __sethi(double x, int hi)
{
    __du t;
    t.d = x;
    t.u = ((unsigned long long)(unsigned)hi << 32) | (t.u & 0xffffffffULL);
    return t.d;
}

static double __setlo(double x, unsigned lo)
{
    __du t;
    t.d = x;
    t.u = (t.u & 0xffffffff00000000ULL) | (unsigned long long)lo;
    return t.d;
}

static double __setboth(int hi, unsigned lo)
{
    __du t;
    t.u = ((unsigned long long)(unsigned)hi << 32) | (unsigned long long)lo;
    return t.d;
}

// -----------------------------------------------------------------------------
// fabs / copysign / scalbn -- bit primitives (fdlibm s_fabs / s_copysign /
// s_scalbn, transcribed via the union word access above).
// -----------------------------------------------------------------------------
double fabs(double x) { return __sethi(x, __hi(x) & 0x7fffffff); }

static double d_fabs(double x) { return __sethi(x, __hi(x) & 0x7fffffff); }

static double d_copysign(double x, double y)
{
    return __sethi(x, (__hi(x) & 0x7fffffff) | (__hi(y) & 0x80000000));
}

static const double
    __two54  = 1.80143985094819840000e+16,
    __twom54 = 5.55111512312578270212e-17,
    __sbhuge = 1.0e+300,
    __sbtiny = 1.0e-300;

static double d_scalbn(double x, int n)
{
    int k, hx, lx;
    hx = __hi(x);
    lx = (int)__lo(x);
    k  = (hx & 0x7ff00000) >> 20;          // extract exponent
    if (k == 0) {                          // 0 or subnormal x
        if ((lx | (hx & 0x7fffffff)) == 0) return x;  // +-0
        x  = x * __two54;
        hx = __hi(x);
        k  = ((hx & 0x7ff00000) >> 20) - 54;
        if (n < -50000) return __sbtiny * x;  // underflow
    }
    if (k == 0x7ff) return x + x;          // NaN or Inf
    k = k + n;
    if (k > 0x7fe) return __sbhuge * d_copysign(__sbhuge, x);  // overflow
    if (k > 0)                              // normal result
        return __sethi(x, (hx & 0x800fffff) | (k << 20));
    if (k <= -54) {
        if (n > 50000) return __sbhuge * d_copysign(__sbhuge, x);  // overflow
        else           return __sbtiny * d_copysign(__sbtiny, x);  // underflow
    }
    k += 54;                                // subnormal result
    x = __sethi(x, (hx & 0x800fffff) | (k << 20));
    return x * __twom54;
}

// -----------------------------------------------------------------------------
// __kernel_sin / __kernel_cos (fdlibm k_sin.c / k_cos.c) -- |x| <= pi/4.
// -----------------------------------------------------------------------------
static const double
    __half = 5.00000000000000000000e-01,
    S1 = -1.66666666666666324348e-01,
    S2 =  8.33333333332248946124e-03,
    S3 = -1.98412698298579493134e-04,
    S4 =  2.75573137070700676789e-06,
    S5 = -2.50507602534068634195e-08,
    S6 =  1.58969099521155010221e-10;

static double __kernel_sin(double x, double y, int iy)
{
    double z, r, v;
    int ix;
    ix = __hi(x) & 0x7fffffff;             // high word of x
    if (ix < 0x3e400000) {                 // |x| < 2**-27
        if ((int)x == 0) return x;         // generate inexact
    }
    z = x * x;
    v = z * x;
    r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
    if (iy == 0) return x + v * (S1 + z * r);
    else         return x - ((z * (__half * y - v * r) - y) - v * S1);
}

static const double
    __one = 1.00000000000000000000e+00,
    C1 =  4.16666666666666019037e-02,
    C2 = -1.38888888888741095749e-03,
    C3 =  2.48015872894767294178e-05,
    C4 = -2.75573143513906633035e-07,
    C5 =  2.08757232129817482790e-09,
    C6 = -1.13596475577881948265e-11;

static double __kernel_cos(double x, double y)
{
    double a, hz, z, r, qx;
    int ix;
    ix = __hi(x) & 0x7fffffff;             // |x|'s high word
    if (ix < 0x3e400000) {                 // |x| < 2**-27
        if ((int)x == 0) return __one;     // generate inexact
    }
    z = x * x;
    r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
    if (ix < 0x3FD33333)                   // |x| < 0.3
        return __one - (0.5 * z - (z * r - x * y));
    else {
        if (ix > 0x3fe90000)               // x > 0.78125
            qx = 0.28125;
        else
            qx = __setboth(ix - 0x00200000, 0);  // x/4
        hz = 0.5 * z - qx;
        a  = __one - qx;
        return a - (hz - (z * r - x * y));
    }
}

// -----------------------------------------------------------------------------
// __rem_pio2 (fdlibm e_rem_pio2.c, MEDIUM path only).
// Valid for |x| < 2^20 * (pi/2); our max |x| is sin(1+63)=sin(64) << that, so
// the big __kernel_rem_pio2 (2/pi table + 3pi/4 branch) is not needed. The
// quick n<32 cancellation skip (npio2_hw table) is dropped -- the always-on
// high-word cancellation check below is correct, just slightly slower.
// -----------------------------------------------------------------------------
static const double
    __invpio2 = 6.36619772367581382433e-01,
    __pio2_1  = 1.57079632673412561417e+00,
    __pio2_1t = 6.07710050650619224932e-11,
    __pio2_2  = 6.07710050630396597660e-11,
    __pio2_2t = 2.02226624879595063154e-21,
    __pio2_3  = 2.02226624871116645580e-21,
    __pio2_3t = 8.47842766036889956997e-32;

static int __rem_pio2(double x, double *y)
{
    double w, t, r, fn;
    int i, j, n, ix, hx;

    hx = __hi(x);
    ix = hx & 0x7fffffff;

    t  = (x < 0.0) ? -x : x;               // fabs(x)
    n  = (int)(t * __invpio2 + __half);
    fn = (double)n;
    r  = t - fn * __pio2_1;
    w  = fn * __pio2_1t;                    // 1st round, good to 85 bit
    j  = ix >> 20;
    y[0] = r - w;
    i = j - ((__hi(y[0]) >> 20) & 0x7ff);
    if (i > 16) {                          // 2nd iteration needed, good to 118
        t = r;
        w = fn * __pio2_2;
        r = t - w;
        w = fn * __pio2_2t - ((t - r) - w);
        y[0] = r - w;
        i = j - ((__hi(y[0]) >> 20) & 0x7ff);
        if (i > 49) {                      // 3rd iteration, 151 bits accuracy
            t = r;
            w = fn * __pio2_3;
            r = t - w;
            w = fn * __pio2_3t - ((t - r) - w);
            y[0] = r - w;
        }
    }
    y[1] = (r - y[0]) - w;
    if (hx < 0) { y[0] = -y[0]; y[1] = -y[1]; return -n; }
    return n;
}

// -----------------------------------------------------------------------------
// sin / cos (fdlibm s_sin.c / s_cos.c).
// -----------------------------------------------------------------------------
double sin(double x)
{
    double y[2], z = 0.0;
    int n, ix;
    ix = __hi(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) return __kernel_sin(x, z, 0);   // |x| < pi/4
    else if (ix >= 0x7ff00000) return x - x;              // Inf/NaN -> NaN
    else {
        n = __rem_pio2(x, y);
        switch (n & 3) {
            case 0:  return  __kernel_sin(y[0], y[1], 1);
            case 1:  return  __kernel_cos(y[0], y[1]);
            case 2:  return -__kernel_sin(y[0], y[1], 1);
            default: return -__kernel_cos(y[0], y[1]);
        }
    }
}

double cos(double x)
{
    double y[2], z = 0.0;
    int n, ix;
    ix = __hi(x) & 0x7fffffff;
    if (ix <= 0x3fe921fb) return __kernel_cos(x, z);      // |x| < pi/4
    else if (ix >= 0x7ff00000) return x - x;              // Inf/NaN -> NaN
    else {
        n = __rem_pio2(x, y);
        switch (n & 3) {
            case 0:  return  __kernel_cos(y[0], y[1]);
            case 1:  return -__kernel_sin(y[0], y[1], 1);
            case 2:  return -__kernel_cos(y[0], y[1]);
            default: return  __kernel_sin(y[0], y[1], 1);
        }
    }
}

// -----------------------------------------------------------------------------
// __atan (fdlibm s_atan.c).
// -----------------------------------------------------------------------------
static const double __atanhi[] = {
    4.63647609000806093515e-01,            // atan(0.5)hi
    7.85398163397448278999e-01,            // atan(1.0)hi
    9.82793723247329054082e-01,            // atan(1.5)hi
    1.57079632679489655800e+00,            // atan(inf)hi
};
static const double __atanlo[] = {
    2.26987774529616870924e-17,            // atan(0.5)lo
    3.06161699786838301793e-17,            // atan(1.0)lo
    1.39033110312309984516e-17,            // atan(1.5)lo
    6.12323399573676603587e-17,            // atan(inf)lo
};
static const double __aT[] = {
    3.33333333333329318027e-01,
   -1.99999999998764832476e-01,
    1.42857142725034663711e-01,
   -1.11111104054623557880e-01,
    9.09088713343650656196e-02,
   -7.69187620504482999495e-02,
    6.66107313738753120669e-02,
   -5.83357013379057348645e-02,
    4.97687799461593236017e-02,
   -3.65315727442169155270e-02,
    2.16941002998257985093e-02,
};
static const double __atanhuge = 1.0e300;

static double __atan(double x)
{
    double w, s1, s2, z;
    int ix, hx, id;
    hx = __hi(x);
    ix = hx & 0x7fffffff;
    if (ix >= 0x44100000) {                // |x| >= 2^66
        if (ix > 0x7ff00000 || (ix == 0x7ff00000 && (__lo(x) != 0)))
            return x + x;                  // NaN
        if (hx > 0) return  __atanhi[3] + __atanlo[3];
        else        return -__atanhi[3] - __atanlo[3];
    }
    if (ix < 0x3fdc0000) {                 // |x| < 0.4375
        if (ix < 0x3e400000) {             // |x| < 2^-27
            if (__atanhuge + x > __one) return x;  // raise inexact
        }
        id = -1;
    } else {
        x = d_fabs(x);
        if (ix < 0x3ff30000) {             // |x| < 1.1875
            if (ix < 0x3fe60000) {         // 7/16 <= |x| < 11/16
                id = 0; x = (2.0 * x - __one) / (2.0 + x);
            } else {                       // 11/16 <= |x| < 19/16
                id = 1; x = (x - __one) / (x + __one);
            }
        } else {
            if (ix < 0x40038000) {         // |x| < 2.4375
                id = 2; x = (x - 1.5) / (__one + 1.5 * x);
            } else {                       // 2.4375 <= |x| < 2^66
                id = 3; x = -1.0 / x;
            }
        }
    }
    z = x * x;
    w = z * z;
    s1 = z * (__aT[0] + w * (__aT[2] + w * (__aT[4] + w * (__aT[6] + w * (__aT[8] + w * __aT[10])))));
    s2 = w * (__aT[1] + w * (__aT[3] + w * (__aT[5] + w * (__aT[7] + w * __aT[9]))));
    if (id < 0) return x - x * (s1 + s2);
    else {
        z = __atanhi[id] - ((x * (s1 + s2) - __atanlo[id]) - x);
        return (hx < 0) ? -z : z;
    }
}

// -----------------------------------------------------------------------------
// atan2 (fdlibm e_atan2.c).
// -----------------------------------------------------------------------------
static const double
    __tiny2 = 1.0e-300,
    __pi_o_4 = 7.8539816339744827900e-01,
    __pi_o_2 = 1.5707963267948965580e+00,
    __pi     = 3.1415926535897931160e+00,
    __pi_lo  = 1.2246467991473531772e-16;

double atan2(double y, double x)
{
    double z;
    int k, m, hx, hy, ix, iy;
    unsigned lx, ly;

    hx = __hi(x); ix = hx & 0x7fffffff; lx = __lo(x);
    hy = __hi(y); iy = hy & 0x7fffffff; ly = __lo(y);
    if (((ix | ((lx | (0u - lx)) >> 31)) > 0x7ff00000) ||   // x or y is NaN
        ((iy | ((ly | (0u - ly)) >> 31)) > 0x7ff00000))
        return x + y;
    if (((hx - 0x3ff00000) | lx) == 0) return __atan(y);    // x = 1.0
    m = ((hy >> 31) & 1) | ((hx >> 30) & 2);                // 2*sign(x)+sign(y)

    if ((iy | ly) == 0) {                  // y = 0
        switch (m) {
            case 0:
            case 1:  return y;             // atan(+-0, +anything) = +-0
            case 2:  return  __pi + __tiny2;
            default: return -__pi - __tiny2;
        }
    }
    if ((ix | lx) == 0)                     // x = 0
        return (hy < 0) ? -__pi_o_2 - __tiny2 : __pi_o_2 + __tiny2;

    if (ix == 0x7ff00000) {                // x is INF
        if (iy == 0x7ff00000) {
            switch (m) {
                case 0:  return  __pi_o_4 + __tiny2;
                case 1:  return -__pi_o_4 - __tiny2;
                case 2:  return  3.0 * __pi_o_4 + __tiny2;
                default: return -3.0 * __pi_o_4 - __tiny2;
            }
        } else {
            switch (m) {
                case 0:  return  0.0;
                case 1:  return -0.0;
                case 2:  return  __pi + __tiny2;
                default: return -__pi - __tiny2;
            }
        }
    }
    if (iy == 0x7ff00000)                  // y is INF
        return (hy < 0) ? -__pi_o_2 - __tiny2 : __pi_o_2 + __tiny2;

    k = (iy - ix) >> 20;
    if (k > 60) z = __pi_o_2 + 0.5 * __pi_lo;          // |y/x| > 2^60
    else if (hx < 0 && k < -60) z = 0.0;               // |y|/x < -2^60
    else z = __atan(d_fabs(y / x));
    switch (m) {
        case 0:  return  z;                            // atan(+,+)
        case 1:  return  __sethi(z, __hi(z) ^ 0x80000000);  // atan(-,+)
        case 2:  return  __pi - (z - __pi_lo);         // atan(+,-)
        default: return  (z - __pi_lo) - __pi;         // atan(-,-)
    }
}

// -----------------------------------------------------------------------------
// d_sqrt -- Newton-Raphson from a bit-hack seed. Used ONLY by asinf/acosf
// (float-table precision). NOT on the MD5 path. 6 iterations from the seed
// converge to full double precision over the [0,1] domain asin/acos feed it.
// -----------------------------------------------------------------------------
static double d_sqrt(double x)
{
    __du t;
    double y;
    int i;
    if (x <= 0.0) return 0.0;
    t.d = x;
    t.u = 0x1ff7a3bea91d9b1bULL + (t.u >> 1);
    y = t.d;
    for (i = 0; i < 6; i++) y = 0.5 * (y + x / y);
    return y;
}

// -----------------------------------------------------------------------------
// pow (fdlibm e_pow.c). Only pow(2,32) is called (Text.cpp:38) and it must
// return EXACTLY 4294967296.0 -- verified by the host gate. Full general
// implementation transcribed (no special-casing).
// -----------------------------------------------------------------------------
static const double
    __bp[]   = { 1.0, 1.5 },
    __dp_h[] = { 0.0, 5.84962487220764160156e-01 },
    __dp_l[] = { 0.0, 1.35003920212974897128e-08 },
    __pzero = 0.0,
    __ptwo  = 2.0,
    __two53 = 9007199254740992.0,
    __phuge = 1.0e300,
    __ptiny = 1.0e-300,
    __L1 =  5.99999999999994648725e-01,
    __L2 =  4.28571428578550184252e-01,
    __L3 =  3.33333329818377432918e-01,
    __L4 =  2.72728123808534006489e-01,
    __L5 =  2.30660745775561366331e-01,
    __L6 =  2.06975017800338417784e-01,
    __P1 =  1.66666666666666019037e-01,
    __P2 = -2.77777777770155933842e-03,
    __P3 =  6.61375632143793436117e-05,
    __P4 = -1.65339022054652515390e-06,
    __P5 =  4.13813679705723846039e-08,
    __lg2   =  6.93147180559945286227e-01,
    __lg2_h =  6.93147182464599609375e-01,
    __lg2_l = -1.90465429995776804525e-09,
    __ovt =  8.0085662595372944372e-017,
    __cp   =  9.61796693925975554329e-01,
    __cp_h =  9.61796700954437255859e-01,
    __cp_l = -7.02846165095275826516e-09,
    __ivln2   =  1.44269504088896338700e+00,
    __ivln2_h =  1.44269502162933349609e+00,
    __ivln2_l =  1.92596299112661746887e-08;

double pow(double x, double y)
{
    double z, ax, z_h, z_l, p_h, p_l;
    double y1, t1, t2, r, s, t, u, v, w;
    int i, j, k, yisint, n;
    int hx, hy, ix, iy;
    unsigned lx, ly;

    hx = __hi(x); lx = __lo(x);
    hy = __hi(y); ly = __lo(y);
    ix = hx & 0x7fffffff;
    iy = hy & 0x7fffffff;

    if ((iy | ly) == 0) return __one;      // x**0 = 1

    if (ix > 0x7ff00000 || ((ix == 0x7ff00000) && (lx != 0)) ||
        iy > 0x7ff00000 || ((iy == 0x7ff00000) && (ly != 0)))
        return x + y;                      // +-NaN

    // determine if y is an odd int when x < 0
    yisint = 0;
    if (hx < 0) {
        if (iy >= 0x43400000) yisint = 2;          // even integer y
        else if (iy >= 0x3ff00000) {
            k = (iy >> 20) - 0x3ff;                 // exponent
            if (k > 20) {
                j = (int)(ly >> (52 - k));
                if ((unsigned)(j << (52 - k)) == ly) yisint = 2 - (j & 1);
            } else if (ly == 0) {
                j = iy >> (20 - k);
                if ((j << (20 - k)) == iy) yisint = 2 - (j & 1);
            }
        }
    }

    if (ly == 0) {                         // special value of y
        if (iy == 0x7ff00000) {            // y is +-inf
            if (((ix - 0x3ff00000) | lx) == 0) return y - y;   // (+-1)**+-inf NaN
            else if (ix >= 0x3ff00000)     return (hy >= 0) ? y : __pzero;
            else                           return (hy < 0) ? -y : __pzero;
        }
        if (iy == 0x3ff00000) {            // y is +-1
            if (hy < 0) return __one / x; else return x;
        }
        if (hy == 0x40000000) return x * x;        // y is 2
        if (hy == 0x3fe00000) {                    // y is 0.5
            if (hx >= 0) return d_sqrt(x);
        }
    }

    ax = d_fabs(x);
    if (lx == 0) {                         // special value of x
        if (ix == 0x7ff00000 || ix == 0 || ix == 0x3ff00000) {
            z = ax;                        // x is +-0,+-inf,+-1
            if (hy < 0) z = __one / z;
            if (hx < 0) {
                if (((ix - 0x3ff00000) | yisint) == 0) {
                    z = (z - z) / (z - z); // (-1)**non-int is NaN
                } else if (yisint == 1)
                    z = -z;
            }
            return z;
        }
    }

    if (((((unsigned)hx >> 31) - 1) | yisint) == 0) return (x - x) / (x - x);  // (x<0)**(non-int)

    s = __one;
    if (((((unsigned)hx >> 31) - 1) | ((unsigned)yisint - 1)) == 0)
        s = -__one;                        // (-ve)**(odd int)

    if (iy > 0x41e00000) {                 // |y| > 2^31
        if (iy > 0x43f00000) {             // |y| > 2^64, must o/uflow
            if (ix <= 0x3fefffff) return (hy < 0) ? __phuge * __phuge : __ptiny * __ptiny;
            if (ix >= 0x3ff00000) return (hy > 0) ? __phuge * __phuge : __ptiny * __ptiny;
        }
        if (ix < 0x3fefffff) return (hy < 0) ? s * __phuge * __phuge : s * __ptiny * __ptiny;
        if (ix > 0x3ff00000) return (hy > 0) ? s * __phuge * __phuge : s * __ptiny * __ptiny;
        t = ax - __one;
        w = (t * t) * (0.5 - t * (0.3333333333333333333333 - t * 0.25));
        u = __ivln2_h * t;
        v = t * __ivln2_l - w * __ivln2;
        t1 = u + v;
        t1 = __setlo(t1, 0);
        t2 = v - (t1 - u);
    } else {
        double ss, s2, s_h, s_l, t_h, t_l;
        n = 0;
        if (ix < 0x00100000) { ax = ax * __two53; n -= 53; ix = __hi(ax); }  // subnormal
        n += ((ix) >> 20) - 0x3ff;
        j = ix & 0x000fffff;
        ix = j | 0x3ff00000;               // normalize ix
        if (j <= 0x3988E) k = 0;           // |x| < sqrt(3/2)
        else if (j < 0xBB67A) k = 1;       // |x| < sqrt(3)
        else { k = 0; n += 1; ix -= 0x00100000; }
        ax = __sethi(ax, ix);

        u = ax - __bp[k];                  // bp[0]=1.0, bp[1]=1.5
        v = __one / (ax + __bp[k]);
        ss = u * v;
        s_h = ss;
        s_h = __setlo(s_h, 0);
        t_h = __setboth(((ix >> 1) | 0x20000000) + 0x00080000 + (k << 18), 0);
        t_l = ax - (t_h - __bp[k]);
        s_l = v * ((u - s_h * t_h) - s_h * t_l);
        s2 = ss * ss;
        r = s2 * s2 * (__L1 + s2 * (__L2 + s2 * (__L3 + s2 * (__L4 + s2 * (__L5 + s2 * __L6)))));
        r += s_l * (s_h + ss);
        s2 = s_h * s_h;
        t_h = 3.0 + s2 + r;
        t_h = __setlo(t_h, 0);
        t_l = r - ((t_h - 3.0) - s2);
        u = s_h * t_h;
        v = s_l * t_h + t_l * ss;
        p_h = u + v;
        p_h = __setlo(p_h, 0);
        p_l = v - (p_h - u);
        z_h = __cp_h * p_h;
        z_l = __cp_l * p_h + p_l * __cp + __dp_l[k];
        t = (double)n;
        t1 = (((z_h + z_l) + __dp_h[k]) + t);
        t1 = __setlo(t1, 0);
        t2 = z_l - (((t1 - t) - __dp_h[k]) - z_h);
    }

    // split up y into y1+y2 and compute (y1+y2)*(t1+t2)
    y1 = y;
    y1 = __setlo(y1, 0);
    p_l = (y - y1) * t1 + y * t2;
    p_h = y1 * t1;
    z = p_l + p_h;
    j = __hi(z);
    i = (int)__lo(z);
    if (j >= 0x40900000) {                 // z >= 1024
        if (((j - 0x40900000) | i) != 0) return s * __phuge * __phuge;  // overflow
        else { if (p_l + __ovt > z - p_h) return s * __phuge * __phuge; }
    } else if ((j & 0x7fffffff) >= 0x4090cc00) {  // z <= -1075
        if (((j - (int)0xc090cc00) | i) != 0) return s * __ptiny * __ptiny;  // underflow
        else { if (p_l <= z - p_h) return s * __ptiny * __ptiny; }
    }

    // compute 2**(p_h+p_l)
    i = j & 0x7fffffff;
    k = (i >> 20) - 0x3ff;
    n = 0;
    if (i > 0x3fe00000) {                  // |z| > 0.5, set n = [z+0.5]
        n = j + (0x00100000 >> (k + 1));
        k = ((n & 0x7fffffff) >> 20) - 0x3ff;       // new k for n
        t = __setboth(n & ~(0x000fffff >> k), 0);
        n = ((n & 0x000fffff) | 0x00100000) >> (20 - k);
        if (j < 0) n = -n;
        p_h -= t;
    }
    t = p_l + p_h;
    t = __setlo(t, 0);
    u = t * __lg2_h;
    v = (p_l - (t - p_h)) * __lg2 + t * __lg2_l;
    z = u + v;
    w = v - (z - u);
    t = z * z;
    t1 = z - t * (__P1 + t * (__P2 + t * (__P3 + t * (__P4 + t * __P5))));
    r = (z * t1) / (t1 - __ptwo) - (w + z * w);
    z = __one - (r - z);
    j = __hi(z);
    j += (n << 20);
    if ((j >> 20) <= 0) z = d_scalbn(z, n);         // subnormal output
    else z = __sethi(z, __hi(z) + (n << 20));
    return s * z;
}

// -----------------------------------------------------------------------------
// float wrappers (Math.cpp trig-table builders). Lower-precision regime: the
// results are immediately (int32)(.. * 1024.f)-truncated and the exact quadrant
// points are hard-set afterward, so computing in double then narrowing is well
// within tolerance. asin/acos via the identities
//   asin(t) = atan2(t, sqrt(1-t*t)),  acos(t) = atan2(sqrt(1-t*t), t).
// -----------------------------------------------------------------------------
float sinf(float x) { return (float)sin((double)x); }
float cosf(float x) { return (float)cos((double)x); }
float tanf(float x) { double d = (double)x; return (float)(sin(d) / cos(d)); }

float asinf(float x)
{
    double d = (double)x;
    return (float)atan2(d, d_sqrt(__one - d * d));
}

float acosf(float x)
{
    double d = (double)x;
    return (float)atan2(d_sqrt(__one - d * d), d);
}
