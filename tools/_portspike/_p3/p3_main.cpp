// =============================================================================
// p3_main.cpp -- P3 boot entry (engine true-port, #202).
//
// The minimal main() the P3 task names: it calls the REAL core entry
// RSDK::RunRetroEngine(0, NULL). ParseArguments(0,NULL) is argv-safe (its only
// loop is `for(a=0;a<argc;++a)`, never run at argc==0; RetroEngine.cpp:594), so
// no fake argv is needed. RunRetroEngine never returns on a successful boot --
// the while(RenderDevice::isRunning) frame loop (RetroEngine.cpp:110) spins
// forever (isRunning is only cleared by a quit the Saturn shim never sends), so
// the captured savestate finds SH2-M parked in core .text with engine.initialized
// == true. crt0.s calls _main; if main ever returns, crt0 hangs.
//
// The freestanding newlib backend (_sbrk over the WRAM-L heap + the unused
// syscall stubs) lives in p3_syscalls.c -- kept in its own C TU so the WHOLE
// newlib syscall set is defined by us, and libc.a's lib_a-syscalls.o (which would
// otherwise drag in its own conflicting _sbrk plus an undefined `end`) is never
// pulled into the link. Saturn-only P3 scaffolding (P6 decides its fate).
// =============================================================================
#include "RSDK/Core/RetroEngine.hpp"

int main(void)
{
    RSDK::RunRetroEngine(0, NULL);
    return 0;
}
