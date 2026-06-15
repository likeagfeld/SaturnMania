/* FR-1 (Task #177) -- Sonic player animation atlas with MRU residency.
 *
 * See player_atlas.h for the residency model and source citations. This
 * file is the loader/walker/upload implementation.
 *
 * On-disk formats consumed (tools/build_entity_atlas.py):
 *   cd/SONIC.MET   "MET1", u16BE anim_count, u16BE frame_total,
 *                  per anim:  u16BE frame_count, speed, loop, first,
 *                             char[24] name                (34 B)
 *                  per frame: u8 anim_id, u8 frame_id, i16BE px, i16BE py,
 *                             u16BE duration, u16BE unicode (10 B)
 *   cd/SONICnn.SP2 "SPR2", u16BE frame_count, u16BE reserved,
 *                  per frame: u16BE w, u16BE h, w*h u16BE BGR1555 pixels
 *                  -- ONE anim's frames per slice (nn = MET anim index).
 */

#include "player_atlas.h"
#include "storage.h"
#include "spc.h"

#include <jo/jo.h>
#include <string.h>

/* === BSS global + .data anchor (defeat LTO BSS-name collapse so the gate
 *     can locate the atlas by symbol; mirrors entity_atlas.c). ========= */
__attribute__((used)) player_atlas_t g_player_atlas;
__attribute__((used)) player_atlas_t * const g_player_atlas_anchor =
    &g_player_atlas;

/* === Resident compressed pack + decode scratch (Task #192 player-only) == *
 * Placed in the FREE LWRAM tail ABOVE the live #188 FG.CEL region. FG.CEL
 * occupies [0x002D0000, 0x002E8000) (scene_ghz.c GHZ_FG_CEL_LWRAM_ADDR/_SIZE)
 * and is retained by jo's NBG1 internal pointers across gameplay -> it must
 * NOT be overwritten (Gate V-188). The earlier #192 layout put the packs ON
 * TOP of FG.CEL (0x002D0000..) -> foreground corruption / visual crash after
 * the title card. Fix: only the player pack stays resident, relocated wholly
 * into the free tail [0x002E8000, 0x00300000). Entities reverted to CD
 * streaming (GHZENT.SPC retired), so no entity pack lives here anymore.
 *
 * The player no longer CD-reads per anim: SONIC.SPC is loaded ONCE into
 * SPC_PLAYER_ADDR at scene start (player_atlas_pack_load), and each anim
 * slice is puff-inflated into PLAYER_DECODE_ADDR RAM->RAM on the anim change
 * that needs it. This kills the frequent player-anim CD seeks that fought the
 * GHZ CD-DA music track (Game.c jo_audio_play_cd_track(2,2,true)); the
 * occasional entity SP2 stream remains but is far rarer.
 *
 * Region map (free tail 0x002E8000..0x002FFFFF, 96 KB):
 *   0x002E8000 +48 KB  SONIC.SPC  resident pack       (36781 B)
 *   0x002F4000 +40 KB  player one-slice decode buffer (max 35480 B)
 *   0x002FE000  +8 KB  slack (unused)
 * No other LWRAM region is touched (FR-2 entity pool 0x00260000, scene arena,
 * colwindow GCO3, FG.TMP, GHZ1SURF mask, FG.CEL all unchanged). */
#define SPC_PLAYER_ADDR    ((uint8_t *)0x002E8000)       /* SONIC.SPC        */
#define SPC_PLAYER_CAP     0xC000u                       /* 48 KB            */
#define PLAYER_DECODE_ADDR ((unsigned char *)0x002F4000) /* one slice        */
#define PLAYER_DECODE_CAP  0xA000u                       /* 40 KB            */

static unsigned char *const s_pack   = SPC_PLAYER_ADDR;
static unsigned char *const s_decode = PLAYER_DECODE_ADDR;
static bool s_pack_ready   = false;
static int  s_decoded_anim = -1;   /* which anim's slice is in s_decode      */

/* Load SONIC.SPC into the resident LWRAM pack region ONCE (scene-load time,
 * CD read permitted here). Idempotent: re-running re-reads the pack. */
bool player_atlas_pack_load(void)
{
    int n = rsdk_storage_load_to_lwram("SONIC.SPC", s_pack, SPC_PLAYER_CAP);
    s_pack_ready = (n > 0) && spc_valid(s_pack);
    s_decoded_anim = -1;
    return s_pack_ready;
}

/* jo sprite-stack base the player owns (top of stack after entity loads). */
static int s_player_base = -1;

/* === BE byte readers (SH-2 big-endian; match struct.pack(">H")). ===== */
static inline uint16_t _rd_u16(const unsigned char *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}
static inline int16_t _rd_i16(const unsigned char *p)
{
    return (int16_t)_rd_u16(p);
}

/* Build the resident-pack key "SONICnn" (nn = two-digit anim index). 7 chars
 * + NUL; matches the stem key emitted by tools/build_sprite_packs.py. */
static void _slice_stem(char *dst, int anim_id)
{
    dst[0] = 'S'; dst[1] = 'O'; dst[2] = 'N'; dst[3] = 'I'; dst[4] = 'C';
    dst[5] = (char)('0' + (anim_id / 10) % 10);
    dst[6] = (char)('0' + anim_id % 10);
    dst[7] = '\0';
}

/* === player_atlas_load -- read cd/SONIC.MET metadata (no pixels) ===== */
bool player_atlas_load(player_atlas_t *atlas)
{
    if (!atlas) return false;
    memset(atlas, 0, sizeof(*atlas));
    for (int i = 0; i < PLAYER_ATLAS_MAX_ANIM_FRAMES; ++i)
        atlas->sprite_id[i] = -1;

    /* Reset decode state + base on (re)load. The resident pack persists
     * across player_atlas_load (loaded once via player_atlas_pack_load). */
    s_decoded_anim = -1;
    s_player_base  = -1;

    int met_len = 0;
    unsigned char *met = (unsigned char *)jo_fs_read_file("SONIC.MET", &met_len);
    if (!met || met_len < 8) {
        if (met) jo_free(met);
        return false;
    }
    if (met[0] != 'M' || met[1] != 'E' || met[2] != 'T' || met[3] != '1') {
        jo_free(met);
        return false;
    }
    uint16_t ac = _rd_u16(met + 4);
    uint16_t mc = _rd_u16(met + 6);
    if (ac > PLAYER_ATLAS_MAX_ANIMS)  ac = PLAYER_ATLAS_MAX_ANIMS;
    if (mc > PLAYER_ATLAS_MAX_FRAMES) mc = PLAYER_ATLAS_MAX_FRAMES;
    atlas->anim_count  = (uint8_t)ac;
    atlas->frame_total = mc;

    const int ANIM_REC  = 2 + 2 + 2 + 2 + 24;   /* 34 B */
    const int FRAME_REC = 1 + 1 + 2 + 2 + 2 + 2; /* 10 B */
    unsigned char *p   = met + 8;
    unsigned char *end = met + met_len;

    for (int i = 0; i < ac; ++i) {
        if (p + ANIM_REC > end) break;
        atlas->anims[i].frame_count = _rd_u16(p);
        atlas->anims[i].speed       = _rd_u16(p + 2);
        atlas->anims[i].loop_index  = _rd_u16(p + 4);
        atlas->anims[i].first       = _rd_u16(p + 6);
        p += ANIM_REC;
    }
    for (int i = 0; i < mc; ++i) {
        if (p + FRAME_REC > end) break;
        /* p[0]=anim_id, p[1]=frame_id (unused at runtime). */
        atlas->pivot_x[i]  = _rd_i16(p + 2);
        atlas->pivot_y[i]  = _rd_i16(p + 4);
        atlas->duration[i] = _rd_u16(p + 6);
        /* width/height come from the slice SP2 at upload time. */
        p += FRAME_REC;
    }
    jo_free(met);

    atlas->ready        = 1;
    atlas->current_anim = 0xFFFF;   /* force first play to upload */
    return true;
}

void player_atlas_set_base(int base_id)
{
    s_player_base = base_id;
}

int player_atlas_base(void)
{
    return s_player_base;
}

int player_atlas_top(void)
{
    if (s_player_base < 0) return -1;
    /* No anim uploaded yet (first play hasn't run, or load failed): the
     * player owns zero VDP1 slots, so the dynamic block starts at base. */
    if (!g_player_atlas.ready || g_player_atlas.current_anim == 0xFFFF)
        return s_player_base;
    int fc = g_player_atlas.anims[g_player_atlas.current_anim].frame_count;
    if (fc < 0) fc = 0;
    if (fc > PLAYER_ATLAS_MAX_ANIM_FRAMES) fc = PLAYER_ATLAS_MAX_ANIM_FRAMES;
    return s_player_base + fc;
}

/* Return the decode buffer iff it already holds `anim_id`'s slice, else NULL.
 * Re-decoding an already-present anim is cheap (RAM->RAM puff), but skipping
 * it avoids a redundant inflate when the same anim is requested twice. */
static unsigned char *_cache_find(int anim_id)
{
    if (s_decoded_anim == anim_id) return s_decode;
    return NULL;
}

/* Inflate SONICnn's SPR2 slice from the resident SONIC.SPC pack into the
 * single decode buffer (RAM->RAM via puff -- NO CD access). Returns the
 * decode-buffer pointer (and the raw byte length via *out_len) or NULL on
 * failure. This is the per-anim-change path that previously CD-read
 * SONICnn.SP2 and contended with the GHZ CD-DA music track. */
static unsigned char *_cache_load(int anim_id, int *out_len)
{
    if (!s_pack_ready) return NULL;
    char stem[8];
    _slice_stem(stem, anim_id);
    uint32_t raw = 0;
    if (!spc_inflate(s_pack, stem, s_decode, PLAYER_DECODE_CAP, &raw)) {
        s_decoded_anim = -1;
        return NULL;
    }
    if (raw < 8 ||
        s_decode[0] != 'S' || s_decode[1] != 'P' ||
        s_decode[2] != 'R' || s_decode[3] != '2') {
        s_decoded_anim = -1;
        return NULL;
    }
    s_decoded_anim = anim_id;
    if (out_len) *out_len = (int)raw;
    return s_decode;
}

/* === player_atlas_play -- residency switch + VDP1 upload ============= */
void player_atlas_play(player_atlas_t *atlas, int anim_id, int speed_override)
{
    if (!atlas || !atlas->ready) return;
    if (anim_id < 0 || anim_id >= atlas->anim_count) return;
    if (atlas->anims[anim_id].frame_count == 0) return;

    int speed = (speed_override >= 0)
                    ? speed_override
                    : (int)atlas->anims[anim_id].speed;

    /* Already resident with the same anim + speed: nothing to upload. */
    if (atlas->current_anim == (uint16_t)anim_id &&
        atlas->animator.speed == (int16_t)speed) {
        return;
    }

    int first = atlas->anims[anim_id].first;
    int fc    = atlas->anims[anim_id].frame_count;
    if (fc > PLAYER_ATLAS_MAX_ANIM_FRAMES) fc = PLAYER_ATLAS_MAX_ANIM_FRAMES;

    /* If only the speed changed (same anim already resident), keep the VDP1
     * frames and just retarget the animator speed -- no re-upload. */
    bool same_anim = (atlas->current_anim == (uint16_t)anim_id);

    if (!same_anim) {
        /* Acquire the slice's pixel bytes (pool hit or CD read). */
        unsigned char *slice = _cache_find(anim_id);
        if (!slice) {
            int slen = 0;
            slice = _cache_load(anim_id, &slen);
        }
        if (!slice) return;   /* CD/pool failure -- keep prior frames */

        /* Free the previously resident anim's VDP1 sprites (LIFO rewind),
         * then upload this anim's frames. The player owns the stack top:
         * free_from(base) when base > __jo_sprite_id is a safe no-op
         * (sprites.c:298). */
        if (s_player_base >= 0)
            jo_sprite_free_from(s_player_base);
        for (int i = 0; i < PLAYER_ATLAS_MAX_ANIM_FRAMES; ++i)
            atlas->sprite_id[i] = -1;

        uint16_t sfc = _rd_u16(slice + 4);
        unsigned char *q   = slice + 8;
        /* The slice's frame count must match the MET anim's frame_count. */
        int n = (sfc < fc) ? sfc : fc;
        jo_img img;
        for (int i = 0; i < n; ++i) {
            uint16_t w = _rd_u16(q);
            uint16_t h = _rd_u16(q + 2);
            q += 4;
            size_t bytes = (size_t)w * (size_t)h * 2u;
            img.data   = (jo_color *)q;
            img.width  = (unsigned short)w;
            img.height = (unsigned short)h;
            int sid = jo_sprite_add(&img);
            if (sid < 0) break;          /* VDP1 exhausted */
            atlas->sprite_id[i] = (int16_t)sid;
            atlas->width[first + i]  = w;
            atlas->height[first + i] = h;
            q += bytes;
        }
    }

    /* Rebuild the anim-local scratch_frames the walker reads durations from
     * (decomp Animation.cpp:174 reads frames[frame_id].duration). */
    for (int i = 0; i < fc; ++i) {
        atlas->scratch_frames[i].duration = atlas->duration[first + i];
        atlas->scratch_frames[i].width    = atlas->width[first + i];
        atlas->scratch_frames[i].height   = atlas->height[first + i];
        atlas->scratch_frames[i].pivot_x  = atlas->pivot_x[first + i];
        atlas->scratch_frames[i].pivot_y  = atlas->pivot_y[first + i];
    }

    atlas->current_anim        = (uint16_t)anim_id;
    atlas->current_atlas_frame = (uint16_t)first;

    atlas->animator.frames            = &atlas->scratch_frames[0];
    atlas->animator.frame_id          = 0;
    atlas->animator.animation_id      = (int16_t)anim_id;
    atlas->animator.prev_animation_id = (int16_t)anim_id;
    atlas->animator.speed             = (int16_t)speed;
    atlas->animator.timer             = 0;
    atlas->animator.frame_count       = (int16_t)fc;
    atlas->animator.loop_index        = (uint8_t)atlas->anims[anim_id].loop_index;
    atlas->animator.frame_duration    = (int16_t)atlas->scratch_frames[0].duration;
    atlas->animator.rotation_style    = 0;
    atlas->animator.list_id           = 0;
}

/* === player_atlas_tick -- ProcessAnimation per Animation.cpp:150-177 = */
void player_atlas_tick(player_atlas_t *atlas)
{
    if (!atlas || !atlas->ready) return;
    if (atlas->animator.frame_count == 0) return;

    rsdk_process_animation(&atlas->animator);

    int fid = atlas->animator.frame_id;
    if (fid < 0) fid = 0;
    if (fid >= atlas->animator.frame_count)
        fid = atlas->animator.frame_count - 1;
    int first = atlas->anims[atlas->current_anim].first;
    atlas->current_atlas_frame = (uint16_t)(first + fid);
}

int player_atlas_current_sprite(const player_atlas_t *atlas)
{
    if (!atlas || !atlas->ready) return -1;
    int fid = atlas->animator.frame_id;
    if (fid < 0 || fid >= PLAYER_ATLAS_MAX_ANIM_FRAMES) return -1;
    return (int)atlas->sprite_id[fid];
}

void player_atlas_pivot(const player_atlas_t *atlas, int *out_px, int *out_py)
{
    int px = 0, py = 0;
    if (atlas && atlas->ready) {
        int idx = atlas->current_atlas_frame;
        if (idx >= 0 && idx < atlas->frame_total) {
            px = atlas->pivot_x[idx];
            py = atlas->pivot_y[idx];
        }
    }
    if (out_px) *out_px = px;
    if (out_py) *out_py = py;
}

void player_atlas_size(const player_atlas_t *atlas, int *out_w, int *out_h)
{
    int w = 0, h = 0;
    if (atlas && atlas->ready) {
        int idx = atlas->current_atlas_frame;
        if (idx >= 0 && idx < atlas->frame_total) {
            w = atlas->width[idx];
            h = atlas->height[idx];
        }
    }
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

int player_atlas_current_frame_id(const player_atlas_t *atlas)
{
    if (!atlas || !atlas->ready) return 0;
    return atlas->animator.frame_id;
}
