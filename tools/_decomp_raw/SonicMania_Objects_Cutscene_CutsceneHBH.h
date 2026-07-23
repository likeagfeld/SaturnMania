#ifndef OBJ_CUTSCENEHBH_H
#define OBJ_CUTSCENEHBH_H

#include "Game.h"

typedef enum {
    HBH_GUNNER,
    HBH_SHINOBI,
    HBH_MYSTIC,
    HBH_RIDER,
    HBH_KING,
    HBH_ROGUE_FANG,
    HBH_ROGUE_BEAN,
    HBH_ROGUE_BARK,
    HBH_KINGDAMAGED,
    HBH_PILE,
    HBH_KINGTMZ2,
} HBHTypes;

// Object Class
struct ObjectCutsceneHBH {
    RSDK_OBJECT
    uint16 aniFrames;
#if defined(P6_GHZCUT_BOOT)
    // Saturn: the bank0 0x80-0xFF save/restore this backs is NO-OP'd
    // (Store/RestorePalette in the .c) -- shrink the dead 512 B.
    color paletteColors[1];
#else
    color paletteColors[128];
#endif
};

// Entity Class
struct EntityCutsceneHBH {
    RSDK_ENTITY
    StateMachine(state);
    uint8 characterID;
    int32 oscillate;
    int32 hiddenAtStart;
    int32 curAngle;
    int32 unused1;
    Vector2 startPos;
    Vector2 originPos;
    int32 timer;
    int32 angleOffset;
    int32 colorSet;
#if defined(P6_GHZCUT_BOOT)
    // Saturn ENTITY-SIZE FIX (#328, measured 2026-07-23): with colors[128]
    // (512 B) sizeof(EntityCutsceneHBH) = ~712 > P6_MAX_ENTITY_SIZE (592), so
    // the Saturn RegisterObject size guard (Object.cpp RETRO_SATURN arm)
    // SILENTLY REFUSED the class -> CutsceneHBH absent from the live class
    // table (probe: GHZCutsceneST entry 37, Decoration 38, no CutsceneHBH) ->
    // the five Heavies never existed in the chain cutscene. The array's ONLY
    // consumer (SetupPalettes -> SetPaletteEntry, .c:195) is NO-OP'd on
    // Saturn (5 resident CRAM blocks from HBHPAL.BIN replace the SW palette
    // swap, task #311), so the snapshot is dead weight: shrink to [1]. The
    // colorSet memcpys copy sizeof(self->colors) = 4 B -- safe.
    color colors[1];
#else
    color colors[128];
#endif
    bool32 useCustomPalettes;
    bool32 noGlobalTimer;
    uint16 aniFrames;
    Animator mainAnimator;
    Animator fxAnimator;
};

// Object Struct
extern ObjectCutsceneHBH *CutsceneHBH;

// Standard Entity Events
void CutsceneHBH_Update(void);
void CutsceneHBH_LateUpdate(void);
void CutsceneHBH_StaticUpdate(void);
void CutsceneHBH_Draw(void);
void CutsceneHBH_Create(void *data);
void CutsceneHBH_StageLoad(void);
#if GAME_INCLUDE_EDITOR
void CutsceneHBH_EditorDraw(void);
void CutsceneHBH_EditorLoad(void);
#endif
void CutsceneHBH_Serialize(void);

// Extra Entity Functions
void CutsceneHBH_SetupColors(void);
void CutsceneHBH_SetupPalettes(void);
void CutsceneHBH_StorePalette(void);
void CutsceneHBH_RestorePalette(void);
void CutsceneHBH_LoadSprites(void);
EntityCutsceneHBH *CutsceneHBH_GetEntity(uint8 charID);

void CutsceneHBH_ShinobiJumpSetup(void);
void CutsceneHBH_ShinobiBounceSetup(void);
void CutsceneHBH_RiderSetup(void);
void CutsceneHBH_KingSetup(void);
void CutsceneHBH_KingTMZ2Setup(void);

void CutsceneHBH_State_GunnerExit(void);
void CutsceneHBH_State_ShinobiBounce(void);
void CutsceneHBH_State_ShinobiJump(void);
void CutsceneHBH_State_MysticExit(void);
void CutsceneHBH_State_RiderMove(void);
void CutsceneHBH_State_RiderExit(void);
void CutsceneHBH_State_KingExit(void);
void CutsceneHBH_State_KingFall(void);
void CutsceneHBH_State_KingTMZ2_Fall(void);
void CutsceneHBH_State_KingTMZ2_Land(void);

#endif //! OBJ_CUTSCENEHBH_H
