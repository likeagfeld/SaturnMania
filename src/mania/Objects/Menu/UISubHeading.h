#ifndef MANIA_MENU_UISUBHEADING_H
#define MANIA_MENU_UISUBHEADING_H

/* Phase 3.2.c.1 (Task #148) — UISubHeading class header.
 *
 * Mechanical port of `tools/_decomp_raw/SonicMania_Objects_Menu_UISubHeading.h`
 * (65 LOC). The section-header widget that paints a parallelogram
 * background + sprite-frame label for each Save Select sub-menu group
 * ("CHARACTER SELECT" / "SECRETS" / etc.).
 *
 * Decomp citation: tools/_decomp_raw/SonicMania_Objects_Menu_UISubHeading.h
 *   L6-10   struct ObjectUISubHeading  (aniFrames)
 *   L12-30  struct EntityUISubHeading  (state + 4 unused + size + listID +
 *                                       frameID + align + offset +
 *                                       bgEdgeSize + storedListID +
 *                                       storedFrameID + animator + textFrames)
 *   L35-46  Standard Entity Events + Serialize
 *
 * Saturn-side decisions:
 *   * MANIA_USE_PLUS == 0 — the entire SaveSelect/Secrets helper block
 *     (UISubHeading_Initialize/HandleUnlocks/etc., decomp L82-413) is
 *     skipped (Plus-only DLC scope). Saturn ports keep ONLY the per-entity
 *     Update/Draw/Create/StageLoad lifecycle so Scene1.bin's UISubHeading
 *     entity hashes resolve cleanly. */

#include "../../Game.h"

typedef struct ObjectUISubHeading {
    RSDK_OBJECT
    uint16 aniFrames;
} ObjectUISubHeading;

typedef struct EntityUISubHeading {
    RSDK_ENTITY
    StateMachine(state);
    int32   unused1;
    int32   unused2;
    int32   unused3;
    int32   unused4;
    Vector2 size;
    int32   listID;
    int32   frameID;
    int32   align;
    int32   offset;
    int32   bgEdgeSize;
    int32   storedListID;
    int32   storedFrameID;
    Animator animator;
    uint16  textFrames;
} EntityUISubHeading;

extern ObjectUISubHeading *UISubHeading;

/* Standard Entity Events — mirror decomp .h L35-41. */
void UISubHeading_Update(void);
void UISubHeading_LateUpdate(void);
void UISubHeading_StaticUpdate(void);
void UISubHeading_Draw(void);
void UISubHeading_Create(void *data);
void UISubHeading_StageLoad(void);

#endif /* MANIA_MENU_UISUBHEADING_H */
