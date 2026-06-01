#ifndef RSDK_STORAGE_H
#define RSDK_STORAGE_H

/* Phase A1 — Storage layer, Saturn port of RSDKv5/RSDK/Storage/.
 *
 * Per docs/rsdkv5_engine_catalog.md §1 + BIBLE.md Phase A row A1.
 *
 * Authoritative source (cite by §section when touching this code):
 *   §1.1 Data.rsdk pack file format (Reader.cpp)
 *   §1.2 Hash lookup convention (MD5(lowercased POSIX path))
 *   §1.3 eLoad cipher (Mania ships unencrypted; flag must still be honored)
 *   §1.4 Scene.bin (Scene.cpp LoadSceneFile L461-665)
 *   §1.5 Anim.bin (Animation.cpp LoadSpriteAnimation L29-108)
 *
 * Saturn-port deviations from upstream:
 *   * We do NOT parse Data.rsdk at runtime. The offline extractor
 *     tools/dump_data_rsdk.py unpacks the pack into per-file blobs under
 *     cd/, and the engine reads those individual files via jo_fs_read_file.
 *     This trades runtime RAM (no resident pack index) for cd-side disk
 *     space (~1.8 MB extracted vs ~1.4 MB packed).
 *   * 16.16 fixed-point is preserved as int32; helpers convert to integer
 *     pixels where Saturn calls (jo_sprite_draw3D) want int coords.
 *
 * Memory budget: Saturn Work RAM-H has ~256 KB free heap (jo malloc pool).
 * Per-scene loads run in the low tens of KB. Per-anim loads in low single
 * digits. Cumulative load policy follows RSDKv5: load on scene-enter,
 * reset on scene-exit. */

#include <stdint.h>
#include <stdbool.h>

/* --- §1.1: Data.rsdk constants ---------------------------------------- */

#define RSDK_SIGNATURE_RSDK   0x4B445352u   /* "RSDK" little-endian       */
#define RSDK_SIGNATURE_SCN    0x004E4353u   /* "SCN\0" little-endian      */
#define RSDK_SIGNATURE_SPR    0x00525053u   /* "SPR\0" little-endian      */
#define RSDK_SIGNATURE_TIL    0x004C4954u   /* "TIL\0" little-endian      */
#define RSDK_SIGNATURE_CFG    0x00474643u   /* "CFG\0" little-endian      */
#define RSDK_DATAFILE_COUNT   0x1000
#define RSDK_SPRITEANIM_COUNT 0x40
#define RSDK_TILE_COUNT       1024
#define RSDK_TILE_SIZE        16
#define RSDK_CPATH_COUNT      2             /* path 0 = primary, 1 = alt  */

/* --- §1.5 ROTSTYLE enum ----------------------------------------------- */

enum {
    ROTSTYLE_NONE         = 0,
    ROTSTYLE_FULL         = 1,
    ROTSTYLE_45DEG        = 2,
    ROTSTYLE_90DEG        = 3,
    ROTSTYLE_180DEG       = 4,
    ROTSTYLE_STATICFRAMES = 5
};

/* --- §1.4 Scene layer types ------------------------------------------- */

enum {
    LAYER_HSCROLL = 0,
    LAYER_VSCROLL = 1,
    LAYER_ROTOZOOM = 2,
    LAYER_BASIC   = 3
};

/* --- Hitbox + SpriteFrame + Animator (Animation.hpp:21-71) ------------ */

typedef struct {
    int16_t left, top, right, bottom;
} rsdk_hitbox_t;

typedef struct {
    uint8_t  sheet_id;
    uint16_t duration;
    uint16_t unicode_char;
    uint16_t src_x, src_y;        /* source rect on sheet                 */
    uint16_t width, height;
    int16_t  pivot_x, pivot_y;    /* signed!                              */
    uint8_t  hitbox_count;
    rsdk_hitbox_t hitboxes[8];    /* RSDK FRAMEHITBOX_COUNT               */
} rsdk_sprite_frame_t;

typedef struct {
    uint32_t            hash[4];          /* MD5(name)                    */
    char                name[32];
    rsdk_sprite_frame_t *frames;
    uint16_t             frame_count;
    uint16_t             speed;           /* fixed-tick rate              */
    uint8_t              loop_index;
    uint8_t              rotation_style;
} rsdk_sprite_animation_entry_t;

typedef struct {
    uint32_t                       hash[4];
    char                           name[32];
    rsdk_sprite_frame_t           *frames;       /* shared frame storage  */
    rsdk_sprite_animation_entry_t *animations;
    uint16_t                       anim_count;
    uint8_t                        sheet_count;
    char                           sheet_names[8][64];
} rsdk_sprite_animation_t;

/* Per-instance animator (one per drawing entity).
 *
 * Phase 1.23 GAP A — `list_id` propagated through the draw callback so
 * mania-side resolve_asset can key off (list_id, anim_id) without the
 * §11.28 `s_active_list_id` global hack. Stored by
 * rsdk_set_sprite_animation; read by rsdk_draw_sprite_ex when invoking
 * the registered sprite callback. */
typedef struct {
    rsdk_sprite_frame_t *frames;
    int32_t              frame_id;
    int16_t              animation_id, prev_animation_id;
    int16_t              speed, timer;
    int16_t              frame_duration, frame_count;
    uint8_t              loop_index;
    uint8_t              rotation_style;
    uint16_t             list_id;             /* Phase 1.23 GAP A      */
} rsdk_animator_t;

/* --- Scene layer + entity types --------------------------------------- */

typedef struct {
    char     name[32];
    uint8_t  type;
    uint8_t  draw_group;
    uint8_t  width_shift;
    uint8_t  height_shift;
    uint16_t xsize, ysize;                /* tile dimensions              */
    int16_t  parallax_factor;             /* 8.8 fixed                    */
    int16_t  scroll_speed;                /* 16.16-ish                    */
    uint8_t  deform;
    uint16_t *layout;                     /* xsize*ysize uint16 entries   */
    uint16_t  layout_len;
} rsdk_scene_layer_t;

typedef struct {
    uint32_t hash[4];                     /* MD5 of attribute name        */
    uint8_t  type;                        /* VAR_UINT8..VAR_VECTOR2       */
    uint16_t offset;                      /* offset inside entity struct  */
} rsdk_scene_attrib_t;

typedef struct {
    uint32_t hash[4];                     /* MD5 of class name (verbatim) */
    char     name[32];                    /* if known (resolved post-load)*/
    uint8_t  var_count;                   /* INCLUDES implicit filter @0  */
    rsdk_scene_attrib_t attribs[16];
    uint16_t entity_count;
    uint32_t entity_data_offset;          /* byte offset into entity blob */
} rsdk_scene_class_t;

typedef struct {
    uint16_t class_index;                 /* into scene->classes[]        */
    uint16_t slot;
    int32_t  pos_x, pos_y;                /* 16.16 fixed                  */
    uint8_t  filter;                      /* defaults to 0xFF             */
    /* Per-attribute payload follows in entity_data[] blob (raw bytes).
     * Caller indexes via scene_class->attribs[v]. */
    uint32_t attr_payload_offset;
} rsdk_scene_entity_t;

typedef struct {
    uint8_t              layer_count;
    rsdk_scene_layer_t   layers[8];
    uint8_t              class_count;
    rsdk_scene_class_t   classes[64];
    uint16_t             entity_count;
    rsdk_scene_entity_t *entities;
    uint8_t             *entity_data;     /* raw attribute payload blob   */
    uint32_t             entity_data_size;
    uint8_t              mem_lwram;        /* 1 = entities/entity_data live
                                            * in the LWRAM scene arena (do
                                            * NOT jo_free); see Phase 2.4g.1
                                            * LWRAM scene-load note in
                                            * storage.c. */
} rsdk_scene_t;

/* --- §1.4 StageConfig.bin (verified format from Mania catalog §1.1) --- */

typedef struct {
    uint8_t load_globals;
    uint8_t object_count;
    char    object_names[64][32];         /* class names to preload       */
    /* palette banks + masks come after, format TBD; for now keep raw blob*/
    uint8_t *trailing_blob;
    uint32_t trailing_size;
} rsdk_stage_config_t;

/* --- TileConfig.bin (collision data) ---------------------------------- */
/* Per path (2 paths total):
 *   Per tile (1024):
 *     uint8 maskHeights[16]
 *     uint8 maskActive[16]
 *     uint8 yFlip
 *     uint8 floorAngle, lWallAngle, rWallAngle, roofAngle  (Q0.8)
 *     uint8 flag                                                          */

typedef struct {
    uint8_t mask_heights[16];
    uint8_t mask_active[16];
    uint8_t y_flip;
    uint8_t floor_angle;
    uint8_t left_wall_angle;
    uint8_t right_wall_angle;
    uint8_t roof_angle;
    uint8_t flag;
} rsdk_tile_t;

typedef struct {
    rsdk_tile_t paths[RSDK_CPATH_COUNT][RSDK_TILE_COUNT];
} rsdk_tile_config_t;

/* ===== Public API ===================================================== */

/* Compute MD5 hash of a name string (case-preserved). Output is 16 bytes
 * in `out_hash` (laid out as 4 uint32 in big-endian network order so that
 * memcmp against scene-file hashes works). */
void rsdk_md5_name(const char *name, uint32_t out_hash[4]);

/* Load a Scene*.bin from CD into `scene`. Returns true on success.
 * Caller frees with rsdk_scene_free(). */
bool rsdk_scene_load(const char *filename, rsdk_scene_t *scene);
void rsdk_scene_free(rsdk_scene_t *scene);

/* Phase 2.4g.1 — select the backing memory for the NEXT rsdk_scene_load.
 * When on, the file read + entity table + attribute blob come from the
 * LWRAM scene arena (0x00278000+) instead of jo's 256 KB pool. Required
 * for large stage scenes (GHZSCN1.BIN = 85191 B, 1041 entities) that do
 * not fit jo's pool residue at stage-active time. Binding rule:
 * memory/ghz-sky-dat-lwram-bypass.md. The flag is sticky until changed;
 * the GHZ load path sets it true, the Title path leaves it false. */
void rsdk_scene_set_lwram_mode(bool on);

/* Find a class by name (computes hash internally). Returns -1 if absent. */
int  rsdk_scene_find_class(const rsdk_scene_t *scene, const char *class_name);

/* Iterate entities of a given class. Returns the entity at `index`, or
 * NULL if out of range. */
const rsdk_scene_entity_t *rsdk_scene_entity_at(const rsdk_scene_t *scene,
                                                int class_index, int index);

/* Read one attribute value from an entity. `attr_index` is the LOCAL
 * variable index (1..var_count-1; index 0 = filter is read directly from
 * entity->filter). Returns 0 / 0.0 if attr_index is out of range. */
uint8_t  rsdk_entity_attr_u8(const rsdk_scene_t *scene,
                             const rsdk_scene_entity_t *ent, int attr_index);
uint32_t rsdk_entity_attr_u32(const rsdk_scene_t *scene,
                              const rsdk_scene_entity_t *ent, int attr_index);
int32_t  rsdk_entity_attr_i32(const rsdk_scene_t *scene,
                              const rsdk_scene_entity_t *ent, int attr_index);

/* Load a sprite animation .bin. */
bool rsdk_sprite_animation_load(const char *filename,
                                rsdk_sprite_animation_t *anim);
void rsdk_sprite_animation_free(rsdk_sprite_animation_t *anim);

/* Load TileConfig.bin (decompresses the zlib payload). */
bool rsdk_tile_config_load(const char *filename, rsdk_tile_config_t *out);

/* Load StageConfig.bin (just the object list — no music block per Mania
 * catalog §1.1 confirmation). */
bool rsdk_stage_config_load(const char *filename, rsdk_stage_config_t *cfg);
void rsdk_stage_config_free(rsdk_stage_config_t *cfg);

/* Phase 2.2b — raw SBL/GFS read into a caller-supplied buffer.
 *
 * Unlike rsdk_scene_load / rsdk_sprite_animation_load (which go through
 * jo_fs_read_file -> jo_malloc and consume the jo pool), this helper
 * bypasses jo entirely and uses the SBL GFS API directly. The caller
 * supplies the destination buffer — typically a static address in LWRAM
 * (0x00200000..0x002FFFFF, 1 MB unused by jo) for large read-only
 * residents that don't justify pool consumption.
 *
 * Per DTS-136-R2-093094 (Saturn File System Library) §4: GFS_Fread reads
 * in sector units (1 sector = 2048 B). We pass max_bytes as the bsize
 * parameter; GFS_Fread truncates output to that bound. Returns total
 * bytes read on success (>=0), or -1 on any failure.
 *
 * The destination buffer MUST be large enough to hold the entire file
 * AND aligned to a 4-byte boundary (SH-2 long-word access). LWRAM at
 * 0x00200000 is 32-byte aligned naturally; sub-region offsets that the
 * caller picks must preserve at least 4-byte alignment.
 *
 * Used by mania_ghz_player_preload_world (Phase 2.2b) to land the 64 KB
 * GHZ1SURF.BIN collision table at LWRAM 0x00200000 without touching jo's
 * pool. See docs/COMPREHENSIVE_PLAN.md §12.2b for the rationale. */
int rsdk_storage_load_to_lwram(const char *iso9660_name,
                               void *dst, uint32_t max_bytes);

#endif /* RSDK_STORAGE_H */
