#ifndef MANIA_TITLE_TITLEBG_H
#define MANIA_TITLE_TITLEBG_H

/* Phase 1.2 — Title scene class header.
 * Surface mirror of `tools/_decomp_raw/SonicMania_Objects_Title_TitleBG.h
 * :6-29`. */

#include "../../Game.h"

typedef enum {
    TITLEBG_MOUNTAIN1     = 0,
    TITLEBG_MOUNTAIN2     = 1,
    TITLEBG_REFLECTION    = 2,
    TITLEBG_WATERSPARKLE  = 3,
    TITLEBG_WINGSHINE     = 4
} TitleBGTypes;

typedef struct ObjectTitleBG {
    RSDK_OBJECT
    int32  palTimer;
    int32  timer;
    int32  angle;
    uint16 aniFrames;
} ObjectTitleBG;

typedef struct EntityTitleBG {
    RSDK_ENTITY
    int32    type;
    int32    timer;
    Animator animator;
} EntityTitleBG;

extern ObjectTitleBG *TitleBG;

void TitleBG_Update(void);
void TitleBG_LateUpdate(void);
void TitleBG_StaticUpdate(void);
void TitleBG_Draw(void);
void TitleBG_Create(void *data);
void TitleBG_StageLoad(void);

void TitleBG_SetupFX(void);

/* Phase 1.34 — Saturn-batched API. Called from mania_tick's title branch
 * BEFORE Title3DSprite_Tick_All/Draw_All so the TitleBG sub-types
 * (Mountain Top strips, Reflection strips, WingShines) sort BEHIND the
 * Title3DSprite billboards (smaller Z = closer per Saturn perspective).
 *
 * Tick_All applies the decomp's per-type motion (Update at TitleBG.c:12-29):
 *   * MOUNTAIN1/MOUNTAIN2/REFLECTION/WATERSPARKLE: position.x -= 0x10000;
 *     wrap when position.x < -0x800000 by adding 0x3000000.
 *   * WINGSHINE: position.y += 0x10000; every 32 frames position.y -= 0x200000.
 *
 * Draw_All issues one slDispSprite per entity at its current position via
 * title3d_bg_draw_frame (4-bpp Color Bank 16). */
void TitleBG_Tick_All(void);
void TitleBG_Draw_All(void);

#endif
