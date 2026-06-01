/*
 * save.c - Phase 3 backup-RAM save module.
 *
 * Wraps jo_engine's backup module against the Saturn's internal 64 KB
 * battery-backed memory device (JoInternalMemoryBackup). The save struct
 * is sms_save_t (see save.h); a single fixed-name file ("SMSV.0") holds
 * the latest snapshot. Future multi-slot support (NEW GAME / CONTINUE
 * over three slots) is a Phase 5 menu task; for now Phase 3 ships with
 * one slot.
 *
 * All four entry points return int (0 = false, 1 = true) so the .h does
 * not need to pull in stdbool.h.
 */
#include <jo/jo.h>
#include <jo/backup.h>      /* jo's main jo.h does not pull this in */
#include "save.h"

#define DEV  JoInternalMemoryBackup

static int g_mounted;
static int g_mount_attempted;

/* Lazy mount: jo_backup_mount() at boot was corrupting the FG cell bank
 * (bisect-verified during Phase 3) -- presumably the SMPC backup-driver
 * touchpoint disrupts something else jo just initialised. Defer the mount
 * to the first save_* call after boot, by which time FG/SKY/sprites are
 * all set up and the graphics pipeline is stable. */
static void _ensure_mounted(void)
{
    if (g_mount_attempted) return;
    g_mount_attempted = 1;
    g_mounted = jo_backup_mount(DEV) ? 1 : 0;
}

int sms_save_init(void)
{
    /* No-op now; real mount happens lazily on first read/write/exists. */
    return 1;
}

int sms_save_exists(void)
{
    _ensure_mounted();
    if (!g_mounted) return 0;
    return jo_backup_file_exists(DEV, SMS_SAVE_FNAME) ? 1 : 0;
}

int sms_save_write(const sms_save_t *s)
{
    _ensure_mounted();
    if (!g_mounted) return 0;
    /* jo's helper signature is non-const for fname/comment/contents even
     * though the data is only read; cast away const at the boundary. */
    return jo_backup_save_file_contents(
        DEV,
        (char *)SMS_SAVE_FNAME,
        (char *)SMS_SAVE_COMMENT,
        (void *)s,
        sizeof(sms_save_t)) ? 1 : 0;
}

int sms_save_read(sms_save_t *out)
{
    unsigned int len = 0;
    void *data;
    sms_save_t  *s;

    _ensure_mounted();
    if (!g_mounted) return 0;
    if (!jo_backup_file_exists(DEV, SMS_SAVE_FNAME)) return 0;

    data = jo_backup_load_file_contents(DEV, (char *)SMS_SAVE_FNAME, &len);
    if (data == NULL || len < sizeof(sms_save_t)) {
        if (data != NULL) jo_free(data);
        return 0;
    }
    s = (sms_save_t *)data;
    if (s->magic != SMS_SAVE_MAGIC || s->version != SMS_SAVE_VERSION) {
        jo_free(data);
        return 0;
    }
    *out = *s;
    jo_free(data);
    return 1;
}
