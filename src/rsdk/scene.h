#ifndef RSDK_SCENE_H
#define RSDK_SCENE_H

/* Phase 0.5 — Scene engine stub. The full Phase A9 Scene module
 * implements RSDK.SetScene / LoadScene / CheckSceneFolder /
 * CheckValidScene / SetEngineState plus the SceneInfo->* accessors
 * (state/entity/position/center/size). For now this header declares
 * just the API surface the Phase 0.5 boot stub needs so it links
 * cleanly; Phase 1 fills in the actual implementation by porting
 * the relevant pieces of `tools/_decomp_raw/_RSDKv5_Scene.cpp`
 * (LoadScene at L461-665) and the per-scene init in `SonicMania_Game.c`.
 *
 * Authoritative source paths in upstream:
 *   _RSDKv5_Scene.cpp:461-665   LoadSceneFile
 *   _RSDKv5_Scene.cpp:670-740   InitObjects + InitEntities
 *   SonicMania_Game.c:78-...    LinkGameLogicDLL: RSDK_REGISTER_OBJECT chain
 *
 * Saturn-port deliverables (Phase 1):
 *   1. Resolve "Title" scene name -> Data/Scenes/Title.bin (TitleSetup +
 *      TitleLogo + TitleSonic + TitleBG entities placed by the .bin).
 *   2. Walk the entities through rsdk_create_entity to instantiate them.
 *   3. Surface SceneInfo->state to the per-entity callbacks. */

#include <stdint.h>
#include <stdbool.h>
#include "storage.h"

/* Scene-engine state machine (mirrors SceneInfo->state from upstream). */
enum {
    RSDK_SCENESTATE_LOAD     = 0,
    RSDK_SCENESTATE_REGULAR  = 1,
    RSDK_SCENESTATE_PAUSED   = 2,
    RSDK_SCENESTATE_FROZEN   = 3
};

/* Phase 2.4j.1 — engine-state aliases mirroring decomp ENGINESTATE_*
 * (RetroEngine.hpp). rsdk_set_engine_state(ENGINESTATE_*) writes
 * g_rsdk_scene_info.state; the GHZ tick loop reads g_titlecard_active
 * (exported from TitleCard.c) for the actual per-frame freeze, since the
 * Saturn scene state field does not gate ticks on its own. The numeric
 * values match the decomp enum order (REGULAR=1 .. PAUSED=2 differs from
 * decomp's 0..2 but the Saturn setter only stores it; TitleCard uses the
 * symbol, not the literal). */
enum {
    ENGINESTATE_REGULAR = RSDK_SCENESTATE_REGULAR,
    ENGINESTATE_PAUSED  = RSDK_SCENESTATE_PAUSED,
    ENGINESTATE_FROZEN  = RSDK_SCENESTATE_FROZEN,
    ENGINESTATE_LOAD    = RSDK_SCENESTATE_LOAD
};

/* Phase 1.1 — global SceneInfo mirror (decomp `SceneInfo->state/entity/
 * position/center/size`). Stored alongside drawing.c's g_rsdk_screen
 * since the two are read together by every entity update/draw. */
typedef struct {
    uint8_t state;
    uint8_t in_editor;
    uint16_t entity_slot;        /* slot of the currently-executing entity */
    /* `position` and `size` are mirrored from g_rsdk_screen in drawing.h
     * via rsdk_scene_sync_screen() — the engine carries one of them at
     * a time but per-entity code reads SceneInfo->position. */
} rsdk_scene_info_t;

extern rsdk_scene_info_t g_rsdk_scene_info;

/* === Public API ===================================================== */

/* Init: zero the SceneInfo mirror, clear queued category/name. */
void rsdk_scene_init(void);

/* Queue a scene transition. Stores `category` + `name`; the actual load
 * happens on the next rsdk_load_scene call. */
void rsdk_set_scene(const char *category, const char *name);

/* Phase 1.1 — apply the queued scene transition:
 *   1. Compose `Data/Scenes/<category>/Scene<name>.bin` path.
 *   2. Call rsdk_scene_load (existing storage.c parser).
 *   3. For every entity in scene->entities[]: look up the class by name
 *      (using the class hash stored in scene->classes[i].hash), and call
 *      rsdk_create_entity at the parsed (pos_x, pos_y).
 *   4. Optionally register every tile layer with rsdk_tilelayer_register.
 *   5. Invoke each class's stage_load callback.
 *
 * Returns true if the Scene.bin loaded; per-entity create() failures are
 * silent (the class may not be registered yet — that's fine).
 *
 * Decomp reference: tools/_decomp_raw/_RSDKv5_Scene.cpp:461-665
 * (LoadSceneFile) + :670-740 (InitObjects/InitEntities). */
bool rsdk_load_scene(void);

/* Phase 2.4g.1 (Task #153) — same as rsdk_load_scene but SKIPS the final
 * all-class stage_load pass. Used by the GHZ bring-up, which owns an
 * explicit ordered StageLoad chain (mania_load_ghz_scene -> GHZSetup_StageLoad)
 * and must not re-fire the Title* StageLoads. Set the scene name with
 * rsdk_set_scene("GHZ", "GHZ") (or rsdk_set_scene(NULL, "GHZ")) first. */
bool rsdk_load_scene_no_stage_load(void);

/* Convenience for the boot stub: calls rsdk_set_scene(NULL, name)
 * followed by rsdk_load_scene(). The `name` is the bare scene name
 * (e.g. "Title") — the loader will look in `Data/Scenes/<name>/Scene1.bin`
 * if no category was set OR `Data/Scenes/<category>/Scene<name>.bin`. */
bool rsdk_load_scene_by_name(const char *name);

/* Engine state setter (mirrors upstream SetEngineState). */
void rsdk_set_engine_state(uint8_t state);

/* Read the currently-loaded scene (NULL until rsdk_load_scene runs). */
const rsdk_scene_t *rsdk_current_scene(void);

/* Free the currently-loaded scene; safe to call when no scene is loaded. */
void rsdk_unload_scene(void);

/* Saturn-port helper: returns true if `folder_name` matches the current
 * scene category. Mirrors decomp `CheckSceneFolder` from
 * RSDK_TO_SATURN_API_MAP.md §9. */
bool rsdk_check_scene_folder(const char *folder_name);

#endif /* RSDK_SCENE_H */
