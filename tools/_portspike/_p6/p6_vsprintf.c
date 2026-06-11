/* =============================================================================
 * p6_vsprintf.c -- P6.7 wave-1 (Task #210): integer-only vsprintf for the
 * proof pack, same platform-shim class as the pack's _sbrk + syscall set.
 *
 * WHY (MEASURED 2026-06-11): LogHelpers_Print (verbatim decomp) calls
 * vsprintf; newlib's vsprintf drags the float-capable machinery
 * (_svfprintf_r 4,984 B + _dtoa_r 3,828 B + mprec + localeconv ~= 10.2 KB),
 * pushing _end to 0x060C0B28 -- 2,856 B PAST the 0x060C0000 overlay floor.
 * The format strings actually reaching it are only "%s" / "%s: %d" /
 * "... = %d" (LogHelpers.c:30,46 + Options.c:59-68,223-224 -- the full
 * decomp cache greps to {%s,%d} for this call path), so an integer-only
 * implementation is behavior-identical for every live call site.
 *
 * Supports: %s %d %i %u %x %X %c %p %%  (no width/precision/length mods --
 * none appear in the measured format set; an unsupported directive emits
 * the raw characters so a future caller is VISIBLE in the log, not silent).
 * Defining `vsprintf` here resolves the pack-internal reference at the
 * ld -r stage, so newlib's lib_a-vsprintf.o (and its float closure) never
 * enters the final link (sole referencer measured: p6_scene_pack.o).
 * ============================================================================= */
#include <stdarg.h>
#include <stddef.h>

static char *p6_put_uint(char *p, unsigned v, unsigned base, int upper)
{
    char tmp[12];
    int n = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do {
        tmp[n++] = digits[v % base];
        v /= base;
    } while (v);
    while (n)
        *p++ = tmp[--n];
    return p;
}

int vsprintf(char *buf, const char *fmt, va_list ap)
{
    char *p = buf;
    for (; *fmt; ++fmt) {
        if (*fmt != '%') {
            *p++ = *fmt;
            continue;
        }
        ++fmt;
        switch (*fmt) {
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s)
                    s = "(null)";
                while (*s)
                    *p++ = *s++;
                break;
            }
            case 'd':
            case 'i': {
                int v = va_arg(ap, int);
                unsigned u = (unsigned)v;
                if (v < 0) {
                    *p++ = '-';
                    u = (unsigned)(-v);
                }
                p = p6_put_uint(p, u, 10, 0);
                break;
            }
            case 'u':
                p = p6_put_uint(p, va_arg(ap, unsigned), 10, 0);
                break;
            case 'x':
                p = p6_put_uint(p, va_arg(ap, unsigned), 16, 0);
                break;
            case 'X':
                p = p6_put_uint(p, va_arg(ap, unsigned), 16, 1);
                break;
            case 'p':
                *p++ = '0';
                *p++ = 'x';
                p = p6_put_uint(p, (unsigned)(size_t)va_arg(ap, void *), 16, 0);
                break;
            case 'c':
                *p++ = (char)va_arg(ap, int);
                break;
            case '%':
                *p++ = '%';
                break;
            case 0:
                --fmt; /* trailing '%': emit and let the loop terminate */
                *p++ = '%';
                break;
            default:
                /* unsupported directive: emit raw so it is VISIBLE */
                *p++ = '%';
                *p++ = *fmt;
                break;
        }
    }
    *p = 0;
    return (int)(p - buf);
}
