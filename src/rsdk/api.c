/* Phase 1.1 — API stub module (Saturn implementation).
 *
 * Every function below is a deliberate link-time stub. Replace in place
 * as each subsystem lands in Phase 2+. */

#include "api.h"
#include "string.h"

int32_t g_rsdk_engine_state = 0;
static bool s_no_save = false;

void rsdk_api_init(void)
{
    g_rsdk_engine_state = 0;
    s_no_save = false;
}

void rsdk_api_set_rich_presence(int32_t id, void *string)
{
    /* TODO Phase Z: no Discord integration on Saturn — permanent no-op. */
    (void)id; (void)string;
}

void rsdk_api_set_no_save(bool flag)
{
    /* TODO Phase 2: gate rsdk_save_user_file on this flag (called by
     * Player.c on death + by TitleSetup on boot). */
    s_no_save = flag;
}

bool rsdk_api_get_no_save(void) { return s_no_save; }

void rsdk_api_clear_preroll_errors(void)
{
    /* TODO Phase 2: preroll is the upstream's pre-stage asset-load
     * pipeline. Saturn loads synchronously so there's no preroll-error
     * state to clear — permanent no-op. */
}

bool rsdk_api_check_dlc(int32_t id)
{
    /* TODO Phase 5+: per docs/COMPREHENSIVE_PLAN.md Plus DLC is out of
     * scope for the Saturn port (`MANIA_USE_PLUS=0`). Always false. */
    (void)id;
    return false;
}

void rsdk_api_unlock_achievement(int32_t id)
{
    /* No achievement back-end. Permanent no-op. */
    (void)id;
}

void rsdk_api_clear_save_status(void)
{
    /* Saturn-side save is synchronous via jo_backup; no status to clear. */
}

void rsdk_api_clear_user_db(uint16_t table_id)
{
    /* TODO Phase 5+: replay/time-attack tables. Permanent no-op for
     * Phase 1.1's Title-scene scope (TitleSetup_StageLoad calls this
     * for replayTableID + taTableID under MANIA_USE_PLUS only). */
    (void)table_id;
}

void rsdk_localization_get_string(rsdk_string_t *out_string, int32_t id)
{
    /* TODO Phase 2: port Localization.c. For Phase 1.1 fill with a
     * single English literal so the Discord rich-presence call site
     * compiles (the rich-presence stub above ignores its argument). */
    (void)id;
    if (out_string) rsdk_set_string(out_string, "Title Screen");
}

bool rsdk_scene_in_editor(void) { return false; }
