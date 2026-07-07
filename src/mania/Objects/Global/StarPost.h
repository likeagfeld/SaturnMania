/* Phase 2.4k -- StarPost.h
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_Global_StarPost.h`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * Saturn-fit deviations vs decomp (see StarPost.c for rationale):
 *
 *   - EntityStarPost drops poleAnimator + starAnimator (decomp has 3
 *     rsdk_animator_t fields; on Saturn that pushes sizeof to 261 bytes,
 *     past the 256-byte RSDK_ENTITY_STRIDE limit). The pole is static
 *     (always anim 0 frame 0) so no per-entity animator needed. The star
 *     frame is a single index driven externally (starFrameID). Keep only
 *     ballAnimator for the spinning ball per-entity cadence tracking.
 *
 *   - Saturn-only fields added:
 *       ballAnimID  -- current ball animation index (1=idle, 2=spin);
 *                      a single global atlas tracks the frame walker, but
 *                      per-entity ballAnimID lets draw_only switch anim
 *                      when an entity's activation state changes.
 *       starFrameID -- (starAngleY >> 3) & 3, cached here so draw_only
 *                      can read it without re-computing the frame math.
 *
 *   - ObjectStarPost collapses the PLAYER_COUNT (4) arrays to 1-element
 *     arrays for Saturn's single-player build.
 *
 *   Struct layout (Saturn, 32-bit SH-2):
 *       RSDK_ENTITY base   +0..131  (132 bytes)
 *       StateMachine state +132     (4 bytes, fn-pointer)
 *       id                 +136     (int32, 4 bytes)
 *       vsRemove           +140     (bool32, 4 bytes)
 *       ballSpeed          +144     (int32, 4 bytes)
 *       timer              +148     (int32, 4 bytes)
 *       starTimer          +152     (int32, 4 bytes)
 *       bonusStageID       +156     (int32, 4 bytes)
 *       starAngleX         +160     (int32, 4 bytes)
 *       starAngleY         +164     (int32, 4 bytes)
 *       starRadius         +168     (int32, 4 bytes)
 *       ballPos            +172     (Vector2, 8 bytes)
 *       ballAnimator       +180     (rsdk_animator_t, 24 bytes)
 *       hitboxStars        +204     (hitbox_t, 8 bytes)
 *       interactedPlayers  +212     (uint8, 1 byte)
 *       ballAnimID         +213     (uint8, Saturn-only, 1 byte)
 *       starFrameID        +214     (uint8, Saturn-only, 1 byte)
 *       [pad]              +215..255
 *   Total: 215 bytes < 256 RSDK_ENTITY_STRIDE.
 */

#ifndef SATURN_STARPOST_H
#define SATURN_STARPOST_H

#include "../../Game.h"

/* Saturn single-player: PLAYER_COUNT = 1. */
#define STARPOST_PLAYER_COUNT 1

typedef struct ObjectStarPost {
    RSDK_OBJECT
    hitbox_t   hitbox;             /* decomp hitbox {-8,-44,8,20} */
    bool32     hasAchievement;
    Vector2    playerPositions[STARPOST_PLAYER_COUNT];
    uint8      playerDirections[STARPOST_PLAYER_COUNT];
    uint16     postIDs[STARPOST_PLAYER_COUNT];
    uint8      storedMinutes;
    uint8      storedSeconds;
    uint8      storedMS;
    uint8      interactablePlayers;
    uint16     aniFrames;          /* atlas list_id (unused on Saturn; atlas loaded direct) */
    uint16     sfxStarPost;
    uint16     sfxWarp;
} ObjectStarPost;

typedef struct EntityStarPost {
    RSDK_ENTITY
    StateMachine(state);           /* +132 (fn-pointer) */
    int32      id;                 /* +136 (from Serialize: VAR_ENUM) */
    bool32     vsRemove;           /* +140 (from Serialize: VAR_BOOL) */
    int32      ballSpeed;          /* +144 */
    int32      timer;              /* +148 */
    int32      starTimer;          /* +152 */
    int32      bonusStageID;       /* +156 */
    int32      starAngleX;         /* +160 */
    int32      starAngleY;         /* +164 */
    int32      starRadius;         /* +168 */
    Vector2    ballPos;            /* +172 */
    Animator   ballAnimator;       /* +180 (24 bytes; rsdk_animator_t) */
    hitbox_t   hitboxStars;        /* +204 (8 bytes) */
    uint8      interactedPlayers;  /* +212 */
    uint8      ballAnimID;         /* +213 (Saturn: 1=idle, 2=spin) */
    uint8      starFrameID;        /* +214 (Saturn: (starAngleY>>3)&3) */
} EntityStarPost;                  /* 215 bytes < 256 RSDK_ENTITY_STRIDE */

extern ObjectStarPost *StarPost;

void StarPost_Update(void);
void StarPost_LateUpdate(void);
void StarPost_StaticUpdate(void);
void StarPost_Draw(void);
void StarPost_Create(void *data);
void StarPost_StageLoad(void);
void StarPost_Serialize(void);

void StarPost_State_Idle(void);
void StarPost_State_Spinning(void);
void StarPost_CheckCollisions(void);
void StarPost_CheckBonusStageEntry(void);
void StarPost_ResetStarPosts(void);

/* Saturn-side additions -- not in the decomp header. */
void StarPost_load_assets(void);
void StarPost_draw_only(int cam_x, int cam_y);

#endif /* SATURN_STARPOST_H */
