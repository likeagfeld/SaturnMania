#ifndef MANIA_GAME_H
#define MANIA_GAME_H

/* Phase 1.1 — src/mania/Game.h
 *
 * Saturn-side game header. Provides minimum-viable RSDK_OBJECT /
 * RSDK_ENTITY macros + StateMachine helpers so the per-class headers
 * in src/mania/Objects/Title/ can compile without dragging in the full
 * upstream RetroEngine.hpp.
 *
 * Decomp citation: tools/_decomp_raw/SonicMania_Game.h declares
 * `RSDK_OBJECT` + `RSDK_ENTITY` macros plus the global engine accessors.
 * Saturn-side these reduce to compact equivalents — our entity struct
 * already carries the same fields under different names (see
 * src/rsdk/object.h::rsdk_entity_t). */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>                /* NULL, size_t                        */
#include "../rsdk/object.h"        /* rsdk_entity_t, rsdk_object_class_t  */
#include "../rsdk/storage.h"       /* rsdk_animator_t                     */
#include "../rsdk/animation.h"

/* === Upstream type aliases (so decomp signatures translate 1:1) ===== */

typedef uint8_t   uint8;
typedef uint16_t  uint16;
typedef uint32_t  uint32;
typedef int8_t    int8;
typedef int16_t   int16;
typedef int32_t   int32;
typedef int32_t   bool32;

typedef struct { int32 x, y; } Vector2;
typedef rsdk_animator_t Animator;

/* StateMachine — upstream calls states-as-funcs; the macro just expands
 * to a typed function pointer. */
typedef void (*StateMachine_fn)(void);
#define StateMachine(name)   StateMachine_fn name
#define StateMachine_None    NULL
#define StateMachine_Run(fn) do { if (fn) (fn)(); } while (0)

/* === RSDK_OBJECT / RSDK_ENTITY (mirrors upstream macro contracts) ==
 *
 * Upstream's RSDK_OBJECT block declares the class header common fields;
 * RSDK_ENTITY likewise for entity structs. We match the field NAMES so
 * decomp `self->position.x`, `self->velocity.y`, `self->state` etc.
 * resolve correctly. Our rsdk_entity_t already lays out position +
 * velocity + scale + update_range + ... at the same offsets; here we
 * just expose them via the decomp-spelling field names via a union-like
 * shadow struct. */

#define RSDK_OBJECT                                  \
    uint16 classID;                                  \
    uint8  active;

/* The decomp Entity struct begins with these fields. We replicate the
 * layout precisely so per-class Entity* types alias rsdk_entity_t at
 * offset 0. The Saturn rsdk_entity_t already has all of them; this
 * macro is just the decomp-spelling for ports.
 *
 * Phase 1.17 CRITICAL FIX: The Saturn-side rsdk_entity_t (object.h:80-
 * 107) uses u8 fields for inRange/isPermanent/tileCollisions/interaction/
 * onGround/active starting at offset 56. The original RSDK_ENTITY macro
 * here used bool32 (int32) for inRange/isPermanent/interaction/onGround,
 * which pushed `active` to offset 76 — wildly out of sync with the
 * rsdk_entity_t's `active` at offset 61. That meant rsdk_object_tick
 * wrote ACTIVE_NORMAL to offset 61 (junk u8 of Mania's isPermanent),
 * while Mania's State_FlashIn wrote ACTIVE_NORMAL to offset 76 (a
 * different junk byte that rsdk_object_draw_all then never read). Net
 * effect: NO TitleLogo entity ever rendered because the active flag
 * mismatch broke the draw-pass active!=NEVER gate. Bringing the macro
 * into 1:1 byte-offset alignment with rsdk_entity_t fixes this. The
 * decomp's per-class subclass fields (which begin AFTER this macro)
 * still align at 132 bytes — see Phase 1.2 scene.c::MANIA_RSDK_ENTITY_
 * BASE_BYTES; we pad to that offset with reserved bytes to keep that
 * contract. */
#define RSDK_ENTITY                                  \
    Vector2 position;            /* +0..7   */       \
    Vector2 scale;               /* +8..15  */       \
    Vector2 velocity;            /* +16..23 */       \
    Vector2 updateRange;         /* +24..31 */       \
    int32   angle;               /* +32..35 */       \
    int32   alpha;               /* +36..39 */       \
    int32   rotation;            /* +40..43 */       \
    int32   groundVel;           /* +44..47 */       \
    int32   zdepth;              /* +48..51 */       \
    uint16  group;               /* +52..53 */       \
    uint16  classID;             /* +54..55 */       \
    uint8   inRange;             /* +56    (was bool32) */ \
    uint8   isPermanent;         /* +57    (was bool32) */ \
    uint8   tileCollisions;      /* +58    (was int8, kept u8 for offsets) */ \
    uint8   interaction;         /* +59    (was bool32) */ \
    uint8   onGround;            /* +60    (was bool32) */ \
    uint8   active;              /* +61    matches rsdk_entity_t */ \
    uint8   filter;              /* +62    matches rsdk_entity_t */ \
    uint8   direction;           /* +63    matches rsdk_entity_t */ \
    /* Saturn-side subclass-area extension. rsdk_entity_t ends at +63;    \
     * Mania-side appends drawGroup/collisionLayers/...onScreen here.     \
     * We pad to offset 132 (MANIA_RSDK_ENTITY_BASE_BYTES in scene.c)     \
     * so each Entity subclass's first field lands at +132 as scene.c     \
     * expects when it writes the parsed scene-attribute value. */        \
    uint8   drawGroup;           /* +64 */           \
    uint8   collisionLayers;     /* +65 */           \
    uint8   collisionPlane;      /* +66 */           \
    uint8   collisionMode;       /* +67 */           \
    uint8   drawFX;              /* +68 */           \
    uint8   inkEffect;           /* +69 */           \
    uint8   visible;             /* +70 */           \
    uint8   onScreen;            /* +71 */           \
    uint8   _rsdk_entity_pad[60]; /* +72..131 — reserves remaining bytes \
                                    * up to the +132 base used by scene  \
                                    * loader (fill_first_attribute). */

/* drawFX flag bits (Drawing.hpp:27-37 paraphrased). */
enum {
    FX_NONE  = 0,
    FX_FLIP  = 1,
    FX_ROTATE = 2,
    FX_SCALE = 4
};

/* Active enum aliases — match upstream spelling. */
#ifndef ACTIVE_NEVER
enum {
    ACTIVE_NEVER_ALIAS = 0
};
#endif

/* Scope-of-allocation enum (upstream Storage). */
enum {
    SCOPE_NONE  = 0,
    SCOPE_GLOBAL = 1,
    SCOPE_STAGE = 2
};

/* DRAWGROUP_COUNT alias for decomp call sites. */
#ifndef DRAWGROUP_COUNT
#define DRAWGROUP_COUNT 16
#endif

/* SCREEN_YSIZE alias (decomp Drawing.hpp). */
#ifndef SCREEN_YSIZE
#define SCREEN_YSIZE   224
#endif

/* === Mania-side ScreenInfo + SceneInfo accessors (decomp aliasing) ====
 *
 * Decomp ports reference ScreenInfo->size.x etc. We expose those via a
 * static-allocated mirror struct + a tiny accessor macro. The Saturn-side
 * src/rsdk/drawing.c owns g_rsdk_screen with parallel field names; we
 * keep both in sync via mania_engine_sync_screen() called once per frame
 * (the Saturn-side ScreenInfo mirror is what the per-class draw code
 * reads).
 *
 * Decomp uses dot-syntax (ScreenInfo->size.x); we publish the mirror so
 * the per-class port reads `mania_screen()->size.x`. The Phase 1.2 mania
 * Game.c provides this. */

typedef struct {
    Vector2 position;        /* world-coord scroll origin (16.16)   */
    Vector2 size;            /* viewport pixel size                 */
    Vector2 center;
    int32   clipBound_X1;
    int32   clipBound_Y1;
    int32   clipBound_X2;
    int32   clipBound_Y2;
    int32   waterCheck;
    int32   waterDrawPos;
} ManiaScreenInfo;

extern ManiaScreenInfo *ScreenInfo;

/* SceneInfo->inEditor + ->entity replacement. Saturn never has inEditor
 * true; we expose a permanent-false mirror. */
typedef struct {
    void *entity;
    uint8 inEditor;
    uint8 state;
} ManiaSceneInfo;

extern ManiaSceneInfo *SceneInfo;

/* === Mania-side entity extended-field accessors =====================
 *
 * Decomp expects `self->drawGroup`, `self->inkEffect`, `self->visible`,
 * `self->drawFX`, `self->alpha` to resolve directly on EntityX structs.
 * The Saturn rsdk_entity_t carries `alpha` in the base, but drawGroup /
 * inkEffect / visible / drawFX live in the Mania-side RSDK_ENTITY
 * subclass region. Since RSDK_ENTITY's field order matches the decomp's
 * Entity struct layout (verified by Game.h above), `self->drawGroup` on
 * the Mania-side struct directly reads the correct byte.
 *
 * Saturn-side per-class draw callbacks invoke `rsdk_draw_sprite_ex` with
 * these fields explicitly. */

/* === Engine-side bootstrap exposed to main.c ======================= */

void mania_engine_init(void);
void mania_tick(void);

/* Phase 2.3c — option (alpha) diagnostic probe sprite (per
 * docs/COMPREHENSIVE_PLAN.md Sec 12.3c). Registered at engine init AFTER
 * entities_load_assets; drawn each frame via TWO paths (jo wrapper and
 * direct slDispSprite) to bisect the GHZ-state VDP1 sprite suppressor. */
void mania_diag_probe_register(void);
void mania_diag_probe_draw(void);
int  mania_diag_probe_sprite_id(void);

/* Phase 2.3j: mania_ghz_deferred_audio_kick REMOVED. The CD-track switch
 * runs synchronously inside mania_load_ghz_scene under the new sync-load
 * architecture. See src/mania/Game.c Phase 2.3j comments for rationale. */

/* Phase 1.2 — Saturn-side asset bridge for the Title scene. mania_engine
 * _init loads the per-class .SPR atlases at boot (TSONIC.ATL + MLOGO.SPR
 * + MWINGS.SPR + MRIBBON.SPR + MRIBSIDE.SPR + MRING.SPR + MPRESS.SPR)
 * and exposes per-(list_id, anim_id, frame_id) draw operations through
 * the rsdk_drawing.h sprite callback. */

void mania_screen_sync(void);   /* called per-frame from mania_tick */

/* Phase 2.2 — Player + Sonic sprite + camera-follow access for main.c.
 *
 * Game.c owns the Player runtime (since the title→GHZ transition state
 * machine is here). main.c calls these only to expose the per-frame Sonic
 * draw + camera math through the jo_core_run loop. */
void mania_ghz_player_load_assets(void);   /* idempotent; lazy on transition */
void mania_ghz_player_preload_world(void); /* load GHZ1SURF.BIN BEFORE FG.TMP */
void mania_ghz_player_init_on_transition(void); /* called from title→GHZ transition */
void mania_ghz_tick_and_draw(void);        /* called per-frame when GHZ active */

/* Phase 2.3j — synchronous scene-load API (replaces async readiness gate).
 *
 * mania_load_ghz_scene() mirrors the decomp's
 * RetroEngine.cpp:345-384 ENGINESTATE_LOAD chain (LoadSceneFolder ->
 * LoadSceneAssets -> InitObjects -> first ProcessObjects). Called once
 * from TS_TRANSITION_TO_GHZ. Blocks the caller for the full GHZ asset
 * load (~one dropped frame; matches decomp behavior).
 *
 * mania_is_ghz_active() replaces the removed ghz_is_active() helper.
 * Returns true once mania_load_ghz_scene has completed (i.e. the title
 * state machine has advanced to TS_GHZ_ACTIVE).
 *
 * Phase 2.3k-mid (2026-05-28): mania_load_ghz_scene returns bool — false
 * if GHZSetup_StageLoad's sub-asset load (FG.* / SKY.*) failed. Caller
 * must NOT advance the state machine on false return. The bit-precise
 * failure is recorded in g_ghz_load_error_code (scene_ghz.h). */
bool mania_load_ghz_scene(void);
bool mania_is_ghz_active(void);

/* Phase 2.3e — Path-4 empirical re-routing of GHZ sprite draws.
 *
 * §12.3d empirically established that any sprite draw issued from
 * inside the `if (ghz_is_active()) { ... }` block of mania_tick is
 * silently dropped, while the SAME sprite ID drawn from
 * mania_diag_probe_draw (called at mania_tick body scope) renders
 * fine. Path 4 (§12.3e) is the lowest-effort empirical workaround:
 * factor the sprite-DRAW portion of mania_ghz_tick_and_draw into
 * mania_ghz_draw_only and call it from mania_tick at body scope,
 * AFTER mania_diag_probe_draw but OUTSIDE any conditional. The
 * physics+camera+page-build portion stays inside the if-block
 * (its side effects don't depend on call-site context).
 *
 * Internally gated by ghz_is_active() AND g_ghz_player_ready; safe
 * to call unconditionally per-frame (no-op until ready). */
void mania_ghz_draw_only(void);

/* ===== Decomp helpers commonly used in callback bodies =============== */

/* Music_PlayTrack — decomp uses this; on Saturn maps to a CD-DA track. */
enum {
    TRACK_STAGE = 0,        /* default zone music slot                */
    TRACK_MENU  = 1,
    TRACK_INVINCIBLE = 2
};

void Music_PlayTrack(uint8 trackID);
void Music_Stop(void);

/* destroyEntity / foreach_all decomp idioms — minimal Saturn impls. */
#define destroyEntity(ent)                                              \
    do { if ((ent)) {                                                   \
        ((rsdk_entity_t *)(ent))->active   = ACTIVE_NEVER;              \
        ((rsdk_entity_t *)(ent))->class_id = 0;                         \
    } } while (0)

/* foreach_all: walks every entity of class `klass` regardless of active
 * state (mirrors upstream Object.cpp:1218-1263 + macro definition). */
#define foreach_all(klass, var_name)                                    \
    for (int _fea_cur_##klass = -1;                                     \
         (var_name = (Entity##klass *)rsdk_get_all_entity(              \
             (uint16_t)rsdk_object_find_class(#klass),                  \
             &_fea_cur_##klass)) != NULL; )

/* Plus-only paths used by the decomp's #ifdef MANIA_USE_PLUS gates —
 * we always undefine on the Saturn build. */
#undef MANIA_USE_PLUS

#endif /* MANIA_GAME_H */
