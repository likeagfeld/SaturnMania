#ifndef RSDK_OBJECT_H
#define RSDK_OBJECT_H

/* Phase A2 — Object/Entity system, Saturn port of RSDKv5/RSDK/Scene/Object.
 *
 * Per docs/rsdkv5_engine_catalog.md §2 + BIBLE.md Phase A row A2.
 *
 * Authoritative source (cite by §section when touching this code):
 *   §2.1 Slot constants (Object.hpp:11-23)
 *   §2.2 ACTIVE states (Object.hpp:60-70)
 *   §2.3 Entity struct (Object.hpp:107-128)
 *   §2.4 ObjectClass (Object.hpp:148-175)
 *   §2.5 Frame loop (Object.cpp:506-740)
 *   §2.6 Lifecycle API (Object.cpp:1121-1214)
 *   §2.7 Foreach iterators (Object.cpp:1218-1263)
 *
 * Saturn-port deviations from upstream:
 *   * ENTITY_COUNT reduced from 0x940 (2368) to 0x200 (512). Mania's
 *     full slot budget assumes a high-RAM gameplay system; Saturn Work
 *     RAM-H (1 MB total) needs the headroom for streaming + audio +
 *     sprite atlas budgets documented in BIBLE.md. 512 slots @ 64 B
 *     per entity = 32 KB resident, fits comfortably.
 *   * `staticVars` ObjectClass pointer-pointer is collapsed to a single
 *     pointer since we don't support hot-reload of static data.
 *   * Per-class `editorLoad`/`editorDraw` callbacks omitted (no editor
 *     on Saturn).
 *   * Entity struct field offsets MIRROR upstream so data tables built
 *     from PC decomp transfer 1:1.
 *
 * Memory budget:
 *   ENTITY_COUNT = 512
 *   sizeof(rsdk_entity_t) = 64 B (verified by static_assert in .c)
 *   Total resident entity table = 32 KB
 *   ObjectClass table = OBJECT_COUNT (1024) * sizeof(class) ~ 48 KB
 *   foreach stack = 1 KB
 *   Total RSDK Object subsystem state ~ 81 KB */

#include <stdint.h>
#include <stdbool.h>
#include "storage.h"   /* for hash routines + scene types */

/* --- §2.1 Slot constants -------------------------------------------- */

/* Phase 1.15 ROOT CAUSE FIX (docs/COMPREHENSIVE_PLAN.md §11.22):
 * Reduced from 0x400 (1024) to 0x100 (256) because the original
 * 1024-slot array (rsdk_object_class_t * 1024 = ~73 KB BSS) had its
 * sizeof memset in rsdk_object_init() corrupting SGL's work area at
 * top of WRAM-H. The memset extended past 0x060C0000 boundary,
 * zeroing SGL's sortlist + sprite attribute table + Zbuffer per
 * HANDOFF.md §5.5. Sonic Mania has ~200 distinct object classes
 * total per the decomp catalog, so 256 fits with margin. */
#define RSDK_OBJECT_COUNT       0x40    /* 64 class slots (Saturn-reduced for Phase 1 Title scope) */
/* Phase 1.22 fix (docs/COMPREHENSIVE_PLAN.md §11.28): scene-bin slot
 * IDs in Title/Scene1.bin range 0..75 (Title3DSprite's 58 entities live
 * in slots 16..73; TitleLogo PRESSSTART at slot 74; Music at slot 75).
 * The decomp's SCENEENTITY_COUNT is 256, so all of these are scene-slot
 * remaps (no temp-ring overflow). On Saturn we shrank SCENEENTITY_COUNT
 * to 32 in Phase 1.17 because "Title only needs ~10" -- but that was
 * counting unique CLASSES, not scene-bin SLOTS. With 32 scene slots,
 * Title3DSprite + TitleLogo + Music slot IDs immediately overflow into
 * the temp ring (or worse, beyond the table). Phase 1.17's failure mode
 * was that rsdk_create_entity allocated from g_rsdk_create_slot (temp
 * head) ignoring the scene-bin slot, so the 76 scene entities
 * round-robin'd through 16 temp slots and 60 entities were silently
 * overwritten -- including all 7 TitleLogo entities.
 *
 * Bump SCENEENTITY_COUNT to 80 to encompass all Title scene-bin slot
 * IDs with margin. BSS cost: +48 slots * 256B stride = +12 KB; current
 * _end has 51 KB headroom to the 0x060C0000 SGL boundary.
 *
 * Phase 2.1+ GHZ Act 1 scene-bin may exceed 80 slots (rings + badniks);
 * a Phase 2.x sweep should re-audit this once GHZ Scene1.bin is parsed
 * the same way (currently a placeholder).
 *
 * Phase 2.4g.2 (Task #153): the GHZ Act 1 scene-bin spawn pipeline
 * (scene.c) compacts entities whose scene-bin slot is beyond
 * SCENEENTITY_COUNT into the temp ring via a class-agnostic cursor
 * (scene.c:359). 2.4g.1 registered InvisibleBlock (18 overflow
 * instances); 2.4g.2 adds BoundsMarker (22). 18 + 22 = 40 instances must
 * fit the temp budget or scene.c silently drops the excess
 * (++g_scene_diag_create_fail). Bumped 0x20 -> 0x30 (32 -> 48) to hold
 * both classes' overflow with margin. Cost: the temp slots live in the
 * jo_malloc'd entity buffer (HEAP: +16 slots * 256 B = +4 KB), NOT BSS;
 * the only BSS growth is s_entity_draw_group_override[RSDK_ENTITY_COUNT]
 * (uint8) which grows +16 bytes (128 -> 144). Does not approach the
 * 0x060C0000 SGL work-area floor.
 *
 * Phase 2.4g.3 (Task #153): registers PlaneSwitch (106 GHZ Act 1 overflow
 * instances). IB 18 + BM 22 + PS 106 = 146 instances must fit the temp
 * budget. Bumped 0x30 -> 0xA0 (48 -> 160) to hold all three classes'
 * overflow with margin (160 - 146 = 14 spare). Cost analysis (object.c
 * is the authority): the entity table is jo_malloc'd in
 * rsdk_object_init (object.c:82-85), so the +112 added slots (128 -> 240
 * ENTITY_COUNT) cost +112 * 256 B = +28 KB HEAP, NOT BSS. The ONLY
 * ENTITY_COUNT-sized BSS object is s_entity_draw_group_override[ENTITY_COUNT]
 * (uint8, object.c:50) which grows +112 bytes (144 -> 240). +112 BSS bytes
 * is nowhere near the 0x060C0000 SGL work-area floor (Phase 1.15). Measure
 * _end in game.map after build to confirm.
 *
 * Phase 2.4h: registers Chopper (13) + Crabmeat (11) + Batbrain (7) GHZ Act 1
 * badniks. PlaneSwitch's overflow is 100 (6 of its 106 sit at direct slots
 * <80), so the combined GHZ overflow is IB18 + BM22 + PS100 + Chopper13 +
 * Crabmeat11 + Batbrain7 = 171 > 160. Bumped 0xA0 -> 0xC0 (160 -> 192) for
 * 21 spare. Cost (object.c authority): +32 added slots cost +32 * 256 B =
 * +8 KB HEAP (jo_malloc'd table), and only +32 BSS bytes in
 * s_entity_draw_group_override (240 -> 272). _end measured < 0x060C0000
 * after build per gate P3.3c.
 *
 * Phase 2.4-PLAT (Task #155): registers the five GHZ Act 1 platforming
 * classes CollapsingPlatform / Bridge / ForceSpin / BreakableWall /
 * SpinBooster. Their GHZ Scene1.bin overflow (slot >= SCENEENTITY_COUNT
 * = 0x50) measured via qa_phase2_4plat_gate.py P3.3b: CollapsingPlatform
 * 15 + Bridge 11 + ForceSpin 13 + BreakableWall 17 + SpinBooster 4 = 60.
 * Combined GHZ overflow becomes 171 (2.4g+2.4h) + 60 = 231 > 192. Bumped
 * 0xC0 -> 0xF8 (192 -> 248) for 17 spare. Cost (object.c authority): +56
 * added slots cost +56 * 256 B = +14 KB HEAP (jo_malloc'd table), and only
 * +56 BSS bytes in s_entity_draw_group_override (272 -> 328). _end measured
 * < 0x060C0000 after build per gate P3.3c. */
#define RSDK_TEMPENTITY_COUNT   0xF8    /* 248 (Phase 2.4-PLAT: was 0xC0/192; holds combined GHZ overflow 231 with 17 spare) */
#define RSDK_RESERVE_ENTITY_COUNT 0x10  /* 16 reserved (player+camera)  */
#define RSDK_SCENEENTITY_COUNT  0x50    /* 80 (Phase 1.22: was 32, see above) */
#define RSDK_ENTITY_COUNT       (RSDK_RESERVE_ENTITY_COUNT + RSDK_SCENEENTITY_COUNT + RSDK_TEMPENTITY_COUNT)  /* 128 */
#define RSDK_TEMPENTITY_START   (RSDK_ENTITY_COUNT - RSDK_TEMPENTITY_COUNT)
#define RSDK_TYPEGROUP_COUNT    0x104
#define RSDK_FOREACH_STACK_COUNT 0x40   /* 64 nested loops              */
#define RSDK_DRAWGROUP_COUNT    16

/* Phase 1.17 CRITICAL: per-slot entity stride MUST equal the largest
 * Mania-side EntityX subclass size, NOT sizeof(rsdk_entity_t) (64 bytes).
 * Title-scene subclasses with the corrected Game.h::RSDK_ENTITY layout
 * (Phase 1.17 fix) measure:
 *   EntityTitleLogo  ~152 B (132-byte common header + type + 3 anim slots)
 *   EntityTitleSetup ~160 B
 *   EntityTitleSonic ~152 B
 *   EntityTitleBG    ~144 B
 * Picking 256 B gives ample headroom; index via byte stride instead of
 * rsdk_entity_t pointer arithmetic so neighboring slots don't get
 * trampled by Create-body memsets. Memory cost: 80 slots * 256 B = 20 KB
 * (was 80*64=5KB) — fits well inside Phase 1.16's BSS budget. */
#define RSDK_ENTITY_STRIDE      256

/* --- §2.2 ACTIVE states (Object.hpp:60-70) ------------------------- */

enum {
    ACTIVE_NEVER   = 0,
    ACTIVE_ALWAYS  = 1,
    ACTIVE_NORMAL  = 2,
    ACTIVE_PAUSED  = 3,
    ACTIVE_BOUNDS  = 4,
    ACTIVE_XBOUNDS = 5,
    ACTIVE_YBOUNDS = 6,
    ACTIVE_RBOUNDS = 7
};

/* --- §2.3 Entity struct (Object.hpp:107-128) -----------------------
 * Fixed 64-byte layout. Subclassed entity types embed this at offset 0
 * and append type-specific fields. The Saturn port keeps offsets aligned
 * with upstream so per-object data tables transfer 1:1. */

typedef struct {
    int32_t x, y;                       /* 16.16 fixed (Vector2)       */
} rsdk_vec2_t;

typedef struct {
    rsdk_vec2_t position;               /*  +0  (8 B)                  */
    rsdk_vec2_t scale;                  /*  +8                         */
    rsdk_vec2_t velocity;               /* +16                         */
    rsdk_vec2_t update_range;           /* +24                         */
    int32_t  angle;                     /* +32                         */
    int32_t  alpha;                     /* +36                         */
    int32_t  rotation;                  /* +40                         */
    int32_t  ground_vel;                /* +44                         */
    int32_t  zdepth;                    /* +48                         */
    uint16_t group;                     /* +52                         */
    uint16_t class_id;                  /* +54                         */
    uint8_t  in_range;                  /* +56  (bool packed as u8)    */
    uint8_t  is_permanent;              /* +57                         */
    uint8_t  tile_collisions;           /* +58                         */
    uint8_t  interaction;               /* +59                         */
    uint8_t  on_ground;                 /* +60                         */
    uint8_t  active;                    /* +61                         */
    uint8_t  filter;                    /* +62  (REV02 filter)         */
    uint8_t  direction;                 /* +63                         */
    /* Remaining bytes (drawGroup, collisionLayers, collisionPlane,
     * collisionMode, drawFX, inkEffect, visible, onScreen) live in the
     * type-specific subclass region. */
} rsdk_entity_t;

/* --- §2.4 ObjectClass (Object.hpp:148-175) -------------------------
 * Function pointers driving the per-frame loop. NULL = skip that callback
 * for this class. Layout chosen so the Saturn-port struct fits in 48 B. */

typedef void (*rsdk_class_update_fn)(void);
typedef void (*rsdk_class_create_fn)(void *data);
typedef void (*rsdk_class_stage_load_fn)(void);

typedef struct {
    uint32_t                  hash[4];          /* MD5 of class name    */
    char                      name[24];         /* debug + lookup       */
    rsdk_class_update_fn      update;
    rsdk_class_update_fn      late_update;
    rsdk_class_update_fn      static_update;
    rsdk_class_update_fn      draw;
    rsdk_class_create_fn      create;
    rsdk_class_stage_load_fn  stage_load;
    void                     *static_vars;      /* scene-scoped state   */
    uint16_t                  entity_class_size;
    uint16_t                  static_class_size;
} rsdk_object_class_t;

/* --- Saturn-side global registries (defined in object.c) ----------- */

extern rsdk_object_class_t   g_rsdk_classes[RSDK_OBJECT_COUNT];
/* Phase 1.17: entity table is now a flat byte buffer striped by
 * RSDK_ENTITY_STRIDE so per-slot subclass payloads don't trample
 * neighbors. Access via rsdk_entity_at(slot). The legacy global
 * `g_rsdk_entities` is preserved for backward compat but is just the
 * byte-buffer base cast to rsdk_entity_t*. Iteration MUST use
 * rsdk_entity_at(s) — `g_rsdk_entities[s]` would skip by 64 bytes
 * (sizeof rsdk_entity_t) and visit the wrong slot. */
extern uint8_t              *g_rsdk_entity_buf;          /* base byte ptr   */
extern rsdk_entity_t        *g_rsdk_entities;            /* alias of buf — DO NOT INDEX */
extern uint16_t              g_rsdk_class_count;
extern uint16_t              g_rsdk_create_slot;        /* temp ring head  */
extern uint16_t              g_rsdk_scene_camera_count; /* per-frame       */

static inline rsdk_entity_t *rsdk_entity_at(uint16_t slot)
{
    if (!g_rsdk_entity_buf) return (rsdk_entity_t *)0;
    return (rsdk_entity_t *)(g_rsdk_entity_buf + (uint32_t)slot * RSDK_ENTITY_STRIDE);
}

/* === Public API ===================================================== */

/* Allocates the entity table from jo's heap. Returns true on success.
 * Call once at boot before any CreateEntity call. */
bool rsdk_object_init(void);
void rsdk_object_shutdown(void);

/* Phase 2.4g.1 (Task #153) — zero the per-scene + temp slot regions so a
 * scene transition does not carry stale entities into the new scene. Call
 * after rsdk_unload_scene and before spawning the new scene's entities.
 * Leaves the reserve region (persistent globals) intact. */
void rsdk_object_clear_scene_slots(void);

/* Register an object class by name. Returns the class index (0..class_count-1)
 * or -1 if the registry is full. The class's update/draw/etc. callbacks may
 * be NULL — skipped during the frame loop. */
int rsdk_object_register(const char *name,
                         uint16_t entity_class_size,
                         rsdk_class_update_fn update,
                         rsdk_class_update_fn draw,
                         rsdk_class_create_fn create,
                         rsdk_class_stage_load_fn stage_load);

/* Phase 1.1 extension: register an object class with the FULL upstream
 * callback set (update, late_update, static_update, draw, create,
 * stage_load) AND a per-class static_vars block.
 *
 * Authoritative source: _RSDKv5_Scene_Object.cpp::RegisterObject /
 * RegisterObjectStatic — the engine reserves a `static_class_size` block
 * for the class's globals (per-scene state shared across all instances:
 * TitleSetup->aniFrames, TitleSetup->sfxRing, ...).
 *
 * Saturn-side: jo_malloc'd at registration time, freed at shutdown. The
 * returned `*out_static_vars` is the class-scoped globals pointer that
 * the decomp port stores (e.g. `TitleSetup = (ObjectTitleSetup *)svars;`).
 *
 * Returns the class index, or -1 if the registry is full / allocation
 * failed. */
int rsdk_object_register_ex(const char *name,
                            uint16_t entity_class_size,
                            uint16_t static_class_size,
                            rsdk_class_update_fn update,
                            rsdk_class_update_fn late_update,
                            rsdk_class_update_fn static_update,
                            rsdk_class_update_fn draw,
                            rsdk_class_create_fn create,
                            rsdk_class_stage_load_fn stage_load,
                            void **out_static_vars);

/* Find a registered class by name. Returns -1 if absent. */
int rsdk_object_find_class(const char *name);

/* Set the per-class drawGroup priority. Used by classes whose Create
 * callback assigns `self->drawGroup = N` (decomp convention). For Phase
 * 1.1 the class default is stored alongside the class; per-entity
 * drawGroup overrides land in Phase 1.2 with the full Drawing.cpp port.
 *
 * drawGroup range: 0..15 (RSDK_DRAWGROUP_COUNT). */
void rsdk_object_set_class_draw_group(uint16_t class_id, uint8_t draw_group);

/* Saturn-side: returns the per-entity drawGroup, default 2 if unset. */
uint8_t rsdk_object_get_entity_draw_group(const rsdk_entity_t *ent);
void    rsdk_object_set_entity_draw_group(rsdk_entity_t *ent, uint8_t draw_group);

/* Saturn-side currently-executing entity context. Set by the frame loop
 * before invoking each entity's update/draw callback so the decomp
 * `RSDK_THIS(Class)` macro can resolve `self` from the global. */
extern rsdk_entity_t *g_rsdk_current_entity;
extern uint16_t        g_rsdk_current_entity_slot;

/* RSDK_THIS macro — used by every decomp .c file. Mirrors the upstream
 * macro convention. Defines `self` as the typed pointer to the currently-
 * executing entity. Per-class entity structs embed `rsdk_entity_t` at
 * offset 0 so the cast is safe. */
#define RSDK_THIS(Class) Entity##Class *self = (Entity##Class *)g_rsdk_current_entity

/* --- §2.6 Lifecycle API -------------------------------------------- *
 * Create an entity at the next-available slot in the temp ring (or at
 * the explicit reserved slot via *Slot variants). Calls the class's
 * create() callback with `data` after zeroing the entity record. */
rsdk_entity_t *rsdk_create_entity(uint16_t class_id, void *data,
                                  int32_t x_fixed, int32_t y_fixed);
void           rsdk_reset_entity (rsdk_entity_t *ent, uint16_t class_id,
                                  void *data);
void           rsdk_reset_entity_slot(uint16_t slot, uint16_t class_id,
                                      void *data);
void           rsdk_copy_entity  (rsdk_entity_t *dst, const rsdk_entity_t *src,
                                  bool clear_src);

/* --- §2.7 Foreach iterators ---------------------------------------- *
 * Walk all entities of class `group`. On first call pass *cursor = -1;
 * function advances *cursor and returns the next matching entity or
 * NULL when the walk ends. Reentrant: caller's cursor variable is the
 * iteration state, not shared with other walks. */
rsdk_entity_t *rsdk_get_active_entity(uint16_t group, int *cursor);
rsdk_entity_t *rsdk_get_all_entity   (uint16_t class_id, int *cursor);

/* --- §2.5 Frame loop ----------------------------------------------- *
 * Caller invokes once per game tick (60 Hz). Internally walks all entities
 * once for update + once for late_update; bounds-active recompute drives
 * each entity's in_range flag from the cameras the Saturn port sets up
 * (jo's camera state mirror lives in src/main.c::g_cam_x/g_cam_y). */
void rsdk_object_tick(void);

/* Sync the entity-engine camera used by rsdk_entity_in_bounds (ACTIVE_BOUNDS
 * / *BOUNDS active states). Pass the camera CENTER in Q16.16 world coords and
 * the half-screen extents as the offset (160<<16, 112<<16 for 320x224). The
 * GHZ runtime (src/mania/Game.c::mania_ghz_tick_and_draw) calls this each
 * tick so spawned GHZ entities at nonzero world coords pass the bounds gate
 * and receive Update(). Without it s_cam_x_fixed stays 0 and every entity
 * past update_range of world origin is permanently culled. */
void rsdk_object_set_camera(int32_t x_fx, int32_t y_fx,
                            int32_t off_x_fx, int32_t off_y_fx);

/* Called from the Saturn frame-render pass after physics finalizes.
 * Walks the drawGroup queues built during tick() and invokes each
 * class->draw() in priority order. */
void rsdk_object_draw_all(void);

/* --- BOUNDS check (paraphrased Object.cpp:553-598) ----------------- *
 * Public so the per-class update can re-check bounds mid-frame if the
 * entity teleports. Most callers don't need this. */
bool rsdk_entity_in_bounds(const rsdk_entity_t *ent, int32_t cam_x_fixed,
                           int32_t cam_y_fixed, int32_t cam_offset_x,
                           int32_t cam_offset_y);

#endif /* RSDK_OBJECT_H */
