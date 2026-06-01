#ifndef MANIA_GHZ_GHZSETUP_H
#define MANIA_GHZ_GHZSETUP_H

/* Phase 2.1 — GHZSetup class header.
 *
 * Mirrors `tools/_decomp_raw/SonicMania_Objects_GHZ_GHZSetup.c` (the
 * decomp ships only the .c — no separate .h — so the Object/Entity
 * structs are derived from field references in the .c body). Per the
 * decomp:
 *   - aniTiles                 (uint16) used by RSDK.DrawAniTiles in StaticUpdate
 *   - paletteTimer             (int32)  drives palette rotation (lines 17-27)
 *   - sunFlowerTimer/Frame     (int32)  + sunFlowerDurationTable
 *   - extendFlowerTimer/Frame  (int32)  + extendFlowerDurationTable
 *
 * The Phase 2.1 subset uses only `aniTiles` (sentinel) — palette
 * rotation + animated tiles are deferred to 2.3 cosmetic work. The
 * fields are kept in the struct for 1:1 decomp-port compatibility so
 * subsequent ports compile without touching the layout. */

#include "../../Game.h"

typedef struct ObjectGHZSetup {
    RSDK_OBJECT
    uint16 aniTiles;
    int32  paletteTimer;
    int32  sunFlowerFrame;
    int32  sunFlowerTimer;
    int32  extendFlowerFrame;
    int32  extendFlowerTimer;
    int32  sunFlowerDurationTable[8];
    int32  extendFlowerDurationTable[16];
} ObjectGHZSetup;

typedef struct EntityGHZSetup {
    RSDK_ENTITY
    /* GHZSetup has no per-entity fields in the decomp (all state is on
     * the Object globals). Empty body — the RSDK_ENTITY macro already
     * provides the standard entity header. */
} EntityGHZSetup;

extern ObjectGHZSetup *GHZSetup;

/* Standard RSDK class callbacks (per decomp). */
void GHZSetup_Update(void);
void GHZSetup_LateUpdate(void);
void GHZSetup_StaticUpdate(void);
void GHZSetup_Draw(void);
void GHZSetup_Create(void *data);
/* Phase 2.3k-mid (2026-05-28): void return preserves the RSDK class-
 * registry stage_load contract (rsdk_class_stage_load_fn = void (*)(void)).
 * Sub-asset load failure is communicated via g_ghz_load_error_code
 * (scene_ghz.h) — bits 0..5 record which of FG.TMP/PAL/CEL/PAT or
 * SKY.PAL/DAT failed. mania_load_ghz_scene checks the bitmask before
 * advancing the title state machine. */
void GHZSetup_StageLoad(void);

#endif /* MANIA_GHZ_GHZSETUP_H */
