#ifndef MANIA_MENU_UIBACKGROUND_H
#define MANIA_MENU_UIBACKGROUND_H

/* Phase 3.2.a (Task #145) — UIBackground class header.
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Menu_UIBackground.h`
 * (91 LOC body + this header). Mania Mode menu's animated circular
 * backdrop entity. One per UIControl (27 instances in Menu/Scene1.bin).
 *
 * Decomp citation: tools/_decomp_raw/SonicMania_Objects_Menu_UIBackground.h
 *   L9-20  struct ObjectUIBackground  (non-Plus 15-entry bgColors[])
 *   L22-29 struct EntityUIBackground  (stateDraw + type + timer + animator)
 *
 * Saturn-side adaptations:
 *   * MANIA_USE_PLUS == 0 (Mania-only target, no DLC); bgColors[] is the
 *     15-color non-Plus table.
 *   * `color` is upstream's RGB888 packed-u32 alias. We expose it via
 *     uint32_t since Saturn-side palette/CRAM conversion happens in
 *     UIBackground_DrawNormal (Phase 3.2.c+ when the VDP2 circle-fill
 *     path lands; the Phase 3.2.a foundation port stubs DrawNormal to
 *     a clear-screen so the menu scene's draw pass produces a flat
 *     non-corrupted backdrop). */

#include "../../Game.h"

/* Upstream `color` alias (RetroEngine.hpp). Decomp uses it as a packed
 * RGB888 quantity (0x00RRGGBB layout per Drawing.cpp FillScreen). */
typedef uint32_t color;

typedef struct ObjectUIBackground {
    RSDK_OBJECT
    /* Decomp .h L15-16 — non-Plus 15-color background palette table.
     * One color per main-menu mode (Mania / SaveSelect / TimeAttack /
     * Competition / Options / Extras / ...). Layout-verbatim from
     * decomp: `bgColors[15] = { 0xF0C800, 0xF08C18, ... }`. */
    color   bgColors[15];
    color  *activeColors;
    uint16  aniFrames;
} ObjectUIBackground;

typedef struct EntityUIBackground {
    RSDK_ENTITY
    StateMachine(stateDraw);
    int32    type;       /* UIBACKGROUND_UNUSED = 0 (only one type)  */
    int32    timer;
    Animator animator;   /* editor-only per decomp .h L29; unused at  */
                         /* runtime but kept for offset compat       */
} EntityUIBackground;

extern ObjectUIBackground *UIBackground;

/* Standard Entity Events — mirror decomp .h L35-40. */
void UIBackground_Update(void);
void UIBackground_LateUpdate(void);
void UIBackground_StaticUpdate(void);
void UIBackground_Draw(void);
void UIBackground_Create(void *data);
void UIBackground_StageLoad(void);

/* Extra entity helper — mirrors decomp .h L48. */
void UIBackground_DrawNormal(void);

#endif /* MANIA_MENU_UIBACKGROUND_H */
