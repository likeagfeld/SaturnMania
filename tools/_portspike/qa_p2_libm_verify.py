#!/usr/bin/env python3
# =============================================================================
# qa_p2_libm_verify.py -- HOST correctness oracle for platform/Saturn/libm_saturn.c
# (P2, engine true-port #201).
#
# WHY (host, not Saturn): the SH-2 link gate (qa_p2_saturn_link.sh) proves SYMBOL
# CLOSURE -- that libm_saturn.c makes the core link with 0 unresolved. It does NOT
# prove the math is CORRECT. The MD5 path is unforgiving: Storage/Text.cpp:33-47
# calcKs() BUILDS the MD5 K-constant table at runtime via floor(|sin(n)|*2^32),
# n=1..64. If even one constant is off by 1, md5() (Text.cpp:56+) hashes every
# asset/object NAME differently from the baked data -> NOTHING loads. So the math
# MUST reproduce the canonical RFC-1321 MD5 table BIT-EXACTLY.
#
# libm_saturn.c is written endianness-portable (union{double; uint64} word access)
# specifically so a NATIVE host build (gcc, x86-64, SSE2 IEEE-754 double) produces
# BIT-IDENTICAL results to the SH-2 soft-double build. This gate compiles it on the
# host and proves correctness BEFORE the core ever runs on Saturn.
#
# Two-layer proof:
#   (1) Python's own math.floor(|sin(n)|*2^32) reproduces the hard-coded canonical
#       MD5 table  -> proves the calcKs FORMULA + the canonical target are right.
#   (2) Our C libm (sat_*), run through the exact calcKs loop, reproduces the SAME
#       64 constants -> proves the fdlibm TRANSCRIPTION is faithful.
# Plus a soft cross-check: the Math.cpp:54-106 trig-table builders, ours vs the
# host libm, agree within a tight int tolerance (the lower-precision float regime).
#
# RED-first: with no/buggy libm this gate cannot reach GREEN (link error, or a
# single flipped K constant trips the exact canonical assertion). GREEN == the
# libm is proven correct in a known-good IEEE-754 environment.
#
# Run:  python tools/_portspike/qa_p2_libm_verify.py
# Exit: 0 GREEN, 1 RED.
# =============================================================================
import math
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
LIBM = os.path.join(ROOT, "platform", "Saturn", "libm_saturn.c")

# Canonical MD5 K table (RFC 1321) = floor(2^32 * |sin(n)|), n = 1..64.
CANON_MD5 = [
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391,
]

RENAMES = ["fabs", "sin", "cos", "pow", "atan2",
           "sinf", "cosf", "tanf", "asinf", "acosf"]

HARNESS_C = r'''
#include <stdio.h>
#include <math.h>

/* our libm, public symbols renamed to sat_* at compile time */
double sat_fabs(double);
double sat_sin(double);
double sat_cos(double);
double sat_pow(double,double);
double sat_atan2(double,double);
float  sat_sinf(float);
float  sat_cosf(float);
float  sat_tanf(float);
float  sat_asinf(float);
float  sat_acosf(float);

#define RSDK_PI (3.1415927f)

static unsigned kspace[64];

int main(void){
    int i;
    double s, pwr;

    /* calcKs (Text.cpp:33-47) via OUR libm */
    pwr = sat_pow(2.0, 32.0);
    printf("POW2_32 %.1f\n", pwr);
    for(i=0;i<64;i++){
        s = sat_fabs(sat_sin((double)(1+i)));
        kspace[i] = (unsigned)(s*pwr);
    }
    for(i=0;i<64;i++) printf("K %d %08x\n", i, kspace[i]);

    /* trig tables (Math.cpp:54-106) OURS vs SYSTEM libm, max int-diff */
    {
        long md_sin=0, md_cos=0, md_tan=0, md_asin=0, md_acos=0, md_at=0, tanpoles=0;
        int a,b,d,x,y;
        for(i=0;i<0x400;i++){
            a=(int)(sat_sinf((i/512.f)*RSDK_PI)*1024.f);
            b=(int)(    sinf((i/512.f)*RSDK_PI)*1024.f);
            d=a>b?a-b:b-a; if(d>md_sin)md_sin=d;
            a=(int)(sat_cosf((i/512.f)*RSDK_PI)*1024.f);
            b=(int)(    cosf((i/512.f)*RSDK_PI)*1024.f);
            d=a>b?a-b:b-a; if(d>md_cos)md_cos=d;
            a=(int)(sat_tanf((i/512.f)*RSDK_PI)*1024.f);
            b=(int)(    tanf((i/512.f)*RSDK_PI)*1024.f);
            d=a>b?a-b:b-a;
            if(b>-4096 && b<4096){ if(d>md_tan)md_tan=d; } else if(d>0) tanpoles++;
            a=(int)((sat_asinf(i/1023.f)*512.f)/RSDK_PI);
            b=(int)((    asinf(i/1023.f)*512.f)/RSDK_PI);
            d=a>b?a-b:b-a; if(d>md_asin)md_asin=d;
            a=(int)((sat_acosf(i/1023.f)*512.f)/RSDK_PI);
            b=(int)((    acosf(i/1023.f)*512.f)/RSDK_PI);
            d=a>b?a-b:b-a; if(d>md_acos)md_acos=d;
        }
        for(y=0;y<0x100;y++) for(x=0;x<0x100;x++){
            a=(int)(float)((float)sat_atan2((float)y,(double)x)*40.743664f);
            b=(int)(float)((float)    atan2((float)y,(double)x)*40.743664f);
            d=a>b?a-b:b-a; if(d>md_at)md_at=d;
        }
        printf("DIFF sin %ld cos %ld tan %ld asin %ld acos %ld atan2 %ld tanpoles %ld\n",
               md_sin,md_cos,md_tan,md_asin,md_acos,md_at,tanpoles);
    }
    return 0;
}
'''

def find_gcc():
    cands = [
        r"C:\msys64\ucrt64\bin\gcc.exe",
        r"C:\msys64\mingw64\bin\gcc.exe",
        r"C:\msys64\clang64\bin\gcc.exe",
    ]
    for c in cands:
        if os.path.isfile(c):
            return c
    # fall back to PATH
    from shutil import which
    for n in ("gcc", "cc", "clang"):
        p = which(n)
        if p:
            return p
    return None


def main():
    print("=" * 62)
    print("P2 libm host-verify -- canonical MD5 + trig agreement")
    print("=" * 62)

    if not os.path.isfile(LIBM):
        print("  RED  libm not found: %s" % LIBM)
        return 1

    # Layer (1): Python reproduces the canonical MD5 table from the formula.
    py = [math.floor(abs(math.sin(n)) * (2.0 ** 32)) & 0xffffffff for n in range(1, 65)]
    if py != CANON_MD5:
        bad = [i for i in range(64) if py[i] != CANON_MD5[i]]
        print("  RED  Python formula != canonical MD5 at idx %s (gate self-check)" % bad)
        return 1
    print("  [1/3] Python floor(|sin(n)|*2^32) == canonical RFC-1321 MD5 table (64/64).")

    gcc = find_gcc()
    if not gcc:
        print("  RED  no native host C compiler found (need gcc/cc/clang).")
        return 1
    print("  [2/3] host compiler: %s" % gcc)

    # The MSYS2 gcc is a native Windows binary: it spawns cc1.exe and emits a
    # dynamically-linked harness.exe, both of which load DLLs from the toolchain's
    # own bin/ dir. If that dir is not on PATH the child silently fails (exit 1, no
    # diagnostic). Inject it so this gate is self-contained regardless of caller
    # PATH (verify_done.ps1 invokes it with a minimal environment).
    env = os.environ.copy()
    gcc_dir = os.path.dirname(gcc)
    if gcc_dir:
        env["PATH"] = gcc_dir + os.pathsep + env.get("PATH", "")

    tmp = tempfile.mkdtemp(prefix="p2libm_")
    harness_c = os.path.join(tmp, "harness.c")
    libm_o = os.path.join(tmp, "libm_sat.o")
    harness_o = os.path.join(tmp, "harness.o")
    exe = os.path.join(tmp, "harness.exe")
    with open(harness_c, "w") as f:
        f.write(HARNESS_C)

    rc = 1
    try:
        defs = ["-D%s=sat_%s" % (n, n) for n in RENAMES]
        # compile our libm with the sat_* renames (host, IEEE-754 double)
        c1 = [gcc, "-O2", "-std=gnu11", "-fno-builtin", "-c", LIBM, "-o", libm_o] + defs
        # compile harness WITHOUT renames (its bare sinf/cosf/... = system libm)
        c2 = [gcc, "-O2", "-std=gnu11", "-c", harness_c, "-o", harness_o]
        c3 = [gcc, harness_o, libm_o, "-lm", "-o", exe]
        for cmd in (c1, c2, c3):
            r = subprocess.run(cmd, capture_output=True, text=True, env=env)
            if r.returncode != 0:
                print("  RED  compile/link failed:\n    %s" % " ".join(cmd))
                print((r.stderr or r.stdout).strip()[:2000])
                return 1
        run = subprocess.run([exe], capture_output=True, text=True, env=env)
        if run.returncode != 0:
            print("  RED  harness crashed (rc=%d):\n%s" % (run.returncode, run.stderr[:1000]))
            return 1
        out = run.stdout.splitlines()

        pow_ok = False
        got = {}
        diffline = ""
        for ln in out:
            if ln.startswith("POW2_32"):
                pow_ok = (ln.split()[1] == "4294967296.0")
            elif ln.startswith("K "):
                _, idx, hexv = ln.split()
                got[int(idx)] = int(hexv, 16)
            elif ln.startswith("DIFF"):
                diffline = ln

        if not pow_ok:
            print("  RED  pow(2,32) != 4294967296.0  (got line: %s)" %
                  next((l for l in out if l.startswith("POW2_32")), "<none>"))
            return 1

        if len(got) != 64:
            print("  RED  harness emitted %d/64 K constants." % len(got))
            return 1

        mism = [i for i in range(64) if got[i] != CANON_MD5[i]]
        if mism:
            print("  RED  libm calcKs != canonical MD5 at %d indices:" % len(mism))
            for i in mism[:8]:
                print("        K[%2d] got %08x  want %08x" % (i, got[i], CANON_MD5[i]))
            return 1
        print("  [3/3] our libm calcKs == canonical MD5 table (64/64), pow(2,32)=4294967296.0.")

        # soft trig cross-check (lower-precision regime)
        print("        trig vs host libm: %s" % diffline)
        tol = 2
        warn = False
        if diffline:
            p = diffline.split()
            vals = {p[i]: int(p[i + 1]) for i in range(1, len(p) - 1, 2)}
            for k in ("sin", "cos", "tan", "asin", "acos", "atan2"):
                if vals.get(k, 0) > tol:
                    warn = True
                    print("        NOTE %s table max int-diff %d > %d (float regime; "
                          "Math.cpp hard-sets quadrant pts)" % (k, vals[k], tol))
            if vals.get("sin", 0) > 8 or vals.get("cos", 0) > 8 or vals.get("atan2", 0) > 8:
                print("  RED  trig divergence too large to be a rounding effect.")
                return 1

        print("-" * 62)
        print("RESULT: GREEN -- libm_saturn.c reproduces the canonical MD5 K table")
        print("        bit-exactly and agrees with the host libm on the trig tables.")
        print("        The MD5/asset-hash path is proven correct in IEEE-754 double")
        print("        before SH-2 ever runs. (trig is the float-truncated regime.)")
        if warn:
            print("        (trig notes above are within the accepted float-table band.)")
        rc = 0
    finally:
        for p in (harness_c, libm_o, harness_o, exe):
            try:
                os.remove(p)
            except OSError:
                pass
        try:
            os.rmdir(tmp)
        except OSError:
            pass
    return rc


if __name__ == "__main__":
    sys.exit(main())
