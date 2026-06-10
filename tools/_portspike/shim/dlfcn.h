#ifndef _PORTSPIKE_DLFCN_H
#define _PORTSPIKE_DLFCN_H
// Probe 2 (engine true-port feasibility spike, Task #194).
// The Saturn has NO dynamic linker. RSDKv5 Link.hpp:400 includes <dlfcn.h> for
// every platform EXCEPT Windows and Switch:
//     #if !(RETRO_PLATFORM == RETRO_WIN || RETRO_PLATFORM == RETRO_SWITCH)
// The real Saturn port adds `|| RETRO_PLATFORM == RETRO_SATURN` to that
// Switch-style exclusion (the Switch console is the proven no-dlopen template).
// This stub keeps rsdkv5-src pristine while the spike measures the core; the
// dl* symbols are only ever called from Link.cpp's mod/dynamic-library loader,
// which is NOT in any Saturn core TU (mod loader off, no plugin DLLs).
#define RTLD_LAZY   1
#define RTLD_NOW    2
#define RTLD_LOCAL  0
#define RTLD_GLOBAL 256
#ifdef __cplusplus
extern "C" {
#endif
static inline void *dlopen(const char *p, int m) { (void)p; (void)m; return 0; }
static inline int dlclose(void *h) { (void)h; return 0; }
static inline void *dlsym(void *h, const char *s) { (void)h; (void)s; return 0; }
static inline char *dlerror(void) { return 0; } // POSIX: char *dlerror(void)
#ifdef __cplusplus
}
#endif
#endif
