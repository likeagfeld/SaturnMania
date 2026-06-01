#ifndef MANIA_OBJECTS_GLOBAL_BOUNDSMARKER_H
#define MANIA_OBJECTS_GLOBAL_BOUNDSMARKER_H

/* Phase 2.4g.2 (Task #153) — BoundsMarker port.
 *
 * Mechanical port of
 * `tools/_decomp_raw/SonicMania_Objects_Global_BoundsMarker.c` +
 * `..._BoundsMarker.h` (Christian Whitehead / Simon Thomley /
 * Hunter Bridges; decomp by Rubberduckycooly & RMGRich).
 *
 * Second GHZ entity ported onto the RSDK entity engine (after
 * InvisibleBlock, 2.4g.1) per memory/ghz-pivot-to-rsdk-engine.md. Struct
 * field order mirrors the decomp BoundsMarker.h:14-26 1:1 so future ports
 * compile against the same layout.
 *
 * BoundsMarker writes the Zone camera/player/death bounds (see Zone.h
 * subset) when the player crosses the marker's X. Its 22 GHZ Act 1
 * instances spawn into RSDK slots through the same scene-spawn pipeline
 * 2.4g.1 added for InvisibleBlock. */

#include "../../Game.h"
#include "Player.h"

/* decomp BoundsMarker.h:6-11 */
typedef enum {
    BOUNDSMARKER_ANY_Y,
    BOUNDSMARKER_ABOVE_Y,
    BOUNDSMARKER_BELOW_Y,
    BOUNDSMARKER_BELOW_Y_ANY,
} BoundsMarkerTypes;

/* Object Class (decomp ObjectBoundsMarker, BoundsMarker.h:13-17). */
typedef struct ObjectBoundsMarker {
    RSDK_OBJECT
    uint16 aniFrames;
} ObjectBoundsMarker;

/* Entity Class (decomp EntityBoundsMarker, BoundsMarker.h:19-26). */
typedef struct EntityBoundsMarker {
    RSDK_ENTITY
    uint8  type;
    int32  width;
    bool32 vsDisable;
    int32  offset;
} EntityBoundsMarker;

extern ObjectBoundsMarker *BoundsMarker;

/* Standard RSDK class callbacks (per decomp). */
void BoundsMarker_Update(void);
void BoundsMarker_LateUpdate(void);
void BoundsMarker_StaticUpdate(void);
void BoundsMarker_Draw(void);
void BoundsMarker_Create(void *data);
void BoundsMarker_StageLoad(void);

/* Extra Entity Functions (decomp BoundsMarker.c:51-104). */
void BoundsMarker_ApplyBounds(player_t *player, EntityBoundsMarker *marker, bool32 setPos);

#endif /* MANIA_OBJECTS_GLOBAL_BOUNDSMARKER_H */
