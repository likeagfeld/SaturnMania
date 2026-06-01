#ifndef RSDK_AUDIO_H
#define RSDK_AUDIO_H

/* Phase A6 — Audio system, Saturn port of RSDKv5/RSDK/Audio.
 *
 * Per docs/rsdkv5_engine_catalog.md §6 + BIBLE.md Phase A row A6.
 *
 * Authoritative source (cite by §section when touching this code):
 *   §6.1 Constants     (Audio.hpp:12-19)
 *   §6.2 Structs       (Audio.hpp:17-36)
 *   §6.3 PlayStream    (Audio.cpp:274-319)
 *   §6.4 PlaySfx       (Audio.cpp:425-483)
 *
 * Saturn-port deviations from upstream:
 *   * `PlayStream` maps to `jo_audio_play_cd_track(track, track, loop)`
 *     because Saturn BGM is CD-DA, not OGG-from-disk. The caller's
 *     filename is mapped to a CD track index via a name->track table
 *     (the existing zone descriptor in src/main.c::g_zones[].music_track).
 *   * `PlaySfx` maps to `jo_audio_play_sound` on a pre-loaded jo_sound;
 *     SFX are RAM-resident PCM blobs loaded via setup_audio() in main.c.
 *     The RSDK SfxInfo float* sample buffer is opaque on Saturn — the
 *     port stores a jo_sound pointer in the same slot.
 *   * CHANNEL_COUNT = 0x10 matches Saturn SCSP 16 channels exactly (a
 *     1:1 mapping noted in docs/rsdkv5_engine_catalog.md §6.1).
 *   * INK_BLEND/duck mixing is unsupported (Audio.cpp doesn't ship duck
 *     either; per-channel volume is manual).
 *
 * The Phase A6 shim is intentionally THIN: existing main.c continues
 * to call jo_audio_play_cd_track + jo_audio_play_sound for the actual
 * playback. This file exposes the canonical RSDK API names so Phase B
 * per-object ports can write `RSDK.PlaySfx(sfx)` style code. */

#include <stdint.h>
#include <stdbool.h>

#define RSDK_SFX_COUNT      0x100   /* max SFX slots                       */
#define RSDK_CHANNEL_COUNT  0x10    /* 16 — exactly Saturn SCSP capacity  */
#define RSDK_AUDIO_FREQUENCY 22050  /* Saturn-side SFX rate (jo default)   */

/* Loop-point sentinel: stream loops back to this frame index when it
 * reaches the end. 0xFFFFFFFF = no loop (one-shot). */
#define RSDK_NO_LOOP         0xFFFFFFFFu

/* === Public API ===================================================== */

/* Initialise audio subsystem. Idempotent; jo's audio module init is
 * already done by setup_audio() in main.c. */
void rsdk_audio_init(void);

/* §6.3 PlayStream — start a BGM stream on the given channel. The
 * Saturn implementation maps the filename to a CD track via the zone
 * descriptor table. Returns the channel index on success, -1 on fail.
 *
 * On Saturn: `filename` may be a track-name string like "TitleScreen"
 * or a numeric track index encoded as "track%u". For shipped builds
 * the existing main.c knows zone -> track mapping directly; this
 * shim is primarily for per-object Phase B code that wants the RSDK
 * API surface. */
int rsdk_play_stream(const char *filename, int slot, uint32_t start_pos,
                     uint32_t loop_point, bool load_async);

/* §6.4 PlaySfx — play a pre-loaded SFX. `sfx_id` is an index into the
 * Saturn-side SFX table populated at boot by setup_audio in main.c.
 * Returns the channel index (0..CHANNEL_COUNT-1), or -1 if no channel
 * available. */
int rsdk_play_sfx(int sfx_id, uint32_t loop_point, uint8_t priority);

/* Stop calls. */
void rsdk_stop_sfx(int sfx_id);
void rsdk_stop_channel(int channel);
void rsdk_pause_channel(int channel);
void rsdk_resume_channel(int channel);

/* Phase 3.2.b (Task #146) — GetSfx lookup by filename. Mirrors decomp
 * Audio.cpp GetSfx contract: returns the SFX-table slot index for
 * `filename`, or -1 if not registered. Saturn-side: walks the lazily-
 * populated SFX name table. Phase 3.2.b ships the API surface; the
 * menu-asset bootstrap (Phase 3.0-prep + 3.2.l) wires the actual
 * MenuBleep / MenuAccept / SpecialWarp / Event / MenuWoosh / Fail
 * SFX into the table. Until then this returns -1 for menu SFX names
 * which UIWidgets_StageLoad records in ObjectUIWidgets->sfx*. */
int rsdk_get_sfx(const char *filename);

/* Phase 3.2.b — register an SFX file -> slot mapping by name. Called
 * by the per-stage asset bootstrap; pairs with rsdk_audio_register_sfx
 * (slot -> jo_sound pointer) already declared internally in audio.c. */
int rsdk_audio_register_sfx_name(const char *filename, void *jo_sound_ptr);

#endif /* RSDK_AUDIO_H */
