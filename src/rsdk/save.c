/* Phase A8 — Save system, Saturn port of RSDKv5/RSDK/User/Core/UserStorage.
 *
 * Thin wrapper over jo_engine's backup API (jo_backup_save/load/exists/
 * delete). The legacy src/save.{h,c} module continues to handle the
 * Phase 3 act-clear SMSV slot; this file owns the GENERIC user-file
 * API that Phase B per-object ports call via the RSDK.LoadUserFile /
 * RSDK.SaveUserFile entries.
 *
 * Mount target: JoInternalMemoryBackup (the Saturn's on-board 64 KB
 * battery-backed RAM at 0x00180000). Cartridge backup is supported by
 * jo but adds complexity we don't need yet.
 *
 * Comment field: every file gets the comment "RSDK SAVE" so the Saturn
 * BIOS save manager shows a recognisable label. */

#include "save.h"

#include <jo/jo.h>
#include <string.h>

#define RSDK_SAVE_DEVICE   JoInternalMemoryBackup
#define RSDK_SAVE_COMMENT  "RSDK SAVE"

static bool s_mounted = false;

bool rsdk_save_init(void)
{
    if (s_mounted) return true;
    s_mounted = jo_backup_mount(RSDK_SAVE_DEVICE);
    return s_mounted;
}

/* Truncate the caller's name to fit Saturn BIOS's 11-char limit. */
static const char *_short_name(const char *name, char *scratch)
{
    if (!name) return "";
    int n = 0;
    while (name[n] && n < RSDK_SAVE_NAME_MAX) {
        scratch[n] = name[n];
        ++n;
    }
    scratch[n] = '\0';
    return scratch;
}

bool rsdk_save_user_file_exists(const char *name)
{
    if (!rsdk_save_init() || !name) return false;
    char scratch[RSDK_SAVE_NAME_MAX + 1];
    const char *trim = _short_name(name, scratch);
    return jo_backup_file_exists(RSDK_SAVE_DEVICE, (char *)trim) ? true : false;
}

uint32_t rsdk_load_user_file(const char *name, void *out, uint32_t size)
{
    if (!rsdk_save_init() || !name || !out || size == 0) return 0;
    char scratch[RSDK_SAVE_NAME_MAX + 1];
    const char *trim = _short_name(name, scratch);
    unsigned int len = 0;
    void *data = jo_backup_load_file_contents(RSDK_SAVE_DEVICE,
                                              (char *)trim, &len);
    if (!data || len == 0) return 0;
    uint32_t copy = (len < size) ? len : size;
    memcpy(out, data, copy);
    jo_free(data);
    return copy;
}

bool rsdk_save_user_file(const char *name, const void *data, uint32_t size)
{
    if (!rsdk_save_init() || !name || !data || size == 0) return false;
    char scratch[RSDK_SAVE_NAME_MAX + 1];
    const char *trim = _short_name(name, scratch);
    /* jo_backup_save_file_contents wraps jo_backup_save with a fresh
     * jo_backup struct; the static helper in backup.h is inline so we
     * call it directly. Signature (verified jo/backup.h:145-146):
     *   bool jo_backup_save_file_contents(device, fname, comment,
     *                                     contents, content_size). */
    return jo_backup_save_file_contents(RSDK_SAVE_DEVICE,
                                        (char *)trim,
                                        RSDK_SAVE_COMMENT,
                                        (void *)data,
                                        size) ? true : false;
}

void rsdk_save_user_file_delete(const char *name)
{
    if (!rsdk_save_init() || !name) return;
    char scratch[RSDK_SAVE_NAME_MAX + 1];
    const char *trim = _short_name(name, scratch);
    jo_backup_delete_file(RSDK_SAVE_DEVICE, (char *)trim);
}
