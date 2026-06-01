#ifndef MANIA_TITLE_TITLE3DSPRITE_H
#define MANIA_TITLE_TITLE3DSPRITE_H

/* Phase 1.1 — Title scene class header (stub).
 * Mirrors the public surface declared in
 * tools/_decomp_raw/SonicMania_Objects_Title_Title3DSprite.h.
 * Real callback bodies land in Phase 1.2. */

#include "../../Game.h"

typedef enum {
    TITLE3DSPRITE_MOUNTAIN_L = 0,
    TITLE3DSPRITE_MOUNTAIN_M = 1,
    TITLE3DSPRITE_MOUNTAIN_S = 2,
    TITLE3DSPRITE_TREE       = 3,
    TITLE3DSPRITE_BUSH       = 4
} Title3DSpriteFrames;

typedef struct ObjectTitle3DSprite {
    RSDK_OBJECT
    int32  islandSize;
    int32  height;
    int32  baseDepth;
    uint16 aniFrames;
} ObjectTitle3DSprite;

typedef struct EntityTitle3DSprite {
    RSDK_ENTITY
    int32    frame;
    Vector2  relativePos;
    Animator animator;
} EntityTitle3DSprite;

extern ObjectTitle3DSprite *Title3DSprite;

void Title3DSprite_Update(void);
void Title3DSprite_LateUpdate(void);
void Title3DSprite_StaticUpdate(void);
void Title3DSprite_Draw(void);
void Title3DSprite_Create(void *data);
void Title3DSprite_StageLoad(void);

/* Phase 1.32 — Saturn-batched API. Called from mania_tick's title branch.
 * Tick_All advances `g_title_bg_angle` (decomp TitleBG_StaticUpdate:39-40)
 * + computes relativePos/zdepth for all 58 entities. Draw_All issues the
 * software-projected billboard draws via title3d_bb_draw_frame. */
void Title3DSprite_Tick_All(void);
void Title3DSprite_Draw_All(void);

extern int g_title_bg_angle;

#endif
