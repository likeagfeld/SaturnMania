/* ============================================================================
 * p6_handport_stubs.c -- P6.7 Player wave (Task #227, 2026-06-12).
 *
 * The DIAG (P6SCENE=1) flavor drops every parked hand-port game source from
 * SRCS (Makefile; the pack imports exactly ONE src/ symbol,
 * rsdk_storage_load_to_lwram, so only src/rsdk/storage.c stays). The jo
 * build compiles everything with -fkeep-inline-functions, so `static inline`
 * helpers in the surviving headers (src/rsdk/object.h rsdk_entity_at, etc.)
 * are EMITTED in every including TU even though nothing reachable calls
 * them -- and their referenced globals, defined in the dropped sources, come
 * up undefined at the final link. This TU defines those anchors as inert
 * NULLs for the diag image only; the SHIPPING `make` never compiles it and
 * keeps the real definitions (src/rsdk/object.c, ...).
 * ========================================================================== */
#include <stdint.h>

/* src/rsdk/object.c:29 -- entity window base for object.h rsdk_entity_at().
 * NULL is correct here: the diag never allocates the hand-port entity window
 * (the ENGINE's objectEntityList window replaces it). */
uint8_t *g_rsdk_entity_buf = (uint8_t *)0;
