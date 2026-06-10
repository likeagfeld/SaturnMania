// =============================================================================
// CppRuntime_Saturn.cpp -- freestanding C++ ABI allocation operators for the
// engine true-port (P2, #201).
//
// The jo Saturn toolchain links no libstdc++/libsupc++, so the global
// operator new / operator delete that the core's C++ codegen references are
// unresolved at the real Saturn link. The P2 link gate (qa_p2_saturn_link.sh)
// named exactly one such symbol -- `operator delete(void*)` (_ZdlPv) -- pulled
// in by a destructor/virtual-deletion path in the linked core object set.
//
// Route every global allocation operator to newlib malloc/free (the same heap
// the core's own RSDK allocations use). The set is defined in full (new / new[]
// / delete / delete[] + the C++14 sized-delete variants) so P3/P4 -- which pull
// more of the core -- stay closed without revisiting this file. Listed as an
// explicit .o in the link, so all operators are included regardless of which
// the current object set happens to reference.
//
// -fno-exceptions: operator new CANNOT throw std::bad_alloc. A failed malloc
// returns 0; the core validates its own allocations (it never assumes new
// throws). size_t is taken from <stdlib.h> so the mangled names (_Znwj/_Znaj
// on SH-2, where size_t == unsigned int) match the compiler-generated calls
// exactly. The deletes are noexcept to match the standard replaceable-function
// signatures (exception specs are not mangled, but matching avoids GCC's
// "looser throw specifier" diagnostic).
// =============================================================================
#include <stdlib.h> // malloc, free, size_t

// __dso_handle -- the DSO-identity token __cxa_atexit(dtor, obj, &__dso_handle)
// passes when a static C++ object with a non-trivial destructor is registered
// for teardown. crtbegin.o normally defines it, but -nostartfiles excludes the
// CRT objects, so the freestanding C++ runtime must provide it (Itanium C++ ABI
// 3.3.5). The engine never calls exit() (RunRetroEngine spins forever), so the
// registered destructors never run and the token's VALUE is irrelevant -- only
// its existence matters for link closure. The sh-none-elf +1-underscore label
// convention maps this C identifier `__dso_handle` to ELF `___dso_handle`, the
// exact symbol UserCore_Saturn.o's saturnUserCore ctor references (verified via
// the .symtab dump: `___dso_handle  shndx=UND`).
extern "C" void *__dso_handle = 0;

void *operator new(size_t sz) { return malloc(sz ? sz : 1); }
void *operator new[](size_t sz) { return malloc(sz ? sz : 1); }

void operator delete(void *p) noexcept { free(p); }
void operator delete[](void *p) noexcept { free(p); }
void operator delete(void *p, size_t) noexcept { free(p); }
void operator delete[](void *p, size_t) noexcept { free(p); }
