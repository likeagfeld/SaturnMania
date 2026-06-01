#ifndef MANIA_TITLE_TITLESETUP_H
#define MANIA_TITLE_TITLESETUP_H

/* Phase 1.2 — Title scene class header.
 *
 * Mirrors `tools/_decomp_raw/SonicMania_Objects_Title_TitleSetup.h:7-28`
 * verbatim. Field layout of EntityTitleSetup + ObjectTitleSetup is copied
 * 1:1 so the decomp port's struct accesses (`self->state`, `self->timer`,
 * `TitleSetup->aniFrames`, etc.) compile unchanged. */

#include "../../Game.h"

typedef struct ObjectTitleSetup {
    RSDK_OBJECT
    bool32  useAltIntroMusic;
    uint16  aniFrames;
    uint16  sfxMenuBleep;
    uint16  sfxMenuAccept;
    uint16  sfxRing;
} ObjectTitleSetup;

typedef struct EntityTitleSetup {
    RSDK_ENTITY
    StateMachine(state);
    StateMachine(stateDraw);
    int32    timer;
    Vector2  drawPos;
    int32    touched;
    Animator animator;
} EntityTitleSetup;

extern ObjectTitleSetup *TitleSetup;

void TitleSetup_Update(void);
void TitleSetup_LateUpdate(void);
void TitleSetup_StaticUpdate(void);
void TitleSetup_Draw(void);
void TitleSetup_Create(void *data);
void TitleSetup_StageLoad(void);

bool32 TitleSetup_VideoSkipCB(void);

/* State machine (decomp TitleSetup.c:137-384). */
void TitleSetup_State_Wait(void);
void TitleSetup_State_AnimateUntilFlash(void);
void TitleSetup_State_FlashIn(void);
void TitleSetup_State_WaitForSonic(void);
void TitleSetup_State_SetupLogo(void);
void TitleSetup_State_WaitForEnter(void);
void TitleSetup_State_FadeToMenu(void);
void TitleSetup_State_FadeToVideo(void);

/* StateDraw callbacks (decomp TitleSetup.c:386-409). */
void TitleSetup_Draw_FadeBlack(void);
void TitleSetup_Draw_DrawRing(void);
void TitleSetup_Draw_Flash(void);

#endif
