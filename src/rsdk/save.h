#ifndef RSDK_SAVE_H
#define RSDK_SAVE_H

/* Phase A8 — Save system, Saturn port of RSDKv5/RSDK/User/Core/UserStorage.
 *
 * Per docs/rsdkv5_engine_catalog.md §10 + BIBLE.md Phase A row A8.
 *
 * Authoritative source (cite by §section when touching this code):
 *   §10.1 LoadUserFile / SaveUserFile (UserStorage.cpp:1290-1350)
 *   §10.2 UserDB binary format        (UserStorage.cpp:195-285)
 *   §10.3 SaveGameProgress notes
 *
 * Saturn-port deviations from upstream:
 *   * No filesystem — backup-RAM is the persistence target (64 KB
 *     battery-backed). Each "file" is a named backup-RAM record (max
 *     11 ASCII chars per Saturn BIOS limit).
 *   * Async-callback path (RSDK calls callbacks on completion) is
 *     stubbed synchronous: jo's backup API is blocking. Callers can
 *     pretend to dispatch a callback after the call returns.
 *   * UserDB row/column table format is NOT implemented at the engine
 *     layer; if a per-object port needs UserDB semantics it builds
 *     them on top of these primitives.
 *
 * The legacy src/save.{h,c} module ("SMSV.0" record, 16-byte
 * sms_save_t struct, magic+version-guarded) continues to work
 * unchanged for the Phase 3 act-clear save -- this A8 module exposes
 * a SECOND, more general API for per-object Mania-object ports that
 * call RSDK.LoadUserFile / RSDK.SaveUserFile. */

#include <stdint.h>
#include <stdbool.h>

/* Saturn backup-RAM filename limit; Saturn BIOS allows max 11 ASCII chars
 * plus null. Truncation policy: names longer than this are SILENTLY
 * shortened (warning at JO_DEBUG). */
#define RSDK_SAVE_NAME_MAX 11

/* Initialise the save subsystem. Lazy-mounts the backup device on first
 * call; subsequent calls are idempotent. Returns true if the device is
 * available. */
bool rsdk_save_init(void);

/* Check whether a named user-file exists. */
bool rsdk_save_user_file_exists(const char *name);

/* Read up to `size` bytes from the named user-file into `out`. Returns
 * the number of bytes actually read (0 on failure / missing file). */
uint32_t rsdk_load_user_file(const char *name, void *out, uint32_t size);

/* Write `size` bytes from `data` to the named user-file. Returns true
 * on success. */
bool rsdk_save_user_file(const char *name, const void *data, uint32_t size);

/* Delete a user-file. No-op if absent. */
void rsdk_save_user_file_delete(const char *name);

#endif /* RSDK_SAVE_H */
