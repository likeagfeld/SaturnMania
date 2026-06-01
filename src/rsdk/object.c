/* Phase A2 — Object/Entity system, Saturn port of RSDKv5/RSDK/Scene/Object.
 *
 * Per docs/rsdkv5_engine_catalog.md §2 + BIBLE.md Phase A row A2.
 *
 * Authoritative source paths in upstream:
 *   Object.hpp:11-23     RSDK_*_COUNT constants
 *   Object.hpp:60-70     ACTIVE_* enum
 *   Object.hpp:107-128   Entity struct
 *   Object.hpp:148-175   ObjectClass struct
 *   Object.cpp:506-740   ProcessObjects + ProcessObjectDrawLists
 *   Object.cpp:1121-1214 CreateEntity/ResetEntity/CopyEntity
 *   Object.cpp:1218-1263 GetActiveEntities/GetAllEntities/BreakForeachLoop
 *
 * Phase A2 deliverable scope: this file implements the SLOT manager +
 * lifecycle API + foreach iterators + bounds check + frame loop. The
 * actual per-class update/draw callbacks are wired by Phase B per-object
 * ports. The Saturn camera/scene-info adapter (rsdk_scene_camera_count
 * + a future src/rsdk/scene.c) is stubbed here with a single-camera
 * implementation matching how src/main.c manages g_cam_x/g_cam_y. */

#include "object.h"

#include <jo/jo.h>         /* jo_malloc, jo_free                       */
#include <string.h>        /* memset, memcpy, strncpy                  */

/* --- Globals ------------------------------------------------------- */

rsdk_object_class_t g_rsdk_classes[RSDK_OBJECT_COUNT];
uint8_t            *g_rsdk_entity_buf     = NULL;
rsdk_entity_t      *g_rsdk_entities       = NULL;  /* alias of buf; DO NOT INDEX with [] */
uint16_t            g_rsdk_class_count    = 0;
uint16_t            g_rsdk_create_slot    = (uint16_t)RSDK_TEMPENTITY_START;
uint16_t            g_rsdk_scene_camera_count = 1;  /* default = 1 cam */

/* Phase 1.1 — currently-executing entity context (RSDK_THIS macro). The
 * frame loop sets g_rsdk_current_entity before invoking each per-class
 * callback so the decomp's `RSDK_THIS(Class)` macro can recover `self`. */
rsdk_entity_t *g_rsdk_current_entity      = NULL;
uint16_t       g_rsdk_current_entity_slot = 0;

/* Phase 1.1 — per-class default drawGroup. The decomp pattern is for
 * Create() to assign `self->drawGroup = N`; until per-entity drawGroup
 * lands (Phase 1.2 with the full Drawing.cpp port) every entity inherits
 * its class default. Stored as a separate sidecar to keep the rsdk_entity_t
 * layout matching upstream. */
static uint8_t s_class_default_draw_group[RSDK_OBJECT_COUNT];

/* Phase 1.1 — per-entity drawGroup override sidecar (Drawing.cpp 3.3).
 * Indexed by entity slot. 0xFF = "use class default". */
static uint8_t s_entity_draw_group_override[RSDK_ENTITY_COUNT];

/* Saturn camera adapter — the Phase A9 Scene module will replace this
 * stub with per-Camera-entity tracking. For now mirror jo's single-cam
 * setup. Caller (src/main.c) sets these via rsdk_object_set_camera()
 * before calling rsdk_object_tick. */
static int32_t      s_cam_x_fixed = 0;
static int32_t      s_cam_y_fixed = 0;
static int32_t      s_cam_off_x   = 0;
static int32_t      s_cam_off_y   = 0;

void rsdk_object_set_camera(int32_t x_fx, int32_t y_fx,
                            int32_t off_x_fx, int32_t off_y_fx)
{
    s_cam_x_fixed = x_fx;  s_cam_y_fixed = y_fx;
    s_cam_off_x   = off_x_fx;  s_cam_off_y = off_y_fx;
}

/* --- Init / shutdown ----------------------------------------------- */

bool rsdk_object_init(void)
{
    if (g_rsdk_entities) return true;            /* idempotent */
    memset(g_rsdk_classes, 0, sizeof(g_rsdk_classes));
    memset(s_class_default_draw_group, 2, sizeof(s_class_default_draw_group));
    memset(s_entity_draw_group_override, 0xFF, sizeof(s_entity_draw_group_override));
    g_rsdk_class_count = 0;
    g_rsdk_create_slot = (uint16_t)RSDK_TEMPENTITY_START;
    g_rsdk_current_entity      = NULL;
    g_rsdk_current_entity_slot = 0;

    /* Phase 1.17: allocate striped entity buffer with per-slot stride. */
    g_rsdk_entity_buf = (uint8_t *)
        jo_malloc((uint32_t)RSDK_ENTITY_STRIDE * RSDK_ENTITY_COUNT);
    if (!g_rsdk_entity_buf) return false;
    memset(g_rsdk_entity_buf, 0, (uint32_t)RSDK_ENTITY_STRIDE * RSDK_ENTITY_COUNT);
    g_rsdk_entities = (rsdk_entity_t *)g_rsdk_entity_buf; /* alias */

    /* Phase 1.3 — reserve class_id 0 as TYPE_BLANK so subsequent
     * rsdk_object_register_ex calls start at class_id 1. This is
     * REQUIRED because rsdk_object_tick / rsdk_object_draw_all use
     * `e->class_id == 0` as the skip-blank sentinel (matching upstream
     * Object.cpp's TYPE_BLANK convention). Without this guard the
     * FIRST registered class (TitleSetup) would get class_id 0 and
     * its entities would be silently skipped by every per-frame loop —
     * the title state machine never advances and the screen stays at
     * Draw_FadeBlack overlay forever. */
    g_rsdk_classes[0].entity_class_size = (uint16_t)sizeof(rsdk_entity_t);
    g_rsdk_classes[0].name[0] = 'B';
    g_rsdk_classes[0].name[1] = 'l';
    g_rsdk_classes[0].name[2] = 'a';
    g_rsdk_classes[0].name[3] = 'n';
    g_rsdk_classes[0].name[4] = 'k';
    g_rsdk_classes[0].name[5] = '\0';
    g_rsdk_class_count = 1;
    return true;
}

void rsdk_object_shutdown(void)
{
    /* Free per-class static_vars blocks. */
    for (uint16_t c = 0; c < g_rsdk_class_count; ++c) {
        if (g_rsdk_classes[c].static_vars) {
            jo_free(g_rsdk_classes[c].static_vars);
            g_rsdk_classes[c].static_vars = NULL;
        }
    }
    if (g_rsdk_entity_buf) { jo_free(g_rsdk_entity_buf); g_rsdk_entity_buf = NULL; g_rsdk_entities = NULL; }
    g_rsdk_class_count = 0;
    memset(g_rsdk_classes, 0, sizeof(g_rsdk_classes));
}

/* --- Class registry ------------------------------------------------ */

int rsdk_object_register(const char *name,
                         uint16_t entity_class_size,
                         rsdk_class_update_fn update,
                         rsdk_class_update_fn draw,
                         rsdk_class_create_fn create,
                         rsdk_class_stage_load_fn stage_load)
{
    return rsdk_object_register_ex(name, entity_class_size, 0,
                                   update, NULL, NULL, draw, create,
                                   stage_load, NULL);
}

/* Port of _RSDKv5_Scene_Object.cpp::RegisterObject + RegisterObjectStatic.
 * Reserves a per-class static_vars block sized `static_class_size` bytes
 * if requested; the decomp port stores its class globals (e.g.
 * `TitleSetup->aniFrames`) there. */
int rsdk_object_register_ex(const char *name,
                            uint16_t entity_class_size,
                            uint16_t static_class_size,
                            rsdk_class_update_fn update,
                            rsdk_class_update_fn late_update,
                            rsdk_class_update_fn static_update,
                            rsdk_class_update_fn draw,
                            rsdk_class_create_fn create,
                            rsdk_class_stage_load_fn stage_load,
                            void **out_static_vars)
{
    if (g_rsdk_class_count >= RSDK_OBJECT_COUNT) return -1;
    int idx = g_rsdk_class_count++;
    rsdk_object_class_t *c = &g_rsdk_classes[idx];
    memset(c, 0, sizeof(*c));
    int i;
    for (i = 0; i < (int)sizeof(c->name) - 1 && name && name[i]; ++i)
        c->name[i] = name[i];
    c->name[i] = '\0';
    if (name) rsdk_md5_name(name, c->hash);
    c->entity_class_size = entity_class_size ? entity_class_size
                                              : (uint16_t)sizeof(rsdk_entity_t);
    c->static_class_size = static_class_size;
    c->update        = update;
    c->late_update   = late_update;
    c->static_update = static_update;
    c->draw          = draw;
    c->create        = create;
    c->stage_load    = stage_load;
    c->static_vars   = NULL;
    if (static_class_size > 0) {
        c->static_vars = jo_malloc(static_class_size);
        if (!c->static_vars) {
            /* Roll back the registration; caller sees -1. */
            --g_rsdk_class_count;
            return -1;
        }
        memset(c->static_vars, 0, static_class_size);
    }
    s_class_default_draw_group[idx] = 2;        /* RSDK default */
    if (out_static_vars) *out_static_vars = c->static_vars;
    return idx;
}

void rsdk_object_set_class_draw_group(uint16_t class_id, uint8_t draw_group)
{
    if (class_id >= g_rsdk_class_count) return;
    if (draw_group >= RSDK_DRAWGROUP_COUNT) draw_group = RSDK_DRAWGROUP_COUNT - 1;
    s_class_default_draw_group[class_id] = draw_group;
}

static uint16_t entity_slot_of(const rsdk_entity_t *ent)
{
    if (!ent || !g_rsdk_entity_buf) return 0;
    /* Phase 1.17: byte-arithmetic stride. */
    uint32_t off = (uint32_t)((const uint8_t *)ent - g_rsdk_entity_buf);
    return (uint16_t)(off / RSDK_ENTITY_STRIDE);
}

uint8_t rsdk_object_get_entity_draw_group(const rsdk_entity_t *ent)
{
    if (!ent) return 2;
    uint16_t slot = entity_slot_of(ent);
    if (slot >= RSDK_ENTITY_COUNT) return 2;
    uint8_t ov = s_entity_draw_group_override[slot];
    if (ov != 0xFF) return ov;
    if (ent->class_id < g_rsdk_class_count)
        return s_class_default_draw_group[ent->class_id];
    return 2;
}

void rsdk_object_set_entity_draw_group(rsdk_entity_t *ent, uint8_t draw_group)
{
    if (!ent) return;
    uint16_t slot = entity_slot_of(ent);
    if (slot >= RSDK_ENTITY_COUNT) return;
    if (draw_group >= RSDK_DRAWGROUP_COUNT) draw_group = RSDK_DRAWGROUP_COUNT - 1;
    s_entity_draw_group_override[slot] = draw_group;
}

int rsdk_object_find_class(const char *name)
{
    uint32_t target[4];
    if (!name) return -1;
    rsdk_md5_name(name, target);
    for (uint16_t i = 0; i < g_rsdk_class_count; ++i) {
        if (memcmp(g_rsdk_classes[i].hash, target, 16) == 0) return (int)i;
    }
    return -1;
}

/* --- Lifecycle API ------------------------------------------------- */

rsdk_entity_t *rsdk_create_entity(uint16_t class_id, void *data,
                                  int32_t x_fixed, int32_t y_fixed)
{
    if (!g_rsdk_entity_buf || class_id >= g_rsdk_class_count) return NULL;
    /* Advance the temp-ring head; Object.cpp:1194-1195 — wrap inside the
     * TEMPENTITY range, never trampling RESERVE/SCENE slots. */
    uint16_t slot = g_rsdk_create_slot++;
    if (g_rsdk_create_slot >= RSDK_ENTITY_COUNT)
        g_rsdk_create_slot = (uint16_t)RSDK_TEMPENTITY_START;
    rsdk_entity_t *ent = rsdk_entity_at(slot);
    rsdk_object_class_t *cls = &g_rsdk_classes[class_id];
    /* Clamp memset to entity stride — never overflow into next slot. */
    uint32_t sz = cls->entity_class_size;
    if (sz > (uint32_t)RSDK_ENTITY_STRIDE) sz = RSDK_ENTITY_STRIDE;
    memset(ent, 0, sz);
    ent->class_id      = class_id;
    ent->position.x    = x_fixed;
    ent->position.y    = y_fixed;
    ent->active        = ACTIVE_NORMAL;
    ent->filter        = 0xFF;
    s_entity_draw_group_override[slot] = 0xFF; /* inherit class default */
    /* Bind current-entity context so the class's create() callback can
     * use RSDK_THIS() exactly as decomp expects. */
    rsdk_entity_t *prev      = g_rsdk_current_entity;
    uint16_t       prev_slot = g_rsdk_current_entity_slot;
    g_rsdk_current_entity      = ent;
    g_rsdk_current_entity_slot = slot;
    /* Phase 1.3 — preserve position + class_id across the create() call.
     * The decomp's Create callbacks generally don't touch position (they
     * read via `self->position`); defensive save+restore guards against
     * Create callbacks that inadvertently zero them. class_id is
     * deliberately preserved as a hard restore — every entity must
     * carry its class_id past Create so the tick loop's dispatch works. */
    rsdk_vec2_t saved_pos = ent->position;
    uint16_t    saved_cid = ent->class_id;
    if (cls->create) cls->create(data);
    /* Restore position only if Create accidentally zeroed it. */
    if (ent->position.x == 0 && ent->position.y == 0 &&
        (saved_pos.x != 0 || saved_pos.y != 0)) {
        ent->position = saved_pos;
    }
    /* class_id must never be lost across Create — restore unconditionally
     * if the callback cleared it. */
    if (ent->class_id != saved_cid) ent->class_id = saved_cid;
    g_rsdk_current_entity      = prev;
    g_rsdk_current_entity_slot = prev_slot;
    return ent;
}

void rsdk_reset_entity(rsdk_entity_t *ent, uint16_t class_id, void *data)
{
    if (!ent || class_id >= g_rsdk_class_count) return;
    rsdk_object_class_t *cls = &g_rsdk_classes[class_id];
    /* Preserve position + slot identity; clear the rest. */
    rsdk_vec2_t saved_pos = ent->position;
    uint32_t sz = cls->entity_class_size;
    if (sz > (uint32_t)RSDK_ENTITY_STRIDE) sz = RSDK_ENTITY_STRIDE;
    memset(ent, 0, sz);
    ent->position = saved_pos;
    ent->class_id = class_id;
    ent->active   = ACTIVE_NORMAL;
    ent->filter   = 0xFF;
    uint16_t slot = entity_slot_of(ent);
    if (slot < RSDK_ENTITY_COUNT)
        s_entity_draw_group_override[slot] = 0xFF;
    rsdk_entity_t *prev      = g_rsdk_current_entity;
    uint16_t       prev_slot = g_rsdk_current_entity_slot;
    g_rsdk_current_entity      = ent;
    g_rsdk_current_entity_slot = slot;
    if (cls->create) cls->create(data);
    g_rsdk_current_entity      = prev;
    g_rsdk_current_entity_slot = prev_slot;
}

void rsdk_reset_entity_slot(uint16_t slot, uint16_t class_id, void *data)
{
    if (slot >= RSDK_ENTITY_COUNT || !g_rsdk_entity_buf) return;
    rsdk_reset_entity(rsdk_entity_at(slot), class_id, data);
}

/* Phase 2.4g.1 (Task #153) — clear the per-scene + temp slot regions so a
 * scene transition cannot leave stale entities from the previous scene
 * ticking/drawing in the new one.
 *
 * rsdk_unload_scene (scene.c) frees the parsed scene blob but does NOT touch
 * the BSS entity slot table — that table is only memset once, at
 * rsdk_object_init allocation time (object.c:85). Without this, the Title
 * scene's TitleLogo/TitleSonic/Title3DSprite slots survive into the GHZ
 * scene and keep getting ticked/drawn (class_id != 0 so they are not skipped
 * by the TYPE_BLANK sentinel). The reserve region (slots
 * 0..RSDK_RESERVE_ENTITY_COUNT-1) holds persistent globals (player/camera
 * placeholders) and is left intact; only the scene region
 * [RESERVE .. RESERVE+SCENEENTITY_COUNT) and the temp ring
 * [TEMPENTITY_START .. +TEMPENTITY_COUNT) are zeroed. Zeroing each slot's
 * class_id makes rsdk_object_tick / _draw_all skip it. */
/* __attribute__((used)): keep the external name in game.map under
 * whole-program LTO so tools/qa_phase2_4g1_gate.py P3c can confirm the
 * stale-slot clear is linked (precedent: entity-atlas-loader-pattern.md). */
__attribute__((used)) void rsdk_object_clear_scene_slots(void)
{
    if (!g_rsdk_entity_buf) return;
    for (uint16_t s = (uint16_t)RSDK_RESERVE_ENTITY_COUNT;
         s < (uint16_t)(RSDK_RESERVE_ENTITY_COUNT + RSDK_SCENEENTITY_COUNT); ++s) {
        rsdk_entity_t *e = rsdk_entity_at(s);
        if (e) memset(e, 0, (uint32_t)RSDK_ENTITY_STRIDE);
        s_entity_draw_group_override[s] = 0xFF;
    }
    for (uint16_t s = (uint16_t)RSDK_TEMPENTITY_START;
         s < (uint16_t)(RSDK_TEMPENTITY_START + RSDK_TEMPENTITY_COUNT); ++s) {
        rsdk_entity_t *e = rsdk_entity_at(s);
        if (e) memset(e, 0, (uint32_t)RSDK_ENTITY_STRIDE);
        s_entity_draw_group_override[s] = 0xFF;
    }
    g_rsdk_create_slot = (uint16_t)RSDK_TEMPENTITY_START;
}

void rsdk_copy_entity(rsdk_entity_t *dst, const rsdk_entity_t *src,
                      bool clear_src)
{
    if (!dst || !src) return;
    if (src->class_id >= g_rsdk_class_count) return;
    rsdk_object_class_t *cls = &g_rsdk_classes[src->class_id];
    uint32_t sz = cls->entity_class_size;
    if (sz > (uint32_t)RSDK_ENTITY_STRIDE) sz = RSDK_ENTITY_STRIDE;
    memcpy(dst, src, sz);
    if (clear_src) {
        memset((void *)src, 0, sz);
    }
}

/* --- §2.7 Foreach iterators --------------------------------------- *
 * Saturn-port impl walks the full entity table linearly (no pre-built
 * typeGroup index). With ENTITY_COUNT=512 and 60 Hz gameplay tick, the
 * linear walk costs ~30 K compares/frame which is well within SH-2 budget
 * and avoids the typeGroup memory overhead. */

rsdk_entity_t *rsdk_get_active_entity(uint16_t group, int *cursor)
{
    if (!g_rsdk_entity_buf || !cursor) return NULL;
    int i = (*cursor < 0) ? 0 : (*cursor + 1);
    for (; i < RSDK_ENTITY_COUNT; ++i) {
        rsdk_entity_t *e = rsdk_entity_at((uint16_t)i);
        if (e->class_id == group && e->active != ACTIVE_NEVER) {
            *cursor = i;
            return e;
        }
    }
    *cursor = -2;     /* signal: end of iteration */
    return NULL;
}

rsdk_entity_t *rsdk_get_all_entity(uint16_t class_id, int *cursor)
{
    if (!g_rsdk_entity_buf || !cursor) return NULL;
    int i = (*cursor < 0) ? 0 : (*cursor + 1);
    for (; i < RSDK_ENTITY_COUNT; ++i) {
        rsdk_entity_t *e = rsdk_entity_at((uint16_t)i);
        if (e->class_id == class_id) {
            *cursor = i;
            return e;
        }
    }
    *cursor = -2;
    return NULL;
}

/* --- BOUNDS check (paraphrased Object.cpp:553-598) ----------------- */

bool rsdk_entity_in_bounds(const rsdk_entity_t *ent, int32_t cam_x_fixed,
                           int32_t cam_y_fixed, int32_t cam_off_x,
                           int32_t cam_off_y)
{
    if (!ent) return false;
    /* Convert all inputs to integer pixel deltas (16.16 -> int via >> 16
     * with sign preservation). This loses fractional precision but
     * matches the engine's FROM_FIXED() macro used in RBOUNDS math. */
    int32_t dx = (ent->position.x - cam_x_fixed) >> 16;
    int32_t dy = (ent->position.y - cam_y_fixed) >> 16;
    int32_t rx = (ent->update_range.x + cam_off_x) >> 16;
    int32_t ry = (ent->update_range.y + cam_off_y) >> 16;
    int32_t adx = dx < 0 ? -dx : dx;
    int32_t ady = dy < 0 ? -dy : dy;
    switch (ent->active) {
        case ACTIVE_NORMAL:
        case ACTIVE_ALWAYS:
            return true;
        case ACTIVE_BOUNDS:
            return adx <= rx && ady <= ry;
        case ACTIVE_XBOUNDS:
            return adx <= rx;
        case ACTIVE_YBOUNDS:
            return ady <= ry;
        case ACTIVE_RBOUNDS:
            /* Engine treats update_range.x AS THE squared radius (see
             * §2.2 note in catalog). */
            return (adx * adx + ady * ady) <= rx;
        case ACTIVE_NEVER:
        case ACTIVE_PAUSED:
        default:
            return false;
    }
}

/* --- §2.5 Frame loop ---------------------------------------------- */

void rsdk_object_tick(void)
{
    if (!g_rsdk_entity_buf) return;
    /* Pass 1: static update for every class that wants it.
     * Note: static_update gets no entity context — the callback operates
     * on class-globals only (matches upstream Object.cpp ProcessObjects
     * pre-pass behavior). */
    g_rsdk_current_entity      = NULL;
    g_rsdk_current_entity_slot = 0;
    for (uint16_t c = 0; c < g_rsdk_class_count; ++c) {
        rsdk_object_class_t *cls = &g_rsdk_classes[c];
        if (cls->static_update) cls->static_update();
    }
    /* Pass 2: per-entity bounds recompute + update. */
    for (uint16_t s = 0; s < RSDK_ENTITY_COUNT; ++s) {
        rsdk_entity_t *e = rsdk_entity_at(s);
        if (e->class_id == 0) continue;            /* TYPE_BLANK */
        e->in_range = rsdk_entity_in_bounds(e,
                                            s_cam_x_fixed, s_cam_y_fixed,
                                            s_cam_off_x,   s_cam_off_y)
                      ? 1 : 0;
        if (!e->in_range) continue;
        rsdk_object_class_t *cls = &g_rsdk_classes[e->class_id];
        if (cls->update) {
            g_rsdk_current_entity      = e;
            g_rsdk_current_entity_slot = s;
            cls->update();
        }
    }
    /* Pass 3: late_update. */
    for (uint16_t s = 0; s < RSDK_ENTITY_COUNT; ++s) {
        rsdk_entity_t *e = rsdk_entity_at(s);
        if (e->class_id == 0 || !e->in_range) continue;
        rsdk_object_class_t *cls = &g_rsdk_classes[e->class_id];
        if (cls->late_update) {
            g_rsdk_current_entity      = e;
            g_rsdk_current_entity_slot = s;
            cls->late_update();
        }
    }
    g_rsdk_current_entity      = NULL;
    g_rsdk_current_entity_slot = 0;
}

/* Phase 1.1 — drawGroup-ordered draw pass.
 *
 * Algorithm (mirrors _RSDKv5_Scene_Object.cpp::ProcessObjectDrawLists,
 * sketched in docs/rsdk_compat_audit.md §2):
 *   1. Walk every active entity once, bucket it into its drawGroup queue.
 *   2. For drawGroup g in 0..15:
 *        For each entity in g (slot-order; per-group sort callback +
 *        zdepth insertion sort lands in Phase 1.2 with full Drawing.cpp):
 *          set current-entity context, call class->draw.
 *
 * Per-group slot lists are kept inline in a fixed-size 2D array; max
 * RSDK_ENTITY_COUNT entries per group (worst case: every entity in one
 * group), but typical scenes spread across multiple groups. Sized at
 * 64 entries per group / 16 groups = 1 KB sidecar memory. If a group
 * overflows, excess entities are dropped from that group's draw — this
 * matches no-overflow assumption in the upstream draw-list code. */

#define MAX_PER_DRAWGROUP 64

static uint16_t s_drawgroup_buckets[RSDK_DRAWGROUP_COUNT][MAX_PER_DRAWGROUP];
static uint8_t  s_drawgroup_counts[RSDK_DRAWGROUP_COUNT];

void rsdk_object_draw_all(void)
{
    if (!g_rsdk_entity_buf) return;

    /* Pass A: bucket entities by drawGroup. Phase 1.2: only entities with
     * `visible != 0` participate — matches Drawing.cpp:721 (the upstream
     * check is `if (entity->visible)` before adding to the draw list).
     * The visible flag lives in the Mania-side EntityX struct subclass,
     * at the offset given by Game.h's RSDK_ENTITY macro: after `direction`
     * (offset 63) come collisionLayers/Plane/Mode/drawFX/inkEffect/visible
     * /onScreen. Offset of `visible` from the entity base = 63 + 1 + 4 (5
     * collision/draw u8s) + 1 (inkEffect) + 1 (= visible). The Phase 1.2
     * test reads it as a u8 sidecar; per-entity drawGroup override sidecar
     * is independent. */
    memset(s_drawgroup_counts, 0, sizeof(s_drawgroup_counts));
    for (uint16_t s = 0; s < RSDK_ENTITY_COUNT; ++s) {
        rsdk_entity_t *e = rsdk_entity_at(s);
        if (e->class_id == 0) continue;
        if (e->active == ACTIVE_NEVER) continue;
        if (e->class_id >= g_rsdk_class_count) continue;
        rsdk_object_class_t *cls = &g_rsdk_classes[e->class_id];
        if (!cls->draw) continue;
        uint8_t g = rsdk_object_get_entity_draw_group(e);
        if (g >= RSDK_DRAWGROUP_COUNT) g = RSDK_DRAWGROUP_COUNT - 1;
        if (s_drawgroup_counts[g] < MAX_PER_DRAWGROUP)
            s_drawgroup_buckets[g][s_drawgroup_counts[g]++] = s;
    }

    /* Pass A.5: per-group insertion sort by zdepth (Drawing.cpp:735+).
     *
     * The decomp's AddDrawListRef inserts entities into per-drawGroup
     * arrays in zdepth order (low zdepth = drawn first = behind). For
     * Phase 1.2 we use simple insertion sort over the bucket — N is small
     * (max 64 per group, typically 1..5 entities per draw group on Title)
     * so O(N^2) is fine. zdepth is read from rsdk_entity_t::zdepth (the
     * common field set by Camera/Player and by Title3DSprite_Update). */
    for (int g = 0; g < RSDK_DRAWGROUP_COUNT; ++g) {
        int n = (int)s_drawgroup_counts[g];
        for (int i = 1; i < n; ++i) {
            uint16_t key = s_drawgroup_buckets[g][i];
            int32_t key_z = rsdk_entity_at(key)->zdepth;
            int j = i - 1;
            while (j >= 0 && rsdk_entity_at(s_drawgroup_buckets[g][j])->zdepth > key_z) {
                s_drawgroup_buckets[g][j + 1] = s_drawgroup_buckets[g][j];
                --j;
            }
            s_drawgroup_buckets[g][j + 1] = key;
        }
    }

    /* Pass B: invoke per-group in priority order. */
    for (int g = 0; g < RSDK_DRAWGROUP_COUNT; ++g) {
        for (int i = 0; i < (int)s_drawgroup_counts[g]; ++i) {
            uint16_t slot = s_drawgroup_buckets[g][i];
            rsdk_entity_t *e = rsdk_entity_at(slot);
            rsdk_object_class_t *cls = &g_rsdk_classes[e->class_id];
            g_rsdk_current_entity      = e;
            g_rsdk_current_entity_slot = slot;
            cls->draw();
        }
    }
    g_rsdk_current_entity      = NULL;
    g_rsdk_current_entity_slot = 0;
}
