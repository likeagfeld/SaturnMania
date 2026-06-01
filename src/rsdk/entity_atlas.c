/* Phase 2.4e v2 (Task #144) -- SPR2 + MET entity atlas loader.
 *
 * Mechanical translation of the decomp ProcessAnimation walker
 * (rsdkv5-src/RSDKv5/RSDK/Graphics/Animation.cpp:150-177) bound to the
 * Saturn-side on-disk SPR2 + MET sidecar layout emitted by
 * tools/build_entity_atlas.py.
 *
 * On-disk format (cited from tools/build_entity_atlas.py + docs/anim_
 * completeness_audit.md "SPR2 format"):
 *
 *   <NAME>.SP2:
 *     4 B  "SPR2"
 *     u16 BE frame_count
 *     u16 BE reserved (0)
 *     for each frame:
 *       u16 BE width
 *       u16 BE height
 *       width*height * u16 BE  BGR1555 pixels
 *
 *   <NAME>.MET:
 *     4 B  "MET1"
 *     u16 BE anim_count
 *     u16 BE frame_count_total
 *     for each anim:
 *       u16 BE frame_count_in_anim
 *       u16 BE speed
 *       u16 BE loop_index
 *       u16 BE first_frame_index_in_atlas
 *       char[24] anim_name
 *     for each frame:
 *       u8  anim_id
 *       u8  frame_id_in_anim
 *       i16 BE pivot_x
 *       i16 BE pivot_y
 *       u16 BE duration
 */

#include "entity_atlas.h"

#include <jo/jo.h>
#include <string.h>

/* === Per-entity BSS globals (P4-peek-accessible).
 *
 * `used` attribute prevents LTO from eliding the symbol name from the
 * link map; without it the LTO pass collapses everything into a single
 * unnamed ltrans BSS block and the qa gate cannot locate individual
 * atlases by symbol. */

__attribute__((used)) entity_atlas_t g_ring_atlas;
__attribute__((used)) entity_atlas_t g_itembox_atlas;
__attribute__((used)) entity_atlas_t g_spring_atlas;
__attribute__((used)) entity_atlas_t g_signpost_atlas;
__attribute__((used)) entity_atlas_t g_spikes_atlas;
__attribute__((used)) entity_atlas_t g_motobug_atlas;
__attribute__((used)) entity_atlas_t g_buzz_atlas;
/* Phase 2.4c.2 Task #147 — Platform / SpikeLog / Newtron atlases. */
__attribute__((used)) entity_atlas_t g_spikelog_atlas;
__attribute__((used)) entity_atlas_t g_platform_atlas;
__attribute__((used)) entity_atlas_t g_newtron_atlas;
/* Phase 2.4h — GHZ Act 1 badnik atlases (Chopper/Crabmeat/Batbrain). */
__attribute__((used)) entity_atlas_t g_chopper_atlas;
__attribute__((used)) entity_atlas_t g_crabmeat_atlas;
__attribute__((used)) entity_atlas_t g_batbrain_atlas;
/* Phase 2.4i Task #154 — authentic HUD atlas (replaces fabricated
 * DIGITS.SPR). Built from extracted/Data/Sprites/Global/HUD.bin. */
__attribute__((used)) entity_atlas_t g_hud_atlas;
/* Phase 2.4-PLAT Task #155 — Bridge planks (GHZ/Bridge.bin). The only
 * in-game-visible class in the platforming set. */
__attribute__((used)) entity_atlas_t g_bridge_atlas;
/* Phase 2.4j.1 Task #156 — TitleCard act-intro atlas (Global/TitleCard.bin).
 * Drives the zone-name glyph string + ZONE + act-number sprites. */
__attribute__((used)) entity_atlas_t g_titlecard_atlas;

/* Anchor table: an initialised pointer array in .data so the gate can
 * locate it by symbol (g_entity_atlas_table) and read each atlas's
 * address from the table. This is how the P4 gate locates the atlases
 * given that LTO collapses BSS names.
 *
 * Phase 2.4c.2 extended table size from 7 to 10.
 * Phase 2.4h extended table size from 10 to 13 (badnik atlases).
 * Phase 2.4i extended table size from 13 to 14 (HUD atlas).
 * Phase 2.4-PLAT extended table size from 14 to 15 (Bridge atlas).
 * Phase 2.4j.1 extended table size from 15 to 16 (TitleCard atlas). */
__attribute__((used)) entity_atlas_t * const g_entity_atlas_table[16] = {
    &g_ring_atlas,
    &g_itembox_atlas,
    &g_spring_atlas,
    &g_signpost_atlas,
    &g_spikes_atlas,
    &g_motobug_atlas,
    &g_buzz_atlas,
    &g_spikelog_atlas,
    &g_platform_atlas,
    &g_newtron_atlas,
    &g_chopper_atlas,
    &g_crabmeat_atlas,
    &g_batbrain_atlas,
    &g_hud_atlas,
    &g_bridge_atlas,
};

/* === BE byte readers (Saturn SH-2 is big-endian; ALSO match decomp
 *     ReadInt* contract). ============================================ */

static inline uint16_t _rd_u16(const unsigned char *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static inline int16_t _rd_i16(const unsigned char *p)
{
    return (int16_t)_rd_u16(p);
}

/* === Filename concat (CD volume is ISO-9660 8.3 uppercase; jo_fs_read
 *     accepts e.g. "RING.SP2" / "RING.MET"). ========================= */

static void _concat(char *dst, size_t dstn, const char *base, const char *ext)
{
    size_t i = 0;
    while (base[i] && i < dstn - 1) { dst[i] = base[i]; ++i; }
    size_t j = 0;
    while (ext[j] && i < dstn - 1) { dst[i++] = ext[j++]; }
    dst[i] = '\0';
}

/* === Load implementation =============================================
 *
 * Sequence:
 *   1. Read SP2; for each frame, upload native-size pixels via
 *      jo_sprite_add into VDP1 VRAM. Record per-frame (w, h, sprite_id).
 *   2. Read MET; for each frame, record (pivot_x, pivot_y, duration).
 *      For each anim, record (frame_count, speed, loop_index, first).
 *   3. Synthesize one rsdk_sprite_frame_t per atlas frame (the walker
 *      reads duration from this array). Point animator.frames at it.
 *   4. Default to anim 0 frame 0; ready=true. */

/* Phase 2.4j.2 (Task #157) — scratch-buffer variant. When `scratch` is
 * non-NULL the SP2 (and then the MET) file is read into the caller's
 * buffer via jo_fs_read_file_ptr (fs.c:282 takes the buffer directly,
 * skipping jo_malloc), so a large atlas can load even when jo's 256 KB
 * pool is exhausted mid-GHZ. This is the entity-atlas analogue of the
 * FG.TMP / SKY.DAT LWRAM bypass (memory/ghz-sky-dat-lwram-bypass.md):
 * GFS finds TITLCARD.SP2 (fid valid) but jo_malloc(50721) fails — proven
 * via the 2.4j.2 diagnostic latch (dbg[1]=97, dbg[3]=0). `scratch_cap`
 * must be >= file size + 1; the caller (titlecard) reserves 64 KB in
 * LWRAM for a 50720-byte SP2. The buffer is caller-owned (LWRAM-resident,
 * never jo_free'd). */
bool entity_atlas_load_ex(entity_atlas_t *atlas, const char *base_name,
                          void *scratch, int scratch_cap)
{
    if (!atlas || !base_name) return false;
    memset(atlas, 0, sizeof(*atlas));

    char path[24];

    /* --- SP2 ------------------------------------------------------- */
    _concat(path, sizeof(path), base_name, ".SP2");
    int sp2_len = 0;
    unsigned char *sp2;
    if (scratch) {
        sp2 = (unsigned char *)jo_fs_read_file_ptr(path, scratch, &sp2_len);
    } else {
        sp2 = (unsigned char *)jo_fs_read_file(path, &sp2_len);
    }
    if (!sp2 || sp2_len < 8) {
        if (sp2 && !scratch) jo_free(sp2);
        return false;
    }
    if (sp2[0] != 'S' || sp2[1] != 'P' || sp2[2] != 'R' || sp2[3] != '2') {
        if (!scratch) jo_free(sp2);
        return false;
    }
    uint16_t fc = _rd_u16(sp2 + 4);
    if (fc == 0 || fc > ENTITY_ATLAS_MAX_FRAMES) {
        /* Cap silently at MAX_FRAMES (excess frames simply don't load
         * sprites; the walker will index into the loaded range only). */
        if (fc > ENTITY_ATLAS_MAX_FRAMES) fc = ENTITY_ATLAS_MAX_FRAMES;
    }
    atlas->frame_total = fc;

    /* Iterate frames: each is u16 w + u16 h + w*h * u16 BGR1555. */
    unsigned char *p = sp2 + 8;
    unsigned char *end = sp2 + sp2_len;
    int loaded = 0;
    {
        jo_img img;
        for (int i = 0; i < fc; ++i) {
            if (p + 4 > end) break;
            uint16_t w = _rd_u16(p);
            uint16_t h = _rd_u16(p + 2);
            p += 4;
            size_t bytes = (size_t)w * (size_t)h * 2u;
            if (p + bytes > end) break;
            /* jo_sprite_add reads jo_color (u16) pixels in the platform's
             * byte order. SH-2 is big-endian and the SP2 file was emitted
             * in big-endian network order by Python struct.pack(">H"), so
             * the bytes already match the in-register u16 value. We can
             * pass the buffer pointer directly. */
            img.data   = (jo_color *)p;
            img.width  = (unsigned short)w;
            img.height = (unsigned short)h;
            int sid = jo_sprite_add(&img);
            if (sid < 0) {
                /* jo VRAM / slot exhausted. Stop loading further frames;
                 * the walker will still play the loaded subset. */
                break;
            }
            atlas->sprite_id[i] = (int16_t)sid;
            atlas->width[i]     = w;
            atlas->height[i]    = h;
            p += bytes;
            ++loaded;
        }
    }
    /* SP2 pixels are now resident in VDP1 VRAM; the SP2 bytes are no
     * longer needed. For the pool path, free them. For the scratch path,
     * the buffer is caller-owned and we reuse it below for the MET read. */
    if (!scratch) jo_free(sp2);
    atlas->frame_total = (uint16_t)loaded;
    if (loaded == 0) return false;

    /* --- MET ------------------------------------------------------- */
    _concat(path, sizeof(path), base_name, ".MET");
    int met_len = 0;
    unsigned char *met;
    if (scratch) {
        /* Reuse the scratch buffer (SP2 processing is complete). MET is
         * ~500 B, far under scratch_cap. */
        (void)scratch_cap;
        met = (unsigned char *)jo_fs_read_file_ptr(path, scratch, &met_len);
    } else {
        met = (unsigned char *)jo_fs_read_file(path, &met_len);
    }
    if (!met || met_len < 8) {
        if (met && !scratch) jo_free(met);
        return false;
    }
    if (met[0] != 'M' || met[1] != 'E' || met[2] != 'T' || met[3] != '1') {
        if (!scratch) jo_free(met);
        return false;
    }
    uint16_t ac = _rd_u16(met + 4);
    uint16_t mc = _rd_u16(met + 6);
    if (ac > ENTITY_ATLAS_MAX_ANIMS) ac = ENTITY_ATLAS_MAX_ANIMS;
    atlas->anim_count = (uint8_t)ac;

    /* Per-anim record (10 + 24 = 34 B with the gate's layout): u16 fc,
     * u16 speed, u16 loop, u16 first, char[24] name. */
    const int ANIM_REC = 2 + 2 + 2 + 2 + 24;
    p = met + 8;
    end = met + met_len;
    for (int i = 0; i < ac; ++i) {
        if (p + ANIM_REC > end) break;
        atlas->anims[i].frame_count = _rd_u16(p);
        atlas->anims[i].speed       = _rd_u16(p + 2);
        atlas->anims[i].loop_index  = _rd_u16(p + 4);
        atlas->anims[i].first       = _rd_u16(p + 6);
        p += ANIM_REC;
    }
    /* Per-frame record: u8 anim_id, u8 frame_id, i16 px, i16 py, u16 dur,
     * u16 unicode = 10 B per frame (unicode added Phase 2.4j.1; see
     * tools/build_entity_atlas.py MET1 format docstring). */
    const int FRAME_REC = 1 + 1 + 2 + 2 + 2 + 2;
    for (int i = 0; i < mc && i < loaded; ++i) {
        if (p + FRAME_REC > end) break;
        /* p[0] = anim_id, p[1] = frame_id_in_anim -- consumed by P2 gate
         *        but not needed at runtime (we walk the atlas-flat
         *        first[anim]+offset table). */
        atlas->pivot_x[i]      = _rd_i16(p + 2);
        atlas->pivot_y[i]      = _rd_i16(p + 4);
        atlas->duration[i]     = _rd_u16(p + 6);
        atlas->unicode_char[i] = _rd_u16(p + 8);  /* glyph codepoint */
        p += FRAME_REC;
    }
    if (!scratch) jo_free(met);

    /* Synthesize the scratch_frames table the rsdk walker reads from.
     * Decomp Animation.cpp:174 reads animator->frames[frame_id].duration
     * -- we mirror by populating duration only (other fields unused by
     * ProcessAnimation per animation.cpp:96-118). */
    for (int i = 0; i < loaded; ++i) {
        atlas->scratch_frames[i].duration = atlas->duration[i];
        atlas->scratch_frames[i].width    = atlas->width[i];
        atlas->scratch_frames[i].height   = atlas->height[i];
        atlas->scratch_frames[i].pivot_x  = atlas->pivot_x[i];
        atlas->scratch_frames[i].pivot_y  = atlas->pivot_y[i];
    }

    /* Initial state: play anim 0 by default so something is on screen
     * the moment the load returns. */
    atlas->ready = 1;
    entity_atlas_play(atlas, 0);
    return true;
}

bool entity_atlas_load(entity_atlas_t *atlas, const char *base_name)
{
    return entity_atlas_load_ex(atlas, base_name, NULL, 0);
}

/* === entity_atlas_play -- SetSpriteAnimation per Animation.cpp:131-153
 *
 * Saturn-port simplification: we drive rsdk_animator_t directly with the
 * atlas's scratch_frames table (offset to first frame of the chosen anim).
 * The walker advances frame_id within [0, frame_count); we map it back
 * to atlas-flat index via current_anim's first[]. */

void entity_atlas_play(entity_atlas_t *atlas, int anim_id)
{
    if (!atlas || !atlas->ready) return;
    if (anim_id < 0 || anim_id >= atlas->anim_count) return;
    if (atlas->anims[anim_id].frame_count == 0) return;

    atlas->current_anim        = (uint16_t)anim_id;
    int first                  = atlas->anims[anim_id].first;
    atlas->current_atlas_frame = (uint16_t)first;

    /* Point animator at the anim's slice of the scratch_frames array.
     * rsdk_process_animation reads animator->frames[frame_id].duration
     * with frame_id in [0, frame_count), so frames = scratch + first. */
    atlas->animator.frames         = &atlas->scratch_frames[first];
    atlas->animator.frame_id       = 0;
    atlas->animator.animation_id   = (int16_t)anim_id;
    atlas->animator.prev_animation_id = (int16_t)anim_id;
    atlas->animator.speed          = (int16_t)atlas->anims[anim_id].speed;
    atlas->animator.timer          = 0;
    atlas->animator.frame_count    = (int16_t)atlas->anims[anim_id].frame_count;
    atlas->animator.loop_index     = (uint8_t)atlas->anims[anim_id].loop_index;
    atlas->animator.frame_duration =
        (int16_t)atlas->scratch_frames[first].duration;
    atlas->animator.rotation_style = 0;
    atlas->animator.list_id        = 0;
}

/* === entity_atlas_tick -- ProcessAnimation per Animation.cpp:150-177
 *
 * Delegates to rsdk_process_animation (the canonical Saturn walker)
 * which advances animator->frame_id (in anim-local space) by
 * accumulating per-frame duration. We then map back to atlas-flat
 * current_atlas_frame for the gate peek + sprite-id lookup. */

void entity_atlas_tick(entity_atlas_t *atlas)
{
    if (!atlas || !atlas->ready) return;
    if (atlas->animator.frame_count == 0) return;

    rsdk_process_animation(&atlas->animator);

    /* Defensive bounds (decomp Animation.cpp clamps via loop_index but
     * a malformed MET could push frame_id out of range). */
    int fid = atlas->animator.frame_id;
    if (fid < 0) fid = 0;
    if (fid >= atlas->animator.frame_count) {
        fid = atlas->animator.frame_count - 1;
    }
    int first = atlas->anims[atlas->current_anim].first;
    atlas->current_atlas_frame = (uint16_t)(first + fid);
}

int entity_atlas_current_sprite(const entity_atlas_t *atlas)
{
    if (!atlas || !atlas->ready) return -1;
    int idx = atlas->current_atlas_frame;
    if (idx < 0 || idx >= atlas->frame_total) return -1;
    return (int)atlas->sprite_id[idx];
}

void entity_atlas_pivot(const entity_atlas_t *atlas, int *out_px, int *out_py)
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

void entity_atlas_size(const entity_atlas_t *atlas, int *out_w, int *out_h)
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

/* === Phase 2.4j.1 by-atlas-frame accessors (text trio support) =======
 *
 * The text-rendering trio (rsdk_set_sprite_string / rsdk_get_string_width
 * / rsdk_draw_text in animation.c) needs to address arbitrary glyph
 * frames by their atlas-flat index (resolved from the unicode_char table),
 * independent of the animator's current_atlas_frame. These mirror the
 * current_* accessors above but take an explicit frame index. */

int entity_atlas_first_of_anim(const entity_atlas_t *atlas, int anim_id)
{
    if (!atlas || !atlas->ready) return -1;
    if (anim_id < 0 || anim_id >= atlas->anim_count) return -1;
    return (int)atlas->anims[anim_id].first;
}

int entity_atlas_frame_for_unicode(const entity_atlas_t *atlas, int anim_id,
                                   uint16_t uc)
{
    /* SetSpriteString (Animation.cpp:211-231): scan the anim's frames for
     * a frame whose unicodeChar matches `uc`; return the atlas-flat index,
     * or -1 if not found. */
    if (!atlas || !atlas->ready) return -1;
    if (anim_id < 0 || anim_id >= atlas->anim_count) return -1;
    int first = atlas->anims[anim_id].first;
    int count = atlas->anims[anim_id].frame_count;
    for (int i = 0; i < count; ++i) {
        int idx = first + i;
        if (idx >= 0 && idx < atlas->frame_total &&
            atlas->unicode_char[idx] == uc) {
            return idx;
        }
    }
    return -1;
}

int entity_atlas_sprite_at(const entity_atlas_t *atlas, int idx)
{
    if (!atlas || !atlas->ready) return -1;
    if (idx < 0 || idx >= atlas->frame_total) return -1;
    return (int)atlas->sprite_id[idx];
}

void entity_atlas_pivot_at(const entity_atlas_t *atlas, int idx,
                           int *out_px, int *out_py)
{
    int px = 0, py = 0;
    if (atlas && atlas->ready && idx >= 0 && idx < atlas->frame_total) {
        px = atlas->pivot_x[idx];
        py = atlas->pivot_y[idx];
    }
    if (out_px) *out_px = px;
    if (out_py) *out_py = py;
}

void entity_atlas_size_at(const entity_atlas_t *atlas, int idx,
                          int *out_w, int *out_h)
{
    int w = 0, h = 0;
    if (atlas && atlas->ready && idx >= 0 && idx < atlas->frame_total) {
        w = atlas->width[idx];
        h = atlas->height[idx];
    }
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}
