#ifndef MANIA_TITLE_TITLELOGO_H
#define MANIA_TITLE_TITLELOGO_H

/* Phase 1.2 — Title scene class header.
 * Surface mirror of `tools/_decomp_raw/SonicMania_Objects_Title_TitleLogo.h
 * :7-44`. Plus-only fields are #ifdef'd OUT for the shipping non-Plus
 * Saturn build. */

#include "../../Game.h"

typedef enum {
    TITLELOGO_EMBLEM      = 0,
    TITLELOGO_RIBBON      = 1,
    TITLELOGO_GAMETITLE   = 2,
    TITLELOGO_POWERLED    = 3,
    TITLELOGO_COPYRIGHT   = 4,
    TITLELOGO_RINGBOTTOM  = 5,
    TITLELOGO_PRESSSTART  = 6
} TitleLogoTypes;

typedef struct ObjectTitleLogo {
    RSDK_OBJECT
    uint16 aniFrames;
} ObjectTitleLogo;

typedef struct EntityTitleLogo {
    RSDK_ENTITY
    int32    type;
    bool32   showRibbonCenter;
    int32    timer;
    int32    storeY;
    Animator mainAnimator;
    Animator ribbonCenterAnimator;
} EntityTitleLogo;

extern ObjectTitleLogo *TitleLogo;

void TitleLogo_Update(void);
void TitleLogo_LateUpdate(void);
void TitleLogo_StaticUpdate(void);
void TitleLogo_Draw(void);
void TitleLogo_Create(void *data);
void TitleLogo_StageLoad(void);

void TitleLogo_SetupPressStart(void);

#endif
