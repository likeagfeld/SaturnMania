// =============================================================================
// p3_syscalls.c -- freestanding newlib syscall layer for the P3 core boot (#202).
//
// newlib's libc.a ships a single object (lib_a-syscalls.o, newlib-3.0.0
// newlib/libc/sys/sh/syscalls.c) that defines the ENTIRE POSIX syscall set --
// including _sbrk -- and its _sbrk grows a heap from the linker symbol `end`
// with no relation to our two-bank WRAM-L heap. If that object is pulled (for
// ANY one syscall an libc path references, e.g. _write from a stdio tail), it
// drags in its own _sbrk (-> "multiple definition" against ours) AND references
// an undefined `end`. The only robust fix for a custom two-bank heap is to
// DEFINE THE WHOLE SET ourselves so lib_a-syscalls.o is never linked.
//
// This TU is compiled -x c (C linkage), so each `_name` becomes ELF `__name`
// (the sh-none-elf +1-underscore convention), exactly the symbols newlib's
// internal callers reference. Every stub here is link-bait that is never CALLED
// on the Saturn boot path (the engine does no file/console I/O and never exits)
// EXCEPT _sbrk, which is the real malloc backend InitStorage (Storage.cpp:60)
// depends on for its 608 KB of pools.
// =============================================================================

// p3.linker defines these at the WRAM-L heap window bounds. C `__heap_start`
// (two underscores) becomes ELF `___heap_start` (three) -- the names p3.linker
// emits. The heap is [___heap_start, ___heap_end), carved AFTER the WRAM-L .bss
// with >= 608 KB of room (measured window 650,068 B).
extern char __heap_start;
extern char __heap_end;

void *_sbrk(int incr)
{
    static char *heapBreak = &__heap_start;
    char *prev = heapBreak;
    char *next = heapBreak + incr;
    if (next > &__heap_end)
        return (void *)-1; // out of heap: malloc returns NULL, InitStorage fails
    heapBreak = next;
    return (void *)prev;
}

// --- unused POSIX stubs: present only so lib_a-syscalls.o stays unlinked -------
int _close(int fd) { (void)fd; return -1; }
int _fstat(int fd, void *st) { (void)fd; (void)st; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
int _lseek(int fd, int off, int dir) { (void)fd; (void)off; (void)dir; return 0; }
int _read(int fd, char *buf, int len) { (void)fd; (void)buf; (void)len; return 0; }
int _write(int fd, const char *buf, int len) { (void)fd; (void)buf; return len; }
int _open(const char *path, int flags, int mode) { (void)path; (void)flags; (void)mode; return -1; }
int _getpid(void) { return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; return -1; }
void _exit(int code) { (void)code; for (;;) {} }
int _fork(void) { return -1; }
int _wait(int *status) { (void)status; return -1; }
int _link(const char *a, const char *b) { (void)a; (void)b; return -1; }
int _unlink(const char *a) { (void)a; return -1; }
int _stat(const char *a, void *st) { (void)a; (void)st; return 0; }
int _times(void *buf) { (void)buf; return -1; }
int _gettimeofday(void *tv, void *tz) { (void)tv; (void)tz; return 0; }
int _execve(const char *a, char *const b[], char *const c[]) { (void)a; (void)b; (void)c; return -1; }
