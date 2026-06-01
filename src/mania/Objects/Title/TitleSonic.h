#ifndef MANIA_TITLE_TITLESONIC_H
#define MANIA_TITLE_TITLESONIC_H

/* Phase 1.1 — Title scene class header (stub).
 * Surface mirror of tools/_decomp_raw/SonicMania_Objects_Title_TitleSonic.h.
 * Real bodies land in Phase 1.2. */

#include "../../Game.h"

typedef struct ObjectTitleSonic {
    RSDK_OBJECT
    uint16 aniFrames;
} ObjectTitleSonic;

typedef struct EntityTitleSonic {
    RSDK_ENTITY
    Animator animatorSonic;
    Animator animatorFinger;
} EntityTitleSonic;

extern ObjectTitleSonic *TitleSonic;

void TitleSonic_Update(void);
void TitleSonic_LateUpdate(void);
void TitleSonic_StaticUpdate(void);
void TitleSonic_Draw(void);
void TitleSonic_Create(void *data);
void TitleSonic_StageLoad(void);

#endif
