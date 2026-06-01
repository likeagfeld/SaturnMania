#ifndef RSDK_API_H
#define RSDK_API_H

/* Phase 1.1 — API stub module (link-time stubs for not-yet-ported APIs).
 *
 * Decomp call sites that reference these APIs must link against
 * something; the rest of the engine will land in Phase 2+ (gameplay)
 * or Phase Z (Saturn-incompatible features). This module catalogs every
 * API surface the Title-scene port needs but doesn't yet implement
 * meaningfully — providing zero/false/no-op returns so the binary links.
 *
 * Each stub is annotated with its decomp source + phase-to-implement
 * TODO. When the actual work lands the stub is replaced in place.
 *
 * Source contracts (read but not reproduced):
 *   docs/RSDK_TO_SATURN_API_MAP.md §11-12         API_* family
 *   tools/_decomp_raw/SonicMania_Game.c           DLLExport contract
 *   tools/_decomp_raw/_RSDKv5_RetroEngine.hpp     Engine state enums
 *
 * Phase 1.1 priority: link-time. The 8 stubs below cover the call sites
 * in TitleSetup.c + TitleLogo.c + TitleSonic.c + TitleBG.c that the
 * Phase 1.2 per-class ports will hit. */

#include <stdint.h>
#include <stdbool.h>

/* === Engine state (referenced by SceneInfo->state callbacks) ======== */

extern int32_t g_rsdk_engine_state;

void rsdk_api_init(void);

/* === API_ family (decomp `API_*` and `API.*` calls) ================ */

/* SonicMania_Game.c declares globals->presenceID; API_SetRichPresence is
 * called by every per-zone Setup_StageLoad. Phase 1.2: TitleSetup calls
 * it with PRESENCE_TITLE. Saturn-side: no Discord, no-op. */
void rsdk_api_set_rich_presence(int32_t id, void *string);

/* TitleSetup_StageLoad sets nosave=false after init. Saturn-side: store
 * flag, plumb to rsdk_save_user_file as a write-disable check (Phase 2). */
void rsdk_api_set_no_save(bool flag);
bool rsdk_api_get_no_save(void);

/* Player.c calls API_ClearPrerollErrors on death. Saturn: no preroll. */
void rsdk_api_clear_preroll_errors(void);

/* MANIA_USE_PLUS-only DLC + achievement no-ops. */
bool rsdk_api_check_dlc(int32_t id);
void rsdk_api_unlock_achievement(int32_t id);
void rsdk_api_clear_save_status(void);
void rsdk_api_clear_user_db(uint16_t table_id);

/* === Localization placeholder ====================================== */

/* TitleSetup_StageLoad calls Localization_GetString(&str, STR_RPC_TITLE);
 * the full Localization module is Phase 2+. Provide a stub that fills
 * `out_string` with a hard-coded English literal. */
#include "string.h"
void rsdk_localization_get_string(rsdk_string_t *out_string, int32_t id);

/* === SceneInfo helpers needed by every entity Create =============== */

/* Mirrors upstream `SceneInfo->inEditor` — Saturn never sets it, always
 * false. Provides the API surface for `if (!SceneInfo->inEditor)` guards. */
bool rsdk_scene_in_editor(void);

#endif /* RSDK_API_H */
