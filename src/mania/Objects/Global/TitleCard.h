#ifndef MANIA_OBJ_TITLECARD_H
#define MANIA_OBJ_TITLECARD_H

/* ---------------------------------------------------------------------
 * Phase 2.4j.1 (Task #156) — TitleCard (act-intro card) port.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_Global_TitleCard.c` + `.h`
 * (Christian Whitehead / Simon Thomley / Hunter Bridges; decomp by
 * Rubberduckycooly & RMGRich).
 *
 * The act-intro slides in colored parallelogram strips + yellow BG
 * curtains, drops the zone-name glyphs + "ZONE" + act number, holds
 * for ~60 ticks while the engine is PAUSED, then slides everything away
 * and returns the engine to REGULAR.
 *
 * Saturn-fit architecture (Bridge-model, Task #155 precedent):
 *   - Registered as an RSDK class via rsdk_object_register_ex("TitleCard")
 *     so TitleCard_Create / _Update land in game.map; the engine slot
 *     callbacks are real ported functions but the GHZ path SUPPRESSES
 *     rsdk_object_draw_all, so TitleCard_Draw is a no-op there. The
 *     actual per-frame drive is the bespoke titlecard_tick() +
 *     titlecard_draw_only() pair called from Game.c (mirroring
 *     Bridge_draw_only).
 *   - A single module-static EntityTitleCard instance (g_titlecard) is
 *     spawned by titlecard_spawn(); there is no Scene.bin placement for
 *     the GHZ TitleCard (the decomp spawns it from the stage's object
 *     list; Saturn spawns one on GHZ entry).
 *   - Engine PAUSE: rsdk_set_engine_state only stores the state field
 *     (scene.c:177) and does NOT gate ticks, so we export
 *     g_titlecard_active. Game.c's mania_ghz_tick_and_draw skips the
 *     Player tick + input while it is true (the decomp's PAUSED freeze).
 *
 * Plus-only fields/branches (MANIA_USE_PLUS) are omitted (treated FALSE
 * per the project's non-Plus parity target).
 * ------------------------------------------------------------------- */

#include <stdint.h>
#include <stdbool.h>

#include "../../../rsdk/string.h"      /* rsdk_string_t */
#include "../../../rsdk/storage.h"     /* rsdk_animator_t */

#ifdef __cplusplus
extern "C" {
#endif

/* Act IDs (decomp Game.h ACT_*). */
#define TC_ACT_1    0
#define TC_ACT_2    1
#define TC_ACT_3    2
#define TC_ACT_NONE 3

/* Local 16.16 fixed Vector2 (decomp Vector2 — Game.h:34). Self-contained
 * so this header doesn't pull the full mania Game.h into the rsdk-layer
 * register call. */
typedef struct { int32_t x, y; } tc_vec2;

/* EntityTitleCard — mirrors decomp struct (TitleCard.h:17-60), Plus
 * fields omitted. State/stateDraw are encoded as small enum tags rather
 * than function pointers (the Saturn tick/draw dispatchers switch on
 * them) so the whole struct is a plain POD usable as an RSDK entity. */
typedef struct {
    int32_t  state;            /* TC_STATE_* */
    int32_t  stateDraw;        /* TC_DRAW_* */
    int32_t  actionTimer;
    int32_t  timer;
    tc_vec2  decorationPos;
    int32_t  stripPos[4];
    tc_vec2  vertMovePos[2];
    tc_vec2  vertTargetPos[2];
    tc_vec2  word1DecorVerts[4];
    tc_vec2  word2DecorVerts[4];
    tc_vec2  zoneDecorVerts[4];
    tc_vec2  stripVertsBlue[4];
    tc_vec2  stripVertsRed[4];
    tc_vec2  stripVertsOrange[4];
    tc_vec2  stripVertsGreen[4];
    tc_vec2  bgLCurtainVerts[4];
    tc_vec2  bgRCurtainVerts[4];
    rsdk_string_t zoneName;
    int32_t  zoneCharPos[4];
    int32_t  zoneCharVel[4];
    int32_t  zoneXPos;
    tc_vec2  charPos[20];
    int32_t  charVel[20];
    int32_t  titleCardWord2;
    int32_t  word1Width;
    int32_t  word2Width;
    int32_t  word1XPos;
    int32_t  word2XPos;
    uint8_t  actID;
    int32_t  actNumScale;
    tc_vec2  actNumPos;
    rsdk_animator_t decorationAnimator;
    rsdk_animator_t nameLetterAnimator;
    rsdk_animator_t zoneLetterAnimator;
    rsdk_animator_t actNumbersAnimator;
} EntityTitleCard;

/* ObjectTitleCard — class-scoped statics (decomp ObjectTitleCard:7-14). */
typedef struct {
    uint16_t aniFrames;        /* spriteAnimationList slot (Saturn: atlas) */
} ObjectTitleCard;

/* State tags (decomp TitleCard_State_*). */
enum {
    TC_STATE_SETUPBG = 0,
    TC_STATE_OPENINGBG,
    TC_STATE_ENTERTITLE,
    TC_STATE_SHOWING,
    TC_STATE_SLIDEAWAY,
    TC_STATE_SUPRESSED,
    TC_STATE_DONE
};

/* Draw-state tags (decomp TitleCard_Draw_*). */
enum {
    TC_DRAW_NONE = 0,
    TC_DRAW_SLIDEIN,
    TC_DRAW_SHOWCARD,
    TC_DRAW_SLIDEAWAY
};

/* === RSDK class callbacks (registered in Game.c) ==================== */
void TitleCard_Update(void);
void TitleCard_LateUpdate(void);
void TitleCard_StaticUpdate(void);
void TitleCard_Draw(void);
void TitleCard_Create(void *data);
void TitleCard_StageLoad(void);

/* === Ported helpers + states (decomp names preserved) ============== */
void TitleCard_SetupVertices(void);
void TitleCard_SetupTitleWords(void);
void TitleCard_HandleWordMovement(void);
void TitleCard_HandleZoneCharMovement(void);
void TitleCard_HandleCamera(void);

void TitleCard_State_SetupBGElements(void);
void TitleCard_State_OpeningBG(void);
void TitleCard_State_EnterTitle(void);
void TitleCard_State_ShowingTitle(void);
void TitleCard_State_SlideAway(void);
void TitleCard_State_Supressed(void);

void TitleCard_Draw_SlideIn(void);
void TitleCard_Draw_ShowTitleCard(void);
void TitleCard_Draw_SlideAway(void);

/* === Saturn-fit drive surface (Bridge-model) ======================= */

/* The single TitleCard instance (Saturn spawns one on GHZ entry). */
extern EntityTitleCard g_titlecard;
extern ObjectTitleCard *TitleCard;

/* TRUE while the card is on-screen and the engine should be PAUSED
 * (Game.c reads this to freeze the Player tick). Cleared when the card
 * reaches ShowingTitle->SlideAway (engine REGULAR) per decomp L497. */
extern volatile int g_titlecard_active;

/* Lazy-load cd/TITLECARD.SP2 + .MET into g_titlecard_atlas (no-op if
 * already loaded). Returns true on success. */
bool titlecard_load_assets(void);

/* Spawn the single card with a zone name + actID, run StageLoad-equiv
 * (load atlas) and Create-equiv (init state). zone_name is a C string
 * (e.g. "GREEN HILL"); actID is TC_ACT_*. */
void titlecard_spawn(const char *zone_name, uint8_t actID);

/* Advance the card one game tick (runs the state machine). No-op when
 * inactive. */
void titlecard_tick(void);

/* Draw the card (runs the draw-state machine). Called from
 * mania_ghz_draw_only at mania_tick body scope (SGL command context). */
void titlecard_draw_only(void);

#ifdef __cplusplus
}
#endif

#endif /* MANIA_OBJ_TITLECARD_H */
