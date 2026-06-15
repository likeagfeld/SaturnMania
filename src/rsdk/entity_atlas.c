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
#include "storage.h"

#include <jo/jo.h>
#include <string.h>

/* HUD-regression diag (#186): records the exact return-false branch of the
 * most recent entity_atlas_load_ex so the HUD gate can discriminate WHY the
 * HUD atlas reports ready=0. Codes:
 *   0  = success (ready=1)
 *   1  = SP2 read returned NULL or length < 8 (jo_fs_read_file/pool/missing)
 *   2  = SP2 magic mismatch
 *   3  = zero frames uploaded (all jo_sprite_add failed = VDP1 exhausted)
 *   4  = MET read returned NULL or length < 8
 *   5  = MET magic mismatch
 *   9  = entered with NULL atlas/name
 * The caller (hud_load) snapshots this immediately after the HUD load, before
 * any other atlas overwrites it. */
__attribute__((used)) int32_t g_entity_atlas_last_fail = -1;

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
 * Phase 2.4j.1 extended table size from 15 to 16 (TitleCard atlas).
 * FR-2: TitleCard now populated (was a trailing NULL) so the lazy
 * residency begin-frame loop clears its per-frame sprite_id[] too. */
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
    &g_titlecard_atlas,
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

/* === FR-2 lazy residency: MRU blob pool + per-tick rebuild ===========
 *
 * Pixel source for the per-tick uploads is a 192 KB MRU pool of whole-atlas
 * SP2 blobs in LWRAM at 0x00260000. This region holds the dead-after-upload
 * SKY.DAT staging (0x260000..0x278000, scene_ghz.c GHZ_SKY_DAT_LWRAM_ADDR)
 * plus the per-scene file-read scratch (0x278000..0x290000, storage.c
 * SCENE_LWRAM_RAW). Both are idle during GHZ gameplay (SKY.DAT was uploaded
 * to VDP2 once; the scratch is only used while rsdk_load_scene runs, which
 * completes synchronously before the first gameplay tick). entity_residency_
 * reset() flushes the pool at every scene (re)load so no blob survives the
 * window where rsdk_load_scene reuses the upper half as scratch.
 *
 * Distinct from the player MRU pool (0x002D0000, player_atlas.c) -- no
 * overlap; the live scene entity table sits between them (0x290000..0x2D0000,
 * storage.c SCENE_LWRAM_ARENA). */
#define ENTITY_POOL_ADDR   ((unsigned char *)0x00260000)
#define ENTITY_POOL_SIZE   0x30000u      /* 192 KB (0x260000..0x290000)     */
/* Largest single atlas SP2 = SPRING 94592 B (measured cd/SPRING.SP2). The
 * worst-case headroom must therefore exceed 94592; 0x18000 (98304) guarantees
 * a whole-blob inflate never overruns the pool tail (we flush if it won't
 * fit). Two such blobs fit the 192 KB pool. */
#define ENTITY_MAX_BLOB    0x18000u      /* 96 KB (>= SPRING 94592 B)        */
#define ENTITY_BLOB_SLOTS  18

/* Task #192 (player-only resident): entities are NOT resident. The earlier
 * resident entity sprite pack was retired -- it collided byte-for-byte with
 * the live #188 FG.CEL LWRAM region and corrupted the foreground after the
 * title card. Entity SP2 pixel blobs are CD-streamed on demand into the MRU
 * pool below (occasional reads, far rarer than the per-anim player reads that
 * the resident SONIC.SPC eliminates -- see player_atlas.c). */

static unsigned char *const s_epool = ENTITY_POOL_ADDR;
static uint32_t s_epool_head;
static struct {
    const entity_atlas_t *atlas;   /* owner (NULL = empty slot)             */
    uint32_t off;
    uint32_t len;
} s_eblob[ENTITY_BLOB_SLOTS];
static int s_eblob_count;

/* The jo sprite id just above the player's resident block; the dynamic
 * entity/HUD/titlecard sprites for the current tick start here. -1 until the
 * first begin_frame arms residency. */
static int s_dyn_base = -1;

/* #192 slow-motion diagnostic (behavior-neutral, never reset). Divide each by
 * g_gp_draw_ticks (Game.c) to get the per-tick rate:
 *   g_er_cd_reads   -- count of jo_fs_read_file_ptr CD streams in _blob_get.
 *                      A per-tick rate >> 0 means the 192 KB MRU pool is
 *                      thrashing (working set > ~96 KB effective) and every
 *                      tick re-streams atlas blobs from CD (~100 ms/seek ->
 *                      multi-frame stall -> the user-reported slow motion).
 *   g_er_sprite_adds-- count of jo_sprite_add VDP1 uploads in _er_ensure.
 *                      A per-tick rate ~= on-screen distinct frame count
 *                      (~77 measured) is the unavoidable rebuild DMA cost;
 *                      that alone does NOT explain a 10x slowdown, CD thrash
 *                      does. The two counters separate the two hypotheses. */
__attribute__((used)) uint32_t g_er_cd_reads   = 0;
__attribute__((used)) uint32_t g_er_sprite_adds = 0;

void entity_residency_reset(void)
{
    s_epool_head  = 0;
    s_eblob_count = 0;
    s_dyn_base    = -1;
    for (int i = 0; i < ENTITY_BLOB_SLOTS; ++i) s_eblob[i].atlas = NULL;
}

/* Locate (or CD-stream) the atlas's whole SP2 blob in the MRU pool. Returns
 * the pool pointer or NULL on read/validate failure. Wholesale-flushes the
 * pool (MRU) when the next blob cannot fit -- harmless mid-tick because any
 * already-uploaded VDP1 sprites are independent of the blob bytes. */
static unsigned char *_blob_get(const entity_atlas_t *atlas)
{
    for (int i = 0; i < s_eblob_count; ++i)
        if (s_eblob[i].atlas == atlas)
            return s_epool + s_eblob[i].off;

    if (s_epool_head + ENTITY_MAX_BLOB > ENTITY_POOL_SIZE ||
        s_eblob_count >= ENTITY_BLOB_SLOTS) {
        s_epool_head  = 0;
        s_eblob_count = 0;
        for (int i = 0; i < ENTITY_BLOB_SLOTS; ++i) s_eblob[i].atlas = NULL;
    }

    /* CD-stream the atlas's SPR2 blob into the pool. The filename stem is the
     * atlas base_name the builder used (e.g. "RING" / "SPRING" / "TITLCARD"),
     * so the CD file is "<base>.SP2". This is an occasional gameplay CD read
     * (only on first display / after MRU eviction), far rarer than the player
     * per-anim reads that #192 made resident. */
    char fname[24];
    _concat(fname, sizeof(fname), atlas->base_name, ".SP2");
    unsigned char *dst = s_epool + s_epool_head;
    int raw = 0;
    if (!jo_fs_read_file_ptr(fname, dst, &raw)) return NULL;
    ++g_er_cd_reads;            /* #192 diag: a CD blob stream just happened */
    if (raw < 8 ||
        dst[0] != 'S' || dst[1] != 'P' || dst[2] != 'R' || dst[3] != '2') {
        return NULL;
    }
    s_eblob[s_eblob_count].atlas = atlas;
    s_eblob[s_eblob_count].off   = s_epool_head;
    s_eblob[s_eblob_count].len   = (uint32_t)raw;
    ++s_eblob_count;
    s_epool_head += ((uint32_t)raw + 3u) & ~3u;
    return dst;
}

/* Ensure atlas-flat frame `idx` is VDP1-resident this tick; return its jo
 * sprite id (-1 on failure). Per-tick dedup: a non-negative sprite_id[idx]
 * means this frame was already uploaded this tick (begin_frame cleared them
 * all to -1), so we reuse it. */
static int _er_ensure(entity_atlas_t *atlas, int idx)
{
    if (!atlas || !atlas->ready) return -1;
    if (idx < 0 || idx >= atlas->frame_total) return -1;
    if (atlas->sprite_id[idx] >= 0) return atlas->sprite_id[idx];

    unsigned char *blob = _blob_get(atlas);
    if (!blob) return -1;

    unsigned char *fp = blob + atlas->frame_off[idx];
    uint16_t w = _rd_u16(fp);
    uint16_t h = _rd_u16(fp + 2);
    jo_img img;
    img.data   = (jo_color *)(fp + 4);
    img.width  = (unsigned short)w;
    img.height = (unsigned short)h;
    int sid = jo_sprite_add(&img);
    if (sid < 0) return -1;          /* VDP1 slots/VRAM exhausted          */
    ++g_er_sprite_adds;              /* #192 diag: a VDP1 upload just ran   */
    atlas->sprite_id[idx] = (int16_t)sid;
    return sid;
}

void entity_residency_begin_frame(int dyn_base)
{
    s_dyn_base = dyn_base;
    if (dyn_base >= 0)
        jo_sprite_free_from(dyn_base);   /* LIFO rewind -- free last tick   */

    /* Clear every atlas's resident sprite_id[] so accessors re-upload this
     * tick (the VDP1 slots above dyn_base were just freed). */
    for (int t = 0; t < ENTITY_BLOB_SLOTS && t < 16; ++t) {
        entity_atlas_t *a = g_entity_atlas_table[t];
        if (!a) continue;
        int n = a->frame_total;
        if (n > ENTITY_ATLAS_MAX_FRAMES) n = ENTITY_ATLAS_MAX_FRAMES;
        for (int f = 0; f < n; ++f) a->sprite_id[f] = -1;
    }
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
    if (!atlas || !base_name) { g_entity_atlas_last_fail = 9; return false; }
    g_entity_atlas_last_fail = -2;   /* in-progress sentinel */
    memset(atlas, 0, sizeof(*atlas));

    /* FR-2: scratch/scratch_cap are no longer needed -- the lazy loader
     * reads the SP2 transiently into the MRU blob pool (LWRAM) for a
     * metadata-only parse and NEVER calls jo_malloc, so the pool-exhaustion
     * concern the scratch path addressed (Phase 2.4j.2) is moot. */
    (void)scratch;
    (void)scratch_cap;

    /* Remember the basename so the residency manager can CD-stream the
     * pixel blob on demand (jo_fs filenames are <=12 chars; base <=8). */
    {
        int i = 0;
        for (; base_name[i] && i < (int)sizeof(atlas->base_name) - 1; ++i)
            atlas->base_name[i] = base_name[i];
        atlas->base_name[i] = '\0';
    }

    char path[24];

    /* --- SP2 (metadata-only: per-frame w/h + byte offset; NO upload) ---
     * CD-stream the whole SP2 transiently into the blob pool base (load-time
     * read; entities are NOT resident). We do NOT register a cache entry
     * (entities_load_assets ran entity_residency_reset first; the upper pool
     * half is reused as rsdk_load_scene scratch right after this load, so
     * nothing may persist). Runtime first-ensure re-reads the blob into the
     * pool via _blob_get (also a CD stream -- occasional, far rarer than the
     * per-anim player reads that #192 made resident). */
    _concat(path, sizeof(path), base_name, ".SP2");
    int sp2_len = 0;
    unsigned char *sp2 =
        (unsigned char *)jo_fs_read_file_ptr(path, s_epool, &sp2_len);
    if (!sp2 || sp2_len < 8) {
        g_entity_atlas_last_fail = 1;
        return false;
    }
    if (sp2[0] != 'S' || sp2[1] != 'P' || sp2[2] != 'R' || sp2[3] != '2') {
        g_entity_atlas_last_fail = 2;
        return false;
    }
    uint16_t fc = _rd_u16(sp2 + 4);
    if (fc > ENTITY_ATLAS_MAX_FRAMES) fc = ENTITY_ATLAS_MAX_FRAMES;

    /* Walk frames recording (w, h, byte-offset-of-record). No jo_sprite_add:
     * pixels stay in CD/the pool and upload on demand per displayed frame. */
    unsigned char *p   = sp2 + 8;
    unsigned char *end = sp2 + sp2_len;
    int loaded = 0;
    for (int i = 0; i < fc; ++i) {
        if (p + 4 > end) break;
        uint16_t w = _rd_u16(p);
        uint16_t h = _rd_u16(p + 2);
        size_t bytes = (size_t)w * (size_t)h * 2u;
        if (p + 4 + bytes > end) break;
        atlas->frame_off[i] = (uint32_t)(p - sp2);   /* offset of (w,h,px) */
        atlas->width[i]      = w;
        atlas->height[i]     = h;
        atlas->sprite_id[i]  = -1;                   /* not resident yet   */
        p += 4 + bytes;
        ++loaded;
    }
    atlas->frame_total = (uint16_t)loaded;
    if (loaded == 0) { g_entity_atlas_last_fail = 3; return false; }

    /* --- MET (cadence + pivot/duration; reuse the pool for the read) --- */
    _concat(path, sizeof(path), base_name, ".MET");
    int met_len = 0;
    unsigned char *met =
        (unsigned char *)jo_fs_read_file_ptr(path, s_epool, &met_len);
    if (!met || met_len < 8) {
        g_entity_atlas_last_fail = 4;
        return false;
    }
    if (met[0] != 'M' || met[1] != 'E' || met[2] != 'T' || met[3] != '1') {
        g_entity_atlas_last_fail = 5;
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
    /* met lives in the MRU pool (no jo_malloc) -- nothing to free. */

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
    g_entity_atlas_last_fail = 0;
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
    /* FR-2: upload-on-demand. The atlas globals are mutable BSS; the const
     * qualifier is an API courtesy (callers pass const pointers, e.g.
     * animation.c:250). Casting it away to mutate residency is safe. */
    if (!atlas || !atlas->ready) return -1;
    int idx = atlas->current_atlas_frame;
    return _er_ensure((entity_atlas_t *)atlas, idx);
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
    /* FR-2: upload-on-demand (see entity_atlas_current_sprite). */
    return _er_ensure((entity_atlas_t *)atlas, idx);
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
