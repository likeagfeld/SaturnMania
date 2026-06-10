// =============================================================================
// p6_dma_stubs.c -- SGL/SCU coupling stubs for the P6.2 CD/GFS file backend
// (Task #206). io-green only.
//
// WHY THIS FILE EXISTS:
//   LIBCD.A (the SBL/SGL CD library that owns GFS_*) is compiled against the
//   full SGL runtime and carries undefined references to a handful of SGL/SCU
//   transfer helpers (slDMACopy/slDMAXCopy/slDMAStatus, the SCU-DMA register
//   shims DMA_ScuSetPrm/Start/GetStatus, SetCDFunc, and the cache-purge helper
//   CSH_Purge). Those live in LIBSGL.A, which the bare P6 boot does NOT link
//   (P6 has no slInitSystem). So the linker would fail on those symbols.
//
//   WHY NO-OP / memcpy IS CORRECT AT RUNTIME (not just link-satisfying):
//   GFS defaults to GFS_TMODE_CPU (GFS_TRN.C:41 GFTR_TMODE_DFL == GFS_TMODE_CPU;
//   set via GFTR_SetMode at transfer init, GFS_TRN.C:230). In the CPU transfer
//   path the library copies sector data with a software (SH-2) memcpy and never
//   touches the SCU-DMA register shims -- those are referenced only from the
//   GFS_TMODE_SCU / GFS_TMODE_SDMA branches (GFS_TRN.C:611-627 / 660-710), which
//   our backend never selects (p6_gfs.c forces GFS_SetTmode(gfs, GFS_TMODE_CPU)).
//   So slDMA*/DMA_Scu*/SetCDFunc are link-only and stay dormant. CSH_Purge is a
//   cache write-back/invalidate; with CPU copy the SH-2 reads back its own cached
//   writes coherently, so a no-op purge is safe.
//
//   slDMACopy/slDMAXCopy are given a real memcpy body (dst = arg2, src = arg1,
//   n = arg3) so that IF any code path did reach them the behaviour would be a
//   correct synchronous copy rather than a silent no-op.
//
// SELF-CONTAINED ON PURPOSE: no SGL header is included. C symbols carry no name
// mangling, so the linker matches purely by name; and SH-2 ELF gcc passes every
// <=32-bit argument in r4-r7 identically, so primitive types (void*/unsigned
// long) are ABI-compatible with SGL's Uint32/Bool prototypes. If the real
// LIBCD.A references a slightly different subset, the link error names the exact
// missing/duplicate symbol and this file is edited to match.
// =============================================================================
#include <string.h>

void slDMACopy(void *src, void *dst, unsigned long nbyte)
{
    if (dst && src && nbyte)
        memcpy(dst, src, (size_t)nbyte);
}

void slDMAXCopy(void *src, void *dst, unsigned long nbyte, unsigned short mode)
{
    (void)mode;
    if (dst && src && nbyte)
        memcpy(dst, src, (size_t)nbyte);
}

int slDMAStatus(void)
{
    return 0; // never busy: our copies complete synchronously inside slDMACopy
}

void DMA_ScuSetPrm(void *prm, unsigned long mode)
{
    (void)prm;
    (void)mode;
}

void DMA_ScuStart(unsigned long mode)
{
    (void)mode;
}

void DMA_ScuGetStatus(void *status, unsigned long mode)
{
    (void)status;
    (void)mode;
}

void SetCDFunc(void (*func)(void))
{
    (void)func;
}

void CSH_Purge(void *addr, unsigned long nbyte)
{
    (void)addr;
    (void)nbyte;
}
