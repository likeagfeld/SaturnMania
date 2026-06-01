/* Phase 1.1 — Scene module (Saturn implementation).
 *
 * Port reference (NOT reproduced verbatim — re-implemented per the API
 * contract in the header):
 *   tools/_decomp_raw/_RSDKv5_Scene.cpp:461-665   LoadSceneFile (layer
 *                                                  + object + entity
 *                                                  arrays).
 *   tools/_decomp_raw/_RSDKv5_Scene.cpp:670-740   InitObjects /
 *                                                  InitEntities (per-
 *                                                  entity create-call
 *                                                  pass with class hash
 *                                                  resolution).
 *
 * The heavy parsing work already lives in src/rsdk/storage.c:
 * `rsdk_scene_load(filename, &scene_t)` reads the file + populates the
 * scene_t with layers + class hashes + entity table. This module
 * orchestrates: path-build -> storage.load -> hash-resolve classes ->
 * per-entity rsdk_create_entity -> per-class stage_load callback. */

#include "scene.h"
#include "object.h"
#include "tilelayer.h"
#include "drawing.h"

#include <jo/jo.h>
#include <string.h>
#include <stddef.h>

/* Saturn-side per-class attribute-fill table.
 *
 * Phase 1.2: hard-coded for the Title-scene classes. The decomp uses
 * `RSDK_EDITABLE_VAR(Class, VAR_X, fieldName)` to register per-class
 * attribute names + types. For the 5 Title classes the only attribute
 * that matters is `type` (TitleLogo) and `frame` (Title3DSprite). Their
 * offsets in the Mania-side Entity struct:
 *
 *   EntityTitleLogo->type    @ offset 132  (after RSDK_ENTITY)
 *   EntityTitleBG->type      @ offset 132
 *   EntityTitle3DSprite->frame @ offset 132
 *
 * These match the offset of the first field after the RSDK_ENTITY macro
 * fields (which sum to 132 bytes per Game.h's layout: 8+8+8+8+4+4+4+4+4
 * +2+2+4+4+1+4+4+1+1+1+1+1+1+1+1+1+1+1+1 = 132). Phase 2 will generalise
 * this via a runtime attribute-offset table populated when each class
 * registers, but for Phase 1.2 the hard-coded value matches all 5 Title
 * classes since their first subclass field is uniformly at offset 132. */

#define MANIA_RSDK_ENTITY_BASE_BYTES 132

static void fill_first_attribute(const rsdk_scene_t *scene,
                                 const rsdk_scene_entity_t *se,
                                 rsdk_entity_t *ent)
{
    const rsdk_scene_class_t *C = &scene->classes[se->class_index];
    if (C->var_count <= 1) return;
    /* Attribute index 1 is the FIRST stored attribute (index 0 = filter,
     * implicit). For TitleLogo / TitleBG / Title3DSprite this is `type`
     * (or `frame`), a VAR_ENUM stored as u32 in the payload. Write the
     * low byte into the Mania-side struct's first subclass field (offset
     * 132 — see comment above). */
    uint32_t v = rsdk_entity_attr_u32(scene, se, 1);
    uint8_t *p = (uint8_t *)ent + MANIA_RSDK_ENTITY_BASE_BYTES;
    /* Decomp's `int32 type` field is 4 bytes; write all 4 little-endian. */
    p[0] = (uint8_t)(v        & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16)& 0xFF);
    p[3] = (uint8_t)((v >> 24)& 0xFF);
}

/* Phase 2.4g.3 (Task #153) — multi-attribute fill for PlaneSwitch.
 *
 * fill_first_attribute (above) writes only scene attribute index 1 to the
 * first subclass field at offset +132. That suffices for the Title classes
 * and for InvisibleBlock/BoundsMarker (whose extra fields default safely in
 * Create). PlaneSwitch is different: its `size` (attribute index 2) sets the
 * collision AABB Y-window (`size << 19` in PlaneSwitch_CheckCollisions) and
 * `angle` (index 3) drives negAngle; both MUST come from the scene payload
 * or every switch is inert (size 0 -> zero-height window).
 *
 * decomp PlaneSwitch_Serialize (PlaneSwitch.c:161-167) registers, in order:
 *   idx1 flags  (VAR_ENUM,  int32)  -> EntityPlaneSwitch subclass +0  (= +132)
 *   idx2 size   (VAR_ENUM,  int32)  -> EntityPlaneSwitch subclass +4  (= +136)
 *   idx3 angle  (VAR_INT32, int32)  -> RSDK_ENTITY base `angle`  (= +32)
 *   idx4 onPath (VAR_BOOL,  bool32) -> EntityPlaneSwitch subclass +8  (= +140)
 * (negAngle is computed in Create from angle; not serialized.)
 *
 * Offsets are taken from Game.h RSDK_ENTITY (base 132, angle@32) +
 * PlaneSwitch.h struct order. Little-endian byte writes match the Saturn
 * struct in-memory layout (the int32 fields are read natively after). */
static void fill_planeswitch_attributes(const rsdk_scene_t *scene,
                                         const rsdk_scene_entity_t *se,
                                         rsdk_entity_t *ent)
{
    const rsdk_scene_class_t *C = &scene->classes[se->class_index];
    uint8_t *base = (uint8_t *)ent;

    if (C->var_count > 2) {
        uint32_t size = rsdk_entity_attr_u32(scene, se, 2);
        uint8_t *p = base + MANIA_RSDK_ENTITY_BASE_BYTES + 4; /* size @ +136 */
        p[0] = (uint8_t)(size & 0xFF);
        p[1] = (uint8_t)((size >> 8) & 0xFF);
        p[2] = (uint8_t)((size >> 16) & 0xFF);
        p[3] = (uint8_t)((size >> 24) & 0xFF);
    }
    if (C->var_count > 3) {
        uint32_t angle = rsdk_entity_attr_u32(scene, se, 3);
        uint8_t *p = base + 32; /* RSDK_ENTITY angle @ +32 */
        p[0] = (uint8_t)(angle & 0xFF);
        p[1] = (uint8_t)((angle >> 8) & 0xFF);
        p[2] = (uint8_t)((angle >> 16) & 0xFF);
        p[3] = (uint8_t)((angle >> 24) & 0xFF);
    }
    if (C->var_count > 4) {
        uint32_t onPath = rsdk_entity_attr_u32(scene, se, 4);
        uint8_t *p = base + MANIA_RSDK_ENTITY_BASE_BYTES + 8; /* onPath @ +140 */
        p[0] = (uint8_t)(onPath & 0xFF);
        p[1] = (uint8_t)((onPath >> 8) & 0xFF);
        p[2] = (uint8_t)((onPath >> 16) & 0xFF);
        p[3] = (uint8_t)((onPath >> 24) & 0xFF);
    }
}

rsdk_scene_info_t g_rsdk_scene_info;

/* Phase 1.18 — entity-driven path diagnostic counters.
 * Read via Mednafen savestate inspection or via in-game printout when
 * isolating "entity-driven path silent" failures (Phase 1.17 §11.24
 * deferred item B). Each counter is volatile so the compiler doesn't
 * elide the writes in a sealed-translation-unit build. */
/* Phase 2.4g.1 (Task #153) — `used` so GCC 8.2 whole-program LTO keeps a
 * stable named address in game.map for the savestate harness to peek when
 * diagnosing the GHZ InvisibleBlock spawn pipeline (entity_total -> parse OK;
 * class_resolved/unresolved -> hash match; create_ok -> slot fill). Same LTO
 * defeat as InvisibleBlock.c's QA landmarks. */
__attribute__((used)) volatile uint32_t g_scene_diag_entity_total = 0;
__attribute__((used)) volatile uint32_t g_scene_diag_class_resolved = 0;
__attribute__((used)) volatile uint32_t g_scene_diag_class_unresolved = 0;
__attribute__((used)) volatile uint32_t g_scene_diag_create_ok = 0;
__attribute__((used)) volatile uint32_t g_scene_diag_create_fail = 0;
volatile uint32_t g_scene_diag_titlelogo_created = 0;
volatile uint32_t g_scene_diag_titlesonic_created = 0;
volatile uint32_t g_scene_diag_titlesetup_created = 0;

static char         s_queued_category[32];
static char         s_queued_name[32];
static char         s_current_category[32];
static char         s_current_name[32];

static rsdk_scene_t s_current_scene;
static bool         s_have_scene = false;

void rsdk_scene_init(void)
{
    memset(&g_rsdk_scene_info, 0, sizeof(g_rsdk_scene_info));
    s_queued_category[0] = 0;
    s_queued_name[0]     = 0;
    s_current_category[0] = 0;
    s_current_name[0]     = 0;
    memset(&s_current_scene, 0, sizeof(s_current_scene));
    s_have_scene = false;
}

static void copy_short_name(char *dst, size_t cap, const char *src)
{
    size_t i;
    if (!src) { dst[0] = 0; return; }
    for (i = 0; i + 1 < cap && src[i]; ++i) dst[i] = src[i];
    dst[i] = 0;
}

void rsdk_set_scene(const char *category, const char *name)
{
    copy_short_name(s_queued_category, sizeof(s_queued_category), category);
    copy_short_name(s_queued_name,     sizeof(s_queued_name),     name);
}

void rsdk_set_engine_state(uint8_t state)
{
    g_rsdk_scene_info.state = state;
}

const rsdk_scene_t *rsdk_current_scene(void)
{
    return s_have_scene ? &s_current_scene : NULL;
}

void rsdk_unload_scene(void)
{
    if (s_have_scene) {
        rsdk_scene_free(&s_current_scene);
        memset(&s_current_scene, 0, sizeof(s_current_scene));
        s_have_scene = false;
    }
}

bool rsdk_check_scene_folder(const char *folder_name)
{
    if (!folder_name) return false;
    /* String-compare against s_current_category (with case sensitivity). */
    for (int i = 0; i < (int)sizeof(s_current_category); ++i) {
        if (folder_name[i] != s_current_category[i]) return false;
        if (!folder_name[i]) return true;
    }
    return false;
}

/* Compose the Saturn-side scene filename.
 *
 * Saturn ISO9660 lookup uses GFS_NameToId on a flat directory by 8.3
 * uppercase name. The mkisofs `-full-iso9660-filenames` flag allows longer
 * names but jo's GFS_NameToId is keyed off the iso's flat name table; the
 * cd/ directory is flat (no subdirectories).
 *
 * Phase 3.2 — extend the scene -> cd/ filename map.  Previously
 * hard-coded to "SCENE1.BIN" (Title only); the new map dispatches by
 * scene name so rsdk_load_scene("Menu") resolves to MENUSCN1.BIN.
 *
 * Mapping table (8.3 ISO9660 names, ALL UPPERCASE):
 *   "Title" / NULL    -> "SCENE1.BIN"      (legacy / default; Title scene)
 *   "Menu"            -> "MENUSCN1.BIN"
 *   "Logos"           -> "LOGOSCN1.BIN"    (reserved for Phase 3.1b)
 *   "GHZ"             -> "GHZSCN1.BIN"     (reserved for Phase 4 scene-load)
 *
 * Any name not in the table falls back to the legacy "SCENE1.BIN" so
 * existing Title-scene boot path is unaffected.
 *
 * The corresponding cd/ files are produced by the offline build pipeline
 * (build.bat copies extracted/Data/Stages/<Folder>/Scene1.bin to the cd/
 * 8.3 name).  Missing cd/ files cause rsdk_scene_load to return false;
 * the caller decides whether that's fatal or a graceful no-op. */
static int sat_eq_ci(const char *a, const char *b)
{
    if (!a || !b) return 0;
    int i;
    for (i = 0; a[i] && b[i]; ++i) {
        char ca = a[i]; char cb = b[i];
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
        if (ca != cb) return 0;
    }
    return a[i] == 0 && b[i] == 0;
}

static void build_scene_path(char *out_buf, size_t cap,
                             const char *category, const char *name)
{
    (void)category;
    if (cap == 0) return;
    const char *p = "SCENE1.BIN";   /* default: Title / legacy */
    if (name && name[0]) {
        if      (sat_eq_ci(name, "Menu"))  p = "MENUSCN1.BIN";
        else if (sat_eq_ci(name, "Logos")) p = "LOGOSCN1.BIN";
        else if (sat_eq_ci(name, "GHZ"))   p = "GHZSCN1.BIN";
        /* "Title" and everything else fall through to SCENE1.BIN. */
    }
    size_t i;
    for (i = 0; p[i] && i + 1 < cap; ++i) out_buf[i] = p[i];
    out_buf[i] = 0;
}

/* Phase 2.4g.1 (Task #153) — factored scene-load core.
 *
 * `run_stage_load` controls the all-class stage_load pass at the end:
 *   - true  (Title / Menu via rsdk_load_scene / _by_name): unchanged
 *     legacy behaviour — every registered class's StageLoad fires. This
 *     keeps the stabilized Title boot path bit-identical (zero regression).
 *   - false (GHZ via rsdk_load_scene_no_stage_load): SKIP the all-class
 *     loop. GHZ owns an explicit, ordered StageLoad chain (GHZSetup_StageLoad
 *     in mania_load_ghz_scene); running the all-class loop here would
 *     additionally fire all five Title* StageLoads (re-loading freed title
 *     assets) plus GHZSetup_StageLoad a second time. RSDKv5 only fires
 *     StageLoad for GameConfig globals + the current StageConfig.bin objects
 *     list (Object.cpp ProcessStage) — the Saturn all-class loop is a known
 *     divergence; proper StageConfig.bin scoping is deferred to a later task. */
static bool rsdk_load_scene_impl(bool run_stage_load)
{
    if (!s_queued_name[0]) return false;

    /* Drop any previously-loaded scene. */
    rsdk_unload_scene();

    /* Phase 2.4g.1 — clear stale per-scene + temp entity slots from the
     * previous scene so they don't tick/draw in this one (rsdk_unload_scene
     * frees the parsed blob but not the BSS slot table). */
    rsdk_object_clear_scene_slots();

    char path[96];
    build_scene_path(path, sizeof(path),
                     s_queued_category, s_queued_name);

    /* Phase 2.4g.1 — GHZ Scene1.bin (85 KB, 1041 entities) does not fit
     * jo's 256 KB pool residue at stage-active time (measured: jo_fs_read_
     * file returned NULL). Route its file read + entity tables through the
     * LWRAM scene arena per binding rule memory/ghz-sky-dat-lwram-bypass.md.
     * The tiny Title scene (SCENE1.BIN, ~2.5 KB) stays on jo's pool. */
    bool use_lwram = (path[0] == 'G' && path[1] == 'H' && path[2] == 'Z');
    rsdk_scene_set_lwram_mode(use_lwram);

    if (!rsdk_scene_load(path, &s_current_scene)) {
        /* Path may be invalid; surface as failure so the boot stub can
         * decide. Phase 1.2 will add more candidate paths. */
        rsdk_scene_set_lwram_mode(false);
        return false;
    }
    rsdk_scene_set_lwram_mode(false);
    s_have_scene = true;
    copy_short_name(s_current_category, sizeof(s_current_category),
                    s_queued_category[0] ? s_queued_category : s_queued_name);
    copy_short_name(s_current_name, sizeof(s_current_name), s_queued_name);

    /* Register every tile layer with the tilelayer module. Phase 1.1
     * Title scene has 0..N layers; per docs/title_ground_truth.md the
     * Title scene uses layers 0+1+2 for parallax sky and 3 for the BG
     * sprite plane. We register them all unconditionally — empty layers
     * stay with layout==NULL and GetTileLayer returns NULL for those. */
    for (int i = 0; i < s_current_scene.layer_count; ++i) {
        rsdk_tilelayer_register(&s_current_scene.layers[i]);
    }

    /* Per-entity create pass. For each entity in the scene's flat list:
     *   1. Look up the class index from class hash via
     *      rsdk_object_find_class on the matching class's stored name
     *      (storage.c resolves class names post-hash via the post-load
     *      pass; if name is empty we fall back to scanning the registry
     *      by hash). For Phase 1.1 only Title classes are registered;
     *      missing-class entities silently skip.
     *   2. Call rsdk_create_entity with the parsed pos_x, pos_y.
     *
     * Decomp source: _RSDKv5_Scene.cpp:670-720 InitObjects + per-entity
     * create call. The upstream's variable-attribute fill-in
     * (entity->varN from scene_attrib payload) is deferred to Phase 1.2 —
     * Title classes don't currently need it, only TitleSetup creates
     * itself with all-defaults via TitleSetup_Create's NULL data arg. */
    g_scene_diag_entity_total      = (uint32_t)s_current_scene.entity_count;
    g_scene_diag_class_resolved    = 0;
    g_scene_diag_class_unresolved  = 0;
    g_scene_diag_create_ok         = 0;
    g_scene_diag_create_fail       = 0;
    g_scene_diag_titlelogo_created = 0;
    g_scene_diag_titlesonic_created= 0;
    g_scene_diag_titlesetup_created= 0;

    /* Phase 2.4g.1 (Task #153) — sparse-slot compaction cursor. See the
     * overflow branch below. */
    int compact_cursor = 0;

    for (int i = 0; i < (int)s_current_scene.entity_count; ++i) {
        const rsdk_scene_entity_t *se = &s_current_scene.entities[i];
        if (se->class_index >= s_current_scene.class_count) continue;
        const rsdk_scene_class_t *sc = &s_current_scene.classes[se->class_index];
        int class_id = -1;
        if (sc->name[0]) {
            class_id = rsdk_object_find_class(sc->name);
        } else {
            /* Name was not resolved by storage.c — scan registry by hash. */
            for (int k = 0; k < (int)g_rsdk_class_count; ++k) {
                if (memcmp(g_rsdk_classes[k].hash, sc->hash, 16) == 0) {
                    class_id = k;
                    break;
                }
            }
        }
        if (class_id < 0) {
            ++g_scene_diag_class_unresolved;
            continue;
        }
        ++g_scene_diag_class_resolved;
        /* Phase 1.18 — per-class create count. Names checked verbatim
         * against the registry's stored name; this is a pure diagnostic
         * so it can lag the canonical mapping by one frame on miss. */
        {
            const char *cn = g_rsdk_classes[class_id].name;
            if (cn[0] == 'T' && cn[1] == 'i' && cn[2] == 't' && cn[3] == 'l' && cn[4] == 'e') {
                if (cn[5] == 'L') ++g_scene_diag_titlelogo_created;
                else if (cn[5] == 'S' && cn[6] == 'o') ++g_scene_diag_titlesonic_created;
                else if (cn[5] == 'S' && cn[6] == 'e') ++g_scene_diag_titlesetup_created;
            }
        }
        /* Phase 1.22 fix (§11.28) — place the entity at the scene-bin
         * slot, NOT the temp-ring head.  Decomp _RSDKv5_Scene.cpp:528-537:
         *   if (slotID < SCENEENTITY_COUNT)
         *       entity = &objectEntityList[slotID + RESERVE_ENTITY_COUNT];
         *   else
         *       entity = &tempEntityList[slotID - SCENEENTITY_COUNT];
         * The previous Saturn code called rsdk_create_entity (which
         * advances g_rsdk_create_slot through the 16-slot temp ring) so
         * 76 scene entities round-robin'd through 16 slots -- only the
         * last 16 (all Title3DSprite) survived and every TitleLogo /
         * TitleSonic / TitleBG entity was silently overwritten. */
        uint16_t target_slot;
        if (se->slot < RSDK_SCENEENTITY_COUNT) {
            target_slot = (uint16_t)(se->slot + RSDK_RESERVE_ENTITY_COUNT);
        } else {
            uint16_t off = (uint16_t)(se->slot - RSDK_SCENEENTITY_COUNT);
            if (off >= RSDK_TEMPENTITY_COUNT) {
                /* Phase 2.4g.1 (Task #153) — sparse-slot compaction.
                 * GHZ Scene1.bin packs its entities at sparse slot IDs far
                 * beyond the Saturn temp-entity budget (the 18 resolved
                 * InvisibleBlocks live at slots 423..1020). The prior code
                 * silently dropped every such entity, so no GHZ entity ever
                 * spawned. We compact resolved overflow entities into the
                 * temp ring with a monotonic per-load cursor.
                 *
                 * Scope note: for 2.4g.1 only InvisibleBlock is a registered
                 * GHZ class, so every entity reaching this branch is an
                 * overflow slot and the direct (off < TEMPENTITY_COUNT) temp
                 * mapping below is unused for GHZ — the cursor cannot collide
                 * with a directly-mapped temp slot. When a later increment
                 * registers GHZ classes that land in the off<32 temp range
                 * CONCURRENTLY with overflow slots, this cursor must start
                 * above the highest directly-mapped temp slot (revisit then). */
                if (compact_cursor >= RSDK_TEMPENTITY_COUNT) {
                    ++g_scene_diag_create_fail;
                    continue;
                }
                target_slot = (uint16_t)(RSDK_TEMPENTITY_START + compact_cursor);
                ++compact_cursor;
            } else {
                target_slot = (uint16_t)(RSDK_TEMPENTITY_START + off);
            }
        }
        rsdk_reset_entity_slot(target_slot, (uint16_t)class_id, NULL);
        rsdk_entity_t *ent = rsdk_entity_at(target_slot);
        /* rsdk_reset_entity_slot clears + invokes Create; preserves
         * position only if Create accidentally zeros it.  Manually set
         * the scene-bin position before re-invoking Create with the
         * filled-in type attribute below. */
        if (ent) {
            ent->position.x = se->pos_x;
            ent->position.y = se->pos_y;
            ++g_scene_diag_create_ok;
        } else {
            ++g_scene_diag_create_fail;
        }
        if (ent) {
            /* Phase 1.2: fill in the first per-class attribute (TitleLogo
             * `type`, TitleBG `type`, Title3DSprite `frame`) from the
             * scene-bin payload BEFORE re-invoking Create with the typed
             * default. We rewrite the byte then re-run Create so the
             * type-switch branches resolve correctly. */
            ent->filter = se->filter;
            fill_first_attribute(&s_current_scene, se, ent);
            /* Phase 2.4g.3 — PlaneSwitch needs size/angle/onPath too (the
             * generic path only fills attribute idx 1 = flags). Keyed on the
             * registered class name so Title/InvisibleBlock/BoundsMarker
             * layouts are untouched. */
            {
                const char *cn = g_rsdk_classes[class_id].name;
                if (cn[0] == 'P' && cn[1] == 'l' && cn[2] == 'a' && cn[3] == 'n'
                    && cn[4] == 'e' && cn[5] == 'S') {
                    fill_planeswitch_attributes(&s_current_scene, se, ent);
                }
            }
            /* Phase 1.3 — Re-run Create with the now-populated type field.
             * Preserve position (the scene's pos_x/pos_y is what determines
             * where each entity actually lives — the decomp Create body
             * sometimes initialises drawPos based on position so position
             * must survive both creates). */
            int32_t saved_x = ent->position.x;
            int32_t saved_y = ent->position.y;
            rsdk_object_class_t *cls = &g_rsdk_classes[class_id];
            if (cls->create) {
                rsdk_entity_t *prev = g_rsdk_current_entity;
                g_rsdk_current_entity = ent;
                cls->create(NULL);
                g_rsdk_current_entity = prev;
            }
            /* Restore position if Create inadvertently changed it. */
            if (ent->position.x != saved_x || ent->position.y != saved_y) {
                ent->position.x = saved_x;
                ent->position.y = saved_y;
            }
        }
    }

    /* Per-class stage_load callback (mirrors Object.cpp ProcessStage).
     * Gated by run_stage_load — see rsdk_load_scene_impl header. */
    if (run_stage_load) {
        g_rsdk_current_entity      = NULL;
        g_rsdk_current_entity_slot = 0;
        for (uint16_t c = 0; c < g_rsdk_class_count; ++c) {
            rsdk_object_class_t *cls = &g_rsdk_classes[c];
            if (cls->stage_load) cls->stage_load();
        }
    }

    g_rsdk_scene_info.state = RSDK_SCENESTATE_REGULAR;
    return true;
}

bool rsdk_load_scene(void)
{
    return rsdk_load_scene_impl(true);
}

/* Phase 2.4g.1 (Task #153) — GHZ scene-entity spawn without the all-class
 * stage_load pass. mania_load_ghz_scene owns the explicit, ordered GHZ
 * StageLoad chain; this entry parses GHZSCN1.BIN and spawns its registered
 * entities (InvisibleBlock for 2.4g.1) into the slot table. */
bool rsdk_load_scene_no_stage_load(void)
{
    return rsdk_load_scene_impl(false);
}

bool rsdk_load_scene_by_name(const char *name)
{
    rsdk_set_scene(NULL, name);
    return rsdk_load_scene_impl(true);
}
