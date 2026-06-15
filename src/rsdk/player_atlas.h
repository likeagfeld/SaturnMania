#ifndef RSDK_PLAYER_ATLAS_H
#define RSDK_PLAYER_ATLAS_H

/* FR-1 (Task #177) -- Sonic player animation atlas with MRU residency.
 *
 * WHY a dedicated atlas (not entity_atlas):
 *   The gameplay-reachable Sonic keep set is 15 anims / 149 frames /
 *   ~343 KB of BGR1555 pixels (cd/SONIC.SP2). That blows past every
 *   entity_atlas bound: ENTITY_ATLAS_MAX_FRAMES=40, the JO_MAX_SPRITE=255
 *   VDP1 slot ceiling shared with ~14 live entity atlases, and the 512 KB
 *   VDP1 VRAM budget (the GHZ entity SP2 set already pins ~480 KB). All 149
 *   frames cannot be VDP1-resident at once.
 *
 * RESIDENCY MODEL (user-chosen "MRU pool + CD slices", 2026-06-01):
 *   - SINGLE-ANIM VDP1 RESIDENCY. Only the current anim's frames (<=16,
 *     <=35.5 KB -- Air Walk bounds it) live in VDP1 VRAM. On an anim change
 *     we jo_sprite_free_from(player_base) (jo LIFO rewind, sprites.c:290)
 *     then jo_sprite_add the new anim's frames. The player therefore MUST
 *     own the TOP of the jo sprite stack: capture the base id AFTER every
 *     entity atlas has uploaded, and nothing else may jo_sprite_add during
 *     gameplay (entity atlases upload once at load and draw via existing
 *     ids -- verified safe).
 *   - 192 KB LWRAM MRU PIXEL POOL at 0x002D0000 (the only free LWRAM region
 *     during GHZ gameplay; 0x290000-0x2CFFFF holds the live 1041-entity
 *     scene table, storage.c:221). The pool caches whole per-anim slice
 *     files (SONIC00.SP2..SONIC14.SP2) so re-entering a recently-played
 *     anim costs no CD read. Bump-allocated; a wholesale flush when the
 *     next slice cannot fit (MRU: recently-used stay until the pool fills).
 *   - PER-ANIM CD SLICES. tools/build_entity_atlas.py emits one standalone
 *     SPR2 per kept anim (SONICnn.SP2). A cache miss CD-reads exactly that
 *     one slice (<=35.5 KB). All 15 anims stay reachable; only the hot
 *     working set stays resident. No anim is permanently dropped.
 *
 * Per-anim cadence metadata (frame_count/speed/loop + per-frame duration/
 * pivot/size) comes from cd/SONIC.MET, loaded ONCE in player_atlas_load and
 * held in BSS. The slice SP2 carries only pixels + width/height.
 *
 * Authoritative sources:
 *   - rsdkv5-src/RSDKv5/RSDK/Graphics/Animation.cpp:150-177 ProcessAnimation
 *     (the per-frame-duration walker; mirrored by rsdk_process_animation,
 *     animation.c:96-118).
 *   - tools/build_entity_atlas.py SPR2 + MET1 byte layout + the slice writer.
 *   - jo-engine/jo_engine/sprites.c:290 jo_sprite_free_from (LIFO rewind),
 *     :225 jo_sprite_add, :92 jo_get_last_sprite_id.
 *   - memory/entity-atlas-must-ship-all-frames.md (ship ALL frames of every
 *     KEPT anim; drop only unreachable anims, each with cited rationale).
 *
 * P4-peek header layout (qa_fr1_parity_gate.py): the leading fields mirror
 * entity_atlas_t so the runtime savestate check can read current_anim
 * (+0x04) and current_atlas_frame (+0x06) directly. */

#include "animation.h"
#include "storage.h"

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 15 kept anims; 16 headroom. */
#define PLAYER_ATLAS_MAX_ANIMS        16
/* 149 kept frames across all anims; 160 headroom (per-frame metadata). */
#define PLAYER_ATLAS_MAX_FRAMES       160
/* Max frames within any single anim (Jump/Spindash/Dropdash = 16). Bounds
 * the VDP1-resident sprite-id table + the scratch animator-frame table. */
#define PLAYER_ATLAS_MAX_ANIM_FRAMES  16

/* Sentinel for player_atlas_play(): use the MET per-anim default speed. */
#define PLAYER_ATLAS_SPEED_DEFAULT    (-1)

typedef struct player_atlas_s {
    /* P4-peek-accessible header (DO NOT REORDER). */
    uint8_t  ready;                    /* +0x00 */
    uint8_t  anim_count;               /* +0x01 */
    uint16_t frame_total;              /* +0x02  total flat frames (149)     */
    uint16_t current_anim;             /* +0x04  active ANI_ id              */
    uint16_t current_atlas_frame;      /* +0x06  flat index (first+frame_id) */

    /* Decomp animator (rsdk_process_animation drives this). */
    rsdk_animator_t animator;

    /* Per-anim cadence (from cd/SONIC.MET, all 15 anims). */
    struct {
        uint16_t frame_count;
        uint16_t speed;
        uint16_t loop_index;
        uint16_t first;                /* flat index of this anim's frame 0 */
    } anims[PLAYER_ATLAS_MAX_ANIMS];

    /* Per-frame metadata, atlas-flat (from cd/SONIC.MET, all 149 frames). */
    int16_t  pivot_x[PLAYER_ATLAS_MAX_FRAMES];
    int16_t  pivot_y[PLAYER_ATLAS_MAX_FRAMES];
    uint16_t duration[PLAYER_ATLAS_MAX_FRAMES];
    uint16_t width[PLAYER_ATLAS_MAX_FRAMES];
    uint16_t height[PLAYER_ATLAS_MAX_FRAMES];

    /* VDP1 sprite ids for the CURRENTLY resident anim only (indexed by
     * anim-local frame_id 0..frame_count-1). -1 = not uploaded. */
    int16_t  sprite_id[PLAYER_ATLAS_MAX_ANIM_FRAMES];

    /* rsdk_sprite_frame_t slice the walker reads durations from (rebuilt
     * for the current anim on each play; anim-local indexing). */
    rsdk_sprite_frame_t scratch_frames[PLAYER_ATLAS_MAX_ANIM_FRAMES];
} player_atlas_t;

/* === Public API ====================================================== */

/* Load cd/SONIC.MET metadata for all 15 anims (NO pixels uploaded yet;
 * those stream per-anim on the first player_atlas_play of each). Returns
 * true on success; on failure atlas->ready stays false and every helper
 * no-ops. */
bool player_atlas_load(player_atlas_t *atlas);

/* Load the resident compressed pack cd/SONIC.SPC into LWRAM ONCE at scene-load
 * time (CD read permitted here). After this, every per-anim slice is puff-
 * inflated RAM->RAM with zero CD access, so the single CD head stays on the
 * GHZ CD-DA music track during gameplay (Task #180 step 5). Returns true on a
 * valid 'SPC1' load. Must be called before the first player_atlas_play. */
bool player_atlas_pack_load(void);

/* Capture the jo sprite-stack base the player owns. Call ONCE, after every
 * entity atlas has uploaded its frames and before the first player_atlas_
 * play. `base_id` is the next sprite id that jo_sprite_add will return,
 * i.e. jo_get_last_sprite_id()+1 at capture time. */
void player_atlas_set_base(int base_id);

/* Current jo sprite-stack base the player owns, or -1 before set_base.
 * Exposed so the HUD diag can verify the player rewind (jo_sprite_free_from)
 * never crosses below the HUD's sprite ids. */
int player_atlas_base(void);

/* FR-2: the sprite id just ABOVE the player's currently-resident block,
 * i.e. base + (current anim's resident frame count). This is where the
 * per-tick lazy entity/HUD/titlecard dynamic block begins; the GHZ draw
 * pass passes it to entity_residency_begin_frame() each tick. Returns -1
 * before set_base, or base when no anim has uploaded yet. */
int player_atlas_top(void);

/* Switch to `anim_id` (an ANI_ value, == MET anim index for the keep set).
 * On a pool cache miss the slice SONICnn.SP2 is CD-read into the MRU pool;
 * then the previous anim's VDP1 sprites are freed and the new anim's frames
 * uploaded. `speed_override` >= 0 sets animator.speed explicitly (e.g.
 * spindash charge ramp); PLAYER_ATLAS_SPEED_DEFAULT uses the MET default.
 * No-op if anim_id is out of range or already current with the same speed. */
void player_atlas_play(player_atlas_t *atlas, int anim_id, int speed_override);

/* Advance the animator by one game tick (rsdk_process_animation walker,
 * per-frame durations). Updates current_atlas_frame for the gate peek and
 * the draw path. */
void player_atlas_tick(player_atlas_t *atlas);

/* jo sprite id for the current frame (-1 if not ready). */
int  player_atlas_current_sprite(const player_atlas_t *atlas);

/* Pivot (signed px) + size (px) of the current frame. */
void player_atlas_pivot(const player_atlas_t *atlas, int *out_px, int *out_py);
void player_atlas_size(const player_atlas_t *atlas, int *out_w, int *out_h);

/* Anim-local frame_id (0..frame_count-1) of the current frame. */
int  player_atlas_current_frame_id(const player_atlas_t *atlas);

/* === BSS global (P4 gate locates it via the .data anchor below). ===== */
extern player_atlas_t g_player_atlas;

#ifdef __cplusplus
}
#endif

#endif /* RSDK_PLAYER_ATLAS_H */
