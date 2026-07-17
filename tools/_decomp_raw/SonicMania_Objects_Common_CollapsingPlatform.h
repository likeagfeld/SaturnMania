#ifndef OBJ_COLLAPSINGPLATFORM_H
#define OBJ_COLLAPSINGPLATFORM_H

#include "Game.h"

typedef enum {
    COLLAPSEPLAT_LEFT,
    COLLAPSEPLAT_RIGHT,
    COLLAPSEPLAT_CENTER,
    COLLAPSEPLAT_LR,
    COLLAPSEPLAT_LRC,
} CollapsingPlatformTypes;

typedef enum {
    COLLAPSEPLAT_TARGET_LOW,
    COLLAPSEPLAT_TARGET_HIGH,
} CollapsingPlatformTargetLayers;

// Object Class
struct ObjectCollapsingPlatform {
    RSDK_OBJECT
    uint8 shift;
    Animator animator;
    uint16 aniFrames;
    uint16 sfxCrumble;
};

// Entity Class
struct EntityCollapsingPlatform {
    RSDK_ENTITY
    StateMachine(state);
    Vector2 size;
    bool32 respawn;
    uint16 targetLayer;
    uint8 type;
    int32 delay;
    bool32 eventOnly;
    bool32 mightyOnly;
    int32 unused1;
    int32 collapseDelay;
#if defined(SATURN_GLOBALS_RETARGET)
    // CollapsingPlatform port (2026-07-16): storedTiles[256] (512 B) makes
    // sizeof(EntityCollapsingPlatform) ~656 B -- scene placements live in NARROW
    // 344 B slots (Object.hpp:88/114) and even a clamped array cannot cover the
    // MEASURED whole-game ceiling (176 tiles GHZ2 slot 45; 121 in GHZ1 slot 828;
    // tools/_cplat_census.py) inside the 200 B budget. The Saturn .c reads the
    // tiles LIVE via RSDK.GetTile in State_Left/Right/Center instead (exact for
    // every shipped placement: BreakableWall_State_FallingTile removes tiles only
    // after timer>=3 frames, and all GHZ placements are respawn=false). Precedent:
    // CutsceneHBH colors[128]->[1] (census Cutscene/CutsceneHBH.h). Entity 148 B
    // (compile-measured) <= 344.
    uint16 storedTiles[1];
#else
    uint16 storedTiles[256];
#endif
    Hitbox hitboxTrigger;
    Vector2 stoodPos;
};

// Object Struct
extern ObjectCollapsingPlatform *CollapsingPlatform;

// Standard Entity Events
void CollapsingPlatform_Update(void);
void CollapsingPlatform_LateUpdate(void);
void CollapsingPlatform_StaticUpdate(void);
void CollapsingPlatform_Draw(void);
void CollapsingPlatform_Create(void *data);
void CollapsingPlatform_StageLoad(void);
#if GAME_INCLUDE_EDITOR
void CollapsingPlatform_EditorDraw(void);
void CollapsingPlatform_EditorLoad(void);
#endif
void CollapsingPlatform_Serialize(void);

// Extra Entity Functions
void CollapsingPlatform_State_Left(void);
void CollapsingPlatform_State_Right(void);
void CollapsingPlatform_State_Center(void);
void CollapsingPlatform_State_LeftRight(void);
void CollapsingPlatform_State_LeftRightCenter(void);

#endif //! OBJ_COLLAPSINGPLATFORM_H
