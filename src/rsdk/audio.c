/* Phase A6 — Audio shim. Saturn port of RSDKv5/RSDK/Audio.
 *
 * Thin wrapper exposing the RSDK API surface. The actual playback
 * routing lives in src/main.c (setup_audio + bgm_pump + per-zone
 * jo_audio_play_cd_track + jo_audio_play_sound calls).
 *
 * This module owns a small Saturn-side SFX-table that Phase B per-
 * object ports register their SFX through. The legacy `g_sfx_*` jo_sound
 * globals in src/main.c continue to work for the shipped title/game
 * SFX (ring/jump/break/bounce/stomp/hurt/lose); future per-object ports
 * register through this shim. */

#include "audio.h"

#include <jo/jo.h>
#include <string.h>

/* Saturn-side SFX table — maps RSDK sfx_id to a jo_sound* + active
 * channel. Filled lazily as per-object ports register their SFX. */
typedef struct {
    void  *jo_sound_ptr;    /* jo_sound * (kept opaque to avoid jo type
                             * leak into the public header) */
    bool   in_use;
    int    active_channel;
} rsdk_sfx_slot_t;

static rsdk_sfx_slot_t s_sfx_table[RSDK_SFX_COUNT];

static int s_channel_busy[RSDK_CHANNEL_COUNT];   /* simple ref count */

void rsdk_audio_init(void)
{
    memset(s_sfx_table, 0, sizeof(s_sfx_table));
    memset(s_channel_busy, 0, sizeof(s_channel_busy));
}

/* §6.3 PlayStream — Saturn maps to CD-DA. The filename is expected to
 * be a numeric track index for now (parse leading digits). Per-zone
 * music mapping lives in src/main.c::g_zones[].music_track and is
 * called directly by enter_game(); this shim is for object code that
 * wants the canonical API. */
int rsdk_play_stream(const char *filename, int slot, uint32_t start_pos,
                     uint32_t loop_point, bool load_async)
{
    (void)slot; (void)start_pos; (void)load_async;
    if (!filename) return -1;
#ifdef JO_COMPILE_WITH_AUDIO_SUPPORT
    /* Parse a leading uint from filename. "2" -> track 2, "TitleScreen"
     * -> not handled here, caller must use the zone descriptor path. */
    int track = 0;
    const char *p = filename;
    while (*p >= '0' && *p <= '9') {
        track = track * 10 + (*p - '0');
        ++p;
    }
    if (track <= 0) return -1;
    /* jo_audio_play_cd_track(start, end, loop) — Saturn CD-DA. */
    jo_audio_play_cd_track((unsigned char)track, (unsigned char)track,
                           loop_point != RSDK_NO_LOOP);
    return slot >= 0 ? slot : 0;
#else
    (void)loop_point;
    return -1;
#endif
}

/* §6.4 PlaySfx — find an available channel + play the registered SFX.
 * Saturn-side: caller must have previously registered via the Phase B
 * SFX-load helper (which calls jo_audio_load_pcm to populate the table).
 * The shipped game SFX in main.c (ring/jump/break/etc.) bypass this
 * shim and call jo_audio_play_sound directly; this shim is for new
 * per-object ports. */
int rsdk_play_sfx(int sfx_id, uint32_t loop_point, uint8_t priority)
{
    (void)loop_point; (void)priority;
    if (sfx_id < 0 || sfx_id >= RSDK_SFX_COUNT) return -1;
    if (!s_sfx_table[sfx_id].in_use) return -1;
#ifdef JO_COMPILE_WITH_AUDIO_SUPPORT
    jo_sound *snd = (jo_sound *)s_sfx_table[sfx_id].jo_sound_ptr;
    if (!snd) return -1;
    /* Find a free channel via simple round-robin. */
    static int next_ch = 0;
    int ch = -1;
    for (int i = 0; i < RSDK_CHANNEL_COUNT; ++i) {
        int candidate = (next_ch + i) % RSDK_CHANNEL_COUNT;
        if (!s_channel_busy[candidate]) { ch = candidate; break; }
    }
    if (ch < 0) ch = next_ch;   /* all busy: stomp the head */
    next_ch = (ch + 1) % RSDK_CHANNEL_COUNT;
    jo_audio_play_sound_on_channel(snd, (unsigned char)ch);
    s_channel_busy[ch] = 1;
    s_sfx_table[sfx_id].active_channel = ch;
    return ch;
#else
    return -1;
#endif
}

void rsdk_stop_sfx(int sfx_id)
{
    if (sfx_id < 0 || sfx_id >= RSDK_SFX_COUNT) return;
    if (!s_sfx_table[sfx_id].in_use) return;
    /* jo's audio module doesn't expose a per-channel stop; just clear
     * our busy bit so the slot rotates out. Future Phase Z: drive
     * the SCSP channel-stop register directly. */
    int ch = s_sfx_table[sfx_id].active_channel;
    if (ch >= 0 && ch < RSDK_CHANNEL_COUNT) s_channel_busy[ch] = 0;
    s_sfx_table[sfx_id].active_channel = -1;
}

void rsdk_stop_channel(int channel)
{
    if (channel < 0 || channel >= RSDK_CHANNEL_COUNT) return;
    s_channel_busy[channel] = 0;
}

void rsdk_pause_channel(int channel)
{
    if (channel < 0 || channel >= RSDK_CHANNEL_COUNT) return;
    /* jo's audio module doesn't expose per-channel pause; this is a
     * Phase Z follow-up. */
}

void rsdk_resume_channel(int channel)
{
    if (channel < 0 || channel >= RSDK_CHANNEL_COUNT) return;
    /* See rsdk_pause_channel. */
}

/* Helper for Phase B SFX-load code: register a pre-loaded jo_sound at
 * a given slot. Returns true on success. */
bool rsdk_audio_register_sfx(int sfx_id, void *jo_sound_ptr)
{
    if (sfx_id < 0 || sfx_id >= RSDK_SFX_COUNT) return false;
    s_sfx_table[sfx_id].jo_sound_ptr   = jo_sound_ptr;
    s_sfx_table[sfx_id].in_use         = jo_sound_ptr != NULL;
    s_sfx_table[sfx_id].active_channel = -1;
    return true;
}

/* Phase 3.2.b — name-to-slot mapping. The decomp's RSDK.GetSfx looks up
 * by full filename ("Global/MenuBleep.wav") and returns the SFX-table
 * slot. Saturn-side we keep a parallel small name table populated by
 * rsdk_audio_register_sfx_name (called by the Phase 3.0-prep menu
 * asset bootstrap). For Phase 3.2.b foundation the table starts empty;
 * UIWidgets_StageLoad calls rsdk_get_sfx for each menu SFX and gets
 * -1 back, which it stores in UIWidgets->sfxBleep etc. The Phase 3.2.l
 * menu-integration step registers the actual SFX so playback works. */

#define RSDK_SFX_NAME_MAX  32
#define RSDK_SFX_NAME_CAP  64   /* room for ~all menu SFX + per-zone SFX */

typedef struct {
    char  name[RSDK_SFX_NAME_MAX];
    int   slot;
} rsdk_sfx_name_entry_t;

static rsdk_sfx_name_entry_t s_sfx_names[RSDK_SFX_NAME_CAP];
static int                    s_sfx_name_count = 0;

static bool _name_eq(const char *a, const char *b)
{
    if (!a || !b) return false;
    for (int i = 0; i < RSDK_SFX_NAME_MAX; ++i) {
        if (a[i] != b[i]) return false;
        if (a[i] == 0) return true;
    }
    return false;
}

int rsdk_get_sfx(const char *filename)
{
    if (!filename) return -1;
    for (int i = 0; i < s_sfx_name_count; ++i) {
        if (_name_eq(s_sfx_names[i].name, filename))
            return s_sfx_names[i].slot;
    }
    return -1;
}

int rsdk_audio_register_sfx_name(const char *filename, void *jo_sound_ptr)
{
    if (!filename || s_sfx_name_count >= RSDK_SFX_NAME_CAP) return -1;
    /* Reuse existing entry if name already registered. */
    int existing = rsdk_get_sfx(filename);
    int slot = existing;
    if (slot < 0) {
        /* Pick next free SFX-table slot. */
        for (int i = 0; i < RSDK_SFX_COUNT; ++i) {
            if (!s_sfx_table[i].in_use) { slot = i; break; }
        }
        if (slot < 0) return -1;
        /* Record name -> slot. */
        rsdk_sfx_name_entry_t *e = &s_sfx_names[s_sfx_name_count++];
        for (int i = 0; i < RSDK_SFX_NAME_MAX - 1; ++i) {
            e->name[i] = filename[i];
            if (filename[i] == 0) break;
        }
        e->name[RSDK_SFX_NAME_MAX - 1] = 0;
        e->slot = slot;
    }
    rsdk_audio_register_sfx(slot, jo_sound_ptr);
    return slot;
}
